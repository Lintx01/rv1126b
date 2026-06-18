#pragma once

#include "BlockingQueue.hpp"
#include "Interfaces.hpp"
#include "PerceptionModules.hpp"
#include "Types.hpp"
#include "VideoPipeline.hpp"

#include <atomic>
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
    void enqueueAlarmMessages(const VisionResult& result);
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
    static int64_t nowMs();

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
    BlockingQueue<DisplayFace> display_queue_{8};

    std::thread camera_thread_;
    std::thread encoder_thread_;
    std::thread ai_thread_;
    std::thread mqtt_thread_;
    std::thread display_thread_;

    std::atomic<bool> exit_requested_{false};
    std::atomic<bool> thread_error_{false};
    std::atomic<SystemState> state_{SystemState::Idle};
    std::mutex state_mutex_;
};

}  // namespace rv1126b
