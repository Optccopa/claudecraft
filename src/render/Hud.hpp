#pragma once

#include "gl/GlObjects.hpp"
#include "gl/Shader.hpp"
#include "world/Item.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
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

    void beginFrame() noexcept { m_scratch.clear(); }

    void rect(float x, float y, float w, float h, RectStyle style);
    void icon(float x, float y, float size, std::uint8_t tile);
    // yTop is the top edge of the text block (text grows downward).
    void text(float x, float yTop, float scale, std::string_view str);
    [[nodiscard]] static float textWidth(std::string_view str, float scale);

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
        std::uint32_t data; // tile | flags << 8 (bit 0: textured, bit 1: dim)
    };

    void pushQuad(float x, float y, float w, float h, std::uint32_t data);

    gl::ShaderProgram m_shader;
    gl::VertexArray m_vao;
    gl::Buffer m_vbo;
    std::vector<Vertex> m_scratch;
    // Reused across text() calls so each label isn't a fresh malloc+free.
    std::string m_textBuf;
    std::vector<char> m_quadScratch;
};

} // namespace cc
