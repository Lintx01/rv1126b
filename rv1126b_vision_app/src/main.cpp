#include "App.hpp"

#include <iostream>
#include <cstdlib>
#include <cstring>

int main() {
    rv1126b::AppConfig config;

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
    const char* preprocess_geometry = std::getenv("RV_PREPROCESS_GEOMETRY");

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

    if (preprocess_geometry != nullptr && std::strcmp(preprocess_geometry, "letterbox") == 0) {
        config.use_letterbox_preprocess = true;
        std::cout << "[Config] preprocess_geometry=letterbox\n";
    } else {
        config.use_letterbox_preprocess = false;
        std::cout << "[Config] preprocess_geometry=resize(default)\n";
    }

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
    config.cup_model_path = "model/yolov8n_rv1126b_i8.rknn";
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
    config.debug_ai_delay_ms = 0;

    config.enable_mqtt = false;
    config.mqtt_host = "127.0.0.1";
    config.mqtt_port = 1883;
    config.mqtt_client_id = "rv1126b-vision-node";
    config.mqtt_keepalive_seconds = 30;
    config.mqtt_reconnect_delay_ms = 1000;
    config.mqtt_max_publish_retries = 3;

    /*
     * ST7789 默认配置是占位值。
     * 实机点屏前必须根据 P16 接线确认 /dev/spidevX.Y 和 Linux GPIO 编号。
     * P16 使用 SPI1 时，设备节点可能是 /dev/spidev1.0，需要在板子上用 /dev/spidev* 实测确认。
     * 第一次点屏建议先用 8MHz 或 16MHz；确认稳定后再提高速度，不建议直接 40MHz。
     */
    config.enable_display = true;
    config.enable_lvgl_display = true;
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

    rv1126b::VisionApp app(config);
    const int rc = app.run();
    if (rc != 0) {
        std::cerr << "app start failed, check camera, image processor and model path first.\n";
    }
    return rc;
}
