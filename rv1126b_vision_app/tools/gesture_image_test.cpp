#include "Interfaces.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(RV1126B_HAS_OPENCV)
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace {

const char* gestureTypeToText(rv1126b::GestureType type) {
    switch (type) {
        case rv1126b::GestureType::Start:
            return "start";
        case rv1126b::GestureType::Stop:
            return "stop";
        case rv1126b::GestureType::Heart:
            return "heart";
        case rv1126b::GestureType::Like:
            return "like";
        case rv1126b::GestureType::None:
        default:
            return "none";
    }
}

rv1126b::AppConfig makeTestConfig() {
    rv1126b::AppConfig config;

    config.enable_mock_camera = false;
    config.enable_display = false;
    config.enable_mqtt = false;
    config.enable_web_stream = false;
    config.enable_mpp_decoder = false;
    config.enable_mpp_encoder = false;

    config.use_rga_preprocess = false;
    config.fallback_to_opencv = true;

    config.gesture_model_path = "model/yolov5_gesture_rv1126b.rknn";

    // 和主程序保持一致
    config.gesture_input_width = 224;
    config.gesture_input_height = 224;

    return config;
}

bool loadImageAsRgbInput(
    const std::string& image_path,
    int input_width,
    int input_height,
    rv1126b::Frame& frame) {
#if defined(RV1126B_HAS_OPENCV)
    cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
    if (bgr.empty()) {
        std::cerr << "[Test] failed to read image: " << image_path << "\n";
        return false;
    }

    cv::Mat resized_bgr;
    cv::resize(
        bgr,
        resized_bgr,
        cv::Size(input_width, input_height),
        0,
        0,
        cv::INTER_LINEAR);

    cv::Mat rgb;
    cv::cvtColor(resized_bgr, rgb, cv::COLOR_BGR2RGB);

    frame.id = 1;
    frame.width = input_width;
    frame.height = input_height;
    frame.channels = 3;
    frame.format = rv1126b::PixelFormat::RGB888;
    frame.timestamp_ms = 0;

    const std::size_t bytes =
        static_cast<std::size_t>(input_width) *
        static_cast<std::size_t>(input_height) * 3U;

    frame.data.resize(bytes);
    std::memcpy(frame.data.data(), rgb.data, bytes);

    // 保存模型真正看到的 RGB 版本图片
    cv::Mat debug_bgr;
    cv::cvtColor(rgb, debug_bgr, cv::COLOR_RGB2BGR);
    cv::imwrite("/tmp/gesture_input_rgb_debug.jpg", debug_bgr);

    return true;
#else
    (void)image_path;
    (void)input_width;
    (void)input_height;
    (void)frame;
    std::cerr << "[Test] OpenCV is not enabled\n";
    return false;
#endif
}

bool loadImageAsBgrInput(
    const std::string& image_path,
    int input_width,
    int input_height,
    rv1126b::Frame& frame) {
#if defined(RV1126B_HAS_OPENCV)
    cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
    if (bgr.empty()) {
        std::cerr << "[Test] failed to read image: " << image_path << "\n";
        return false;
    }

    cv::Mat resized_bgr;
    cv::resize(
        bgr,
        resized_bgr,
        cv::Size(input_width, input_height),
        0,
        0,
        cv::INTER_LINEAR);

    frame.id = 2;
    frame.width = input_width;
    frame.height = input_height;
    frame.channels = 3;
    frame.format = rv1126b::PixelFormat::BGR888;
    frame.timestamp_ms = 0;

    const std::size_t bytes =
        static_cast<std::size_t>(input_width) *
        static_cast<std::size_t>(input_height) * 3U;

    frame.data.resize(bytes);
    std::memcpy(frame.data.data(), resized_bgr.data, bytes);

    // 保存模型真正看到的 BGR 版本图片
    cv::imwrite("/tmp/gesture_input_bgr_debug.jpg", resized_bgr);

    return true;
#else
    (void)image_path;
    (void)input_width;
    (void)input_height;
    (void)frame;
    std::cerr << "[Test] OpenCV is not enabled\n";
    return false;
#endif
}

bool runOneTest(
    rv1126b::GestureModel& gesture_model,
    const std::string& tag,
    const rv1126b::Frame& input) {
    std::cout << "\n========== " << tag << " ==========\n";
    std::cout << "[Test] input=" << input.width << "x" << input.height
              << ", format="
              << ((input.format == rv1126b::PixelFormat::RGB888) ? "RGB888" : "BGR888")
              << "\n";

    rv1126b::GestureResult result;

    try {
        result = gesture_model.infer(input);
    } catch (const std::exception& e) {
        std::cerr << "[Test][" << tag << "] gesture infer exception: "
                  << e.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "[Test][" << tag << "] gesture infer unknown exception\n";
        return false;
    }

    std::cout << "[Test][" << tag << "] result type="
              << gestureTypeToText(result.type)
              << ", gesture_name=" << result.gesture_name
              << ", score=" << result.score
              << ", frame_id=" << result.frame_id
              << "\n";

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " /path/to/image.jpg\n";
        return 1;
    }

    const std::string image_path = argv[1];

    rv1126b::AppConfig config = makeTestConfig();

    std::cout << "[Test] image=" << image_path << "\n";
    std::cout << "[Test] model=" << config.gesture_model_path << "\n";
    std::cout << "[Test] size="
              << config.gesture_input_width << "x"
              << config.gesture_input_height << "\n";

    rv1126b::Frame rgb_input;
    rv1126b::Frame bgr_input;

    if (!loadImageAsRgbInput(
            image_path,
            config.gesture_input_width,
            config.gesture_input_height,
            rgb_input)) {
        return 1;
    }

    if (!loadImageAsBgrInput(
            image_path,
            config.gesture_input_width,
            config.gesture_input_height,
            bgr_input)) {
        return 1;
    }

    std::cout << "[Test] debug RGB image=/tmp/gesture_input_rgb_debug.jpg\n";
    std::cout << "[Test] debug BGR image=/tmp/gesture_input_bgr_debug.jpg\n";

    rv1126b::GestureModel gesture_model;
    if (!gesture_model.load(config)) {
        std::cerr << "[Test] gesture model load failed\n";
        return 1;
    }

    bool ok = true;

    ok = runOneTest(gesture_model, "RGB_INPUT", rgb_input) && ok;
    ok = runOneTest(gesture_model, "BGR_INPUT", bgr_input) && ok;

    return ok ? 0 : 1;
}