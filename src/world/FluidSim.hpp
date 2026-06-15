#pragma once

#include "world/Block.hpp"
#include "world/ChunkCoord.hpp"

#include <glm/vec3.hpp>

#include <cstdint>
#include <deque>

namespace cc {

class World;
class Chunk;

// Cellular liquid flow shared by every liquid (water now, lava later). Level 8
// is a source — the only state persisted, as the liquid block itself; levels
// 1..7 are flowing strength that drains when unsupported. A cell's level is a
// pure function of its neighbours (max source/feed minus one step), so flow and
// drain are the same relaxation: a changed cell re-evaluates its neighbours.
// One ring is processed per tick (~0.25 s) so the liquid visibly creeps, like
// Minecraft's 1 block / 5 game ticks. See docs/fluid.md.
class FluidSim {
public:
    static constexpr std::uint8_t Source = 8;
    static constexpr std::uint8_t MaxFlow = 7; // strongest flowing level

    // A block was edited at pos (already written to the chunk). Queues the cell
    // and its neighbours so the next tick fills or drains around the change.
    void onBlockChanged(const glm::ivec3& pos);

    // Seed flow from a freshly loaded chunk's sources, since flowing levels are
    // not persisted. Cheap: only sources that border an open cell are queued.
    void onChunkLoaded(World& world, ChunkCoord coord);

    // Advance flow/drain by one ring. Returns true while work remains.
    bool tick(World& world);

private:
    struct ChunkRef {
        ChunkCoord coord{INT32_MIN, INT32_MIN};
        Chunk* chunk = nullptr;
    };

    [[nodiscard]] Chunk* cachedChunk(World& world, const glm::ivec3& pos) noexcept;
    [[nodiscard]] BlockType readBlock(World& world, const glm::ivec3& pos) noexcept;
    [[nodiscard]] std::uint8_t readFluid(World& world, const glm::ivec3& pos) noexcept;
    [[nodiscard]] std::uint8_t computeLevel(World& world, const glm::ivec3& pos) noexcept;
    bool process(World& world, const glm::ivec3& pos);
    void enqueueNeighbours(const glm::ivec3& pos);
    void markDirtyAround(World& world, const glm::ivec3& pos) noexcept;

    ChunkRef m_cache;
    std::deque<glm::ivec3> m_current; // this tick's ring
    std::deque<glm::ivec3> m_next;    // cells to revisit next tick
};

} // namespace cc
