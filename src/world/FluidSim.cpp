#include "world/FluidSim.hpp"

#include "world/Chunk.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>

namespace cc {
namespace {

constexpr std::array<glm::ivec3, 4> kHoriz{
    {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
constexpr std::array<glm::ivec3, 6> kAll{
    {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};

// Water replaces air and non-solid plants as it spreads; solids block it.
[[nodiscard]] bool fluidReplaceable(BlockType type) noexcept {
    return type == BlockType::Air || isLiquid(type) || isCrossPlant(type);
}

} // namespace

Chunk* FluidSim::cachedChunk(World& world, const glm::ivec3& pos) noexcept {
    if (pos.y < 0 || pos.y >= Chunk::SizeY) {
        return nullptr;
    }
    const ChunkCoord c = World::chunkCoordOf(pos.x, pos.z);
    if (c != m_cache.coord || m_cache.chunk == nullptr) {
        m_cache = ChunkRef{c, world.chunkAt(c)};
    }
    return m_cache.chunk;
}

BlockType FluidSim::readBlock(World& world, const glm::ivec3& pos) noexcept {
    if (pos.y < 0) {
        return BlockType::Bedrock; // world floor — never flow below it
    }
    if (pos.y >= Chunk::SizeY) {
        return BlockType::Air;
    }
    const Chunk* chunk = cachedChunk(world, pos);
    // Unloaded neighbours read as solid so liquid never spills off the edge of
    // the loaded region (the write would fail there anyway).
    return chunk == nullptr ? BlockType::Bedrock : chunk->at(pos.x & 15, pos.y, pos.z & 15);
}

std::uint8_t FluidSim::readFluid(World& world, const glm::ivec3& pos) noexcept {
    if (pos.y < 0 || pos.y >= Chunk::SizeY) {
        return 0;
    }
    const Chunk* chunk = cachedChunk(world, pos);
    return chunk == nullptr ? 0 : chunk->fluidAt(pos.x & 15, pos.y, pos.z & 15);
}

// The level this cell should settle to: full when fed from directly above
// (falling), otherwise one below the strongest grounded horizontal neighbour.
std::uint8_t FluidSim::computeLevel(World& world, const glm::ivec3& pos) noexcept {
    std::uint8_t best = 0;
    if (isLiquid(readBlock(world, pos + glm::ivec3{0, 1, 0}))) {
        best = MaxFlow; // fed from above
    }
    for (const glm::ivec3& dir : kHoriz) {
        const glm::ivec3 n = pos + dir;
        if (!isLiquid(readBlock(world, n))) {
            continue;
        }
        const std::uint8_t nl = readFluid(world, n);
        if (nl < 2) {
            continue; // level 1 is already too weak to feed onward
        }
        // A neighbour only spreads sideways if it is a source or grounded;
        // mid-air falling water flows straight down, not outward.
        const bool grounded =
            nl == Source || !fluidReplaceable(readBlock(world, n + glm::ivec3{0, -1, 0}));
        if (grounded) {
            best = std::max(best, static_cast<std::uint8_t>(nl - 1));
        }
    }
    return best;
}

bool FluidSim::process(World& world, const glm::ivec3& pos) {
    const BlockType block = readBlock(world, pos);
    const std::uint8_t cur = readFluid(world, pos);
    if (cur == Source) {
        return false; // sources are fixed; only edits/buckets change them
    }
    Chunk* chunk = cachedChunk(world, pos);
    if (chunk == nullptr) {
        return false;
    }
    const int lx = pos.x & 15, lz = pos.z & 15;

    if (!fluidReplaceable(block)) {
        if (cur != 0) {
            chunk->setFluid(lx, pos.y, lz, 0); // solid cell can't hold liquid
        }
        return false;
    }

    const std::uint8_t target = computeLevel(world, pos);
    if (target == cur) {
        return false;
    }
    chunk->setFluid(lx, pos.y, lz, target);
    if (target > 0 && block != BlockType::Water) {
        chunk->set(lx, pos.y, lz, BlockType::Water); // fill air / wash away a plant
    } else if (target == 0 && block == BlockType::Water) {
        chunk->set(lx, pos.y, lz, BlockType::Air); // drained dry
    }
    markDirtyAround(world, pos);
    enqueueNeighbours(pos);
    return true;
}

void FluidSim::enqueueNeighbours(const glm::ivec3& pos) {
    for (const glm::ivec3& dir : kAll) {
        m_next.push_back(pos + dir);
    }
}

// A liquid change at a chunk border alters the seam faces of the neighbour
// chunk too, so dirty both. Cheap and only fires on the 1-cell border.
void FluidSim::markDirtyAround(World& world, const glm::ivec3& pos) noexcept {
    const ChunkCoord c = World::chunkCoordOf(pos.x, pos.z);
    world.markMeshDirty(c);
    const int lx = pos.x & 15, lz = pos.z & 15;
    if (lx == 0) {
        world.markMeshDirty({c.x - 1, c.z});
    } else if (lx == 15) {
        world.markMeshDirty({c.x + 1, c.z});
    }
    if (lz == 0) {
        world.markMeshDirty({c.x, c.z - 1});
    } else if (lz == 15) {
        world.markMeshDirty({c.x, c.z + 1});
    }
}

void FluidSim::onBlockChanged(const glm::ivec3& pos) {
    m_next.push_back(pos);
    enqueueNeighbours(pos);
}

void FluidSim::onChunkLoaded(World& world, ChunkCoord coord) {
    m_cache = ChunkRef{};
    Chunk* chunk = world.chunkAt(coord);
    if (chunk == nullptr) {
        return;
    }
    const glm::ivec3 base{coord.x * Chunk::SizeX, 0, coord.z * Chunk::SizeZ};
    for (int x = 0; x < Chunk::SizeX; ++x) {
        for (int z = 0; z < Chunk::SizeZ; ++z) {
            for (int y = 0; y < Chunk::SizeY; ++y) {
                if (chunk->fluidAt(x, y, z) != Source) {
                    continue;
                }
                // Sources are fixed, so queue the open cells they can flow into
                // (down and sideways) rather than the source itself.
                const glm::ivec3 pos = base + glm::ivec3{x, y, z};
                for (const glm::ivec3& dir : kAll) {
                    if (dir.y > 0) {
                        continue; // liquid never flows up
                    }
                    const glm::ivec3 n = pos + dir;
                    if (fluidReplaceable(readBlock(world, n)) && readFluid(world, n) < MaxFlow) {
                        m_next.push_back(n);
                    }
                }
            }
        }
    }
}

bool FluidSim::tick(World& world) {
    if (m_current.empty()) {
        if (m_next.empty()) {
            return false;
        }
        std::swap(m_current, m_next);
    }
    m_cache = ChunkRef{};
    while (!m_current.empty()) {
        const glm::ivec3 pos = m_current.front();
        m_current.pop_front();
        process(world, pos);
    }
    return !m_next.empty();
}

} // namespace cc
