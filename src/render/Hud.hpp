#pragma once

#include "gl/GlObjects.hpp"
#include "gl/Shader.hpp"
#include "render/Font.hpp"
#include "world/Item.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cc {

class TextureAtlas;

// Immediate-mode 2D overlay. Callers compose a frame from primitives
// (rect/text/icon, plus the gameplay crosshair/hotbar helpers) between
// beginFrame() and flush(); everything batches into one dynamic vertex
// buffer and a single draw call. Coordinates are pixels, origin bottom-left.
class Hud {
public:
    enum class RectStyle : std::uint8_t { Bright, Dim };

    Hud();

    // Rebuilds the text font from the pack stack (main thread). An empty span or
    // a pack without textures/font/ascii.png leaves the vector-font fallback.
    void setFont(std::span<const std::filesystem::path> packs) { m_font = Font::create(packs); }
    // Loads the widget button sprites (normal/highlighted/disabled) from the
    // pack stack into one texture. Without them, buttons fall back to flat rects.
    void setGuiSprites(std::span<const std::filesystem::path> packs);
    [[nodiscard]] bool hasGuiSprites() const noexcept { return m_hasGuiSprites; }

    // Minecraft 9-slice widget button background. state: 0 normal, 1 hover,
    // 2 disabled. Falls back to a flat rect when the pack lacks the sprites.
    void buttonPatch(float x, float y, float w, float h, int state);

    void beginFrame() noexcept { m_scratch.clear(); }

    void rect(float x, float y, float w, float h, RectStyle style);
    // Flat-coloured quad from a small palette (health/hunger/air pips). The
    // palette lives in the fragment shader; see ColorIndex.
    void coloredRect(float x, float y, float w, float h, std::uint8_t color);
    void icon(float x, float y, float size, std::uint8_t tile);

    enum ColorIndex : std::uint8_t {
        HeartFull = 0,
        HeartEmpty = 1,
        HungerFull = 2,
        HungerEmpty = 3,
        AirBubble = 4,
    };
    // yTop is the top edge of the text block (text grows downward).
    void text(float x, float yTop, float scale, std::string_view str);
    [[nodiscard]] float textWidth(std::string_view str, float scale) const;

    void crosshair(const glm::ivec2& framebufferSize);
    // showCounts: survival draws stack sizes; creative slots are infinite.
    void hotbar(const glm::ivec2& framebufferSize, int selectedSlot,
                std::span<const ItemStack> slots, bool showCounts);
    // One inventory-style slot anywhere on screen (shared by hotbar and the
    // inventory grid): dim background, icon, optional count.
    void itemSlot(float x, float y, float size, const ItemStack& stack, bool showCount);

    void flush(const glm::ivec2& framebufferSize, const TextureAtlas& atlas);

private:
    struct Vertex {
        float x, y;
        float u, v;
        // tile | flags << 8. flags: 1 textured-atlas, 2 dim, 4 colored,
        // 8 font glyph, 16 font shadow.
        std::uint32_t data;
    };

    void pushQuad(float x, float y, float w, float h, std::uint32_t data);
    // Quad with explicit per-corner UVs (font glyphs index arbitrary cells).
    void pushQuadUv(float x, float yTop, float w, float h, float u0, float vTop, float u1,
                    float vBot, std::uint32_t data);

    gl::ShaderProgram m_shader;
    Font m_font;
    gl::Texture2D m_guiSprites;   // stacked button states (normal/hover/disabled)
    bool m_hasGuiSprites = false;
    int m_buttonSrcW = 200;       // source button width/height (per state row)
    int m_buttonSrcH = 20;
    gl::VertexArray m_vao;
    gl::Buffer m_vbo;
    std::vector<Vertex> m_scratch;
    // Reused across text() calls so each label isn't a fresh malloc+free.
    std::string m_textBuf;
    std::vector<char> m_quadScratch;
};

} // namespace cc
