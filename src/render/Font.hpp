#pragma once

#include "gl/GlObjects.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace cc {

// Minecraft's legacy bitmap font (textures/font/ascii.png): a 16x16 grid of
// 8x8 glyph cells covering code points 0..255. Per-glyph advance widths are
// derived by scanning each cell, matching how the game measures the font.
// Without a pack supplying it the font is invalid and the HUD falls back to its
// vector font.
class Font {
public:
    static constexpr int Glyph = 8;   // cell size in source pixels
    static constexpr int Columns = 16;

    // Default-constructs an invalid font (HUD fallback); use create() to load.
    Font() = default;
    [[nodiscard]] static Font create(std::span<const std::filesystem::path> packs);

    [[nodiscard]] bool valid() const noexcept { return m_valid; }
    void bind(unsigned unit) const noexcept;

    // Pen advance for a character, in source pixels (glyph width + 1 spacing).
    [[nodiscard]] int advance(unsigned char ch) const noexcept { return m_advance[ch]; }
    // Total advance of a string in source pixels.
    [[nodiscard]] int measure(std::string_view text) const noexcept;

    // UV rect (u0, v0, u1, v1) of a glyph cell in the 0..1 texture.
    struct Uv {
        float u0, v0, u1, v1;
    };
    [[nodiscard]] Uv uv(unsigned char ch) const noexcept;

private:
    gl::Texture2D m_texture;
    std::array<std::uint8_t, 256> m_advance{};
    bool m_valid = false;
};

} // namespace cc
