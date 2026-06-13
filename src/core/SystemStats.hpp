#pragma once

namespace cc {

// Process + system CPU and memory sampling for the debug overlay (Windows).
// CPU load is a delta between samples, so it needs two ticks to settle; all
// figures refresh on a throttled interval to keep the cost off the frame.
class SystemStats {
public:
    void update(double nowSeconds);

    [[nodiscard]] double cpuPercent() const noexcept { return m_cpuPercent; }
    [[nodiscard]] double processRamMB() const noexcept { return m_processRamMB; }
    [[nodiscard]] double usedRamMB() const noexcept { return m_usedRamMB; }
    [[nodiscard]] double totalRamMB() const noexcept { return m_totalRamMB; }

private:
    double m_lastSample = -1.0;
    unsigned long long m_prevIdle = 0;
    unsigned long long m_prevBusy = 0;
    double m_cpuPercent = 0.0;
    double m_processRamMB = 0.0;
    double m_usedRamMB = 0.0;
    double m_totalRamMB = 0.0;
};

} // namespace cc
