#include "WebRtcTransport.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace rv1126b {

namespace {

void writeU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
}

void writeU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
}

bool isStartCode3(const std::vector<uint8_t>& bytes, std::size_t index) {
    return index + 3 <= bytes.size() && bytes[index] == 0x00 && bytes[index + 1] == 0x00 && bytes[index + 2] == 0x01;
}

bool isStartCode4(const std::vector<uint8_t>& bytes, std::size_t index) {
    return index + 4 <= bytes.size() && bytes[index] == 0x00 && bytes[index + 1] == 0x00 &&
           bytes[index + 2] == 0x00 && bytes[index + 3] == 0x01;
}

}  // namespace

void H264RtpPacketizer::configure(uint8_t payload_type, uint32_t ssrc, std::size_t max_payload_size) {
    payload_type_ = payload_type;
    ssrc_ = ssrc;
    max_payload_size_ = std::max<std::size_t>(128, max_payload_size);
    sequence_number_ = 0;
}

std::vector<RtpPacket> H264RtpPacketizer::packetize(const EncodedPacket& packet) {
    std::vector<RtpPacket> result;
    const std::vector<NaluView> nalus = splitAnnexB(packet.data);
    const uint32_t rtp_timestamp = static_cast<uint32_t>(packet.timestamp_ms * 90);

    for (std::size_t i = 0; i < nalus.size(); ++i) {
        const NaluView& nalu = nalus[i];
        const bool marker = (i + 1 == nalus.size());
        const uint8_t* ptr = packet.data.data() + nalu.offset;

        std::vector<RtpPacket> nalu_packets;
        if (nalu.size <= max_payload_size_) {
            nalu_packets = packetizeSingleNalu(ptr, nalu.size, rtp_timestamp, marker);
        } else {
            nalu_packets = packetizeFuA(ptr, nalu.size, rtp_timestamp, marker);
        }

        result.insert(result.end(), nalu_packets.begin(), nalu_packets.end());
    }

    return result;
}

std::vector<H264RtpPacketizer::NaluView> H264RtpPacketizer::splitAnnexB(const std::vector<uint8_t>& bytes) const {
    std::vector<NaluView> nalus;
    std::size_t pos = 0;

    while (pos < bytes.size()) {
        std::size_t start = bytes.size();
        std::size_t start_code_size = 0;
        for (std::size_t i = pos; i < bytes.size(); ++i) {
            if (isStartCode4(bytes, i)) {
                start = i;
                start_code_size = 4;
                break;
            }
            if (isStartCode3(bytes, i)) {
                start = i;
                start_code_size = 3;
                break;
            }
        }

        if (start == bytes.size()) {
            break;
        }

        const std::size_t nalu_start = start + start_code_size;
        std::size_t next = bytes.size();
        for (std::size_t i = nalu_start; i < bytes.size(); ++i) {
            if (isStartCode4(bytes, i) || isStartCode3(bytes, i)) {
                next = i;
                break;
            }
        }

        if (next > nalu_start) {
            nalus.push_back(NaluView{nalu_start, next - nalu_start});
        }
        pos = next;
    }

    // 如果没有 Annex-B 起始码，则把整段当作一个 NALU，便于兼容上游已经去 start code 的输出。
    if (nalus.empty() && !bytes.empty()) {
        nalus.push_back(NaluView{0, bytes.size()});
    }

    return nalus;
}

RtpPacket H264RtpPacketizer::makePacket(const uint8_t* payload, std::size_t payload_size, uint32_t timestamp, bool marker) {
    RtpPacket packet;
    packet.sequence_number = sequence_number_++;
    packet.timestamp = timestamp;
    packet.ssrc = ssrc_;
    packet.marker = marker;
    packet.payload_type = payload_type_;
    packet.bytes.reserve(12 + payload_size);

    // RTP 固定头 12 字节：
    // V=2, P=0, X=0, CC=0；M=marker；PT=动态 H.264 payload type；随后是序号、时间戳、SSRC。
    packet.bytes.push_back(0x80);
    packet.bytes.push_back(static_cast<uint8_t>((marker ? 0x80 : 0x00) | (payload_type_ & 0x7FU)));
    writeU16(packet.bytes, packet.sequence_number);
    writeU32(packet.bytes, timestamp);
    writeU32(packet.bytes, ssrc_);
    packet.bytes.insert(packet.bytes.end(), payload, payload + payload_size);
    return packet;
}

std::vector<RtpPacket> H264RtpPacketizer::packetizeSingleNalu(const uint8_t* nalu, std::size_t nalu_size, uint32_t timestamp, bool marker) {
    if (nalu == nullptr || nalu_size == 0) {
        return {};
    }
    return {makePacket(nalu, nalu_size, timestamp, marker)};
}

std::vector<RtpPacket> H264RtpPacketizer::packetizeFuA(const uint8_t* nalu, std::size_t nalu_size, uint32_t timestamp, bool marker) {
    std::vector<RtpPacket> packets;
    if (nalu == nullptr || nalu_size <= 1 || max_payload_size_ <= 2) {
        return packets;
    }

    const uint8_t nalu_header = nalu[0];
    const uint8_t forbidden_nri = nalu_header & 0xE0U;
    const uint8_t nalu_type = nalu_header & 0x1FU;
    const uint8_t fu_indicator = static_cast<uint8_t>(forbidden_nri | 28U);
    const std::size_t fragment_capacity = max_payload_size_ - 2U;

    std::size_t offset = 1;
    bool first = true;
    while (offset < nalu_size) {
        const std::size_t remaining = nalu_size - offset;
        const std::size_t fragment_size = std::min(fragment_capacity, remaining);
        const bool last = (offset + fragment_size) >= nalu_size;

        std::vector<uint8_t> payload;
        payload.reserve(fragment_size + 2U);
        payload.push_back(fu_indicator);
        payload.push_back(static_cast<uint8_t>((first ? 0x80U : 0x00U) | (last ? 0x40U : 0x00U) | nalu_type));
        payload.insert(payload.end(), nalu + offset, nalu + offset + fragment_size);

        // FU-A 的 marker 只在整个视频帧最后一个 NALU 的最后一个分片上置位。
        packets.push_back(makePacket(payload.data(), payload.size(), timestamp, marker && last));
        offset += fragment_size;
        first = false;
    }

    return packets;
}

bool WebRtcSignalingClient::connect(const AppConfig& config) {
    config_ = config;
    connected_ = true;
    outbox_.clear();

    // 当前实现生成标准信令 JSON 并保存在 outbox 中。
    // 后续接 libdatachannel/WebRTC native 或 WebSocket 客户端时，只需要替换 sendJson 的实际发送层。
    std::cout << "[WebRTC] signaling adapter start, url=" << config.webrtc_signaling_url << "\n";
    return sendStartupOffer();
}

void WebRtcSignalingClient::disconnect() {
    if (connected_) {
        std::cout << "[WebRTC] signaling adapter stop\n";
    }
    connected_ = false;
    outbox_.clear();
}

bool WebRtcSignalingClient::sendStartupOffer() {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"publisher_hello\","
        << "\"device_ip\":\"" << escapeJson(config_.device_ip) << "\","
        << "\"video_track\":\"" << escapeJson(config_.webrtc_video_track_id) << "\","
        << "\"result_channel\":\"" << escapeJson(config_.webrtc_result_channel) << "\","
        << "\"codec\":\"H264\","
        << "\"payload_type\":" << static_cast<int>(config_.webrtc_h264_payload_type) << ","
        << "\"ssrc\":" << config_.webrtc_video_ssrc
        << "}";
    return sendJson(oss.str());
}

bool WebRtcSignalingClient::sendIceCandidate(const std::string& candidate, const std::string& mid, int mline_index) {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"candidate\","
        << "\"candidate\":\"" << escapeJson(candidate) << "\","
        << "\"sdpMid\":\"" << escapeJson(mid) << "\","
        << "\"sdpMLineIndex\":" << mline_index
        << "}";
    return sendJson(oss.str());
}

bool WebRtcSignalingClient::sendAiResult(const VisionResult& result) {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"ai_result\","
        << "\"channel\":\"" << escapeJson(config_.webrtc_result_channel) << "\","
        << "\"frame_id\":" << result.frame_id << ","
        << "\"bad_posture\":" << (result.bad_posture ? "true" : "false") << ","
        << "\"drink_detected\":" << (result.drink_detected ? "true" : "false") << ","
        << "\"drink_reminder\":" << (result.drink_reminder ? "true" : "false") << ","
        << "\"box_count\":" << result.boxes.size() << ","
        << "\"message\":\"" << escapeJson(result.message) << "\""
        << "}";
    return sendJson(oss.str());
}

bool WebRtcSignalingClient::sendRtpSummary(uint64_t frame_id, std::size_t packet_count, bool key_frame) {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"rtp_summary\","
        << "\"frame_id\":" << frame_id << ","
        << "\"packet_count\":" << packet_count << ","
        << "\"key_frame\":" << (key_frame ? "true" : "false")
        << "}";
    return sendJson(oss.str());
}

bool WebRtcSignalingClient::sendJson(const std::string& json) {
    if (!connected_) {
        return false;
    }

    outbox_.push_back(json);
    while (outbox_.size() > 128) {
        outbox_.pop_front();
    }

    std::cout << "[WebRTC] signaling json=" << json << "\n";
    return true;
}

std::string WebRtcSignalingClient::escapeJson(const std::string& value) {
    std::ostringstream oss;
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                oss << "\\\\";
                break;
            case '"':
                oss << "\\\"";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                oss << ch;
                break;
        }
    }
    return oss.str();
}

}  // namespace rv1126b
