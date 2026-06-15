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
constexpr float kCrouchFactor = 0.3f; // walk-speed scale while sneaking
constexpr float kSprintFactor = 1.5f; // walk/fly-speed scale while sprinting
constexpr float kFlySpeed = 12.0f;
constexpr float kGravity = 26.0f;
constexpr float kJumpVelocity = 8.2f;
constexpr float kTerminalVelocity = 60.0f;
constexpr float kSkin = 1e-4f; // gap kept between AABB and voxel faces

// Swimming: water drags movement to half speed, you sink slowly, and the jump
// bind swims up / the descend bind dives. Vertical velocity eases toward a
// target so it feels buoyant rather than gravity-driven.
constexpr float kSwimFactor = 0.5f;
constexpr float kSwimSink = 1.6f;
constexpr float kSwimRise = 4.0f;
constexpr float kSwimVertAccel = 9.0f;

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
    return pos + glm::vec3{0.0f, m_halfHeight - kEyeBelowTop, 0.0f};
}

bool Player::intersectsBlock(const glm::ivec3& cell) const noexcept {
    const glm::vec3 half = halfExtents();
    const glm::vec3 mn = m_position - half;
    const glm::vec3 mx = m_position + half;
    return static_cast<float>(cell.x + 1) > mn.x && static_cast<float>(cell.x) < mx.x &&
           static_cast<float>(cell.y + 1) > mn.y && static_cast<float>(cell.y) < mx.y &&
           static_cast<float>(cell.z + 1) > mn.z && static_cast<float>(cell.z) < mx.z;
}

// Scan the thin slab just under the feet across the full XZ footprint.
bool Player::groundBelow(const World& world) const noexcept {
    const glm::vec3 half = halfExtents();
    const float feet = m_position.y - half.y;
    const int y = static_cast<int>(std::floor(feet - 2.0f * kSkin));
    const int x0 = static_cast<int>(std::floor(m_position.x - half.x + kSkin));
    const int x1 = static_cast<int>(std::floor(m_position.x + half.x - kSkin));
    const int z0 = static_cast<int>(std::floor(m_position.z - half.z + kSkin));
    const int z1 = static_cast<int>(std::floor(m_position.z + half.z - kSkin));
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
            if (solidAt(world, x, y, z)) {
                return true;
            }
        }
    }
    return false;
}

bool Player::headroomBlocked(const World& world) const noexcept {
    const glm::vec3 mn = m_position - halfExtents();
    const glm::vec3 mx = m_position + halfExtents();
    const glm::ivec3 lo{static_cast<int>(std::floor(mn.x + kSkin)),
                        static_cast<int>(std::floor(mn.y + kSkin)),
                        static_cast<int>(std::floor(mn.z + kSkin))};
    const glm::ivec3 hi{static_cast<int>(std::floor(mx.x - kSkin)),
                        static_cast<int>(std::floor(mx.y - kSkin)),
                        static_cast<int>(std::floor(mx.z - kSkin))};
    for (int x = lo.x; x <= hi.x; ++x) {
        for (int y = lo.y; y <= hi.y; ++y) {
            for (int z = lo.z; z <= hi.z; ++z) {
                if (solidAt(world, x, y, z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Swept AABB against the voxel grid, one axis at a time: scan every solid
// voxel the box would pass through along this axis and clamp the displacement
// to the nearest face. Scanning the whole travel region (not just the
// destination) makes the sweep tunnel-proof at any speed.
void Player::moveAxis(const World& world, int axis, float displacement) {
    if (displacement == 0.0f) {
        return;
    }
    const glm::vec3 half = halfExtents();
    glm::vec3 mn = m_position - half;
    glm::vec3 mx = m_position + half;
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
    const float face = displacement > 0.0f ? m_position[axis] + half[axis]
                                           : m_position[axis] - half[axis];
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

    // Crouch shares the descend bind; only meaningful on foot. The box
    // shrinks from the top, so keep the feet planted by lowering the centre
    // (and raise it back on stand-up, but only when there's headroom).
    updateCrouch(world);

    const glm::vec3 forward = Camera::direction(m_yaw, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}));
    glm::vec3 wish = forward * m_input.move.y + right * m_input.move.x;
    if (glm::dot(wish, wish) > 1.0f) {
        wish = glm::normalize(wish);
    }

    // Sprint scales horizontal speed; crouch wins so sneaking stays slow.
    const float sprintFactor = m_input.sprint ? kSprintFactor : 1.0f;

    // Sample at the feet so swimming holds until the player has fully risen
    // out — that's what lets a held jump carry you up onto the bank.
    const float feetY = m_position.y - m_halfHeight + 0.1f;
    const glm::ivec3 feetCell{static_cast<int>(std::floor(m_position.x)),
                              static_cast<int>(std::floor(feetY)),
                              static_cast<int>(std::floor(m_position.z))};
    const bool inWater = isLiquid(world.blockAt(feetCell));

    if (m_flying) {
        const float flySpeed = kFlySpeed * m_speedMultiplier * sprintFactor;
        glm::vec3 target = wish * flySpeed;
        target.y = (m_input.jump ? flySpeed : 0.0f) - (m_input.descend ? flySpeed : 0.0f);
        m_velocity = target;
        m_onGround = false;
    } else if (inWater) {
        const float swimSpeed = kWalkSpeed * m_speedMultiplier * kSwimFactor;
        m_velocity.x = wish.x * swimSpeed;
        m_velocity.z = wish.z * swimSpeed;
        const float targetY =
            m_input.jump ? kSwimRise : (m_input.descend ? -kSwimRise : -kSwimSink);
        m_velocity.y += (targetY - m_velocity.y) * std::min(kSwimVertAccel * dt, 1.0f);
        m_onGround = false;
    } else {
        const float walkSpeed = kWalkSpeed * m_speedMultiplier *
                                (m_crouching ? kCrouchFactor : sprintFactor);
        m_velocity.x = wish.x * walkSpeed;
        m_velocity.z = wish.z * walkSpeed;
        m_velocity.y = std::max(m_velocity.y - kGravity * dt, -kTerminalVelocity);
        if (m_input.jump && m_onGround) {
            m_velocity.y = kJumpVelocity;
            if (m_survival) {
                m_exhaustion += m_input.sprint ? 0.2f : 0.05f; // (sprint-)jump cost
            }
        }
        m_onGround = false;
    }

    // Y first so landing settles before horizontal sweeps hug walls.
    moveAxis(world, 1, m_velocity.y * dt);
    moveHorizontalAxis(world, 0, m_velocity.x * dt);
    moveHorizontalAxis(world, 2, m_velocity.z * dt);

    if (m_survival) {
        updateSurvival(dt, world, inWater);
    }
}

void Player::updateSurvival(float dt, const World& world, bool inWater) {
    // Fall damage: track the apex since leaving the ground; on landing, lose
    // one half-heart per block past a 3-block cushion. Water cancels it.
    if (m_flying || inWater) {
        m_airborne = false;
    } else if (!m_onGround) {
        if (!m_airborne) {
            m_airborne = true;
            m_fallPeakY = m_position.y;
        }
        m_fallPeakY = std::max(m_fallPeakY, m_position.y);
    } else if (m_airborne) {
        const float fallen = m_fallPeakY - m_position.y;
        m_health -= std::max(0.0f, fallen - 3.0f);
        m_airborne = false;
    }

    // Hunger: movement and actions build exhaustion; every 4 points spends a
    // saturation, then a food point. (Walking on foot is free, as in vanilla.)
    const float horiz = std::hypot(m_position.x - m_prevPosition.x, m_position.z - m_prevPosition.z);
    if (inWater) {
        m_exhaustion += 0.01f * horiz;
    } else if (m_input.sprint && !m_flying) {
        m_exhaustion += 0.1f * horiz;
    }
    while (m_exhaustion >= 4.0f) {
        m_exhaustion -= 4.0f;
        if (m_saturation > 0.0f) {
            m_saturation = std::max(0.0f, m_saturation - 1.0f);
        } else {
            m_hunger = std::max(0.0f, m_hunger - 1.0f);
        }
    }

    // Natural regen at 9+ shanks: one half-heart every 4 s, paid in exhaustion.
    if (m_health < kMaxHealth && m_hunger >= 18.0f) {
        m_regenTimer += dt;
        if (m_regenTimer >= 4.0f) {
            m_regenTimer = 0.0f;
            m_health = std::min(kMaxHealth, m_health + 1.0f);
            m_exhaustion += 6.0f;
        }
    } else {
        m_regenTimer = 0.0f;
    }

    // Starvation at empty hunger: one half-heart every 4 s, floored above death.
    if (m_hunger <= 0.0f) {
        m_starveTimer += dt;
        if (m_starveTimer >= 4.0f) {
            m_starveTimer = 0.0f;
            m_health = std::max(1.0f, m_health - 1.0f);
        }
    } else {
        m_starveTimer = 0.0f;
    }

    // Drowning: a submerged head drains ~15 s of breath, then one heart per
    // second until it surfaces; air refills quickly out of water.
    const float eyeY = m_position.y + m_halfHeight - kEyeBelowTop;
    const glm::ivec3 headCell{static_cast<int>(std::floor(m_position.x)),
                              static_cast<int>(std::floor(eyeY)),
                              static_cast<int>(std::floor(m_position.z))};
    if (isLiquid(world.blockAt(headCell))) {
        m_air = std::max(0.0f, m_air - dt / 15.0f);
        if (m_air <= 0.0f) {
            m_drownTimer += dt;
            if (m_drownTimer >= 1.0f) {
                m_drownTimer = 0.0f;
                m_health -= 2.0f;
            }
        }
    } else {
        m_air = std::min(1.0f, m_air + dt * 0.5f);
        m_drownTimer = 0.0f;
    }

    // Death is surfaced to the app (death screen + loot drop + respawn), not
    // handled here. Clamp so the HUD never shows negative or overfull hearts.
    m_health = std::clamp(m_health, 0.0f, kMaxHealth);
}

void Player::updateCrouch(const World& world) {
    const bool want = !m_flying && m_input.descend;
    if (want == m_crouching) {
        return;
    }
    if (want) {
        m_position.y -= kStandHalfHeight - kCrouchHalfHeight;
        m_halfHeight = kCrouchHalfHeight;
        m_crouching = true;
    } else {
        // Only stand if the taller box wouldn't clip into a block above.
        m_halfHeight = kStandHalfHeight;
        m_position.y += kStandHalfHeight - kCrouchHalfHeight;
        if (headroomBlocked(world)) {
            m_position.y -= kStandHalfHeight - kCrouchHalfHeight;
            m_halfHeight = kCrouchHalfHeight;
        } else {
            m_crouching = false;
        }
    }
}

// Horizontal move with sneak edge-stop: a crouching, grounded player won't
// step past a ledge — revert the axis if the move would leave no ground.
void Player::moveHorizontalAxis(const World& world, int axis, float displacement) {
    if (m_crouching && m_onGround && displacement != 0.0f) {
        const float before = m_position[axis];
        moveAxis(world, axis, displacement);
        if (!groundBelow(world)) {
            m_position[axis] = before;
            m_velocity[axis] = 0.0f;
        }
        return;
    }
    moveAxis(world, axis, displacement);
}

} // namespace cc
