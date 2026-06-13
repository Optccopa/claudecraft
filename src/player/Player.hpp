#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

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

    void fixedUpdate(float dt, const World& world);

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
};

} // namespace cc
