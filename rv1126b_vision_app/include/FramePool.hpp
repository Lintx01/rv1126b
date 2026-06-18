#pragma once

#include "Types.hpp"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace rv1126b {

class FramePool {
public:
    static FramePool& instance() {
        /* 单例模式：帧内存池是全局稀缺资源，统一分配和回收，避免多模块重复申请大块图像内存。 */
        static FramePool pool;
        return pool;
    }

    void initialize(std::size_t frame_count, std::size_t frame_bytes, int width, int height, int channels) {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = false;
        free_frames_.clear();
        storage_.clear();
        storage_.reserve(frame_count);
        free_frames_.reserve(frame_count);

        for (std::size_t i = 0; i < frame_count; ++i) {
            auto frame = std::make_unique<Frame>();
            frame->width = width;
            frame->height = height;
            frame->channels = channels;
            frame->data.resize(frame_bytes);
            free_frames_.push_back(frame.get());
            storage_.push_back(std::move(frame));
        }
    }

    FramePtr acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        available_.wait(lock, [this] { return !free_frames_.empty() || closed_; });
        if (closed_) {
            return nullptr;
        }

        Frame* raw = free_frames_.back();
        free_frames_.pop_back();

        /* 零拷贝思想：队列之间只传递 shared_ptr<Frame>，不复制图像 data。 */
        /* 自定义 deleter 在最后一个消费者释放后把 Frame 归还对象池，保证内存生命周期清晰。 */
        return FramePtr(raw, [](Frame* frame) {
            FramePool::instance().release(frame);
        });
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        available_.notify_all();
    }

private:
    FramePool() = default;

    void release(Frame* frame) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            free_frames_.push_back(frame);
        }
        available_.notify_one();
    }

    std::mutex mutex_;
    std::condition_variable available_;
    std::vector<std::unique_ptr<Frame>> storage_;
    std::vector<Frame*> free_frames_;
    bool closed_{false};
};

}  // namespace rv1126b
