#pragma once

#include "gl/GlObjects.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cc {

// 8x8 tile atlas, 16px tiles, nearest-filtered with no mips (prevents tile
// bleed with the fract()-tiled UVs greedy meshing produces).
//
// Source: an ordered list of Minecraft resource packs (highest priority
// first) — each tile takes the texture from the topmost pack that provides
// it, like Minecraft's pack stack. A tile no enabled pack supplies renders
// magenta. With no packs the atlas falls back to textures/atlas.png, then a
// deterministic procedural atlas so the game runs without binary assets.
class TextureAtlas {
public:
    static constexpr int TilesPerRow = 8;

    [[nodiscard]] static TextureAtlas create(std::span<const std::filesystem::path> packs = {});

    void bind(unsigned unit) const noexcept;

private:
    explicit TextureAtlas(gl::Texture2D texture) noexcept : m_texture{std::move(texture)} {}

    gl::Texture2D m_texture;
};

namespace resourcepacks {

// Names (zip filename or folder name) of every readable pack under `root`,
// sorted. Names resolve back to a path with pathFor.
[[nodiscard]] std::vector<std::string> available(const std::filesystem::path& root);
[[nodiscard]] std::filesystem::path pathFor(const std::filesystem::path& root,
                                            std::string_view name);

} // namespace resourcepacks
} // namespace cc
