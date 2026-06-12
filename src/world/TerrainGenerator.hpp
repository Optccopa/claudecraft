#pragma once

#include "world/ChunkCoord.hpp"
#include "world/Noise.hpp"

#include <cstdint>
#include <memory>

namespace cc {

class Chunk;

// Deterministic terrain from a seed: a low-frequency biome field blends plains
// into mountains, with sea level, beaches, snow caps and scattered trees.
// All methods are const and thread-safe; generation runs on worker threads.
class TerrainGenerator {
public:
    static constexpr int SeaLevel = 62;

    explicit TerrainGenerator(std::uint32_t seed);

    [[nodiscard]] std::unique_ptr<Chunk> generate(ChunkCoord coord) const;
    [[nodiscard]] int surfaceHeight(int wx, int wz) const noexcept;

private:
    [[nodiscard]] float mountainFactor(int wx, int wz) const noexcept;

    Noise m_heightNoise;
    Noise m_biomeNoise;
    std::uint32_t m_seed;
};

} // namespace cc
