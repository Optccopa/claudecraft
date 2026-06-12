#pragma once

#include "world/ChunkCoord.hpp"

#include <filesystem>
#include <memory>

namespace cc {

class Chunk;

// One RLE-compressed binary file per modified chunk. Stateless after
// construction, so loads may run on worker threads while saves run on the
// main thread — a chunk is never loaded and saved concurrently because only
// unloaded chunks get loaded.
class WorldSave {
public:
    explicit WorldSave(std::filesystem::path directory);

    // nullptr when the file is absent or fails validation (corrupt/old version).
    [[nodiscard]] std::unique_ptr<Chunk> tryLoad(ChunkCoord coord) const;
    void save(const Chunk& chunk) const;

private:
    [[nodiscard]] std::filesystem::path pathFor(ChunkCoord coord) const;

    std::filesystem::path m_directory;
};

} // namespace cc
