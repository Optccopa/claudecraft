#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace cc {

struct ChunkCoord {
    std::int32_t x = 0;
    std::int32_t z = 0;

    [[nodiscard]] friend bool operator==(const ChunkCoord&, const ChunkCoord&) = default;
};

struct ChunkCoordHash {
    [[nodiscard]] std::size_t operator()(const ChunkCoord& c) const noexcept {
        const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.x));
        const auto uz = static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.z));
        return std::hash<std::uint64_t>{}((ux << 32) | uz);
    }
};

} // namespace cc
