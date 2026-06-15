#include "world/Mob.hpp"

#include <array>

namespace cc {
namespace {

using B = BlockType;

// Health and hitbox from minecraft.wiki (Cow/Pig/Sheep). Walk speed is an
// approximation of the in-game movement-speed attribute in blocks/second.
constexpr std::array<MobInfo, static_cast<std::size_t>(MobType::Count)> kMobs{{
    // Cow: 10 HP, 0.9 wide x 1.4 tall. Beef 1-3 always, leather 0-2.
    {10.0f, 0.45f, 1.4f, 1.9f, {{{B::RawBeef, 1, 3}, {B::Leather, 0, 2}}}},
    // Pig: 10 HP, 0.9 x 0.9. Porkchop 1-3.
    {10.0f, 0.45f, 0.9f, 2.2f, {{{B::RawPorkchop, 1, 3}, {B::Air, 0, 0}}}},
    // Sheep: 8 HP, 0.9 x 1.3. Mutton 1-2, one wool block.
    {8.0f, 0.45f, 1.3f, 2.0f, {{{B::RawMutton, 1, 2}, {B::Wool, 1, 1}}}},
}};

constexpr std::array<std::string_view, static_cast<std::size_t>(MobType::Count)> kNames{
    "cow", "pig", "sheep"};

// Skin candidates in priority order: current biome-variant name (temperate is
// the overworld default), the plain modern name, then the flat pre-1.13 path.
constexpr std::array<std::string_view, 3> kCowTex{
    "entity/cow/temperate_cow", "entity/cow/cow", "entity/cow"};
constexpr std::array<std::string_view, 3> kPigTex{
    "entity/pig/temperate_pig", "entity/pig/pig", "entity/pig"};
constexpr std::array<std::string_view, 2> kSheepTex{"entity/sheep/sheep", "entity/sheep"};

// Wool overlay (sheep only); sheep_fur is the pre-1.13 name.
constexpr std::array<std::string_view, 2> kSheepWool{
    "entity/sheep/sheep_wool", "entity/sheep/sheep_fur"};

// Box dimensions and texture offsets follow the vanilla quadruped models so the
// pack's entity textures unwrap correctly; positions are tuned to the wiki
// hitbox rather than copying Minecraft's pivot/offset conventions. Bodies lie
// down at pitch 90 (a vertical model box becomes the horizontal torso).
constexpr glm::vec3 kCowBrown{0.42f, 0.30f, 0.19f};
constexpr glm::vec3 kCowDark{0.26f, 0.19f, 0.12f};

constexpr std::array<MobPart, 6> kCow{{
    {{0.0f, 0.80f, 0.0f}, {12, 18, 10}, {18, 4}, 90.0f, 0, kCowBrown},  // body
    {{0.0f, 1.02f, 0.70f}, {8, 8, 6}, {0, 0}, 0.0f, 0, kCowBrown},      // head
    {{-0.20f, 0.375f, -0.40f}, {4, 12, 4}, {0, 16}, 0.0f, 0, kCowDark}, // legs
    {{0.20f, 0.375f, -0.40f}, {4, 12, 4}, {0, 16}, 0.0f, 0, kCowDark},
    {{-0.20f, 0.375f, 0.40f}, {4, 12, 4}, {0, 16}, 0.0f, 0, kCowDark},
    {{0.20f, 0.375f, 0.40f}, {4, 12, 4}, {0, 16}, 0.0f, 0, kCowDark},
}};

constexpr glm::vec3 kPink{0.90f, 0.55f, 0.58f};
constexpr glm::vec3 kPinkDark{0.78f, 0.45f, 0.48f};

constexpr std::array<MobPart, 7> kPig{{
    {{0.0f, 0.60f, 0.0f}, {10, 16, 8}, {28, 8}, 90.0f, 0, kPink},       // body
    {{0.0f, 0.58f, 0.58f}, {8, 8, 8}, {0, 0}, 0.0f, 0, kPink},          // head
    {{0.0f, 0.52f, 0.86f}, {4, 3, 1}, {16, 16}, 0.0f, 0, kPinkDark},    // snout
    {{-0.18f, 0.1875f, -0.32f}, {4, 6, 4}, {0, 16}, 0.0f, 0, kPinkDark},
    {{0.18f, 0.1875f, -0.32f}, {4, 6, 4}, {0, 16}, 0.0f, 0, kPinkDark},
    {{-0.18f, 0.1875f, 0.32f}, {4, 6, 4}, {0, 16}, 0.0f, 0, kPinkDark},
    {{0.18f, 0.1875f, 0.32f}, {4, 6, 4}, {0, 16}, 0.0f, 0, kPinkDark},
}};

constexpr glm::vec3 kWool{0.92f, 0.92f, 0.90f};
constexpr glm::vec3 kSkin{0.86f, 0.74f, 0.70f};
constexpr glm::vec3 kSheepLeg{0.78f, 0.66f, 0.62f};

constexpr std::array<MobPart, 8> kSheep{{
    {{0.0f, 0.82f, 0.0f}, {8, 16, 6}, {28, 8}, 90.0f, 0, kSkin, 0.0f},  // body skin
    {{0.0f, 0.95f, 0.62f}, {6, 6, 8}, {0, 0}, 0.0f, 0, kSkin, 0.0f},    // head
    {{-0.20f, 0.375f, -0.34f}, {4, 12, 4}, {0, 16}, 0.0f, 0, kSheepLeg, 0.0f},
    {{0.20f, 0.375f, -0.34f}, {4, 12, 4}, {0, 16}, 0.0f, 0, kSheepLeg, 0.0f},
    {{-0.20f, 0.375f, 0.34f}, {4, 12, 4}, {0, 16}, 0.0f, 0, kSheepLeg, 0.0f},
    {{0.20f, 0.375f, 0.34f}, {4, 12, 4}, {0, 16}, 0.0f, 0, kSheepLeg, 0.0f},
    // Wool layer: same UV size as the skin (sheep_wool.png is 64x32), dilated
    // 1.75 px so it wraps just outside the body/head like vanilla fur.
    {{0.0f, 0.82f, 0.0f}, {8, 16, 6}, {28, 8}, 90.0f, 1, kWool, 1.75f}, // wool body
    {{0.0f, 0.95f, 0.50f}, {6, 6, 6}, {0, 0}, 0.0f, 1, kWool, 1.0f},    // wool head
}};

} // namespace

const MobInfo& mobInfo(MobType type) noexcept {
    return kMobs[static_cast<std::size_t>(type)];
}

std::string_view mobName(MobType type) noexcept {
    return kNames[static_cast<std::size_t>(type)];
}

std::span<const std::string_view> mobTextureCandidates(MobType type) noexcept {
    switch (type) {
    case MobType::Cow: return kCowTex;
    case MobType::Pig: return kPigTex;
    case MobType::Sheep: return kSheepTex;
    case MobType::Count: break;
    }
    return kCowTex;
}

std::span<const std::string_view> mobOverlayCandidates(MobType type) noexcept {
    if (type == MobType::Sheep) {
        return kSheepWool;
    }
    return {};
}

std::span<const MobPart> mobModel(MobType type) noexcept {
    switch (type) {
    case MobType::Cow: return kCow;
    case MobType::Pig: return kPig;
    case MobType::Sheep: return kSheep;
    case MobType::Count: break;
    }
    return kCow;
}

} // namespace cc
