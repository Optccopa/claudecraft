#include "world/WorldList.hpp"

#include "core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <sstream>

namespace cc::worldlist {
namespace {

constexpr std::string_view kMetaFile = "world.meta";
// v1: seed. v2: + time of day. v3: + game mode. v4: + saved player pose.
// Readers accept all versions so old worlds open seamlessly; writers emit v4.
constexpr std::string_view kMetaPrefix = "claudecraft-world ";

[[nodiscard]] bool readMeta(const std::filesystem::path& dir, WorldInfo& out) {
    std::ifstream file(dir / kMetaFile);
    std::string header;
    if (!std::getline(file, header) || !header.starts_with(kMetaPrefix)) {
        return false;
    }
    int version = 0;
    const char* verFirst = header.data() + kMetaPrefix.size();
    const auto verResult = std::from_chars(verFirst, header.data() + header.size(), version);
    if (verResult.ec != std::errc{} || version < 1 || version > 4) {
        return false;
    }

    std::string line;
    if (!std::getline(file, line)) {
        return false;
    }
    const auto seedResult = std::from_chars(line.data(), line.data() + line.size(), out.seed);
    if (seedResult.ec != std::errc{} || seedResult.ptr != line.data() + line.size()) {
        return false;
    }
    if (version >= 2 && std::getline(file, line)) {
        if (std::from_chars(line.data(), line.data() + line.size(), out.timeOfDay).ec !=
            std::errc{}) {
            return false;
        }
    }
    if (version >= 3 && std::getline(file, line)) {
        out.mode = (line == "survival") ? GameMode::Survival : GameMode::Creative;
    }
    if (version >= 4 && std::getline(file, line)) {
        std::istringstream pose{line};
        if (pose >> out.playerX >> out.playerY >> out.playerZ >> out.yaw >> out.pitch) {
            out.hasPlayer = true;
            // Vitals were appended later; absent on early v4 worlds (defaults).
            pose >> out.health >> out.hunger >> out.saturation >> out.exhaustion >> out.air;
        }
    }
    return true;
}

// Pre-meta saves were named world_<seed>; recover the seed from the name.
[[nodiscard]] bool parseLegacyName(const std::string& name, std::uint32_t& seedOut) {
    constexpr std::string_view prefix = "world_";
    if (!name.starts_with(prefix)) {
        return false;
    }
    const char* first = name.data() + prefix.size();
    const char* last = name.data() + name.size();
    const auto result = std::from_chars(first, last, seedOut);
    return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] std::string sanitize(std::string_view name) {
    std::string out;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') {
            out.push_back(c);
        } else if (c == ' ') {
            out.push_back('_');
        }
        if (out.size() >= 24) {
            break;
        }
    }
    return out.empty() ? std::string{"world"} : out;
}

} // namespace

std::vector<WorldInfo> list(const std::filesystem::path& savesRoot) {
    std::vector<std::pair<std::filesystem::file_time_type, WorldInfo>> found;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(savesRoot, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        WorldInfo info;
        info.name = name;
        info.directory = entry.path();
        if (!readMeta(entry.path(), info) && !parseLegacyName(name, info.seed)) {
            continue;
        }
        found.emplace_back(entry.last_write_time(ec), std::move(info));
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<WorldInfo> worlds;
    worlds.reserve(found.size());
    for (auto& [time, info] : found) {
        worlds.push_back(std::move(info));
    }
    return worlds;
}

WorldInfo create(const std::filesystem::path& savesRoot, std::string_view name,
                 std::uint32_t seed, GameMode mode) {
    const std::string base = sanitize(name);
    std::string dirName = base;
    for (int suffix = 2; std::filesystem::exists(savesRoot / dirName); ++suffix) {
        dirName = std::format("{}_{}", base, suffix);
    }

    const std::filesystem::path dir = savesRoot / dirName;
    const WorldInfo info{dirName, seed, 0.05, mode, dir};
    std::filesystem::create_directories(dir);
    saveMeta(info);
    logInfo(std::format("created world '{}' (seed {}, {})", dirName, seed,
                        mode == GameMode::Survival ? "survival" : "creative"));
    return info;
}

void saveMeta(const WorldInfo& info) {
    std::ofstream meta(info.directory / kMetaFile, std::ios::trunc);
    meta << kMetaPrefix << 4 << '\n'
         << info.seed << '\n'
         << info.timeOfDay << '\n'
         << (info.mode == GameMode::Survival ? "survival" : "creative") << '\n';
    if (info.hasPlayer) {
        meta << info.playerX << ' ' << info.playerY << ' ' << info.playerZ << ' ' << info.yaw
             << ' ' << info.pitch << ' ' << info.health << ' ' << info.hunger << ' '
             << info.saturation << ' ' << info.exhaustion << ' ' << info.air << '\n';
    }
    if (!meta) {
        logError(std::format("failed to write world.meta for '{}'", info.name));
    }
}

} // namespace cc::worldlist
