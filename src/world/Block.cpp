#include "world/Block.hpp"

#include <array>

namespace cc {
namespace {

// Atlas tiles (4x4 grid, row 0 at the bottom of the texture):
// Atlas tiles (8x8 grid): 0 stone, 1 dirt, 2 grass top, 3 grass side, 4 sand,
// 5 water, 6 bark, 7 wood rings, 8 leaves, 9 plank, 10 snow, 11 bedrock,
// 12 coal ore, 13 iron ore, 14 gold ore, 15 diamond ore, 16 glowstone,
// 17 cherry bark, 18 cherry leaves, 19 spruce bark, 20 spruce leaves,
// 21 cactus side, 22 cactus top, 23 cactus bottom, 24 short grass
using S = BlockShape;
constexpr std::array<BlockInfo, static_cast<std::size_t>(BlockType::Count)> kBlocks{{
    {0, 0, 0, 0, false, false, false, false, S::Cube, 0, 0.0f}, // Air
    {0, 0, 0, 0, true, true, true, true, S::Cube, 0, 1.5f},     // Stone
    {1, 1, 1, 0, true, true, true, true, S::Cube, 0, 0.5f},     // Dirt
    {2, 3, 1, 0, true, true, true, true, S::Cube, 0, 0.6f},     // Grass
    {4, 4, 4, 0, true, true, true, true, S::Cube, 0, 0.5f},     // Sand
    {5, 5, 5, 0, false, false, false, false, S::Cube, 0, 0.0f}, // Water
    {7, 6, 7, 0, true, true, true, true, S::Cube, 0, 1.2f},     // Wood
    {8, 8, 8, 0, true, true, true, true, S::Cube, 0, 0.2f},     // Leaves
    {9, 9, 9, 0, true, true, true, true, S::Cube, 0, 1.2f},     // Plank
    {10, 10, 10, 0, true, true, true, true, S::Cube, 0, 0.3f},  // Snow
    {11, 11, 11, 0, true, true, false, false, S::Cube, 0, 0.0f}, // Bedrock
    {12, 12, 12, 0, true, true, true, true, S::Cube, 0, 2.5f},  // CoalOre
    {13, 13, 13, 0, true, true, true, true, S::Cube, 0, 3.0f},  // IronOre
    {14, 14, 14, 0, true, true, true, true, S::Cube, 0, 3.0f},  // GoldOre
    {15, 15, 15, 0, true, true, true, true, S::Cube, 0, 3.5f},  // DiamondOre
    {16, 16, 16, 15, true, true, true, true, S::Cube, 0, 0.6f}, // Glowstone
    {7, 17, 7, 0, true, true, true, true, S::Cube, 0, 1.2f},    // CherryWood
    {18, 18, 18, 0, true, true, true, true, S::Cube, 0, 0.2f},  // CherryLeaves
    {7, 19, 7, 0, true, true, true, true, S::Cube, 0, 1.2f},    // SpruceWood
    {20, 20, 20, 0, true, true, true, true, S::Cube, 0, 0.2f},  // SpruceLeaves
    {22, 21, 23, 0, false, true, true, true, S::Box, 1, 0.4f},  // Cactus (inset column)
    {24, 24, 24, 0, false, false, true, false, S::Cross, 0, 0.0f}, // TallGrass (no drop)
}};

// Exact Minecraft block ids: also the stem of the texture file each tile
// loads from a resource pack (see TextureAtlas).
constexpr std::array<std::string_view, static_cast<std::size_t>(BlockType::Count)> kNames{
    "air",       "stone",     "dirt",       "grass_block",   "sand",
    "water",     "oak_log",   "oak_leaves", "oak_planks",    "snow_block",
    "bedrock",   "coal_ore",  "iron_ore",   "gold_ore",      "diamond_ore",
    "glowstone", "cherry_log", "cherry_leaves", "spruce_log", "spruce_leaves",
    "cactus",    "short_grass",
};

} // namespace

const BlockInfo& blockInfo(BlockType type) noexcept {
    return kBlocks[static_cast<std::size_t>(type)];
}

std::string_view blockName(BlockType type) noexcept {
    return kNames[static_cast<std::size_t>(type)];
}

} // namespace cc
