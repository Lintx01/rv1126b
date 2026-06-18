#pragma once

#include "Types.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace rv1126b {

class H264RtpPacketizer {
public:
    void configure(uint8_t payload_type, uint32_t ssrc, std::size_t max_payload_size);
    std::vector<RtpPacket> packetize(const EncodedPacket& packet);

private:
    struct NaluView {
        std::size_t offset{0};
        std::size_t size{0};
    };

    std::vector<NaluView> splitAnnexB(const std::vector<uint8_t>& bytes) const;
    RtpPacket makePacket(const uint8_t* payload, std::size_t payload_size, uint32_t timestamp, bool marker);
    std::vector<RtpPacket> packetizeSingleNalu(const uint8_t* nalu, std::size_t nalu_size, uint32_t timestamp, bool marker);
    std::vector<RtpPacket> packetizeFuA(const uint8_t* nalu, std::size_t nalu_size, uint32_t timestamp, bool marker);

    uint8_t payload_type_{96};
    uint32_t ssrc_{0x1126B001U};
    std::size_t max_payload_size_{1200};
    uint16_t sequence_number_{0};
};

class WebRtcSignalingClient {
public:
    bool connect(const AppConfig& config);
    void disconnect();

    bool sendStartupOffer();
    bool sendIceCandidate(const std::string& candidate, const std::string& mid, int mline_index);
    bool sendAiResult(const VisionResult& result);
    bool sendRtpSummary(uint64_t frame_id, std::size_t packet_count, bool key_frame);

private:
    bool sendJson(const std::string& json);
    static std::string escapeJson(const std::string& value);

    AppConfig config_;
    bool connected_{false};
    std::deque<std::string> outbox_;
};

}  // namespace rv1126b
