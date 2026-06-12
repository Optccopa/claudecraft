#include "player/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace cc {

Camera::Camera(float fovDeg, float aspect, float nearZ, float farZ) noexcept
    : m_fovDeg{fovDeg}, m_aspect{aspect}, m_near{nearZ}, m_far{farZ} {}

glm::mat4 Camera::projection() const noexcept {
    return glm::perspective(glm::radians(m_fovDeg), m_aspect, m_near, m_far);
}

glm::vec3 Camera::direction(float yawDeg, float pitchDeg) noexcept {
    const float yaw = glm::radians(yawDeg);
    const float pitch = glm::radians(pitchDeg);
    return glm::normalize(glm::vec3{std::cos(yaw) * std::cos(pitch), std::sin(pitch),
                                    std::sin(yaw) * std::cos(pitch)});
}

glm::mat4 Camera::view(const glm::vec3& eye, float yawDeg, float pitchDeg) noexcept {
    return glm::lookAt(eye, eye + direction(yawDeg, pitchDeg), glm::vec3{0.0f, 1.0f, 0.0f});
}

} // namespace cc
