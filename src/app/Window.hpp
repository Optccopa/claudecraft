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
    // Sets the title-bar/taskbar icon from a PNG (RGBA). No-op if unreadable;
    // the exe's own file icon is a separate Win32 resource (see app.rc).
    void setIcon(const char* pngPath) noexcept;
    void setCursorCaptured(bool captured) noexcept;
    void setVsync(bool on) noexcept;
    void setFullscreen(bool fullscreen) noexcept;

private:
    GLFWwindow* m_window = nullptr;
    glm::ivec2 m_windowedPos{100, 100};
    glm::ivec2 m_windowedSize{1600, 900};
    bool m_fullscreen = false;
    bool m_vsync = false;
};

} // namespace cc
