#pragma once

#include "world/Block.hpp"
#include "world/Chunk.hpp"
#include "world/ChunkCoord.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace cc {

struct ChunkVertex {
    // chunk-local position, x | z << 5 | y << 10 (x,z in [0,16], y in [0,256])
    std::uint32_t pos;
    // block-space texcoords u | v << 16; fract() in the shader tiles the atlas
    std::uint32_t uv;
    // tile | aoCorner << 8 | normalIndex << 10 | skyLight << 13 | blockLight << 17
    std::uint32_t data;
};
static_assert(sizeof(ChunkVertex) == 12);

// Coordinates are exact small integers (greedy quads land on block edges), so
// the float positions/UVs pack losslessly into two uint32s. Shared by the
// mesher and the dropped-item cubes that reuse the chunk vertex format.
[[nodiscard]] constexpr ChunkVertex packChunkVertex(int x, int y, int z, int u, int v,
                                                    std::uint32_t data) noexcept {
    return ChunkVertex{
        static_cast<std::uint32_t>(x) | (static_cast<std::uint32_t>(z) << 5) |
            (static_cast<std::uint32_t>(y) << 10),
        static_cast<std::uint32_t>(u) | (static_cast<std::uint32_t>(v) << 16),
        data,
    };
}

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
    static constexpr std::size_t Cells =
        static_cast<std::size_t>(PaddedX) * PaddedZ * Chunk::SizeY;

    ChunkCoord coord;
    std::uint32_t revision = 0;
    bool smoothLighting = true;             // off: flat per-face light, no AO
    std::array<BlockType, Cells> blocks{};  // PaddedX * PaddedZ * SizeY
    std::array<std::uint8_t, Cells> light{}; // same layout, packed sky | block << 4

    // x and z accept [-1, Size]; any y outside [0, SizeY) reads as Air.
    [[nodiscard]] BlockType at(int x, int y, int z) const noexcept {
        if (y < 0 || y >= Chunk::SizeY) {
            return BlockType::Air;
        }
        const int idx = ((x + 1) * PaddedZ + (z + 1)) * Chunk::SizeY + y;
        return blocks[static_cast<std::size_t>(idx)];
    }

    // Above the world is full sky; below is darkness.
    [[nodiscard]] std::uint8_t lightAt(int x, int y, int z) const noexcept {
        if (y >= Chunk::SizeY) {
            return Chunk::packLight(15, 0);
        }
        if (y < 0) {
            return 0;
        }
        const int idx = ((x + 1) * PaddedZ + (z + 1)) * Chunk::SizeY + y;
        return light[static_cast<std::size_t>(idx)];
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
