#include "core/SystemStats.hpp"

#include <windows.h>

#include <psapi.h>

namespace cc {
namespace {

constexpr double kSampleInterval = 0.5; // seconds between refreshes
constexpr double kBytesPerMB = 1024.0 * 1024.0;

[[nodiscard]] unsigned long long toU64(const FILETIME& ft) noexcept {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

} // namespace

void SystemStats::update(double nowSeconds) {
    if (m_lastSample >= 0.0 && nowSeconds - m_lastSample < kSampleInterval) {
        return;
    }
    m_lastSample = nowSeconds;

    // CPU: kernel time already includes idle, so busy = kernel + user - idle.
    // The first sample only seeds the baseline (delta needs two ticks).
    FILETIME idle;
    FILETIME kernel;
    FILETIME user;
    if (GetSystemTimes(&idle, &kernel, &user) != 0) {
        const unsigned long long idleNow = toU64(idle);
        const unsigned long long busyNow = toU64(kernel) + toU64(user) - idleNow;
        if (m_prevBusy != 0) {
            const unsigned long long busyDelta = busyNow - m_prevBusy;
            const unsigned long long idleDelta = idleNow - m_prevIdle;
            const unsigned long long total = busyDelta + idleDelta;
            m_cpuPercent = total != 0 ? 100.0 * static_cast<double>(busyDelta) /
                                            static_cast<double>(total)
                                      : 0.0;
        }
        m_prevIdle = idleNow;
        m_prevBusy = busyNow;
    }

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem) != 0) {
        m_totalRamMB = static_cast<double>(mem.ullTotalPhys) / kBytesPerMB;
        m_usedRamMB =
            static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys) / kBytesPerMB;
    }

    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) != 0) {
        m_processRamMB = static_cast<double>(pmc.WorkingSetSize) / kBytesPerMB;
    }
}

} // namespace cc
