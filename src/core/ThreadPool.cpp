#include "core/ThreadPool.hpp"

namespace cc {

ThreadPool::ThreadPool(unsigned threadCount) {
    m_workers.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; ++i) {
        m_workers.emplace_back([this] {
            std::function<void()> job;
            while (m_jobs.waitPop(job)) {
                job();
            }
        });
    }
}

// Closing the queue lets workers drain remaining jobs, then jthread joins.
ThreadPool::~ThreadPool() {
    m_jobs.close();
}

void ThreadPool::submit(std::function<void()> job) {
    m_jobs.push(std::move(job));
}

unsigned ThreadPool::threadCount() const noexcept {
    return static_cast<unsigned>(m_workers.size());
}

} // namespace cc
