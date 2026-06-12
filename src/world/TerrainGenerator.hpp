#pragma once

#include "world/ChunkCoord.hpp"
#include "world/Noise.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace cc {

class Chunk;
enum class BlockType : std::uint8_t;

enum class Biome : std::uint8_t { Plains, Forest, Desert, Mountains, Ocean };

[[nodiscard]] std::string_view biomeName(Biome biome) noexcept;

// Deterministic terrain from a seed. Two low-frequency fields select biomes:
// a continental field blends plains into mountains and sinks ocean basins,
// a moisture field splits the lowlands into desert / plains / forest.
// Per-biome surface blocks and tree density, plus caves, ores, sea level,
// beaches and snow caps. All methods are const and thread-safe; generation
// runs on worker threads.
class TerrainGenerator {
public:
    static constexpr int SeaLevel = 62;

    explicit TerrainGenerator(std::uint32_t seed);

    [[nodiscard]] std::unique_ptr<Chunk> generate(ChunkCoord coord) const;
    [[nodiscard]] int surfaceHeight(int wx, int wz) const noexcept;
    [[nodiscard]] Biome biomeAt(int wx, int wz) const noexcept;

private:
    struct BiomeFactors {
        float mountain; // 0..1 blend toward mountain heights
        float ocean;    // 0..1 blend toward the ocean floor
        float moisture; // raw fBm, roughly -1..1
    };

    [[nodiscard]] BiomeFactors factorsAt(int wx, int wz) const noexcept;
    [[nodiscard]] static Biome classify(const BiomeFactors& factors) noexcept;
    [[nodiscard]] int heightFor(const BiomeFactors& factors, int wx, int wz) const noexcept;
    void carveAndSeed(BlockType* column, int wx, int wz, int surface) const noexcept;

    Noise m_heightNoise;
    Noise m_biomeNoise;
    Noise m_moistureNoise;
    Noise m_caveNoiseA;
    Noise m_caveNoiseB;
    Noise m_oreNoise;
    std::uint32_t m_seed;
};

} // namespace cc
