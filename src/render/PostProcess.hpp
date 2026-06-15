#pragma once

#include "gl/GlObjects.hpp"
#include "gl/Shader.hpp"

#include <glm/vec2.hpp>

namespace cc {

// Offscreen capture for the underwater screen effect. The world renders into a
// colour+depth FBO; composite() warps and blue-tints it onto the screen. Only
// engaged while the camera is submerged — otherwise the world draws straight to
// the default framebuffer and this stays idle.
class PostProcess {
public:
    PostProcess();

    // Bind the scene FBO (recreating its targets if the framebuffer resized).
    // The caller renders the world as usual; its clear hits the FBO.
    void begin(const glm::ivec2& fbSize);
    // Resolve the captured scene to the default framebuffer with the warp/tint.
    void composite(float time);

private:
    void resize(const glm::ivec2& fbSize);

    gl::Framebuffer m_fbo;
    gl::Texture2D m_color;
    gl::Renderbuffer m_depth;
    gl::VertexArray m_emptyVao; // fullscreen triangle is generated from gl_VertexID
    gl::ShaderProgram m_shader;
    glm::ivec2 m_size{0, 0};
};

} // namespace cc
