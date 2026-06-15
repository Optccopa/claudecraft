#pragma once

#include <glad/glad.h>

#include <utility>

namespace cc::gl {

// Move-only RAII handles for GL objects. Construction requires a current GL
// context; destruction with id 0 is a silent no-op (moved-from state).
class Buffer {
public:
    Buffer() { glGenBuffers(1, &m_id); }
    ~Buffer() { glDeleteBuffers(1, &m_id); }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept : m_id{std::exchange(other.m_id, 0)} {}
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            glDeleteBuffers(1, &m_id);
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    [[nodiscard]] GLuint id() const noexcept { return m_id; }

private:
    GLuint m_id = 0;
};

class VertexArray {
public:
    VertexArray() { glGenVertexArrays(1, &m_id); }
    ~VertexArray() { glDeleteVertexArrays(1, &m_id); }

    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    VertexArray(VertexArray&& other) noexcept : m_id{std::exchange(other.m_id, 0)} {}
    VertexArray& operator=(VertexArray&& other) noexcept {
        if (this != &other) {
            glDeleteVertexArrays(1, &m_id);
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    [[nodiscard]] GLuint id() const noexcept { return m_id; }
    void bind() const noexcept { glBindVertexArray(m_id); }

private:
    GLuint m_id = 0;
};

class Texture2D {
public:
    Texture2D() { glGenTextures(1, &m_id); }
    ~Texture2D() { glDeleteTextures(1, &m_id); }

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept : m_id{std::exchange(other.m_id, 0)} {}
    Texture2D& operator=(Texture2D&& other) noexcept {
        if (this != &other) {
            glDeleteTextures(1, &m_id);
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    [[nodiscard]] GLuint id() const noexcept { return m_id; }

private:
    GLuint m_id = 0;
};

class Renderbuffer {
public:
    Renderbuffer() { glGenRenderbuffers(1, &m_id); }
    ~Renderbuffer() { glDeleteRenderbuffers(1, &m_id); }

    Renderbuffer(const Renderbuffer&) = delete;
    Renderbuffer& operator=(const Renderbuffer&) = delete;
    Renderbuffer(Renderbuffer&& other) noexcept : m_id{std::exchange(other.m_id, 0)} {}
    Renderbuffer& operator=(Renderbuffer&& other) noexcept {
        if (this != &other) {
            glDeleteRenderbuffers(1, &m_id);
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    [[nodiscard]] GLuint id() const noexcept { return m_id; }

private:
    GLuint m_id = 0;
};

class Framebuffer {
public:
    Framebuffer() { glGenFramebuffers(1, &m_id); }
    ~Framebuffer() { glDeleteFramebuffers(1, &m_id); }

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept : m_id{std::exchange(other.m_id, 0)} {}
    Framebuffer& operator=(Framebuffer&& other) noexcept {
        if (this != &other) {
            glDeleteFramebuffers(1, &m_id);
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    [[nodiscard]] GLuint id() const noexcept { return m_id; }

private:
    GLuint m_id = 0;
};

} // namespace cc::gl
