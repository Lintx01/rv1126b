#pragma once

#include "BlockingQueue.hpp"

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace rv1126b {

class ThreadPool {
public:
    ThreadPool() = default;
    ~ThreadPool() {
        stop();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void start(std::size_t worker_count) {
        if (running_.exchange(true)) {
            return;
        }

        // 线程池适用场景：短任务、可并行、无固定硬件亲和性的工作。
        // 本项目中 MQTT JSON 组包、告警消息生成属于轻量任务，适合进入线程池。
        // 摄像头采集、模型推理、显示刷屏是持续型硬件/模型任务，保留独立线程更清晰。
        workers_.reserve(worker_count);
        for (std::size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this, i] { workerLoop(i); });
        }
    }

    bool submit(std::function<void()> task) {
        if (!running_.load()) {
            return false;
        }
        return tasks_.push(std::move(task));
    }

    void stop() {
        const bool was_running = running_.exchange(false);
        if (!was_running) {
            return;
        }

        // 先关闭任务队列，再 join 工作线程。
        // BlockingQueue::close 后仍允许 worker 取完队列中已提交的任务，避免丢失关键告警。
        tasks_.close();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }

private:
    void workerLoop(std::size_t worker_index) {
        while (true) {
            auto task = tasks_.pop();
            if (!task.has_value()) {
                break;
            }

            try {
                (*task)();
            } catch (const std::exception& e) {
                // 异常处理：线程函数内部必须兜底捕获异常，否则 C++ 线程中异常逃逸会触发 std::terminate。
                // 这里记录错误并继续处理后续任务，避免单条消息组包异常导致整个系统退出。
                std::cerr << "[ThreadPool] worker " << worker_index << " exception: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[ThreadPool] worker " << worker_index << " unknown exception\n";
            }
        }
    }

    std::atomic<bool> running_{false};
    BlockingQueue<std::function<void()>> tasks_{64};
    std::vector<std::thread> workers_;
};

}  // namespace rv1126b
