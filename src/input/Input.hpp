#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <string>

struct GLFWwindow;

namespace cc {

// Polled view over GLFW input events. Installs the window callbacks and
// claims the window user pointer. Call beginFrame() before glfwPollEvents()
// each frame to reset edge-triggered state and deltas.
class Input {
public:
    explicit Input(GLFWwindow* window) noexcept;

    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&&) = delete;
    Input& operator=(Input&&) = delete;
    ~Input() = default;

    void beginFrame() noexcept;

    [[nodiscard]] bool isDown(int key) const noexcept;
    // Edge-triggered; includes OS key-repeat so held backspace keeps erasing.
    [[nodiscard]] bool wasPressed(int key) const noexcept;
    // Printable ASCII typed since the last beginFrame (for text fields).
    [[nodiscard]] const std::string& typedText() const noexcept { return m_typed; }
    [[nodiscard]] bool isMouseDown(int button) const noexcept;
    [[nodiscard]] bool wasMousePressed(int button) const noexcept;
    [[nodiscard]] glm::vec2 mouseDelta() const noexcept { return m_mouseDelta; }
    [[nodiscard]] glm::vec2 mousePosition() const noexcept { return m_lastMousePos; }
    [[nodiscard]] float scrollDelta() const noexcept { return m_scroll; }

    // Call when recapturing the cursor: the first sample after a capture is
    // a position jump, not a look motion, and must not register as a delta.
    void resetMouseDelta() noexcept { m_firstMouse = true; }

private:
    static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void onChar(GLFWwindow* window, unsigned int codepoint);
    static void onMouseButton(GLFWwindow* window, int button, int action, int mods);
    static void onCursorPos(GLFWwindow* window, double x, double y);
    static void onScroll(GLFWwindow* window, double dx, double dy);

    static constexpr int kKeyCount = 349;   // GLFW_KEY_LAST + 1
    static constexpr int kButtonCount = 8;  // GLFW_MOUSE_BUTTON_LAST + 1

    std::array<bool, kKeyCount> m_keyDown{};
    std::array<bool, kKeyCount> m_keyPressed{};
    std::array<bool, kButtonCount> m_buttonDown{};
    std::array<bool, kButtonCount> m_buttonPressed{};
    glm::vec2 m_mouseDelta{0.0f};
    glm::vec2 m_lastMousePos{0.0f};
    std::string m_typed;
    float m_scroll = 0.0f;
    bool m_firstMouse = true;
};

} // namespace cc
