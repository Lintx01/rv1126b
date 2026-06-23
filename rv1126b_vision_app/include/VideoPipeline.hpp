#pragma once

#include "Types.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace rv1126b {

class MppEncoder {
public:
    MppEncoder();
    ~MppEncoder();

    bool open(const AppConfig& config);
    bool encode(const Frame& frame, EncodedPacket& packet);
    void close();

private:
    struct Impl;

    AppConfig config_;
    std::unique_ptr<Impl> impl_;
    bool opened_{false};
    uint64_t encoded_count_{0};
};

class MppDecoder {
public:
    bool open(const AppConfig& config);
    bool decode(const EncodedPacket& packet, Frame& frame);
    void close();

private:
    AppConfig config_;
    bool opened_{false};
};

class ImageProcessor {
public:
    bool open(const AppConfig& config);
    bool cropResize(const Frame& src, const CropRect& crop, int dst_width, int dst_height, Frame& dst);
    void close();

private:
    bool cropResizeByRga(const Frame& src, const CropRect& crop, int dst_width, int dst_height, Frame& dst);
    bool cropResizeByOpenCv(const Frame& src, const CropRect& crop, int dst_width, int dst_height, Frame& dst);
    bool cropResizeBySoftware(const Frame& src, const CropRect& crop, int dst_width, int dst_height, Frame& dst);
    static CropRect normalizeCrop(const Frame& src, const CropRect& crop);

    AppConfig config_;
    bool opened_{false};
    bool rga_available_{false};
};

std::string toString(WebStreamProtocol protocol);

}  // namespace rv1126b
