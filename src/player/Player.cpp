#include "player/Player.hpp"

#include "player/Camera.hpp"
#include "world/Block.hpp"
#include "world/World.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace cc {
namespace {

constexpr float kWalkSpeed = 4.5f;
constexpr float kFlySpeed = 12.0f;
constexpr float kGravity = 26.0f;
constexpr float kJumpVelocity = 8.2f;
constexpr float kTerminalVelocity = 60.0f;
constexpr float kSkin = 1e-4f; // gap kept between AABB and voxel faces

[[nodiscard]] bool solidAt(const World& world, int x, int y, int z) noexcept {
    return blockInfo(world.blockAt({x, y, z})).solid;
}

} // namespace

Player::Player(const glm::vec3& spawnPos) noexcept
    : m_position{spawnPos}, m_prevPosition{spawnPos} {}

void Player::addLook(float yawDelta, float pitchDelta) noexcept {
    m_yaw += yawDelta;
    m_pitch = std::clamp(m_pitch + pitchDelta, -89.0f, 89.0f);
}

glm::vec3 Player::eyePosition(float alpha) const noexcept {
    const glm::vec3 pos = m_prevPosition + (m_position - m_prevPosition) * alpha;
    return pos + glm::vec3{0.0f, kEyeAboveCenter, 0.0f};
}

bool Player::intersectsBlock(const glm::ivec3& cell) const noexcept {
    const glm::vec3 mn = m_position - kHalfExtents;
    const glm::vec3 mx = m_position + kHalfExtents;
    return static_cast<float>(cell.x + 1) > mn.x && static_cast<float>(cell.x) < mx.x &&
           static_cast<float>(cell.y + 1) > mn.y && static_cast<float>(cell.y) < mx.y &&
           static_cast<float>(cell.z + 1) > mn.z && static_cast<float>(cell.z) < mx.z;
}

// Swept AABB against the voxel grid, one axis at a time: scan every solid
// voxel the box would pass through along this axis and clamp the displacement
// to the nearest face. Scanning the whole travel region (not just the
// destination) makes the sweep tunnel-proof at any speed.
void Player::moveAxis(const World& world, int axis, float displacement) {
    if (displacement == 0.0f) {
        return;
    }
    glm::vec3 mn = m_position - kHalfExtents;
    glm::vec3 mx = m_position + kHalfExtents;
    if (displacement > 0.0f) {
        mx[axis] += displacement;
    } else {
        mn[axis] += displacement;
    }

    const glm::ivec3 lo{static_cast<int>(std::floor(mn.x)), static_cast<int>(std::floor(mn.y)),
                        static_cast<int>(std::floor(mn.z))};
    const glm::ivec3 hi{static_cast<int>(std::floor(mx.x - kSkin)),
                        static_cast<int>(std::floor(mx.y - kSkin)),
                        static_cast<int>(std::floor(mx.z - kSkin))};

    float allowed = displacement;
    const float face = displacement > 0.0f ? m_position[axis] + kHalfExtents[axis]
                                           : m_position[axis] - kHalfExtents[axis];
    bool collided = false;

    for (int x = lo.x; x <= hi.x; ++x) {
        for (int y = lo.y; y <= hi.y; ++y) {
            for (int z = lo.z; z <= hi.z; ++z) {
                if (!solidAt(world, x, y, z)) {
                    continue;
                }
                const glm::ivec3 cell{x, y, z};
                const float gap = displacement > 0.0f
                                      ? static_cast<float>(cell[axis]) - face
                                      : static_cast<float>(cell[axis] + 1) - face;
                if (displacement > 0.0f) {
                    if (gap < allowed) {
                        allowed = std::max(gap - kSkin, 0.0f);
                        collided = true;
                    }
                } else {
                    if (gap > allowed) {
                        allowed = std::min(gap + kSkin, 0.0f);
                        collided = true;
                    }
                }
            }
        }
    }

    m_position[axis] += allowed;
    if (collided) {
        if (axis == 1 && m_velocity.y < 0.0f) {
            m_onGround = true;
        }
        m_velocity[axis] = 0.0f;
    }
}

void Player::fixedUpdate(float dt, const World& world) {
    m_prevPosition = m_position;

    // Hold still until the terrain under us exists — otherwise the player
    // falls through chunks that are still generating.
    if (!world.isChunkLoadedAt(m_position)) {
        return;
    }

    const glm::vec3 forward = Camera::direction(m_yaw, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}));
    glm::vec3 wish = forward * m_input.move.y + right * m_input.move.x;
    if (glm::dot(wish, wish) > 1.0f) {
        wish = glm::normalize(wish);
    }

    if (m_flying) {
        const float flySpeed = kFlySpeed * m_speedMultiplier;
        glm::vec3 target = wish * flySpeed;
        target.y = (m_input.jump ? flySpeed : 0.0f) - (m_input.descend ? flySpeed : 0.0f);
        m_velocity = target;
        m_onGround = false;
    } else {
        const float walkSpeed = kWalkSpeed * m_speedMultiplier;
        m_velocity.x = wish.x * walkSpeed;
        m_velocity.z = wish.z * walkSpeed;
        m_velocity.y = std::max(m_velocity.y - kGravity * dt, -kTerminalVelocity);
        if (m_input.jump && m_onGround) {
            m_velocity.y = kJumpVelocity;
        }
        m_onGround = false;
    }

    // Y first so landing settles before horizontal sweeps hug walls.
    moveAxis(world, 1, m_velocity.y * dt);
    moveAxis(world, 0, m_velocity.x * dt);
    moveAxis(world, 2, m_velocity.z * dt);
}

} // namespace cc
