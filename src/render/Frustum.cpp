#include "render/Frustum.hpp"

#include <glm/geometric.hpp>

namespace cc {

// Gribb/Hartmann plane extraction: each clip plane is a row combination of
// the view-projection matrix (glm is column-major, hence the transposed
// indexing). Normals point inward.
Frustum Frustum::fromMatrix(const glm::mat4& m) noexcept {
    Frustum f;
    for (int i = 0; i < 3; ++i) {
        f.m_planes[static_cast<std::size_t>(i * 2)] =
            glm::vec4{m[0][3] + m[0][i], m[1][3] + m[1][i], m[2][3] + m[2][i], m[3][3] + m[3][i]};
        f.m_planes[static_cast<std::size_t>(i * 2 + 1)] =
            glm::vec4{m[0][3] - m[0][i], m[1][3] - m[1][i], m[2][3] - m[2][i], m[3][3] - m[3][i]};
    }
    for (auto& plane : f.m_planes) {
        plane /= glm::length(glm::vec3{plane});
    }
    return f;
}

bool Frustum::intersectsAabb(const glm::vec3& mn, const glm::vec3& mx) const noexcept {
    // p-vertex test: the box is outside iff its most positive corner along
    // the plane normal is behind any plane.
    for (const glm::vec4& plane : m_planes) {
        const glm::vec3 p{plane.x > 0.0f ? mx.x : mn.x, plane.y > 0.0f ? mx.y : mn.y,
                          plane.z > 0.0f ? mx.z : mn.z};
        if (glm::dot(glm::vec3{plane}, p) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace cc
