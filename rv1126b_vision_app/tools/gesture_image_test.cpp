#include "Interfaces.hpp"
#include "RknnModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(RV1126B_HAS_OPENCV)
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace {

constexpr std::size_t kGestureClassCount = 15;
constexpr std::size_t kStartClassId = 6;
constexpr std::size_t kStopClassId = 5;
constexpr std::size_t kConfirmOkClassId = 10;
constexpr std::size_t kRockClassId = 12;
constexpr std::size_t kConfirmThumbClassId = 13;
constexpr const char* kDefaultGestureModelPath = "model/yolov5_gesture_rv1126b.rknn";
constexpr const char* kRgbDebugPath = "/tmp/gesture_input_rgb_debug.jpg";
constexpr const char* kBgrDebugPath = "/tmp/gesture_input_bgr_debug.jpg";

struct TopEntry {
    std::size_t class_id{0};
    float prob{0.0F};
    float logit{0.0F};
};

const char* gestureTypeToText(rv1126b::GestureType type) {
    switch (type) {
        case rv1126b::GestureType::Start:
            return "Start";
        case rv1126b::GestureType::Stop:
            return "Stop";
        case rv1126b::GestureType::Confirm:
            return "Confirm";
        case rv1126b::GestureType::Rock:
            return "Rock";
        case rv1126b::GestureType::None:
        default:
            return "None";
    }
}

rv1126b::GestureType mapClassToGesture(std::size_t class_id) {
    if (class_id == kStartClassId) {
        return rv1126b::GestureType::Start;
    }
    if (class_id == kStopClassId) {
        return rv1126b::GestureType::Stop;
    }
    if (class_id == kConfirmOkClassId || class_id == kConfirmThumbClassId) {
        return rv1126b::GestureType::Confirm;
    }
    if (class_id == kRockClassId) {
        return rv1126b::GestureType::Rock;
    }
    return rv1126b::GestureType::None;
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

    config.gesture_model_path = kDefaultGestureModelPath;
    config.gesture_score_threshold = 0.60F;
    config.gesture_input_width = 224;
    config.gesture_input_height = 224;

    return config;
}

std::string formatFloat(float value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << value;
    return oss.str();
}

bool loadImageAsRgbInput(
    const std::string& image_path,
    int input_width,
    int input_height,
    rv1126b::Frame& frame) {
#if defined(RV1126B_HAS_OPENCV)
    cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
    if (bgr.empty()) {
        std::cerr << "[GestureTest] failed to read image: " << image_path << "\n";
        return false;
    }

    cv::Mat resized_bgr;
    cv::resize(bgr, resized_bgr, cv::Size(input_width, input_height), 0, 0, cv::INTER_LINEAR);

    cv::Mat rgb;
    cv::cvtColor(resized_bgr, rgb, cv::COLOR_BGR2RGB);

    frame.id = 1;
    frame.width = input_width;
    frame.height = input_height;
    frame.channels = 3;
    frame.format = rv1126b::PixelFormat::RGB888;
    frame.timestamp_ms = 0;

    const std::size_t bytes =
        static_cast<std::size_t>(input_width) * static_cast<std::size_t>(input_height) * 3U;
    frame.data.resize(bytes);
    std::memcpy(frame.data.data(), rgb.data, bytes);

    cv::Mat debug_bgr;
    cv::cvtColor(rgb, debug_bgr, cv::COLOR_RGB2BGR);
    cv::imwrite(kRgbDebugPath, debug_bgr);

    return true;
#else
    (void)image_path;
    (void)input_width;
    (void)input_height;
    (void)frame;
    std::cerr << "[GestureTest] OpenCV is not enabled\n";
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
        std::cerr << "[GestureTest] failed to read image: " << image_path << "\n";
        return false;
    }

    cv::Mat resized_bgr;
    cv::resize(bgr, resized_bgr, cv::Size(input_width, input_height), 0, 0, cv::INTER_LINEAR);

    frame.id = 2;
    frame.width = input_width;
    frame.height = input_height;
    frame.channels = 3;
    frame.format = rv1126b::PixelFormat::BGR888;
    frame.timestamp_ms = 0;

    const std::size_t bytes =
        static_cast<std::size_t>(input_width) * static_cast<std::size_t>(input_height) * 3U;
    frame.data.resize(bytes);
    std::memcpy(frame.data.data(), resized_bgr.data, bytes);

    cv::imwrite(kBgrDebugPath, resized_bgr);

    return true;
#else
    (void)image_path;
    (void)input_width;
    (void)input_height;
    (void)frame;
    std::cerr << "[GestureTest] OpenCV is not enabled\n";
    return false;
#endif
}

bool computeTopEntries(
    const std::vector<std::vector<float>>& outputs,
    std::vector<TopEntry>& top_entries) {
    top_entries.clear();
    if (outputs.empty() || outputs.front().size() < kGestureClassCount) {
        std::cerr << "[GestureTest] invalid gesture output, outputs=" << outputs.size();
        if (!outputs.empty()) {
            std::cerr << ", output0_size=" << outputs.front().size();
        }
        std::cerr << ", expected_at_least=" << kGestureClassCount << "\n";
        return false;
    }

    const auto& logits = outputs.front();
    float max_logit = logits[0];
    for (std::size_t i = 1; i < kGestureClassCount; ++i) {
        max_logit = std::max(max_logit, logits[i]);
    }

    std::vector<float> probs(kGestureClassCount, 0.0F);
    float sum_exp = 0.0F;
    for (std::size_t i = 0; i < kGestureClassCount; ++i) {
        probs[i] = std::exp(logits[i] - max_logit);
        sum_exp += probs[i];
    }
    if (sum_exp <= 0.0F) {
        std::cerr << "[GestureTest] softmax failed, sum_exp=" << sum_exp << "\n";
        return false;
    }

    top_entries.reserve(kGestureClassCount);
    for (std::size_t i = 0; i < kGestureClassCount; ++i) {
        top_entries.push_back(TopEntry{i, probs[i] / sum_exp, logits[i]});
    }
    std::sort(
        top_entries.begin(),
        top_entries.end(),
        [](const TopEntry& lhs, const TopEntry& rhs) {
            return lhs.prob > rhs.prob;
        });
    return true;
}

std::string formatTop5(const std::vector<TopEntry>& top_entries) {
    std::ostringstream oss;
    const std::size_t count = std::min<std::size_t>(5, top_entries.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << "class_" << top_entries[i].class_id << ":" << formatFloat(top_entries[i].prob);
    }
    return oss.str();
}

bool runOneTest(
    rv1126b::RknnModel& raw_model,
    rv1126b::GestureModel& gesture_model,
    const std::string& tag,
    const rv1126b::Frame& input,
    float threshold,
    const char* debug_path) {
    std::cout << "\n========== " << tag << " ==========\n";
    std::cout << "[GestureTest] input=" << input.width << "x" << input.height
              << ", format="
              << ((input.format == rv1126b::PixelFormat::RGB888) ? "RGB888" : "BGR888")
              << "\n";

    std::vector<std::vector<float>> outputs;
    if (!raw_model.run(input, outputs)) {
        std::cerr << "[GestureTest][" << tag << "] RKNN run failed\n";
        return false;
    }

    std::vector<TopEntry> top_entries;
    if (!computeTopEntries(outputs, top_entries) || top_entries.empty()) {
        return false;
    }

    rv1126b::GestureResult result;
    try {
        result = gesture_model.infer(input);
    } catch (const std::exception& e) {
        std::cerr << "[GestureTest][" << tag << "] gesture infer exception: " << e.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "[GestureTest][" << tag << "] gesture infer unknown exception\n";
        return false;
    }

    const TopEntry& top1 = top_entries.front();
    const rv1126b::GestureType class_mapping = mapClassToGesture(top1.class_id);

    std::cout << "[GestureTest] threshold=" << formatFloat(threshold) << "\n";
    std::cout << "[GestureTest] top1=class_" << top1.class_id
              << ", prob=" << formatFloat(top1.prob)
              << ", logit=" << formatFloat(top1.logit) << "\n";
    std::cout << "[GestureTest] top5=" << formatTop5(top_entries) << "\n";
    std::cout << "[GestureTest] class_mapping=" << gestureTypeToText(class_mapping) << "\n";
    std::cout << "[GestureTest] mapped=" << gestureTypeToText(result.type)
              << ", valid=" << (result.valid ? "true" : "false")
              << ", score=" << formatFloat(result.score)
              << ", gesture_name=" << result.gesture_name << "\n";
    if (result.type != rv1126b::GestureType::None && !result.valid) {
        std::cout << "[GestureTest][WARN] mapped gesture is not None but valid=false; "
                  << "check GestureModel::parseOutput later if this matters.\n";
    }
    std::cout << "[GestureTest] saved " << debug_path << "\n";

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " /path/to/image.jpg [model/gesture_mobilenetv3_small_fp16.rknn]\n";
        return 1;
    }

    const std::string image_path = argv[1];

    rv1126b::AppConfig config = makeTestConfig();
    if (argc >= 3) {
        config.gesture_model_path = argv[2];
    }

    std::cout << "[GestureTest] image=" << image_path << "\n";
    std::cout << "[GestureTest] model=" << config.gesture_model_path << "\n";
    std::cout << "[GestureTest] input="
              << config.gesture_input_width << "x" << config.gesture_input_height << "\n";
    std::cout << "[GestureTest] threshold=" << formatFloat(config.gesture_score_threshold) << "\n";

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

    rv1126b::RknnModel raw_model;
    if (!raw_model.load(config.gesture_model_path)) {
        std::cerr << "[GestureTest] raw RKNN model load failed\n";
        return 1;
    }

    rv1126b::GestureModel gesture_model;
    if (!gesture_model.load(config)) {
        std::cerr << "[GestureTest] gesture model load failed\n";
        return 1;
    }

    bool ok = true;
    ok = runOneTest(raw_model, gesture_model, "RGB_INPUT", rgb_input, config.gesture_score_threshold, kRgbDebugPath) && ok;
    ok = runOneTest(raw_model, gesture_model, "BGR_INPUT", bgr_input, config.gesture_score_threshold, kBgrDebugPath) && ok;

    return ok ? 0 : 1;
}