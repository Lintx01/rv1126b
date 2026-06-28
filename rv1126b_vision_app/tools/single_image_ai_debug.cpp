#include "Interfaces.hpp"
#include "VideoPipeline.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#if defined(RV1126B_HAS_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace {

constexpr const char* kKeypointNames[17] = {
    "nose",
    "left_eye",
    "right_eye",
    "left_ear",
    "right_ear",
    "left_shoulder",
    "right_shoulder",
    "left_elbow",
    "right_elbow",
    "left_wrist",
    "right_wrist",
    "left_hip",
    "right_hip",
    "left_knee",
    "right_knee",
    "left_ankle",
    "right_ankle",
};

rv1126b::AppConfig makeDebugConfig() {
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
    config.pose_model_path = "model/yolov8n-pose-rv1126b-i8.rknn";
    config.cup_model_path = "model/yolov8n_rv1126b_i8.rknn";

    config.gesture_input_width = 224;
    config.gesture_input_height = 224;
    config.pose_input_width = 640;
    config.pose_input_height = 640;
    config.cup_input_width = 640;
    config.cup_input_height = 640;

    config.gesture_score_threshold = 0.60F;
    config.pose_keypoint_score_threshold = 0.35F;
    config.cup_score_threshold = 0.50F;
    config.drink_distance_norm_threshold = 0.40F;
    config.drink_consecutive_hits = 3;
    return config;
}

const char* postureText(rv1126b::PostureState state) {
    switch (state) {
        case rv1126b::PostureState::Good:
            return "NORMAL";
        case rv1126b::PostureState::BadPending:
        case rv1126b::PostureState::BadAlert:
            return "HUNCHBACK";
        case rv1126b::PostureState::Unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* drinkText(rv1126b::DrinkState state) {
    switch (state) {
        case rv1126b::DrinkState::DrinkDetected:
            return "DRINKING";
        case rv1126b::DrinkState::NeedRemind:
            return "NEED_DRINK";
        case rv1126b::DrinkState::Normal:
            return "NO_DRINK";
    }
    return "UNKNOWN";
}

std::string scoreText(float score) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << score;
    return oss.str();
}

std::string boxText(const rv1126b::Box& box) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << box.x << "," << box.y << "," << box.w << "," << box.h
        << "," << std::setprecision(3) << box.score;
    return oss.str();
}

std::string boxRectText(const rv1126b::Box& box) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << box.x << "," << box.y << "," << box.w << "," << box.h;
    return oss.str();
}

std::string messageField(const std::string& message, const std::string& key) {
    const std::string prefix = key + "=";
    const std::size_t begin = message.find(prefix);
    if (begin == std::string::npos) {
        return std::string();
    }
    const std::size_t value_begin = begin + prefix.size();
    const std::size_t value_end = message.find(',', value_begin);
    if (value_end == std::string::npos) {
        return message.substr(value_begin);
    }
    return message.substr(value_begin, value_end - value_begin);
}

std::string messageFieldOr(const std::string& message, const std::string& key, const std::string& fallback) {
    const std::string value = messageField(message, key);
    return value.empty() ? fallback : value;
}

int classIdFromLabel(const std::string& label) {
    const std::string marker = "class_";
    const std::size_t begin = label.find(marker);
    if (begin == std::string::npos) {
        return -1;
    }
    const std::size_t digits_begin = begin + marker.size();
    std::size_t digits_end = digits_begin;
    while (digits_end < label.size() && std::isdigit(static_cast<unsigned char>(label[digits_end]))) {
        ++digits_end;
    }
    if (digits_end == digits_begin) {
        return -1;
    }
    try {
        return std::stoi(label.substr(digits_begin, digits_end - digits_begin));
    } catch (...) {
        return -1;
    }
}

std::string classIdText(int class_id) {
    if (class_id < 0) {
        return "unknown";
    }
    return std::to_string(class_id);
}

std::string applyPreprocessMode(rv1126b::AppConfig& config) {
    const char* preprocess_mode = std::getenv("RV_PREPROCESS_MODE");
    if (preprocess_mode != nullptr && std::strcmp(preprocess_mode, "rga") == 0) {
        config.use_rga_preprocess = true;
        config.fallback_to_opencv = false;
        return "rga";
    }
    if (preprocess_mode != nullptr && std::strcmp(preprocess_mode, "opencv") == 0) {
        config.use_rga_preprocess = false;
        config.fallback_to_opencv = true;
        return "opencv";
    }
    return "default";
}
float estimateDrinkDistanceNorm(const rv1126b::PoseResult& pose, const rv1126b::CupResult& cup, float threshold) {
    if (!pose.valid || !pose.has_person || !cup.valid || cup.cups.empty()) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const std::array<int, 3> head_indices{0, 3, 4};
    float x_sum = 0.0F;
    float y_sum = 0.0F;
    int count = 0;
    for (const int index : head_indices) {
        const auto& point = pose.keypoints[static_cast<std::size_t>(index)];
        if (point.score >= threshold) {
            x_sum += point.x;
            y_sum += point.y;
            ++count;
        }
    }
    if (count == 0) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const float head_x = x_sum / static_cast<float>(count);
    const float head_y = y_sum / static_cast<float>(count);
    const float diag = std::max(
        1.0F,
        std::sqrt(pose.person_box.w * pose.person_box.w + pose.person_box.h * pose.person_box.h));

    float best = std::numeric_limits<float>::max();
    for (const auto& box : cup.cups) {
        const float cx = box.x + box.w * 0.5F;
        const float cy = box.y + box.h * 0.5F;
        const float dx = cx - head_x;
        const float dy = cy - head_y;
        best = std::min(best, std::sqrt(dx * dx + dy * dy) / diag);
    }
    return best;
}

#if defined(RV1126B_HAS_OPENCV)
rv1126b::CropRect normalizeCropForFallback(const rv1126b::Frame& source, const rv1126b::CropRect& crop) {
    rv1126b::CropRect normalized = crop;
    if (normalized.width <= 0 || normalized.height <= 0) {
        normalized = rv1126b::CropRect{0, 0, source.width, source.height};
    }
    normalized.x = std::clamp(normalized.x, 0, std::max(0, source.width - 1));
    normalized.y = std::clamp(normalized.y, 0, std::max(0, source.height - 1));
    normalized.width = std::clamp(normalized.width, 1, source.width - normalized.x);
    normalized.height = std::clamp(normalized.height, 1, source.height - normalized.y);
    return normalized;
}

void fillTransform(
    const rv1126b::Frame& source,
    const rv1126b::CropRect& crop,
    int model_width,
    int model_height,
    rv1126b::Frame& frame) {
    frame.transform.source_width = source.width;
    frame.transform.source_height = source.height;
    frame.transform.source_crop = crop;
    frame.transform.model_width = model_width;
    frame.transform.model_height = model_height;
    frame.transform.valid = true;
}

bool makeSourceFrameFromRgb(const cv::Mat& original_rgb, uint64_t frame_id, rv1126b::Frame& frame) {
    if (original_rgb.empty() || original_rgb.channels() != 3) {
        return false;
    }
    const cv::Mat rgb = original_rgb.isContinuous() ? original_rgb : original_rgb.clone();
    frame.id = frame_id;
    frame.width = rgb.cols;
    frame.height = rgb.rows;
    frame.channels = 3;
    frame.format = rv1126b::PixelFormat::RGB888;
    frame.timestamp_ms = 0;
    frame.transform = rv1126b::PreprocessTransform{};
    frame.data.assign(rgb.data, rgb.data + rgb.total() * rgb.elemSize());
    return true;
}

bool saveRgbFrameDebug(const rv1126b::Frame& frame, const std::string& debug_path) {
    if (frame.data.empty() || frame.width <= 0 || frame.height <= 0 ||
        frame.format != rv1126b::PixelFormat::RGB888 || frame.channels < 3) {
        return false;
    }
    cv::Mat rgb(frame.height, frame.width, CV_8UC3, const_cast<uint8_t*>(frame.data.data()));
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    return cv::imwrite(debug_path, bgr);
}

bool fallbackResizeWithOpenCv(
    const rv1126b::Frame& source,
    const rv1126b::CropRect& crop,
    int width,
    int height,
    const std::string& debug_path,
    rv1126b::Frame& frame) {
    if (source.data.empty() || source.width <= 0 || source.height <= 0 ||
        source.format != rv1126b::PixelFormat::RGB888 || source.channels < 3) {
        return false;
    }

    const rv1126b::CropRect normalized = normalizeCropForFallback(source, crop);
    cv::Mat source_rgb(source.height, source.width, CV_8UC3, const_cast<uint8_t*>(source.data.data()));
    cv::Rect roi(normalized.x, normalized.y, normalized.width, normalized.height);
    cv::Mat resized_rgb;
    cv::resize(source_rgb(roi), resized_rgb, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);

    frame.id = source.id;
    frame.width = width;
    frame.height = height;
    frame.channels = 3;
    frame.format = rv1126b::PixelFormat::RGB888;
    frame.timestamp_ms = source.timestamp_ms;
    frame.data.assign(resized_rgb.data, resized_rgb.data + resized_rgb.total() * resized_rgb.elemSize());
    fillTransform(source, normalized, width, height, frame);
    (void)saveRgbFrameDebug(frame, debug_path);
    return true;
}

bool preprocessModelInput(
    rv1126b::ImageProcessor& image_processor,
    const rv1126b::Frame& source,
    const rv1126b::CropRect& crop,
    int width,
    int height,
    const char* model_name,
    const std::string& debug_path,
    rv1126b::Frame& frame) {
    if (image_processor.cropResize(source, crop, width, height, frame)) {
        (void)saveRgbFrameDebug(frame, debug_path);
        std::cout << "[SingleImage] " << model_name << "_input="
                  << width << "x" << height << " via ImageProcessor\n";
        return true;
    }

    std::cerr << "[SingleImage][WARN] " << model_name << " preprocess fallback\n";
    if (!fallbackResizeWithOpenCv(source, crop, width, height, debug_path, frame)) {
        std::cerr << "[SingleImage][ERROR] " << model_name << " preprocess failed\n";
        return false;
    }
    std::cout << "[SingleImage] " << model_name << "_input="
              << width << "x" << height << " via OpenCV fallback\n";
    return true;
}

cv::Rect toRect(const rv1126b::Box& box, int width, int height) {
    const int x0 = std::clamp(static_cast<int>(std::round(box.x)), 0, std::max(0, width - 1));
    const int y0 = std::clamp(static_cast<int>(std::round(box.y)), 0, std::max(0, height - 1));
    const int x1 = std::clamp(static_cast<int>(std::round(box.x + box.w)), 0, std::max(0, width - 1));
    const int y1 = std::clamp(static_cast<int>(std::round(box.y + box.h)), 0, std::max(0, height - 1));
    return cv::Rect(cv::Point(std::min(x0, x1), std::min(y0, y1)),
                    cv::Point(std::max(x0 + 1, x1), std::max(y0 + 1, y1)));
}

void putLabel(cv::Mat& image, const std::string& text, int x, int y, const cv::Scalar& color) {
    const cv::Point origin(std::clamp(x, 0, std::max(0, image.cols - 1)),
                           std::clamp(y, 12, std::max(12, image.rows - 1)));
    cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
    cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, 0.55, color, 1, cv::LINE_AA);
}

void drawOverlay(
    const cv::Mat& original_bgr,
    const rv1126b::GestureResult& gesture,
    const rv1126b::PoseResult& pose,
    const rv1126b::CupResult& cup,
    rv1126b::PostureState posture,
    rv1126b::DrinkState drink) {
    cv::Mat overlay = original_bgr.clone();
    const cv::Scalar person_color(0, 255, 0);
    const cv::Scalar keypoint_color(0, 255, 255);
    const cv::Scalar skeleton_color(255, 255, 0);
    const cv::Scalar cup_color(255, 160, 0);
    const cv::Scalar text_color(255, 255, 255);

    if (pose.valid && pose.has_person) {
        const cv::Rect rect = toRect(pose.person_box, overlay.cols, overlay.rows);
        cv::rectangle(overlay, rect, person_color, 2);
        putLabel(overlay, "person", rect.x, std::max(14, rect.y - 4), person_color);

        static const std::array<std::pair<int, int>, 16> skeleton{{
            {0, 1}, {0, 2}, {1, 3}, {2, 4},
            {5, 6}, {5, 7}, {7, 9}, {6, 8},
            {8, 10}, {5, 11}, {6, 12}, {11, 12},
            {11, 13}, {13, 15}, {12, 14}, {14, 16}
        }};

        auto visible = [&](int index) {
            return pose.keypoints[static_cast<std::size_t>(index)].score >= 0.35F;
        };

        for (const auto& link : skeleton) {
            if (!visible(link.first) || !visible(link.second)) {
                continue;
            }
            const auto& a = pose.keypoints[static_cast<std::size_t>(link.first)];
            const auto& b = pose.keypoints[static_cast<std::size_t>(link.second)];
            cv::line(overlay,
                     cv::Point(static_cast<int>(std::round(a.x)), static_cast<int>(std::round(a.y))),
                     cv::Point(static_cast<int>(std::round(b.x)), static_cast<int>(std::round(b.y))),
                     skeleton_color,
                     2,
                     cv::LINE_AA);
        }

        for (const auto& point : pose.keypoints) {
            if (point.score < 0.35F) {
                continue;
            }
            cv::circle(overlay,
                       cv::Point(static_cast<int>(std::round(point.x)), static_cast<int>(std::round(point.y))),
                       3,
                       keypoint_color,
                       -1,
                       cv::LINE_AA);
        }
    }

    for (const auto& box : cup.cups) {
        const cv::Rect rect = toRect(box, overlay.cols, overlay.rows);
        cv::rectangle(overlay, rect, cup_color, 2);
        putLabel(overlay, box.label.empty() ? "cup" : box.label, rect.x, std::max(14, rect.y - 4), cup_color);
    }

    const std::string gesture_name = gesture.gesture_name.empty() ? rv1126b::toString(gesture.type) : gesture.gesture_name;
    int y = 24;
    putLabel(overlay, "gesture: " + gesture_name, 10, y, text_color);
    y += 22;
    putLabel(overlay, "posture: " + std::string(postureText(posture)), 10, y, text_color);
    y += 22;
    putLabel(overlay, "cup_count: " + std::to_string(cup.cups.size()), 10, y, text_color);
    y += 22;
    putLabel(overlay, "drink_state: " + std::string(drinkText(drink)), 10, y, text_color);

    cv::imwrite("/tmp/single_image_ai_debug_overlay.jpg", overlay);
}
#endif

void printPose(const rv1126b::PoseResult& pose, rv1126b::PostureState posture) {
    std::cout << "\n========== POSE ==========\n";
    std::cout << "valid=" << (pose.valid ? "true" : "false") << "\n";
    std::cout << "has_person=" << (pose.has_person ? "true" : "false") << "\n";
    std::cout << "person_score=" << pose.person_score << "\n";
    std::cout << "person_box=" << boxText(pose.person_box) << "\n";
    std::cout << "posture=" << postureText(posture) << "\n";
    std::cout << "message=" << pose.message << "\n";
    std::cout << "keypoints:\n";
    for (std::size_t i = 0; i < pose.keypoints.size(); ++i) {
        const auto& point = pose.keypoints[i];
        std::cout << "  " << i << " " << kKeypointNames[i] << " "
                  << std::fixed << std::setprecision(1)
                  << point.x << " " << point.y << " "
                  << std::setprecision(3) << point.score << "\n";
    }
}

void printCup(const rv1126b::CupResult& cup, const rv1126b::AppConfig& config) {
    const std::string class_ids = messageFieldOr(cup.message, "class_ids", "39|40|41");
    const std::string candidates_before_nms = messageFieldOr(
        cup.message,
        "candidates_before_nms",
        messageFieldOr(cup.message, "candidates", "0"));
    const std::string kept_after_nms = messageFieldOr(
        cup.message,
        "kept_after_nms",
        std::to_string(cup.cups.size()));
    const int best_class_id = cup.cups.empty() ? -1 : classIdFromLabel(cup.cup_box.label);
    const float best_score = cup.cups.empty() ? 0.0F : cup.cup_box.score;

    std::cout << "\n========== CUP ==========\n";
    std::cout << "valid=" << (cup.valid ? "true" : "false") << "\n";
    std::cout << "cup_count=" << cup.cups.size() << "\n";
    std::cout << "threshold=" << scoreText(config.cup_score_threshold) << "\n";
    std::cout << "class_ids=" << class_ids << "\n";
    std::cout << "candidates_before_nms=" << candidates_before_nms << "\n";
    std::cout << "kept_after_nms=" << kept_after_nms << "\n";
    std::cout << "best_score=" << scoreText(best_score) << "\n";
    std::cout << "best_class_id=" << best_class_id << "\n";
    std::cout << "best_box=" << boxText(cup.cup_box) << "\n";
    std::cout << "message=" << cup.message << "\n";
    if (cup.cups.empty()) {
        std::cout << "cups: none\n";
        return;
    }

    std::cout << "cups:\n";
    for (std::size_t i = 0; i < cup.cups.size(); ++i) {
        const rv1126b::Box& box = cup.cups[i];
        std::cout << i
                  << " class_id=" << classIdText(classIdFromLabel(box.label))
                  << " score=" << scoreText(box.score)
                  << " box=" << boxRectText(box) << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " /path/to/image.jpg\n";
        return 1;
    }

#if !defined(RV1126B_HAS_OPENCV)
    std::cerr << "single_image_ai_debug requires OpenCV. Rebuild with RV1126B_ENABLE_OPENCV=ON.\n";
    return 1;
#else
    const std::string image_path = argv[1];
    cv::Mat original_bgr = cv::imread(image_path, cv::IMREAD_COLOR);
    if (original_bgr.empty()) {
        std::cerr << "failed to read image: " << image_path << "\n";
        return 1;
    }

    rv1126b::AppConfig config = makeDebugConfig();
    const std::string preprocess_mode = applyPreprocessMode(config);

    cv::Mat original_rgb;
    cv::cvtColor(original_bgr, original_rgb, cv::COLOR_BGR2RGB);

    rv1126b::Frame source_frame;
    if (!makeSourceFrameFromRgb(original_rgb, 1, source_frame)) {
        std::cerr << "failed to create source frame from image: " << image_path << "\n";
        return 1;
    }

    std::cout << "[SingleImage] image=" << image_path << "\n";
    std::cout << "[SingleImage] original=" << source_frame.width << "x" << source_frame.height << "\n";
    std::cout << "[SingleImage] preprocess_mode=" << preprocess_mode << "\n";

    rv1126b::ImageProcessor image_processor;
    if (!image_processor.open(config)) {
        std::cerr << "[SingleImage][ERROR] ImageProcessor open failed\n";
        return 1;
    }

    const rv1126b::CropRect full_frame_crop{0, 0, source_frame.width, source_frame.height};
    rv1126b::Frame gesture_input;
    rv1126b::Frame pose_input;
    rv1126b::Frame cup_input;
    if (!preprocessModelInput(
            image_processor,
            source_frame,
            full_frame_crop,
            config.gesture_input_width,
            config.gesture_input_height,
            "gesture",
            "/tmp/debug_gesture_input.jpg",
            gesture_input) ||
        !preprocessModelInput(
            image_processor,
            source_frame,
            full_frame_crop,
            config.pose_input_width,
            config.pose_input_height,
            "pose",
            "/tmp/debug_pose_input.jpg",
            pose_input) ||
        !preprocessModelInput(
            image_processor,
            source_frame,
            full_frame_crop,
            config.cup_input_width,
            config.cup_input_height,
            "cup",
            "/tmp/debug_cup_input.jpg",
            cup_input)) {
        image_processor.close();
        return 1;
    }
    rv1126b::GestureModel gesture_model;
    rv1126b::PoseModel pose_model;
    rv1126b::CupModel cup_model;
    rv1126b::PostureAnalyzer posture_analyzer;
    rv1126b::DrinkDetector drink_detector;

    gesture_model.load(config);
    pose_model.load(config);
    cup_model.load(config);
    posture_analyzer.configure(config);
    drink_detector.configure(config);

    rv1126b::GestureResult gesture = gesture_model.infer(gesture_input);
    rv1126b::PoseResult pose = pose_model.infer(pose_input);
    rv1126b::CupResult cup = cup_model.infer(cup_input);


    const rv1126b::PostureState posture = posture_analyzer.update(pose);
    rv1126b::DrinkState drink = rv1126b::DrinkState::Normal;
    for (int i = 0; i < config.drink_consecutive_hits; ++i) {
        drink = drink_detector.update(pose, cup);
    }
    const float distance_norm = estimateDrinkDistanceNorm(pose, cup, config.pose_keypoint_score_threshold);

    std::cout << std::boolalpha;
    std::cout << "========== IMAGE ==========\n";
    std::cout << "path=" << image_path << "\n";
    std::cout << "original=" << original_bgr.cols << "x" << original_bgr.rows << "\n";
    std::cout << "preprocess_mode=" << preprocess_mode << "\n";

    std::cout << "\n========== GESTURE ==========\n";
    std::cout << "valid=" << gesture.valid << "\n";
    std::cout << "type=" << rv1126b::toString(gesture.type) << "\n";
    std::cout << "name=" << gesture.gesture_name << "\n";
    std::cout << "score=" << gesture.score << "\n";
    std::cout << "message=\n";

    printPose(pose, posture);
    printCup(cup, config);

    std::cout << "\n========== DRINK ==========\n";
    std::cout << "drink_state=" << drinkText(drink) << "\n";
    std::cout << "distance_rule_result=";
    if (std::isnan(distance_norm)) {
        std::cout << "unavailable";
    } else {
        std::cout << std::fixed << std::setprecision(3) << distance_norm
                  << (distance_norm <= config.drink_distance_norm_threshold ? " <= " : " > ")
                  << config.drink_distance_norm_threshold;
    }
    std::cout << "\n";

    drawOverlay(original_bgr, gesture, pose, cup, posture, drink);
    image_processor.close();
    std::cout << "\noutputs=/tmp/debug_gesture_input.jpg,/tmp/debug_pose_input.jpg,/tmp/debug_cup_input.jpg,"
              << "/tmp/single_image_ai_debug_overlay.jpg\n";
    return 0;
#endif
}
