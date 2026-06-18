#include "Interfaces.hpp"

#include "WebRtcTransport.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace rv1126b {

struct WebStreamer::Impl {
    H264RtpPacketizer packetizer;
    WebRtcSignalingClient signaling;
};

WebStreamer::WebStreamer() : impl_(std::make_unique<Impl>()) {}

WebStreamer::~WebStreamer() {
    stop();
}

bool WebStreamer::start(const AppConfig& config) {
    config_ = config;

    if (config.web_stream_protocol != WebStreamProtocol::WebRTC) {
        std::cerr << "[Web] unsupported protocol, this project uses WebRTC only\n";
        return false;
    }

    impl_->packetizer.configure(
        config.webrtc_h264_payload_type,
        config.webrtc_video_ssrc,
        config.webrtc_rtp_max_payload_size);

    if (!impl_->signaling.connect(config)) {
        return false;
    }

    running_ = true;
    std::cout << "[Web] WebRTC streamer start"
              << ", signaling=" << config.webrtc_signaling_url
              << ", stun=" << config.webrtc_stun_url
              << ", payload_type=" << static_cast<int>(config.webrtc_h264_payload_type)
              << ", ssrc=" << config.webrtc_video_ssrc
              << ", max_payload=" << config.webrtc_rtp_max_payload_size << "\n";
    return true;
}

void WebStreamer::publishFrame(const FramePtr& frame) {
    if (!running_ || !frame) {
        return;
    }

    // 调试兜底：正式链路应使用 MPP H.264 编码后的 publishEncodedVideo。
    // 原始 RGB 帧带宽过高，只适合排查采集链路是否有图像。
    if (frame->id % 25 == 0) {
        std::cout << "[Web] raw frame fallback id=" << frame->id << "\n";
    }
}

void WebStreamer::publishEncodedVideo(const EncodedPacket& packet) {
    if (!running_ || packet.data.empty()) {
        return;
    }

    std::vector<RtpPacket> rtp_packets = impl_->packetizer.packetize(packet);
    if (rtp_packets.empty()) {
        std::cerr << "[Web] H264 packetize failed, frame=" << packet.frame_id << "\n";
        return;
    }

    // 这里已经生成真实 RTP packet，包括单 NALU 和 FU-A 分片。
    // 后续接 libdatachannel/WebRTC native 时，把 rtp.bytes 送入 video track 即可。
    // 当前先保留发送摘要，避免把 WebRTC native 复杂依赖强行塞入板端主工程。
    if (packet.frame_id % 25 == 0 || packet.key_frame) {
        const RtpPacket& first = rtp_packets.front();
        const RtpPacket& last = rtp_packets.back();
        std::cout << "[Web] RTP frame=" << packet.frame_id
                  << ", packets=" << rtp_packets.size()
                  << ", key=" << (packet.key_frame ? "true" : "false")
                  << ", seq=" << first.sequence_number << "-" << last.sequence_number
                  << ", timestamp=" << first.timestamp << "\n";
    }

    impl_->signaling.sendRtpSummary(packet.frame_id, rtp_packets.size(), packet.key_frame);
}

void WebStreamer::publishResult(const VisionResult& result) {
    if (!running_) {
        return;
    }

    // AI 结果走 DataChannel/信令适配层，网页端按 frame_id 与 video/canvas 叠加绘制。
    // MQTT 告警也可以由后端订阅后转发到同一个结果通道，实现统一可视化。
    impl_->signaling.sendAiResult(result);
}

void WebStreamer::publishAppState(const AppState& state) {
    if (!running_) {
        return;
    }

    const std::string payload = appStateToJson(state);
    // 当前 WebRTC 仍是适配层：这里先输出 AppState JSON，后续接真实 DataChannel 时复用同一数据契约。
    std::cout << "[Web] app_state " << payload << "\n";
}

void WebStreamer::stop() {
    if (impl_) {
        impl_->signaling.disconnect();
    }
    if (running_) {
        std::cout << "[Web] streamer stop\n";
    }
    running_ = false;
}

}  // namespace rv1126b
