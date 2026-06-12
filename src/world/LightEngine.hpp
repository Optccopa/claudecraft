#pragma once

#include "world/Block.hpp"
#include "world/ChunkCoord.hpp"

#include <glm/vec3.hpp>

#include <cstdint>
#include <deque>

namespace cc {

class Chunk;
class World;

// Voxel lighting: two 0..15 channels per cell (sky and block light), spread
// by BFS flood fill losing 1 per step (water costs 3; opaque blocks stop
// light). Sky light additionally falls straight down at no cost while at
// full strength, which is what makes open terrain bright and caves dark.
//
// Threading split mirrors the chunk pipeline: a chunk's internal light is a
// pure function of its blocks and is computed on the worker that generated
// or loaded it; everything that crosses chunk borders (seam merging when a
// chunk integrates, relighting after edits) runs on the main thread, where
// the chunk map is owned.
class LightEngine {
public:
    static constexpr std::uint8_t MaxLevel = 15;

    // Sky columns + in-chunk BFS, borders treated as lightless. Worker-safe.
    static void initializeChunkLight(Chunk& chunk) noexcept;

    // Pulls light across the seams between the freshly integrated chunk and
    // its loaded lateral neighbours, then floods to exhaustion world-wide.
    void onChunkLoaded(World& world, ChunkCoord coord);

    // Two-phase relight after a block edit: darken everything the old cell
    // fed, then re-flood from the surviving borders and any new emission.
    void onBlockChanged(World& world, const glm::ivec3& pos, BlockType oldType);

private:
    struct Node {
        glm::ivec3 pos;
        std::uint8_t level;
    };

    enum class Channel : std::uint8_t { Sky, Block };

    void propagate(World& world, Channel channel);
    void removeLight(World& world, Channel channel, const glm::ivec3& pos,
                     std::uint8_t oldLevel);
    void seedNeighbours(World& world, Channel channel, const glm::ivec3& pos);

    // Scratch queues reused across calls to avoid per-edit allocation.
    std::deque<Node> m_queue;
    std::deque<Node> m_removalQueue;
};

} // namespace cc
