#pragma once

#include <cstdint>
#include <string_view>

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
    // Serialized as raw u8 in chunk saves — append only, never reorder.
    CoalOre,
    IronOre,
    GoldOre,
    DiamondOre,
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
[[nodiscard]] std::string_view blockName(BlockType type) noexcept;

[[nodiscard]] constexpr bool isAir(BlockType type) noexcept {
    return type == BlockType::Air;
}

} // namespace cc
