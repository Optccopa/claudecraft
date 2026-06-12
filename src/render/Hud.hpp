#pragma once

#include "gl/GlObjects.hpp"
#include "gl/Shader.hpp"
#include "world/Block.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace cc {

class TextureAtlas;

// Immediate-mode overlay: crosshair plus hotbar, rebuilt into one dynamic
// vertex buffer each frame and drawn in a single call.
class Hud {
public:
    Hud();

    void render(const glm::ivec2& framebufferSize, const TextureAtlas& atlas, int selectedSlot,
                std::span<const BlockType> hotbar);

private:
    struct Vertex {
        float x, y;
        float u, v;
        std::uint32_t data; // tile | flags << 8 (bit 0: textured, bit 1: dim)
    };

    void pushQuad(float x, float y, float w, float h, std::uint32_t data);

    gl::ShaderProgram m_shader;
    gl::VertexArray m_vao;
    gl::Buffer m_vbo;
    std::vector<Vertex> m_scratch;
};

} // namespace cc
