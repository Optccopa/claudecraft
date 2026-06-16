#include "render/Hud.hpp"

#include "render/TextureAtlas.hpp"

#include <glad/glad.h>
#include <stb_easy_font.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace cc {
namespace {

constexpr std::uint32_t kFlagTextured = 1u << 8;
constexpr std::uint32_t kFlagDim = 2u << 8;
constexpr std::uint32_t kFlagColored = 4u << 8;
constexpr std::uint32_t kFlagFont = 8u << 8;
constexpr std::uint32_t kFlagFontShadow = 16u << 8;
constexpr std::uint32_t kFlagSprite = 32u << 8;

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

void Hud::pushQuadUv(float x, float yTop, float w, float h, float u0, float vTop, float u1,
                     float vBot, std::uint32_t data) {
    const Vertex v0{x, yTop, u0, vTop, data};
    const Vertex v1{x + w, yTop, u1, vTop, data};
    const Vertex v2{x + w, yTop - h, u1, vBot, data};
    const Vertex v3{x, yTop - h, u0, vBot, data};
    m_scratch.insert(m_scratch.end(), {v0, v1, v2, v0, v2, v3});
}

void Hud::rect(float x, float y, float w, float h, RectStyle style) {
    pushQuad(x, y, w, h, style == RectStyle::Dim ? kFlagDim : 0u);
}

void Hud::coloredRect(float x, float y, float w, float h, std::uint8_t color) {
    pushQuad(x, y, w, h, kFlagColored | color);
}

void Hud::icon(float x, float y, float size, std::uint8_t tile) {
    pushQuad(x, y, size, size, kFlagTextured | tile);
}

void Hud::setGuiSprites(std::span<const std::filesystem::path> packs) {
    m_hasGuiSprites = false;
    const auto normal = loadPackImage(packs, "gui/sprites/widget/button", false);
    const auto hover = loadPackImage(packs, "gui/sprites/widget/button_highlighted", false);
    const auto disabled = loadPackImage(packs, "gui/sprites/widget/button_disabled", false);
    if (!normal || !hover || !disabled) {
        return;
    }
    const int w = normal->width;
    const int h = normal->height;
    if (w <= 0 || h <= 0 || hover->width != w || hover->height != h || disabled->width != w ||
        disabled->height != h) {
        return;
    }
    // Stack the three states top-to-bottom into one texture so every button
    // state batches in the single HUD draw.
    std::vector<std::uint8_t> stacked(static_cast<std::size_t>(w) * h * 3 * 4);
    const std::array<const PackImage*, 3> states{&*normal, &*hover, &*disabled};
    for (std::size_t s = 0; s < states.size(); ++s) {
        const std::size_t offset = s * static_cast<std::size_t>(w) * h * 4;
        std::memcpy(stacked.data() + offset, states[s]->rgba.data(), states[s]->rgba.size());
    }
    glBindTexture(GL_TEXTURE_2D, m_guiSprites.id());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h * 3, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 stacked.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_buttonSrcW = w;
    m_buttonSrcH = h;
    m_hasGuiSprites = true;
}

void Hud::buttonPatch(float x, float y, float w, float h, int state) {
    if (!m_hasGuiSprites) {
        rect(x, y, w, h, state == 1 ? RectStyle::Bright : RectStyle::Dim);
        return;
    }
    // 9-slice: 3px border in the 200x20 source scales with the sheet resolution.
    const float srcW = static_cast<float>(m_buttonSrcW);
    const float srcH = static_cast<float>(m_buttonSrcH);
    const float texH = srcH * 3.0f;
    const float b = 3.0f * srcW / 200.0f;          // source border in texels
    const float db = 2.0f * b;                     // destination border in pixels (2x)
    const float sy = static_cast<float>(state) * srcH;

    const std::array<float, 4> su{0.0f, b, srcW - b, srcW};                 // source x splits
    const std::array<float, 4> sv{sy, sy + b, sy + srcH - b, sy + srcH};     // source y (top-down)
    const std::array<float, 4> dx{x, x + db, x + w - db, x + w};            // dest x splits
    const std::array<float, 4> dyTop{y + h, y + h - db, y + db, y};          // dest y edges (top-down)

    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            const float dW = dx[static_cast<std::size_t>(col) + 1] - dx[static_cast<std::size_t>(col)];
            const float dH = dyTop[static_cast<std::size_t>(row)] - dyTop[static_cast<std::size_t>(row) + 1];
            if (dW <= 0.0f || dH <= 0.0f) {
                continue;
            }
            pushQuadUv(dx[static_cast<std::size_t>(col)], dyTop[static_cast<std::size_t>(row)], dW, dH,
                       su[static_cast<std::size_t>(col)] / srcW, sv[static_cast<std::size_t>(row)] / texH,
                       su[static_cast<std::size_t>(col) + 1] / srcW, sv[static_cast<std::size_t>(row) + 1] / texH,
                       kFlagSprite);
        }
    }
}

void Hud::text(float x, float yTop, float scale, std::string_view str) {
    if (m_font.valid()) {
        // Bitmap font: one cell quad per glyph, advanced by the glyph's width,
        // with a 1px-scaled down-right drop shadow drawn first (Minecraft style).
        const float cell = static_cast<float>(Font::Glyph) * scale;
        float pen = x;
        for (const char c : str) {
            const auto ch = static_cast<unsigned char>(c);
            const int adv = m_font.advance(ch);
            if (ch != ' ' && adv > 0) {
                const Font::Uv uv = m_font.uv(ch);
                pushQuadUv(pen + scale, yTop - scale, cell, cell, uv.u0, uv.v0, uv.u1, uv.v1,
                           kFlagFontShadow);
                pushQuadUv(pen, yTop, cell, cell, uv.u0, uv.v0, uv.u1, uv.v1, kFlagFont);
            }
            pen += static_cast<float>(adv) * scale;
        }
        return;
    }

    m_textBuf.assign(str);
    // stb_easy_font's "270 bytes per character" is an average over long
    // text; worst-case glyphs run far past it and short labels don't
    // amortize, which silently truncates the last character's quads — so keep
    // the generous bound, but in a member buffer that only grows.
    const std::size_t needed = m_textBuf.size() * 1000 + 64;
    if (m_quadScratch.size() < needed) {
        m_quadScratch.resize(needed);
    }
    const int quadCount =
        stb_easy_font_print(0.0f, 0.0f, m_textBuf.data(), nullptr, m_quadScratch.data(),
                            static_cast<int>(m_quadScratch.size()));

    // The font is authored y-down; flip into our y-up space around yTop.
    const auto* verts = reinterpret_cast<const EasyFontVertex*>(m_quadScratch.data());
    for (int q = 0; q < quadCount; ++q) {
        Vertex out[4];
        for (int i = 0; i < 4; ++i) {
            const EasyFontVertex& v = verts[q * 4 + i];
            out[i] = Vertex{x + v.x * scale, yTop - v.y * scale, 0.0f, 0.0f, 0u};
        }
        m_scratch.insert(m_scratch.end(), {out[0], out[1], out[2], out[0], out[2], out[3]});
    }
}

float Hud::textWidth(std::string_view str, float scale) const {
    if (m_font.valid()) {
        return static_cast<float>(m_font.measure(str)) * scale;
    }
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

void Hud::itemSlot(float x, float y, float size, const ItemStack& stack, bool showCount) {
    rect(x, y, size, size, RectStyle::Dim);
    if (stack.empty()) {
        return;
    }
    const float inset = size * 0.125f;
    icon(x + inset, y + inset, size - 2.0f * inset, blockInfo(stack.type).sideTile);
    if (showCount && stack.count > 1) {
        const std::string count = std::to_string(stack.count);
        const float scale = 1.6f;
        text(x + size - textWidth(count, scale) - 4.0f, y + 6.0f + 8.0f * scale, scale, count);
    }
}

void Hud::hotbar(const glm::ivec2& framebufferSize, int selectedSlot,
                 std::span<const ItemStack> slots, bool showCounts) {
    const auto width = static_cast<float>(framebufferSize.x);
    const auto slotCount = static_cast<float>(slots.size());
    const float barWidth = slotCount * kSlotSize + (slotCount - 1.0f) * kSlotGap;
    float slotX = width * 0.5f - barWidth * 0.5f;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (static_cast<int>(i) == selectedSlot) {
            rect(slotX - 3.0f, kHotbarBottom - 3.0f, kSlotSize + 6.0f, kSlotSize + 6.0f,
                 RectStyle::Bright);
        }
        itemSlot(slotX, kHotbarBottom, kSlotSize, slots[i], showCounts);
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
    m_shader.setInt("uFont", 1);
    m_shader.setInt("uSprite", 2);
    atlas.bind(0);
    if (m_font.valid()) {
        m_font.bind(1);
    }
    if (m_hasGuiSprites) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_guiSprites.id());
    }

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
