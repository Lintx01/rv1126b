#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rv1126b {

class FlvMuxer {
public:
    std::vector<uint8_t> makeHeader() const;

    bool updateSpsPpsFromAnnexB(const uint8_t* data, std::size_t size);

    bool hasSequenceHeader() const;

    std::vector<uint8_t> makeSequenceHeaderTag(uint32_t timestamp_ms) const;

    std::vector<uint8_t> makeVideoTagFromAnnexB(
        const uint8_t* data,
        std::size_t size,
        uint32_t timestamp_ms,
        bool key_frame);

private:
    struct NaluView {
        const uint8_t* data{nullptr};
        std::size_t size{0};
        uint8_t type{0};
    };

    static std::vector<NaluView> splitAnnexB(const uint8_t* data, std::size_t size);
    static uint8_t naluType(const uint8_t* data, std::size_t size);
    static void appendU16(std::vector<uint8_t>& out, uint16_t value);
    static void appendU24(std::vector<uint8_t>& out, uint32_t value);
    static void appendU32(std::vector<uint8_t>& out, uint32_t value);
    static std::vector<uint8_t> makeFlvTag(uint8_t tag_type, uint32_t timestamp_ms, const std::vector<uint8_t>& payload);

    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
};

}  // namespace rv1126b
