#include "input/Input.hpp"

#include <GLFW/glfw3.h>

namespace cc {
namespace {

[[nodiscard]] Input& self(GLFWwindow* window) noexcept {
    return *static_cast<Input*>(glfwGetWindowUserPointer(window));
}

} // namespace

Input::Input(GLFWwindow* window) noexcept {
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursorPos);
    glfwSetScrollCallback(window, onScroll);
}

void Input::beginFrame() noexcept {
    m_keyPressed.fill(false);
    m_buttonPressed.fill(false);
    m_mouseDelta = glm::vec2{0.0f};
    m_scroll = 0.0f;
}

bool Input::isDown(int key) const noexcept {
    return key >= 0 && key < kKeyCount && m_keyDown[static_cast<std::size_t>(key)];
}

bool Input::wasPressed(int key) const noexcept {
    return key >= 0 && key < kKeyCount && m_keyPressed[static_cast<std::size_t>(key)];
}

bool Input::isMouseDown(int button) const noexcept {
    return button >= 0 && button < kButtonCount && m_buttonDown[static_cast<std::size_t>(button)];
}

bool Input::wasMousePressed(int button) const noexcept {
    return button >= 0 && button < kButtonCount &&
           m_buttonPressed[static_cast<std::size_t>(button)];
}

void Input::onKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (key < 0 || key >= kKeyCount) {
        return;
    }
    Input& input = self(window);
    if (action == GLFW_PRESS) {
        input.m_keyDown[static_cast<std::size_t>(key)] = true;
        input.m_keyPressed[static_cast<std::size_t>(key)] = true;
    } else if (action == GLFW_RELEASE) {
        input.m_keyDown[static_cast<std::size_t>(key)] = false;
    }
}

void Input::onMouseButton(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;
    if (button < 0 || button >= kButtonCount) {
        return;
    }
    Input& input = self(window);
    if (action == GLFW_PRESS) {
        input.m_buttonDown[static_cast<std::size_t>(button)] = true;
        input.m_buttonPressed[static_cast<std::size_t>(button)] = true;
    } else if (action == GLFW_RELEASE) {
        input.m_buttonDown[static_cast<std::size_t>(button)] = false;
    }
}

void Input::onCursorPos(GLFWwindow* window, double x, double y) {
    Input& input = self(window);
    const glm::vec2 pos{static_cast<float>(x), static_cast<float>(y)};
    // Swallow the first sample: the initial cursor position would otherwise
    // register as a huge look jump.
    if (input.m_firstMouse) {
        input.m_lastMousePos = pos;
        input.m_firstMouse = false;
        return;
    }
    input.m_mouseDelta += pos - input.m_lastMousePos;
    input.m_lastMousePos = pos;
}

void Input::onScroll(GLFWwindow* window, double dx, double dy) {
    (void)dx;
    self(window).m_scroll += static_cast<float>(dy);
}

} // namespace cc
