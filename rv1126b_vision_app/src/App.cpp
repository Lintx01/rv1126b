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
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace rv1126b {

namespace {

std::atomic<bool> g_signal_exit_requested{false};
constexpr int64_t kMaxPoseCupFusionDeltaMs = 300;

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

struct LatestVisionResultState {
    std::mutex mutex;
    VisionResult latest_result{};
    bool has_result{false};
};

LatestVisionResultState g_latest_vision_result_state;
std::atomic<bool> g_overlay_enabled_logged{false};
std::atomic<bool> g_overlay_format_warned{false};

void resetLatestVisionResult() {
    std::lock_guard<std::mutex> lock(g_latest_vision_result_state.mutex);
    g_latest_vision_result_state.latest_result = VisionResult{};
    g_latest_vision_result_state.has_result = false;
}

void updateLatestVisionResult(const VisionResult& result) {
    std::lock_guard<std::mutex> lock(g_latest_vision_result_state.mutex);
    g_latest_vision_result_state.latest_result = result;
    g_latest_vision_result_state.has_result = true;
}

bool getLatestVisionResult(VisionResult& result) {
    std::lock_guard<std::mutex> lock(g_latest_vision_result_state.mutex);
    if (!g_latest_vision_result_state.has_result) {
        return false;
    }
    result = g_latest_vision_result_state.latest_result;
    return true;
}

int clampInt(int value, int lo, int hi) {
    return std::max(lo, std::min(value, hi));
}

void drawRectOnNv12Y(
    Frame& frame,
    int x,
    int y,
    int w,
    int h,
    uint8_t y_value,
    int thickness) {
    if (frame.format != PixelFormat::NV12 || frame.width <= 0 || frame.height <= 0) {
        return;
    }
    if (frame.data.size() < static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height)) {
        return;
    }

    int x0 = clampInt(x, 0, frame.width - 1);
    int y0 = clampInt(y, 0, frame.height - 1);
    int x1 = clampInt(x + w - 1, 0, frame.width - 1);
    int y1 = clampInt(y + h - 1, 0, frame.height - 1);
    if (x1 < x0) {
        std::swap(x0, x1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
    }
    if ((x1 - x0 + 1) < 2 || (y1 - y0 + 1) < 2) {
        return;
    }

    const int line_width = std::max(1, thickness);
    const int stride = frame.width;
    uint8_t* y_plane = frame.data.data();

    for (int t = 0; t < line_width; ++t) {
        const int top = y0 + t;
        const int bottom = y1 - t;
        if (top > y1 || bottom < y0) {
            break;
        }
        for (int px = x0; px <= x1; ++px) {
            y_plane[top * stride + px] = y_value;
            y_plane[bottom * stride + px] = y_value;
        }
        for (int py = y0; py <= y1; ++py) {
            const int left = x0 + t;
            const int right = x1 - t;
            if (left <= x1) {
                y_plane[py * stride + left] = y_value;
            }
            if (right >= x0) {
                y_plane[py * stride + right] = y_value;
            }
        }
    }
}

bool labelContains(const Box& box, const char* token) {
    return !box.label.empty() && box.label.find(token) != std::string::npos;
}

bool isPersonOverlayBox(const Box& box) {
    return labelContains(box, "person");
}

bool isDrinkOverlayBox(const Box& box) {
    return labelContains(box, "cup") || labelContains(box, "bottle") || labelContains(box, "wine_glass");
}

std::vector<Box> selectDisplayOverlayBoxes(const VisionResult& result) {
    const Box* best_person = nullptr;
    const Box* best_drink = nullptr;

    for (const Box& box : result.boxes) {
        if (box.label.empty()) {
            continue;
        }
        if (isPersonOverlayBox(box)) {
            if (best_person == nullptr || box.score > best_person->score) {
                best_person = &box;
            }
            continue;
        }
        if (isDrinkOverlayBox(box)) {
            if (best_drink == nullptr || box.score > best_drink->score) {
                best_drink = &box;
            }
        }
    }

    std::vector<Box> selected;
    selected.reserve(2);
    if (best_person != nullptr) {
        selected.push_back(*best_person);
    }
    if (best_drink != nullptr) {
        selected.push_back(*best_drink);
    }
    return selected;
}

bool isDrawableOverlayBox(const Frame& frame, const Box& box) {
    const int w = static_cast<int>(box.w + 0.5F);
    const int h = static_cast<int>(box.h + 0.5F);
    if (w < 2 || h < 2) {
        return false;
    }
    const int x = static_cast<int>(box.x + 0.5F);
    const int y = static_cast<int>(box.y + 0.5F);
    return !(x >= frame.width || y >= frame.height || (x + w) <= 0 || (y + h) <= 0);
}

int countDrawableOverlayBoxes(const Frame& frame, const VisionResult& result, const AppConfig& config) {
    (void)config;
    int count = 0;
    for (const Box& box : selectDisplayOverlayBoxes(result)) {
        if (isDrawableOverlayBox(frame, box)) {
            ++count;
        }
    }
    return count;
}

bool applyLightweightOverlay(Frame& frame, const VisionResult& result, const AppConfig& config) {
    if (frame.format != PixelFormat::NV12) {
        if (!g_overlay_format_warned.exchange(true)) {
            std::cout << "[Overlay][WARN] lightweight overlay only supports NV12, skip\n";
        }
        return false;
    }

    if (!g_overlay_enabled_logged.exchange(true)) {
        std::cout << "[Overlay] lightweight NV12 Y-plane overlay enabled\n";
    }

    const int base_thickness = std::max(1, config.video_overlay_thickness);
    int drawn = 0;
    for (const Box& box : selectDisplayOverlayBoxes(result)) {
        const int x = static_cast<int>(box.x + 0.5F);
        const int y = static_cast<int>(box.y + 0.5F);
        const int w = static_cast<int>(box.w + 0.5F);
        const int h = static_cast<int>(box.h + 0.5F);
        if (!isDrawableOverlayBox(frame, box)) {
            continue;
        }

        const int thickness = isDrinkOverlayBox(box) ? base_thickness + 1 : base_thickness;
        drawRectOnNv12Y(frame, x, y, w, h, 235U, thickness);
        if (w > (thickness * 2 + 2) && h > (thickness * 2 + 2)) {
            drawRectOnNv12Y(frame, x + thickness, y + thickness, w - thickness * 2, h - thickness * 2, 40U, 1);
        }
        ++drawn;
    }
    return drawn > 0;
}

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

bool isTransientDisplayFace(DisplayFace face) {
    return face == DisplayFace::START_FACE || face == DisplayFace::STOP_FACE ||
           face == DisplayFace::CONFIRM_FACE || face == DisplayFace::ROCK_FACE ||
           face == DisplayFace::GESTURE_OK_FACE;
}

bool isBaseDisplayFace(DisplayFace face) {
    return face == DisplayFace::NORMAL_FACE || face == DisplayFace::IDLE_CLOCK ||
           face == DisplayFace::SLEEP_FACE || face == DisplayFace::BAD_POSTURE_FACE ||
           face == DisplayFace::DRINK_REMIND_FACE || face == DisplayFace::DRINK_OK_FACE;
}

const char* displayStateLogName(DisplayFace face) {
    switch (face) {
        case DisplayFace::START_FACE:
            return "start";
        case DisplayFace::STOP_FACE:
            return "stop";
        case DisplayFace::CONFIRM_FACE:
        case DisplayFace::GESTURE_OK_FACE:
            return "confirm";
        case DisplayFace::ROCK_FACE:
            return "rock";
        case DisplayFace::NORMAL_FACE:
            return "normal";
        case DisplayFace::IDLE_CLOCK:
            return "idle_clock";
        case DisplayFace::SLEEP_FACE:
            return "sleep";
        case DisplayFace::BAD_POSTURE_FACE:
            return "bad_posture";
        case DisplayFace::DRINK_REMIND_FACE:
            return "drink_remind";
        case DisplayFace::DRINK_OK_FACE:
            return "drink_ok";
        case DisplayFace::ERROR_FACE:
            return "error";
    }
    return "unknown";
}

int transientDisplayDurationMs(DisplayFace face, const AppConfig& config) {
    switch (face) {
        case DisplayFace::START_FACE:
        case DisplayFace::STOP_FACE:
            return std::max(0, config.display_start_stop_ms);
        case DisplayFace::CONFIRM_FACE:
        case DisplayFace::GESTURE_OK_FACE:
            return std::max(0, config.display_confirm_ms);
        case DisplayFace::ROCK_FACE:
            return std::max(0, config.display_rock_ms);
        default:
            return 0;
    }
}

DisplayFace baseDisplayFaceForState(SystemState state) {
    return state == SystemState::Running ? DisplayFace::NORMAL_FACE : DisplayFace::IDLE_CLOCK;
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

std::string boxSummary(const Box& box) {
    std::ostringstream oss;
    oss << (box.label.empty() ? "box" : box.label)
        << "(score=" << std::fixed << std::setprecision(2) << box.score
        << ",xywh=" << static_cast<int>(box.x) << ","
        << static_cast<int>(box.y) << ","
        << static_cast<int>(box.w) << ","
        << static_cast<int>(box.h) << ")";
    return oss.str();
}

std::string cupBoxesSummary(const CupResult& result, const AppConfig& config) {
    if (result.cups.empty()) {
        return "count=0,best=none";
    }

    std::ostringstream oss;
    oss << "count=" << result.cups.size()
        << ",best=" << boxSummary(result.cup_box);
    if (config.cup_log_verbose_boxes) {
        const std::size_t max_boxes = std::min(config.cup_log_max_boxes, result.cups.size());
        oss << ",boxes=";
        for (std::size_t i = 0; i < max_boxes; ++i) {
            if (i > 0) {
                oss << "|";
            }
            oss << boxSummary(result.cups[i]);
        }
        if (result.cups.size() > max_boxes) {
            oss << "|...";
        }
    }
    return oss.str();
}

int64_t absDeltaMs(int64_t lhs, int64_t rhs) {
    return lhs >= rhs ? lhs - rhs : rhs - lhs;
}

std::string formatDurationZh(int64_t duration_ms) {
    const int64_t total_seconds = (std::max<int64_t>(0, duration_ms) + 999) / 1000;
    const int64_t minutes = total_seconds / 60;
    const int64_t seconds = total_seconds % 60;
    std::ostringstream oss;
    if (minutes > 0) {
        oss << minutes << "分" << seconds << "秒";
    } else {
        oss << seconds << "秒";
    }
    return oss.str();
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
                  << "，喝水距离阈值=" << config_.drink_distance_threshold << "px"
                  << "，喝水连续命中=" << config_.drink_consecutive_hits << "\n";
        std::cout << std::fixed << std::setprecision(3)
                  << "[Config] gesture_score_threshold=" << config_.gesture_score_threshold << "\n";
        std::cout << "[Config] gesture_stable_required=" << config_.gesture_stable_required << "\n";
        std::cout << "[Config] gesture_trigger_cooldown_ms=" << config_.gesture_trigger_cooldown_ms << "\n";
        std::cout << "[Config] gesture_require_release=" << (config_.gesture_require_release ? "true" : "false") << "\n";

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
        if (config_.force_ai_running) {
            state_ = SystemState::Running;
            drink_detector_.reset();
            ai_scheduler_.reset();
            resetDrinkTimer("running_start", nowMs());
            std::cout << "[State] initial Running by RV_FORCE_AI_RUNNING\n";
        } else {
            state_ = SystemState::Idle;
            std::cout << "[State] initial Idle\n";
        }
        g_signal_exit_requested = false;
        resetMqttRuntimeState();
        resetLatestVisionResult();

        const DisplayFace initial_face = baseDisplayFaceForState(state_.load());
        if (!display_queue_.push(initial_face)) {
            std::cerr << "[DisplayState][WARN] initial display queue push failed\n";
        }
        std::cout << "[DisplayState] initial face=" << displayStateLogName(initial_face) << "\n";

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

        const Frame* frame_to_encode = item->get();
        Frame overlay_frame;
        bool overlay_applied = false;
        int overlay_box_count = 0;
        if (config_.enable_video_overlay) {
            VisionResult latest_result;
            if (getLatestVisionResult(latest_result) && latest_result.frame_id <= frame_id) {
                const uint64_t age_frames = frame_id - latest_result.frame_id;
                const uint64_t ttl_frames = static_cast<uint64_t>(std::max(0, config_.video_overlay_result_ttl_frames));
                if (age_frames <= ttl_frames) {
                    overlay_box_count = countDrawableOverlayBoxes(**item, latest_result, config_);
                    if (overlay_box_count > 0) {
                        overlay_frame = **item;
                        const auto overlay_begin = std::chrono::steady_clock::now();
                        overlay_applied = applyLightweightOverlay(overlay_frame, latest_result, config_);
                        perf_.overlay.addSample(elapsedUs(overlay_begin));
                        if (overlay_applied) {
                            frame_to_encode = &overlay_frame;
                            if ((frame_id % 300U) == 0U) {
                                std::cout << "[Overlay] lightweight enabled, display_boxes=" << overlay_box_count
                                          << ", raw_boxes=" << latest_result.boxes.size()
                                          << ", frame=" << frame_id << "\n";
                            }
                        }
                    }
                }
            }
        }

        EncodedPacket packet;
        const auto encode_begin = std::chrono::steady_clock::now();
        if (mpp_encoder_.encode(*frame_to_encode, packet)) {
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
    GestureType pending_gesture_type = GestureType::None;
    int pending_gesture_count = 0;
    GestureType locked_gesture_type = GestureType::None;
    int64_t last_gesture_trigger_ms = 0;

    auto triggerStableGesture = [&](const GestureResult& gesture_result) {
        const int required_count = std::max(1, config_.gesture_stable_required);
        const int cooldown_ms = std::max(0, config_.gesture_trigger_cooldown_ms);
        const GestureType candidate = gesture_result.valid ? gesture_result.type : GestureType::None;

        if (candidate == GestureType::None) {
            pending_gesture_type = GestureType::None;
            pending_gesture_count = 0;
            if (config_.gesture_require_release) {
                locked_gesture_type = GestureType::None;
            }
            std::cout << "[手势稳定] frame=" << gesture_result.frame_id
                      << ", candidate=none, count=0/" << required_count
                      << ", action=reset\n";
            return;
        }

        if (candidate == pending_gesture_type) {
            pending_gesture_count = std::min(pending_gesture_count + 1, required_count);
        } else {
            pending_gesture_type = candidate;
            pending_gesture_count = 1;
            if (config_.gesture_require_release && locked_gesture_type != GestureType::None && locked_gesture_type != candidate) {
                locked_gesture_type = GestureType::None;
            }
        }

        if (pending_gesture_count < required_count) {
            std::cout << "[手势稳定] frame=" << gesture_result.frame_id
                      << ", candidate=" << toString(candidate)
                      << ", count=" << pending_gesture_count << "/" << required_count
                      << ", action=waiting\n";
            return;
        }

        const int64_t now_ms = steadyMs();
        if (last_gesture_trigger_ms > 0 && (now_ms - last_gesture_trigger_ms) < cooldown_ms) {
            std::cout << "[手势稳定] frame=" << gesture_result.frame_id
                      << ", candidate=" << toString(candidate)
                      << ", count=" << pending_gesture_count << "/" << required_count
                      << ", action=cooldown\n";
            return;
        }

        if (config_.gesture_require_release && locked_gesture_type == candidate) {
            std::cout << "[手势稳定] frame=" << gesture_result.frame_id
                      << ", candidate=" << toString(candidate)
                      << ", count=" << pending_gesture_count << "/" << required_count
                      << ", action=locked_wait_release\n";
            return;
        }

        switch (candidate) {
            case GestureType::Start:
                requestStartByGesture();
                publishDisplayState(nullptr, PostureState::UNKNOWN, DrinkState::NORMAL, "start");
                break;
            case GestureType::Stop:
                requestStopByGesture();
                publishDisplayState(nullptr, PostureState::UNKNOWN, DrinkState::NORMAL, "stop");
                break;
            case GestureType::Confirm:
                acknowledgeDrinkTimerByConfirm(now_ms);
                acknowledgePostureAlertByGesture("confirm", now_ms);
                publishDisplayState(nullptr, PostureState::UNKNOWN, DrinkState::NORMAL, "confirm");
                break;
            case GestureType::Rock:
                acknowledgePostureAlertByGesture("rock", now_ms);
                publishDisplayState(nullptr, PostureState::UNKNOWN, DrinkState::NORMAL, "rock");
                break;
            case GestureType::None:
                break;
        }

        last_gesture_trigger_ms = now_ms;
        if (config_.gesture_require_release) {
            locked_gesture_type = candidate;
        }
        std::cout << "[手势触发] frame=" << gesture_result.frame_id
                  << ", type=" << toString(candidate)
                  << ", count=" << pending_gesture_count << "/" << required_count
                  << ", cooldown_ms=" << cooldown_ms
                  << ", action=trigger\n";
    };

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
        const auto ai_total_begin = std::chrono::steady_clock::now();

        if (config_.debug_ai_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.debug_ai_delay_ms));
        }

        const CropRect full_frame_crop{0, 0, frame->width, frame->height};
        auto preprocessInput = [&](const char* model_name, int width, int height, Frame& output) {
            const auto preprocess_begin = std::chrono::steady_clock::now();
            const bool ok = image_processor_.cropResize(*frame, full_frame_crop, width, height, output);
            perf_.ai_preprocess.addSample(elapsedUs(preprocess_begin));
            if (!ok) {
                std::cerr << "[AI] image preprocess failed, model=" << model_name
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

                triggerStableGesture(gesture_result);
            }
        }

        if (!running_mode) {
            perf_.ai_total.addSample(elapsedUs(ai_total_begin));
            continue;
        }

        const bool perception_tick = decision.run_pose || decision.run_cup;
        if (!perception_tick) {
            perf_.ai_total.addSample(elapsedUs(ai_total_begin));
            continue;
        }

        const auto ai_perception_begin = std::chrono::steady_clock::now();
        VisionResult result;
        PostureState posture_state = PostureState::UNKNOWN;
        DrinkState drink_state = DrinkState::NORMAL;
        DrinkState visual_drink_state = DrinkState::NORMAL;
        bool has_result_to_publish = false;
        bool need_legacy_fallback = false;

        if (config_.use_three_model_pipeline) {
            if (decision.run_pose) {
                Frame pose_input;
                if (!preprocessInput(
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
                              << ", preprocess=resize\n";
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
                              << ", frame=" << last_pose_result.frame_id
                              << ", nms_box="
                              << (last_pose_result.has_person ? boxSummary(last_pose_result.person_box) : "none")
                              << "\n";
                }
            }

            if (decision.run_cup) {
                Frame cup_input;
                if (!preprocessInput(
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
                              << ", preprocess=resize\n";
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
                              << ", frame=" << last_cup_result.frame_id
                              << ", boxes_summary=" << cupBoxesSummary(last_cup_result, config_)
                              << "\n";
                }
            }

            const bool has_valid_three_model_result =
                (has_pose_result && last_pose_result.valid) ||
                (has_cup_result && last_cup_result.valid);

            if (has_valid_three_model_result) {
                if (has_pose_result) {
                    posture_state = posture_analyzer_.update(last_pose_result);
                    std::cout << "[核心][坐姿判断] final_state=" << toString(posture_state)
                              << "(" << postureStateLabel(posture_state) << ")"
                              << ", pose_valid=" << (last_pose_result.valid ? "true" : "false")
                              << ", has_person=" << (last_pose_result.has_person ? "true" : "false")
                              << "\n";
                }
                if (has_pose_result && has_cup_result) {
                    const int64_t fusion_delta_ms =
                        absDeltaMs(last_pose_result.timestamp_ms, last_cup_result.timestamp_ms);
                    if (fusion_delta_ms <= kMaxPoseCupFusionDeltaMs) {
                        drink_state = drink_detector_.update(last_pose_result, last_cup_result);
                        std::cout << "[核心][喝水判断] final_state=" << toString(drink_state)
                                  << "(" << drinkStateLabel(drink_state) << ")"
                                  << ", pose_valid=" << (last_pose_result.valid ? "true" : "false")
                                  << ", cup_valid=" << (last_cup_result.valid ? "true" : "false")
                                  << ", cups=" << last_cup_result.cups.size()
                                  << ", fusion_delta_ms=" << fusion_delta_ms
                                  << "\n";
                    } else {
                        drink_detector_.reset();
                        drink_state = DrinkState::NORMAL;
                        std::cout << "[核心][喝水判断] final_state=" << toString(drink_state)
                                  << "(" << drinkStateLabel(drink_state) << ")"
                                  << ", reason=pose_cup_stale"
                                  << ", pose_frame=" << last_pose_result.frame_id
                                  << ", cup_frame=" << last_cup_result.frame_id
                                  << ", fusion_delta_ms=" << fusion_delta_ms
                                  << ", max_delta_ms=" << kMaxPoseCupFusionDeltaMs
                                  << "\n";
                    }
                }
                visual_drink_state = drink_state;
                const int64_t drink_timer_now_ms = nowMs();
                drink_state = updateDrinkTimerAndCombine(visual_drink_state, drink_timer_now_ms);
                std::cout << "[核心][喝水判断] timer_combined_state=" << toString(drink_state)
                          << "(" << drinkStateLabel(drink_state) << ")"
                          << ", visual_state=" << toString(visual_drink_state)
                          << ", 距离下次喝水提醒=" << drinkReminderCountdownText(drink_timer_now_ms)
                          << "\n";
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
                    visual_drink_state = DrinkState::NORMAL;
                    const int64_t drink_timer_now_ms = nowMs();
                    drink_state = updateDrinkTimerAndCombine(visual_drink_state, drink_timer_now_ms);
                    std::cout << "[核心][喝水判断] timer_combined_state=" << toString(drink_state)
                              << "(" << drinkStateLabel(drink_state) << ")"
                              << ", visual_state=" << toString(visual_drink_state)
                              << ", 距离下次喝水提醒=" << drinkReminderCountdownText(drink_timer_now_ms)
                              << "\n";
                    result = composeVisionResult(
                        pose_for_state,
                        cup_for_state,
                        PostureState::UNKNOWN,
                        drink_state);
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
                const auto legacy_begin = std::chrono::steady_clock::now();
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
                perf_.legacy_infer.addSample(elapsedUs(legacy_begin));
                posture_state = result.bad_posture ? PostureState::BAD_ALERT : PostureState::UNKNOWN;
                drink_state = result.drink_detected ? DrinkState::DRINK_DETECTED :
                              (result.drink_reminder ? DrinkState::NEED_REMIND : DrinkState::NORMAL);
                visual_drink_state = drink_state;
                const int64_t drink_timer_now_ms = nowMs();
                drink_state = updateDrinkTimerAndCombine(visual_drink_state, drink_timer_now_ms);
                std::cout << "[核心][喝水判断] timer_combined_state=" << toString(drink_state)
                          << "(" << drinkStateLabel(drink_state) << ")"
                          << ", visual_state=" << toString(visual_drink_state)
                          << ", 距离下次喝水提醒=" << drinkReminderCountdownText(drink_timer_now_ms)
                          << "\n";
                result.drink_detected = (drink_state == DrinkState::DRINK_DETECTED);
                result.drink_reminder = (drink_state == DrinkState::NEED_REMIND);
                result.message += ",drink_timer_combined=" + std::string(toString(drink_state));
                std::cout << "[AI][模型] 调用 legacy_posture_drink(旧姿态饮水), frame=" << legacy_input.id
                          << ", input=" << legacy_input.width << "x" << legacy_input.height << "\n";
                has_result_to_publish = true;
            }
        }

        if (!has_result_to_publish) {
            perf_.ai_perception.addSample(elapsedUs(ai_perception_begin));
            perf_.ai_total.addSample(elapsedUs(ai_total_begin));
            continue;
        }

        updateLatestVisionResult(result);
        web_.publishResult(result);
        enqueueAlarmMessages(result, visual_drink_state);
        publishDisplayState(&result, posture_state, drink_state, std::string());
        perf_.ai_perception.addSample(elapsedUs(ai_perception_begin));
        perf_.ai_total.addSample(elapsedUs(ai_total_begin));
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
    bool has_transient_display = false;
    int64_t transient_display_until_ms = 0;

    auto show_face = [&](DisplayFace face) {
        const auto display_begin = std::chrono::steady_clock::now();
        display_.showFace(face);
        perf_.display_show.addSample(elapsedUs(display_begin));
    };

    display_.tick();
    while (!exit_requested_) {
        const int64_t loop_now_ms = nowMs();
        if (has_transient_display && loop_now_ms >= transient_display_until_ms) {
            has_transient_display = false;
            const DisplayFace back_to = baseDisplayFaceForState(state_.load());
            std::cout << "[DisplayState] transient expired, back_to=" << displayStateLogName(back_to) << "\n";
            show_face(back_to);
        }

        auto item = display_queue_.popFor(std::chrono::milliseconds(200));
        if (item.has_value()) {
            const DisplayFace face = *item;
            const int64_t now_ms = nowMs();
            if (isTransientDisplayFace(face)) {
                const int duration_ms = transientDisplayDurationMs(face, config_);
                has_transient_display = duration_ms > 0;
                transient_display_until_ms = now_ms + duration_ms;
                std::cout << "[DisplayState] transient face=" << displayStateLogName(face)
                          << " duration_ms=" << duration_ms << "\n";
                show_face(face);
            } else if (has_transient_display && now_ms < transient_display_until_ms && isBaseDisplayFace(face)) {
                std::cout << "[DisplayState] skip face=" << displayStateLogName(face)
                          << " because transient active\n";
            } else {
                has_transient_display = false;
                show_face(face);
            }
        }
        display_.tick();
    }
}

void VisionApp::requestStartByGesture() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_.load() == SystemState::Idle) {
        const SystemState from = state_.load();
        state_ = SystemState::Running;
        posture_alert_silenced_until_ms_ = 0;
        drink_detector_.reset();
        ai_scheduler_.reset();
        resetDrinkTimer("running_start", nowMs());
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
        posture_alert_silenced_until_ms_ = 0;
        drink_detector_.reset();
        ai_scheduler_.reset();
        pauseDrinkTimer("idle_stop");
        logStateTransition(SystemState::Stopping, SystemState::Idle, "stop complete");
    }
}

void VisionApp::acknowledgePostureAlertByGesture(const char* gesture_name, int64_t now_ms) {
    const int64_t silence_ms = std::max<int64_t>(0, config_.posture_ack_silence_ms);
    if (silence_ms <= 0) {
        return;
    }
    posture_alert_silenced_until_ms_ = now_ms + silence_ms;
    std::cout << "[Posture] acknowledged by " << (gesture_name == nullptr ? "gesture" : gesture_name)
              << ", suppress_display_ms=" << silence_ms << "\n";
}

bool VisionApp::isPostureAlertSilenced(int64_t now_ms) const {
    return posture_alert_silenced_until_ms_ > now_ms;
}

void VisionApp::resetDrinkTimer(const char*, int64_t now_ms) {
    if (!config_.drink_timer_reminder_enabled) {
        return;
    }
    drink_timer_last_reset_ms_ = now_ms;
    drink_timer_last_event_ms_ = 0;
    drink_timer_active_ = false;
    drink_timer_initialized_ = true;
    std::cout << "[DrinkTimer] start/reset by running_start, interval_ms="
              << config_.drink_timer_interval_ms << "\n";
}

void VisionApp::pauseDrinkTimer(const char*) {
    if (!config_.drink_timer_reminder_enabled) {
        return;
    }
    drink_timer_active_ = false;
    drink_timer_last_event_ms_ = 0;
    drink_timer_initialized_ = false;
    std::cout << "[DrinkTimer] paused by idle_stop\n";
}

void VisionApp::clearDrinkTimerByDrinkDetected(int64_t now_ms) {
    if (!config_.drink_timer_reminder_enabled || !config_.drink_timer_reset_on_drink_detected) {
        return;
    }
    const bool should_log = drink_timer_active_ || drink_timer_last_event_ms_ != 0 ||
        !drink_timer_initialized_ || (now_ms - drink_timer_last_reset_ms_) >= 1000;
    drink_timer_active_ = false;
    drink_timer_last_reset_ms_ = now_ms;
    drink_timer_last_event_ms_ = 0;
    drink_timer_initialized_ = true;
    if (should_log) {
        std::cout << "[DrinkTimer] cleared by drink_detected, next_interval_ms="
                  << config_.drink_timer_interval_ms << "\n";
    }
}

void VisionApp::acknowledgeDrinkTimerByConfirm(int64_t now_ms) {
    if (!config_.drink_timer_reminder_enabled || !config_.drink_timer_confirm_ack_enabled ||
        !drink_timer_active_) {
        return;
    }
    drink_timer_active_ = false;
    drink_timer_last_reset_ms_ = now_ms;
    drink_timer_last_event_ms_ = 0;
    drink_timer_initialized_ = true;
    std::cout << "[DrinkTimer] acknowledged by confirm, next_interval_ms="
              << config_.drink_timer_interval_ms << "\n";
}

void VisionApp::queueDrinkTimerReminderEvent() {
    queueMqttEvent(config_, mqtt_queue_, "drink_timer_remind", "Time to drink water", false);
}

std::string VisionApp::drinkReminderCountdownText(int64_t now_ms) const {
    if (!config_.drink_timer_reminder_enabled) {
        return "未启用";
    }
    if (state_.load() != SystemState::Running || !drink_timer_initialized_) {
        return "待机暂停";
    }

    const int64_t interval_ms = std::max<int64_t>(0, config_.drink_timer_interval_ms);
    const int64_t repeat_ms = std::max<int64_t>(0, config_.drink_timer_repeat_ms);

    if (drink_timer_active_) {
        if (repeat_ms <= 0 || drink_timer_last_event_ms_ == 0) {
            return "已到期，正在提醒";
        }
        const int64_t elapsed_since_event_ms = now_ms - drink_timer_last_event_ms_;
        const int64_t remaining_repeat_ms = repeat_ms - elapsed_since_event_ms;
        if (remaining_repeat_ms <= 0) {
            return "已到期，等待重复提醒";
        }
        return "提醒中，距离重复提醒还有" + formatDurationZh(remaining_repeat_ms);
    }

    const int64_t elapsed_since_reset_ms = now_ms - drink_timer_last_reset_ms_;
    const int64_t remaining_ms = interval_ms - elapsed_since_reset_ms;
    if (remaining_ms <= 0) {
        return "即将提醒";
    }
    return "还有" + formatDurationZh(remaining_ms);
}

DrinkState VisionApp::updateDrinkTimerAndCombine(DrinkState visual_drink_state, int64_t now_ms) {
    if (!config_.drink_timer_reminder_enabled || state_.load() != SystemState::Running) {
        return visual_drink_state;
    }

    if (!drink_timer_initialized_) {
        resetDrinkTimer("running_start", now_ms);
    }

    if (visual_drink_state == DrinkState::DRINK_DETECTED) {
        clearDrinkTimerByDrinkDetected(now_ms);
        return DrinkState::DRINK_DETECTED;
    }

    const int64_t interval_ms = std::max<int64_t>(0, config_.drink_timer_interval_ms);
    const int64_t repeat_ms = std::max<int64_t>(0, config_.drink_timer_repeat_ms);
    const int64_t elapsed_since_reset_ms = now_ms - drink_timer_last_reset_ms_;

    if (!drink_timer_active_ && elapsed_since_reset_ms >= interval_ms) {
        drink_timer_active_ = true;
        drink_timer_last_event_ms_ = now_ms;
        std::cout << "[DrinkTimer] due elapsed_ms=" << elapsed_since_reset_ms
                  << ", interval_ms=" << interval_ms << "\n";
        queueDrinkTimerReminderEvent();
    } else if (drink_timer_active_ && repeat_ms > 0 &&
               (drink_timer_last_event_ms_ == 0 || (now_ms - drink_timer_last_event_ms_) >= repeat_ms)) {
        const int64_t elapsed_since_event_ms = drink_timer_last_event_ms_ == 0
                                                  ? elapsed_since_reset_ms
                                                  : now_ms - drink_timer_last_event_ms_;
        drink_timer_last_event_ms_ = now_ms;
        std::cout << "[DrinkTimer] repeat reminder elapsed_ms=" << elapsed_since_event_ms
                  << ", repeat_ms=" << repeat_ms << "\n";
        queueDrinkTimerReminderEvent();
    }

    if (drink_timer_active_) {
        return DrinkState::NEED_REMIND;
    }
    return visual_drink_state;
}

void VisionApp::enqueueAlarmMessages(const VisionResult& result, DrinkState visual_drink_state) {
    if (!config_.enable_mqtt) {
        return;
    }

    if (result.bad_posture) {
        queueMqttEvent(config_, mqtt_queue_, "bad_posture", "Please sit up", true);
    }

    if (visual_drink_state == DrinkState::NEED_REMIND) {
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
    state.posture_alert_suppressed =
        state.posture_alert && isPostureAlertSilenced(state.timestamp_ms);
    if (state.posture_alert_suppressed) {
        const int64_t remaining_ms =
            std::max<int64_t>(0, posture_alert_silenced_until_ms_ - state.timestamp_ms);
        std::cout << "[Posture] alert display suppressed, remaining_ms=" << remaining_ms << "\n";
    }

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
        } else if (gesture_name == "confirm") {
            queueMqttEvent(config_, mqtt_queue_, "gesture_confirm", "Confirm gesture detected", false);
        } else if (gesture_name == "rock") {
            queueMqttEvent(config_, mqtt_queue_, "gesture_rock", "Rock gesture detected", false);
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

    struct PerfStatSnapshot {
        double avg_ms{0.0};
        double min_ms{0.0};
        double max_ms{0.0};
    };

    auto consumeStat = [](PerfStat& stat) -> PerfStatSnapshot {
        const uint64_t count = stat.count.exchange(0, std::memory_order_relaxed);
        const uint64_t total_us = stat.total_us.exchange(0, std::memory_order_relaxed);
        const uint64_t min_us = stat.min_us.exchange(
            std::numeric_limits<uint64_t>::max(),
            std::memory_order_relaxed);
        const uint64_t max_us = stat.max_us.exchange(0, std::memory_order_relaxed);
        if (count == 0) {
            return PerfStatSnapshot{};
        }
        return PerfStatSnapshot{
            static_cast<double>(total_us) / static_cast<double>(count) / 1000.0,
            min_us == std::numeric_limits<uint64_t>::max() ? 0.0
                                                           : static_cast<double>(min_us) / 1000.0,
            static_cast<double>(max_us) / 1000.0
        };
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

    const PerfStatSnapshot ai_total = consumeStat(perf_.ai_total);
    const PerfStatSnapshot ai_perception = consumeStat(perf_.ai_perception);
    const PerfStatSnapshot ai_preprocess = consumeStat(perf_.ai_preprocess);
    const PerfStatSnapshot gesture = consumeStat(perf_.gesture_infer);
    const PerfStatSnapshot pose = consumeStat(perf_.pose_infer);
    const PerfStatSnapshot cup = consumeStat(perf_.cup_infer);
    const PerfStatSnapshot legacy = consumeStat(perf_.legacy_infer);
    const PerfStatSnapshot overlay = consumeStat(perf_.overlay);
    const PerfStatSnapshot enc = consumeStat(perf_.mpp_encode);
    const PerfStatSnapshot display = consumeStat(perf_.display_show);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << "[Perf] cam_fps=" << cam_fps
        << ",enc_fps=" << enc_fps
        << ",ai_fps=" << ai_fps;

    auto appendStat = [&oss](const char* name, const PerfStatSnapshot& stat) {
        oss << "," << name << "平均_ms=" << stat.avg_ms
            << "," << name << "最小_ms=" << stat.min_ms
            << "," << name << "最大_ms=" << stat.max_ms;
    };

    appendStat("AI总耗时", ai_total);
    appendStat("AI感知链路", ai_perception);
    appendStat("AI预处理", ai_preprocess);
    appendStat("手势推理", gesture);
    appendStat("姿态推理", pose);
    appendStat("水杯推理", cup);
    appendStat("旧模型推理", legacy);

    if (overlay.avg_ms <= 0.0) {
        oss << ",overlay_ms=NA";
    } else {
        oss << ",overlay_ms=" << overlay.avg_ms;
    }

    oss << ",enc_ms=" << enc.avg_ms
        << ",display_ms=" << display.avg_ms
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
