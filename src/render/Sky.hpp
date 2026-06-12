#pragma once

#include <glm/vec3.hpp>

namespace cc {

struct SkyState {
    glm::vec3 sunDirection; // unit; below the horizon at night
    glm::vec3 skyColor;     // also the fog/clear colour
    float skyLight;         // 0..1 scale on the vertex sky-light channel
};

// Time of day is a [0,1) day fraction: 0 sunrise, 0.25 noon, 0.5 sunset,
// 0.75 midnight.
[[nodiscard]] SkyState skyStateAt(double timeOfDay) noexcept;

} // namespace cc
