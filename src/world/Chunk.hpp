#pragma once

#include "world/Block.hpp"
#include "world/ChunkCoord.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace cc {

class Chunk {
public:
    static constexpr int SizeX = 16;
    static constexpr int SizeY = 256;
    static constexpr int SizeZ = 16;
    static constexpr int BlockCount = SizeX * SizeY * SizeZ;

    explicit Chunk(ChunkCoord coord) noexcept : m_coord{coord} {}

    // y varies fastest so a vertical column is contiguous — terrain generation
    // and mesh-input snapshots copy whole columns at once.
    [[nodiscard]] static constexpr int index(int x, int y, int z) noexcept {
        return (x * SizeZ + z) * SizeY + y;
    }

    [[nodiscard]] BlockType at(int x, int y, int z) const noexcept {
        assert(x >= 0 && x < SizeX && y >= 0 && y < SizeY && z >= 0 && z < SizeZ);
        return m_blocks[static_cast<std::size_t>(index(x, y, z))];
    }

    void set(int x, int y, int z, BlockType type) noexcept {
        assert(x >= 0 && x < SizeX && y >= 0 && y < SizeY && z >= 0 && z < SizeZ);
        m_blocks[static_cast<std::size_t>(index(x, y, z))] = type;
    }

    [[nodiscard]] const BlockType* column(int x, int z) const noexcept {
        return m_blocks.data() + index(x, 0, z);
    }

    [[nodiscard]] BlockType* columnMutable(int x, int z) noexcept {
        return m_blocks.data() + index(x, 0, z);
    }

    [[nodiscard]] const std::array<BlockType, BlockCount>& blocks() const noexcept {
        return m_blocks;
    }
    [[nodiscard]] std::array<BlockType, BlockCount>& blocksMutable() noexcept { return m_blocks; }

    [[nodiscard]] ChunkCoord coord() const noexcept { return m_coord; }

    [[nodiscard]] bool isModified() const noexcept { return m_modified; }
    void markModified() noexcept { m_modified = true; }
    void clearModified() noexcept { m_modified = false; }

    // Counts mesh-relevant changes (including neighbour edits at our border).
    // A mesh job snapshots the revision it was built from; stale results are
    // dropped when the revisions no longer match.
    [[nodiscard]] std::uint32_t meshRevision() const noexcept { return m_meshRevision; }
    void bumpMeshRevision() noexcept { ++m_meshRevision; }

    [[nodiscard]] std::uint32_t scheduledRevision() const noexcept { return m_scheduledRevision; }
    void setScheduledRevision(std::uint32_t rev) noexcept { m_scheduledRevision = rev; }

private:
    std::array<BlockType, BlockCount> m_blocks{};
    ChunkCoord m_coord;
    bool m_modified = false;
    std::uint32_t m_meshRevision = 1;
    std::uint32_t m_scheduledRevision = 0;
};

} // namespace cc
