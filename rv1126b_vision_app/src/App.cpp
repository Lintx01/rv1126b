#include "App.hpp"

#include "FramePool.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace rv1126b {

namespace {

std::atomic<bool> g_signal_exit_requested{false};

void handleExitSignal(int) {
    g_signal_exit_requested = true;
}

uint64_t elapsedUs(const std::chrono::steady_clock::time_point& begin) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - begin)
            .count());
}

int64_t steadyMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

struct MqttRuntimeState {
    std::mutex mutex;
    AppState latest_app_state{};
    bool has_app_state{false};
    int64_t last_status_publish_ms{0};
    int64_t last_telemetry_publish_ms{0};
    int64_t last_bad_posture_event_ms{0};
    int64_t last_drink_remind_event_ms{0};
};

MqttRuntimeState g_mqtt_runtime_state;

void resetMqttRuntimeState() {
    std::lock_guard<std::mutex> lock(g_mqtt_runtime_state.mutex);
    g_mqtt_runtime_state.latest_app_state = AppState{};
    g_mqtt_runtime_state.latest_app_state.device_mode = DeviceMode::STANDBY;
    g_mqtt_runtime_state.latest_app_state.posture_state = PostureState::UNKNOWN;
    g_mqtt_runtime_state.latest_app_state.drink_state = DrinkState::NORMAL;
    g_mqtt_runtime_state.has_app_state = true;
    g_mqtt_runtime_state.last_status_publish_ms = 0;
    g_mqtt_runtime_state.last_telemetry_publish_ms = 0;
    g_mqtt_runtime_state.last_bad_posture_event_ms = 0;
    g_mqtt_runtime_state.last_drink_remind_event_ms = 0;
}

void updateLatestMqttAppState(const AppState& state) {
    std::lock_guard<std::mutex> lock(g_mqtt_runtime_state.mutex);
    g_mqtt_runtime_state.latest_app_state = state;
    g_mqtt_runtime_state.has_app_state = true;
}

std::string mqttTopic(const AppConfig& config, const char* leaf) {
    return config.mqtt_base_topic + "/" + leaf;
}

const char* mqttStateString(const AppState& state) {
    if (state.display_face == DisplayFace::ERROR_FACE) {
        return "error";
    }
    return state.device_mode == DeviceMode::STANDBY ? "idle" : "running";
}

int64_t unixTimestampSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string buildStatusPayload(const AppState& state) {
    const std::string gesture = state.gesture_triggered && !state.gesture_name.empty()
                                    ? state.gesture_name
                                    : "none";
    std::ostringstream oss;
    oss << "{"
        << "\"device\":\"rv1126b\","
        << "\"state\":\"" << mqttStateString(state) << "\","
        << "\"posture\":\"" << toString(state.posture_state) << "\","
        << "\"drink\":\"" << toString(state.drink_state) << "\","
        << "\"gesture\":\"" << jsonEscape(gesture) << "\","
        << "\"timestamp\":" << unixTimestampSeconds()
        << "}";
    return oss.str();
}

std::string buildEventPayload(const std::string& event_name, const std::string& message) {
    std::ostringstream oss;
    oss << "{"
        << "\"event\":\"" << jsonEscape(event_name) << "\","
        << "\"message\":\"" << jsonEscape(message) << "\","
        << "\"timestamp\":" << unixTimestampSeconds()
        << "}";
    return oss.str();
}

std::string buildTelemetryPayload(const AppConfig& config) {
    std::ostringstream oss;
    oss << "{"
        << "\"fps\":0,"
        << "\"ai_fps\":0,"
        << "\"encoder\":\"" << (config.enable_mpp_encoder ? "mpp_h264" : "disabled") << "\","
        << "\"stream\":\"rtsp\","
        << "\"http_flv_port\":" << config.web_server_port << ","
        << "\"rtsp_port\":8554"
        << "}";
    return oss.str();
}

std::string scheduledModelList(const AiScheduleDecision& decision) {
    std::vector<std::string> models;
    if (decision.run_gesture) {
        models.emplace_back("gesture(手势)");
    }
    if (decision.run_pose) {
        models.emplace_back("pose(姿态)");
    }
    if (decision.run_cup) {
        models.emplace_back("cup(饮品)");
    }

    if (models.empty()) {
        return "none";
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < models.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << models[i];
    }
    return oss.str();
}

void logStateTransition(SystemState from, SystemState to, const char* reason) {
    std::cout << "\n"
              << "==================== [状态机转移] ====================\n"
              << "状态: " << toChineseString(from) << "(" << toString(from) << ")"
              << " -> " << toChineseString(to) << "(" << toString(to) << ")\n"
              << "原因: " << (reason == nullptr ? "unknown" : reason) << "\n"
              << "======================================================\n";
}

const char* postureStateLabel(PostureState state) {
    switch (state) {
        case PostureState::Unknown:
            return "未知";
        case PostureState::Good:
            return "正常";
        case PostureState::BadPending:
            return "疑似异常";
        case PostureState::BadAlert:
            return "坐姿异常";
    }
    return "未知";
}

const char* drinkStateLabel(DrinkState state) {
    switch (state) {
        case DrinkState::Normal:
            return "正常";
        case DrinkState::NeedRemind:
            return "需要提醒";
        case DrinkState::DrinkDetected:
            return "检测到喝水";
    }
    return "未知";
}

bool shouldPublishCooldownEvent(const AppConfig& config, const std::string& event_name) {
    const int64_t now_ms = steadyMs();
    const int64_t cooldown_ms = std::max<int64_t>(0, config.mqtt_event_cooldown_ms);
    std::lock_guard<std::mutex> lock(g_mqtt_runtime_state.mutex);

    int64_t* last_publish_ms = nullptr;
    if (event_name == "bad_posture") {
        last_publish_ms = &g_mqtt_runtime_state.last_bad_posture_event_ms;
    } else if (event_name == "drink_remind") {
        last_publish_ms = &g_mqtt_runtime_state.last_drink_remind_event_ms;
    } else {
        return true;
    }

    if (cooldown_ms > 0 && *last_publish_ms != 0 && (now_ms - *last_publish_ms) < cooldown_ms) {
        return false;
    }

    *last_publish_ms = now_ms;
    return true;
}

void collectPeriodicMqttMessages(const AppConfig& config, std::vector<MqttMessage>& messages) {
    const int64_t now_ms = steadyMs();
    std::lock_guard<std::mutex> lock(g_mqtt_runtime_state.mutex);
    if (!g_mqtt_runtime_state.has_app_state) {
        return;
    }

    const int64_t status_interval_ms = std::max<int64_t>(200, config.mqtt_status_interval_ms);
    if (g_mqtt_runtime_state.last_status_publish_ms == 0 ||
        (now_ms - g_mqtt_runtime_state.last_status_publish_ms) >= status_interval_ms) {
        messages.push_back(MqttMessage{
            mqttTopic(config, "status"),
            buildStatusPayload(g_mqtt_runtime_state.latest_app_state),
            0,
            false
        });
        g_mqtt_runtime_state.last_status_publish_ms = now_ms;
    }

    const int64_t telemetry_interval_ms = std::max<int64_t>(200, config.mqtt_telemetry_interval_ms);
    if (g_mqtt_runtime_state.last_telemetry_publish_ms == 0 ||
        (now_ms - g_mqtt_runtime_state.last_telemetry_publish_ms) >= telemetry_interval_ms) {
        messages.push_back(MqttMessage{
            mqttTopic(config, "telemetry"),
            buildTelemetryPayload(config),
            0,
            false
        });
        g_mqtt_runtime_state.last_telemetry_publish_ms = now_ms;
    }
}

void queueMqttEvent(
    const AppConfig& config,
    BlockingQueue<MqttMessage>& mqtt_queue,
    const std::string& event_name,
    const std::string& message,
    bool use_cooldown) {
    if (!config.enable_mqtt) {
        return;
    }
    if (use_cooldown && !shouldPublishCooldownEvent(config, event_name)) {
        return;
    }

    const MqttMessage mqtt_message{
        mqttTopic(config, "event"),
        buildEventPayload(event_name, message),
        0,
        false
    };
    if (!mqtt_queue.push(mqtt_message)) {
        std::cerr << "[MQTT] warning: queue rejected event=" << event_name << "\n";
    }
}

}  // namespace

VisionApp::VisionApp(AppConfig config) : config_(std::move(config)) {}

VisionApp::~VisionApp() {
    stop();
}

bool VisionApp::start() {
    try {
        const bool use_nv12_pool_bytes =
            (config_.frame_channels == 1) && !config_.input_stream_is_h264;
        const std::size_t frame_bytes = use_nv12_pool_bytes
                                            ? (static_cast<std::size_t>(config_.frame_width) *
                                               static_cast<std::size_t>(config_.frame_height) * 3U / 2U)
                                            : (static_cast<std::size_t>(config_.frame_width) *
                                               static_cast<std::size_t>(config_.frame_height) *
                                               static_cast<std::size_t>(config_.frame_channels));

        const char* frame_pool_format = "UNKNOWN";
        if (use_nv12_pool_bytes) {
            frame_pool_format = "NV12";
        } else if (config_.frame_channels == 3) {
            frame_pool_format = "RGB";
        } else if (config_.frame_channels == 1) {
            frame_pool_format = "GRAY";
        }
        std::cout << "[FramePool] frame_bytes=" << frame_bytes
                  << ", format=" << frame_pool_format << "\n";

        FramePool::instance().initialize(
            8,
            frame_bytes,
            config_.frame_width,
            config_.frame_height,
            config_.frame_channels);

        if (!camera_.open(config_)) {
            throw std::runtime_error("camera open failed");
        }
        if (!gesture_model_.load(config_)) {
            std::cerr << "[App] warning: gesture model load failed, fallback/no-gesture mode may be used\n";
        }

        if (config_.use_three_model_pipeline && config_.use_legacy_posture_drink_model) {
            std::cerr << "[AI] warning: three_model and legacy fallback are both enabled; "
                      << "this is compatibility mode and is not recommended for three-model debugging\n";
        }

        if (config_.use_three_model_pipeline) {
            if (!pose_model_.load(config_)) {
                std::cerr << "[App] warning: pose model load failed, pose result will stay invalid\n";
            }
            if (!cup_model_.load(config_)) {
                std::cerr << "[App] warning: cup model load failed, cup result will stay invalid\n";
            }
        } else {
            std::cout << "[AI] three-model pipeline disabled by config\n";
        }

        if (config_.use_legacy_posture_drink_model && !posture_drink_model_.load(config_)) {
            std::cerr << "[App] warning: legacy posture_drink model load failed, legacy fallback disabled\n";
            config_.use_legacy_posture_drink_model = false;
        }

        posture_analyzer_.configure(config_);
        drink_detector_.configure(config_);
        ai_scheduler_.configure(config_);
        std::cout << "[AI] pipeline config: three_model="
                  << (config_.use_three_model_pipeline ? "true" : "false")
                  << ", legacy_fallback="
                  << (config_.use_legacy_posture_drink_model ? "true" : "false")
                  << ", intervals_ms gesture/pose/cup="
                  << config_.gesture_interval_ms << "/"
                  << config_.pose_interval_ms << "/"
                  << config_.cup_interval_ms << "\n";
        std::cout << "[阈值][总览] gesture_prob=" << config_.gesture_score_threshold
                  << ", cup_score=" << config_.cup_score_threshold
                  << ", pose_keypoint=" << config_.pose_keypoint_score_threshold
                  << ", drink_distance_norm=" << config_.drink_distance_norm_threshold
                  << ", drink_consecutive_hits=" << config_.drink_consecutive_hits << "\n";

        if (!image_processor_.open(config_)) {
            throw std::runtime_error("image processor open failed");
        }

        if (config_.enable_display) {
            if (!display_.open(config_)) {
                std::cerr << "[App] warning: display open failed, display output will use mock log\n";
            }
        } else {
            std::cout << "[Display] disabled, use mock log output\n";
        }

        if (config_.enable_mqtt) {
            if (!mqtt_.connect(config_)) {
                std::cerr << "[MQTT][WARN] connect failed, continue without MQTT\n";
                config_.enable_mqtt = false;
            }
        } else {
            std::cout << "[App] MQTT disabled by config\n";
        }

        if (config_.enable_mpp_decoder) {
            if (!mpp_decoder_.open(config_)) {
                std::cerr << "[App] warning: mpp decoder open failed, decoder disabled\n";
            }
        } else {
            std::cout << "[App] MPP decoder disabled by config\n";
        }

        if (config_.enable_mpp_encoder) {
            if (!mpp_encoder_.open(config_)) {
                std::cerr << "[App] warning: mpp encoder open failed, encoderLoop will use raw-frame fallback\n";
            }
        } else {
            std::cout << "[App] MPP encoder disabled by config\n";
        }

        if (config_.enable_web_stream) {
            if (!web_.start(config_)) {
                std::cerr << "[App] warning: web streamer start failed, web output disabled\n";
            }
        } else {
            std::cout << "[App] Web stream disabled by config\n";
        }

        exit_requested_ = false;
        thread_error_ = false;
        state_ = SystemState::Idle;
        g_signal_exit_requested = false;
        resetMqttRuntimeState();

        camera_thread_ = std::thread(&VisionApp::runThread, this, "camera", [this] { cameraLoop(); });
        if (shouldRunEncoderPipeline()) {
            encoder_thread_ = std::thread(&VisionApp::runThread, this, "encoder", [this] { encoderLoop(); });
        } else {
            std::cout << "[Encoder] disabled because both MPP encoder and Web stream are disabled\n";
        }
        ai_thread_ = std::thread(&VisionApp::runThread, this, "ai", [this] { aiLoop(); });
        mqtt_thread_ = std::thread(&VisionApp::runThread, this, "mqtt", [this] { mqttLoop(); });
        display_thread_ = std::thread(&VisionApp::runThread, this, "display", [this] { displayLoop(); });
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[App] start exception: " << e.what() << "\n";
        stop();
        return false;
    } catch (...) {
        std::cerr << "[App] start unknown exception\n";
        stop();
        return false;
    }
}

void VisionApp::stop() {
    const bool already_requested = exit_requested_.exchange(true);
    if (already_requested) {
        return;
    }

    latest_ai_frame_.close();
    encode_queue_.close();
    mqtt_queue_.close();
    display_queue_.close();
    FramePool::instance().close();

    if (camera_thread_.joinable()) {
        camera_thread_.join();
    }
    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }
    if (ai_thread_.joinable()) {
        ai_thread_.join();
    }
    if (mqtt_thread_.joinable()) {
        mqtt_thread_.join();
    }
    if (display_thread_.joinable()) {
        display_thread_.join();
    }

    web_.stop();
    image_processor_.close();
    mpp_encoder_.close();
    mpp_decoder_.close();
    mqtt_.disconnect();
    display_.close();
    camera_.close();
}

int VisionApp::run() {
    std::signal(SIGINT, handleExitSignal);
#ifdef SIGTERM
    std::signal(SIGTERM, handleExitSignal);
#endif

    if (!start()) {
        std::cerr << "[App] start failed\n";
        return 1;
    }

    while (!exit_requested_.load() && !g_signal_exit_requested.load()) {
        logPerformanceIfDue();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (g_signal_exit_requested.load()) {
        std::cout << "[App] signal received, shutting down\n";
    }

    stop();
    return thread_error_.load() ? 2 : 0;
}

void VisionApp::runThread(const std::string& name, const std::function<void()>& loop) {
    try {
        loop();
    } catch (const std::exception& e) {
        thread_error_ = true;
        exit_requested_ = true;
        latest_ai_frame_.close();
        encode_queue_.close();
        mqtt_queue_.close();
        display_queue_.close();
        std::cerr << "[Thread] " << name << " exception: " << e.what() << "\n";
    } catch (...) {
        thread_error_ = true;
        exit_requested_ = true;
        latest_ai_frame_.close();
        encode_queue_.close();
        mqtt_queue_.close();
        display_queue_.close();
        std::cerr << "[Thread] " << name << " unknown exception\n";
    }
}

void VisionApp::cameraLoop() {
    while (!exit_requested_) {
        FramePtr frame = FramePool::instance().acquire();
        if (!frame) {
            break;
        }

        if (!camera_.read(*frame)) {
            if (config_.enable_mock_camera) {
                std::cout << "[CameraMock] finished\n";
                exit_requested_ = true;
                latest_ai_frame_.close();
                encode_queue_.close();
                display_queue_.close();
                mqtt_queue_.close();
                break;
            }
            continue;
        }

        if ((frame->id % 30U) == 0U) {
            std::cout << "[Camera] frame_id=" << frame->id << "\n";
        }

        perf_.camera_frames.fetch_add(1, std::memory_order_relaxed);

        if (!latest_ai_frame_.update(frame)) {
            break;
        }

        if (shouldRunEncoderPipeline() && !encode_queue_.push(frame) && exit_requested_) {
            break;
        }
    }
}

void VisionApp::encoderLoop() {
    uint64_t last_encoded_frame_id = 0;

    while (!exit_requested_) {
        auto item = encode_queue_.pop();
        if (!item.has_value()) {
            break;
        }

        const uint64_t frame_id = (*item)->id;
        if ((frame_id % 30U) == 0U || (last_encoded_frame_id != 0 && frame_id > last_encoded_frame_id + 1U)) {
            std::cout << "[Encoder] frame_id=" << frame_id;
            if (last_encoded_frame_id != 0 && frame_id > last_encoded_frame_id + 1U) {
                std::cout << ", skipped=" << (frame_id - last_encoded_frame_id - 1U);
            }
            std::cout << "\n";
        }
        last_encoded_frame_id = frame_id;
        perf_.encoder_frames.fetch_add(1, std::memory_order_relaxed);

        EncodedPacket packet;
        const auto encode_begin = std::chrono::steady_clock::now();
        if (mpp_encoder_.encode(**item, packet)) {
            perf_.mpp_encode.addSample(elapsedUs(encode_begin));
            web_.publishEncodedVideo(packet);
        } else {
            perf_.mpp_encode.addSample(elapsedUs(encode_begin));
            web_.publishFrame(*item);
        }
    }
}

void VisionApp::aiLoop() {
    PoseResult last_pose_result;
    CupResult last_cup_result;
    bool has_pose_result = false;
    bool has_cup_result = false;
    uint64_t last_ai_buffer_version = 0;
    uint64_t last_ai_frame_id = 0;

    while (!exit_requested_) {
        FramePtr frame;
        uint64_t new_ai_buffer_version = 0;
        if (!latest_ai_frame_.getLatestIfNewer(last_ai_buffer_version, frame, new_ai_buffer_version)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        last_ai_buffer_version = new_ai_buffer_version;
        if (!frame) {
            continue;
        }

        if ((frame->id % 30U) == 0U || (last_ai_frame_id != 0 && frame->id > last_ai_frame_id + 1U)) {
            std::cout << "[AI] latest frame_id=" << frame->id;
            if (last_ai_frame_id != 0 && frame->id > last_ai_frame_id + 1U) {
                std::cout << ", skipped=" << (frame->id - last_ai_frame_id - 1U);
            }
            std::cout << "\n";
        }
        last_ai_frame_id = frame->id;

        const SystemState current_state = state_.load();
        const bool running_mode = (current_state == SystemState::Running);
        const AiScheduleDecision decision = ai_scheduler_.next(frame->timestamp_ms, running_mode);
        if (!decision.run_gesture && !decision.run_pose && !decision.run_cup) {
            continue;
        }

        std::cout << "[AI][调度] 状态机=" << toChineseString(current_state)
                  << "(" << toString(current_state) << ")"
                  << ", 本轮模型=" << scheduledModelList(decision)
                  << ", frame=" << frame->id << "\n";

        perf_.ai_iterations.fetch_add(1, std::memory_order_relaxed);

        if (config_.debug_ai_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.debug_ai_delay_ms));
        }

        const CropRect full_frame_crop{0, 0, frame->width, frame->height};
        auto preprocessInput = [&](const char* model_name, int width, int height, Frame& output) {
            if (!image_processor_.cropResize(*frame, full_frame_crop, width, height, output)) {
                std::cerr << "[AI] image preprocess failed, model=" << model_name
                          << ", frame=" << frame->id
                          << ", input=" << width << "x" << height << "\n";
                return false;
            }
            return true;
        };
        auto preprocessLetterboxInput = [&](const char* model_name, int width, int height, Frame& output) {
            if (!image_processor_.letterbox(*frame, full_frame_crop, width, height, output)) {
                std::cerr << "[AI] image letterbox failed, model=" << model_name
                          << ", frame=" << frame->id
                          << ", input=" << width << "x" << height << "\n";
                return false;
            }
            return true;
        };

        if (decision.run_gesture) {
            Frame gesture_input;
            if (!preprocessInput(
                    "gesture",
                    config_.gesture_input_width,
                    config_.gesture_input_height,
                    gesture_input)) {
                std::cerr << "[AI] skip gesture inference, frame=" << frame->id << "\n";
            } else {
                std::cout << "[AI][模型] 调用 gesture(手势), frame=" << gesture_input.id
                          << ", input=" << gesture_input.width << "x" << gesture_input.height
                          << ", preprocess=resize\n";

                GestureResult gesture_result;
                const auto gesture_begin = std::chrono::steady_clock::now();
                try {
                    gesture_result = gesture_model_.infer(gesture_input);
                } catch (const std::exception& e) {
                    gesture_result.frame_id = gesture_input.id;
                    gesture_result.timestamp_ms = gesture_input.timestamp_ms;
                    gesture_result.type = GestureType::None;
                    gesture_result.gesture_name = "gesture_infer_exception";
                    std::cerr << "[AI] gesture infer exception: " << e.what() << "\n";
                } catch (...) {
                    gesture_result.frame_id = gesture_input.id;
                    gesture_result.timestamp_ms = gesture_input.timestamp_ms;
                    gesture_result.type = GestureType::None;
                    gesture_result.gesture_name = "gesture_infer_unknown_exception";
                    std::cerr << "[AI] gesture infer unknown exception\n";
                }
                perf_.gesture_infer.addSample(elapsedUs(gesture_begin));

                switch (gesture_result.type) {
                    case GestureType::Start:
                    {
                        requestStartByGesture();
                        publishDisplayState(nullptr, PostureState::UNKNOWN, DrinkState::NORMAL, "start");
                        break;
                    }
                    case GestureType::Stop:
                    {
                        requestStopByGesture();
                        publishDisplayState(nullptr, PostureState::UNKNOWN, DrinkState::NORMAL, "stop");
                        break;
                    }
                    case GestureType::Heart:
                    {
                        publishDisplayState(nullptr, PostureState::UNKNOWN, DrinkState::NORMAL, "heart");
                        break;
                    }
                    case GestureType::Like:
                    {
                        queueMqttEvent(
                            config_,
                            mqtt_queue_,
                            "gesture_like",
                            "Like gesture detected",
                            false);
                        break;
                    }
                    case GestureType::None:
                        break;
                }
            }
        }

        if (!running_mode) {
            continue;
        }

        const bool perception_tick = decision.run_pose || decision.run_cup;
        if (!perception_tick) {
            continue;
        }

        VisionResult result;
        PostureState posture_state = PostureState::UNKNOWN;
        DrinkState drink_state = DrinkState::NORMAL;
        bool has_result_to_publish = false;
        bool need_legacy_fallback = false;

        if (config_.use_three_model_pipeline) {
            if (decision.run_pose) {
                Frame pose_input;
                if (!preprocessLetterboxInput(
                        "pose",
                        config_.pose_input_width,
                        config_.pose_input_height,
                        pose_input)) {
                    std::cerr << "[AI] skip pose inference, frame=" << frame->id << "\n";
                    last_pose_result = PoseResult{};
                    last_pose_result.frame_id = frame->id;
                    last_pose_result.timestamp_ms = frame->timestamp_ms;
                    last_pose_result.valid = false;
                    last_pose_result.message = "pose_preprocess_failed";
                    has_pose_result = true;
                } else {
                    std::cout << "[AI][模型] 调用 pose(姿态), frame=" << pose_input.id
                              << ", input=" << pose_input.width << "x" << pose_input.height
                              << ", preprocess=letterbox\n";
                    const auto pose_begin = std::chrono::steady_clock::now();
                    try {
                        last_pose_result = pose_model_.infer(pose_input);
                    } catch (const std::exception& e) {
                        last_pose_result = PoseResult{};
                        last_pose_result.frame_id = pose_input.id;
                        last_pose_result.timestamp_ms = pose_input.timestamp_ms;
                        last_pose_result.valid = false;
                        last_pose_result.message = "pose_infer_exception";
                        std::cerr << "[AI] pose infer exception: " << e.what() << "\n";
                    } catch (...) {
                        last_pose_result = PoseResult{};
                        last_pose_result.frame_id = pose_input.id;
                        last_pose_result.timestamp_ms = pose_input.timestamp_ms;
                        last_pose_result.valid = false;
                        last_pose_result.message = "pose_infer_unknown_exception";
                        std::cerr << "[AI] pose infer unknown exception\n";
                    }
                    perf_.pose_infer.addSample(elapsedUs(pose_begin));
                    has_pose_result = true;
                    std::cout << "[核心][pose] valid=" << (last_pose_result.valid ? "true" : "false")
                              << ", has_person=" << (last_pose_result.has_person ? "true" : "false")
                              << ", person_score=" << last_pose_result.person_score
                              << ", frame=" << last_pose_result.frame_id << "\n";
                }
            }

            if (decision.run_cup) {
                Frame cup_input;
                if (!preprocessLetterboxInput(
                        "cup",
                        config_.cup_input_width,
                        config_.cup_input_height,
                        cup_input)) {
                    std::cerr << "[AI] skip cup inference, frame=" << frame->id << "\n";
                    last_cup_result = CupResult{};
                    last_cup_result.frame_id = frame->id;
                    last_cup_result.timestamp_ms = frame->timestamp_ms;
                    last_cup_result.valid = false;
                    last_cup_result.message = "cup_preprocess_failed";
                    has_cup_result = true;
                } else {
                    std::cout << "[AI][模型] 调用 cup(饮品), frame=" << cup_input.id
                              << ", input=" << cup_input.width << "x" << cup_input.height
                              << ", preprocess=letterbox\n";
                    const auto cup_begin = std::chrono::steady_clock::now();
                    try {
                        last_cup_result = cup_model_.infer(cup_input);
                    } catch (const std::exception& e) {
                        last_cup_result = CupResult{};
                        last_cup_result.frame_id = cup_input.id;
                        last_cup_result.timestamp_ms = cup_input.timestamp_ms;
                        last_cup_result.valid = false;
                        last_cup_result.message = "cup_infer_exception";
                        std::cerr << "[AI] cup infer exception: " << e.what() << "\n";
                    } catch (...) {
                        last_cup_result = CupResult{};
                        last_cup_result.frame_id = cup_input.id;
                        last_cup_result.timestamp_ms = cup_input.timestamp_ms;
                        last_cup_result.valid = false;
                        last_cup_result.message = "cup_infer_unknown_exception";
                        std::cerr << "[AI] cup infer unknown exception\n";
                    }
                    perf_.cup_infer.addSample(elapsedUs(cup_begin));
                    has_cup_result = true;
                    std::cout << "[核心][cup] valid=" << (last_cup_result.valid ? "true" : "false")
                              << ", cups=" << last_cup_result.cups.size()
                              << ", frame=" << last_cup_result.frame_id << "\n";
                }
            }

            const bool has_valid_three_model_result =
                (has_pose_result && last_pose_result.valid) ||
                (has_cup_result && last_cup_result.valid);

            if (has_valid_three_model_result) {
                if (has_pose_result) {
                    posture_state = posture_analyzer_.update(last_pose_result);
                    std::cout << "[核心][坐姿判断] posture_state=" << toString(posture_state)
                              << "(" << postureStateLabel(posture_state) << ")"
                              << ", pose_valid=" << (last_pose_result.valid ? "true" : "false")
                              << ", has_person=" << (last_pose_result.has_person ? "true" : "false")
                              << "\n";
                }
                if (has_pose_result && has_cup_result) {
                    drink_state = drink_detector_.update(last_pose_result, last_cup_result);
                    std::cout << "[核心][喝水判断] drink_state=" << toString(drink_state)
                              << "(" << drinkStateLabel(drink_state) << ")"
                              << ", pose_valid=" << (last_pose_result.valid ? "true" : "false")
                              << ", cup_valid=" << (last_cup_result.valid ? "true" : "false")
                              << ", cups=" << last_cup_result.cups.size()
                              << "\n";
                }
                result = composeVisionResult(last_pose_result, last_cup_result, posture_state, drink_state);
                result.message += ",pipeline=three_model";
                has_result_to_publish = true;
            } else {
                need_legacy_fallback = config_.use_legacy_posture_drink_model;
                if (!need_legacy_fallback) {
                    PoseResult pose_for_state = has_pose_result ? last_pose_result : PoseResult{};
                    CupResult cup_for_state = has_cup_result ? last_cup_result : CupResult{};
                    if (pose_for_state.frame_id == 0) {
                        pose_for_state.frame_id = frame->id;
                        pose_for_state.timestamp_ms = frame->timestamp_ms;
                    }
                    if (cup_for_state.frame_id == 0) {
                        cup_for_state.frame_id = frame->id;
                        cup_for_state.timestamp_ms = frame->timestamp_ms;
                    }
                    result = composeVisionResult(
                        pose_for_state,
                        cup_for_state,
                        PostureState::UNKNOWN,
                        DrinkState::NORMAL);
                    result.message += ",pipeline=three_model_invalid";
                    std::cout << "[核心][判断] 三模型输出无有效结果，保持 posture=UNKNOWN/drink=NORMAL, frame="
                              << frame->id << "\n";
                    has_result_to_publish = true;
                }
            }
        } else {
            need_legacy_fallback = config_.use_legacy_posture_drink_model;
        }

        if (need_legacy_fallback) {
            Frame legacy_input;
            if (!preprocessInput(
                    "legacy_posture_drink",
                    config_.ai_input_width,
                    config_.ai_input_height,
                    legacy_input)) {
                std::cerr << "[AI] skip legacy posture_drink inference, frame=" << frame->id << "\n";
            } else {
                try {
                    result = posture_drink_model_.infer(legacy_input);
                } catch (const std::exception& e) {
                    result = VisionResult{};
                    result.frame_id = legacy_input.id;
                    result.timestamp_ms = legacy_input.timestamp_ms;
                    result.message = "legacy_posture_drink_infer_exception";
                    std::cerr << "[AI] legacy posture_drink infer exception: " << e.what() << "\n";
                } catch (...) {
                    result = VisionResult{};
                    result.frame_id = legacy_input.id;
                    result.timestamp_ms = legacy_input.timestamp_ms;
                    result.message = "legacy_posture_drink_infer_unknown_exception";
                    std::cerr << "[AI] legacy posture_drink infer unknown exception\n";
                }
                posture_state = result.bad_posture ? PostureState::BAD_ALERT : PostureState::UNKNOWN;
                drink_state = result.drink_detected ? DrinkState::DRINK_DETECTED :
                              (result.drink_reminder ? DrinkState::NEED_REMIND : DrinkState::NORMAL);
                std::cout << "[AI][模型] 调用 legacy_posture_drink(旧姿态饮水), frame=" << legacy_input.id
                          << ", input=" << legacy_input.width << "x" << legacy_input.height << "\n";
                has_result_to_publish = true;
            }
        }

        if (!has_result_to_publish) {
            continue;
        }

        web_.publishResult(result);
        enqueueAlarmMessages(result);
        publishDisplayState(&result, posture_state, drink_state, std::string());
    }
}

void VisionApp::mqttLoop() {
    while (true) {
        if (config_.enable_mqtt) {
            std::vector<MqttMessage> periodic_messages;
            collectPeriodicMqttMessages(config_, periodic_messages);
            for (const auto& message : periodic_messages) {
                try {
                    mqtt_.publish(message);
                } catch (const std::exception& e) {
                    std::cerr << "[MQTT] publish exception: " << e.what() << "\n";
                }
            }
        }

        auto item = mqtt_queue_.popFor(std::chrono::milliseconds(200));
        if (!item.has_value()) {
            if (exit_requested_.load()) {
                break;
            }
            continue;
        }

        try {
            mqtt_.publish(*item);
        } catch (const std::exception& e) {
            std::cerr << "[MQTT] publish exception: " << e.what() << "\n";
        }
    }
}

void VisionApp::displayLoop() {
    display_.tick();
    while (!exit_requested_) {
        auto item = display_queue_.popFor(std::chrono::milliseconds(200));
        if (item.has_value()) {
            const auto display_begin = std::chrono::steady_clock::now();
            display_.showFace(*item);
            perf_.display_show.addSample(elapsedUs(display_begin));
        }
        display_.tick();
    }
}

void VisionApp::requestStartByGesture() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_.load() == SystemState::Idle) {
        const SystemState from = state_.load();
        state_ = SystemState::Running;
        drink_detector_.reset();
        ai_scheduler_.reset();
        logStateTransition(from, SystemState::Running, "gesture(start)");
    }
}

void VisionApp::requestStopByGesture() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_.load() == SystemState::Running) {
        const SystemState from = state_.load();
        state_ = SystemState::Stopping;
        logStateTransition(from, SystemState::Stopping, "gesture(stop)");
        state_ = SystemState::Idle;
        drink_detector_.reset();
        ai_scheduler_.reset();
        logStateTransition(SystemState::Stopping, SystemState::Idle, "stop complete");
    }
}

void VisionApp::enqueueAlarmMessages(const VisionResult& result) {
    if (!config_.enable_mqtt) {
        return;
    }

    if (result.bad_posture) {
        queueMqttEvent(config_, mqtt_queue_, "bad_posture", "Please sit up", true);
    }

    if (result.drink_reminder) {
        queueMqttEvent(config_, mqtt_queue_, "drink_remind", "Time to drink water", true);
    }
}

VisionResult VisionApp::composeVisionResult(
    const PoseResult& pose_result,
    const CupResult& cup_result,
    PostureState posture_state,
    DrinkState drink_state) const {
    VisionResult result;
    result.frame_id = pose_result.frame_id != 0 ? pose_result.frame_id : cup_result.frame_id;
    result.timestamp_ms = pose_result.timestamp_ms != 0 ? pose_result.timestamp_ms : cup_result.timestamp_ms;
    result.bad_posture =
        (posture_state == PostureState::BAD_ALERT || posture_state == PostureState::BAD_PENDING);
    result.drink_detected = (drink_state == DrinkState::DRINK_DETECTED);
    result.drink_reminder = (drink_state == DrinkState::NEED_REMIND);

    if (pose_result.has_person && !pose_result.keypoints.empty()) {
        result.head_or_nose = Point{pose_result.keypoints.front().x, pose_result.keypoints.front().y};
    }

    if (pose_result.has_person) {
        Box person_box = pose_result.person_box;
        person_box.label = "person";
        result.boxes.push_back(person_box);
    }

    for (const Box& cup : cup_result.cups) {
        result.boxes.push_back(cup);
    }

    std::ostringstream oss;
    oss << "pose=" << pose_result.message
        << ",cup=" << cup_result.message
        << ",posture_state=" << static_cast<int>(posture_state)
        << ",drink_state=" << static_cast<int>(drink_state);
    result.message = oss.str();
    return result;
}

AppState VisionApp::buildAppState(
    const VisionResult* result,
    PostureState posture_state,
    DrinkState drink_state,
    const std::string& gesture_name) const {
    AppState state;
    state.timestamp_ms = nowMs();
    state.gesture_name = gesture_name;
    state.gesture_triggered = !gesture_name.empty();

    if (thread_error_.load()) {
        state.display_face = DisplayFace::ERROR_FACE;
        return state;
    }

    state.device_mode = (state_.load() == SystemState::Idle) ? DeviceMode::STANDBY : DeviceMode::NORMAL;

    if (result == nullptr) {
        state.display_face = selectDisplayFace(state);
        return state;
    }

    state.posture_alert = result->bad_posture;
    state.drink_alert = result->drink_reminder;
    state.posture_state = posture_state;
    state.drink_state = drink_state;
    state.frame_id = result->frame_id;

    if (state.posture_state == PostureState::UNKNOWN && !result->bad_posture && !result->boxes.empty()) {
        state.posture_state = PostureState::GOOD;
    }

    state.display_face = selectDisplayFace(state);
    return state;
}

void VisionApp::publishDisplayState(
    const VisionResult* result,
    PostureState posture_state,
    DrinkState drink_state,
    const std::string& gesture_name) {
    const AppState state = buildAppState(result, posture_state, drink_state, gesture_name);
    updateLatestMqttAppState(state);

    if (!display_queue_.push(state.display_face)) {
        std::cerr << "[下游][WARN] 显示队列已满，丢弃表情 face="
                  << static_cast<int>(state.display_face)
                  << ", frame_id=" << state.frame_id << "\n";
    }

    web_.publishAppState(state);

    if (config_.enable_mqtt) {
        if (gesture_name == "start") {
            queueMqttEvent(config_, mqtt_queue_, "gesture_start", "Start gesture detected", false);
        } else if (gesture_name == "stop") {
            queueMqttEvent(config_, mqtt_queue_, "gesture_stop", "Stop gesture detected", false);
        } else if (gesture_name == "heart") {
            queueMqttEvent(config_, mqtt_queue_, "gesture_heart", "Heart gesture detected", false);
        }
    }

}

DisplayFace VisionApp::selectDisplayFace(const AppState& state) const {
    if (thread_error_.load()) {
        return DisplayFace::ERROR_FACE;
    }

    return rv1126b::selectDisplayFace(state);
}

bool VisionApp::shouldRunEncoderPipeline() const {
    return config_.enable_mpp_encoder || config_.enable_web_stream;
}

void VisionApp::logPerformanceIfDue() {
    if (!config_.enable_perf_log) {
        return;
    }

    const int64_t now_ms = nowMs();
    int64_t last_log_ms = perf_.last_log_ms.load(std::memory_order_relaxed);
    if (last_log_ms == 0) {
        perf_.last_log_ms.store(now_ms, std::memory_order_relaxed);
        return;
    }

    const int64_t interval_ms = std::max<int64_t>(200, config_.perf_log_interval_ms);
    const int64_t elapsed_ms = now_ms - last_log_ms;
    if (elapsed_ms < interval_ms) {
        return;
    }

    if (!perf_.last_log_ms.compare_exchange_strong(last_log_ms, now_ms, std::memory_order_relaxed)) {
        return;
    }

    auto consumeCounter = [](std::atomic<uint64_t>& value) -> uint64_t {
        return value.exchange(0, std::memory_order_relaxed);
    };
    auto consumeAverageMs = [](PerfStat& stat) -> double {
        const uint64_t count = stat.count.exchange(0, std::memory_order_relaxed);
        const uint64_t total_us = stat.total_us.exchange(0, std::memory_order_relaxed);
        if (count == 0) {
            return 0.0;
        }
        return static_cast<double>(total_us) / static_cast<double>(count) / 1000.0;
    };

    const double elapsed_s = static_cast<double>(elapsed_ms) / 1000.0;
    const double cam_fps = elapsed_s > 0.0
                               ? static_cast<double>(consumeCounter(perf_.camera_frames)) / elapsed_s
                               : 0.0;
    const double enc_fps = elapsed_s > 0.0
                               ? static_cast<double>(consumeCounter(perf_.encoder_frames)) / elapsed_s
                               : 0.0;
    const double ai_fps = elapsed_s > 0.0
                              ? static_cast<double>(consumeCounter(perf_.ai_iterations)) / elapsed_s
                              : 0.0;

    const double gesture_ms = consumeAverageMs(perf_.gesture_infer);
    const double pose_ms = consumeAverageMs(perf_.pose_infer);
    const double cup_ms = consumeAverageMs(perf_.cup_infer);
    const double overlay_ms = consumeAverageMs(perf_.overlay);
    const double enc_ms = consumeAverageMs(perf_.mpp_encode);
    const double display_ms = consumeAverageMs(perf_.display_show);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << "[Perf] cam_fps=" << cam_fps
        << ",enc_fps=" << enc_fps
        << ",ai_fps=" << ai_fps
        << ",gesture_ms=" << gesture_ms
        << ",pose_ms=" << pose_ms
        << ",cup_ms=" << cup_ms;

    if (overlay_ms <= 0.0) {
        oss << ",overlay_ms=NA";
    } else {
        oss << ",overlay_ms=" << overlay_ms;
    }

    oss << ",enc_ms=" << enc_ms
        << ",display_ms=" << display_ms
        << ",eq=" << encode_queue_.size()
        << ",dq=" << display_queue_.size()
        << ",mq=" << mqtt_queue_.size();
    std::cout << oss.str() << "\n";
}

int64_t VisionApp::nowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

}  // namespace rv1126b
