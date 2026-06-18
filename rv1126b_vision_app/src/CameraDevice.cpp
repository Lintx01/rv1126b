#include "Interfaces.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#endif

namespace rv1126b {

namespace {

int64_t steadyNowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

uint8_t clampToByte(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

#ifdef __linux__
int xioctl(int fd, unsigned long request, void* arg) {
    int ret = 0;
    do {
        ret = ::ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

void yuyvToRgb24(const uint8_t* src, std::size_t src_size, Frame& frame) {
    const std::size_t pixel_count = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    frame.data.resize(pixel_count * 3U);

    std::size_t out = 0;
    for (std::size_t i = 0; i + 3 < src_size && out + 5 < frame.data.size(); i += 4) {
        const int y0 = src[i + 0];
        const int u = src[i + 1] - 128;
        const int y1 = src[i + 2];
        const int v = src[i + 3] - 128;

        const auto write_pixel = [&](int y) {
            const int c = y - 16;
            const int r = (298 * c + 409 * v + 128) >> 8;
            const int g = (298 * c - 100 * u - 208 * v + 128) >> 8;
            const int b = (298 * c + 516 * u + 128) >> 8;
            frame.data[out++] = clampToByte(r);
            frame.data[out++] = clampToByte(g);
            frame.data[out++] = clampToByte(b);
        };

        write_pixel(y0);
        write_pixel(y1);
    }
}
#endif

}  // namespace

struct CameraDevice::Impl {
#ifdef __linux__
    struct MmapBuffer {
        void* start{nullptr};
        std::size_t length{0};
    };

    int fd{-1};
    uint32_t pixel_format{0};
    std::vector<MmapBuffer> buffers;
#endif
};

CameraDevice::CameraDevice() : impl_(std::make_unique<Impl>()) {}

CameraDevice::~CameraDevice() {
    close();
}

bool CameraDevice::open(const AppConfig& config) {
    config_ = config;
    next_frame_id_ = 0;

    if (config_.enable_mock_camera) {
        opened_ = true;
        std::cout << "[CameraMock] open width=" << config_.frame_width
                  << ", height=" << config_.frame_height
                  << ", fps=" << config_.target_fps << "\n";
        return true;
    }

#ifdef __linux__
    impl_->fd = ::open(config.camera_device.c_str(), O_RDWR);
    if (impl_->fd < 0) {
        std::cerr << "[Camera] open failed: " << config.camera_device << ", errno=" << errno << "\n";
        return false;
    }

    v4l2_capability capability{};
    if (xioctl(impl_->fd, VIDIOC_QUERYCAP, &capability) < 0) {
        std::cerr << "[Camera] VIDIOC_QUERYCAP failed\n";
        close();
        return false;
    }

    if ((capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0 ||
        (capability.capabilities & V4L2_CAP_STREAMING) == 0) {
        std::cerr << "[Camera] device does not support capture streaming\n";
        close();
        return false;
    }

    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = static_cast<uint32_t>(config.frame_width);
    format.fmt.pix.height = static_cast<uint32_t>(config.frame_height);
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    format.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(impl_->fd, VIDIOC_S_FMT, &format) < 0 || format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB24) {
        // 兼容路径：很多 USB 摄像头默认输出 YUYV。这里先采 YUYV，再在 CPU 转 RGB。
        // 后续在 RV1126B 上建议改为 NV12/DMABUF，然后交给 RGA 做颜色转换，减少 CPU 拷贝。
        format = {};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = static_cast<uint32_t>(config.frame_width);
        format.fmt.pix.height = static_cast<uint32_t>(config.frame_height);
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        if (xioctl(impl_->fd, VIDIOC_S_FMT, &format) < 0) {
            std::cerr << "[Camera] VIDIOC_S_FMT RGB24/YUYV failed\n";
            close();
            return false;
        }
    }

    impl_->pixel_format = format.fmt.pix.pixelformat;
    config_.frame_width = static_cast<int>(format.fmt.pix.width);
    config_.frame_height = static_cast<int>(format.fmt.pix.height);

    v4l2_requestbuffers request{};
    request.count = 4;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(impl_->fd, VIDIOC_REQBUFS, &request) < 0 || request.count < 2) {
        std::cerr << "[Camera] VIDIOC_REQBUFS failed\n";
        close();
        return false;
    }

    impl_->buffers.resize(request.count);
    for (uint32_t i = 0; i < request.count; ++i) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        if (xioctl(impl_->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            std::cerr << "[Camera] VIDIOC_QUERYBUF failed\n";
            close();
            return false;
        }

        impl_->buffers[i].length = buffer.length;
        impl_->buffers[i].start = ::mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, impl_->fd, buffer.m.offset);
        if (impl_->buffers[i].start == MAP_FAILED) {
            std::cerr << "[Camera] mmap failed\n";
            close();
            return false;
        }

        if (xioctl(impl_->fd, VIDIOC_QBUF, &buffer) < 0) {
            std::cerr << "[Camera] VIDIOC_QBUF failed\n";
            close();
            return false;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(impl_->fd, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[Camera] VIDIOC_STREAMON failed\n";
        close();
        return false;
    }

    opened_ = true;
    std::cout << "[Camera] V4L2/MMAP open: " << config.camera_device
              << ", size=" << config_.frame_width << "x" << config_.frame_height
              << ", format=" << (impl_->pixel_format == V4L2_PIX_FMT_RGB24 ? "RGB24" : "YUYV") << "\n";
    return true;
#else
    opened_ = true;
    std::cout << "[Camera] non-linux stub open device: " << config.camera_device << "\n";
    return true;
#endif
}

bool CameraDevice::read(Frame& frame) {
    if (!opened_) {
        return false;
    }

    if (config_.enable_mock_camera) {
        if (config_.mock_camera_frame_count > 0 &&
            next_frame_id_ >= static_cast<uint64_t>(config_.mock_camera_frame_count)) {
            return false;
        }

        if (config_.target_fps > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / config_.target_fps));
        }

        frame.id = ++next_frame_id_;
        frame.width = config_.frame_width;
        frame.height = config_.frame_height;
        frame.channels = 3;
        frame.format = PixelFormat::RGB888;
        frame.timestamp_ms = steadyNowMs();
        frame.data.resize(static_cast<std::size_t>(frame.width) *
                          static_cast<std::size_t>(frame.height) * 3U);

        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                const std::size_t index = static_cast<std::size_t>((y * frame.width + x) * 3);
                frame.data[index + 0] = static_cast<uint8_t>((x + static_cast<int>(frame.id)) & 0xFF);
                frame.data[index + 1] = static_cast<uint8_t>((y + static_cast<int>(frame.id * 2U)) & 0xFF);
                frame.data[index + 2] = static_cast<uint8_t>((x + y + static_cast<int>(frame.id * 3U)) & 0xFF);
            }
        }
        return true;
    }

    frame.id = ++next_frame_id_;
    frame.width = config_.frame_width;
    frame.height = config_.frame_height;
    frame.channels = 3;
    frame.format = PixelFormat::RGB888;
    frame.timestamp_ms = steadyNowMs();

#ifdef __linux__
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(impl_->fd, &fds);

    timeval timeout{};
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    const int ready = ::select(impl_->fd + 1, &fds, nullptr, nullptr, &timeout);
    if (ready <= 0) {
        std::cerr << "[Camera] select timeout or error\n";
        return false;
    }

    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (xioctl(impl_->fd, VIDIOC_DQBUF, &buffer) < 0) {
        std::cerr << "[Camera] VIDIOC_DQBUF failed\n";
        return false;
    }

    const auto requeue = [&]() {
        if (xioctl(impl_->fd, VIDIOC_QBUF, &buffer) < 0) {
            std::cerr << "[Camera] VIDIOC_QBUF requeue failed\n";
        }
    };

    const auto& mmap_buffer = impl_->buffers[buffer.index];
    const auto* src = static_cast<const uint8_t*>(mmap_buffer.start);
    const std::size_t used = static_cast<std::size_t>(buffer.bytesused);

    if (impl_->pixel_format == V4L2_PIX_FMT_RGB24) {
        const std::size_t expected = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 3U;
        frame.data.resize(expected);
        std::memcpy(frame.data.data(), src, std::min(expected, used));
    } else if (impl_->pixel_format == V4L2_PIX_FMT_YUYV) {
        yuyvToRgb24(src, used, frame);
    } else {
        requeue();
        return false;
    }

    // V4L2/MMAP 是生产级采集基础：内核驱动把帧写入 mmap buffer，用户态避免 read() 额外复制。
    // 当前仍需复制到 FramePool，后续可升级为 DMABUF fd 在 Camera/RGA/MPP 间传递，实现更完整的零拷贝。
    requeue();
    return true;
#else
    frame.data.resize(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 3U);
    std::fill(frame.data.begin(), frame.data.end(), static_cast<uint8_t>(frame.id % 255U));
    if (config_.target_fps > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / config_.target_fps));
    }
    return true;
#endif
}

void CameraDevice::close() {
#ifdef __linux__
    if (impl_ && impl_->fd >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        (void)xioctl(impl_->fd, VIDIOC_STREAMOFF, &type);

        for (auto& buffer : impl_->buffers) {
            if (buffer.start != nullptr && buffer.start != MAP_FAILED) {
                ::munmap(buffer.start, buffer.length);
                buffer.start = nullptr;
            }
        }
        impl_->buffers.clear();
        ::close(impl_->fd);
        impl_->fd = -1;
    }
#endif

    if (opened_) {
        std::cout << "[Camera] close\n";
    }
    opened_ = false;
}

}  // namespace rv1126b
