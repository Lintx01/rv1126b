#include "Interfaces.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <vector>

namespace rv1126b {

namespace {

constexpr std::size_t kGestureClassCount = 15;

// 手势分类模型输出 0~14 共 15 类。这里集中定义系统动作映射，后续只改 ID 即可。
constexpr std::size_t kGestureNoneClassId = 0;
constexpr std::size_t kGestureStartClassId = 1;
constexpr std::size_t kGestureStopClassId = 2;
constexpr std::size_t kGestureHeartClassId = 3;

bool looksLikeProbability(const std::vector<float>& values) {
    if (values.empty()) {
        return false;
    }

    float sum = 0.0F;
    for (float value : values) {
        if (value < 0.0F || value > 1.0F) {
            return false;
        }
        sum += value;
    }
    return std::abs(sum - 1.0F) <= 0.05F;
}

std::vector<float> softmax(const std::vector<float>& logits) {
    std::vector<float> probabilities = logits;
    if (probabilities.empty()) {
        return probabilities;
    }

    const float max_value = *std::max_element(probabilities.begin(), probabilities.end());
    float sum = 0.0F;
    for (float& value : probabilities) {
        value = std::exp(value - max_value);
        sum += value;
    }
    if (sum <= 0.0F) {
        return probabilities;
    }
    for (float& value : probabilities) {
        value /= sum;
    }
    return probabilities;
}

}  // namespace

bool GestureModel::load(const AppConfig& config) {
    loaded_ = true;
    score_threshold_ = config.gesture_score_threshold;
    fallback_mode_ = !model_.load(config.gesture_model_path);
    infer_count_ = 0;

    /*
     * 手势模型独立部署：系统在 Idle 模式下也持续运行，用于启动/停止/表情触发。
     * 输出统一为 GestureResult，便于后续接入 AppState、网页侧和日志侧。
     */
    std::cout << "[GestureModel] load: "
              << (config.gesture_model_path.empty() ? "<pending>" : config.gesture_model_path)
              << ", mode=" << (fallback_mode_ ? "fallback" : "rknn") << "\n";
    return true;
}

GestureResult GestureModel::infer(const Frame& frame) {
    GestureResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;
    result.gesture_name = "none";

    if (!loaded_) {
        return result;
    }

    ++infer_count_;

    if (!fallback_mode_) {
        std::vector<std::vector<float>> outputs;
        if (model_.run(frame, outputs)) {
            return parseOutput(frame, outputs);
        }

        std::cerr << "[GestureModel] RKNN inference failed, use fallback for frame " << frame.id << "\n";
        fallback_mode_ = true;
    }

    return fallbackInfer(frame);
}

GestureResult GestureModel::parseOutput(const Frame& frame, const std::vector<std::vector<float>>& outputs) const {
    GestureResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;
    result.gesture_name = "none";

    if (outputs.empty() || outputs.front().empty()) {
        return result;
    }

    std::vector<float> scores = outputs.front();
    if (scores.size() > kGestureClassCount) {
        scores.resize(kGestureClassCount);
    }
    if (!looksLikeProbability(scores)) {
        scores = softmax(scores);
    }

    const auto best = std::max_element(scores.begin(), scores.end());
    const std::size_t index = static_cast<std::size_t>(std::distance(scores.begin(), best));
    result.score = *best;
    if (*best < score_threshold_) {
        result.gesture_name = "class_" + std::to_string(index);
        return result;
    }

    switch (index) {
        case kGestureStartClassId:
            result.valid = true;
            result.type = GestureType::Start;
            result.gesture_name = "start";
            return result;
        case kGestureStopClassId:
            result.valid = true;
            result.type = GestureType::Stop;
            result.gesture_name = "stop";
            return result;
        case kGestureHeartClassId:
            result.valid = true;
            result.type = GestureType::Heart;
            result.gesture_name = "heart";
            return result;
        case kGestureNoneClassId:
            result.gesture_name = "none";
            return result;
        default:
            result.gesture_name = "class_" + std::to_string(index);
            return result;
    }
}

GestureResult GestureModel::fallbackInfer(const Frame& frame) {
    GestureResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;
    result.gesture_name = "none";

    // fallback 只用于无 RKNN SDK 或模型推理失败的主机自测，按推理次数触发更稳定，
    // 避免 AI 调度跳帧时错过固定 frame_id。
    if (infer_count_ == 1) {
        result.valid = true;
        result.type = GestureType::Start;
        result.score = 0.95F;
        result.gesture_name = "start";
        return result;
    }
    if (infer_count_ == 5) {
        result.valid = true;
        result.type = GestureType::Heart;
        result.score = 0.97F;
        result.gesture_name = "heart";
        return result;
    }
    if (infer_count_ == 12) {
        result.valid = true;
        result.type = GestureType::Stop;
        result.score = 0.96F;
        result.gesture_name = "stop";
        return result;
    }
    return result;
}

}  // namespace rv1126b
