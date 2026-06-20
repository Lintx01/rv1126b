#include "FlvMuxer.hpp"

#include <algorithm>

namespace rv1126b {

namespace {

constexpr uint8_t kFlvTagVideo = 0x09;
constexpr uint8_t kAvcSequenceHeader = 0;
constexpr uint8_t kAvcNalu = 1;

bool isStartCode3(const uint8_t* data, std::size_t size, std::size_t pos) {
    return pos + 3 <= size &&
           data[pos] == 0x00 &&
           data[pos + 1] == 0x00 &&
           data[pos + 2] == 0x01;
}

bool isStartCode4(const uint8_t* data, std::size_t size, std::size_t pos) {
    return pos + 4 <= size &&
           data[pos] == 0x00 &&
           data[pos + 1] == 0x00 &&
           data[pos + 2] == 0x00 &&
           data[pos + 3] == 0x01;
}

std::size_t startCodeSize(const uint8_t* data, std::size_t size, std::size_t pos) {
    if (isStartCode4(data, size, pos)) {
        return 4;
    }
    if (isStartCode3(data, size, pos)) {
        return 3;
    }
    return 0;
}

std::size_t findStartCode(const uint8_t* data, std::size_t size, std::size_t pos) {
    while (pos + 3 <= size) {
        if (startCodeSize(data, size, pos) != 0) {
            return pos;
        }
        ++pos;
    }
    return size;
}

std::size_t trimTrailingZeros(const uint8_t* data, std::size_t begin, std::size_t end) {
    while (end > begin && data[end - 1] == 0x00) {
        --end;
    }
    return end;
}

}  // namespace

std::vector<uint8_t> FlvMuxer::makeHeader() const {
    return {
        'F', 'L', 'V',
        0x01,
        0x01,
        0x00, 0x00, 0x00, 0x09,
        0x00, 0x00, 0x00, 0x00,
    };
}

bool FlvMuxer::updateSpsPpsFromAnnexB(const uint8_t* data, std::size_t size) {
    bool updated = false;
    for (const auto& nalu : splitAnnexB(data, size)) {
        if (nalu.type == 7) {
            sps_.assign(nalu.data, nalu.data + nalu.size);
            updated = true;
        } else if (nalu.type == 8) {
            pps_.assign(nalu.data, nalu.data + nalu.size);
            updated = true;
        }
    }
    return updated;
}

bool FlvMuxer::hasSequenceHeader() const {
    return sps_.size() >= 4 && !pps_.empty();
}

std::vector<uint8_t> FlvMuxer::makeSequenceHeaderTag(uint32_t timestamp_ms) const {
    if (!hasSequenceHeader()) {
        return {};
    }

    std::vector<uint8_t> payload;
    payload.reserve(16 + sps_.size() + pps_.size());
    payload.push_back(0x17);
    payload.push_back(kAvcSequenceHeader);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);

    payload.push_back(0x01);
    payload.push_back(sps_[1]);
    payload.push_back(sps_[2]);
    payload.push_back(sps_[3]);
    payload.push_back(0xFF);
    payload.push_back(0xE1);
    appendU16(payload, static_cast<uint16_t>(sps_.size()));
    payload.insert(payload.end(), sps_.begin(), sps_.end());
    payload.push_back(0x01);
    appendU16(payload, static_cast<uint16_t>(pps_.size()));
    payload.insert(payload.end(), pps_.begin(), pps_.end());

    return makeFlvTag(kFlvTagVideo, timestamp_ms, payload);
}

std::vector<uint8_t> FlvMuxer::makeVideoTagFromAnnexB(
    const uint8_t* data,
    std::size_t size,
    uint32_t timestamp_ms,
    bool key_frame) {
    const auto nalus = splitAnnexB(data, size);
    bool contains_idr = false;

    std::vector<uint8_t> avcc;
    for (const auto& nalu : nalus) {
        if (nalu.type == 7) {
            sps_.assign(nalu.data, nalu.data + nalu.size);
            continue;
        }
        if (nalu.type == 8) {
            pps_.assign(nalu.data, nalu.data + nalu.size);
            continue;
        }
        if (nalu.type == 5) {
            contains_idr = true;
        }
        if (nalu.type != 1 && nalu.type != 5) {
            continue;
        }

        appendU32(avcc, static_cast<uint32_t>(nalu.size));
        avcc.insert(avcc.end(), nalu.data, nalu.data + nalu.size);
    }

    if (avcc.empty()) {
        return {};
    }

    const bool output_key_frame = key_frame || contains_idr;
    std::vector<uint8_t> payload;
    payload.reserve(5 + avcc.size());
    payload.push_back(static_cast<uint8_t>((output_key_frame ? 0x10 : 0x20) | 0x07));
    payload.push_back(kAvcNalu);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.insert(payload.end(), avcc.begin(), avcc.end());

    return makeFlvTag(kFlvTagVideo, timestamp_ms, payload);
}

std::vector<FlvMuxer::NaluView> FlvMuxer::splitAnnexB(const uint8_t* data, std::size_t size) {
    std::vector<NaluView> nalus;
    if (data == nullptr || size == 0) {
        return nalus;
    }

    std::size_t start = findStartCode(data, size, 0);
    while (start < size) {
        const std::size_t prefix_size = startCodeSize(data, size, start);
        if (prefix_size == 0) {
            break;
        }

        const std::size_t nalu_begin = start + prefix_size;
        const std::size_t next_start = findStartCode(data, size, nalu_begin);
        const std::size_t nalu_end = trimTrailingZeros(data, nalu_begin, next_start);
        if (nalu_end > nalu_begin) {
            nalus.push_back(NaluView{
                data + nalu_begin,
                nalu_end - nalu_begin,
                naluType(data + nalu_begin, nalu_end - nalu_begin),
            });
        }

        start = next_start;
    }

    return nalus;
}

uint8_t FlvMuxer::naluType(const uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }
    return data[0] & 0x1FU;
}

void FlvMuxer::appendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
}

void FlvMuxer::appendU24(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
}

void FlvMuxer::appendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
}

std::vector<uint8_t> FlvMuxer::makeFlvTag(uint8_t tag_type, uint32_t timestamp_ms, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> tag;
    tag.reserve(11 + payload.size() + 4);
    tag.push_back(tag_type);
    appendU24(tag, static_cast<uint32_t>(payload.size()));
    appendU24(tag, timestamp_ms & 0x00FFFFFFU);
    tag.push_back(static_cast<uint8_t>((timestamp_ms >> 24) & 0xFFU));
    tag.push_back(0x00);
    tag.push_back(0x00);
    tag.push_back(0x00);
    tag.insert(tag.end(), payload.begin(), payload.end());
    appendU32(tag, static_cast<uint32_t>(11 + payload.size()));
    return tag;
}

}  // namespace rv1126b
