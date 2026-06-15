#include "render/PostProcess.hpp"

#include <glad/glad.h>

namespace cc {

PostProcess::PostProcess()
    : m_shader{gl::ShaderProgram::fromFiles("shaders/post.vert", "shaders/post.frag")} {}

void PostProcess::resize(const glm::ivec2& fbSize) {
    m_size = fbSize;

    glBindTexture(GL_TEXTURE_2D, m_color.id());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, fbSize.x, fbSize.y, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindRenderbuffer(GL_RENDERBUFFER, m_depth.id());
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, fbSize.x, fbSize.y);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo.id());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color.id(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth.id());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcess::begin(const glm::ivec2& fbSize) {
    if (fbSize != m_size) {
        resize(fbSize);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo.id());
}

void PostProcess::composite(float time) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    m_shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_color.id());
    m_shader.setInt("uScene", 0);
    m_shader.setFloat("uTime", time);
    m_emptyVao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

} // namespace cc
