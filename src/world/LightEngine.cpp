#include "world/LightEngine.hpp"

#include "world/Chunk.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace cc {
namespace {

constexpr std::array<glm::ivec3, 6> kDirections{
    glm::ivec3{1, 0, 0},  glm::ivec3{-1, 0, 0}, glm::ivec3{0, 1, 0},
    glm::ivec3{0, -1, 0}, glm::ivec3{0, 0, 1},  glm::ivec3{0, 0, -1},
};

[[nodiscard]] bool stopsLight(BlockType type) noexcept {
    return blockInfo(type).opaque;
}

[[nodiscard]] std::uint8_t costInto(BlockType type) noexcept {
    return type == BlockType::Water ? 3 : 1;
}

// Full-strength sky light falls straight down through air for free; every
// other move (lateral, attenuated, or through water) pays the entry cost.
[[nodiscard]] std::uint8_t stepCost(bool sky, const glm::ivec3& dir, std::uint8_t level,
                                    BlockType target) noexcept {
    if (sky && dir.y == -1 && level == LightEngine::MaxLevel && target != BlockType::Water) {
        return 0;
    }
    return costInto(target);
}

[[nodiscard]] std::uint8_t channelLevel(std::uint8_t packed, bool sky) noexcept {
    return sky ? Chunk::skyLevel(packed) : Chunk::blockLevel(packed);
}

[[nodiscard]] std::uint8_t withChannel(std::uint8_t packed, bool sky,
                                       std::uint8_t level) noexcept {
    return sky ? Chunk::packLight(level, Chunk::blockLevel(packed))
               : Chunk::packLight(Chunk::skyLevel(packed), level);
}

struct LocalNode {
    glm::ivec3 pos; // chunk-local
    std::uint8_t level;
};

// In-chunk BFS used by worker-side initialization; out-of-chunk neighbours
// are simply skipped (the main-thread seam merge fixes borders later).
void localPropagate(Chunk& chunk, std::deque<LocalNode>& queue, bool sky) {
    while (!queue.empty()) {
        const LocalNode node = queue.front();
        queue.pop_front();
        for (const glm::ivec3& dir : kDirections) {
            const glm::ivec3 t = node.pos + dir;
            if (t.x < 0 || t.x >= Chunk::SizeX || t.z < 0 || t.z >= Chunk::SizeZ || t.y < 0 ||
                t.y >= Chunk::SizeY) {
                continue;
            }
            const BlockType target = chunk.at(t.x, t.y, t.z);
            if (stopsLight(target)) {
                continue;
            }
            const std::uint8_t cost = stepCost(sky, dir, node.level, target);
            if (node.level <= cost) {
                continue;
            }
            const auto candidate = static_cast<std::uint8_t>(node.level - cost);
            const std::uint8_t packed = chunk.lightAt(t.x, t.y, t.z);
            if (candidate <= channelLevel(packed, sky)) {
                continue;
            }
            chunk.setLight(t.x, t.y, t.z, withChannel(packed, sky, candidate));
            queue.push_back(LocalNode{t, candidate});
        }
    }
}

} // namespace

void LightEngine::initializeChunkLight(Chunk& chunk) noexcept {
    auto& light = chunk.lightMutable();
    light.fill(0);

    std::deque<LocalNode> skyQueue;
    std::deque<LocalNode> blockQueue;

    for (int x = 0; x < Chunk::SizeX; ++x) {
        for (int z = 0; z < Chunk::SizeZ; ++z) {
            std::uint8_t level = MaxLevel;
            for (int y = Chunk::SizeY - 1; y >= 0 && level > 0; --y) {
                const BlockType block = chunk.at(x, y, z);
                if (stopsLight(block)) {
                    break;
                }
                const std::uint8_t cost =
                    stepCost(true, glm::ivec3{0, -1, 0}, level, block);
                level = static_cast<std::uint8_t>(level > cost ? level - cost : 0);
                chunk.setLight(x, y, z, Chunk::packLight(level, 0));
            }
        }
    }

    // Frontier scan: only cells that can actually spread into darker space
    // seed the BFS, which keeps the queue tiny on open terrain where columns
    // are already uniformly lit.
    for (int x = 0; x < Chunk::SizeX; ++x) {
        for (int z = 0; z < Chunk::SizeZ; ++z) {
            for (int y = 0; y < Chunk::SizeY; ++y) {
                const BlockType block = chunk.at(x, y, z);
                const std::uint8_t emission = blockInfo(block).emission;
                if (emission > 0) {
                    const std::uint8_t packed = chunk.lightAt(x, y, z);
                    chunk.setLight(x, y, z, withChannel(packed, false, emission));
                    blockQueue.push_back(LocalNode{{x, y, z}, emission});
                }
                const std::uint8_t sky = Chunk::skyLevel(chunk.lightAt(x, y, z));
                if (sky <= 1) {
                    continue;
                }
                for (const glm::ivec3& dir : kDirections) {
                    const int nx = x + dir.x;
                    const int ny = y + dir.y;
                    const int nz = z + dir.z;
                    if (nx < 0 || nx >= Chunk::SizeX || nz < 0 || nz >= Chunk::SizeZ || ny < 0 ||
                        ny >= Chunk::SizeY) {
                        continue;
                    }
                    if (!stopsLight(chunk.at(nx, ny, nz)) &&
                        Chunk::skyLevel(chunk.lightAt(nx, ny, nz)) + 1 < sky) {
                        skyQueue.push_back(LocalNode{{x, y, z}, sky});
                        break;
                    }
                }
            }
        }
    }

    localPropagate(chunk, skyQueue, true);
    localPropagate(chunk, blockQueue, false);
}

Chunk* LightEngine::cachedChunk(World& world, const glm::ivec3& pos) noexcept {
    const ChunkCoord coord = World::chunkCoordOf(pos.x, pos.z);
    if (!(coord == m_cache.coord)) {
        m_cache.coord = coord;
        m_cache.chunk = world.chunkAt(coord);
    }
    return m_cache.chunk;
}

std::uint8_t LightEngine::readLight(World& world, const glm::ivec3& pos) noexcept {
    if (pos.y >= Chunk::SizeY) {
        return Chunk::packLight(MaxLevel, 0);
    }
    if (pos.y < 0) {
        return 0;
    }
    const Chunk* chunk = cachedChunk(world, pos);
    return chunk != nullptr ? chunk->lightAt(pos.x & 15, pos.y, pos.z & 15) : 0;
}

BlockType LightEngine::readBlock(World& world, const glm::ivec3& pos) noexcept {
    if (pos.y < 0 || pos.y >= Chunk::SizeY) {
        return BlockType::Air;
    }
    const Chunk* chunk = cachedChunk(world, pos);
    return chunk != nullptr ? chunk->at(pos.x & 15, pos.y, pos.z & 15) : BlockType::Air;
}

bool LightEngine::writeLight(World& world, const glm::ivec3& pos, std::uint8_t packed) noexcept {
    if (pos.y < 0 || pos.y >= Chunk::SizeY) {
        return false;
    }
    Chunk* chunk = cachedChunk(world, pos);
    if (chunk == nullptr) {
        return false;
    }
    chunk->setLight(pos.x & 15, pos.y, pos.z & 15, packed);
    m_touched.insert(m_cache.coord);
    return true;
}

// Conservatively remesh every touched chunk and its neighbours once per
// relight pass; the revision protocol dedupes repeated bumps for free.
void LightEngine::flushTouched(World& world) {
    for (const ChunkCoord coord : m_touched) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (Chunk* chunk = world.chunkAt({coord.x + dx, coord.z + dz})) {
                    chunk->bumpMeshRevision();
                }
            }
        }
    }
    m_touched.clear();
}

void LightEngine::propagate(World& world, Channel channel) {
    const bool sky = channel == Channel::Sky;
    while (!m_queue.empty()) {
        const Node node = m_queue.front();
        m_queue.pop_front();
        // Re-read: removal may have darkened this cell after it was queued.
        const std::uint8_t level = channelLevel(readLight(world, node.pos), sky);
        if (level <= 1) {
            continue;
        }
        for (const glm::ivec3& dir : kDirections) {
            const glm::ivec3 t = node.pos + dir;
            const BlockType target = readBlock(world, t);
            if (stopsLight(target)) {
                continue;
            }
            const std::uint8_t cost = stepCost(sky, dir, level, target);
            if (level <= cost) {
                continue;
            }
            const auto candidate = static_cast<std::uint8_t>(level - cost);
            const std::uint8_t packed = readLight(world, t);
            if (candidate <= channelLevel(packed, sky)) {
                continue;
            }
            if (!writeLight(world, t, withChannel(packed, sky, candidate))) {
                continue; // unloaded chunk; seam merge relights it on load
            }
            m_queue.push_back(Node{t, candidate});
        }
    }
}

void LightEngine::removeLight(World& world, Channel channel, const glm::ivec3& pos,
                              std::uint8_t oldLevel) {
    const bool sky = channel == Channel::Sky;
    m_removalQueue.push_back(Node{pos, oldLevel});
    while (!m_removalQueue.empty()) {
        const Node node = m_removalQueue.front();
        m_removalQueue.pop_front();
        for (const glm::ivec3& dir : kDirections) {
            const glm::ivec3 t = node.pos + dir;
            const std::uint8_t packed = readLight(world, t);
            const std::uint8_t neighbour = channelLevel(packed, sky);
            if (neighbour == 0) {
                continue;
            }
            const BlockType target = readBlock(world, t);
            const std::uint8_t cost = stepCost(sky, dir, node.level, target);
            // Anything at or below what we could have fed it was (possibly)
            // ours: darken and recurse. Anything brighter survives and
            // becomes a re-flood source.
            if (cost < node.level && neighbour <= node.level - cost) {
                if (writeLight(world, t, withChannel(packed, sky, 0))) {
                    m_removalQueue.push_back(Node{t, neighbour});
                }
            } else {
                m_queue.push_back(Node{t, neighbour});
            }
        }
    }
}

void LightEngine::seedNeighbours(World& world, Channel channel, const glm::ivec3& pos) {
    const bool sky = channel == Channel::Sky;
    for (const glm::ivec3& dir : kDirections) {
        const glm::ivec3 t = pos + dir;
        const std::uint8_t level = channelLevel(readLight(world, t), sky);
        if (level > 1) {
            m_queue.push_back(Node{t, level});
        }
    }
}

void LightEngine::onChunkLoaded(World& world, ChunkCoord coord) {
    m_cache = ChunkRef{}; // chunks may have unloaded since the last pass
    Chunk* center = world.chunkAt(coord);
    if (center == nullptr) {
        return;
    }
    const glm::ivec3 base{coord.x * Chunk::SizeX, 0, coord.z * Chunk::SizeZ};
    constexpr std::array<glm::ivec3, 4> kLateral{
        glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, 0, 1}, glm::ivec3{0, 0, -1}};

    for (const bool sky : {true, false}) {
        const Channel channel = sky ? Channel::Sky : Channel::Block;
        for (const glm::ivec3& dir : kLateral) {
            Chunk* neighbour = world.chunkAt({coord.x + dir.x, coord.z + dir.z});
            if (neighbour == nullptr) {
                continue;
            }
            // Direct array access on the chunk pair: this scan runs 16k cells
            // per face and per-cell map lookups here caused visible freezes.
            const int innerX = dir.x > 0 ? Chunk::SizeX - 1 : 0;
            const int innerZ = dir.z > 0 ? Chunk::SizeZ - 1 : 0;
            for (int i = 0; i < Chunk::SizeX; ++i) {
                const int ix = dir.x != 0 ? innerX : i;
                const int iz = dir.x != 0 ? i : innerZ;
                const int ox = dir.x != 0 ? (Chunk::SizeX - 1 - innerX) : i;
                const int oz = dir.x != 0 ? i : (Chunk::SizeZ - 1 - innerZ);
                for (int y = 0; y < Chunk::SizeY; ++y) {
                    const std::uint8_t a = channelLevel(center->lightAt(ix, y, iz), sky);
                    const std::uint8_t b = channelLevel(neighbour->lightAt(ox, y, oz), sky);
                    // Only a >=2 difference can flow anywhere (min cost 1).
                    if (a >= b + 2) {
                        m_queue.push_back(Node{base + glm::ivec3{ix, y, iz}, a});
                    } else if (b >= a + 2) {
                        m_queue.push_back(Node{base + glm::ivec3{ix + dir.x, y, iz + dir.z}, b});
                    }
                }
            }
        }
        propagate(world, channel);
    }
    flushTouched(world);
}

void LightEngine::onBlockChanged(World& world, const glm::ivec3& pos, BlockType oldType) {
    m_cache = ChunkRef{}; // chunks may have unloaded since the last pass
    const BlockType newType = world.blockAt(pos);
    if (newType == oldType) {
        return;
    }
    const std::uint8_t packed = readLight(world, pos);

    {
        const std::uint8_t oldSky = Chunk::skyLevel(packed);
        if (oldSky > 0) {
            (void)writeLight(world, pos, withChannel(readLight(world, pos), true, 0));
            removeLight(world, Channel::Sky, pos, oldSky);
        }
        if (!stopsLight(newType)) {
            seedNeighbours(world, Channel::Sky, pos);
        }
        propagate(world, Channel::Sky);
    }
    {
        const std::uint8_t oldBlock = Chunk::blockLevel(packed);
        if (oldBlock > 0) {
            (void)writeLight(world, pos, withChannel(readLight(world, pos), false, 0));
            removeLight(world, Channel::Block, pos, oldBlock);
        }
        const std::uint8_t emission = blockInfo(newType).emission;
        if (emission > 0) {
            (void)writeLight(world, pos, withChannel(readLight(world, pos), false, emission));
            m_queue.push_back(Node{pos, emission});
        }
        if (!stopsLight(newType)) {
            seedNeighbours(world, Channel::Block, pos);
        }
        propagate(world, Channel::Block);
    }
    flushTouched(world);
}

} // namespace cc
