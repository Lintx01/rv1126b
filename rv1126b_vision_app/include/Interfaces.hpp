#pragma once

#include "RknnModel.hpp"
#include "Types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rv1126b {

class CameraDevice {
public:
    CameraDevice();
    ~CameraDevice();

    bool open(const AppConfig& config);
    bool read(Frame& frame);
    void close();

private:
    struct Impl;

    AppConfig config_;
    std::unique_ptr<Impl> impl_;
    uint64_t next_frame_id_{0};
    bool opened_{false};
};

class GestureModel {
public:
    bool load(const AppConfig& config);
    GestureResult infer(const Frame& frame);

private:
    GestureResult parseOutput(const Frame& frame, const std::vector<std::vector<float>>& outputs) const;
    GestureResult fallbackInfer(const Frame& frame);

    RknnModel model_;
    bool loaded_{false};
    bool fallback_mode_{false};
    uint64_t infer_count_{0};
    float score_threshold_{0.60F};
};

class PostureDrinkModel {
public:
    bool load(const AppConfig& config);
    VisionResult infer(const Frame& frame);

private:
    VisionResult parseOutput(const Frame& frame, const std::vector<std::vector<float>>& outputs) const;
    VisionResult fallbackInfer(const Frame& frame) const;
    void applyDrinkRule(VisionResult& result) const;

    RknnModel model_;
    AppConfig config_;
    bool loaded_{false};
};

class PoseModel {
public:
    bool load(const AppConfig& config);
    PoseResult infer(const Frame& frame);

private:
    PoseResult parseOutput(const Frame& frame, const std::vector<std::vector<float>>& outputs) const;
    PoseResult fallbackInfer(const Frame& frame) const;

    RknnModel model_;
    AppConfig config_;
    bool loaded_{false};
    bool fallback_mode_{false};
};

class CupModel {
public:
    bool load(const AppConfig& config);
    CupResult infer(const Frame& frame);

private:
    CupResult parseOutput(const Frame& frame, const std::vector<std::vector<float>>& outputs) const;
    CupResult fallbackInfer(const Frame& frame) const;

    RknnModel model_;
    AppConfig config_;
    bool loaded_{false};
    bool fallback_mode_{false};
};

class PostureAnalyzer {
public:
    void configure(const AppConfig& config);
    PostureState update(const PoseResult& pose);
    void reset();

private:
    float keypoint_score_threshold_{0.35F};
};

class DrinkDetector {
public:
    void configure(const AppConfig& config);
    DrinkState update(const PoseResult& pose, const CupResult& cup);
    void reset();

private:
    float keypoint_score_threshold_{0.35F};
    float drink_distance_norm_threshold_{0.40F};
    int drink_consecutive_hits_{3};
    int consecutive_hits_{0};
};

class DisplayDevice {
public:
    DisplayDevice();
    ~DisplayDevice();

    bool open(const AppConfig& config);
    void tick();
    void showIdleClock();
    void showFace(DisplayFace face);
    void showHeartExpression();
    bool drawRgb565Area(int x, int y, int width, int height, const uint16_t* pixels);
    void close();

private:
    struct LvglUiState;

    bool ensureLvglInitialized();
    void clearLvglPage();
    void applyThemeStyles();
    void createIdleClockPage();
    void createStatusPill();
    void createBreathingDot();
    void updateIdleClock(bool force);
    bool resetPanel();
    bool initPanel();
    bool drawRgb565Bitmap(const uint16_t* pixels, int width, int height);
    bool writeCommand(uint8_t command);
    bool writeData(const uint8_t* data, std::size_t length);
    bool setGpio(int gpio, bool high);

    AppConfig config_;
    int spi_fd_{-1};
    bool opened_{false};
    bool lvgl_initialized_{false};
    bool lvgl_compile_warning_printed_{false};
    bool lvgl_time_warning_printed_{false};
    bool idle_clock_visible_{false};
    int64_t lvgl_last_tick_ms_{0};
    int64_t idle_clock_last_update_ms_{0};
    int idle_clock_last_second_{-1};
    std::unique_ptr<LvglUiState> lvgl_ui_;
};

class MqttClient {
public:
    MqttClient();
    ~MqttClient();

    bool connect(const AppConfig& config);
    bool publish(const MqttMessage& message);
    void disconnect();

private:
    struct Impl;

    bool reconnect();
    bool publishOnce(const MqttMessage& message);

    AppConfig config_;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> connected_{false};
};

class WebStreamer {
public:
    WebStreamer();
    ~WebStreamer();

    bool start(const AppConfig& config);
    void publishFrame(const FramePtr& frame);
    void publishEncodedVideo(const EncodedPacket& packet);
    void publishResult(const VisionResult& result);
    void publishAppState(const AppState& state);
    void stop();

private:
    struct Impl;

    AppConfig config_;
    std::unique_ptr<Impl> impl_;
    bool running_{false};
};

}  // namespace rv1126b
