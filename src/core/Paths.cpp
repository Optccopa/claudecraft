#include "core/Paths.hpp"

#include "core/Log.hpp"

#include <array>
#include <cstdlib>
#include <format>

namespace cc::paths {
namespace {

// _dupenv_s over std::getenv to satisfy MSVC's secure-CRT warning.
[[nodiscard]] std::filesystem::path envPath(const char* name) {
    char* value = nullptr;
    std::size_t len = 0;
    std::filesystem::path result;
    if (_dupenv_s(&value, &len, name) == 0 && value != nullptr && value[0] != '\0') {
        result = std::filesystem::path{value};
    }
    std::free(value);
    return result;
}

[[nodiscard]] std::filesystem::path resolveBase() {
    if (std::filesystem::path local = envPath("LOCALAPPDATA"); !local.empty()) {
        return local;
    }
    if (std::filesystem::path home = envPath("USERPROFILE"); !home.empty()) {
        return home / "AppData" / "Local";
    }
    return std::filesystem::current_path();
}

// Rename when possible (same volume), else copy + delete (cross-volume).
void moveInto(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (!ec) {
        logInfo(std::format("migrated '{}' to '{}'", from.string(), to.string()));
        return;
    }
    ec.clear();
    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, ec);
    if (ec) {
        logError(std::format("could not migrate '{}': {}", from.string(), ec.message()));
        return;
    }
    std::filesystem::remove_all(from, ec);
    logInfo(std::format("migrated '{}' to '{}'", from.string(), to.string()));
}

} // namespace

std::filesystem::path dataRoot() {
    const std::filesystem::path root = resolveBase() / ".claudecraft";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root;
}

void migrateLegacy(const std::filesystem::path& dataRoot) {
    constexpr std::array<const char*, 3> kLegacy{"settings.txt", "saves", "texture_packs"};
    for (const char* name : kLegacy) {
        const std::filesystem::path from{name};
        const std::filesystem::path to = dataRoot / name;
        if (std::filesystem::exists(from) && !std::filesystem::exists(to)) {
            moveInto(from, to);
        }
    }
}

} // namespace cc::paths
