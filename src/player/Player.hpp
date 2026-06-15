#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>

namespace cc {

class World;

struct PlayerInput {
    glm::vec2 move{0.0f}; // x: strafe right, y: forward (each -1..1)
    bool jump = false;    // also "fly up"
    bool descend = false; // fly down
    bool sprint = false;  // move faster (ignored while crouching)
};

// First-person player: swept AABB collision against voxels, gravity/jump in
// walk mode, free movement in fly mode. Runs at a fixed timestep; the
// previous position is kept for render interpolation.
class Player {
public:
    explicit Player(const glm::vec3& spawnPos) noexcept;

    void setInput(const PlayerInput& input) noexcept { m_input = input; }
    void addLook(float yawDelta, float pitchDelta) noexcept;
    void toggleFly() noexcept { m_flying = !m_flying; }
    void setSpeedMultiplier(float multiplier) noexcept { m_speedMultiplier = multiplier; }
    // Survival enables hunger, damage and regeneration; creative is invulnerable.
    void setSurvival(bool survival) noexcept { m_survival = survival; }
    void addExhaustion(float amount) noexcept { m_exhaustion += amount; }

    void fixedUpdate(float dt, const World& world);

    static constexpr float kMaxHealth = 20.0f; // half-hearts
    static constexpr float kMaxHunger = 20.0f; // food points
    [[nodiscard]] float health() const noexcept { return m_health; }
    [[nodiscard]] float hunger() const noexcept { return m_hunger; }
    [[nodiscard]] float saturation() const noexcept { return m_saturation; }
    [[nodiscard]] float exhaustion() const noexcept { return m_exhaustion; }
    [[nodiscard]] float air() const noexcept { return m_air; } // 0..1 breath remaining

    // Restore persisted vitals on world re-entry (clamped to valid ranges).
    void restoreVitals(float health, float hunger, float saturation, float exhaustion,
                       float air) noexcept {
        m_health = std::clamp(health, 0.0f, kMaxHealth);
        m_hunger = std::clamp(hunger, 0.0f, kMaxHunger);
        m_saturation = std::max(0.0f, saturation);
        m_exhaustion = std::max(0.0f, exhaustion);
        m_air = std::clamp(air, 0.0f, 1.0f);
    }
    // Dead once health hits zero; the app shows the death screen and respawns.
    [[nodiscard]] bool dead() const noexcept { return m_survival && m_health <= 0.0f; }

    void setLook(float yaw, float pitch) noexcept {
        m_yaw = yaw;
        m_pitch = std::clamp(pitch, -89.0f, 89.0f);
    }
    // Reset to full survival state at pos (death respawn / fresh start).
    void respawn(const glm::vec3& pos) noexcept {
        m_position = pos;
        m_prevPosition = pos;
        m_velocity = glm::vec3{0.0f};
        m_health = kMaxHealth;
        m_hunger = kMaxHunger;
        m_saturation = 5.0f;
        m_exhaustion = 0.0f;
        m_air = 1.0f;
        m_regenTimer = m_starveTimer = m_drownTimer = 0.0f;
        m_airborne = false;
    }

    [[nodiscard]] glm::vec3 position() const noexcept { return m_position; }
    [[nodiscard]] glm::vec3 velocity() const noexcept { return m_velocity; }
    [[nodiscard]] glm::vec3 eyePosition(float alpha) const noexcept;
    [[nodiscard]] float yaw() const noexcept { return m_yaw; }
    [[nodiscard]] float pitch() const noexcept { return m_pitch; }
    [[nodiscard]] bool flying() const noexcept { return m_flying; }
    [[nodiscard]] bool onGround() const noexcept { return m_onGround; }
    [[nodiscard]] bool crouching() const noexcept { return m_crouching; }

    // Placement guard: would a block in this cell overlap the player AABB?
    [[nodiscard]] bool intersectsBlock(const glm::ivec3& cell) const noexcept;

private:
    void moveAxis(const World& world, int axis, float displacement);
    void moveHorizontalAxis(const World& world, int axis, float displacement);
    void updateCrouch(const World& world);
    // Hunger/exhaustion drain, regeneration, fall/drown/starve damage, and
    // respawn on death — all no-ops outside survival.
    void updateSurvival(float dt, const World& world, bool inWater);
    [[nodiscard]] glm::vec3 halfExtents() const noexcept { return {kHalfXZ, m_halfHeight, kHalfXZ}; }
    // Is there solid ground directly under the AABB at its current position?
    [[nodiscard]] bool groundBelow(const World& world) const noexcept;
    // Does the current AABB overlap any solid voxel (stand-up clearance test)?
    [[nodiscard]] bool headroomBlocked(const World& world) const noexcept;

    static constexpr float kHalfXZ = 0.3f;
    static constexpr float kStandHalfHeight = 0.9f;
    static constexpr float kCrouchHalfHeight = 0.6f;
    static constexpr float kEyeBelowTop = 0.18f; // eye sits this far under the box top

    glm::vec3 m_position;     // AABB center
    glm::vec3 m_prevPosition;
    glm::vec3 m_velocity{0.0f};
    float m_yaw = -90.0f;
    float m_pitch = 0.0f;
    bool m_flying = false;
    bool m_onGround = false;
    bool m_crouching = false;
    float m_halfHeight = kStandHalfHeight;
    float m_speedMultiplier = 1.0f; // cheat: scales walk + fly speed
    PlayerInput m_input;

    // Survival state (half-hearts / food points). Inert in creative.
    bool m_survival = false;
    float m_health = kMaxHealth;
    float m_hunger = kMaxHunger;
    float m_saturation = 5.0f;
    float m_exhaustion = 0.0f;
    float m_air = 1.0f;        // breath remaining, 0..1 (full = ~15 s)
    float m_regenTimer = 0.0f;
    float m_starveTimer = 0.0f;
    float m_drownTimer = 0.0f;
    bool m_airborne = false;   // tracking a fall for landing damage
    float m_fallPeakY = 0.0f;  // highest Y reached since leaving the ground
};

} // namespace cc
