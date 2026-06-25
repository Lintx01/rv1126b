// 做了什么？
//mock camera 保留
// 真实 camera 改成 /dev/video23 mplane mmap NV12
// Frame 输出格式为 PixelFormat::NV12
// 不在 CameraDevice 里做 NV12 -> RGB 转换


#include "Interfaces.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
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

#ifdef __linux__

int xioctl(int fd, unsigned long request, void* arg) {
    int ret = 0;
    do {
        ret = ::ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

std::string fourccToString(uint32_t fourcc) {
    char s[5] = {
        static_cast<char>(fourcc & 0xFF),
        static_cast<char>((fourcc >> 8) & 0xFF),
        static_cast<char>((fourcc >> 16) & 0xFF),
        static_cast<char>((fourcc >> 24) & 0xFF),
        '\0'
    };
    return std::string(s);
}

double fpsFromTimePerFrame(const v4l2_fract& time_per_frame) {
    if (time_per_frame.numerator == 0 || time_per_frame.denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(time_per_frame.denominator) /
           static_cast<double>(time_per_frame.numerator);
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
    bool streaming{false};
    uint32_t pixel_format{V4L2_PIX_FMT_NV12};
    std::size_t frame_bytes{0};
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
    impl_->fd = ::open(config_.camera_device.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (impl_->fd < 0) {
        std::cerr << "[Camera] open failed: " << config_.camera_device
                  << ", errno=" << errno
                  << ", msg=" << std::strerror(errno) << "\n";
        return false;
    }

    v4l2_capability capability{};
    if (xioctl(impl_->fd, VIDIOC_QUERYCAP, &capability) < 0) {
        std::cerr << "[Camera] VIDIOC_QUERYCAP failed, errno=" << errno
                  << ", msg=" << std::strerror(errno) << "\n";
        close();
        return false;
    }

    uint32_t caps = capability.capabilities;
    if ((capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0) {
        caps = capability.device_caps;
    }

    if ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) {
        std::cerr << "[Camera] device is not Video Capture Multiplanar: "
                  << config_.camera_device << "\n";
        close();
        return false;
    }

    if ((caps & V4L2_CAP_STREAMING) == 0) {
        std::cerr << "[Camera] device does not support streaming: "
                  << config_.camera_device << "\n";
        close();
        return false;
    }

    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.width = static_cast<uint32_t>(config_.frame_width);
    format.fmt.pix_mp.height = static_cast<uint32_t>(config_.frame_height);
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;
    format.fmt.pix_mp.num_planes = 1;

    if (xioctl(impl_->fd, VIDIOC_S_FMT, &format) < 0) {
        std::cerr << "[Camera] VIDIOC_S_FMT NV12 MPLANE failed, errno=" << errno
                  << ", msg=" << std::strerror(errno) << "\n";
        close();
        return false;
    }

    impl_->pixel_format = format.fmt.pix_mp.pixelformat;
    if (impl_->pixel_format != V4L2_PIX_FMT_NV12) {
        std::cerr << "[Camera] requested NV12 but driver returned "
                  << fourccToString(impl_->pixel_format) << "\n";
        close();
        return false;
    }

    config_.frame_width = static_cast<int>(format.fmt.pix_mp.width);
    config_.frame_height = static_cast<int>(format.fmt.pix_mp.height);

    if (config_.target_fps > 0) {
        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        if (xioctl(impl_->fd, VIDIOC_G_PARM, &parm) == 0) {
            std::cout << "[Camera][FPS] before set: actual_fps="
                      << fpsFromTimePerFrame(parm.parm.capture.timeperframe)
                      << ", timeperframe="
                      << parm.parm.capture.timeperframe.numerator << "/"
                      << parm.parm.capture.timeperframe.denominator
                      << ", capability=0x" << std::hex << parm.parm.capture.capability
                      << std::dec << "\n";
        } else {
            std::cerr << "[Camera][FPS][WARN] VIDIOC_G_PARM before set failed, errno="
                      << errno << ", msg=" << std::strerror(errno) << "\n";
        }

        parm = {};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator =
            static_cast<uint32_t>(config_.target_fps);

        if (xioctl(impl_->fd, VIDIOC_S_PARM, &parm) == 0) {
            std::cout << "[Camera][FPS] request_fps=" << config_.target_fps
                      << ", driver_return_fps="
                      << fpsFromTimePerFrame(parm.parm.capture.timeperframe)
                      << ", timeperframe="
                      << parm.parm.capture.timeperframe.numerator << "/"
                      << parm.parm.capture.timeperframe.denominator << "\n";
        } else {
            std::cerr << "[Camera][FPS][WARN] VIDIOC_S_PARM request_fps="
                      << config_.target_fps
                      << " failed, errno=" << errno
                      << ", msg=" << std::strerror(errno) << "\n";
        }

        parm = {};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (xioctl(impl_->fd, VIDIOC_G_PARM, &parm) == 0) {
            std::cout << "[Camera][FPS] after set: actual_fps="
                      << fpsFromTimePerFrame(parm.parm.capture.timeperframe)
                      << ", timeperframe="
                      << parm.parm.capture.timeperframe.numerator << "/"
                      << parm.parm.capture.timeperframe.denominator << "\n";
        } else {
            std::cerr << "[Camera][FPS][WARN] VIDIOC_G_PARM after set failed, errno="
                      << errno << ", msg=" << std::strerror(errno) << "\n";
        }
    }

    const std::size_t expected_nv12_bytes =
        static_cast<std::size_t>(config_.frame_width) *
        static_cast<std::size_t>(config_.frame_height) * 3U / 2U;

    impl_->frame_bytes = static_cast<std::size_t>(format.fmt.pix_mp.plane_fmt[0].sizeimage);
    if (impl_->frame_bytes == 0) {
        impl_->frame_bytes = expected_nv12_bytes;
    }

    v4l2_requestbuffers request{};
    request.count = 4;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    request.memory = V4L2_MEMORY_MMAP;

    if (xioctl(impl_->fd, VIDIOC_REQBUFS, &request) < 0) {
        std::cerr << "[Camera] VIDIOC_REQBUFS failed, errno=" << errno
                  << ", msg=" << std::strerror(errno) << "\n";
        close();
        return false;
    }

    if (request.count < 2) {
        std::cerr << "[Camera] insufficient mmap buffers, count="
                  << request.count << "\n";
        close();
        return false;
    }

    impl_->buffers.resize(request.count);

    for (uint32_t i = 0; i < request.count; ++i) {
        v4l2_buffer buffer{};
        v4l2_plane planes[VIDEO_MAX_PLANES]{};

        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        buffer.length = VIDEO_MAX_PLANES;
        buffer.m.planes = planes;

        if (xioctl(impl_->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            std::cerr << "[Camera] VIDIOC_QUERYBUF failed, index=" << i
                      << ", errno=" << errno
                      << ", msg=" << std::strerror(errno) << "\n";
            close();
            return false;
        }

        impl_->buffers[i].length = static_cast<std::size_t>(planes[0].length);
        impl_->buffers[i].start = ::mmap(
            nullptr,
            planes[0].length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            impl_->fd,
            planes[0].m.mem_offset);

        if (impl_->buffers[i].start == MAP_FAILED) {
            std::cerr << "[Camera] mmap failed, index=" << i
                      << ", errno=" << errno
                      << ", msg=" << std::strerror(errno) << "\n";
            impl_->buffers[i].start = nullptr;
            close();
            return false;
        }

        if (xioctl(impl_->fd, VIDIOC_QBUF, &buffer) < 0) {
            std::cerr << "[Camera] VIDIOC_QBUF initial failed, index=" << i
                      << ", errno=" << errno
                      << ", msg=" << std::strerror(errno) << "\n";
            close();
            return false;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(impl_->fd, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[Camera] VIDIOC_STREAMON failed, errno=" << errno
                  << ", msg=" << std::strerror(errno) << "\n";
        close();
        return false;
    }

    impl_->streaming = true;
    opened_ = true;

    std::cout << "[Camera] V4L2/MPLANE/MMAP open: " << config_.camera_device
              << ", size=" << config_.frame_width << "x" << config_.frame_height
              << ", format=" << fourccToString(impl_->pixel_format)
              << ", frame_bytes=" << impl_->frame_bytes
              << ", buffers=" << impl_->buffers.size() << "\n";

    return true;
#else
    opened_ = true;
    std::cout << "[Camera] non-linux stub open device: "
              << config_.camera_device << "\n";
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
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1000 / config_.target_fps));
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
                const std::size_t index =
                    static_cast<std::size_t>((y * frame.width + x) * 3);
                frame.data[index + 0] =
                    static_cast<uint8_t>((x + static_cast<int>(frame.id)) & 0xFF);
                frame.data[index + 1] =
                    static_cast<uint8_t>((y + static_cast<int>(frame.id * 2U)) & 0xFF);
                frame.data[index + 2] =
                    static_cast<uint8_t>((x + y + static_cast<int>(frame.id * 3U)) & 0xFF);
            }
        }

        return true;
    }

#ifdef __linux__
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(impl_->fd, &fds);

    timeval timeout{};
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    const int ready = ::select(impl_->fd + 1, &fds, nullptr, nullptr, &timeout);
    if (ready < 0) {
        if (errno == EINTR) {
            return false;
        }

        std::cerr << "[Camera] select error, errno=" << errno
                  << ", msg=" << std::strerror(errno) << "\n";
        return false;
    }

    if (ready == 0) {
        std::cerr << "[Camera] select timeout\n";
        return false;
    }

    v4l2_buffer buffer{};
    v4l2_plane planes[VIDEO_MAX_PLANES]{};

    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.length = VIDEO_MAX_PLANES;
    buffer.m.planes = planes;

    if (xioctl(impl_->fd, VIDIOC_DQBUF, &buffer) < 0) {
        if (errno == EAGAIN) {
            return false;
        }

        std::cerr << "[Camera] VIDIOC_DQBUF failed, errno=" << errno
                  << ", msg=" << std::strerror(errno) << "\n";
        return false;
    }

    const auto requeue = [&]() {
        buffer.length = VIDEO_MAX_PLANES;
        buffer.m.planes = planes;
        if (xioctl(impl_->fd, VIDIOC_QBUF, &buffer) < 0) {
            std::cerr << "[Camera] VIDIOC_QBUF requeue failed, errno=" << errno
                      << ", msg=" << std::strerror(errno) << "\n";
        }
    };

    if (buffer.index >= impl_->buffers.size()) {
        std::cerr << "[Camera] invalid buffer index=" << buffer.index << "\n";
        requeue();
        return false;
    }

    const auto& mmap_buffer = impl_->buffers[buffer.index];
    const auto* src = static_cast<const uint8_t*>(mmap_buffer.start);

    std::size_t used = static_cast<std::size_t>(planes[0].bytesused);
    if (used == 0) {
        used = mmap_buffer.length;
    }

    const std::size_t expected_nv12_bytes =
        static_cast<std::size_t>(config_.frame_width) *
        static_cast<std::size_t>(config_.frame_height) * 3U / 2U;

    frame.id = ++next_frame_id_;
    frame.width = config_.frame_width;
    frame.height = config_.frame_height;

    // NV12 是 Y + UV 半平面格式，不是 RGB 三通道。
    // 这里 channels 设置为 1，真正格式由 frame.format = PixelFormat::NV12 表示。
    frame.channels = 1;
    frame.format = PixelFormat::NV12;
    frame.timestamp_ms = steadyNowMs();

    frame.data.resize(expected_nv12_bytes);
    const std::size_t copy_bytes = std::min(expected_nv12_bytes, used);
    std::memcpy(frame.data.data(), src, copy_bytes);

    if (copy_bytes < expected_nv12_bytes) {
        std::memset(frame.data.data() + copy_bytes, 0,
                    expected_nv12_bytes - copy_bytes);
    }

    requeue();

    return true;
#else
    frame.id = ++next_frame_id_;
    frame.width = config_.frame_width;
    frame.height = config_.frame_height;
    frame.channels = 3;
    frame.format = PixelFormat::RGB888;
    frame.timestamp_ms = steadyNowMs();

    frame.data.resize(static_cast<std::size_t>(frame.width) *
                      static_cast<std::size_t>(frame.height) * 3U);
    std::fill(frame.data.begin(),
              frame.data.end(),
              static_cast<uint8_t>(frame.id % 255U));

    if (config_.target_fps > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1000 / config_.target_fps));
    }

    return true;
#endif
}

void CameraDevice::close() {
#ifdef __linux__
    if (impl_ && impl_->fd >= 0) {
        if (impl_->streaming) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            (void)xioctl(impl_->fd, VIDIOC_STREAMOFF, &type);
            impl_->streaming = false;
        }

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
