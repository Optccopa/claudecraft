#include "render/Hud.hpp"

#include "render/TextureAtlas.hpp"

#include <glm/vec2.hpp>

namespace cc {
namespace {

constexpr std::uint32_t kFlagTextured = 1u << 8;
constexpr std::uint32_t kFlagDim = 2u << 8;

constexpr float kSlotSize = 48.0f;
constexpr float kSlotGap = 6.0f;
constexpr float kIconInset = 6.0f;
constexpr float kHotbarBottom = 16.0f;
constexpr float kCrosshairLength = 18.0f;
constexpr float kCrosshairThickness = 2.0f;

} // namespace

Hud::Hud()
    : m_shader{gl::ShaderProgram::fromFiles("shaders/hud.vert", "shaders/hud.frag")} {
    m_vao.bind();
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo.id());
    constexpr GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, stride,
                           reinterpret_cast<const void*>(offsetof(Vertex, data)));
    glBindVertexArray(0);
}

void Hud::pushQuad(float x, float y, float w, float h, std::uint32_t data) {
    const Vertex v0{x, y, 0.0f, 0.0f, data};
    const Vertex v1{x + w, y, 1.0f, 0.0f, data};
    const Vertex v2{x + w, y + h, 1.0f, 1.0f, data};
    const Vertex v3{x, y + h, 0.0f, 1.0f, data};
    m_scratch.insert(m_scratch.end(), {v0, v1, v2, v0, v2, v3});
}

void Hud::render(const glm::ivec2& framebufferSize, const TextureAtlas& atlas, int selectedSlot,
                 std::span<const BlockType> hotbar) {
    m_scratch.clear();

    const auto width = static_cast<float>(framebufferSize.x);
    const auto height = static_cast<float>(framebufferSize.y);

    pushQuad(width * 0.5f - kCrosshairLength * 0.5f, height * 0.5f - kCrosshairThickness * 0.5f,
             kCrosshairLength, kCrosshairThickness, 0);
    pushQuad(width * 0.5f - kCrosshairThickness * 0.5f, height * 0.5f - kCrosshairLength * 0.5f,
             kCrosshairThickness, kCrosshairLength, 0);

    const auto slotCount = static_cast<float>(hotbar.size());
    const float barWidth = slotCount * kSlotSize + (slotCount - 1.0f) * kSlotGap;
    float slotX = width * 0.5f - barWidth * 0.5f;
    for (std::size_t i = 0; i < hotbar.size(); ++i) {
        if (static_cast<int>(i) == selectedSlot) {
            pushQuad(slotX - 3.0f, kHotbarBottom - 3.0f, kSlotSize + 6.0f, kSlotSize + 6.0f, 0);
        }
        pushQuad(slotX, kHotbarBottom, kSlotSize, kSlotSize, kFlagDim);
        pushQuad(slotX + kIconInset, kHotbarBottom + kIconInset, kSlotSize - 2.0f * kIconInset,
                 kSlotSize - 2.0f * kIconInset,
                 kFlagTextured | blockInfo(hotbar[i]).sideTile);
        slotX += kSlotSize + kSlotGap;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.use();
    m_shader.setVec2("uScreenSize", glm::vec2{width, height});
    m_shader.setInt("uAtlas", 0);
    atlas.bind(0);

    m_vao.bind();
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo.id());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_scratch.size() * sizeof(Vertex)),
                 m_scratch.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_scratch.size()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

} // namespace cc
