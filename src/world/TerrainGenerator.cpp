#include "world/TerrainGenerator.hpp"

#include "world/Chunk.hpp"

#include <algorithm>
#include <cmath>

namespace cc {
namespace {

constexpr int kSnowLine = 108;
constexpr int kDirtDepth = 3;

// SplitMix64-style integer hash; decides tree placement without extra noise.
[[nodiscard]] std::uint32_t hashCoords(std::int64_t x, std::int64_t z, std::uint32_t seed) noexcept {
    std::uint64_t h = static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<std::uint64_t>(z) * 0xC2B2AE3D27D4EB4Full;
    h ^= seed;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    return static_cast<std::uint32_t>(h ^ (h >> 31));
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float x) noexcept {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void plantTree(Chunk& chunk, int lx, int surfaceY, int lz, std::uint32_t hash) {
    const int trunkHeight = 4 + static_cast<int>(hash % 3u);
    const int topY = surfaceY + trunkHeight;
    if (topY + 2 >= Chunk::SizeY) {
        return;
    }
    // Canopy: two 5x5-ish layers below the top, a plus-shape cap above.
    for (int dy = trunkHeight - 2; dy <= trunkHeight + 1; ++dy) {
        const int radius = (dy <= trunkHeight - 1) ? 2 : 1;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                if (std::abs(dx) == radius && std::abs(dz) == radius && radius == 2) {
                    continue; // clip corners for a rounder crown
                }
                const int x = lx + dx;
                const int z = lz + dz;
                const int y = surfaceY + dy;
                if (chunk.at(x, y, z) == BlockType::Air) {
                    chunk.set(x, y, z, BlockType::Leaves);
                }
            }
        }
    }
    for (int dy = 1; dy <= trunkHeight; ++dy) {
        chunk.set(lx, surfaceY + dy, lz, BlockType::Wood);
    }
}

} // namespace

TerrainGenerator::TerrainGenerator(std::uint32_t seed)
    : m_heightNoise{seed},
      m_biomeNoise{seed ^ 0x9E3779B9u},
      m_caveNoiseA{seed ^ 0x85EBCA6Bu},
      m_caveNoiseB{seed ^ 0xC2B2AE35u},
      m_oreNoise{seed ^ 0x27D4EB2Fu},
      m_seed{seed} {}

float TerrainGenerator::mountainFactor(int wx, int wz) const noexcept {
    const float biome = m_biomeNoise.fbm(static_cast<float>(wx) * 0.0011f,
                                         static_cast<float>(wz) * 0.0011f, 2, 2.0f, 0.5f);
    return smoothstep(0.05f, 0.42f, biome);
}

int TerrainGenerator::surfaceHeight(int wx, int wz) const noexcept {
    const float n = m_heightNoise.fbm(static_cast<float>(wx) * 0.008f,
                                      static_cast<float>(wz) * 0.008f, 4, 2.0f, 0.5f);
    const float plains = 66.0f + n * 5.0f;
    const float mountains = 84.0f + n * 52.0f;
    const float h = plains + (mountains - plains) * mountainFactor(wx, wz);
    return std::clamp(static_cast<int>(h), 1, Chunk::SizeY - 16);
}

// Caves: two independent low-frequency 3D noise fields, carved where both
// are near zero. Each field's zero set is a 2D surface; intersecting two of
// them yields 1D winding tunnels ("spaghetti caves") instead of blobs.
// Ores: one shared 3D field with nested thresholds — rarer ores need higher
// field values and sit deeper, so veins naturally shell (coal around iron
// around gold around diamond) without four separate noise lookups.
void TerrainGenerator::carveAndSeed(BlockType* column, int wx, int wz,
                                    int surface) const noexcept {
    const auto x = static_cast<float>(wx);
    const auto z = static_cast<float>(wz);

    // Don't pierce ocean floors: a carved pocket under a water column has no
    // fluid simulation to drain it and renders as a floating water ceiling.
    const int carveTop = (surface <= SeaLevel + 1) ? surface - 4 : surface;

    for (int y = 6; y <= carveTop; ++y) {
        const BlockType block = column[y];
        if (block == BlockType::Air || block == BlockType::Water || block == BlockType::Bedrock) {
            continue;
        }
        const auto fy = static_cast<float>(y);
        const float a = m_caveNoiseA.fbm3(x * 0.030f, fy * 0.045f, z * 0.030f, 2, 2.0f, 0.5f);
        if (std::abs(a) < 0.085f) {
            const float b = m_caveNoiseB.fbm3(x * 0.030f, fy * 0.045f, z * 0.030f, 2, 2.0f, 0.5f);
            if (std::abs(b) < 0.085f) {
                column[y] = BlockType::Air;
                continue;
            }
        }
        if (block != BlockType::Stone) {
            continue;
        }
        const float ore = m_oreNoise.fbm3(x * 0.16f, fy * 0.16f, z * 0.16f, 2, 2.0f, 0.5f);
        if (ore > 0.70f && y < 16) {
            column[y] = BlockType::DiamondOre;
        } else if (ore > 0.64f && y < 34) {
            column[y] = BlockType::GoldOre;
        } else if (ore > 0.58f && y < 64) {
            column[y] = BlockType::IronOre;
        } else if (ore > 0.52f && y < 110) {
            column[y] = BlockType::CoalOre;
        }
    }
}

std::unique_ptr<Chunk> TerrainGenerator::generate(ChunkCoord coord) const {
    auto chunk = std::make_unique<Chunk>(coord);

    for (int lx = 0; lx < Chunk::SizeX; ++lx) {
        for (int lz = 0; lz < Chunk::SizeZ; ++lz) {
            const int wx = coord.x * Chunk::SizeX + lx;
            const int wz = coord.z * Chunk::SizeZ + lz;
            const int h = surfaceHeight(wx, wz);

            BlockType top = BlockType::Grass;
            if (h <= SeaLevel + 1) {
                top = BlockType::Sand;
            } else if (h >= kSnowLine) {
                top = BlockType::Snow;
            }

            BlockType* col = chunk->columnMutable(lx, lz);
            col[0] = BlockType::Bedrock;
            for (int y = 1; y <= h; ++y) {
                if (y == h) {
                    col[y] = top;
                } else if (y >= h - kDirtDepth && top != BlockType::Snow) {
                    col[y] = (top == BlockType::Sand) ? BlockType::Sand : BlockType::Dirt;
                } else {
                    col[y] = BlockType::Stone;
                }
            }
            for (int y = h + 1; y <= SeaLevel; ++y) {
                col[y] = BlockType::Water;
            }
            carveAndSeed(col, wx, wz, h);
        }
    }

    // Trees keep a 2-block margin so the canopy never crosses the chunk
    // border — avoids cross-chunk structure writes during parallel generation.
    for (int lx = 2; lx < Chunk::SizeX - 2; ++lx) {
        for (int lz = 2; lz < Chunk::SizeZ - 2; ++lz) {
            const int wx = coord.x * Chunk::SizeX + lx;
            const int wz = coord.z * Chunk::SizeZ + lz;
            const std::uint32_t hash = hashCoords(wx, wz, m_seed);
            if (hash % 1000u >= 8u) {
                continue;
            }
            const int h = surfaceHeight(wx, wz);
            if (chunk->at(lx, h, lz) == BlockType::Grass && mountainFactor(wx, wz) < 0.3f) {
                plantTree(*chunk, lx, h, lz, hash);
            }
        }
    }

    return chunk;
}

} // namespace cc
