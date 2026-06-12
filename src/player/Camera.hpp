#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace cc {

class Camera {
public:
    Camera(float fovDeg, float aspect, float nearZ, float farZ) noexcept;

    void setAspect(float aspect) noexcept { m_aspect = aspect; }

    [[nodiscard]] glm::mat4 projection() const noexcept;
    [[nodiscard]] static glm::mat4 view(const glm::vec3& eye, float yawDeg,
                                        float pitchDeg) noexcept;
    [[nodiscard]] static glm::vec3 direction(float yawDeg, float pitchDeg) noexcept;

private:
    float m_fovDeg;
    float m_aspect;
    float m_near;
    float m_far;
};

} // namespace cc
