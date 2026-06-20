#include "Interfaces.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(RV1126B_HAS_OPENCV)
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace rv1126b {

namespace {

void closeFd(int& fd) {
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        fd = -1;
    }
}

bool sendAll(int fd, const uint8_t* data, std::size_t size) {
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif

    std::size_t total = 0;
    while (total < size) {
        const ssize_t sent = ::send(fd, data + total, size - total, flags);
        if (sent <= 0) {
            return false;
        }
        total += static_cast<std::size_t>(sent);
    }

    return true;
}

bool sendAllString(int fd, const std::string& text) {
    return sendAll(
        fd,
        reinterpret_cast<const uint8_t*>(text.data()),
        text.size());
}

#if defined(RV1126B_HAS_OPENCV)

int clampInt(int value, int low, int high) {
    return std::max(low, std::min(value, high));
}

void drawText(
    cv::Mat& image,
    const std::string& text,
    int x,
    int y,
    const cv::Scalar& color) {
    cv::putText(
        image,
        text,
        cv::Point(x, y),
        cv::FONT_HERSHEY_SIMPLEX,
        0.55,
        cv::Scalar(0, 0, 0),
        3,
        cv::LINE_AA);

    cv::putText(
        image,
        text,
        cv::Point(x, y),
        cv::FONT_HERSHEY_SIMPLEX,
        0.55,
        color,
        1,
        cv::LINE_AA);
}

std::string scoreText(float score) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", score);
    return std::string(buf);
}

void drawAiOverlay(
    cv::Mat& bgr,
    const VisionResult* result,
    const AppState* state) {
    // 顶部黑色信息栏，避免文字和画面混在一起。
    const int panel_height = 92;
    cv::rectangle(
        bgr,
        cv::Rect(0, 0, bgr.cols, std::min(panel_height, bgr.rows)),
        cv::Scalar(0, 0, 0),
        -1);

    std::string gesture_text = "--";
    if (state != nullptr && !state->gesture_name.empty()) {
        gesture_text = state->gesture_name;
    }

    std::string posture_text = "WAIT";
    std::string drink_text = "WAIT";
    std::string boxes_text = "WAIT";

    if (result != nullptr) {
        posture_text = result->bad_posture ? "BAD" : "OK";

        if (result->drink_reminder) {
            drink_text = "REMIND";
        } else if (result->drink_detected) {
            drink_text = "DRINKING";
        } else {
            drink_text = "OK";
        }

        const std::size_t box_count = result->boxes.size();

        if (box_count == 0U) {
            boxes_text = "0";
        } else if (box_count > 20U) {
            boxes_text = "TOO MANY(" + std::to_string(box_count) + ")";
        } else {
            boxes_text = std::to_string(box_count);
        }
    }

    drawText(
        bgr,
        "AI Preview",
        10,
        22,
        cv::Scalar(255, 255, 255));

    drawText(
        bgr,
        "Gesture: " + gesture_text,
        10,
        44,
        cv::Scalar(255, 255, 255));

    drawText(
        bgr,
        "Posture: " + posture_text +
            "   Drink: " + drink_text +
            "   Boxes: " + boxes_text,
        10,
        66,
        cv::Scalar(255, 255, 255));

    // 框太多时不要画框，否则整张画面会被盖住。
    if (result == nullptr || result->boxes.empty() || result->boxes.size() > 20U) {
        if (result != nullptr && result->boxes.size() > 20U) {
            drawText(
                bgr,
                "Warning: too many boxes, postprocess may be wrong",
                10,
                88,
                cv::Scalar(0, 255, 255));
        }
        return;
    }

    const int max_draw_boxes = 5;
    int drawn = 0;

    for (const Box& box : result->boxes) {
        if (drawn >= max_draw_boxes) {
            break;
        }

        const int x1 = clampInt(static_cast<int>(box.x), 0, bgr.cols - 1);
        const int y1 = clampInt(static_cast<int>(box.y), 0, bgr.rows - 1);
        const int x2 = clampInt(static_cast<int>(box.x + box.w), 0, bgr.cols - 1);
        const int y2 = clampInt(static_cast<int>(box.y + box.h), 0, bgr.rows - 1);

        const int w = std::max(1, x2 - x1);
        const int h = std::max(1, y2 - y1);

        cv::rectangle(
            bgr,
            cv::Rect(x1, y1, w, h),
            cv::Scalar(0, 255, 0),
            2,
            cv::LINE_AA);

        std::string label = box.label.empty() ? "box" : box.label;
        label += " " + scoreText(box.score);

        drawText(
            bgr,
            label,
            x1,
            std::max(112, y1 - 6),
            cv::Scalar(0, 255, 0));

        ++drawn;
    }
}

bool frameToJpeg(
    const Frame& frame,
    const VisionResult* result,
    const AppState* state,
    std::vector<uint8_t>& jpeg) {
    if (frame.data.empty() || frame.width <= 0 || frame.height <= 0) {
        return false;
    }

    cv::Mat bgr;

    if (frame.format == PixelFormat::NV12) {
        const std::size_t required =
            static_cast<std::size_t>(frame.width) *
            static_cast<std::size_t>(frame.height) * 3U / 2U;

        if (frame.data.size() < required) {
            std::cerr << "[Web][MJPEG] NV12 frame too small, size="
                      << frame.data.size()
                      << ", required=" << required << "\n";
            return false;
        }

        cv::Mat nv12(
            frame.height + frame.height / 2,
            frame.width,
            CV_8UC1,
            const_cast<uint8_t*>(frame.data.data()));

        cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);
    } else if (frame.format == PixelFormat::RGB888) {
        cv::Mat rgb(
            frame.height,
            frame.width,
            CV_8UC3,
            const_cast<uint8_t*>(frame.data.data()));

        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    } else if (frame.format == PixelFormat::BGR888) {
        cv::Mat src(
            frame.height,
            frame.width,
            CV_8UC3,
            const_cast<uint8_t*>(frame.data.data()));
        bgr = src.clone();
    } else {
        std::cerr << "[Web][MJPEG] unsupported frame format\n";
        return false;
    }

    drawAiOverlay(bgr, result, state);

    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(75);

    return cv::imencode(".jpg", bgr, jpeg, params);
}

#endif

}  // namespace

struct WebStreamer::Impl {
    std::thread server_thread;
    std::mutex mutex;
    std::condition_variable cv;

    std::vector<uint8_t> latest_jpeg;
    uint64_t latest_frame_id{0};

    VisionResult latest_result;
    bool has_result{false};

    AppState latest_state;
    bool has_state{false};

    std::atomic<bool> server_running{false};
    int server_fd{-1};
    int client_fd{-1};
};

WebStreamer::WebStreamer() : impl_(std::make_unique<Impl>()) {}

WebStreamer::~WebStreamer() {
    stop();
}

bool WebStreamer::start(const AppConfig& config) {
    config_ = config;

    if (config_.web_stream_protocol != WebStreamProtocol::Mjpeg) {
        std::cerr << "[Web] only MJPEG is enabled in this debug version\n";
        return false;
    }

#if !defined(RV1126B_HAS_OPENCV)
    std::cerr << "[Web][MJPEG] OpenCV is not enabled, cannot encode JPEG\n";
    return false;
#else
    impl_->server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->server_fd < 0) {
        std::cerr << "[Web][MJPEG] socket failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }

    int opt = 1;
    ::setsockopt(
        impl_->server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(config_.web_server_port));

    if (::bind(
            impl_->server_fd,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)) < 0) {
        std::cerr << "[Web][MJPEG] bind failed, port="
                  << config_.web_server_port
                  << ", error=" << std::strerror(errno) << "\n";
        closeFd(impl_->server_fd);
        return false;
    }

    if (::listen(impl_->server_fd, 2) < 0) {
        std::cerr << "[Web][MJPEG] listen failed: "
                  << std::strerror(errno) << "\n";
        closeFd(impl_->server_fd);
        return false;
    }

    impl_->server_running = true;
    running_ = true;

    std::cout << "[Web][MJPEG] server listening on 0.0.0.0:"
              << config_.web_server_port
              << ", url=http://" << config_.device_ip
              << ":" << config_.web_server_port
              << "/stream.mjpg\n";

    impl_->server_thread = std::thread([this]() {
        while (impl_->server_running) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            int client_fd = ::accept(
                impl_->server_fd,
                reinterpret_cast<sockaddr*>(&client_addr),
                &client_len);

            if (client_fd < 0) {
                if (impl_->server_running) {
                    std::cerr << "[Web][MJPEG] accept failed: "
                              << std::strerror(errno) << "\n";
                }
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->client_fd = client_fd;
            }

            std::cout << "[Web][MJPEG] client connected\n";

            const std::string header =
                "HTTP/1.1 200 OK\r\n"
                "Server: rv1126b_vision_app\r\n"
                "Connection: close\r\n"
                "Cache-Control: no-cache\r\n"
                "Pragma: no-cache\r\n"
                "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                "\r\n";

            if (!sendAllString(client_fd, header)) {
                closeFd(client_fd);
                continue;
            }

            uint64_t last_sent_frame_id = 0;

            while (impl_->server_running) {
                std::vector<uint8_t> jpeg;
                uint64_t frame_id = 0;

                {
                    std::unique_lock<std::mutex> lock(impl_->mutex);
                    impl_->cv.wait_for(
                        lock,
                        std::chrono::milliseconds(1000),
                        [&]() {
                            return !impl_->server_running ||
                                   impl_->latest_frame_id != last_sent_frame_id;
                        });

                    if (!impl_->server_running) {
                        break;
                    }

                    if (impl_->latest_jpeg.empty() ||
                        impl_->latest_frame_id == last_sent_frame_id) {
                        continue;
                    }

                    jpeg = impl_->latest_jpeg;
                    frame_id = impl_->latest_frame_id;
                }

                const std::string part_header =
                    "--frame\r\n"
                    "Content-Type: image/jpeg\r\n"
                    "Content-Length: " + std::to_string(jpeg.size()) + "\r\n"
                    "\r\n";

                if (!sendAllString(client_fd, part_header) ||
                    !sendAll(client_fd, jpeg.data(), jpeg.size()) ||
                    !sendAllString(client_fd, "\r\n")) {
                    break;
                }

                last_sent_frame_id = frame_id;
            }

            closeFd(client_fd);

            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->client_fd = -1;
            }

            std::cout << "[Web][MJPEG] client disconnected\n";
        }
    });

    return true;
#endif
}

void WebStreamer::publishFrame(const FramePtr& frame) {
    if (!running_ || !frame) {
        return;
    }

    if (config_.web_stream_protocol != WebStreamProtocol::Mjpeg) {
        return;
    }

#if defined(RV1126B_HAS_OPENCV)
    // 25fps 输入时，每 3 帧推一次，约 8fps。
    if ((frame->id % 3U) != 0U) {
        return;
    }

    VisionResult result_snapshot;
    AppState state_snapshot;
    bool has_result_snapshot = false;
    bool has_state_snapshot = false;

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->has_result) {
            result_snapshot = impl_->latest_result;
            has_result_snapshot = true;
        }
        if (impl_->has_state) {
            state_snapshot = impl_->latest_state;
            has_state_snapshot = true;
        }
    }

    std::vector<uint8_t> jpeg;
    if (!frameToJpeg(
            *frame,
            has_result_snapshot ? &result_snapshot : nullptr,
            has_state_snapshot ? &state_snapshot : nullptr,
            jpeg)) {
        std::cerr << "[Web][MJPEG] frame to jpeg failed, frame="
                  << frame->id << "\n";
        return;
    }

    const std::size_t jpeg_size = jpeg.size();

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->latest_jpeg = std::move(jpeg);
        impl_->latest_frame_id = frame->id;
    }

    impl_->cv.notify_all();

    if ((frame->id % 90U) == 0U) {
        std::cout << "[Web][MJPEG] publish frame="
                  << frame->id
                  << ", jpeg_size=" << jpeg_size
                  << ", overlay_result=" << (has_result_snapshot ? "yes" : "no")
                  << ", overlay_state=" << (has_state_snapshot ? "yes" : "no")
                  << "\n";
    }
#endif
}

void WebStreamer::publishEncodedVideo(const EncodedPacket& packet) {
    (void)packet;
}

void WebStreamer::publishResult(const VisionResult& result) {
    if (!running_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->latest_result = result;
        impl_->has_result = true;
    }

    std::cout << "[Web][MJPEG] stored result frame=" << result.frame_id
              << ", boxes=" << result.boxes.size()
              << ", bad_posture=" << (result.bad_posture ? "true" : "false")
              << ", drink_reminder=" << (result.drink_reminder ? "true" : "false")
              << "\n";
}

void WebStreamer::publishAppState(const AppState& state) {
    if (!running_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->latest_state = state;
        impl_->has_state = true;
    }

    std::cout << "[Web][MJPEG] stored app_state frame=" << state.frame_id
              << ", posture_state=" << static_cast<int>(state.posture_state)
              << ", drink_state=" << static_cast<int>(state.drink_state)
              << ", face=" << static_cast<int>(state.display_face)
              << "\n";
}

void WebStreamer::stop() {
    impl_->server_running = false;
    impl_->cv.notify_all();

    closeFd(impl_->client_fd);
    closeFd(impl_->server_fd);

    if (impl_->server_thread.joinable()) {
        impl_->server_thread.join();
    }

    if (running_) {
        std::cout << "[Web] streamer stop\n";
    }

    running_ = false;
}

}  // namespace rv1126b