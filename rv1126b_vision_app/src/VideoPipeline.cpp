#include "VideoPipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

#if defined(RV1126B_HAS_MPP)
#include <mpp_buffer.h>
#include <mpp_enc_cfg.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <rk_mpi.h>
#endif

#if defined(RV1126B_HAS_RGA)
#include <im2d.h>
#endif

#if defined(RV1126B_HAS_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace rv1126b {

namespace {

int alignTo(int value, int alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

uint8_t clipByte(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

bool isH264KeyFrame(const std::vector<uint8_t>& bytes) {
    for (std::size_t i = 0; i + 5 < bytes.size(); ++i) {
        const bool start3 = bytes[i] == 0x00 && bytes[i + 1] == 0x00 && bytes[i + 2] == 0x01;
        const bool start4 = i + 4 < bytes.size() && bytes[i] == 0x00 && bytes[i + 1] == 0x00 &&
                            bytes[i + 2] == 0x00 && bytes[i + 3] == 0x01;
        if (!start3 && !start4) {
            continue;
        }

        const std::size_t nal_index = i + (start4 ? 4U : 3U);
        const uint8_t nal_type = bytes[nal_index] & 0x1FU;
        if (nal_type == 5 || nal_type == 7 || nal_type == 8) {
            return true;
        }
    }
    return false;
}

bool convertRgbLikeToNv12(const Frame& src, int hor_stride, int ver_stride, std::vector<uint8_t>& nv12) {
    if (src.format != PixelFormat::RGB888 && src.format != PixelFormat::BGR888) {
        return false;
    }
    if (src.channels < 3 || src.width <= 0 || src.height <= 0) {
        return false;
    }

    const std::size_t y_size = static_cast<std::size_t>(hor_stride) * static_cast<std::size_t>(ver_stride);
    nv12.assign(y_size + y_size / 2U, 0x80);

    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const std::size_t src_index = static_cast<std::size_t>((y * src.width + x) * src.channels);
            const int r = src.format == PixelFormat::RGB888 ? src.data[src_index + 0] : src.data[src_index + 2];
            const int g = src.data[src_index + 1];
            const int b = src.format == PixelFormat::RGB888 ? src.data[src_index + 2] : src.data[src_index + 0];
            const int yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            nv12[static_cast<std::size_t>(y * hor_stride + x)] = clipByte(yy);
        }
    }

    for (int y = 0; y < src.height; y += 2) {
        for (int x = 0; x < src.width; x += 2) {
            int r_sum = 0;
            int g_sum = 0;
            int b_sum = 0;
            int count = 0;
            for (int dy = 0; dy < 2 && y + dy < src.height; ++dy) {
                for (int dx = 0; dx < 2 && x + dx < src.width; ++dx) {
                    const std::size_t src_index = static_cast<std::size_t>(((y + dy) * src.width + (x + dx)) * src.channels);
                    r_sum += src.format == PixelFormat::RGB888 ? src.data[src_index + 0] : src.data[src_index + 2];
                    g_sum += src.data[src_index + 1];
                    b_sum += src.format == PixelFormat::RGB888 ? src.data[src_index + 2] : src.data[src_index + 0];
                    ++count;
                }
            }

            const int r = r_sum / count;
            const int g = g_sum / count;
            const int b = b_sum / count;
            const int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            const int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
            const std::size_t uv_index = y_size + static_cast<std::size_t>((y / 2) * hor_stride + x);
            nv12[uv_index + 0] = clipByte(u);
            nv12[uv_index + 1] = clipByte(v);
        }
    }
    return true;
}

bool copyFrameToNv12Stride(const Frame& src, int hor_stride, int ver_stride, std::vector<uint8_t>& nv12) {
    const std::size_t y_size = static_cast<std::size_t>(hor_stride) * static_cast<std::size_t>(ver_stride);
    nv12.assign(y_size + y_size / 2U, 0x80);

    if (src.format == PixelFormat::NV12) {
        const std::size_t src_y_size = static_cast<std::size_t>(src.width) * static_cast<std::size_t>(src.height);
        if (src.data.size() < src_y_size + src_y_size / 2U) {
            return false;
        }
        for (int y = 0; y < src.height; ++y) {
            std::memcpy(&nv12[static_cast<std::size_t>(y * hor_stride)],
                        &src.data[static_cast<std::size_t>(y * src.width)],
                        static_cast<std::size_t>(src.width));
        }
        for (int y = 0; y < src.height / 2; ++y) {
            std::memcpy(&nv12[y_size + static_cast<std::size_t>(y * hor_stride)],
                        &src.data[src_y_size + static_cast<std::size_t>(y * src.width)],
                        static_cast<std::size_t>(src.width));
        }
        return true;
    }

    return convertRgbLikeToNv12(src, hor_stride, ver_stride, nv12);
}

#if defined(RV1126B_HAS_RGA)
int toRgaFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::RGB888:
            return RK_FORMAT_RGB_888;
        case PixelFormat::BGR888:
            return RK_FORMAT_BGR_888;
        case PixelFormat::NV12:
            return RK_FORMAT_YCbCr_420_SP;
        case PixelFormat::H264:
            return -1;
    }
    return -1;
}
#endif

#if defined(RV1126B_HAS_OPENCV)
int toOpenCvType(const Frame& frame) {
    if (frame.format == PixelFormat::NV12) {
        return CV_8UC1;
    }
    return CV_8UC(frame.channels);
}
#endif

void prepareRgbDestination(const Frame& src, int dst_width, int dst_height, Frame& dst) {
    dst.id = src.id;
    dst.width = dst_width;
    dst.height = dst_height;
    dst.channels = 3;
    dst.format = PixelFormat::RGB888;
    dst.timestamp_ms = src.timestamp_ms;
    dst.data.resize(static_cast<std::size_t>(dst_width) * static_cast<std::size_t>(dst_height) * 3U);
}

CropRect makeRgaSafeCrop(const Frame& src, const CropRect& crop) {
    CropRect normalized = crop;

    if (normalized.width <= 0 || normalized.height <= 0) {
        normalized = CropRect{0, 0, src.width, src.height};
    }

    normalized.x = std::clamp(normalized.x, 0, std::max(0, src.width - 1));
    normalized.y = std::clamp(normalized.y, 0, std::max(0, src.height - 1));
    normalized.width = std::clamp(normalized.width, 1, src.width - normalized.x);
    normalized.height = std::clamp(normalized.height, 1, src.height - normalized.y);

    // NV12/YUV420 的 crop 坐标和宽高尽量保持偶数，避免 RGA/YUV 对齐问题。
    if (src.format == PixelFormat::NV12) {
        normalized.x &= ~1;
        normalized.y &= ~1;
        normalized.width &= ~1;
        normalized.height &= ~1;

        if (normalized.width <= 0) {
            normalized.width = std::min(2, src.width - normalized.x);
        }
        if (normalized.height <= 0) {
            normalized.height = std::min(2, src.height - normalized.y);
        }

        if (normalized.x + normalized.width > src.width) {
            normalized.width = (src.width - normalized.x) & ~1;
        }
        if (normalized.y + normalized.height > src.height) {
            normalized.height = (src.height - normalized.y) & ~1;
        }
    }

    return normalized;
}

}  // namespace

struct MppEncoder::Impl {
#if defined(RV1126B_HAS_MPP)
    MppCtx ctx{nullptr};
    MppApi* mpi{nullptr};
    MppEncCfg cfg{nullptr};
    std::vector<uint8_t> header;
    int hor_stride{0};
    int ver_stride{0};
    std::size_t frame_size{0};
    bool real_mpp{false};
#endif
};

MppEncoder::MppEncoder() : impl_(std::make_unique<Impl>()) {}

MppEncoder::~MppEncoder() {
    close();
}

bool MppEncoder::open(const AppConfig& config) {
    config_ = config;
    opened_ = config.enable_mpp_encoder;
    encoded_count_ = 0;

    if (!opened_) {
        std::cout << "[MPP] encoder disabled\n";
        return true;
    }

#if defined(RV1126B_HAS_MPP)
    impl_->hor_stride = alignTo(config.frame_width, 16);
    impl_->ver_stride = alignTo(config.frame_height, 16);
    impl_->frame_size = static_cast<std::size_t>(impl_->hor_stride) * static_cast<std::size_t>(impl_->ver_stride) * 3U / 2U;

    // MPP 硬编码：RV1126B 内置 VPU，H.264 编码应交给 MPP，避免 CPU 软编码占满算力。
    // 最佳链路是 V4L2/DMABUF -> RGA 转 NV12 -> MPP；当前代码也支持 RGB/BGR 软件转 NV12 兜底。
    MPP_RET ret = mpp_create(&impl_->ctx, &impl_->mpi);
    if (ret != MPP_OK) {
        std::cerr << "[MPP] mpp_create failed, ret=" << ret << "\n";
        opened_ = false;
        return false;
    }

    ret = mpp_init(impl_->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) {
        std::cerr << "[MPP] mpp_init encoder failed, ret=" << ret << "\n";
        close();
        return false;
    }

    ret = mpp_enc_cfg_init(&impl_->cfg);
    if (ret != MPP_OK) {
        std::cerr << "[MPP] mpp_enc_cfg_init failed, ret=" << ret << "\n";
        close();
        return false;
    }

    ret = impl_->mpi->control(impl_->ctx, MPP_ENC_GET_CFG, impl_->cfg);
    if (ret != MPP_OK) {
        std::cerr << "[MPP] MPP_ENC_GET_CFG failed, ret=" << ret << "\n";
        close();
        return false;
    }

    const int fps = std::max(1, config.target_fps);
    const int bitrate = std::max(256, config.video_bitrate_kbps) * 1000;
    const int gop = std::max(1, config.video_gop);

    mpp_enc_cfg_set_s32(impl_->cfg, "prep:width", config.frame_width);
    mpp_enc_cfg_set_s32(impl_->cfg, "prep:height", config.frame_height);
    mpp_enc_cfg_set_s32(impl_->cfg, "prep:hor_stride", impl_->hor_stride);
    mpp_enc_cfg_set_s32(impl_->cfg, "prep:ver_stride", impl_->ver_stride);
    mpp_enc_cfg_set_s32(impl_->cfg, "prep:format", MPP_FMT_YUV420SP);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:bps_target", bitrate);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:bps_max", bitrate * 17 / 16);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:bps_min", bitrate * 15 / 16);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:gop", gop);
    mpp_enc_cfg_set_s32(impl_->cfg, "codec:type", MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(impl_->cfg, "h264:profile", 100);

    ret = impl_->mpi->control(impl_->ctx, MPP_ENC_SET_CFG, impl_->cfg);
    if (ret != MPP_OK) {
        std::cerr << "[MPP] MPP_ENC_SET_CFG failed, ret=" << ret << "\n";
        close();
        return false;
    }

    MppPacket header_packet = nullptr;
    ret = impl_->mpi->control(impl_->ctx, MPP_ENC_GET_EXTRA_INFO, &header_packet);
    if (ret == MPP_OK && header_packet != nullptr) {
        const auto* ptr = static_cast<const uint8_t*>(mpp_packet_get_pos(header_packet));
        const std::size_t len = static_cast<std::size_t>(mpp_packet_get_length(header_packet));
        impl_->header.assign(ptr, ptr + len);
        mpp_packet_deinit(&header_packet);
    }

    impl_->real_mpp = true;
    std::cout << "[MPP] encoder open H264, bitrate=" << config.video_bitrate_kbps
              << "kbps, gop=" << config.video_gop
              << ", stride=" << impl_->hor_stride << "x" << impl_->ver_stride << "\n";
    return true;
#else
    std::cout << "[MPP] SDK not available, encoder fallback enabled\n";
    return true;
#endif
}

bool MppEncoder::encode(const Frame& frame, EncodedPacket& packet) {
    if (!opened_) {
        return false;
    }

    ++encoded_count_;
    packet.frame_id = frame.id;
    packet.codec = PixelFormat::H264;
    packet.timestamp_ms = frame.timestamp_ms;

#if defined(RV1126B_HAS_MPP)
    if (impl_->real_mpp) {
        std::vector<uint8_t> nv12;
        if (!copyFrameToNv12Stride(frame, impl_->hor_stride, impl_->ver_stride, nv12)) {
            std::cerr << "[MPP] unsupported input format for encoder\n";
            return false;
        }

        MppBuffer buffer = nullptr;
        MppFrame mpp_frame = nullptr;
        MppPacket mpp_packet = nullptr;

        MPP_RET ret = mpp_buffer_get(nullptr, &buffer, impl_->frame_size);
        if (ret != MPP_OK || buffer == nullptr) {
            std::cerr << "[MPP] mpp_buffer_get failed, ret=" << ret << "\n";
            return false;
        }

        void* buffer_ptr = mpp_buffer_get_ptr(buffer);
        std::memcpy(buffer_ptr, nv12.data(), std::min(nv12.size(), impl_->frame_size));

        ret = mpp_frame_init(&mpp_frame);
        if (ret != MPP_OK || mpp_frame == nullptr) {
            std::cerr << "[MPP] mpp_frame_init failed, ret=" << ret << "\n";
            mpp_buffer_put(buffer);
            return false;
        }

        mpp_frame_set_width(mpp_frame, frame.width);
        mpp_frame_set_height(mpp_frame, frame.height);
        mpp_frame_set_hor_stride(mpp_frame, impl_->hor_stride);
        mpp_frame_set_ver_stride(mpp_frame, impl_->ver_stride);
        mpp_frame_set_fmt(mpp_frame, MPP_FMT_YUV420SP);
        mpp_frame_set_buffer(mpp_frame, buffer);
        mpp_frame_set_pts(mpp_frame, frame.timestamp_ms);

        ret = impl_->mpi->encode_put_frame(impl_->ctx, mpp_frame);
        if (ret == MPP_OK) {
            ret = impl_->mpi->encode_get_packet(impl_->ctx, &mpp_packet);
        }

        if (ret != MPP_OK || mpp_packet == nullptr) {
            std::cerr << "[MPP] encode failed, ret=" << ret << "\n";
            mpp_frame_deinit(&mpp_frame);
            mpp_buffer_put(buffer);
            return false;
        }

        const auto* packet_ptr = static_cast<const uint8_t*>(mpp_packet_get_pos(mpp_packet));
        const std::size_t packet_len = static_cast<std::size_t>(mpp_packet_get_length(mpp_packet));
        std::vector<uint8_t> raw_packet(packet_ptr, packet_ptr + packet_len);

        packet.key_frame = isH264KeyFrame(raw_packet);
        packet.data.clear();
        if (packet.key_frame && !impl_->header.empty()) {
            packet.data.insert(packet.data.end(), impl_->header.begin(), impl_->header.end());
        }
        packet.data.insert(packet.data.end(), raw_packet.begin(), raw_packet.end());
        packet.key_frame = packet.key_frame || isH264KeyFrame(packet.data);

        mpp_packet_deinit(&mpp_packet);
        mpp_frame_deinit(&mpp_frame);
        mpp_buffer_put(buffer);
        return true;
    }
#endif

    packet.key_frame = (encoded_count_ == 1) || (config_.video_gop > 0 && encoded_count_ % config_.video_gop == 0);
    // fallback Annex-B 伪码流：用于无 MPP SDK 的普通环境自检，不代表真实编码质量。
    packet.data = {0x00, 0x00, 0x00, 0x01, static_cast<uint8_t>(packet.key_frame ? 0x65 : 0x41),
                   static_cast<uint8_t>(frame.id & 0xFFU)};
    return true;
}

void MppEncoder::close() {
#if defined(RV1126B_HAS_MPP)
    if (impl_) {
        if (impl_->cfg != nullptr) {
            mpp_enc_cfg_deinit(impl_->cfg);
            impl_->cfg = nullptr;
        }
        if (impl_->ctx != nullptr) {
            mpp_destroy(impl_->ctx);
            impl_->ctx = nullptr;
            impl_->mpi = nullptr;
        }
        impl_->header.clear();
        impl_->real_mpp = false;
    }
#endif
    if (opened_) {
        std::cout << "[MPP] encoder close\n";
    }
    opened_ = false;
}

bool MppDecoder::open(const AppConfig& config) {
    config_ = config;
    opened_ = config.enable_mpp_decoder;

    if (!opened_) {
        std::cout << "[MPP] decoder disabled\n";
        return true;
    }

    // MPP 硬解码：当输入源已经是 H.264/H.265 码流时，优先用 MPP 解码成 NV12。
    std::cout << "[MPP] decoder open H264\n";
    return true;
}

bool MppDecoder::decode(const EncodedPacket& packet, Frame& frame) {
    if (!opened_) {
        return false;
    }

    // 桩解码：真实实现中应调用 mpp_decode_put_packet / mpp_decode_get_frame。
    frame.id = packet.frame_id;
    frame.timestamp_ms = packet.timestamp_ms;
    frame.width = config_.frame_width;
    frame.height = config_.frame_height;
    frame.channels = 3;
    frame.format = PixelFormat::RGB888;
    frame.data.assign(static_cast<std::size_t>(frame.width * frame.height * frame.channels), 0);
    return true;
}

void MppDecoder::close() {
    if (opened_) {
        std::cout << "[MPP] decoder close\n";
    }
    opened_ = false;
}

bool ImageProcessor::open(const AppConfig& config) {
    config_ = config;
    opened_ = true;

    // RGA 是 Rockchip 2D 图像加速器，适合裁剪、缩放、颜色空间转换。
    // 本类优先走 RGA，RGA 不存在或处理失败时走 OpenCV，最后用软件最近邻兜底，保证链路不中断。
#if defined(RV1126B_HAS_RGA)
    rga_available_ = config.use_rga_preprocess;
#else
    rga_available_ = false;
#endif

    std::cout << "[ImageProcessor] rga=" << (rga_available_ ? "enabled" : "disabled")
              << ", opencv_fallback=" << (config.fallback_to_opencv ? "true" : "false")
#if defined(RV1126B_HAS_OPENCV)
              << ", opencv=available"
#else
              << ", opencv=not_found"
#endif
              << "\n";
    return true;
}

bool ImageProcessor::cropResize(const Frame& src, const CropRect& crop, int dst_width, int dst_height, Frame& dst) {
    if (!opened_ || src.data.empty() || dst_width <= 0 || dst_height <= 0) {
        return false;
    }

    if (rga_available_ && cropResizeByRga(src, crop, dst_width, dst_height, dst)) {
        return true;
    }

    if (config_.fallback_to_opencv && cropResizeByOpenCv(src, crop, dst_width, dst_height, dst)) {
        return true;
    }

    return cropResizeBySoftware(src, crop, dst_width, dst_height, dst);
}

void ImageProcessor::close() {
    if (opened_) {
        std::cout << "[ImageProcessor] close\n";
    }
    opened_ = false;
    rga_available_ = false;
}

bool ImageProcessor::cropResizeByRga(
    const Frame& src,
    const CropRect& crop,
    int dst_width,
    int dst_height,
    Frame& dst) {
#if defined(RV1126B_HAS_RGA)
    if (src.data.empty() || src.width <= 0 || src.height <= 0 ||
        dst_width <= 0 || dst_height <= 0) {
        return false;
    }

    const int src_format = toRgaFormat(src.format);
    if (src_format < 0) {
        std::cerr << "[RGA] unsupported source pixel format="
                  << static_cast<int>(src.format) << "\n";
        return false;
    }

    const CropRect normalized = makeRgaSafeCrop(src, crop);

    prepareRgbDestination(src, dst_width, dst_height, dst);

    rga_buffer_t src_buffer = wrapbuffer_virtualaddr(
        const_cast<uint8_t*>(src.data.data()),
        src.width,
        src.height,
        src_format,
        src.width,
        src.height);

    rga_buffer_t dst_buffer = wrapbuffer_virtualaddr(
        dst.data.data(),
        dst.width,
        dst.height,
        RK_FORMAT_RGB_888,
        dst.width,
        dst.height);

    im_rect src_rect{};
    src_rect.x = normalized.x;
    src_rect.y = normalized.y;
    src_rect.width = normalized.width;
    src_rect.height = normalized.height;

    im_rect dst_rect{};
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = dst.width;
    dst_rect.height = dst.height;

    IM_STATUS check = imcheck(src_buffer, dst_buffer, src_rect, dst_rect, IM_SYNC);
    if (check != IM_STATUS_NOERROR) {
        std::cerr << "[RGA] imcheck failed: "
                  << imStrError(check)
                  << ", src=" << src.width << "x" << src.height
                  << ", crop=(" << src_rect.x << "," << src_rect.y
                  << "," << src_rect.width << "," << src_rect.height << ")"
                  << ", dst=" << dst.width << "x" << dst.height
                  << ", src_format=" << src_format
                  << "\n";
        return false;
    }

    IM_STATUS status = improcess(
        src_buffer,
        dst_buffer,
        {},
        src_rect,
        dst_rect,
        {},
        IM_SYNC);

    if (status != IM_STATUS_NOERROR) {
        std::cerr << "[RGA] improcess failed: "
                  << imStrError(status)
                  << ", src=" << src.width << "x" << src.height
                  << ", dst=" << dst.width << "x" << dst.height
                  << "\n";
        return false;
    }

    return true;
#else
    (void)src;
    (void)crop;
    (void)dst_width;
    (void)dst_height;
    (void)dst;
    return false;
#endif
}

bool ImageProcessor::cropResizeByOpenCv(
    const Frame& src,
    const CropRect& crop,
    int dst_width,
    int dst_height,
    Frame& dst) {
#if defined(RV1126B_HAS_OPENCV)
    if (src.data.empty() || src.width <= 0 || src.height <= 0 ||
        dst_width <= 0 || dst_height <= 0) {
        return false;
    }

    const CropRect normalized = normalizeCrop(src, crop);

    cv::Mat rgb_src;

    if (src.format == PixelFormat::RGB888 || src.format == PixelFormat::BGR888) {
        if (src.channels < 3) {
            return false;
        }

        cv::Mat input(
            src.height,
            src.width,
            CV_8UC3,
            const_cast<uint8_t*>(src.data.data()));

        if (src.format == PixelFormat::RGB888) {
            rgb_src = input;
        } else {
            cv::cvtColor(input, rgb_src, cv::COLOR_BGR2RGB);
        }
    } else if (src.format == PixelFormat::NV12) {
        const std::size_t required_size =
            static_cast<std::size_t>(src.width) *
            static_cast<std::size_t>(src.height) * 3U / 2U;

        if (src.data.size() < required_size) {
            std::cerr << "[OpenCV] NV12 data too small, data="
                      << src.data.size()
                      << ", required=" << required_size << "\n";
            return false;
        }

        cv::Mat nv12(
            src.height + src.height / 2,
            src.width,
            CV_8UC1,
            const_cast<uint8_t*>(src.data.data()));

        cv::cvtColor(nv12, rgb_src, cv::COLOR_YUV2RGB_NV12);
    } else {
        std::cerr << "[OpenCV] unsupported source format="
                  << static_cast<int>(src.format) << "\n";
        return false;
    }

    cv::Rect roi(
        normalized.x,
        normalized.y,
        normalized.width,
        normalized.height);

    if (roi.x < 0 || roi.y < 0 ||
        roi.x + roi.width > rgb_src.cols ||
        roi.y + roi.height > rgb_src.rows) {
        std::cerr << "[OpenCV] invalid crop roi\n";
        return false;
    }

    cv::Mat cropped = rgb_src(roi);
    cv::Mat resized;

    cv::resize(cropped, resized, cv::Size(dst_width, dst_height), 0, 0, cv::INTER_LINEAR);

    dst.id = src.id;
    dst.width = dst_width;
    dst.height = dst_height;
    dst.channels = 3;
    dst.format = PixelFormat::RGB888;
    dst.timestamp_ms = src.timestamp_ms;

    const std::size_t output_size =
        static_cast<std::size_t>(dst_width) *
        static_cast<std::size_t>(dst_height) * 3U;

    dst.data.resize(output_size);
    std::memcpy(dst.data.data(), resized.data, output_size);

    return true;
#else
    (void)src;
    (void)crop;
    (void)dst_width;
    (void)dst_height;
    (void)dst;
    return false;
#endif
}

bool ImageProcessor::cropResizeBySoftware(
    const Frame& src,
    const CropRect& crop,
    int dst_width,
    int dst_height,
    Frame& dst) {
    if (src.data.empty() || src.width <= 0 || src.height <= 0 ||
        dst_width <= 0 || dst_height <= 0) {
        return false;
    }

    const CropRect normalized = normalizeCrop(src, crop);
    prepareRgbDestination(src, dst_width, dst_height, dst);

    if (src.format == PixelFormat::RGB888 || src.format == PixelFormat::BGR888) {
        if (src.channels < 3) {
            return false;
        }

        for (int y = 0; y < dst_height; ++y) {
            const int src_y = normalized.y + y * normalized.height / dst_height;

            for (int x = 0; x < dst_width; ++x) {
                const int src_x = normalized.x + x * normalized.width / dst_width;

                const std::size_t src_index =
                    static_cast<std::size_t>((src_y * src.width + src_x) * src.channels);

                const std::size_t dst_index =
                    static_cast<std::size_t>((y * dst_width + x) * 3);

                if (src.format == PixelFormat::BGR888) {
                    dst.data[dst_index + 0] = src.data[src_index + 2];
                    dst.data[dst_index + 1] = src.data[src_index + 1];
                    dst.data[dst_index + 2] = src.data[src_index + 0];
                } else {
                    dst.data[dst_index + 0] = src.data[src_index + 0];
                    dst.data[dst_index + 1] = src.data[src_index + 1];
                    dst.data[dst_index + 2] = src.data[src_index + 2];
                }
            }
        }

        return true;
    }

    if (src.format == PixelFormat::NV12) {
        const std::size_t y_plane_size =
            static_cast<std::size_t>(src.width) *
            static_cast<std::size_t>(src.height);

        const std::size_t required_size = y_plane_size + y_plane_size / 2U;

        if (src.data.size() < required_size) {
            std::cerr << "[ImageProcessor] NV12 data too small, data="
                      << src.data.size()
                      << ", required=" << required_size << "\n";
            return false;
        }

        for (int y = 0; y < dst_height; ++y) {
            const int src_y = normalized.y + y * normalized.height / dst_height;
            const int clamped_src_y = std::clamp(src_y, 0, src.height - 1);

            for (int x = 0; x < dst_width; ++x) {
                const int src_x = normalized.x + x * normalized.width / dst_width;
                const int clamped_src_x = std::clamp(src_x, 0, src.width - 1);

                const std::size_t y_index =
                    static_cast<std::size_t>(clamped_src_y) *
                    static_cast<std::size_t>(src.width) +
                    static_cast<std::size_t>(clamped_src_x);

                const int uv_y = clamped_src_y / 2;
                const int uv_x = (clamped_src_x / 2) * 2;

                const std::size_t uv_index =
                    y_plane_size +
                    static_cast<std::size_t>(uv_y) *
                    static_cast<std::size_t>(src.width) +
                    static_cast<std::size_t>(uv_x);

                if (uv_index + 1 >= src.data.size()) {
                    return false;
                }

                const int yy = static_cast<int>(src.data[y_index]);
                const int uu = static_cast<int>(src.data[uv_index]) - 128;
                const int vv = static_cast<int>(src.data[uv_index + 1]) - 128;

                const int rr = yy + ((1436 * vv) >> 10);
                const int gg = yy - ((352 * uu + 731 * vv) >> 10);
                const int bb = yy + ((1815 * uu) >> 10);

                const std::size_t dst_index =
                    static_cast<std::size_t>((y * dst_width + x) * 3);

                dst.data[dst_index + 0] = clipByte(rr);
                dst.data[dst_index + 1] = clipByte(gg);
                dst.data[dst_index + 2] = clipByte(bb);
            }
        }

        return true;
    }

    std::cerr << "[ImageProcessor] software fallback unsupported format="
              << static_cast<int>(src.format) << "\n";
    return false;
}

CropRect ImageProcessor::normalizeCrop(const Frame& src, const CropRect& crop) {
    CropRect normalized = crop;
    if (normalized.width <= 0 || normalized.height <= 0) {
        normalized = CropRect{0, 0, src.width, src.height};
    }

    normalized.x = std::clamp(normalized.x, 0, std::max(0, src.width - 1));
    normalized.y = std::clamp(normalized.y, 0, std::max(0, src.height - 1));
    normalized.width = std::clamp(normalized.width, 1, src.width - normalized.x);
    normalized.height = std::clamp(normalized.height, 1, src.height - normalized.y);
    return normalized;
}

std::string toString(WebStreamProtocol protocol) {
    switch (protocol) {
        case WebStreamProtocol::WebRTC:
            return "WebRTC";
        case WebStreamProtocol::WebSocket:
            return "WebSocket";
        case WebStreamProtocol::Mjpeg:
            return "MJPEG";
    }
    throw std::runtime_error("unknown web stream protocol");
}

}  // namespace rv1126b
