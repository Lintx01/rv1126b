#include "App.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

std::atomic_bool g_exit_requested{false};

void handleExitSignal(int) {
    g_exit_requested = true;
}

bool isEnabledEnvValue(const char* value) {
    if (value == nullptr) {
        return false;
    }
    return std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "on") == 0 ||
           std::strcmp(value, "yes") == 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    rv1126b::AppConfig config;
    bool display_only = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--display-only") == 0) {
            display_only = true;
        }
    }

    config.camera_device = "/dev/video23";
    config.frame_width = 640;
    config.frame_height = 480;
    config.frame_channels = 1;
    config.target_fps = 25;
    // config.enable_mock_camera = false;      
    // config.mock_camera_frame_count = 0;     
    /*
     * 普通 Linux/虚拟机无摄像头自测时可打开：

     */
    config.enable_mock_camera = false;
    config.mock_camera_frame_count = 120; 

    config.ai_input_width = 320;
    config.ai_input_height = 320;
    /*
     * 三个模型的真实输入尺寸以后以 RKNN 导出尺寸为准。
     * 这里仅作为开发阶段默认值，避免手势、姿态、水杯模型被固定到同一输入尺寸。
     */
    config.gesture_input_width = 224;
    config.gesture_input_height = 224;
    config.pose_input_width = 640;
    config.pose_input_height = 640;
    config.cup_input_width = 640;
    config.cup_input_height = 640;

    /*
     * 开发阶段默认关闭外设和网络依赖，保证没有 ST7789、MQTT broker、MPP SDK、
     * Web 服务时，主程序仍可先启动，用于调摄像头采集和 AI 架构。
     */
    config.enable_mpp_encoder = true;
    config.enable_mpp_decoder = false;
    config.enable_web_stream = true;
    config.enable_video_overlay = true;
    config.input_stream_is_h264 = false;
    config.video_bitrate_kbps = 2048;
    config.video_gop = 30;

    const char* preprocess_mode = std::getenv("RV_PREPROCESS_MODE");

    if (preprocess_mode != nullptr && std::strcmp(preprocess_mode, "rga") == 0) {
        config.use_rga_preprocess = true;
        config.fallback_to_opencv = false;
        std::cout << "[Config] preprocess_mode=rga\n";
    } else if (preprocess_mode != nullptr && std::strcmp(preprocess_mode, "opencv") == 0) {
        config.use_rga_preprocess = false;
        config.fallback_to_opencv = true;
        std::cout << "[Config] preprocess_mode=opencv\n";
    } else {
        config.use_rga_preprocess = false;
        config.fallback_to_opencv = true;
        std::cout << "[Config] preprocess_mode=opencv(default)\n";
    }

    config.force_ai_running = isEnabledEnvValue(std::getenv("RV_FORCE_AI_RUNNING"));
    std::cout << "[Config] force_ai_running="
              << (config.force_ai_running ? "true" : "false") << "\n";

    config.web_stream_protocol = rv1126b::WebStreamProtocol::HttpFlv;
    config.device_ip = "192.168.137.2";
    config.web_server_ip = "192.168.1.10";
    config.web_server_port = 8080;
    config.webrtc_signaling_url = "ws://192.168.1.10:8080/webrtc/signaling";
    config.webrtc_stun_url = "stun:stun.l.google.com:19302";
    config.webrtc_video_track_id = "rv1126b-h264-video";
    config.webrtc_result_channel = "ai-result-json";
    config.webrtc_h264_payload_type = 96;
    config.webrtc_video_ssrc = 0x1126B001U;
    config.webrtc_rtp_max_payload_size = 1200;

    config.gesture_model_path = "model/yolov5_gesture_rv1126b.rknn";
    config.gesture_score_threshold = 0.60F;

    config.pose_model_path = "model/yolov8n-pose-rv1126b-i8.rknn";
    config.cup_model_profile = rv1126b::CupModelProfile::BottleBoxesOnly;
    rv1126b::applyCupModelProfile(config);
    config.use_three_model_pipeline = true;
    config.use_legacy_posture_drink_model = false;
    config.pose_interval_ms = 150;
    config.gesture_interval_ms = 300;
    config.cup_interval_ms = 500;
    config.pose_keypoint_score_threshold = 0.35F;

    config.posture_drink_model_path = "/userdata/models/posture_drink.rknn";
    config.bad_posture_threshold = 0.60F;
    config.cup_score_threshold = 0.50F;
    config.drink_distance_threshold = 120.0F;
    config.drink_distance_norm_threshold = 0.40F;
    config.drink_consecutive_hits = 3;
    std::cout << "[Config][Cup] profile=" << rv1126b::cupModelProfileName(config.cup_model_profile)
              << ", model=" << config.cup_model_path
              << ", output_mode=" << rv1126b::cupOutputModeName(config.cup_output_mode)
              << ", class_ids=" << rv1126b::cupClassIdsForConfigLog(config);
    if (config.cup_output_mode == rv1126b::CupOutputMode::BottleBoxesOnly) {
        std::cout << ", label=" << config.cup_box_only_label;
    }
    std::cout << "\n";
    config.debug_ai_delay_ms = 0;

    config.enable_mqtt = false;
    config.mqtt_host = "127.0.0.1";
    config.mqtt_port = 1883;
    config.mqtt_client_id = "rv1126b-vision-node";
    config.mqtt_keepalive_seconds = 30;
    config.mqtt_reconnect_delay_ms = 1000;
    config.mqtt_max_publish_retries = 3;
    config.mqtt_base_topic = "rv1126b";
    config.mqtt_status_interval_ms = 1000;
    config.mqtt_telemetry_interval_ms = 2000;
    config.mqtt_event_cooldown_ms = 5000;

    /*
     * ST7789 默认配置是占位值。
     * 实机点屏前必须根据 P16 接线确认 /dev/spidevX.Y 和 Linux GPIO 编号。
     * P16 使用 SPI1 时，设备节点可能是 /dev/spidev1.0，需要在板子上用 /dev/spidev* 实测确认。
     * 第一次点屏建议先用 8MHz 或 16MHz；确认稳定后再提高速度，不建议直接 40MHz。
     */
    config.enable_display = true;
    config.enable_lvgl_display = true;
    config.display_timezone_offset_minutes = 480;
    config.display_tick_ms = 5;
    config.display_refresh_ms = 20;
    config.st7789_spi_device = "/dev/spidev1.0";
    config.st7789_spi_speed_hz = 8000000;
    config.st7789_width = 240;
    config.st7789_height = 240;
    config.st7789_rotation = 0;
    config.st7789_invert_color = true;
    config.st7789_gpio_dc = 128;
    config.st7789_gpio_reset = 23;
    config.st7789_gpio_backlight = -1;

    std::signal(SIGINT, handleExitSignal);
#ifdef SIGTERM
    std::signal(SIGTERM, handleExitSignal);
#endif

    if (display_only) {
        std::cout << "[App] display-only mode\n";
        config.lvgl_idle_only_test = true;
        rv1126b::DisplayDevice display;
        if (!display.open(config)) {
            std::cerr << "[App] display-only open failed\n";
            return 1;
        }
        display.showIdleClock();
        while (!g_exit_requested.load()) {
            display.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        display.close();
        return 0;
    }

    config.lvgl_idle_only_test = false;

    rv1126b::VisionApp app(config);
    const int rc = app.run();
    if (rc != 0) {
        std::cerr << "app start failed, check camera, image processor and model path first.\n";
    }
    return rc;
}
