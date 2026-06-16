#include "render/Font.hpp"

#include "render/TextureAtlas.hpp"

#include <glad/glad.h>

#include <algorithm>

namespace cc {

Font Font::create(std::span<const std::filesystem::path> packs) {
    Font font;
    // Source rows are indexed top-down (char 0 in the top-left cell), so the
    // sheet is loaded unflipped and sampled with v growing downward.
    const auto image = loadPackImage(packs, "font/ascii", false);
    if (!image || image->width <= 0 || image->height <= 0 ||
        image->width % Columns != 0 || image->height % Columns != 0) {
        return font; // invalid: HUD keeps its vector-font fallback
    }

    const int cellW = image->width / Columns;
    const int cellH = image->height / Columns;
    const auto alphaAt = [&](int x, int y) {
        return image->rgba[(static_cast<std::size_t>(y) * image->width + x) * 4 + 3];
    };
    // Advance = glyph width (rightmost non-empty column + 1) plus one pixel of
    // spacing, normalised to the 8-pixel logical grid; empty cells advance like
    // a space so layout stays stable across HD fonts.
    for (int ch = 0; ch < 256; ++ch) {
        const int cx = (ch % Columns) * cellW;
        const int cy = (ch / Columns) * cellH;
        int lastCol = -1;
        for (int col = 0; col < cellW; ++col) {
            for (int row = 0; row < cellH; ++row) {
                if (alphaAt(cx + col, cy + row) != 0) {
                    lastCol = col;
                    break;
                }
            }
        }
        int adv;
        if (lastCol < 0) {
            adv = (ch == ' ') ? 4 : 0;
        } else {
            const int logical = (lastCol + 1) * Glyph / cellW; // back to the 8px grid
            adv = std::clamp(logical + 2, 1, Glyph);
        }
        font.m_advance[static_cast<std::size_t>(ch)] = static_cast<std::uint8_t>(adv);
    }

    glBindTexture(GL_TEXTURE_2D, font.m_texture.id());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image->rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    font.m_valid = true;
    return font;
}

void Font::bind(unsigned unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_texture.id());
}

int Font::measure(std::string_view text) const noexcept {
    int total = 0;
    for (const char c : text) {
        total += m_advance[static_cast<unsigned char>(c)];
    }
    return total;
}

Font::Uv Font::uv(unsigned char ch) const noexcept {
    const float col = static_cast<float>(ch % Columns);
    const float row = static_cast<float>(ch / Columns);
    constexpr float step = 1.0f / static_cast<float>(Columns);
    return Uv{col * step, row * step, (col + 1.0f) * step, (row + 1.0f) * step};
}

} // namespace cc
