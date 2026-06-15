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
    [[nodiscard]] const std::array<std::uint8_t, BlockCount>& fluidLevels() const noexcept {
        return m_fluid;
    }
    [[nodiscard]] const std::uint8_t* fluidColumn(int x, int z) const noexcept {
        return m_fluid.data() + index(x, 0, z);
    }
    [[nodiscard]] std::array<BlockType, BlockCount>& blocksMutable() noexcept { return m_blocks; }
    [[nodiscard]] std::array<std::uint8_t, BlockCount>& fluidMutable() noexcept { return m_fluid; }

    // Light is derived data (recomputed on load), packed sky | block << 4.
    // Same indexing as blocks, so light columns memcpy alongside block columns.
    [[nodiscard]] std::uint8_t lightAt(int x, int y, int z) const noexcept {
        return m_light[static_cast<std::size_t>(index(x, y, z))];
    }
    void setLight(int x, int y, int z, std::uint8_t packed) noexcept {
        m_light[static_cast<std::size_t>(index(x, y, z))] = packed;
    }
    [[nodiscard]] const std::uint8_t* lightColumn(int x, int z) const noexcept {
        return m_light.data() + index(x, 0, z);
    }
    [[nodiscard]] std::array<std::uint8_t, BlockCount>& lightMutable() noexcept { return m_light; }

    [[nodiscard]] static constexpr std::uint8_t skyLevel(std::uint8_t packed) noexcept {
        return packed & 0x0Fu;
    }
    [[nodiscard]] static constexpr std::uint8_t blockLevel(std::uint8_t packed) noexcept {
        return packed >> 4;
    }
    [[nodiscard]] static constexpr std::uint8_t packLight(std::uint8_t sky,
                                                          std::uint8_t block) noexcept {
        return static_cast<std::uint8_t>(sky | (block << 4));
    }

    // Fluid level per cell: 0 = none, 1..7 = flowing strength (7 strongest),
    // 8 = source. Transient — only sources persist (as the liquid block); the
    // sim re-derives flowing levels on load. See [[fluid]] doc.
    [[nodiscard]] std::uint8_t fluidAt(int x, int y, int z) const noexcept {
        return m_fluid[static_cast<std::size_t>(index(x, y, z))];
    }
    void setFluid(int x, int y, int z, std::uint8_t level) noexcept {
        m_fluid[static_cast<std::size_t>(index(x, y, z))] = level;
    }

    // Every persisted liquid block is a source (flowing levels aren't saved);
    // seed the transient level array from the blocks after generation/load.
    void primeFluidSources() noexcept {
        for (std::size_t i = 0; i < m_blocks.size(); ++i) {
            m_fluid[i] = isLiquid(m_blocks[i]) ? std::uint8_t{8} : std::uint8_t{0};
        }
    }

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
    std::array<std::uint8_t, BlockCount> m_light{};
    std::array<std::uint8_t, BlockCount> m_fluid{};
    ChunkCoord m_coord;
    bool m_modified = false;
    std::uint32_t m_meshRevision = 1;
    std::uint32_t m_scheduledRevision = 0;
};

} // namespace cc
