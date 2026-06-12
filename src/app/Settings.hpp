#pragma once

#include <filesystem>

namespace cc {

// User preferences, persisted as key/value text. Values are clamped to their
// valid ranges on load so a hand-edited file can't produce broken state.
struct Settings {
    int renderDistance = 12; // chunks, 4..16
    int fovDeg = 75;         // 60..110
    bool vsync = false;
    bool fullscreen = false;
    float sensitivity = 1.0f; // mouse look multiplier, 0.2..3.0
    bool invertY = false;

    [[nodiscard]] static Settings load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

} // namespace cc
