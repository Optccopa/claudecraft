#include "world/Drops.hpp"

#include "world/World.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace cc {
namespace {

constexpr float kGravity = 18.0f;
constexpr float kGroundFriction = 6.0f;
constexpr float kMagnetRadius = 2.2f;
constexpr float kPickupRadius = 0.9f;
constexpr float kMagnetSpeed = 6.0f;
constexpr float kDespawnSeconds = 300.0f;
constexpr float kThrowSpeed = 5.0f;
constexpr float kThrowPickupDelay = 0.8f;

[[nodiscard]] bool solidAt(const World& world, const glm::vec3& p) noexcept {
    return blockInfo(world.blockAt(glm::ivec3{static_cast<int>(std::floor(p.x)),
                                              static_cast<int>(std::floor(p.y)),
                                              static_cast<int>(std::floor(p.z))}))
        .solid;
}

} // namespace

void Drops::spawn(const glm::ivec3& cell, BlockType type, std::uint32_t scatterHash) {
    // Small deterministic scatter so stacks of drops don't z-fight.
    const float dx = static_cast<float>(scatterHash & 0xFFu) / 255.0f - 0.5f;
    const float dz = static_cast<float>((scatterHash >> 8) & 0xFFu) / 255.0f - 0.5f;
    m_drops.push_back(Drop{
        glm::vec3{static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.4f,
                  static_cast<float>(cell.z) + 0.5f},
        glm::vec3{dx * 2.0f, 2.2f, dz * 2.0f},
        type,
    });
}

void Drops::throwOut(const glm::vec3& origin, const glm::vec3& dir, BlockType type) {
    m_drops.push_back(Drop{
        origin,
        dir * kThrowSpeed + glm::vec3{0.0f, 1.5f, 0.0f},
        type,
        0.0f,
        kThrowPickupDelay,
    });
}

std::vector<BlockType> Drops::update(float dt, const World& world, const glm::vec3& playerPos) {
    std::vector<BlockType> pickedUp;

    for (Drop& drop : m_drops) {
        drop.age += dt;
        drop.pickupDelay = std::max(drop.pickupDelay - dt, 0.0f);

        const glm::vec3 toPlayer = playerPos - drop.position;
        const float dist = glm::length(toPlayer);
        if (drop.pickupDelay == 0.0f && dist < kMagnetRadius && dist > 1e-3f) {
            drop.velocity = toPlayer * (kMagnetSpeed / dist);
        } else {
            drop.velocity.y -= kGravity * dt;
        }

        glm::vec3 next = drop.position + drop.velocity * dt;
        // Point collision is enough for a 0.3-cube: block the axis that
        // would enter a solid cell instead of resolving a full AABB sweep.
        if (solidAt(world, glm::vec3{next.x, drop.position.y, drop.position.z})) {
            next.x = drop.position.x;
            drop.velocity.x = 0.0f;
        }
        if (solidAt(world, glm::vec3{next.x, drop.position.y, next.z})) {
            next.z = drop.position.z;
            drop.velocity.z = 0.0f;
        }
        if (solidAt(world, glm::vec3{next.x, next.y - 0.18f, next.z})) {
            next.y = drop.position.y;
            drop.velocity.y = 0.0f;
            // settle: bleed horizontal motion once grounded
            drop.velocity.x -= drop.velocity.x * std::min(kGroundFriction * dt, 1.0f);
            drop.velocity.z -= drop.velocity.z * std::min(kGroundFriction * dt, 1.0f);
        }
        drop.position = next;

        if (drop.pickupDelay == 0.0f && dist < kPickupRadius) {
            pickedUp.push_back(drop.type);
            drop.age = kDespawnSeconds + 1.0f; // mark for removal below
        }
    }

    std::erase_if(m_drops, [](const Drop& drop) {
        return drop.age > kDespawnSeconds || drop.position.y < -8.0f;
    });
    return pickedUp;
}

} // namespace cc
