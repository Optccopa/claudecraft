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

void LightEngine::propagate(World& world, Channel channel) {
    const bool sky = channel == Channel::Sky;
    while (!m_queue.empty()) {
        const Node node = m_queue.front();
        m_queue.pop_front();
        // Re-read: removal may have darkened this cell after it was queued.
        const std::uint8_t level = channelLevel(world.lightPackedAt(node.pos), sky);
        if (level <= 1) {
            continue;
        }
        for (const glm::ivec3& dir : kDirections) {
            const glm::ivec3 t = node.pos + dir;
            const BlockType target = world.blockAt(t);
            if (stopsLight(target)) {
                continue;
            }
            const std::uint8_t cost = stepCost(sky, dir, level, target);
            if (level <= cost) {
                continue;
            }
            const auto candidate = static_cast<std::uint8_t>(level - cost);
            const std::uint8_t packed = world.lightPackedAt(t);
            if (candidate <= channelLevel(packed, sky)) {
                continue;
            }
            if (!world.setLightPacked(t, withChannel(packed, sky, candidate))) {
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
            const std::uint8_t packed = world.lightPackedAt(t);
            const std::uint8_t neighbour = channelLevel(packed, sky);
            if (neighbour == 0) {
                continue;
            }
            const BlockType target = world.blockAt(t);
            const std::uint8_t cost = stepCost(sky, dir, node.level, target);
            // Anything at or below what we could have fed it was (possibly)
            // ours: darken and recurse. Anything brighter survives and
            // becomes a re-flood source.
            if (cost < node.level && neighbour <= node.level - cost) {
                if (world.setLightPacked(t, withChannel(packed, sky, 0))) {
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
        const std::uint8_t level = channelLevel(world.lightPackedAt(t), sky);
        if (level > 1) {
            m_queue.push_back(Node{t, level});
        }
    }
}

void LightEngine::onChunkLoaded(World& world, ChunkCoord coord) {
    const glm::ivec3 base{coord.x * Chunk::SizeX, 0, coord.z * Chunk::SizeZ};
    constexpr std::array<glm::ivec3, 4> kLateral{
        glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, 0, 1}, glm::ivec3{0, 0, -1}};

    for (const bool sky : {true, false}) {
        const Channel channel = sky ? Channel::Sky : Channel::Block;
        for (const glm::ivec3& dir : kLateral) {
            // Cells just inside this chunk's face and just outside it.
            for (int i = 0; i < Chunk::SizeX; ++i) {
                for (int y = 0; y < Chunk::SizeY; ++y) {
                    glm::ivec3 inner = base + glm::ivec3{0, y, 0};
                    if (dir.x != 0) {
                        inner.x += dir.x > 0 ? Chunk::SizeX - 1 : 0;
                        inner.z += i;
                    } else {
                        inner.z += dir.z > 0 ? Chunk::SizeZ - 1 : 0;
                        inner.x += i;
                    }
                    const glm::ivec3 outer = inner + dir;
                    const std::uint8_t a = channelLevel(world.lightPackedAt(inner), sky);
                    const std::uint8_t b = channelLevel(world.lightPackedAt(outer), sky);
                    // Only a >=2 difference can flow anywhere (min cost 1).
                    if (a >= b + 2) {
                        m_queue.push_back(Node{inner, a});
                    } else if (b >= a + 2) {
                        m_queue.push_back(Node{outer, b});
                    }
                }
            }
        }
        propagate(world, channel);
    }
}

void LightEngine::onBlockChanged(World& world, const glm::ivec3& pos, BlockType oldType) {
    const BlockType newType = world.blockAt(pos);
    if (newType == oldType) {
        return;
    }
    const std::uint8_t packed = world.lightPackedAt(pos);

    {
        const std::uint8_t oldSky = Chunk::skyLevel(packed);
        if (oldSky > 0) {
            (void)world.setLightPacked(pos, withChannel(world.lightPackedAt(pos), true, 0));
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
            (void)world.setLightPacked(pos, withChannel(world.lightPackedAt(pos), false, 0));
            removeLight(world, Channel::Block, pos, oldBlock);
        }
        const std::uint8_t emission = blockInfo(newType).emission;
        if (emission > 0) {
            (void)world.setLightPacked(pos,
                                       withChannel(world.lightPackedAt(pos), false, emission));
            m_queue.push_back(Node{pos, emission});
        }
        if (!stopsLight(newType)) {
            seedNeighbours(world, Channel::Block, pos);
        }
        propagate(world, Channel::Block);
    }
}

} // namespace cc
