#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace cc {

class World;

struct PlayerInput {
    glm::vec2 move{0.0f}; // x: strafe right, y: forward (each -1..1)
    bool jump = false;    // also "fly up"
    bool descend = false; // fly down
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

    void fixedUpdate(float dt, const World& world);

    [[nodiscard]] glm::vec3 position() const noexcept { return m_position; }
    [[nodiscard]] glm::vec3 eyePosition(float alpha) const noexcept;
    [[nodiscard]] float yaw() const noexcept { return m_yaw; }
    [[nodiscard]] float pitch() const noexcept { return m_pitch; }
    [[nodiscard]] bool flying() const noexcept { return m_flying; }
    [[nodiscard]] bool onGround() const noexcept { return m_onGround; }

    // Placement guard: would a block in this cell overlap the player AABB?
    [[nodiscard]] bool intersectsBlock(const glm::ivec3& cell) const noexcept;

private:
    void moveAxis(const World& world, int axis, float displacement);

    static constexpr glm::vec3 kHalfExtents{0.3f, 0.9f, 0.3f};
    static constexpr float kEyeAboveCenter = 0.72f; // eye at 1.62 m over feet

    glm::vec3 m_position;     // AABB center
    glm::vec3 m_prevPosition;
    glm::vec3 m_velocity{0.0f};
    float m_yaw = -90.0f;
    float m_pitch = 0.0f;
    bool m_flying = false;
    bool m_onGround = false;
    PlayerInput m_input;
};

} // namespace cc
