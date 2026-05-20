#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace zautomate {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count = std::thread::hardware_concurrency())
        : shutdown_(false) {
        if (thread_count == 0) {
            thread_count = 2;
        }
        workers_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this]() {
                while (true) {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        condition_.wait(lock, [this]() { return shutdown_ || !jobs_.empty(); });
                        if (shutdown_ && jobs_.empty()) {
                            return;
                        }
                        job = std::move(jobs_.front());
                        jobs_.pop();
                    }
                    job();
                }
            });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <class Fn, class... Args>
    auto submit(Fn&& fn, Args&&... args)
        -> std::future<std::invoke_result_t<Fn, Args...>> {
        using ReturnType = std::invoke_result_t<Fn, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));

        std::future<ReturnType> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_) {
                throw std::runtime_error("ThreadPool is shut down");
            }
            jobs_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return future;
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> jobs_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool shutdown_;
};

}  // namespace zautomate
