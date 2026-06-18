#include "Interfaces.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

namespace rv1126b {

bool PostureDrinkModel::load(const AppConfig& config) {
    loaded_ = true;
    config_ = config;
    const bool rknn_loaded = model_.load(config.posture_drink_model_path);

    /* legacy fallback：该模型是早期“姿态+水杯”合并模型，当前阶段保留但不作为三模型架构的新接口。 */
    /* 后续主链路稳定后，可以只在 PoseModel/CupModel 不可用时继续作为兜底。 */
    std::cout << "[PostureDrinkModel] legacy fallback load: "
              << (config.posture_drink_model_path.empty() ? "<pending>" : config.posture_drink_model_path)
              << ", mode=" << (rknn_loaded ? "rknn" : "fallback") << "\n";
    return true;
}

VisionResult PostureDrinkModel::infer(const Frame& frame) {
    if (!loaded_) {
        VisionResult result;
        result.frame_id = frame.id;
        result.timestamp_ms = frame.timestamp_ms;
        result.message = "posture_drink_model_not_loaded";
        return result;
    }

    if (model_.usingRealRknn()) {
        std::vector<std::vector<float>> outputs;
        if (model_.run(frame, outputs)) {
            return parseOutput(frame, outputs);
        }
        std::cerr << "[PostureDrinkModel] RKNN inference failed, use legacy fallback for frame "
                  << frame.id << "\n";
    }

    return fallbackInfer(frame);
}

VisionResult PostureDrinkModel::parseOutput(
    const Frame& frame,
    const std::vector<std::vector<float>>& outputs) const {
    VisionResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;

    if (outputs.empty() || outputs.front().size() < 8) {
        result.message = "posture_drink_output_format_mismatch";
        return result;
    }

    const auto& values = outputs.front();

    /* legacy 输出约定：[0] bad_posture_score, [1] head_or_nose_x, [2] head_or_nose_y。 */
    /* legacy 输出约定：[3] cup_score, [4] cup_x, [5] cup_y, [6] cup_w, [7] cup_h。 */
    result.bad_posture = values[0] >= config_.bad_posture_threshold;
    result.head_or_nose = Point{values[1], values[2]};

    if (values[3] >= config_.cup_score_threshold) {
        Box cup;
        cup.label = "cup";
        cup.score = values[3];
        cup.x = values[4];
        cup.y = values[5];
        cup.w = values[6];
        cup.h = values[7];
        result.boxes.push_back(cup);
    }

    applyDrinkRule(result);

    std::ostringstream oss;
    oss << "frame=" << frame.id
        << ",bad_posture=" << result.bad_posture
        << ",drink_detected=" << result.drink_detected
        << ",boxes=" << result.boxes.size()
        << ",source=legacy_rknn";
    result.message = oss.str();
    return result;
}

VisionResult PostureDrinkModel::fallbackInfer(const Frame& frame) const {
    VisionResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;

    const bool simulated_bad_posture = (frame.id % 40 == 0);
    const bool simulated_cup = (frame.id % 25 == 0);

    result.bad_posture = simulated_bad_posture;
    result.head_or_nose = Point{
        static_cast<float>(frame.width) * 0.5F,
        static_cast<float>(frame.height) * 0.375F
    };

    if (simulated_cup) {
        Box cup;
        cup.x = static_cast<float>(frame.width) * 0.47F;
        cup.y = static_cast<float>(frame.height) * 0.44F;
        cup.w = static_cast<float>(frame.width) * 0.10F;
        cup.h = static_cast<float>(frame.height) * 0.16F;
        cup.score = 0.91F;
        cup.label = "cup";
        result.boxes.push_back(cup);
    }

    applyDrinkRule(result);

    std::ostringstream oss;
    oss << "frame=" << frame.id
        << ",bad_posture=" << result.bad_posture
        << ",drink_detected=" << result.drink_detected
        << ",boxes=" << result.boxes.size()
        << ",source=legacy_fallback";
    result.message = oss.str();
    return result;
}

void PostureDrinkModel::applyDrinkRule(VisionResult& result) const {
    result.drink_detected = false;
    result.drink_reminder = false;

    for (const Box& box : result.boxes) {
        if (box.label != "cup") {
            continue;
        }

        const Point cup_center{box.x + box.w * 0.5F, box.y + box.h * 0.5F};
        const float dx = cup_center.x - result.head_or_nose.x;
        const float dy = cup_center.y - result.head_or_nose.y;
        const float distance = std::sqrt(dx * dx + dy * dy);

        /* legacy 规则：保留早期固定像素距离判断，仅用于旧模型兜底，不代表新 DrinkDetector 方案。 */
        if (distance < config_.drink_distance_threshold) {
            result.drink_detected = true;
            result.drink_reminder = false;
            return;
        }
    }

    result.drink_reminder = !result.boxes.empty() && !result.drink_detected;
}

}  // namespace rv1126b
