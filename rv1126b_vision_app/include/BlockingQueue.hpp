#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace rv1126b {

/*
 * 生产者-消费者队列的溢出策略：
 * BLOCK 保持传统阻塞行为；DROP_OLDEST 适合实时视频帧；DROP_NEWEST 适合保留旧任务的场景。
 */
enum class QueueOverflowPolicy {
    BLOCK,
    DROP_OLDEST,
    DROP_NEWEST
};

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(
        std::size_t capacity,
        QueueOverflowPolicy policy = QueueOverflowPolicy::BLOCK)
        : capacity_(capacity), policy_(policy) {}

    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_ || capacity_ == 0) {
            return false;
        }

        // 返回 true 表示元素进入队列；DROP_NEWEST 满队列时丢弃新元素并返回 false。
        if (policy_ == QueueOverflowPolicy::BLOCK) {
            not_full_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
            if (closed_) {
                return false;
            }
        } else if (queue_.size() >= capacity_) {
            if (policy_ == QueueOverflowPolicy::DROP_OLDEST) {
                queue_.pop_front();
            } else {
                return false;
            }
        }

        queue_.push_back(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return item;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::size_t capacity_;
    QueueOverflowPolicy policy_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> queue_;
    bool closed_{false};
};

template <typename T>
class LatestFrameBuffer {
public:
    /*
     * LatestFrameBuffer 只保留最新对象，并通过 version 标记新旧。
     * AI 线程按 version 取最新帧，允许跳帧，避免处理积压旧帧导致端到端延迟升高。
     */
    bool update(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return false;
            }
            latest_ = std::move(item);
            ++version_;
        }
        not_empty_.notify_one();
        return true;
    }

    std::optional<T> getLatest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!latest_.has_value()) {
            return std::nullopt;
        }
        return *latest_;
    }

    bool getLatestIfNewer(std::uint64_t last_version, T& frame, std::uint64_t& new_version) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!latest_.has_value() || version_ <= last_version) {
            return false;
        }
        frame = *latest_;
        new_version = version_;
        return true;
    }

    bool push(T item) {
        return update(std::move(item));
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return closed_ || latest_.has_value(); });
        if (!latest_.has_value()) {
            return std::nullopt;
        }

        T item = std::move(*latest_);
        latest_.reset();
        return item;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            latest_.reset();
        }
        not_empty_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::optional<T> latest_;
    std::uint64_t version_{0};
    bool closed_{false};
};

template <typename T>
using DropOldestQueue = BlockingQueue<T>;

template <typename T>
using LatestValueBuffer = LatestFrameBuffer<T>;

}  // namespace rv1126b
