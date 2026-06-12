#include "world/Block.hpp"

#include <array>

namespace cc {
namespace {

// Atlas tiles (4x4 grid, row 0 at the bottom of the texture):
// Atlas tiles (8x8 grid): 0 stone, 1 dirt, 2 grass top, 3 grass side, 4 sand,
// 5 water, 6 bark, 7 wood rings, 8 leaves, 9 plank, 10 snow, 11 bedrock,
// 12 coal ore, 13 iron ore, 14 gold ore, 15 diamond ore, 16 glowstone
constexpr std::array<BlockInfo, static_cast<std::size_t>(BlockType::Count)> kBlocks{{
    {0, 0, 0, 0, false, false, false, 0.0f},   // Air
    {0, 0, 0, 0, true, true, true, 1.5f},      // Stone
    {1, 1, 1, 0, true, true, true, 0.5f},      // Dirt
    {2, 3, 1, 0, true, true, true, 0.6f},      // Grass
    {4, 4, 4, 0, true, true, true, 0.5f},      // Sand
    {5, 5, 5, 0, false, false, false, 0.0f},   // Water
    {7, 6, 7, 0, true, true, true, 1.2f},      // Wood
    {8, 8, 8, 0, true, true, true, 0.2f},      // Leaves
    {9, 9, 9, 0, true, true, true, 1.2f},      // Plank
    {10, 10, 10, 0, true, true, true, 0.3f},   // Snow
    {11, 11, 11, 0, true, true, false, 0.0f},  // Bedrock
    {12, 12, 12, 0, true, true, true, 2.5f},   // CoalOre
    {13, 13, 13, 0, true, true, true, 3.0f},   // IronOre
    {14, 14, 14, 0, true, true, true, 3.0f},   // GoldOre
    {15, 15, 15, 0, true, true, true, 3.5f},   // DiamondOre
    {16, 16, 16, 15, true, true, true, 0.6f},  // Glowstone
}};

constexpr std::array<std::string_view, static_cast<std::size_t>(BlockType::Count)> kNames{
    "air",      "stone",    "dirt",  "grass",    "sand",        "water",
    "wood",     "leaves",   "plank", "snow",     "bedrock",     "coal ore",
    "iron ore", "gold ore", "diamond ore", "glowstone",
};

} // namespace

const BlockInfo& blockInfo(BlockType type) noexcept {
    return kBlocks[static_cast<std::size_t>(type)];
}

std::string_view blockName(BlockType type) noexcept {
    return kNames[static_cast<std::size_t>(type)];
}

} // namespace cc
