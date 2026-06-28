#include "PerceptionModules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rv1126b {

namespace {

constexpr std::size_t kPoseKeypointCount = 17;
constexpr std::size_t kPoseCandidateStride = 5 + kPoseKeypointCount * 3;
constexpr std::size_t kNose = 0;
constexpr std::size_t kLeftEar = 3;
constexpr std::size_t kRightEar = 4;
constexpr std::size_t kLeftShoulder = 5;
constexpr std::size_t kRightShoulder = 6;
constexpr std::size_t kYoloPoseBoxBins = 16;
constexpr std::size_t kYoloPoseBoxChannels = 4 * kYoloPoseBoxBins;
constexpr std::size_t kYoloPoseClassCount = 1;
constexpr std::size_t kYoloPoseRawChannels = kYoloPoseBoxChannels + kYoloPoseClassCount;
constexpr std::size_t kYoloPoseKeypointChannels = kPoseKeypointCount * 3;
constexpr std::size_t kYoloDetectClassCount = 80;
constexpr std::size_t kYoloDetectRawChannels = kYoloPoseBoxChannels + kYoloDetectClassCount;
constexpr std::size_t kYoloDetectDecodedStride = 4 + kYoloDetectClassCount;
constexpr std::array<int, 3> kDrinkClassIds{{39, 40, 41}};
constexpr float kPoseScoreThreshold = 0.25F;
constexpr float kPoseNmsThreshold = 0.45F;
constexpr float kCupNmsThreshold = 0.45F;
constexpr float kPi = 3.14159265358979323846F;

struct Front45Metrics {
    float head_forward_angle{0.0F};
    float head_pitch_angle{0.0F};
    float confidence{0.0F};
};

struct Front45RuleResult {
    bool bad{false};
    Front45Metrics metrics;
    std::vector<std::string> reasons;
};

struct RawPoseBox {
    Box box;
    float score{0.0F};
    std::size_t keypoint_index{0};
};

struct DetectionCandidate {
    Box box;
    int class_id{-1};
    float score{0.0F};
};

bool visible(const PoseKeypoint& point, float threshold) {
    return point.score >= threshold;
}

float degrees(float radians) {
    return radians * 180.0F / kPi;
}

float clampScore(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

Box makeBoxFromValues(float x0, float y0, float x1_or_w, float y1_or_h, int frame_width, int frame_height) {
    Box box;
    box.x = std::max(0.0F, x0);
    box.y = std::max(0.0F, y0);

    const bool likely_xyxy =
        x1_or_w > x0 && y1_or_h > y0 &&
        x1_or_w <= static_cast<float>(std::max(1, frame_width)) * 1.2F &&
        y1_or_h <= static_cast<float>(std::max(1, frame_height)) * 1.2F;

    if (likely_xyxy) {
        box.w = std::max(1.0F, x1_or_w - x0);
        box.h = std::max(1.0F, y1_or_h - y0);
    } else {
        box.w = std::max(1.0F, x1_or_w);
        box.h = std::max(1.0F, y1_or_h);
    }

    if (frame_width > 0) {
        box.x = std::min(box.x, static_cast<float>(frame_width - 1));
        box.w = std::min(box.w, static_cast<float>(frame_width) - box.x);
    }
    if (frame_height > 0) {
        box.y = std::min(box.y, static_cast<float>(frame_height - 1));
        box.h = std::min(box.h, static_cast<float>(frame_height) - box.y);
    }
    return box;
}

Box makeBoxFromCenter(float center_x, float center_y, float width, float height, int frame_width, int frame_height) {
    return makeBoxFromValues(
        center_x - width * 0.5F,
        center_y - height * 0.5F,
        std::max(1.0F, width),
        std::max(1.0F, height),
        frame_width,
        frame_height);
}

Point inverseTransformPoint(const Point& point, const Frame& frame) {
    const PreprocessTransform& transform = frame.transform;
    if (!transform.valid || transform.model_width <= 0 || transform.model_height <= 0 ||
        transform.source_crop.width <= 0 || transform.source_crop.height <= 0) {
        return point;
    }

    const float source_width = static_cast<float>(std::max(1, transform.source_width));
    const float source_height = static_cast<float>(std::max(1, transform.source_height));
    const float x_scale = static_cast<float>(transform.source_crop.width) /
                          static_cast<float>(transform.model_width);
    const float y_scale = static_cast<float>(transform.source_crop.height) /
                          static_cast<float>(transform.model_height);
    return Point{
        std::clamp(
            static_cast<float>(transform.source_crop.x) + point.x * x_scale,
            0.0F,
            source_width - 1.0F),
        std::clamp(
            static_cast<float>(transform.source_crop.y) + point.y * y_scale,
            0.0F,
            source_height - 1.0F)
    };
}

Box inverseTransformBox(const Box& box, const Frame& frame) {
    const PreprocessTransform& transform = frame.transform;
    if (!transform.valid || transform.model_width <= 0 || transform.model_height <= 0 ||
        transform.source_crop.width <= 0 || transform.source_crop.height <= 0) {
        return box;
    }

    const Point p0 = inverseTransformPoint(Point{box.x, box.y}, frame);
    const Point p1 = inverseTransformPoint(Point{box.x + box.w, box.y + box.h}, frame);
    Box transformed;
    transformed.x = p0.x;
    transformed.y = p0.y;
    transformed.w = std::max(1.0F, p1.x - p0.x);
    transformed.h = std::max(1.0F, p1.y - p0.y);
    transformed.score = box.score;
    transformed.label = box.label;
    return transformed;
}

void inverseTransformPoseResult(PoseResult& result, const Frame& frame) {
    const PreprocessTransform& transform = frame.transform;
    if (!transform.valid || transform.model_width <= 0 || transform.model_height <= 0 ||
        transform.source_crop.width <= 0 || transform.source_crop.height <= 0) {
        return;
    }

    result.person_box = inverseTransformBox(result.person_box, frame);
    for (auto& keypoint : result.keypoints) {
        const Point point = inverseTransformPoint(Point{keypoint.x, keypoint.y}, frame);
        keypoint.x = point.x;
        keypoint.y = point.y;
    }
}

void inverseTransformCupResult(CupResult& result, const Frame& frame) {
    const PreprocessTransform& transform = frame.transform;
    if (!transform.valid || transform.model_width <= 0 || transform.model_height <= 0 ||
        transform.source_crop.width <= 0 || transform.source_crop.height <= 0) {
        return;
    }

    result.cup_box = inverseTransformBox(result.cup_box, frame);
    for (auto& cup : result.cups) {
        cup = inverseTransformBox(cup, frame);
    }
}
float sigmoid(float value) {
    return 1.0F / (1.0F + std::exp(-value));
}

void softmaxInPlace(float* values, std::size_t count) {
    if (values == nullptr || count == 0) {
        return;
    }

    float max_value = values[0];
    for (std::size_t i = 1; i < count; ++i) {
        max_value = std::max(max_value, values[i]);
    }

    float sum = 0.0F;
    for (std::size_t i = 0; i < count; ++i) {
        values[i] = std::exp(values[i] - max_value);
        sum += values[i];
    }

    if (sum <= 0.0F) {
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        values[i] /= sum;
    }
}

float boxIou(const Box& lhs, const Box& rhs) {
    const float lhs_x2 = lhs.x + lhs.w;
    const float lhs_y2 = lhs.y + lhs.h;
    const float rhs_x2 = rhs.x + rhs.w;
    const float rhs_y2 = rhs.y + rhs.h;

    const float inter_x1 = std::max(lhs.x, rhs.x);
    const float inter_y1 = std::max(lhs.y, rhs.y);
    const float inter_x2 = std::min(lhs_x2, rhs_x2);
    const float inter_y2 = std::min(lhs_y2, rhs_y2);
    const float inter_w = std::max(0.0F, inter_x2 - inter_x1);
    const float inter_h = std::max(0.0F, inter_y2 - inter_y1);
    const float inter_area = inter_w * inter_h;
    const float union_area = lhs.w * lhs.h + rhs.w * rhs.h - inter_area;
    if (union_area <= 0.0F) {
        return 0.0F;
    }
    return inter_area / union_area;
}

std::vector<PoseResult> nmsPoseResults(std::vector<PoseResult> candidates, float threshold) {
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const PoseResult& lhs, const PoseResult& rhs) {
            return lhs.person_score > rhs.person_score;
        });

    std::vector<PoseResult> kept;
    for (const auto& candidate : candidates) {
        bool suppressed = false;
        for (const auto& selected : kept) {
            if (boxIou(candidate.person_box, selected.person_box) > threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            kept.push_back(candidate);
        }
    }
    return kept;
}

std::vector<DetectionCandidate> nmsDetections(std::vector<DetectionCandidate> candidates, float threshold) {
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const DetectionCandidate& lhs, const DetectionCandidate& rhs) {
            return lhs.score > rhs.score;
        });

    std::vector<DetectionCandidate> kept;
    for (const auto& candidate : candidates) {
        bool suppressed = false;
        for (const auto& selected : kept) {
            if (candidate.class_id == selected.class_id && boxIou(candidate.box, selected.box) > threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            kept.push_back(candidate);
        }
    }
    return kept;
}

void sortAndLimitDetections(std::vector<DetectionCandidate>& detections, const AppConfig& config) {
    std::sort(
        detections.begin(),
        detections.end(),
        [](const DetectionCandidate& lhs, const DetectionCandidate& rhs) {
            return lhs.score > rhs.score;
        });
    if (config.cup_max_output_boxes > 0 && detections.size() > config.cup_max_output_boxes) {
        detections.resize(config.cup_max_output_boxes);
    }
}

bool tensorGridInfo(
    const RknnTensorInfo& info,
    std::size_t value_count,
    std::size_t& channels,
    std::size_t& grid_h,
    std::size_t& grid_w) {
    if (info.dims.size() < 3) {
        return false;
    }

    grid_w = info.dims.back();
    grid_h = info.dims[info.dims.size() - 2];
    channels = info.dims[info.dims.size() - 3];
    return channels > 0 && grid_h > 0 && grid_w > 0 &&
           channels * grid_h * grid_w <= value_count;
}

float normalizeYoloScore(float value) {
    if (value >= 0.0F && value <= 1.0F) {
        return value;
    }
    return sigmoid(value);
}

void appendDecodedPoseCandidate(
    const std::vector<float>& values,
    std::size_t base,
    std::size_t step,
    int frame_width,
    int frame_height,
    std::vector<PoseResult>& candidates) {
    if (base + (kPoseCandidateStride - 1) * step >= values.size()) {
        return;
    }

    const float score = clampScore(values[base + 4 * step]);
    if (score < kPoseScoreThreshold) {
        return;
    }

    PoseResult candidate;
    candidate.valid = true;
    candidate.has_person = true;
    candidate.person_score = score;
    candidate.person_box = makeBoxFromCenter(
        values[base + 0 * step],
        values[base + 1 * step],
        values[base + 2 * step],
        values[base + 3 * step],
        frame_width,
        frame_height);
    candidate.person_box.label = "person";
    candidate.person_box.score = score;

    for (std::size_t point_index = 0; point_index < kPoseKeypointCount; ++point_index) {
        const std::size_t channel = 5 + point_index * 3;
        candidate.keypoints[point_index].x = values[base + (channel + 0) * step];
        candidate.keypoints[point_index].y = values[base + (channel + 1) * step];
        candidate.keypoints[point_index].score = clampScore(values[base + (channel + 2) * step]);
    }
    candidates.push_back(candidate);
}

void appendCupDetection(
    const Box& box,
    float score,
    int class_id,
    std::vector<DetectionCandidate>& candidates) {
    if (score < 0.0F) {
        return;
    }

    DetectionCandidate candidate;
    candidate.box = box;
    switch (class_id) {
        case 39:
            candidate.box.label = "bottle(class_39)";
            break;
        case 40:
            candidate.box.label = "wine_glass(class_40)";
            break;
        case 41:
            candidate.box.label = "cup(class_41)";
            break;
        default:
            candidate.box.label = "cup(class_unknown)";
            break;
    }
    candidate.box.score = score;
    candidate.class_id = class_id;
    candidate.score = score;
    candidates.push_back(candidate);
}

bool isDrinkClassId(int class_id) {
    return std::find(kDrinkClassIds.begin(), kDrinkClassIds.end(), class_id) != kDrinkClassIds.end();
}

std::string drinkClassIdsForLog() {
    std::ostringstream oss;
    for (std::size_t i = 0; i < kDrinkClassIds.size(); ++i) {
        if (i > 0) {
            oss << "|";
        }
        oss << kDrinkClassIds[i];
    }
    return oss.str();
}

float bestDrinkClassScore(
    const std::vector<float>& values,
    std::size_t base,
    std::size_t step,
    int& class_id) {
    float best_score = -1.0F;
    class_id = -1;
    for (int candidate_class_id : kDrinkClassIds) {
        const std::size_t index = base + (4 + static_cast<std::size_t>(candidate_class_id)) * step;
        if (index >= values.size()) {
            continue;
        }
        const float score = normalizeYoloScore(values[index]);
        if (score > best_score) {
            best_score = score;
            class_id = candidate_class_id;
        }
    }
    return best_score;
}

void appendDecodedYoloDetectionCandidate(
    const std::vector<float>& values,
    std::size_t base,
    std::size_t step,
    int frame_width,
    int frame_height,
    float score_threshold,
    std::vector<DetectionCandidate>& candidates) {
    if (base + (kYoloDetectDecodedStride - 1) * step >= values.size()) {
        return;
    }

    int class_id = -1;
    const float cup_score = bestDrinkClassScore(values, base, step, class_id);
    if (cup_score < score_threshold) {
        return;
    }

    appendCupDetection(
        makeBoxFromCenter(
            values[base + 0 * step],
            values[base + 1 * step],
            values[base + 2 * step],
            values[base + 3 * step],
            frame_width,
            frame_height),
        cup_score,
        class_id,
        candidates);
}

bool decodeYoloDetectionRawOutputs(
    const std::vector<std::vector<float>>& outputs,
    const std::vector<RknnTensorInfo>& output_infos,
    const Frame& frame,
    float score_threshold,
    std::vector<DetectionCandidate>& candidates) {
    if (outputs.size() < 3 || output_infos.size() < 3) {
        return false;
    }

    bool decoded_any_head = false;
    for (std::size_t output_index = 0; output_index < 3; ++output_index) {
        std::size_t channels = 0;
        std::size_t grid_h = 0;
        std::size_t grid_w = 0;
        if (!tensorGridInfo(output_infos[output_index], outputs[output_index].size(), channels, grid_h, grid_w) ||
            channels < kYoloDetectRawChannels) {
            return false;
        }

        decoded_any_head = true;
        const auto& values = outputs[output_index];
        const float stride_x = static_cast<float>(std::max(1, frame.width)) / static_cast<float>(grid_w);
        const float stride_y = static_cast<float>(std::max(1, frame.height)) / static_cast<float>(grid_h);
        const std::size_t grid_size = grid_h * grid_w;

        for (std::size_t y = 0; y < grid_h; ++y) {
            for (std::size_t x = 0; x < grid_w; ++x) {
                const std::size_t cell_index = y * grid_w + x;
                float cup_score = -1.0F;
                int class_id = -1;
                for (int candidate_class_id : kDrinkClassIds) {
                    const std::size_t score_index =
                        (kYoloPoseBoxChannels + static_cast<std::size_t>(candidate_class_id)) * grid_size +
                        cell_index;
                    const float candidate_score = normalizeYoloScore(values[score_index]);
                    if (candidate_score > cup_score) {
                        cup_score = candidate_score;
                        class_id = candidate_class_id;
                    }
                }
                if (cup_score < score_threshold) {
                    continue;
                }

                std::array<float, kYoloPoseBoxChannels> distances{};
                for (std::size_t channel = 0; channel < kYoloPoseBoxChannels; ++channel) {
                    distances[channel] = values[channel * grid_size + cell_index];
                }
                for (std::size_t side = 0; side < 4; ++side) {
                    softmaxInPlace(&distances[side * kYoloPoseBoxBins], kYoloPoseBoxBins);
                }

                std::array<float, 4> decoded{};
                for (std::size_t side = 0; side < 4; ++side) {
                    for (std::size_t bin = 0; bin < kYoloPoseBoxBins; ++bin) {
                        decoded[side] += distances[side * kYoloPoseBoxBins + bin] * static_cast<float>(bin);
                    }
                }

                const float x1 = (static_cast<float>(x) + 0.5F - decoded[0]) * stride_x;
                const float y1 = (static_cast<float>(y) + 0.5F - decoded[1]) * stride_y;
                const float x2 = (static_cast<float>(x) + 0.5F + decoded[2]) * stride_x;
                const float y2 = (static_cast<float>(y) + 0.5F + decoded[3]) * stride_y;

                appendCupDetection(
                    makeBoxFromValues(x1, y1, x2, y2, frame.width, frame.height),
                    cup_score,
                    class_id,
                    candidates);
            }
        }
    }

    return decoded_any_head;
}

bool decodeYoloDetectionSplitOutputs(
    const std::vector<std::vector<float>>& outputs,
    const std::vector<RknnTensorInfo>& output_infos,
    const Frame& frame,
    float score_threshold,
    std::vector<DetectionCandidate>& candidates) {
    if (outputs.size() < 3 || output_infos.size() < 3) {
        return false;
    }

    bool decoded_any_head = false;
    std::vector<bool> used(outputs.size(), false);

    for (std::size_t box_index = 0; box_index < outputs.size(); ++box_index) {
        std::size_t box_channels = 0;
        std::size_t grid_h = 0;
        std::size_t grid_w = 0;
        if (!tensorGridInfo(output_infos[box_index], outputs[box_index].size(), box_channels, grid_h, grid_w) ||
            box_channels < kYoloPoseBoxChannels) {
            continue;
        }

        std::size_t class_index = outputs.size();
        std::size_t object_index = outputs.size();

        for (std::size_t candidate_index = 0; candidate_index < outputs.size(); ++candidate_index) {
            if (candidate_index == box_index) {
                continue;
            }

            std::size_t channels = 0;
            std::size_t candidate_grid_h = 0;
            std::size_t candidate_grid_w = 0;
            if (!tensorGridInfo(
                    output_infos[candidate_index],
                    outputs[candidate_index].size(),
                    channels,
                    candidate_grid_h,
                    candidate_grid_w) ||
                candidate_grid_h != grid_h ||
                candidate_grid_w != grid_w) {
                continue;
            }

            if (channels >= kYoloDetectClassCount && class_index == outputs.size()) {
                class_index = candidate_index;
            } else if (channels == 1 && object_index == outputs.size()) {
                object_index = candidate_index;
            }
        }

        if (class_index == outputs.size() || object_index == outputs.size() ||
            used[box_index] || used[class_index] || used[object_index]) {
            continue;
        }

        used[box_index] = true;
        used[class_index] = true;
        used[object_index] = true;
        decoded_any_head = true;

        const auto& box_values = outputs[box_index];
        const auto& class_values = outputs[class_index];
        const auto& object_values = outputs[object_index];
        const std::size_t grid_size = grid_h * grid_w;
        const float stride_x = static_cast<float>(std::max(1, frame.width)) / static_cast<float>(grid_w);
        const float stride_y = static_cast<float>(std::max(1, frame.height)) / static_cast<float>(grid_h);

        for (std::size_t y = 0; y < grid_h; ++y) {
            for (std::size_t x = 0; x < grid_w; ++x) {
                const std::size_t cell_index = y * grid_w + x;
                float class_score = -1.0F;
                int class_id = -1;
                for (int candidate_class_id : kDrinkClassIds) {
                    const float candidate_score =
                        normalizeYoloScore(
                            class_values[static_cast<std::size_t>(candidate_class_id) * grid_size + cell_index]);
                    if (candidate_score > class_score) {
                        class_score = candidate_score;
                        class_id = candidate_class_id;
                    }
                }
                const float object_score = normalizeYoloScore(object_values[cell_index]);
                const float cup_score = class_score * object_score;
                if (cup_score < score_threshold) {
                    continue;
                }

                std::array<float, kYoloPoseBoxChannels> distances{};
                for (std::size_t channel = 0; channel < kYoloPoseBoxChannels; ++channel) {
                    distances[channel] = box_values[channel * grid_size + cell_index];
                }
                for (std::size_t side = 0; side < 4; ++side) {
                    softmaxInPlace(&distances[side * kYoloPoseBoxBins], kYoloPoseBoxBins);
                }

                std::array<float, 4> decoded{};
                for (std::size_t side = 0; side < 4; ++side) {
                    for (std::size_t bin = 0; bin < kYoloPoseBoxBins; ++bin) {
                        decoded[side] += distances[side * kYoloPoseBoxBins + bin] * static_cast<float>(bin);
                    }
                }

                const float x1 = (static_cast<float>(x) + 0.5F - decoded[0]) * stride_x;
                const float y1 = (static_cast<float>(y) + 0.5F - decoded[1]) * stride_y;
                const float x2 = (static_cast<float>(x) + 0.5F + decoded[2]) * stride_x;
                const float y2 = (static_cast<float>(y) + 0.5F + decoded[3]) * stride_y;

                appendCupDetection(
                    makeBoxFromValues(x1, y1, x2, y2, frame.width, frame.height),
                    cup_score,
                    class_id,
                    candidates);
            }
        }
    }

    return decoded_any_head;
}

bool decodeYoloDetectionDecodedOutput(
    const std::vector<std::vector<float>>& outputs,
    const std::vector<RknnTensorInfo>& output_infos,
    const Frame& frame,
    float score_threshold,
    std::vector<DetectionCandidate>& candidates) {
    bool decoded_any_output = false;
    for (std::size_t output_index = 0; output_index < outputs.size(); ++output_index) {
        const auto& values = outputs[output_index];
        if (values.size() < kYoloDetectDecodedStride || values.size() % kYoloDetectDecodedStride != 0) {
            continue;
        }

        decoded_any_output = true;
        const std::size_t candidate_count = values.size() / kYoloDetectDecodedStride;
        bool prefer_channel_major = false;
        if (output_index < output_infos.size() && !output_infos[output_index].dims.empty()) {
            const auto& dims = output_infos[output_index].dims;
            prefer_channel_major = dims.back() != kYoloDetectDecodedStride &&
                                   std::find(dims.begin(), dims.end(), kYoloDetectDecodedStride) != dims.end();
        }

        // 兼容 [1, N, 84]：每个候选连续存放 cx, cy, w, h 和 80 类分数。
        if (!prefer_channel_major) {
            for (std::size_t index = 0; index < candidate_count; ++index) {
                appendDecodedYoloDetectionCandidate(
                    values,
                    index * kYoloDetectDecodedStride,
                    1,
                    frame.width,
                    frame.height,
                    score_threshold,
                    candidates);
            }
        }

        // 兼容 [1, 84, N]：同一通道的所有候选连续存放。
        if (prefer_channel_major || candidate_count > 1) {
            for (std::size_t index = 0; index < candidate_count; ++index) {
                appendDecodedYoloDetectionCandidate(
                    values,
                    index,
                    candidate_count,
                    frame.width,
                    frame.height,
                    score_threshold,
                    candidates);
            }
        }
    }
    return decoded_any_output;
}

bool decodePostprocessedCupOutput(
    const std::vector<std::vector<float>>& outputs,
    const Frame& frame,
    float score_threshold,
    std::vector<DetectionCandidate>& candidates) {
    bool decoded_any_output = false;
    for (const auto& values : outputs) {
        if (values.size() < 5) {
            continue;
        }

        const std::size_t stride = (values.size() % 6 == 0) ? 6U : 5U;
        if (values.size() % stride != 0) {
            continue;
        }

        decoded_any_output = true;
        const std::size_t candidate_count = values.size() / stride;
        for (std::size_t index = 0; index < candidate_count; ++index) {
            const std::size_t offset = index * stride;
            const float score = normalizeYoloScore(values[offset + 4]);
            if (score < score_threshold) {
                continue;
            }

            if (stride == 6) {
                const int class_id = static_cast<int>(std::lround(values[offset + 5]));
                if (!isDrinkClassId(class_id)) {
                    continue;
                }
            }

            appendCupDetection(
                makeBoxFromValues(
                    values[offset + 0],
                    values[offset + 1],
                    values[offset + 2],
                    values[offset + 3],
                    frame.width,
                    frame.height),
                score,
                stride == 6 ? static_cast<int>(std::lround(values[offset + 5])) : kDrinkClassIds.back(),
                candidates);
        }
    }
    return decoded_any_output;
}

struct BottleBoxesOnlyDecodeInfo {
    bool decoded{false};
    std::string format;
    std::string score_source;
    std::size_t anchors{0};
};

Box makeBottleBoxOnlyBox(float a, float b, float c, float d, float score, const Frame& frame) {
    const int frame_width = std::max(1, frame.width);
    const int frame_height = std::max(1, frame.height);
    const bool normalized =
        a >= 0.0F && a <= 1.5F && b >= 0.0F && b <= 1.5F &&
        c >= 0.0F && c <= 1.5F && d >= 0.0F && d <= 1.5F;

    if (normalized) {
        a *= static_cast<float>(frame_width);
        b *= static_cast<float>(frame_height);
        c *= static_cast<float>(frame_width);
        d *= static_cast<float>(frame_height);
    }

    Box box = makeBoxFromCenter(a, b, c, d, frame_width, frame_height);
    box.score = score;
    return box;
}

void appendBottleBoxesOnlyDetection(
    const Box& box,
    float score,
    const AppConfig& config,
    std::vector<DetectionCandidate>& candidates) {
    if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.w) ||
        !std::isfinite(box.h) || !std::isfinite(score) || box.w < 2.0F || box.h < 2.0F) {
        return;
    }

    DetectionCandidate candidate;
    candidate.box = box;
    candidate.box.score = score;
    candidate.box.label = config.cup_box_only_label;
    candidate.class_id = config.cup_box_only_class_id;
    candidate.score = score;
    candidates.push_back(candidate);
}

bool isBottleChannelFirst1x5x8400(const RknnTensorInfo& info, const std::vector<float>& values, std::size_t& anchors) {
    if (info.n_dims != 3 || info.dims.size() != 3 || values.empty()) {
        return false;
    }
    const bool shape_1x5x8400 = info.dims[0] == 1U && info.dims[1] == 5U && info.dims[2] == 8400U;
    if (!shape_1x5x8400) {
        return false;
    }
    anchors = static_cast<std::size_t>(info.dims[2]);
    return values.size() >= 5U * anchors;
}

void decodeBottleBoxesOnlyChannelFirst(
    const std::vector<float>& values,
    std::size_t anchors,
    const Frame& frame,
    const AppConfig& config,
    std::vector<DetectionCandidate>& candidates) {
    for (std::size_t index = 0; index < anchors; ++index) {
        const float cx = values[0 * anchors + index];
        const float cy = values[1 * anchors + index];
        const float w = values[2 * anchors + index];
        const float h = values[3 * anchors + index];
        const float score = normalizeYoloScore(values[4 * anchors + index]);
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(w) || !std::isfinite(h) ||
            !std::isfinite(score) || w <= 0.0F || h <= 0.0F || score < config.cup_score_threshold) {
            continue;
        }
        appendBottleBoxesOnlyDetection(
            makeBottleBoxOnlyBox(cx, cy, w, h, score, frame),
            score,
            config,
            candidates);
    }
}

void decodeBottleBoxesOnlyFlat(
    const std::vector<float>& values,
    std::size_t stride,
    const Frame& frame,
    const AppConfig& config,
    std::vector<DetectionCandidate>& candidates) {
    if (stride != 4 && stride != 5) {
        return;
    }
    const std::size_t candidate_count = values.size() / stride;
    for (std::size_t index = 0; index < candidate_count; ++index) {
        const std::size_t offset = index * stride;
        const float score = stride == 5 ? normalizeYoloScore(values[offset + 4]) : 1.0F;
        if (score < config.cup_score_threshold) {
            continue;
        }
        appendBottleBoxesOnlyDetection(
            makeBottleBoxOnlyBox(
                values[offset + 0],
                values[offset + 1],
                values[offset + 2],
                values[offset + 3],
                score,
                frame),
            score,
            config,
            candidates);
    }
}


bool isNearOne(float value) {
    return std::isfinite(value) && std::fabs(value - 1.0F) <= 1.0e-4F;
}

void dumpStrideRows(const std::vector<float>& values, std::size_t stride, std::size_t max_rows) {
    if (stride == 0 || values.size() < stride || values.size() % stride != 0) {
        return;
    }
    const std::size_t rows = std::min(max_rows, values.size() / stride);
    std::cout << "[CupModel][OutputDump] out0 as_stride" << stride << "_first" << rows << ":\n";
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t offset = row * stride;
        std::cout << "  row" << row << ":";
        for (std::size_t col = 0; col < stride; ++col) {
            const char name = static_cast<char>('a' + static_cast<char>(col));
            std::cout << " " << name << "=" << values[offset + col];
        }
        std::cout << "\n";
    }
}

void dumpBottleBoxesOnlyOutputs(const std::vector<std::vector<float>>& outputs, const AppConfig& config) {
    static int dump_count = 0;
    const int max_dump_frames = std::max(0, config.cup_output_debug_dump_frames);
    if (dump_count >= max_dump_frames) {
        return;
    }
    ++dump_count;

    const std::size_t max_values = static_cast<std::size_t>(std::max(0, config.cup_output_debug_dump_values));
    const std::ios::fmtflags old_flags = std::cout.flags();
    const std::streamsize old_precision = std::cout.precision();
    std::cout << "[CupModel][OutputDump] mode=boxes_only, output_count=" << outputs.size() << "\n";
    for (std::size_t output_index = 0; output_index < outputs.size(); ++output_index) {
        const auto& values = outputs[output_index];
        float min_value = 0.0F;
        float max_value = 0.0F;
        double sum = 0.0;
        std::size_t near_one_count = 0;
        if (!values.empty()) {
            min_value = values.front();
            max_value = values.front();
            for (float value : values) {
                min_value = std::min(min_value, value);
                max_value = std::max(max_value, value);
                sum += static_cast<double>(value);
                if (isNearOne(value)) {
                    ++near_one_count;
                }
            }
        }
        const double mean = values.empty() ? 0.0 : sum / static_cast<double>(values.size());
        const double near_one_ratio = values.empty()
                                          ? 0.0
                                          : static_cast<double>(near_one_count) / static_cast<double>(values.size());
        std::cout << std::fixed << std::setprecision(6)
                  << "[CupModel][OutputDump] out" << output_index
                  << " size=" << values.size()
                  << ", min=" << min_value
                  << ", max=" << max_value
                  << ", mean=" << mean
                  << ", near_one_count=" << near_one_count
                  << ", near_one_ratio=" << near_one_ratio << "\n";

        const std::size_t first_count = std::min(max_values, values.size());
        std::cout << "[CupModel][OutputDump] out" << output_index << " first" << first_count << "=";
        for (std::size_t i = 0; i < first_count; ++i) {
            if (i > 0) {
                std::cout << ",";
            }
            std::cout << values[i];
        }
        std::cout << "\n";

        if (output_index == 0) {
            dumpStrideRows(values, 5, 10);
            dumpStrideRows(values, 4, 10);
        }

        const bool divisible_by_5 = !values.empty() && values.size() % 5 == 0;
        const bool divisible_by_4 = !values.empty() && values.size() % 4 == 0;
        std::cout << "[CupModel][FormatHint] out" << output_index
                  << "_size=" << values.size()
                  << ", divisible_by_5=" << (divisible_by_5 ? 1 : 0)
                  << ", divisible_by_4=" << (divisible_by_4 ? 1 : 0) << "\n";
        if (divisible_by_5) {
            const std::size_t stride5_candidates = values.size() / 5;
            std::cout << "[CupModel][FormatHint] candidates_if_stride5=" << stride5_candidates << "\n";
            if (stride5_candidates > 500) {
                std::cout << "[CupModel][FormatHint][WARN] boxes-only output has too many candidates; verify this is not raw YOLO output\n";
            }
            std::size_t stride5_score_near_one_count = 0;
            for (std::size_t row = 0; row < stride5_candidates; ++row) {
                if (isNearOne(values[row * 5 + 4])) {
                    ++stride5_score_near_one_count;
                }
            }
            const double stride5_score_near_one_ratio =
                stride5_candidates == 0 ? 0.0 :
                static_cast<double>(stride5_score_near_one_count) / static_cast<double>(stride5_candidates);
            std::cout << "[CupModel][FormatHint] stride5_score_near_one_count="
                      << stride5_score_near_one_count
                      << ", stride5_score_near_one_ratio=" << stride5_score_near_one_ratio << "\n";
            if (stride5_score_near_one_ratio > 0.20) {
                std::cout << "[CupModel][FormatHint][WARN] many scores are near 1.0; score column may be parsed incorrectly\n";
            }
        }
        if (divisible_by_4) {
            const std::size_t stride4_candidates = values.size() / 4;
            std::cout << "[CupModel][FormatHint] candidates_if_stride4=" << stride4_candidates << "\n";
            if (stride4_candidates > 500) {
                std::cout << "[CupModel][FormatHint][WARN] boxes-only output has too many candidates; verify this is not raw YOLO output\n";
            }
        }
        if (near_one_ratio > 0.20) {
            std::cout << "[CupModel][FormatHint][WARN] many float values are near 1.0; verify score parsing\n";
        }
    }
    std::cout.flags(old_flags);
    std::cout.precision(old_precision);
}

BottleBoxesOnlyDecodeInfo decodeBottleBoxesOnlyOutputs(
    const std::vector<std::vector<float>>& outputs,
    const std::vector<RknnTensorInfo>& output_infos,
    const Frame& frame,
    const AppConfig& config,
    std::vector<DetectionCandidate>& candidates) {
    BottleBoxesOnlyDecodeInfo info;

    if (!outputs.empty() && !output_infos.empty()) {
        std::size_t anchors = 0;
        if (isBottleChannelFirst1x5x8400(output_infos[0], outputs[0], anchors)) {
            info.decoded = true;
            info.format = "channel_first_1x5x8400";
            info.score_source = "channel4";
            info.anchors = anchors;
            decodeBottleBoxesOnlyChannelFirst(outputs[0], anchors, frame, config, candidates);
            return info;
        }
    }

    std::cout << "[CupModel][BottleBoxesOnly][WARN] output attr unavailable, fallback to flat parser\n";
    if (outputs.size() >= 2 && outputs[0].size() >= 4 && outputs[0].size() % 4 == 0 &&
        outputs[1].size() == outputs[0].size() / 4) {
        info.decoded = true;
        info.format = "boxes_scores";
        info.score_source = "model_score";
        const std::size_t candidate_count = outputs[1].size();
        for (std::size_t index = 0; index < candidate_count; ++index) {
            const std::size_t offset = index * 4;
            const float score = normalizeYoloScore(outputs[1][index]);
            if (score < config.cup_score_threshold) {
                continue;
            }
            appendBottleBoxesOnlyDetection(
                makeBottleBoxOnlyBox(
                    outputs[0][offset + 0],
                    outputs[0][offset + 1],
                    outputs[0][offset + 2],
                    outputs[0][offset + 3],
                    score,
                    frame),
                score,
                config,
                candidates);
        }
        return info;
    }

    for (const auto& values : outputs) {
        if (values.empty()) {
            continue;
        }
        if (values.size() >= 5 && values.size() % 5 == 0) {
            info.decoded = true;
            info.format = "Nx5";
            info.score_source = "model_score";
            decodeBottleBoxesOnlyFlat(values, 5, frame, config, candidates);
            return info;
        }
        if (values.size() >= 4 && values.size() % 4 == 0) {
            info.decoded = true;
            info.format = "Nx4";
            info.score_source = "default_no_score";
            decodeBottleBoxesOnlyFlat(values, 4, frame, config, candidates);
            return info;
        }
    }

    return info;
}
bool decodeYoloPoseRawOutputs(
    const std::vector<std::vector<float>>& outputs,
    const std::vector<RknnTensorInfo>& output_infos,
    const Frame& frame,
    std::vector<PoseResult>& candidates) {
    if (outputs.size() < 4 || output_infos.size() < 4) {
        return false;
    }

    std::vector<RawPoseBox> raw_boxes;
    std::size_t anchor_offset = 0;
    for (std::size_t output_index = 0; output_index < 3; ++output_index) {
        std::size_t channels = 0;
        std::size_t grid_h = 0;
        std::size_t grid_w = 0;
        if (!tensorGridInfo(output_infos[output_index], outputs[output_index].size(), channels, grid_h, grid_w) ||
            channels < kYoloPoseRawChannels) {
            return false;
        }

        const auto& values = outputs[output_index];
        const float stride_x = static_cast<float>(std::max(1, frame.width)) / static_cast<float>(grid_w);
        const float stride_y = static_cast<float>(std::max(1, frame.height)) / static_cast<float>(grid_h);
        const std::size_t grid_size = grid_h * grid_w;
        for (std::size_t y = 0; y < grid_h; ++y) {
            for (std::size_t x = 0; x < grid_w; ++x) {
                const std::size_t cell_index = y * grid_w + x;
                const float score = sigmoid(values[kYoloPoseBoxChannels * grid_size + cell_index]);
                if (score < kPoseScoreThreshold) {
                    continue;
                }

                std::array<float, kYoloPoseBoxChannels> distances{};
                for (std::size_t channel = 0; channel < kYoloPoseBoxChannels; ++channel) {
                    distances[channel] = values[channel * grid_size + cell_index];
                }
                for (std::size_t side = 0; side < 4; ++side) {
                    softmaxInPlace(&distances[side * kYoloPoseBoxBins], kYoloPoseBoxBins);
                }

                std::array<float, 4> decoded{};
                for (std::size_t side = 0; side < 4; ++side) {
                    for (std::size_t bin = 0; bin < kYoloPoseBoxBins; ++bin) {
                        decoded[side] += distances[side * kYoloPoseBoxBins + bin] * static_cast<float>(bin);
                    }
                }

                const float x1 = (static_cast<float>(x) + 0.5F - decoded[0]) * stride_x;
                const float y1 = (static_cast<float>(y) + 0.5F - decoded[1]) * stride_y;
                const float x2 = (static_cast<float>(x) + 0.5F + decoded[2]) * stride_x;
                const float y2 = (static_cast<float>(y) + 0.5F + decoded[3]) * stride_y;

                RawPoseBox raw;
                raw.box = makeBoxFromValues(x1, y1, x2, y2, frame.width, frame.height);
                raw.box.label = "person";
                raw.box.score = score;
                raw.score = score;
                raw.keypoint_index = anchor_offset + cell_index;
                raw_boxes.push_back(raw);
            }
        }
        anchor_offset += grid_size;
    }

    const auto& keypoint_values = outputs[3];
    const std::size_t total_anchors = anchor_offset;
    if (total_anchors == 0 || keypoint_values.size() < kYoloPoseKeypointChannels * total_anchors) {
        return false;
    }

    for (const auto& raw : raw_boxes) {
        PoseResult candidate;
        candidate.valid = true;
        candidate.has_person = true;
        candidate.person_score = raw.score;
        candidate.person_box = raw.box;

        for (std::size_t point_index = 0; point_index < kPoseKeypointCount; ++point_index) {
            const std::size_t channel = point_index * 3;
            candidate.keypoints[point_index].x = std::clamp(
                keypoint_values[(channel + 0) * total_anchors + raw.keypoint_index],
                0.0F,
                static_cast<float>(std::max(0, frame.width - 1)));
            candidate.keypoints[point_index].y = std::clamp(
                keypoint_values[(channel + 1) * total_anchors + raw.keypoint_index],
                0.0F,
                static_cast<float>(std::max(0, frame.height - 1)));
            candidate.keypoints[point_index].score = clampScore(
                keypoint_values[(channel + 2) * total_anchors + raw.keypoint_index]);
        }
        candidates.push_back(candidate);
    }
    return !candidates.empty();
}

// 迁移自参考工程 posture_features.cpp 的 front45 指标提取：
// 使用鼻尖、单侧耳朵和同侧肩膀计算头前伸角与低头/后仰角。
bool extractFront45Metrics(
    const PoseResult& pose,
    float min_confidence,
    Front45Metrics& metrics) {
    const auto& points = pose.keypoints;
    const auto& nose = points[kNose];
    if (!visible(nose, min_confidence)) {
        return false;
    }

    struct Candidate {
        const PoseKeypoint* ear;
        const PoseKeypoint* shoulder;
        float confidence;
        float face_direction;
    };

    std::vector<Candidate> candidates;
    const std::array<std::pair<std::size_t, std::size_t>, 2> pairs{{
        {kLeftEar, kLeftShoulder},
        {kRightEar, kRightShoulder},
    }};

    for (const auto& pair : pairs) {
        const auto& ear = points[pair.first];
        const auto& shoulder = points[pair.second];
        if (!visible(ear, min_confidence) || !visible(shoulder, min_confidence)) {
            continue;
        }

        const float face_dx = nose.x - ear.x;
        if (std::abs(face_dx) < 1.0F) {
            continue;
        }

        candidates.push_back(Candidate{
            &ear,
            &shoulder,
            std::min({nose.score, ear.score, shoulder.score}),
            face_dx > 0.0F ? 1.0F : -1.0F,
        });
    }

    if (candidates.empty()) {
        return false;
    }

    const auto candidate = *std::max_element(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            return lhs.confidence < rhs.confidence;
        });

    const float neck_vertical = candidate.shoulder->y - candidate.ear->y;
    if (neck_vertical <= 1.0F) {
        return false;
    }

    const float head_forward =
        (candidate.ear->x - candidate.shoulder->x) * candidate.face_direction;
    metrics.head_forward_angle = degrees(std::atan2(head_forward, neck_vertical));
    metrics.head_pitch_angle = degrees(
        std::atan2(
            nose.y - candidate.ear->y,
            std::abs(nose.x - candidate.ear->x)));
    metrics.confidence = candidate.confidence;
    return true;
}

// 迁移自参考工程的 front45 规则分类：
// 头前伸、低头、后仰分别用固定角度阈值判断，头前伸叠加低头时归为胸廓塌陷风险。
bool classifyFront45Rules(
    const PoseResult& pose,
    float min_confidence,
    Front45RuleResult& result) {
    result = {};
    if (!extractFront45Metrics(pose, min_confidence, result.metrics)) {
        return false;
    }

    const bool head_forward = result.metrics.head_forward_angle >= 35.0F;
    const bool head_down = result.metrics.head_pitch_angle >= 15.0F;
    const bool head_backward = result.metrics.head_pitch_angle <= -15.0F;

    if (head_forward && head_down) {
        result.reasons.emplace_back("CHEST_COLLAPSE_RISK");
    } else {
        if (head_forward) {
            result.reasons.emplace_back("HEAD_FORWARD");
        }
        if (head_down) {
            result.reasons.emplace_back("HEAD_DOWN");
        }
    }
    if (head_backward) {
        result.reasons.emplace_back("HEAD_BACKWARD");
    }

    result.bad = !result.reasons.empty();
    return true;
}

// 喝水判断优先使用鼻尖作为头部位置；鼻尖不可见时，用左右耳可见点的平均位置兜底。
bool selectHeadPoint(const PoseResult& pose, float min_confidence, Point& head) {
    if (visible(pose.keypoints[kNose], min_confidence)) {
        head = Point{pose.keypoints[kNose].x, pose.keypoints[kNose].y};
        return true;
    }

    float x_sum = 0.0F;
    float y_sum = 0.0F;
    int count = 0;
    for (std::size_t index : {kLeftEar, kRightEar}) {
        if (!visible(pose.keypoints[index], min_confidence)) {
            continue;
        }
        x_sum += pose.keypoints[index].x;
        y_sum += pose.keypoints[index].y;
        ++count;
    }

    if (count == 0) {
        return false;
    }

    head = Point{x_sum / static_cast<float>(count), y_sum / static_cast<float>(count)};
    return true;
}

}  // namespace

bool PoseModel::load(const AppConfig& config) {
    config_ = config;
    loaded_ = true;
    fallback_mode_ = !model_.load(config.pose_model_path);
    std::cout << "[PoseModel] load: "
              << (config.pose_model_path.empty() ? "<pending>" : config.pose_model_path)
              << ", mode=" << (fallback_mode_ ? "stub" : "rknn") << "\n";
    std::cout << "[阈值][姿态模型] person_score >= " << kPoseScoreThreshold
              << " 才进入人体候选，NMS IoU=" << kPoseNmsThreshold << "\n";
    return true;
}

PoseResult PoseModel::infer(const Frame& frame) {
    PoseResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;

    if (!loaded_) {
        result.message = "pose_model_not_loaded";
        return result;
    }

    if (!fallback_mode_) {
        std::vector<std::vector<float>> outputs;
        if (model_.run(frame, outputs)) {
            return parseOutput(frame, outputs);
        }
        std::cerr << "[PoseModel] RKNN inference failed, use stub for frame " << frame.id << "\n";
        fallback_mode_ = true;
    }

    return fallbackInfer(frame);
}

PoseResult PoseModel::parseOutput(const Frame& frame, const std::vector<std::vector<float>>& outputs) const {
    PoseResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;
    result.valid = false;
    result.has_person = false;
    result.message = "pose_output_empty";

    std::vector<PoseResult> candidates;
    bool used_raw_decoder = decodeYoloPoseRawOutputs(outputs, model_.outputInfos(), frame, candidates);

    if (!used_raw_decoder) {
        for (std::size_t output_index = 0; output_index < outputs.size(); ++output_index) {
            const auto& values = outputs[output_index];
            if (values.size() < kPoseCandidateStride || values.size() % kPoseCandidateStride != 0) {
                continue;
            }

            const std::size_t candidate_count = values.size() / kPoseCandidateStride;
            bool prefer_channel_major = false;
            const auto& infos = model_.outputInfos();
            if (output_index < infos.size() && !infos[output_index].dims.empty()) {
                const auto& dims = infos[output_index].dims;
                prefer_channel_major = dims.back() != kPoseCandidateStride &&
                                       std::find(dims.begin(), dims.end(), kPoseCandidateStride) != dims.end();
            }

            // 兼容 Ultralytics 常见导出：[1, N, 56]，每个候选连续存放 56 个 float。
            if (!prefer_channel_major) {
                for (std::size_t index = 0; index < candidate_count; ++index) {
                    appendDecodedPoseCandidate(
                        values,
                        index * kPoseCandidateStride,
                        1,
                        frame.width,
                        frame.height,
                        candidates);
                }
            }

            // 兼容另一类常见导出：[1, 56, N]，同一通道的所有候选连续存放。
            if (prefer_channel_major || candidate_count > 1) {
                for (std::size_t index = 0; index < candidate_count; ++index) {
                    appendDecodedPoseCandidate(
                        values,
                        index,
                        candidate_count,
                        frame.width,
                        frame.height,
                        candidates);
                }
            }
        }
    }

    if (candidates.empty()) {
        result.message = "pose_output_format_unmatched_raw_or_decoded_yolov8_pose";
        return result;
    }

    std::vector<PoseResult> kept = nmsPoseResults(std::move(candidates), kPoseNmsThreshold);
    if (kept.empty()) {
        result.message = "pose_nms_no_person_kept";
        return result;
    }

    result = kept.front();
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;
    inverseTransformPoseResult(result, frame);
    std::ostringstream oss;
    oss << (used_raw_decoder ? "pose_yolov8_raw_dfl_decoder" : "pose_yolov8_decoded_tensor")
        << ",kept_after_nms=" << kept.size()
        << ",person_score=" << result.person_score
        << ",coords=" << (frame.transform.valid ? "source_after_resize_inverse" : "model_input");
    result.message = oss.str();
    return result;
}

PoseResult PoseModel::fallbackInfer(const Frame& frame) const {
    PoseResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;
    result.valid = false;
    result.has_person = false;
    result.message = "pose_stub_no_valid_output";
    return result;
}

bool CupModel::load(const AppConfig& config) {
    config_ = config;
    applyCupModelProfile(config_);
    loaded_ = true;
    fallback_mode_ = !model_.load(config_.cup_model_path);
    std::cout << "[CupModel] load: "
              << (config_.cup_model_path.empty() ? "<pending>" : config_.cup_model_path)
              << ", mode=" << (fallback_mode_ ? "stub" : "rknn") << "\n";
    std::cout << "[阈值][饮品模型] output_mode=" << cupOutputModeName(config_.cup_output_mode)
              << ", class_ids=" << cupClassIdsForConfigLog(config_)
              << ", score >= " << config_.cup_score_threshold
              << " 才进入候选，NMS IoU=" << kCupNmsThreshold << "\n";
    return true;
}
CupResult CupModel::infer(const Frame& frame) {
    CupResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;

    if (!loaded_) {
        result.message = "cup_model_not_loaded";
        return result;
    }

    if (!fallback_mode_) {
        std::vector<std::vector<float>> outputs;
        if (model_.run(frame, outputs)) {
            return parseOutput(frame, outputs);
        }
        std::cerr << "[CupModel] RKNN inference failed, use stub for frame " << frame.id << "\n";
        fallback_mode_ = true;
    }

    return fallbackInfer(frame);
}

CupResult CupModel::parseOutput(const Frame& frame, const std::vector<std::vector<float>>& outputs) const {
    CupResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;
    result.valid = false;
    result.message = "cup_output_empty";

    std::vector<DetectionCandidate> candidates;
    std::string parser_name;
    std::string score_source;
    std::string format;
    std::size_t anchors = 0;

    if (config_.cup_output_mode == CupOutputMode::BottleBoxesOnly) {
        dumpBottleBoxesOnlyOutputs(outputs, config_);
        BottleBoxesOnlyDecodeInfo info = decodeBottleBoxesOnlyOutputs(outputs, model_.outputInfos(), frame, config_, candidates);
        if (info.decoded) {
            parser_name = "bottle_boxes_only_decoder";
            score_source = info.score_source;
            format = info.format;
            anchors = info.anchors;
        }

        if (!parser_name.empty()) {
            const std::size_t candidate_count = candidates.size();
            auto kept = nmsDetections(std::move(candidates), kCupNmsThreshold);
            const std::size_t kept_after_nms = kept.size();
            sortAndLimitDetections(kept, config_);
            const std::size_t kept_used = kept.size();
            for (const auto& kept_box : kept) {
                result.cups.push_back(kept_box.box);
            }

            if (!result.cups.empty()) {
                result.valid = true;
                result.cup_box = result.cups.front();
                inverseTransformCupResult(result, frame);
            }

            std::cout << "[CupModel][BottleBoxesOnly] outputs=" << outputs.size()
                      << ", format=" << format;
            if (anchors > 0) {
                std::cout << ", anchors=" << anchors;
            }
            std::cout << ", candidates=" << candidate_count
                      << ", kept_after_nms=" << kept_after_nms
                      << ", kept_used=" << kept_used
                      << ", score_source=" << score_source << "\n";

            std::ostringstream oss;
            oss << parser_name
                << ",format=" << format
                << ",class_ids=none"
                << ",candidates_before_nms=" << candidate_count
                << ",candidates=" << candidate_count
                << ",kept_after_nms=" << kept_after_nms
                << ",kept_used=" << kept_used
                << ",best_score=" << (result.cups.empty() ? 0.0F : result.cup_box.score)
                << ",best_class_id=-1"
                << ",score_source=" << score_source;
            if (anchors > 0) {
                oss << ",anchors=" << anchors;
            }
            oss << ",coords=" << (frame.transform.valid ? "source_after_resize_inverse" : "model_input");
            result.message = oss.str();
            return result;
        }

        result.message = "bottle_boxes_only_decoder_no_supported_output,class_ids=none";
        return result;
    }

    if (decodeYoloDetectionRawOutputs(outputs, model_.outputInfos(), frame, config_.cup_score_threshold, candidates)) {
        parser_name = "cup_yolov8_raw_decoder";
    }

    if (parser_name.empty() &&
        decodeYoloDetectionSplitOutputs(outputs, model_.outputInfos(), frame, config_.cup_score_threshold, candidates)) {
        parser_name = "cup_yolov8_split_decoder";
    }

    if (parser_name.empty() &&
        decodeYoloDetectionDecodedOutput(outputs, model_.outputInfos(), frame, config_.cup_score_threshold, candidates)) {
        parser_name = "cup_yolov8_decoded_tensor";
    }

    if (parser_name.empty() &&
        decodePostprocessedCupOutput(outputs, frame, config_.cup_score_threshold, candidates)) {
        parser_name = "cup_postprocessed_boxes";
    }

    if (!parser_name.empty()) {
        const std::size_t candidate_count = candidates.size();
        auto kept = nmsDetections(std::move(candidates), kCupNmsThreshold);
        const std::size_t kept_after_nms = kept.size();
        sortAndLimitDetections(kept, config_);
        const std::size_t kept_used = kept.size();
        for (const auto& kept_box : kept) {
            result.cups.push_back(kept_box.box);
        }

        if (!result.cups.empty()) {
            result.valid = true;
            result.cup_box = result.cups.front();
            inverseTransformCupResult(result, frame);

            std::ostringstream oss;
            oss << parser_name
                << ",class_ids=" << cupClassIdsForConfigLog(config_)
                << ",candidates_before_nms=" << candidate_count
                << ",candidates=" << candidate_count
                << ",kept_after_nms=" << kept_after_nms
                << ",kept_used=" << kept_used
                << ",best_score=" << result.cup_box.score
                << ",coords=" << (frame.transform.valid ? "source_after_resize_inverse" : "model_input");
            result.message = oss.str();
            return result;
        }

        std::ostringstream oss;
        oss << parser_name
            << ",class_ids=" << cupClassIdsForConfigLog(config_)
            << ",candidates_before_nms=" << candidate_count
            << ",candidates=" << candidate_count
            << ",kept_after_nms=" << kept_after_nms
            << ",kept_used=0";
        result.message = oss.str();
        return result;
    }

    result.message = "cup_output_no_box_over_threshold";
    return result;
}CupResult CupModel::fallbackInfer(const Frame& frame) const {
    CupResult result;
    result.frame_id = frame.id;
    result.timestamp_ms = frame.timestamp_ms;
    result.valid = false;
    result.message = "cup_stub_no_valid_output";
    return result;
}

void PostureAnalyzer::configure(const AppConfig& config) {
    keypoint_score_threshold_ = config.pose_keypoint_score_threshold;
    std::cout << "[阈值][坐姿事件] keypoint_score >= " << keypoint_score_threshold_
              << "；头前伸角 >= 35 判 HEAD_FORWARD；低头角 >= 15 判 HEAD_DOWN；后仰角 <= -15 判 HEAD_BACKWARD\n";
}

PostureState PostureAnalyzer::update(const PoseResult& pose) {
    if (!pose.valid || !pose.has_person) {
        return PostureState::UNKNOWN;
    }

    Front45RuleResult rule;
    if (!classifyFront45Rules(pose, keypoint_score_threshold_, rule)) {
        return PostureState::UNKNOWN;
    }

    // front45 规则能完成分类时，直接映射到当前系统已有的姿态状态枚举。
    if (rule.bad) {
        return PostureState::BAD_ALERT;
    }
    return PostureState::GOOD;
}

void PostureAnalyzer::reset() {
    /* 当前 stub 不保存时间窗口状态；保留 reset 接口，后续用于清空滑动窗口和告警计数。 */
}

void DrinkDetector::configure(const AppConfig& config) {
    keypoint_score_threshold_ = config.pose_keypoint_score_threshold;
    drink_distance_norm_threshold_ = config.drink_distance_norm_threshold;
    drink_consecutive_hits_ = std::max(1, config.drink_consecutive_hits);
    consecutive_hits_ = 0;
    std::cout << "[阈值][喝水事件] keypoint_score >= " << keypoint_score_threshold_
              << "；杯子到头部归一化距离 <= " << drink_distance_norm_threshold_
              << " 连续 " << drink_consecutive_hits_ << " 次判 DRINK_DETECTED；有杯但距离更远判 NEED_REMIND\n";
}

DrinkState DrinkDetector::update(const PoseResult& pose, const CupResult& cups) {
    if (!pose.valid || !pose.has_person || !cups.valid || cups.cups.empty()) {
        consecutive_hits_ = 0;
        return DrinkState::NORMAL;
    }

    Point head;
    if (!selectHeadPoint(pose, keypoint_score_threshold_, head)) {
        consecutive_hits_ = 0;
        return DrinkState::NORMAL;
    }

    const float person_diag = std::max(
        1.0F,
        std::sqrt(
            pose.person_box.w * pose.person_box.w +
            pose.person_box.h * pose.person_box.h));

    float best_norm_distance = std::numeric_limits<float>::max();
    for (const Box& cup : cups.cups) {
        const Point cup_center{cup.x + cup.w * 0.5F, cup.y + cup.h * 0.5F};
        const float dx = cup_center.x - head.x;
        const float dy = cup_center.y - head.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        best_norm_distance = std::min(best_norm_distance, distance / person_diag);
    }

    // 用人体框对角线归一化距离，避免固定像素阈值受摄像头距离和分辨率影响过大。
    // 连续命中达到配置次数后才认为正在喝水，减少单帧误检。
    if (best_norm_distance <= drink_distance_norm_threshold_) {
        ++consecutive_hits_;
        if (consecutive_hits_ >= drink_consecutive_hits_) {
            return DrinkState::DRINK_DETECTED;
        }
        return DrinkState::NORMAL;
    }

    consecutive_hits_ = 0;
    return DrinkState::NEED_REMIND;
}

void DrinkDetector::reset() {
    consecutive_hits_ = 0;
    /* 当前 stub 不保存连续帧状态；保留 reset 接口，后续用于清空命中计数和提醒冷却时间。 */
}

void AiScheduler::configure(const AppConfig& config) {
    pose_interval_ms_ = std::max(1, config.pose_interval_ms);
    gesture_interval_ms_ = std::max(1, config.gesture_interval_ms);
    cup_interval_ms_ = std::max(1, config.cup_interval_ms);
    reset();
}

AiScheduleDecision AiScheduler::next(int64_t timestamp_ms, bool running_mode) {
    AiScheduleDecision decision;
    decision.run_gesture = due(timestamp_ms, last_gesture_ms_, gesture_interval_ms_);
    if (decision.run_gesture) {
        last_gesture_ms_ = timestamp_ms;
    }

    if (!running_mode) {
        return decision;
    }

    decision.run_pose = due(timestamp_ms, last_pose_ms_, pose_interval_ms_);
    if (decision.run_pose) {
        last_pose_ms_ = timestamp_ms;
    }

    decision.run_cup = due(timestamp_ms, last_cup_ms_, cup_interval_ms_);
    if (decision.run_cup) {
        last_cup_ms_ = timestamp_ms;
    }

    return decision;
}

void AiScheduler::reset() {
    last_pose_ms_ = -1;
    last_gesture_ms_ = -1;
    last_cup_ms_ = -1;
}

bool AiScheduler::due(int64_t now_ms, int64_t last_ms, int interval_ms) {
    if (last_ms < 0) {
        return true;
    }
    return (now_ms - last_ms) >= interval_ms;
}

}  // namespace rv1126b
