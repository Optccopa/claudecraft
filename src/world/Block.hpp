#pragma once

#include <cstdint>

namespace cc {

enum class BlockType : std::uint8_t {
    Air,
    Stone,
    Dirt,
    Grass,
    Sand,
    Water,
    Wood,
    Leaves,
    Plank,
    Snow,
    Bedrock,
    Count
};

struct BlockInfo {
    std::uint8_t topTile;
    std::uint8_t sideTile;
    std::uint8_t bottomTile;
    bool opaque;     // hides adjacent faces
    bool solid;      // collides with the player
    bool breakable;
};

[[nodiscard]] const BlockInfo& blockInfo(BlockType type) noexcept;

[[nodiscard]] constexpr bool isAir(BlockType type) noexcept {
    return type == BlockType::Air;
}

} // namespace cc
