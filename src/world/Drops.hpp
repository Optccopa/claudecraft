#pragma once

#include "world/Block.hpp"

#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace cc {

class World;

struct Drop {
    glm::vec3 position; // center of the floating mini-cube
    glm::vec3 velocity;
    BlockType type;
    float age = 0.0f;
    float pickupDelay = 0.0f; // suppresses magnet + pickup until it elapses
};

// Dropped-item entities: gravity, settle on solid ground, magnet toward the
// player when close. Not persisted — they despawn with the session (and on
// a timer), which keeps them out of the save format entirely.
class Drops {
public:
    void spawn(const glm::ivec3& cell, BlockType type, std::uint32_t scatterHash);

    // Player-thrown item (Q): launches from origin along dir with a brief
    // pickup delay so it doesn't magnet straight back to the thrower.
    void throwOut(const glm::vec3& origin, const glm::vec3& dir, BlockType type);

    // Fixed-timestep simulation; returns the types picked up this step
    // (one entry per drop collected).
    [[nodiscard]] std::vector<BlockType> update(float dt, const World& world,
                                                const glm::vec3& playerPos);

    [[nodiscard]] std::span<const Drop> all() const noexcept { return m_drops; }
    void clear() noexcept { m_drops.clear(); }

private:
    std::vector<Drop> m_drops;
};

} // namespace cc
