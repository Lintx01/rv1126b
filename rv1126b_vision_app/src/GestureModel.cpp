#include "Interfaces.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace rv1126b {

namespace {

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

GestureResult GestureModel::parseOutput(
    const Frame& frame,
    const std::vector<std::vector<float>>& outputs) const {
    constexpr std::size_t kGestureClassCount = 15;

    constexpr std::size_t kStartClassId = 11;
    constexpr std::size_t kStopClassId = 10;
    constexpr std::size_t kHeartClassId = 12;
    constexpr std::size_t kLikeClassId = 13;

    GestureResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;
    result.type = GestureType::None;
    result.gesture_name = "none";
    result.score = 0.0F;

    if (outputs.empty() || outputs.front().size() < kGestureClassCount) {
        std::cerr << "[GestureModel] invalid classification output, outputs="
                  << outputs.size();
        if (!outputs.empty()) {
            std::cerr << ", output0_size=" << outputs.front().size();
        }
        std::cerr << ", expected_at_least=" << kGestureClassCount << "\n";
        return result;
    }

    const auto& logits = outputs.front();

    // 你的输出分数曾经出现 1.15，说明更像 logits，不是 0~1 概率。
    // 所以这里先做 softmax，再用概率和 threshold 比较。
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
        std::cerr << "[GestureModel] softmax failed, sum_exp=" << sum_exp << "\n";
        return result;
    }

    for (std::size_t i = 0; i < kGestureClassCount; ++i) {
        probs[i] /= sum_exp;
    }

    const auto best = std::max_element(probs.begin(), probs.end());

    const std::size_t class_id =
        static_cast<std::size_t>(std::distance(probs.begin(), best));

    const float best_prob = *best;
    const float best_logit = logits[class_id];

    result.score = best_prob;
    result.gesture_name = "class_" + std::to_string(class_id);

    std::vector<std::pair<float, std::size_t>> ranked;
    ranked.reserve(kGestureClassCount);
    for (std::size_t i = 0; i < kGestureClassCount; ++i) {
        ranked.emplace_back(probs[i], i);
    }

    std::sort(
        ranked.begin(),
        ranked.end(),
        [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

    static int debug_count = 0;
    ++debug_count;

    // 先每次都打印，方便你排查。确认稳定后可以改成 debug_count % 10 == 0。
    std::cout << "[GestureScores] frame=" << frame.id
              << ", top1=class_" << class_id
              << ", prob=" << best_prob
              << ", logit=" << best_logit
              << ", threshold=" << score_threshold_
              << ", top5=";

    for (std::size_t i = 0; i < std::min<std::size_t>(5, ranked.size()); ++i) {
        std::cout << "class_" << ranked[i].second
                  << ":" << ranked[i].first;
        if (i + 1 < std::min<std::size_t>(5, ranked.size())) {
            std::cout << ",";
        }
    }

    std::cout << "\n";

    if (best_prob < score_threshold_) {
        return result;
    }

    if (class_id == kStartClassId) {
        result.type = GestureType::Start;
    } else if (class_id == kStopClassId) {
        result.type = GestureType::Stop;
    } else if (class_id == kHeartClassId) {
        result.type = GestureType::Heart;
    } else if (class_id == kLikeClassId) {
        result.type = GestureType::Like;
    } else {
        result.type = GestureType::None;
    }

    std::cout << "[GestureModel] class_id=" << class_id
              << ", gesture_name=" << result.gesture_name
              << ", prob=" << best_prob
              << ", mapped=" << toString(result.type)
              << "\n";

    return result;
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
