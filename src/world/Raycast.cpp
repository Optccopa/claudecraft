#include "world/Raycast.hpp"

#include "world/Block.hpp"
#include "world/World.hpp"

#include <cmath>
#include <limits>

namespace cc {

// Amanatides & Woo voxel traversal: step one cell at a time along the axis
// whose boundary the ray crosses next, tracking per-axis distance-to-boundary
// (tMax) and distance-per-cell (tDelta). Exact — never skips a corner cell.
RaycastHit raycast(const World& world, const glm::vec3& origin, const glm::vec3& direction,
                   float maxDistance) {
    glm::ivec3 cell{static_cast<int>(std::floor(origin.x)),
                    static_cast<int>(std::floor(origin.y)),
                    static_cast<int>(std::floor(origin.z))};
    glm::ivec3 previous = cell;

    glm::ivec3 step{};
    glm::vec3 tMax{};
    glm::vec3 tDelta{};
    constexpr float kInf = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
        const float d = direction[axis];
        if (d > 0.0f) {
            step[axis] = 1;
            tDelta[axis] = 1.0f / d;
            tMax[axis] = (std::floor(origin[axis]) + 1.0f - origin[axis]) / d;
        } else if (d < 0.0f) {
            step[axis] = -1;
            tDelta[axis] = -1.0f / d;
            tMax[axis] = (origin[axis] - std::floor(origin[axis])) / -d;
        } else {
            step[axis] = 0;
            tDelta[axis] = kInf;
            tMax[axis] = kInf;
        }
    }

    float t = 0.0f;
    while (t <= maxDistance) {
        if (blockInfo(world.blockAt(cell)).solid) {
            return RaycastHit{true, cell, previous};
        }
        previous = cell;
        const int axis = (tMax.x < tMax.y) ? (tMax.x < tMax.z ? 0 : 2)
                                           : (tMax.y < tMax.z ? 1 : 2);
        t = tMax[axis];
        tMax[axis] += tDelta[axis];
        cell[axis] += step[axis];
    }
    return RaycastHit{};
}

} // namespace cc
