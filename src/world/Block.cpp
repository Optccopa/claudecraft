#include "world/Block.hpp"

#include <array>

namespace cc {
namespace {

// Atlas tiles (4x4 grid, row 0 at the bottom of the texture):
// 0 stone, 1 dirt, 2 grass top, 3 grass side, 4 sand, 5 water,
// 6 bark, 7 wood rings, 8 leaves, 9 plank, 10 snow, 11 bedrock,
// 12 coal ore, 13 iron ore, 14 gold ore, 15 diamond ore
constexpr std::array<BlockInfo, static_cast<std::size_t>(BlockType::Count)> kBlocks{{
    {0, 0, 0, false, false, false},   // Air
    {0, 0, 0, true, true, true},      // Stone
    {1, 1, 1, true, true, true},      // Dirt
    {2, 3, 1, true, true, true},      // Grass
    {4, 4, 4, true, true, true},      // Sand
    {5, 5, 5, false, false, false},   // Water
    {7, 6, 7, true, true, true},      // Wood
    {8, 8, 8, true, true, true},      // Leaves
    {9, 9, 9, true, true, true},      // Plank
    {10, 10, 10, true, true, true},   // Snow
    {11, 11, 11, true, true, false},  // Bedrock
    {12, 12, 12, true, true, true},   // CoalOre
    {13, 13, 13, true, true, true},   // IronOre
    {14, 14, 14, true, true, true},   // GoldOre
    {15, 15, 15, true, true, true},   // DiamondOre
}};

constexpr std::array<std::string_view, static_cast<std::size_t>(BlockType::Count)> kNames{
    "air",      "stone",    "dirt",  "grass",    "sand",     "water",
    "wood",     "leaves",   "plank", "snow",     "bedrock",  "coal ore",
    "iron ore", "gold ore", "diamond ore",
};

} // namespace

const BlockInfo& blockInfo(BlockType type) noexcept {
    return kBlocks[static_cast<std::size_t>(type)];
}

std::string_view blockName(BlockType type) noexcept {
    return kNames[static_cast<std::size_t>(type)];
}

} // namespace cc
