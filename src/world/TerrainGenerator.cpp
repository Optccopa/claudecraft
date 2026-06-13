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

void leafLayer(Chunk& chunk, int lx, int y, int lz, int radius, BlockType leaves) {
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            if (std::abs(dx) == radius && std::abs(dz) == radius && radius >= 2) {
                continue; // clip corners for a rounder crown
            }
            if (chunk.at(lx + dx, y, lz + dz) == BlockType::Air) {
                chunk.set(lx + dx, y, lz + dz, leaves);
            }
        }
    }
}

// Oak: stout trunk, two wide layers + cap. Spruce: tall conical skirt of
// alternating radii. Cherry: short trunk under a broad radius-3 blossom
// canopy (this shape is why cherry trees need a 3-block chunk margin).
void plantTree(Chunk& chunk, int lx, int surfaceY, int lz, std::uint32_t hash, Biome biome) {
    BlockType trunk = BlockType::Wood;
    BlockType leaves = BlockType::Leaves;
    int trunkHeight = 4 + static_cast<int>(hash % 3u);
    if (biome == Biome::Taiga) {
        trunk = BlockType::SpruceWood;
        leaves = BlockType::SpruceLeaves;
        trunkHeight = 6 + static_cast<int>(hash % 3u);
    } else if (biome == Biome::CherryGrove) {
        trunk = BlockType::CherryWood;
        leaves = BlockType::CherryLeaves;
        trunkHeight = 4 + static_cast<int>(hash % 2u);
    }
    if (surfaceY + trunkHeight + 2 >= Chunk::SizeY) {
        return;
    }

    if (biome == Biome::Taiga) {
        for (int dy = 3; dy <= trunkHeight; ++dy) {
            leafLayer(chunk, lx, surfaceY + dy, lz, ((trunkHeight - dy) % 2 == 0) ? 1 : 2,
                      leaves);
        }
        leafLayer(chunk, lx, surfaceY + trunkHeight + 1, lz, 0, leaves);
    } else if (biome == Biome::CherryGrove) {
        leafLayer(chunk, lx, surfaceY + trunkHeight - 1, lz, 3, leaves);
        leafLayer(chunk, lx, surfaceY + trunkHeight, lz, 3, leaves);
        leafLayer(chunk, lx, surfaceY + trunkHeight + 1, lz, 2, leaves);
    } else {
        for (int dy = trunkHeight - 2; dy <= trunkHeight + 1; ++dy) {
            leafLayer(chunk, lx, surfaceY + dy, lz, (dy <= trunkHeight - 1) ? 2 : 1, leaves);
        }
    }
    for (int dy = 1; dy <= trunkHeight; ++dy) {
        chunk.set(lx, surfaceY + dy, lz, trunk);
    }
}

} // namespace

std::string_view biomeName(Biome biome) noexcept {
    switch (biome) {
    case Biome::Plains: return "plains";
    case Biome::Forest: return "forest";
    case Biome::Desert: return "desert";
    case Biome::Mountains: return "mountains";
    case Biome::Ocean: return "ocean";
    case Biome::Taiga: return "taiga";
    case Biome::CherryGrove: return "cherry grove";
    }
    return "?";
}

TerrainGenerator::TerrainGenerator(std::uint32_t seed)
    : m_heightNoise{seed},
      m_biomeNoise{seed ^ 0x9E3779B9u},
      m_moistureNoise{seed ^ 0x165667B1u},
      m_temperatureNoise{seed ^ 0xD6E8FEB8u},
      m_caveNoiseA{seed ^ 0x85EBCA6Bu},
      m_caveNoiseB{seed ^ 0xC2B2AE35u},
      m_oreNoise{seed ^ 0x27D4EB2Fu},
      m_seed{seed} {}

// One continental field drives both extremes — high values raise mountains,
// low values sink ocean basins — so the two can never overlap and the
// transition through plains-height coastline falls out for free.
TerrainGenerator::BiomeFactors TerrainGenerator::factorsAt(int wx, int wz) const noexcept {
    const float continental = m_biomeNoise.fbm(static_cast<float>(wx) * 0.0011f,
                                               static_cast<float>(wz) * 0.0011f, 2, 2.0f, 0.5f);
    const float moisture = m_moistureNoise.fbm(static_cast<float>(wx) * 0.0009f,
                                               static_cast<float>(wz) * 0.0009f, 2, 2.0f, 0.5f);
    const float temperature = m_temperatureNoise.fbm(
        static_cast<float>(wx) * 0.0008f, static_cast<float>(wz) * 0.0008f, 2, 2.0f, 0.5f);
    return BiomeFactors{
        smoothstep(0.05f, 0.42f, continental),
        smoothstep(0.16f, 0.40f, -continental),
        moisture,
        temperature,
    };
}

// Temperature splits before moisture so cold lowlands are taiga even when
// wet, and the cherry band needs both warmth and some moisture.
Biome TerrainGenerator::classify(const BiomeFactors& f) noexcept {
    if (f.ocean > 0.5f) {
        return Biome::Ocean;
    }
    if (f.mountain > 0.5f) {
        return Biome::Mountains;
    }
    if (f.temperature < -0.28f) {
        return Biome::Taiga;
    }
    if (f.temperature > 0.30f && f.moisture > 0.05f) {
        return Biome::CherryGrove;
    }
    if (f.moisture < -0.22f) {
        return Biome::Desert;
    }
    if (f.moisture > 0.28f) {
        return Biome::Forest;
    }
    return Biome::Plains;
}

Biome TerrainGenerator::biomeAt(int wx, int wz) const noexcept {
    return classify(factorsAt(wx, wz));
}

int TerrainGenerator::heightFor(const BiomeFactors& f, int wx, int wz) const noexcept {
    const float n = m_heightNoise.fbm(static_cast<float>(wx) * 0.008f,
                                      static_cast<float>(wz) * 0.008f, 4, 2.0f, 0.5f);
    const float plains = 66.0f + n * 5.0f;
    const float mountains = 84.0f + n * 52.0f;
    float h = plains + (mountains - plains) * f.mountain;
    const float oceanFloor = 46.0f + n * 5.0f;
    h += (oceanFloor - h) * f.ocean;
    return std::clamp(static_cast<int>(h), 1, Chunk::SizeY - 16);
}

int TerrainGenerator::surfaceHeight(int wx, int wz) const noexcept {
    return heightFor(factorsAt(wx, wz), wx, wz);
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
            const BiomeFactors factors = factorsAt(wx, wz);
            const Biome biome = classify(factors);
            const int h = heightFor(factors, wx, wz);

            BlockType top = BlockType::Grass;
            if (biome == Biome::Desert || h <= SeaLevel + 1) {
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
            const Biome biome = biomeAt(wx, wz);
            std::uint32_t density = 0;
            switch (biome) {
            case Biome::Plains: density = 8; break;
            case Biome::Forest: density = 55; break;
            case Biome::Taiga: density = 45; break;
            case Biome::CherryGrove: density = 30; break;
            default: break;
            }
            // Cherry canopies reach radius 3, so they need the wider margin.
            if (biome == Biome::CherryGrove &&
                (lx < 3 || lx > Chunk::SizeX - 4 || lz < 3 || lz > Chunk::SizeZ - 4)) {
                continue;
            }
            const std::uint32_t hash = hashCoords(wx, wz, m_seed);
            if (density == 0 || hash % 1000u >= density) {
                continue;
            }
            const int h = surfaceHeight(wx, wz);
            if (chunk->at(lx, h, lz) == BlockType::Grass) {
                plantTree(*chunk, lx, h, lz, hash, biome);
            }
        }
    }

    return chunk;
}

} // namespace cc
