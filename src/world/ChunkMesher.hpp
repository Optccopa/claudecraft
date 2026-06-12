#pragma once

#include "world/Block.hpp"
#include "world/Chunk.hpp"
#include "world/ChunkCoord.hpp"

#include <cstdint>
#include <vector>

namespace cc {

struct ChunkVertex {
    float x, y, z; // chunk-local position
    float u, v;    // block-space texcoords; fract() in the shader tiles the atlas
    std::uint32_t data; // tile | aoCorner << 8 | normalIndex << 10
};
static_assert(sizeof(ChunkVertex) == 24);

struct ChunkMeshData {
    std::vector<ChunkVertex> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool empty() const noexcept { return indices.empty(); }
};

// Immutable snapshot of a chunk plus a one-block lateral border, taken on the
// main thread so mesh workers never touch live world state. The border rows
// come from the eight lateral neighbours (needed for face culling and AO).
struct MeshInput {
    static constexpr int PaddedX = Chunk::SizeX + 2;
    static constexpr int PaddedZ = Chunk::SizeZ + 2;

    ChunkCoord coord;
    std::uint32_t revision = 0;
    std::vector<BlockType> blocks; // PaddedX * PaddedZ * SizeY

    // x and z accept [-1, Size]; any y outside [0, SizeY) reads as Air.
    [[nodiscard]] BlockType at(int x, int y, int z) const noexcept {
        if (y < 0 || y >= Chunk::SizeY) {
            return BlockType::Air;
        }
        const int idx = ((x + 1) * PaddedZ + (z + 1)) * Chunk::SizeY + y;
        return blocks[static_cast<std::size_t>(idx)];
    }
};

class ChunkMesher {
public:
    struct Result {
        ChunkMeshData opaque;
        ChunkMeshData water;
    };

    [[nodiscard]] static Result build(const MeshInput& input);
};

} // namespace cc
