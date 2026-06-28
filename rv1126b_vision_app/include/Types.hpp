#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace rv1126b {

enum class GestureType {
    None,
    Start,
    Stop,
    Confirm,
    Rock
};

enum class SystemState {
    Idle,
    Running,
    Stopping
};

enum class DeviceMode {
    Normal,
    Muted,
    Standby,
    NORMAL = Normal,
    MUTED = Muted,
    STANDBY = Standby
};

enum class PostureState {
    Unknown,
    Good,
    BadPending,
    BadAlert,
    UNKNOWN = Unknown,
    GOOD = Good,
    BAD_PENDING = BadPending,
    BAD_ALERT = BadAlert
};

enum class DrinkState {
    Normal,
    NeedRemind,
    DrinkDetected,
    NORMAL = Normal,
    NEED_REMIND = NeedRemind,
    DRINK_DETECTED = DrinkDetected
};

enum class DisplayFace {
    NormalFace,
    BadPostureFace,
    DrinkRemindFace,
    DrinkOkFace,
    GestureOkFace,
    StartFace,
    StopFace,
    ConfirmFace,
    RockFace,
    IdleClock,
    SleepFace,
    ErrorFace,
    NORMAL_FACE = NormalFace,
    BAD_POSTURE_FACE = BadPostureFace,
    DRINK_REMIND_FACE = DrinkRemindFace,
    DRINK_OK_FACE = DrinkOkFace,
    GESTURE_OK_FACE = GestureOkFace,
    START_FACE = StartFace,
    STOP_FACE = StopFace,
    CONFIRM_FACE = ConfirmFace,
    ROCK_FACE = RockFace,
    IDLE_CLOCK = IdleClock,
    SLEEP_FACE = SleepFace,
    ERROR_FACE = ErrorFace
};

enum class PixelFormat {
    RGB888,
    BGR888,
    NV12,
    H264
};

enum class WebStreamProtocol {
    WebRTC,
    WebSocket,
    Mjpeg,
    HttpFlv
};

enum class CupModelProfile {
    Coco,
    BottleBoxesOnly
};

enum class CupOutputMode {
    CocoClassAware,
    BottleBoxesOnly
};

inline const char* toString(SystemState state) {
    switch (state) {
        case SystemState::Idle:
            return "Idle";
        case SystemState::Running:
            return "Running";
        case SystemState::Stopping:
            return "Stopping";
    }
    return "Unknown";
}

inline const char* toChineseString(SystemState state) {
    switch (state) {
        case SystemState::Idle:
            return "空闲";
        case SystemState::Running:
            return "运行中";
        case SystemState::Stopping:
            return "停止中";
    }
    return "未知";
}

struct AppConfig {
    std::string camera_device{"/dev/video0"};
    int frame_width{640};
    int frame_height{480};
    int frame_channels{3};
    int target_fps{25};
    bool enable_mock_camera{false};
    int mock_camera_frame_count{0};

    int ai_input_width{320};
    int ai_input_height{320};
    int gesture_input_width{320};
    int gesture_input_height{320};
    int pose_input_width{640};
    int pose_input_height{640};
    int cup_input_width{320};
    int cup_input_height{320};
    bool use_rga_preprocess{true};
    bool fallback_to_opencv{true};

    bool enable_mpp_encoder{true};
    bool enable_mpp_decoder{false};
    bool enable_web_stream{false};
    bool enable_video_overlay{true};
    int video_overlay_max_boxes{5};
    int video_overlay_result_ttl_frames{30};
    int video_overlay_thickness{2};
    bool input_stream_is_h264{false};
    int video_bitrate_kbps{2048};
    int video_gop{25};
    WebStreamProtocol web_stream_protocol{WebStreamProtocol::WebRTC};
    std::string device_ip{"192.168.1.50"};
    std::string web_server_ip{"192.168.1.10"};
    int web_server_port{8080};
    std::string webrtc_signaling_url{"ws://192.168.1.10:8080/webrtc/signaling"};
    std::string webrtc_stun_url{"stun:stun.l.google.com:19302"};
    std::string webrtc_video_track_id{"rv1126b-h264-video"};
    std::string webrtc_result_channel{"ai-result-json"};
    uint8_t webrtc_h264_payload_type{96};
    uint32_t webrtc_video_ssrc{0x1126B001U};
    std::size_t webrtc_rtp_max_payload_size{1200};

    std::string gesture_model_path;
    std::string pose_model_path;
    std::string cup_model_path;
    CupModelProfile cup_model_profile{CupModelProfile::Coco};
    CupOutputMode cup_output_mode{CupOutputMode::CocoClassAware};
    std::size_t cup_class_count{80};
    std::vector<int> cup_class_ids{39, 40, 41};
    std::string cup_box_only_label{"bottle(box_only)"};
    int cup_box_only_class_id{-1};
    bool cup_box_only_has_score{true};
    std::string posture_drink_model_path;
    bool use_legacy_posture_drink_model{true};
    bool use_three_model_pipeline{false};
    float gesture_score_threshold{0.60F};
    int gesture_stable_required{2};
    int gesture_trigger_cooldown_ms{1500};
    bool gesture_require_release{true};
    int pose_interval_ms{150};
    int gesture_interval_ms{300};
    int cup_interval_ms{500};
    float pose_keypoint_score_threshold{0.35F};
    float bad_posture_threshold{0.60F};
    float cup_score_threshold{0.50F};
    float drink_distance_threshold{120.0F};
    float drink_distance_norm_threshold{0.40F};
    int drink_consecutive_hits{3};
    int cup_output_debug_dump_frames{1};
    int cup_output_debug_dump_values{80};
    std::size_t cup_max_output_boxes{5};
    std::size_t cup_log_max_boxes{3};
    bool cup_log_verbose_boxes{false};
    bool drink_timer_reminder_enabled{true};
    int drink_timer_interval_ms{30 * 60 * 1000};
    int drink_timer_repeat_ms{5 * 60 * 1000};
    bool drink_timer_reset_on_drink_detected{true};
    bool drink_timer_confirm_ack_enabled{true};
    int debug_ai_delay_ms{0};
    bool force_ai_running{false};

    std::string mqtt_host{"127.0.0.1"};
    bool enable_mqtt{false};
    int mqtt_port{1883};
    std::string mqtt_client_id{"rv1126b-vision-node"};
    int mqtt_keepalive_seconds{30};
    int mqtt_reconnect_delay_ms{1000};
    int mqtt_max_publish_retries{3};
    std::string mqtt_base_topic{"rv1126b"};
    int mqtt_status_interval_ms{1000};
    int mqtt_telemetry_interval_ms{2000};
    int mqtt_event_cooldown_ms{5000};
    std::string posture_alarm_topic{"rv1126b/alarm/posture"};
    std::string drink_remind_topic{"rv1126b/reminder/drink"};
    std::string result_topic{"rv1126b/vision/result"};
    std::string app_state_topic{"rv1126b/app/state"};

    bool enable_display{true};
    bool enable_lvgl_display{false};
    bool lvgl_idle_only_test{true};
    int display_timezone_offset_minutes{480};
    int display_tick_ms{5};
    int display_refresh_ms{20};
    int display_start_stop_ms{3000};
    int display_confirm_ms{3000};
    int display_rock_ms{20000};
    bool enable_perf_log{true};
    int perf_log_interval_ms{2000};
    std::string st7789_spi_device{"/dev/spidev0.0"};
    int st7789_spi_speed_hz{40000000};
    int st7789_width{240};
    int st7789_height{240};
    int st7789_rotation{0};
    bool st7789_invert_color{true};
    int st7789_gpio_dc{24};
    int st7789_gpio_reset{23};
    int st7789_gpio_backlight{25};
};


inline const char* cupModelProfileName(CupModelProfile profile) {
    switch (profile) {
        case CupModelProfile::Coco:
            return "coco";
        case CupModelProfile::BottleBoxesOnly:
            return "bottle_boxes_only";
    }
    return "unknown";
}

inline const char* cupOutputModeName(CupOutputMode mode) {
    switch (mode) {
        case CupOutputMode::CocoClassAware:
            return "class_aware";
        case CupOutputMode::BottleBoxesOnly:
            return "boxes_only";
    }
    return "unknown";
}

inline std::string cupClassIdsForConfigLog(const AppConfig& config) {
    if (config.cup_output_mode == CupOutputMode::BottleBoxesOnly || config.cup_class_ids.empty()) {
        return "none";
    }
    std::ostringstream oss;
    for (std::size_t i = 0; i < config.cup_class_ids.size(); ++i) {
        if (i > 0) {
            oss << "|";
        }
        oss << config.cup_class_ids[i];
    }
    return oss.str();
}

inline void applyCupModelProfile(AppConfig& config) {
    config.cup_box_only_label = "bottle(box_only)";
    config.cup_box_only_class_id = -1;
    config.cup_box_only_has_score = true;

    switch (config.cup_model_profile) {
        case CupModelProfile::Coco:
            config.cup_output_mode = CupOutputMode::CocoClassAware;
            config.cup_model_path = "model/yolov8n_rv1126b_i8.rknn";
            config.cup_class_count = 80;
            config.cup_class_ids = {39, 40, 41};
            break;
        case CupModelProfile::BottleBoxesOnly:
            config.cup_output_mode = CupOutputMode::BottleBoxesOnly;
            config.cup_model_path = "model/bottle_rv1126b_i8.rknn";
            config.cup_class_count = 0;
            config.cup_class_ids.clear();
            break;
    }
}

struct CropRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

struct PreprocessTransform {
    int source_width{0};
    int source_height{0};
    CropRect source_crop;
    int model_width{0};
    int model_height{0};
    bool valid{false};
};

struct Frame {
    uint64_t id{0};
    int width{0};
    int height{0};
    int channels{0};
    PixelFormat format{PixelFormat::RGB888};
    int64_t timestamp_ms{0};
    PreprocessTransform transform;
    std::vector<uint8_t> data;
};

using FramePtr = std::shared_ptr<Frame>;

struct EncodedPacket {
    uint64_t frame_id{0};
    PixelFormat codec{PixelFormat::H264};
    bool key_frame{false};
    int64_t timestamp_ms{0};
    std::vector<uint8_t> data;
};

struct RtpPacket {
    uint16_t sequence_number{0};
    uint32_t timestamp{0};
    uint32_t ssrc{0};
    bool marker{false};
    uint8_t payload_type{96};
    std::vector<uint8_t> bytes;
};

struct Point {
    float x{0.0F};
    float y{0.0F};
};

struct PoseKeypoint {
    float x{0.0F};
    float y{0.0F};
    float score{0.0F};
};

using Keypoint = PoseKeypoint;

struct Box {
    float x{0.0F};
    float y{0.0F};
    float w{0.0F};
    float h{0.0F};
    float score{0.0F};
    std::string label;
};

struct VisionResult {
    uint64_t frame_id{0};
    int64_t timestamp_ms{0};
    bool bad_posture{false};
    bool drink_detected{false};
    bool drink_reminder{false};
    Point head_or_nose;
    std::vector<Box> boxes;
    std::string message;
};

struct GestureResult {
    bool valid{false};
    GestureType type{GestureType::None};
    std::string gesture_name;
    float score{0.0F};
    uint64_t frame_id{0};
    int64_t timestamp_ms{0};
};

struct PoseResult {
    bool valid{false};
    bool has_person{false};
    uint64_t frame_id{0};
    int64_t timestamp_ms{0};
    Box person_box;
    std::array<PoseKeypoint, 17> keypoints{};
    float person_score{0.0F};
    std::string message;
};

struct CupResult {
    bool valid{false};
    uint64_t frame_id{0};
    int64_t timestamp_ms{0};
    Box cup_box;
    std::vector<Box> cups;
    std::string message;
};

struct AiResultBundle {
    std::optional<GestureResult> gesture;
    std::optional<PoseResult> pose;
    std::optional<CupResult> cup;
    uint64_t frame_id{0};
    int64_t timestamp_ms{0};
};

struct AppState {
    uint64_t frame_id{0};
    DeviceMode device_mode{DeviceMode::Normal};
    PostureState posture_state{PostureState::Unknown};
    DrinkState drink_state{DrinkState::Normal};
    DisplayFace display_face{DisplayFace::NormalFace};
    std::string gesture_name;
    bool gesture_triggered{false};
    bool posture_alert{false};
    bool drink_alert{false};
    int64_t timestamp_ms{0};
};

inline const char* toString(DeviceMode mode) {
    switch (mode) {
        case DeviceMode::Normal:
            return "normal";
        case DeviceMode::Muted:
            return "muted";
        case DeviceMode::Standby:
            return "standby";
    }
    return "unknown";
}

inline const char* toString(PostureState state) {
    switch (state) {
        case PostureState::Unknown:
            return "unknown";
        case PostureState::Good:
            return "good";
        case PostureState::BadPending:
            return "bad_pending";
        case PostureState::BadAlert:
            return "bad_alert";
    }
    return "unknown";
}

inline const char* toString(DrinkState state) {
    switch (state) {
        case DrinkState::Normal:
            return "normal";
        case DrinkState::NeedRemind:
            return "need_remind";
        case DrinkState::DrinkDetected:
            return "drink_detected";
    }
    return "unknown";
}

inline const char* toString(DisplayFace face) {
    switch (face) {
        case DisplayFace::NormalFace:
            return "normal_face";
        case DisplayFace::BadPostureFace:
            return "bad_posture_face";
        case DisplayFace::DrinkRemindFace:
            return "drink_remind_face";
        case DisplayFace::DrinkOkFace:
            return "drink_ok_face";
        case DisplayFace::GestureOkFace:
            return "gesture_ok_face";
        case DisplayFace::StartFace:
            return "start_face";
        case DisplayFace::StopFace:
            return "stop_face";
        case DisplayFace::ConfirmFace:
            return "confirm_face";
        case DisplayFace::RockFace:
            return "rock_face";
        case DisplayFace::IdleClock:
            return "idle_clock";
        case DisplayFace::SleepFace:
            return "sleep_face";
        case DisplayFace::ErrorFace:
            return "error_face";
    }
    return "unknown_face";
}

inline const char* toString(GestureType type) {
    switch (type) {
        case GestureType::None:
            return "none";
        case GestureType::Start:
            return "start";
        case GestureType::Stop:
            return "stop";
        case GestureType::Confirm:
            return "confirm";
        case GestureType::Rock:
            return "rock";
    }
    return "unknown";
}

inline std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

inline std::string appStateToJson(const AppState& state) {
    std::ostringstream oss;
    oss << "{"
        << "\"frame_id\":" << state.frame_id << ","
        << "\"timestamp_ms\":" << state.timestamp_ms << ","
        << "\"device_mode\":\"" << toString(state.device_mode) << "\","
        << "\"posture_state\":\"" << toString(state.posture_state) << "\","
        << "\"drink_state\":\"" << toString(state.drink_state) << "\","
        << "\"display_face\":\"" << toString(state.display_face) << "\","
        << "\"gesture_triggered\":" << (state.gesture_triggered ? "true" : "false") << ","
        << "\"gesture_name\":\"" << jsonEscape(state.gesture_name) << "\","
        << "\"posture_alert\":" << (state.posture_alert ? "true" : "false") << ","
        << "\"drink_alert\":" << (state.drink_alert ? "true" : "false")
        << "}";
    return oss.str();
}

inline DisplayFace selectDisplayFace(const AppState& state) {
    if (state.gesture_triggered) {
        if (state.gesture_name == "start") {
            return DisplayFace::StartFace;
        }
        if (state.gesture_name == "stop") {
            return DisplayFace::StopFace;
        }
        if (state.gesture_name == "confirm") {
            return DisplayFace::ConfirmFace;
        }
        if (state.gesture_name == "rock") {
            return DisplayFace::RockFace;
        }
        return DisplayFace::GestureOkFace;
    }

    if (state.device_mode == DeviceMode::Standby) {
        return DisplayFace::SleepFace;
    }

    if (state.drink_state == DrinkState::DrinkDetected) {
        return DisplayFace::DrinkOkFace;
    }

    if (state.drink_state == DrinkState::NeedRemind || state.drink_alert) {
        return DisplayFace::DrinkRemindFace;
    }

    if (state.posture_state == PostureState::BadAlert || state.posture_alert) {
        return DisplayFace::BadPostureFace;
    }

    return DisplayFace::NormalFace;
}

struct MqttMessage {
    std::string topic;
    std::string payload;
    int qos{0};
    bool retained{false};
};

}  // namespace rv1126b
