#include "App.hpp"

#include "FramePool.hpp"

#include <chrono>
#include <algorithm>
#include <array>
#include <csignal>
#include <cstdint>
#include <cmath>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <cstring>

#if defined(RV1126B_HAS_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace rv1126b {

namespace {

std::atomic<bool> g_signal_exit_requested{false};

void handleExitSignal(int) {
    g_signal_exit_requested = true;
}

float clampFloat(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

PreprocessTransform makeTransform(const Frame& source, const CropRect& crop, const Frame& model_input) {
    PreprocessTransform transform;
    transform.source_width = source.width;
    transform.source_height = source.height;
    transform.source_crop = crop;
    if (transform.source_crop.width <= 0 || transform.source_crop.height <= 0) {
        transform.source_crop = CropRect{0, 0, source.width, source.height};
    }
    transform.source_crop.x = std::clamp(transform.source_crop.x, 0, std::max(0, source.width - 1));
    transform.source_crop.y = std::clamp(transform.source_crop.y, 0, std::max(0, source.height - 1));
    transform.source_crop.width = std::clamp(transform.source_crop.width, 1, source.width - transform.source_crop.x);
    transform.source_crop.height = std::clamp(transform.source_crop.height, 1, source.height - transform.source_crop.y);
    transform.model_width = model_input.width;
    transform.model_height = model_input.height;
    transform.valid = source.width > 0 && source.height > 0 &&
                      crop.width > 0 && crop.height > 0 &&
                      model_input.width > 0 && model_input.height > 0;
    return transform;
}

Point mapPointToOriginal(const Point& point, const PreprocessTransform& transform) {
    if (!transform.valid) {
        return point;
    }

    const float x_scale = static_cast<float>(transform.source_crop.width) /
                          static_cast<float>(transform.model_width);
    const float y_scale = static_cast<float>(transform.source_crop.height) /
                          static_cast<float>(transform.model_height);
    Point mapped;
    mapped.x = static_cast<float>(transform.source_crop.x) + point.x * x_scale;
    mapped.y = static_cast<float>(transform.source_crop.y) + point.y * y_scale;
    mapped.x = clampFloat(mapped.x, 0.0F, static_cast<float>(std::max(0, transform.source_width - 1)));
    mapped.y = clampFloat(mapped.y, 0.0F, static_cast<float>(std::max(0, transform.source_height - 1)));
    return mapped;
}

Box mapBoxToOriginal(const Box& box, const PreprocessTransform& transform) {
    if (!transform.valid) {
        return box;
    }

    const float x_scale = static_cast<float>(transform.source_crop.width) /
                          static_cast<float>(transform.model_width);
    const float y_scale = static_cast<float>(transform.source_crop.height) /
                          static_cast<float>(transform.model_height);
    const float x0 = static_cast<float>(transform.source_crop.x) + box.x * x_scale;
    const float y0 = static_cast<float>(transform.source_crop.y) + box.y * y_scale;
    const float x1 = static_cast<float>(transform.source_crop.x) + (box.x + box.w) * x_scale;
    const float y1 = static_cast<float>(transform.source_crop.y) + (box.y + box.h) * y_scale;

    Box mapped = box;
    mapped.x = clampFloat(x0, 0.0F, static_cast<float>(std::max(0, transform.source_width - 1)));
    mapped.y = clampFloat(y0, 0.0F, static_cast<float>(std::max(0, transform.source_height - 1)));
    const float right = clampFloat(x1, 0.0F, static_cast<float>(std::max(0, transform.source_width)));
    const float bottom = clampFloat(y1, 0.0F, static_cast<float>(std::max(0, transform.source_height)));
    mapped.w = std::max(1.0F, right - mapped.x);
    mapped.h = std::max(1.0F, bottom - mapped.y);
    return mapped;
}

void mapPoseToOriginal(PoseResult& pose, const PreprocessTransform& transform) {
    if (!transform.valid || !pose.valid) {
        return;
    }

    if (pose.has_person) {
        pose.person_box = mapBoxToOriginal(pose.person_box, transform);
    }
    for (PoseKeypoint& keypoint : pose.keypoints) {
        const Point mapped = mapPointToOriginal(Point{keypoint.x, keypoint.y}, transform);
        keypoint.x = mapped.x;
        keypoint.y = mapped.y;
    }
    if (pose.has_person) {
        pose.message += ",coords=original";
    }
}

// 6.19加
void mapCupToOriginal(CupResult& cup, const PreprocessTransform& transform) {
    if (!transform.valid || !cup.valid) {
        return;
    }

    cup.cup_box = mapBoxToOriginal(cup.cup_box, transform);
    for (Box& box : cup.cups) {
        box = mapBoxToOriginal(box, transform);
    }
    cup.message += ",coords=original";
}

uint8_t clampToByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}


}  // namespace

VisionApp::VisionApp(AppConfig config) : config_(std::move(config)) {}

VisionApp::~VisionApp() {
    stop();
}

bool VisionApp::start() {
    try {
        const bool camera_uses_nv12 = !config_.input_stream_is_h264 && config_.frame_channels == 1;
        const std::size_t frame_pixels = static_cast<std::size_t>(config_.frame_width) *
                                         static_cast<std::size_t>(config_.frame_height);
        const std::size_t frame_bytes = camera_uses_nv12
                                            ? frame_pixels * 3U / 2U
                                            : frame_pixels *
                                                  static_cast<std::size_t>(config_.frame_channels);

        std::cout << "[FramePool] frame_bytes=" << frame_bytes
                  << ", format=" << (camera_uses_nv12 ? "NV12" : "configured")
                  << "\n";

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

        if (!image_processor_.open(config_)) {
            throw std::runtime_error("image processor open failed");
        }

#if defined(RV1126B_HAS_OPENCV)
        overlay_runtime_enabled_ = config_.enable_video_overlay;
        std::cout << "[Overlay] " << (overlay_runtime_enabled_ ? "enabled" : "disabled") << "\n";
#else
        overlay_runtime_enabled_ = false;
        if (config_.enable_video_overlay) {
            std::cout << "[Overlay] disabled because OpenCV is not available\n";
        } else {
            std::cout << "[Overlay] disabled\n";
        }
#endif

        if (config_.enable_display) {
            if (!display_.open(config_)) {
                std::cerr << "[App] warning: display open failed, display output will use mock log\n";
            }
        } else {
            std::cout << "[Display] disabled, use mock log output\n";
        }

        if (config_.enable_mqtt) {
            if (!mqtt_.connect(config_)) {
                std::cerr << "[App] warning: mqtt connect failed, mqtt publish disabled\n";
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
                std::cerr << "[App] warning: mpp encoder open failed";
                if (config_.web_stream_protocol == WebStreamProtocol::HttpFlv) {
                    std::cerr << ", HTTP-FLV requires real H264 encoder\n";
                } else {
                    std::cerr << ", encoderLoop will use raw-frame fallback\n";
                }
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

        Frame overlay_frame;
        const Frame* encode_frame = item->get();
        if (applyVideoOverlay(**item, overlay_frame)) {
            encode_frame = &overlay_frame;
        }

        EncodedPacket packet;
        if (mpp_encoder_.encode(*encode_frame, packet)) {
            web_.publishEncodedVideo(packet);
        } else if (config_.web_stream_protocol == WebStreamProtocol::HttpFlv) {
            if ((frame_id % 30U) == 0U) {
                std::cerr << "[Encoder] H264 encode failed, HTTP-FLV frame dropped, frame_id="
                          << frame_id << "\n";
            }
        } else {
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

    const char* force_ai_env = std::getenv("RV_FORCE_AI_RUNNING");
    const bool force_ai_running =
        force_ai_env != nullptr &&
        (std::strcmp(force_ai_env, "1") == 0 ||
         std::strcmp(force_ai_env, "true") == 0 ||
         std::strcmp(force_ai_env, "on") == 0);

    if (force_ai_running) {
        std::cout << "[AI] force running enabled, gesture start gate bypassed\n";
    }

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

        const bool running_mode = force_ai_running || state_.load() == SystemState::Running;
        const AiScheduleDecision decision = ai_scheduler_.next(frame->timestamp_ms, running_mode);
        if (!decision.run_gesture && !decision.run_pose && !decision.run_cup) {
            continue;
        }

        if (config_.debug_ai_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.debug_ai_delay_ms));
        }

        // 6.19加
        const CropRect full_frame_crop{0, 0, frame->width, frame->height};
        auto preprocessInput = [&](
                                const char* model_name,
                                int width,
                                int height,
                                Frame& output,
                                PreprocessTransform* transform = nullptr) {
            if (!image_processor_.cropResize(*frame, full_frame_crop, width, height, output)) {
                std::cerr << "[AI] image preprocess failed, model=" << model_name
                        << ", frame=" << frame->id
                        << ", source_format=" << static_cast<int>(frame->format)
                        << ", input=" << width << "x" << height << "\n";
                return false;
            }

            if (transform != nullptr) {
                *transform = makeTransform(*frame, full_frame_crop, output);
            }

            return true;
        };
        // 6.19加

        if (decision.run_gesture) {
            Frame gesture_input;
            if (!preprocessInput(
                    "gesture",
                    config_.gesture_input_width,
                    config_.gesture_input_height,
                    gesture_input)) {
                std::cerr << "[AI] skip gesture inference, frame=" << frame->id << "\n";
            } else {
                std::cout << "[AI] schedule gesture, frame=" << gesture_input.id
                          << ", input=" << gesture_input.width << "x" << gesture_input.height << "\n";

                GestureResult gesture_result;
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

                updateOverlayGesture(gesture_result);

                                switch (gesture_result.type) {
                    case GestureType::Start:
                    {
                        requestStartByGesture();

                        // Start 只负责进入 Running，不显示爱心。
                        // 顺手把屏幕恢复成普通表情，避免 ST7789 保持上一次爱心画面。
                        display_queue_.push(DisplayFace::NormalFace);

                        std::cout << "[AI] gesture start, class="
                                  << gesture_result.gesture_name
                                  << ", DisplayFace pushed=NormalFace\n";
                        break;
                    }

                    case GestureType::Stop:
                    {
                        requestStopByGesture();

                        // Stop 只负责停止，可以显示 SleepFace。
                        display_queue_.push(DisplayFace::SleepFace);

                        std::cout << "[AI] gesture stop, class="
                                  << gesture_result.gesture_name
                                  << ", DisplayFace pushed=SleepFace\n";
                        break;
                    }

                    case GestureType::Heart:
                    {
                        display_queue_.push(DisplayFace::GestureOkFace);

                        std::cout << "[AI] gesture heart, class="
                                  << gesture_result.gesture_name
                                  << ", DisplayFace pushed=GestureOkFace\n";
                        break;
                    }

                    case GestureType::Like:
                    {
                        display_queue_.push(DisplayFace::SmileFace);

                        std::cout << "[AI] gesture like, class="
                                  << gesture_result.gesture_name
                                  << ", DisplayFace pushed=SmileFace\n";
                        break;
                    }

                    case GestureType::None:
                    default:
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
                PreprocessTransform pose_transform;
                if (!preprocessInput(
                        "pose",
                        config_.pose_input_width,
                        config_.pose_input_height,
                        pose_input,
                        &pose_transform)) {
                    std::cerr << "[AI] skip pose inference, frame=" << frame->id << "\n";
                    last_pose_result = PoseResult{};
                    last_pose_result.frame_id = frame->id;
                    last_pose_result.timestamp_ms = frame->timestamp_ms;
                    last_pose_result.valid = false;
                    last_pose_result.message = "pose_preprocess_failed";
                    has_pose_result = true;
                } else {
                    std::cout << "[AI] schedule pose, frame=" << pose_input.id
                              << ", input=" << pose_input.width << "x" << pose_input.height << "\n";
                    try {
                        last_pose_result = pose_model_.infer(pose_input);
                        mapPoseToOriginal(last_pose_result, pose_transform);
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
                    has_pose_result = true;
                    std::cout << "[AI] pose result valid=" << (last_pose_result.valid ? "true" : "false")
                              << ", message=" << last_pose_result.message << "\n";
                }
            }

            if (decision.run_cup) {
                Frame cup_input;
                PreprocessTransform cup_transform;
                if (!preprocessInput(
                        "cup",
                        config_.cup_input_width,
                        config_.cup_input_height,
                        cup_input,
                        &cup_transform)) {
                    std::cerr << "[AI] skip cup inference, frame=" << frame->id << "\n";
                    last_cup_result = CupResult{};
                    last_cup_result.frame_id = frame->id;
                    last_cup_result.timestamp_ms = frame->timestamp_ms;
                    last_cup_result.valid = false;
                    last_cup_result.message = "cup_preprocess_failed";
                    has_cup_result = true;
                } else {
                    std::cout << "[AI] schedule cup, frame=" << cup_input.id
                              << ", input=" << cup_input.width << "x" << cup_input.height << "\n";
                    try {
                        last_cup_result = cup_model_.infer(cup_input);
                        mapCupToOriginal(last_cup_result, cup_transform);
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
                    has_cup_result = true;
                    std::cout << "[AI] cup result valid=" << (last_cup_result.valid ? "true" : "false")
                              << ", message=" << last_cup_result.message << "\n";
                }
            }

            const bool has_valid_three_model_result =
                (has_pose_result && last_pose_result.valid) ||
                (has_cup_result && last_cup_result.valid);

            if (has_valid_three_model_result) {
                if (has_pose_result) {
                    posture_state = posture_analyzer_.update(last_pose_result);
                }
                if (has_pose_result && has_cup_result) {
                    drink_state = drink_detector_.update(last_pose_result, last_cup_result);
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
                    std::cout << "[AI] three-model output invalid, keep UNKNOWN/NORMAL state, frame="
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
                std::cout << "[AI] use legacy posture_drink fallback, frame=" << legacy_input.id
                          << ", input=" << legacy_input.width << "x" << legacy_input.height << "\n";
                has_result_to_publish = true;
            }
        }

        if (!has_result_to_publish) {
            continue;
        }

        web_.publishResult(result);
        enqueueAlarmMessages(result);
        updateOverlayPerception(
            last_pose_result,
            last_cup_result,
            buildAppState(&result, posture_state, drink_state, std::string()));
        publishDisplayState(&result, posture_state, drink_state, std::string());
    }
}

void VisionApp::mqttLoop() {
    while (true) {
        auto item = mqtt_queue_.pop();
        if (!item.has_value()) {
            break;
        }

        try {
            mqtt_.publish(*item);
        } catch (const std::exception& e) {
            std::cerr << "[MQTT] publish exception: " << e.what() << "\n";
        }
    }
}

// 优化1(但是优化后cpu没有减少可恶)
void VisionApp::displayLoop() {
    bool has_last_face = false;
    DisplayFace last_face = DisplayFace::NormalFace;

    while (!exit_requested_) {
        auto item = display_queue_.pop();
        if (!item.has_value()) {
            break;
        }

        const DisplayFace current_face = *item;

        // 表情没有变化时，不重复刷新 ST7789
        if (has_last_face && current_face == last_face) {
            continue;
        }

        has_last_face = true;
        last_face = current_face;

        std::cout << "[Display] refresh face="
                  << static_cast<int>(current_face) << "\n";

        display_.showFace(current_face);
    }
}

void VisionApp::requestStartByGesture() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_.load() == SystemState::Idle) {
        state_ = SystemState::Running;
        drink_detector_.reset();
        ai_scheduler_.reset();
        std::cout << "[State] Idle -> Running\n";
    }
}

void VisionApp::requestStopByGesture() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_.load() == SystemState::Running) {
        state_ = SystemState::Stopping;
        std::cout << "[State] Running -> Stopping\n";
        state_ = SystemState::Idle;
        drink_detector_.reset();
        ai_scheduler_.reset();
        std::cout << "[State] Stopping -> Idle\n";
    }
}

void VisionApp::enqueueAlarmMessages(const VisionResult& result) {
    if (!config_.enable_mqtt) {
        return;
    }

    const AppConfig config = config_;

    if (result.bad_posture) {
        mqtt_queue_.push(MqttMessage{
            config.posture_alarm_topic,
            "{\"type\":\"bad_posture\",\"frame_id\":" + std::to_string(result.frame_id) + "}",
            0,
            false
        });
    }

    if (result.drink_reminder) {
        mqtt_queue_.push(MqttMessage{
            config.drink_remind_topic,
            "{\"type\":\"drink_reminder\",\"frame_id\":" + std::to_string(result.frame_id) + "}",
            0,
            false
        });
    }

    std::ostringstream oss;
    oss << "{\"frame_id\":" << result.frame_id
        << ",\"bad_posture\":" << (result.bad_posture ? "true" : "false")
        << ",\"drink_detected\":" << (result.drink_detected ? "true" : "false")
        << ",\"box_count\":" << result.boxes.size()
        << "}";

    mqtt_queue_.push(MqttMessage{config.result_topic, oss.str(), 0, false});
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
    if (!display_queue_.push(state.display_face)) {
        std::cerr << "[DisplayState] warning: display queue rejected face="
                  << static_cast<int>(state.display_face)
                  << ", frame_id=" << state.frame_id << "\n";
    }

    web_.publishAppState(state);

    if (config_.enable_mqtt) {
        const std::string payload = appStateToJson(state);
        if (!mqtt_queue_.push(MqttMessage{config_.app_state_topic, payload, 0, false})) {
            std::cerr << "[DisplayState] warning: mqtt queue rejected app state, frame_id="
                      << state.frame_id << "\n";
        }
    }

    std::cout << "[DisplayState] frame_id=" << state.frame_id
              << ", gesture=" << (state.gesture_name.empty() ? "<none>" : state.gesture_name)
              << ", device_mode=" << static_cast<int>(state.device_mode)
              << ", posture_state=" << static_cast<int>(state.posture_state)
              << ", drink_state=" << static_cast<int>(state.drink_state)
              << ", display_face=" << static_cast<int>(state.display_face) << "\n";
}

void VisionApp::updateOverlayGesture(const GestureResult& gesture) {
    std::lock_guard<std::mutex> lock(overlay_mutex_);
    latest_overlay_ai_.gesture = gesture;
    latest_overlay_ai_.frame_id = gesture.frame_id;
    latest_overlay_ai_.timestamp_ms = gesture.timestamp_ms;
}

void VisionApp::updateOverlayPerception(
    const PoseResult& pose,
    const CupResult& cup,
    const AppState& state) {
    std::lock_guard<std::mutex> lock(overlay_mutex_);
    latest_overlay_ai_.pose = pose;
    latest_overlay_ai_.cup = cup;
    latest_overlay_ai_.frame_id = state.frame_id;
    latest_overlay_ai_.timestamp_ms = state.timestamp_ms;
    latest_overlay_state_ = state;
}

bool VisionApp::applyVideoOverlay(const Frame& src, Frame& dst) {
    if (!overlay_runtime_enabled_) {
        return false;
    }

#if defined(RV1126B_HAS_OPENCV)
    if (src.width <= 0 || src.height <= 0 || src.data.empty()) {
        return false;
    }
    if ((src.width % 2) != 0 || (src.height % 2) != 0) {
        std::cerr << "[Overlay] disabled for odd frame size "
                  << src.width << "x" << src.height << "\n";
        return false;
    }

    AiResultBundle ai_snapshot;
    AppState state_snapshot;
    {
        std::lock_guard<std::mutex> lock(overlay_mutex_);
        ai_snapshot = latest_overlay_ai_;
        state_snapshot = latest_overlay_state_;
    }

    cv::Mat bgr;
    try {
        if (src.format == PixelFormat::NV12) {
            const std::size_t required =
                static_cast<std::size_t>(src.width) *
                static_cast<std::size_t>(src.height) * 3U / 2U;
            if (src.data.size() < required) {
                return false;
            }
            cv::Mat nv12(src.height + src.height / 2, src.width, CV_8UC1,
                         const_cast<uint8_t*>(src.data.data()));
            cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);
        } else if (src.format == PixelFormat::BGR888) {
            if (src.channels < 3) {
                return false;
            }
            cv::Mat input(src.height, src.width, CV_8UC3,
                          const_cast<uint8_t*>(src.data.data()));
            input.copyTo(bgr);
        } else if (src.format == PixelFormat::RGB888) {
            if (src.channels < 3) {
                return false;
            }
            cv::Mat input(src.height, src.width, CV_8UC3,
                          const_cast<uint8_t*>(src.data.data()));
            cv::cvtColor(input, bgr, cv::COLOR_RGB2BGR);
        } else {
            return false;
        }

        auto clampRect = [&](const Box& box) {
            const int x0 = std::clamp(static_cast<int>(std::round(box.x)), 0, src.width - 1);
            const int y0 = std::clamp(static_cast<int>(std::round(box.y)), 0, src.height - 1);
            const int x1 = std::clamp(static_cast<int>(std::round(box.x + box.w)), 0, src.width - 1);
            const int y1 = std::clamp(static_cast<int>(std::round(box.y + box.h)), 0, src.height - 1);
            return cv::Rect(cv::Point(std::min(x0, x1), std::min(y0, y1)),
                            cv::Point(std::max(x0 + 1, x1), std::max(y0 + 1, y1)));
        };

        auto drawLabel = [&](const std::string& text, int x, int y, const cv::Scalar& color) {
            if (text.empty()) {
                return;
            }
            const int baseline = 0;
            const cv::Point origin(std::clamp(x, 0, std::max(0, src.width - 1)),
                                   std::clamp(y, 12, std::max(12, src.height - 1)));
            cv::putText(bgr, text, origin, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::putText(bgr, text, origin, cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1, cv::LINE_AA);
            (void)baseline;
        };

        const cv::Scalar person_color(0, 255, 0);
        const cv::Scalar cup_color(255, 160, 0);
        const cv::Scalar keypoint_color(0, 255, 255);
        const cv::Scalar skeleton_color(255, 255, 0);
        const cv::Scalar text_color(255, 255, 255);

        if (ai_snapshot.pose.has_value() && ai_snapshot.pose->valid && ai_snapshot.pose->has_person) {
            const cv::Rect rect = clampRect(ai_snapshot.pose->person_box);
            cv::rectangle(bgr, rect, person_color, 2);
            drawLabel("person", rect.x, std::max(14, rect.y - 4), person_color);

            static const std::array<std::pair<int, int>, 16> skeleton{{
                {0, 1}, {0, 2}, {1, 3}, {2, 4},
                {5, 6}, {5, 7}, {7, 9}, {6, 8},
                {8, 10}, {5, 11}, {6, 12}, {11, 12},
                {11, 13}, {13, 15}, {12, 14}, {14, 16}
            }};

            auto validKeypoint = [&](int index) {
                return index >= 0 &&
                       index < static_cast<int>(ai_snapshot.pose->keypoints.size()) &&
                       ai_snapshot.pose->keypoints[static_cast<std::size_t>(index)].score >=
                           config_.pose_keypoint_score_threshold;
            };

            for (const auto& link : skeleton) {
                if (!validKeypoint(link.first) || !validKeypoint(link.second)) {
                    continue;
                }
                const PoseKeypoint& a = ai_snapshot.pose->keypoints[static_cast<std::size_t>(link.first)];
                const PoseKeypoint& b = ai_snapshot.pose->keypoints[static_cast<std::size_t>(link.second)];
                cv::line(bgr,
                         cv::Point(static_cast<int>(std::round(a.x)), static_cast<int>(std::round(a.y))),
                         cv::Point(static_cast<int>(std::round(b.x)), static_cast<int>(std::round(b.y))),
                         skeleton_color,
                         2,
                         cv::LINE_AA);
            }

            for (const PoseKeypoint& keypoint : ai_snapshot.pose->keypoints) {
                if (keypoint.score < config_.pose_keypoint_score_threshold) {
                    continue;
                }
                const int x = std::clamp(static_cast<int>(std::round(keypoint.x)), 0, src.width - 1);
                const int y = std::clamp(static_cast<int>(std::round(keypoint.y)), 0, src.height - 1);
                cv::circle(bgr, cv::Point(x, y), 3, keypoint_color, -1, cv::LINE_AA);
            }
        }

        if (ai_snapshot.cup.has_value() && ai_snapshot.cup->valid) {
            std::vector<Box> cups = ai_snapshot.cup->cups;
            if (cups.empty() && ai_snapshot.cup->cup_box.w > 0.0F && ai_snapshot.cup->cup_box.h > 0.0F) {
                cups.push_back(ai_snapshot.cup->cup_box);
            }
            for (const Box& cup : cups) {
                const cv::Rect rect = clampRect(cup);
                cv::rectangle(bgr, rect, cup_color, 2);
                drawLabel(cup.label.empty() ? "cup" : cup.label, rect.x, std::max(14, rect.y - 4), cup_color);
            }
        }

        const auto postureText = [](PostureState state) {
            switch (state) {
                case PostureState::Good:
                    return "NORMAL";
                case PostureState::BadPending:
                case PostureState::BadAlert:
                    return "HUNCHBACK";
                case PostureState::Unknown:
                    return "UNKNOWN";
            }
            return "UNKNOWN";
        };

        const auto drinkText = [](DrinkState state) {
            switch (state) {
                case DrinkState::DrinkDetected:
                    return "DRINKING";
                case DrinkState::NeedRemind:
                    return "NEED_DRINK";
                case DrinkState::Normal:
                    return "NO_DRINK";
            }
            return "UNKNOWN";
        };

        std::string gesture_text = "none";
        if (ai_snapshot.gesture.has_value()) {
            gesture_text = ai_snapshot.gesture->gesture_name.empty()
                               ? toString(ai_snapshot.gesture->type)
                               : ai_snapshot.gesture->gesture_name;
        }

        const bool ai_running =
            state_.load() == SystemState::Running ||
            ai_snapshot.pose.has_value() ||
            ai_snapshot.cup.has_value() ||
            ai_snapshot.gesture.has_value();

        int text_y = 22;
        drawLabel("AI: " + std::string(ai_running ? "running" : "idle"), 10, text_y, text_color);
        text_y += 20;
        drawLabel("Frame: " + std::to_string(src.id), 10, text_y, text_color);
        text_y += 20;
        drawLabel("Gesture: " + gesture_text, 10, text_y, text_color);
        text_y += 20;
        drawLabel("Posture: " + std::string(postureText(state_snapshot.posture_state)), 10, text_y, text_color);
        text_y += 20;
        drawLabel("Drink: " + std::string(drinkText(state_snapshot.drink_state)), 10, text_y, text_color);

        cv::Mat i420;
        cv::cvtColor(bgr, i420, cv::COLOR_BGR2YUV_I420);

        const std::size_t y_size = static_cast<std::size_t>(src.width) *
                                   static_cast<std::size_t>(src.height);
        const std::size_t uv_plane_size = y_size / 4U;
        dst.id = src.id;
        dst.width = src.width;
        dst.height = src.height;
        dst.channels = 1;
        dst.format = PixelFormat::NV12;
        dst.timestamp_ms = src.timestamp_ms;
        dst.data.resize(y_size + y_size / 2U);

        const uint8_t* y_plane = i420.data;
        const uint8_t* u_plane = y_plane + y_size;
        const uint8_t* v_plane = u_plane + uv_plane_size;
        std::memcpy(dst.data.data(), y_plane, y_size);
        for (int y = 0; y < src.height / 2; ++y) {
            for (int x = 0; x < src.width / 2; ++x) {
                const std::size_t uv_index =
                    y_size + static_cast<std::size_t>(y * src.width + x * 2);
                const std::size_t src_index =
                    static_cast<std::size_t>(y * (src.width / 2) + x);
                dst.data[uv_index + 0] = u_plane[src_index];
                dst.data[uv_index + 1] = v_plane[src_index];
            }
        }
        return true;
    } catch (const cv::Exception& e) {
        std::cerr << "[Overlay] OpenCV draw failed: " << e.what() << "\n";
        return false;
    }
#else
    (void)src;
    (void)dst;
    return false;
#endif
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

int64_t VisionApp::nowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

}  // namespace rv1126b
