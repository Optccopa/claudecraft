#pragma once

#include "world/Block.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace cc {

// Passive overworld animals. The enum is the foundation for further mobs; add
// an entry plus a row in each table in Mob.cpp.
enum class MobType : std::uint8_t { Cow, Pig, Sheep, Count };

// One death-drop entry: item type with the inclusive count range rolled when
// the mob dies. Unused slots set type = Air. Counts from minecraft.wiki.
struct MobDrop {
    BlockType type;
    int minCount;
    int maxCount;
};

struct MobInfo {
    float health;    // hit points, 2 per heart — minecraft.wiki
    float halfWidth; // collision AABB half-extent on x/z, blocks — minecraft.wiki hitbox
    float height;    // collision AABB height, blocks — minecraft.wiki hitbox
    float walkSpeed; // wander speed, blocks/s (approximated from the speed attribute)
    std::array<MobDrop, 2> drops;
};

[[nodiscard]] const MobInfo& mobInfo(MobType type) noexcept;
[[nodiscard]] std::string_view mobName(MobType type) noexcept;

// Entity textures are authored against the vanilla 64x32 layout; box UVs unwrap
// into that space regardless of a pack's actual (HD) resolution.
inline constexpr int kEntityTexW = 64;
inline constexpr int kEntityTexH = 32;

// One box of a mob's blocky model. Authored like a Minecraft model part: the
// box's local size is given in texture pixels (1 px = 1/16 block) so its UV
// unwraps directly into the entity texture at `texOffset`. `center` is the box
// centre in blocks from the feet-centre origin (+z forward); `pitchDeg` rotates
// it about local x (the body lies down at 90°). `color` is the flat-shaded
// fallback when no pack supplies the texture. `layer` 0 is the base texture,
// 1 the overlay (sheep wool).
struct MobPart {
    glm::vec3 center;
    glm::vec3 sizePx;       // local box size in texture pixels (w, h, d)
    glm::ivec2 texOffset;   // top-left of the box's unwrap in the texture
    float pitchDeg;
    int layer;
    glm::vec3 color;
    // Geometry dilation in pixels added to each side, leaving UVs unchanged
    // (Minecraft's fur "inflate"). Used for the sheep wool layer so it sits
    // just outside the skin without its UV running off the texture.
    float inflate = 0.0f;
};

[[nodiscard]] std::span<const MobPart> mobModel(MobType type) noexcept;

// Candidate pack asset stems under textures/, tried in order (the loader takes
// the first a pack supplies). Covers the current biome-variant names
// ("entity/cow/temperate_cow"), the plain modern name, and the flat legacy
// layout. The overlay span is the wool layer (empty for mobs without one).
[[nodiscard]] std::span<const std::string_view> mobTextureCandidates(MobType type) noexcept;
[[nodiscard]] std::span<const std::string_view> mobOverlayCandidates(MobType type) noexcept;

} // namespace cc
