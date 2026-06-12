#include "render/Hud.hpp"

#include "render/TextureAtlas.hpp"

#include <stb_easy_font.h>

#include <cstring>
#include <string>

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

// stb_easy_font emits 16-byte vertices: x, y, z floats plus an ignored color.
struct EasyFontVertex {
    float x, y, z;
    std::uint8_t color[4];
};
static_assert(sizeof(EasyFontVertex) == 16);

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

void Hud::rect(float x, float y, float w, float h, RectStyle style) {
    pushQuad(x, y, w, h, style == RectStyle::Dim ? kFlagDim : 0u);
}

void Hud::icon(float x, float y, float size, std::uint8_t tile) {
    pushQuad(x, y, size, size, kFlagTextured | tile);
}

void Hud::text(float x, float yTop, float scale, std::string_view str) {
    std::string buffer{str};
    // ~270 bytes per character per stb_easy_font's own sizing guidance.
    std::vector<char> quads(buffer.size() * 272 + 16);
    const int quadCount =
        stb_easy_font_print(0.0f, 0.0f, buffer.data(), nullptr, quads.data(),
                            static_cast<int>(quads.size()));

    // The font is authored y-down; flip into our y-up space around yTop.
    const auto* verts = reinterpret_cast<const EasyFontVertex*>(quads.data());
    for (int q = 0; q < quadCount; ++q) {
        Vertex out[4];
        for (int i = 0; i < 4; ++i) {
            const EasyFontVertex& v = verts[q * 4 + i];
            out[i] = Vertex{x + v.x * scale, yTop - v.y * scale, 0.0f, 0.0f, 0u};
        }
        m_scratch.insert(m_scratch.end(), {out[0], out[1], out[2], out[0], out[2], out[3]});
    }
}

float Hud::textWidth(std::string_view str, float scale) {
    std::string buffer{str};
    return static_cast<float>(stb_easy_font_width(buffer.data())) * scale;
}

void Hud::crosshair(const glm::ivec2& framebufferSize) {
    const auto width = static_cast<float>(framebufferSize.x);
    const auto height = static_cast<float>(framebufferSize.y);
    pushQuad(width * 0.5f - kCrosshairLength * 0.5f, height * 0.5f - kCrosshairThickness * 0.5f,
             kCrosshairLength, kCrosshairThickness, 0);
    pushQuad(width * 0.5f - kCrosshairThickness * 0.5f, height * 0.5f - kCrosshairLength * 0.5f,
             kCrosshairThickness, kCrosshairLength, 0);
}

void Hud::hotbar(const glm::ivec2& framebufferSize, int selectedSlot,
                 std::span<const BlockType> blocks) {
    const auto width = static_cast<float>(framebufferSize.x);
    const auto slotCount = static_cast<float>(blocks.size());
    const float barWidth = slotCount * kSlotSize + (slotCount - 1.0f) * kSlotGap;
    float slotX = width * 0.5f - barWidth * 0.5f;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (static_cast<int>(i) == selectedSlot) {
            rect(slotX - 3.0f, kHotbarBottom - 3.0f, kSlotSize + 6.0f, kSlotSize + 6.0f,
                 RectStyle::Bright);
        }
        rect(slotX, kHotbarBottom, kSlotSize, kSlotSize, RectStyle::Dim);
        icon(slotX + kIconInset, kHotbarBottom + kIconInset, kSlotSize - 2.0f * kIconInset,
             blockInfo(blocks[i]).sideTile);
        slotX += kSlotSize + kSlotGap;
    }
}

void Hud::flush(const glm::ivec2& framebufferSize, const TextureAtlas& atlas) {
    if (m_scratch.empty()) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    // Text quads flip winding when mapped into y-up space; culling is off for
    // the whole overlay pass rather than special-casing them.
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.use();
    m_shader.setVec2("uScreenSize", glm::vec2{static_cast<float>(framebufferSize.x),
                                              static_cast<float>(framebufferSize.y)});
    m_shader.setInt("uAtlas", 0);
    atlas.bind(0);

    m_vao.bind();
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo.id());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_scratch.size() * sizeof(Vertex)),
                 m_scratch.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_scratch.size()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace cc
