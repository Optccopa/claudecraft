#pragma once

#include <glm/vec3.hpp>

namespace cc {

class World;

struct RaycastHit {
    bool hit = false;
    glm::ivec3 block{};    // first solid block along the ray
    glm::ivec3 previous{}; // empty cell entered just before the hit (placement target)
};

[[nodiscard]] RaycastHit raycast(const World& world, const glm::vec3& origin,
                                 const glm::vec3& direction, float maxDistance);

} // namespace cc
