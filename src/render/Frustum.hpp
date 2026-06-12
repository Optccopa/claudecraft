#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>

namespace cc {

class Frustum {
public:
    [[nodiscard]] static Frustum fromMatrix(const glm::mat4& viewProj) noexcept;

    [[nodiscard]] bool intersectsAabb(const glm::vec3& mn, const glm::vec3& mx) const noexcept;

private:
    std::array<glm::vec4, 6> m_planes{}; // xyz = normal, w = distance
};

} // namespace cc
