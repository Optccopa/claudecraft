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
    Glowstone,
    CherryWood,
    CherryLeaves,
    SpruceWood,
    SpruceLeaves,
    Count
};

struct BlockInfo {
    std::uint8_t topTile;
    std::uint8_t sideTile;
    std::uint8_t bottomTile;
    std::uint8_t emission; // block light emitted, 0..15
    bool opaque;           // hides adjacent faces and stops light
    bool solid;            // collides with the player
    bool breakable;
    float hardness;        // seconds to mine in survival
};

[[nodiscard]] const BlockInfo& blockInfo(BlockType type) noexcept;
[[nodiscard]] std::string_view blockName(BlockType type) noexcept;

[[nodiscard]] constexpr bool isAir(BlockType type) noexcept {
    return type == BlockType::Air;
}

} // namespace cc
