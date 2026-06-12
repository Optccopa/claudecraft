#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

namespace cc {

// Mutex-guarded MPMC queue. close() drains gracefully: blocked consumers wake
// and waitPop returns false once the queue is both closed and empty.
template <typename T>
class ConcurrentQueue {
public:
    void push(T value) {
        {
            const std::scoped_lock lock(m_mutex);
            m_items.push_back(std::move(value));
        }
        m_cv.notify_one();
    }

    [[nodiscard]] bool waitPop(T& out) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return m_closed || !m_items.empty(); });
        if (m_items.empty()) {
            return false;
        }
        out = std::move(m_items.front());
        m_items.pop_front();
        return true;
    }

    [[nodiscard]] bool tryPop(T& out) {
        const std::scoped_lock lock(m_mutex);
        if (m_items.empty()) {
            return false;
        }
        out = std::move(m_items.front());
        m_items.pop_front();
        return true;
    }

    void close() noexcept {
        {
            const std::scoped_lock lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<T> m_items;
    bool m_closed = false;
};

} // namespace cc
