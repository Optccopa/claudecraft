#pragma once

#include "gl/GlObjects.hpp"

#include <cstdint>
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

    // HUD icon tiles, filled from the pack's gui/sprites/hud when it has them
    // (hasGuiIcons). The HUD draws these textured, else falls back to flat pips.
    static constexpr std::uint8_t HeartFullTile = 25;
    static constexpr std::uint8_t HeartHalfTile = 26;
    static constexpr std::uint8_t HeartBgTile = 27;
    static constexpr std::uint8_t FoodFullTile = 28;
    static constexpr std::uint8_t FoodHalfTile = 29;
    static constexpr std::uint8_t FoodBgTile = 30;
    static constexpr std::uint8_t AirTile = 31;
    // destroy_stage_0..9 occupy tiles 33..42 (block-breaking overlay).
    static constexpr std::uint8_t DestroyStage0Tile = 33;
    static constexpr int DestroyStageCount = 10;

    [[nodiscard]] static TextureAtlas create(std::span<const std::filesystem::path> packs = {});

    void bind(unsigned unit) const noexcept;
    [[nodiscard]] bool hasGuiIcons() const noexcept { return m_hasGuiIcons; }

private:
    TextureAtlas(gl::Texture2D texture, bool hasGuiIcons) noexcept
        : m_texture{std::move(texture)}, m_hasGuiIcons{hasGuiIcons} {}

    gl::Texture2D m_texture;
    bool m_hasGuiIcons = false;
};

namespace resourcepacks {

// Names (zip filename or folder name) of every readable pack under `root`,
// sorted. Names resolve back to a path with pathFor.
[[nodiscard]] std::vector<std::string> available(const std::filesystem::path& root);
[[nodiscard]] std::filesystem::path pathFor(const std::filesystem::path& root,
                                            std::string_view name);

} // namespace resourcepacks
} // namespace cc
