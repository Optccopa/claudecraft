#include "app/Window.hpp"

#include "core/Log.hpp"

#include <glad/glad.h>

#include <GLFW/glfw3.h>

#include <format>
#include <stdexcept>

namespace cc {
namespace {

void onGlfwError(int code, const char* description) {
    logError(std::format("GLFW error {}: {}", code, description));
}

} // namespace

Window::Window(int width, int height, const char* title, bool debugContext) {
    glfwSetErrorCallback(onGlfwError);
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("glfwInit failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, debugContext ? GLFW_TRUE : GLFW_FALSE);

    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (m_window == nullptr) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed (OpenGL 3.3 required)");
    }

    glfwMakeContextCurrent(m_window);
    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
        throw std::runtime_error("gladLoadGLLoader failed");
    }
    glfwSwapInterval(0); // uncapped; the loop runs a fixed-step simulation

    // Starts with a visible cursor (main menu); raw motion applies whenever
    // the cursor is later captured for gameplay.
    if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    logInfo(std::format("OpenGL {}", reinterpret_cast<const char*>(glGetString(GL_VERSION))));
}

Window::~Window() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::shouldClose() const noexcept {
    return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

glm::ivec2 Window::framebufferSize() const noexcept {
    glm::ivec2 size{};
    glfwGetFramebufferSize(m_window, &size.x, &size.y);
    return size;
}

void Window::swapBuffers() noexcept {
    glfwSwapBuffers(m_window);
}

void Window::setTitle(const std::string& title) noexcept {
    glfwSetWindowTitle(m_window, title.c_str());
}

glm::ivec2 Window::size() const noexcept {
    glm::ivec2 size{};
    glfwGetWindowSize(m_window, &size.x, &size.y);
    return size;
}

void Window::setCursorCaptured(bool captured) noexcept {
    glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

} // namespace cc
