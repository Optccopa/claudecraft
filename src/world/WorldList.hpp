#pragma once

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
    std::filesystem::path directory;
};

namespace worldlist {

// Worlds under savesRoot, newest first. A world is a directory with a
// world.meta; legacy "world_<seed>" directories (pre-meta saves) are
// recognised too.
[[nodiscard]] std::vector<WorldInfo> list(const std::filesystem::path& savesRoot);

// Sanitises the name into a directory (suffixing on collision), writes
// world.meta, and returns the new world's info.
[[nodiscard]] WorldInfo create(const std::filesystem::path& savesRoot, std::string_view name,
                               std::uint32_t seed);

// Rewrites world.meta with the info's current mutable state (time of day).
void saveMeta(const WorldInfo& info);

} // namespace worldlist
} // namespace cc
