#pragma once

#include "gl/GlObjects.hpp"

namespace cc {

// 8x8 tile atlas, 16px tiles, nearest-filtered with no mips (prevents tile
// bleed with the fract()-tiled UVs greedy meshing produces). Loads
// textures/atlas.png when present, otherwise paints a deterministic
// procedural fallback so the game runs without binary assets.
class TextureAtlas {
public:
    static constexpr int TilesPerRow = 8;

    [[nodiscard]] static TextureAtlas create();

    void bind(unsigned unit) const noexcept;

private:
    explicit TextureAtlas(gl::Texture2D texture) noexcept : m_texture{std::move(texture)} {}

    gl::Texture2D m_texture;
};

} // namespace cc
