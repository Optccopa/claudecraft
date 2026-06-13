#pragma once

#include <filesystem>

namespace cc::paths {

// Per-user data directory: %LOCALAPPDATA%/.claudecraft (created on call).
// Holds saves/, settings.txt and texture_packs/ — everything the game writes.
[[nodiscard]] std::filesystem::path dataRoot();

// One-time move of pre-existing saves/, settings.txt and texture_packs/ from
// the working directory into dataRoot, so older installs keep their worlds.
void migrateLegacy(const std::filesystem::path& dataRoot);

} // namespace cc::paths
