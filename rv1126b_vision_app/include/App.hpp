#pragma once

#include "BlockingQueue.hpp"
#include "Interfaces.hpp"
#include "PerceptionModules.hpp"
#include "Types.hpp"
#include "VideoPipeline.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rv1126b {

class VisionApp {
public:
    explicit VisionApp(AppConfig config);
    ~VisionApp();

    bool start();
    void stop();
    int run();

private:
    void cameraLoop();
    void encoderLoop();
    void aiLoop();
    void mqttLoop();
    void displayLoop();
    void runThread(const std::string& name, const std::function<void()>& loop);

    void requestStartByGesture();
    void requestStopByGesture();
    void resetDrinkTimer(const char* reason, int64_t now_ms);
    void pauseDrinkTimer(const char* reason);
    void clearDrinkTimerByDrinkDetected(int64_t now_ms);
    void acknowledgeDrinkTimerByConfirm(int64_t now_ms);
    void queueDrinkTimerReminderEvent();
    DrinkState updateDrinkTimerAndCombine(DrinkState visual_drink_state, int64_t now_ms);
    void enqueueAlarmMessages(const VisionResult& result, DrinkState visual_drink_state);
    VisionResult composeVisionResult(
        const PoseResult& pose_result,
        const CupResult& cup_result,
        PostureState posture_state,
        DrinkState drink_state) const;
    AppState buildAppState(
        const VisionResult* result,
        PostureState posture_state = PostureState::UNKNOWN,
        DrinkState drink_state = DrinkState::NORMAL,
        const std::string& gesture_name = std::string()) const;
    void publishDisplayState(
        const VisionResult* result,
        PostureState posture_state,
        DrinkState drink_state,
        const std::string& gesture_name);
    DisplayFace selectDisplayFace(const AppState& state) const;
    bool shouldRunEncoderPipeline() const;
    void logPerformanceIfDue();
    static int64_t nowMs();

    struct PerfStat {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> total_us{0};

        void addSample(uint64_t duration_us) {
            count.fetch_add(1, std::memory_order_relaxed);
            total_us.fetch_add(duration_us, std::memory_order_relaxed);
        }
    };

    struct PerfCounters {
        std::atomic<uint64_t> camera_frames{0};
        std::atomic<uint64_t> encoder_frames{0};
        std::atomic<uint64_t> ai_iterations{0};
        PerfStat gesture_infer;
        PerfStat pose_infer;
        PerfStat cup_infer;
        PerfStat overlay;
        PerfStat mpp_encode;
        PerfStat display_show;
        std::atomic<int64_t> last_log_ms{0};
    };

    AppConfig config_;
    CameraDevice camera_;
    GestureModel gesture_model_;
    PoseModel pose_model_;
    CupModel cup_model_;
    PostureDrinkModel posture_drink_model_;
    PostureAnalyzer posture_analyzer_;
    DrinkDetector drink_detector_;
    AiScheduler ai_scheduler_;
    DisplayDevice display_;
    MqttClient mqtt_;
    WebStreamer web_;
    MppEncoder mpp_encoder_;
    MppDecoder mpp_decoder_;
    ImageProcessor image_processor_;

    LatestFrameBuffer<FramePtr> latest_ai_frame_;
    BlockingQueue<FramePtr> encode_queue_{4, QueueOverflowPolicy::DROP_OLDEST};
    BlockingQueue<MqttMessage> mqtt_queue_{32, QueueOverflowPolicy::BLOCK};
    BlockingQueue<DisplayFace> display_queue_{8, QueueOverflowPolicy::DROP_OLDEST};

    std::thread camera_thread_;
    std::thread encoder_thread_;
    std::thread ai_thread_;
    std::thread mqtt_thread_;
    std::thread display_thread_;

    std::atomic<bool> exit_requested_{false};
    std::atomic<bool> thread_error_{false};
    std::atomic<SystemState> state_{SystemState::Idle};
    std::mutex state_mutex_;
    int64_t drink_timer_last_reset_ms_{0};
    int64_t drink_timer_last_event_ms_{0};
    bool drink_timer_active_{false};
    bool drink_timer_initialized_{false};
    PerfCounters perf_;
};

}  // namespace rv1126b
