#pragma once

#include "world/Item.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cc {

struct WorldInfo {
    std::string name;          // display name == directory name
    std::uint32_t seed = 0;
    double timeOfDay = 0.05;   // fraction of a day; 0 = sunrise, 0.25 = noon
    GameMode mode = GameMode::Creative;
    std::filesystem::path directory;
    // Saved player pose and survival vitals, restored on re-entry. hasPlayer is
    // false for fresh worlds, which spawn at the default location with full
    // vitals instead.
    bool hasPlayer = false;
    float playerX = 0.0f, playerY = 0.0f, playerZ = 0.0f;
    float yaw = -90.0f, pitch = 0.0f;
    float health = 20.0f, hunger = 20.0f, saturation = 5.0f, exhaustion = 0.0f, air = 1.0f;
};

namespace worldlist {

// Worlds under savesRoot, newest first. A world is a directory with a
// world.meta; legacy "world_<seed>" directories (pre-meta saves) are
// recognised too.
[[nodiscard]] std::vector<WorldInfo> list(const std::filesystem::path& savesRoot);

// Sanitises the name into a directory (suffixing on collision), writes
// world.meta, and returns the new world's info.
[[nodiscard]] WorldInfo create(const std::filesystem::path& savesRoot, std::string_view name,
                               std::uint32_t seed, GameMode mode);

// Rewrites world.meta with the info's current mutable state (time of day).
void saveMeta(const WorldInfo& info);

} // namespace worldlist
} // namespace cc
