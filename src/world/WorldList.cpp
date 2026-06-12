#include "world/WorldList.hpp"

#include "core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>

namespace cc::worldlist {
namespace {

constexpr std::string_view kMetaFile = "world.meta";
constexpr std::string_view kMetaHeader = "claudecraft-world 1";

[[nodiscard]] bool readMeta(const std::filesystem::path& dir, std::uint32_t& seedOut) {
    std::ifstream file(dir / kMetaFile);
    std::string header;
    std::string seedLine;
    if (!std::getline(file, header) || header != kMetaHeader || !std::getline(file, seedLine)) {
        return false;
    }
    const auto result =
        std::from_chars(seedLine.data(), seedLine.data() + seedLine.size(), seedOut);
    return result.ec == std::errc{} && result.ptr == seedLine.data() + seedLine.size();
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
        std::uint32_t seed = 0;
        if (!readMeta(entry.path(), seed) && !parseLegacyName(name, seed)) {
            continue;
        }
        found.emplace_back(entry.last_write_time(ec), WorldInfo{name, seed, entry.path()});
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
                 std::uint32_t seed) {
    const std::string base = sanitize(name);
    std::string dirName = base;
    for (int suffix = 2; std::filesystem::exists(savesRoot / dirName); ++suffix) {
        dirName = std::format("{}_{}", base, suffix);
    }

    const std::filesystem::path dir = savesRoot / dirName;
    std::filesystem::create_directories(dir);
    std::ofstream meta(dir / kMetaFile, std::ios::trunc);
    meta << kMetaHeader << '\n' << seed << '\n';
    if (!meta) {
        logError(std::format("failed to write world.meta for '{}'", dirName));
    }
    logInfo(std::format("created world '{}' (seed {})", dirName, seed));
    return WorldInfo{dirName, seed, dir};
}

} // namespace cc::worldlist
