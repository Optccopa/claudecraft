#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cc {

// GLFW key codes for every rebindable action. Defaults live in Settings.cpp
// (the only place this header's consumers don't need GLFW).
struct Keybinds {
    int forward;
    int back;
    int left;
    int right;
    int jump;
    int descend;
    int fly;
    int inventory;
    int drop;
};

[[nodiscard]] Keybinds defaultKeybinds() noexcept;
// Display name for a key ("W", "SPACE", "LEFT SHIFT", ...).
[[nodiscard]] std::string_view keyName(int key) noexcept;

// User preferences, persisted as key/value text. Values are clamped to their
// valid ranges on load so a hand-edited file can't produce broken state.
struct Settings {
    int renderDistance = 12; // chunks, 4..16
    int fovDeg = 75;         // 60..110
    bool vsync = false;
    bool fullscreen = false;
    bool smoothLighting = true; // per-corner AO + light vs flat per-face
    float sensitivity = 1.0f;   // mouse look multiplier, 0.2..3.0
    bool invertY = false;
    float playerSpeed = 1.0f;   // cheat: move-speed multiplier, 0.5..8.0
    float reach = 5.5f;         // cheat: block reach distance, 3..12
    Keybinds keys = defaultKeybinds();
    // Enabled resource packs, highest texture priority first (filenames under
    // resourcepacks::kRoot). Empty = built-in/procedural atlas.
    std::vector<std::string> resourcePacks;

    [[nodiscard]] static Settings load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

} // namespace cc
