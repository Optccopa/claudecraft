#include "world/WorldSave.hpp"

#include "core/Log.hpp"
#include "world/Chunk.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <vector>

namespace cc {
namespace {

constexpr std::uint32_t kMagic = 0x4B484343u; // "CCHK"
// v2 appends a fluid-level RLE after the block RLE so flowing water (level 1-7)
// survives a reload instead of being re-derived from sources. v1 files (blocks
// only) still load — their fluid is primed from the water sources.
constexpr std::uint16_t kVersion = 2;

struct Header {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t reserved;
};
static_assert(sizeof(Header) == 8);

struct Run {
    std::uint16_t length;
    std::uint8_t block;
};

} // namespace

// The directory is created lazily on first save so unmodified worlds (e.g.
// the menu backdrop, which uses a fresh random seed) leave nothing on disk.
WorldSave::WorldSave(std::filesystem::path directory) : m_directory{std::move(directory)} {}

std::filesystem::path WorldSave::pathFor(ChunkCoord coord) const {
    return m_directory / std::format("c_{}_{}.bin", coord.x, coord.z);
}

std::unique_ptr<Chunk> WorldSave::tryLoad(ChunkCoord coord) const {
    std::ifstream file(pathFor(coord), std::ios::binary);
    if (!file) {
        return nullptr;
    }

    Header header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != kMagic || header.version < 1 || header.version > kVersion) {
        logError(std::format("invalid chunk file for ({}, {})", coord.x, coord.z));
        return nullptr;
    }

    auto chunk = std::make_unique<Chunk>(coord);
    auto& blocks = chunk->blocksMutable();
    std::size_t offset = 0;
    while (offset < blocks.size()) {
        Run run{};
        file.read(reinterpret_cast<char*>(&run.length), sizeof(run.length));
        file.read(reinterpret_cast<char*>(&run.block), sizeof(run.block));
        if (!file || run.length == 0 || offset + run.length > blocks.size() ||
            run.block >= static_cast<std::uint8_t>(BlockType::Count)) {
            logError(std::format("corrupt chunk file for ({}, {})", coord.x, coord.z));
            return nullptr;
        }
        for (std::uint16_t i = 0; i < run.length; ++i) {
            blocks[offset++] = static_cast<BlockType>(run.block);
        }
    }

    if (header.version < 2) {
        // Legacy: no fluid section — derive sources from the water blocks.
        chunk->primeFluidSources();
        return chunk;
    }

    auto& fluid = chunk->fluidMutable();
    std::size_t fo = 0;
    while (fo < fluid.size()) {
        Run run{};
        file.read(reinterpret_cast<char*>(&run.length), sizeof(run.length));
        file.read(reinterpret_cast<char*>(&run.block), sizeof(run.block));
        if (!file || run.length == 0 || fo + run.length > fluid.size() || run.block > 8) {
            logError(std::format("corrupt fluid data for ({}, {})", coord.x, coord.z));
            return nullptr;
        }
        for (std::uint16_t i = 0; i < run.length; ++i) {
            fluid[fo++] = run.block;
        }
    }
    return chunk;
}

void WorldSave::save(const Chunk& chunk) const {
    const auto& blocks = chunk.blocks();
    const auto& fluid = chunk.fluidLevels();

    // RLE both streams: voxel columns are long runs of one block, and the fluid
    // array is overwhelmingly 0 (only water columns carry levels). The block
    // stream keeps flowing water as Water; the fluid stream carries its level.
    const auto runLengthEncode = [](auto valueAt, std::size_t total) {
        std::vector<Run> runs;
        runs.reserve(1024);
        std::size_t i = 0;
        while (i < total) {
            const std::uint8_t value = valueAt(i);
            std::size_t length = 1;
            while (i + length < total && valueAt(i + length) == value && length < 0xFFFF) {
                ++length;
            }
            runs.push_back(Run{static_cast<std::uint16_t>(length), value});
            i += length;
        }
        return runs;
    };

    const std::vector<Run> blockRuns = runLengthEncode(
        [&](std::size_t i) { return static_cast<std::uint8_t>(blocks[i]); }, blocks.size());
    const std::vector<Run> fluidRuns =
        runLengthEncode([&](std::size_t i) { return fluid[i]; }, fluid.size());

    std::filesystem::create_directories(m_directory);
    std::ofstream file(pathFor(chunk.coord()), std::ios::binary | std::ios::trunc);
    if (!file) {
        logError(std::format("cannot write chunk file for ({}, {})", chunk.coord().x,
                             chunk.coord().z));
        return;
    }
    const Header header{kMagic, kVersion, 0};
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    const auto writeRuns = [&](const std::vector<Run>& runs) {
        for (const Run& run : runs) {
            file.write(reinterpret_cast<const char*>(&run.length), sizeof(run.length));
            file.write(reinterpret_cast<const char*>(&run.block), sizeof(run.block));
        }
    };
    writeRuns(blockRuns);
    writeRuns(fluidRuns);
}

} // namespace cc
