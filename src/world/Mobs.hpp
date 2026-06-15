#pragma once

#include "world/Mob.hpp"

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace cc {

class World;

struct MobEntity {
    MobType type;
    glm::vec3 position;          // feet centre
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;            // degrees; model +z faces this heading
    float health = 0.0f;
    float wanderTimer = 0.0f;    // counts down to the next AI decision
    bool moving = false;
    bool onGround = false;
    float hurtFlash = 0.0f;      // seconds of red tint left after a hit
};

// Passive-mob simulation: wander AI, gravity, AABB-vs-world collision, melee
// damage and death drops. Not persisted — mobs repopulate from spawning each
// session, which keeps them out of the save format (like Drops).
class Mobs {
public:
    void update(float dt, const World& world, std::uint32_t& rng);

    // Spawns a herd of one type around `center` on valid grass ground; returns
    // how many actually spawned. No-op once the global cap is reached.
    int spawnHerd(const World& world, const glm::vec3& center, MobType type, int count,
                  std::uint32_t& rng);

    struct Loot {
        BlockType type;
        int count;
        glm::vec3 position;
    };
    // Melee from the camera ray: the nearest mob whose AABB the ray enters
    // within `reach` takes `damage`. On a kill, appends the rolled drops to
    // `loot` and removes the mob. Returns true if any mob was hit.
    bool attack(const glm::vec3& origin, const glm::vec3& dir, float reach, float damage,
                std::uint32_t& rng, std::vector<Loot>& loot);

    [[nodiscard]] std::span<const MobEntity> all() const noexcept { return m_mobs; }
    [[nodiscard]] std::size_t count() const noexcept { return m_mobs.size(); }
    void clear() noexcept { m_mobs.clear(); }

    // Removes mobs farther than `radius` (horizontal) from `center` so herds
    // don't accumulate as the player roams.
    void cullDistant(const glm::vec3& center, float radius);

    // Persistence: a small flat record per mob (type, pose, health). Velocity
    // and AI timers are transient and reset on load. saveTo writes nothing when
    // empty so worlds with no mobs leave no file.
    void saveTo(const std::filesystem::path& path) const;
    void loadFrom(const std::filesystem::path& path);

private:
    std::vector<MobEntity> m_mobs;
};

} // namespace cc
