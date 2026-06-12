#pragma once

#include <glm/vec2.hpp>

#include <string>

struct GLFWwindow;

namespace cc {

// Owns GLFW init/terminate, the window, the GL context and the GLAD load.
// Exactly one instance exists for the program's lifetime, so it is pinned
// (no copies or moves) rather than handle-swappable.
class Window {
public:
    Window(int width, int height, const char* title, bool debugContext);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    [[nodiscard]] GLFWwindow* handle() const noexcept { return m_window; }
    [[nodiscard]] bool shouldClose() const noexcept;
    [[nodiscard]] glm::ivec2 framebufferSize() const noexcept;
    [[nodiscard]] glm::ivec2 size() const noexcept;

    void swapBuffers() noexcept;
    void setTitle(const std::string& title) noexcept;
    void setCursorCaptured(bool captured) noexcept;

private:
    GLFWwindow* m_window = nullptr;
};

} // namespace cc
