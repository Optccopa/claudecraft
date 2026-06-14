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
    Cactus,
    TallGrass,
    Count
};

// How the mesher turns a block into geometry. Cube is the greedy-meshed full
// cell; Cross is a two-quad billboard (plants); Box is a full-height cell
// inset on its four sides by `BlockInfo::inset` sixteenths (cactus, and the
// foundation for slabs/posts later).
enum class BlockShape : std::uint8_t { Cube, Cross, Box };

struct BlockInfo {
    std::uint8_t topTile;
    std::uint8_t sideTile;
    std::uint8_t bottomTile;
    std::uint8_t emission; // block light emitted, 0..15
    bool opaque;           // hides adjacent faces and stops light
    bool solid;            // collides with the player
    bool breakable;
    bool dropsItem;        // breaking spawns a drop (survival)
    BlockShape shape;
    std::uint8_t inset;    // Box only: sixteenths each side face pulls in
    float hardness;        // seconds to mine in survival
};

[[nodiscard]] const BlockInfo& blockInfo(BlockType type) noexcept;
[[nodiscard]] std::string_view blockName(BlockType type) noexcept;

[[nodiscard]] constexpr bool isAir(BlockType type) noexcept {
    return type == BlockType::Air;
}

// Full-cell blocks go through the greedy cube mesher; everything else
// (cross billboards, inset boxes) is emitted by a dedicated per-block pass.
[[nodiscard]] inline bool isFullCube(BlockType type) noexcept {
    return blockInfo(type).shape == BlockShape::Cube;
}

// Cross-shaped billboard plants: rendered as two diagonal quads, no cull.
[[nodiscard]] inline bool isCrossPlant(BlockType type) noexcept {
    return blockInfo(type).shape == BlockShape::Cross;
}

// What the crosshair raycast can select: solid blocks plus non-solid
// breakables (tall grass and future plants), so they can be mined even though
// the player walks through them. Air and water stay non-targetable.
[[nodiscard]] inline bool isTargetable(BlockType type) noexcept {
    const BlockInfo& info = blockInfo(type);
    return info.solid || info.breakable;
}

} // namespace cc
