#pragma once

#include "core/ConcurrentQueue.hpp"

#include <functional>
#include <thread>
#include <vector>

namespace cc {

class ThreadPool {
public:
    explicit ThreadPool(unsigned threadCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    void submit(std::function<void()> job);
    [[nodiscard]] unsigned threadCount() const noexcept;

private:
    ConcurrentQueue<std::function<void()>> m_jobs;
    std::vector<std::jthread> m_workers;
};

} // namespace cc
