#include "render/TextureAtlas.hpp"

#include "core/Log.hpp"
#include "core/ZipArchive.hpp"

#include <glad/glad.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cc {
namespace {

constexpr int kTilePixels = 16;
constexpr int kAtlasPixels = kTilePixels * TextureAtlas::TilesPerRow;

struct Rgba {
    std::uint8_t r, g, b, a;
};

// Deterministic per-pixel hash for texture speckle — independent of any seed
// so visuals stay identical between runs.
[[nodiscard]] float pixelNoise(int x, int y, int salt) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                      static_cast<std::uint32_t>(y) * 668265263u +
                      static_cast<std::uint32_t>(salt) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

[[nodiscard]] Rgba shade(int r, int g, int b, float factor) noexcept {
    return Rgba{static_cast<std::uint8_t>(static_cast<float>(r) * factor),
                static_cast<std::uint8_t>(static_cast<float>(g) * factor),
                static_cast<std::uint8_t>(static_cast<float>(b) * factor), 255};
}

// px, py are tile-local with py measured from the tile's bottom row,
// matching GL's bottom-left texture origin.
[[nodiscard]] Rgba tilePixel(int tile, int px, int py) noexcept {
    const float n = pixelNoise(px, py, tile);
    const float speckle = 1.0f - 0.12f * n;
    switch (tile) {
    case 0: // stone
        return shade(128, 128, 128, 1.0f - 0.2f * n);
    case 1: // dirt
        return shade(134, 96, 67, speckle);
    case 2: // grass top
        return shade(106, 170, 70, 1.0f - 0.18f * n);
    case 3: // grass side: dirt with a grass lip on top
        if (py >= kTilePixels - 3 - static_cast<int>(n * 2.0f)) {
            return shade(106, 170, 70, speckle);
        }
        return shade(134, 96, 67, speckle);
    case 4: // sand
        return shade(218, 206, 160, speckle);
    case 5: // water with faint horizontal banding
        return shade(50, 95, 190, 1.0f - 0.1f * static_cast<float>((py / 4) % 2) - 0.08f * n);
    case 6: // bark: vertical stripes
        return shade(102, 81, 50, ((px % 4) < 2 ? 0.85f : 1.0f) - 0.1f * n);
    case 7: // wood rings: concentric squares
    {
        const int dist = std::max(std::abs(px - 8), std::abs(py - 8));
        return shade(168, 137, 88, (dist % 2 == 0 ? 1.0f : 0.82f) - 0.06f * n);
    }
    case 8: // leaves
        return shade(60, 124, 38, 1.0f - 0.3f * n);
    case 9: // planks: horizontal boards with dark seams
        return shade(160, 130, 78, (py % 4 == 3 ? 0.7f : 1.0f) - 0.08f * n);
    case 10: // snow
        return shade(238, 242, 245, 1.0f - 0.06f * n);
    case 11: // bedrock
        return shade(70, 70, 70, 1.0f - 0.35f * n);
    case 16: // glowstone: warm base with bright nuggets
    {
        const float blob = pixelNoise(px / 2, py / 2, 511);
        if (blob > 0.55f) {
            return shade(252, 234, 150, 1.0f - 0.08f * n);
        }
        return shade(214, 168, 92, 1.0f - 0.12f * n);
    }
    case 17: // cherry bark: dark red-brown stripes
        return shade(94, 56, 56, ((px % 4) < 2 ? 0.85f : 1.0f) - 0.1f * n);
    case 18: // cherry leaves: blossom pink
        return shade(238, 162, 192, 1.0f - 0.22f * n);
    case 19: // spruce bark: dark grey-brown stripes
        return shade(72, 56, 40, ((px % 4) < 2 ? 0.82f : 1.0f) - 0.1f * n);
    case 20: // spruce needles: dark bluish green
        return shade(38, 84, 56, 1.0f - 0.28f * n);
    case 12: // coal ore
    case 13: // iron ore
    case 14: // gold ore
    case 15: // diamond ore
    {
        // Stone base with 2x2 mineral blotches; sampling the noise at half
        // resolution clusters single pixels into readable nuggets.
        const float blob = pixelNoise(px / 2, py / 2, tile * 31);
        if (blob > 0.78f) {
            switch (tile) {
            case 12: return shade(45, 45, 48, 1.0f - 0.15f * n);
            case 13: return shade(216, 168, 138, 1.0f - 0.15f * n);
            case 14: return shade(250, 214, 76, 1.0f - 0.15f * n);
            default: return shade(108, 228, 222, 1.0f - 0.15f * n);
            }
        }
        return shade(128, 128, 128, 1.0f - 0.2f * n);
    }
    default:
        return Rgba{255, 0, 255, 255}; // unassigned tiles scream magenta
    }
}

[[nodiscard]] std::vector<Rgba> buildProceduralAtlas() {
    std::vector<Rgba> pixels(static_cast<std::size_t>(kAtlasPixels) * kAtlasPixels);
    for (int tile = 0; tile < TextureAtlas::TilesPerRow * TextureAtlas::TilesPerRow; ++tile) {
        const int originX = (tile % TextureAtlas::TilesPerRow) * kTilePixels;
        const int originY = (tile / TextureAtlas::TilesPerRow) * kTilePixels;
        for (int py = 0; py < kTilePixels; ++py) {
            for (int px = 0; px < kTilePixels; ++px) {
                pixels[static_cast<std::size_t>((originY + py) * kAtlasPixels + originX + px)] =
                    tilePixel(tile, px, py);
            }
        }
    }
    return pixels;
}

void uploadTexture(GLuint id, const void* data, int size) {
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

constexpr Rgba kMissing{255, 0, 255, 255}; // unresolved pack texture screams magenta

// Multiply tints for the textures Minecraft ships greyscale and colours at
// runtime from the biome colormap. We use fixed plains/evergreen colours;
// per-biome tinting would need colormap sampling the meshing path can't feed.
enum class Tint { None, Grass, Foliage, Spruce, Water };

[[nodiscard]] Rgba applyTint(Rgba c, Tint t) noexcept {
    const auto mul = [](std::uint8_t v, int factor) {
        return static_cast<std::uint8_t>(v * factor / 255);
    };
    switch (t) {
    case Tint::Grass:   return {mul(c.r, 145), mul(c.g, 189), mul(c.b, 89), c.a};
    case Tint::Foliage: return {mul(c.r, 119), mul(c.g, 171), mul(c.b, 47), c.a};
    case Tint::Spruce:  return {mul(c.r, 97), mul(c.g, 153), mul(c.b, 97), c.a};
    case Tint::Water:   return {mul(c.r, 63), mul(c.g, 118), mul(c.b, 228), c.a};
    case Tint::None:    break;
    }
    return c;
}

// Atlas tile -> the resource-pack texture stem that fills it. Block ids (the
// stems for full-cube faces) match Block.cpp's names; the rest are vanilla
// face/top texture names.
struct TileTex {
    int tile;
    const char* name;
    Tint tint;
};
constexpr std::array<TileTex, 21> kTileTextures{{
    {0, "stone", Tint::None},          {1, "dirt", Tint::None},
    {2, "grass_block_top", Tint::Grass}, {3, "grass_block_side", Tint::None},
    {4, "sand", Tint::None},           {5, "water_still", Tint::Water},
    {6, "oak_log", Tint::None},        {7, "oak_log_top", Tint::None},
    {8, "oak_leaves", Tint::Foliage},  {9, "oak_planks", Tint::None},
    {10, "snow", Tint::None},          {11, "bedrock", Tint::None},
    {12, "coal_ore", Tint::None},      {13, "iron_ore", Tint::None},
    {14, "gold_ore", Tint::None},      {15, "diamond_ore", Tint::None},
    {16, "glowstone", Tint::None},     {17, "cherry_log", Tint::None},
    {18, "cherry_leaves", Tint::None}, {19, "spruce_log", Tint::None},
    {20, "spruce_leaves", Tint::Spruce},
}};

// A resource pack, either a .zip or an extracted directory. Resolves block
// textures under both the modern (block/) and legacy (blocks/) folder names.
class ResourcePack {
public:
    [[nodiscard]] static std::optional<ResourcePack> open(const std::filesystem::path& path) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            return ResourcePack{path, path.filename().string()};
        }
        if (auto zip = ZipArchive::open(path)) {
            return ResourcePack{std::move(*zip), path.filename().string()};
        }
        return std::nullopt;
    }

    [[nodiscard]] const std::string& label() const noexcept { return m_label; }

    // Raw PNG bytes for a block texture stem, or nullopt if the pack lacks it.
    [[nodiscard]] std::optional<std::vector<unsigned char>> texture(const char* stem) const {
        for (const char* folder : {"block", "blocks"}) {
            const std::string rel =
                std::format("assets/minecraft/textures/{}/{}.png", folder, stem);
            if (m_zip) {
                if (auto bytes = m_zip->read(rel)) {
                    return bytes;
                }
            } else if (auto bytes = readDirFile(m_dir / rel)) {
                return bytes;
            }
        }
        return std::nullopt;
    }

private:
    ResourcePack(ZipArchive zip, std::string label)
        : m_zip{std::move(zip)}, m_label{std::move(label)} {}
    ResourcePack(std::filesystem::path dir, std::string label)
        : m_dir{std::move(dir)}, m_label{std::move(label)} {}

    [[nodiscard]] static std::optional<std::vector<unsigned char>> readDirFile(
        const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return std::nullopt;
        }
        const std::streamoff size = file.tellg();
        std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        return bytes;
    }

    std::optional<ZipArchive> m_zip;
    std::filesystem::path m_dir;
    std::string m_label;
};

// Decode PNG bytes and nearest-sample the first frame into a 16x16 tile.
// Source rows are top-down (no flip); the atlas is bottom-origin, so the
// vertical sample is mirrored. Animated textures stack frames vertically —
// taking a square frame off the top yields frame 0. Tint is applied per texel.
[[nodiscard]] std::optional<std::array<Rgba, kTilePixels * kTilePixels>> decodeTile(
    std::span<const unsigned char> png, Tint tint) {
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data{
        stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &channels, 4),
        &stbi_image_free};
    if (data == nullptr || w <= 0 || h <= 0) {
        return std::nullopt;
    }
    const int frame = std::min(w, h);
    std::array<Rgba, kTilePixels * kTilePixels> tile{};
    for (int ay = 0; ay < kTilePixels; ++ay) {
        for (int ax = 0; ax < kTilePixels; ++ax) {
            const int sx = ax * frame / kTilePixels;
            const int sy = (kTilePixels - 1 - ay) * frame / kTilePixels;
            const stbi_uc* p = data.get() + (static_cast<std::size_t>(sy) * w + sx) * 4;
            tile[static_cast<std::size_t>(ay) * kTilePixels + ax] =
                applyTint(Rgba{p[0], p[1], p[2], p[3]}, tint);
        }
    }
    return tile;
}

void blitTile(std::vector<Rgba>& atlas, int tile,
              const std::array<Rgba, kTilePixels * kTilePixels>& src) {
    const int originX = (tile % TextureAtlas::TilesPerRow) * kTilePixels;
    const int originY = (tile / TextureAtlas::TilesPerRow) * kTilePixels;
    for (int py = 0; py < kTilePixels; ++py) {
        for (int px = 0; px < kTilePixels; ++px) {
            atlas[static_cast<std::size_t>((originY + py) * kAtlasPixels + originX + px)] =
                src[static_cast<std::size_t>(py) * kTilePixels + px];
        }
    }
}

// Alpha-composite a (tinted) overlay tile over whatever already occupies the
// atlas slot — Minecraft draws the grass side as dirt plus a tinted overlay.
void compositeTile(std::vector<Rgba>& atlas, int tile,
                   const std::array<Rgba, kTilePixels * kTilePixels>& overlay) {
    const int originX = (tile % TextureAtlas::TilesPerRow) * kTilePixels;
    const int originY = (tile / TextureAtlas::TilesPerRow) * kTilePixels;
    for (int py = 0; py < kTilePixels; ++py) {
        for (int px = 0; px < kTilePixels; ++px) {
            const Rgba o = overlay[static_cast<std::size_t>(py) * kTilePixels + px];
            Rgba& dst = atlas[static_cast<std::size_t>((originY + py) * kAtlasPixels + originX + px)];
            const int a = o.a;
            dst.r = static_cast<std::uint8_t>((o.r * a + dst.r * (255 - a)) / 255);
            dst.g = static_cast<std::uint8_t>((o.g * a + dst.g * (255 - a)) / 255);
            dst.b = static_cast<std::uint8_t>((o.b * a + dst.b * (255 - a)) / 255);
        }
    }
}

// First decodable texture for `name` across the pack stack (highest first),
// or nullopt if no enabled pack supplies it.
[[nodiscard]] std::optional<std::array<Rgba, kTilePixels * kTilePixels>> resolveTile(
    std::span<const ResourcePack> packs, const char* name, Tint tint) {
    for (const ResourcePack& pack : packs) {
        if (const auto png = pack.texture(name)) {
            if (auto tile = decodeTile(*png, tint)) {
                return tile;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<Rgba> buildLayeredAtlas(std::span<const ResourcePack> packs) {
    std::vector<Rgba> pixels(static_cast<std::size_t>(kAtlasPixels) * kAtlasPixels, kMissing);
    int loaded = 0;
    int missing = 0;
    for (const TileTex& t : kTileTextures) {
        if (const auto tile = resolveTile(packs, t.name, t.tint)) {
            blitTile(pixels, t.tile, *tile);
            ++loaded;
        } else {
            ++missing; // tile keeps its magenta fill
        }
    }
    // Grass side overlay: tinted grass on top of the dirt-ish side texture.
    if (const auto overlay = resolveTile(packs, "grass_block_side_overlay", Tint::Grass)) {
        compositeTile(pixels, 3, *overlay);
    }
    logInfo(std::format("texture atlas: {} packs, {} tiles loaded, {} missing (magenta)",
                        packs.size(), loaded, missing));
    return pixels;
}

} // namespace

namespace resourcepacks {

std::vector<std::string> available(const std::filesystem::path& root) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".zip") {
            names.push_back(entry.path().filename().string());
        } else if (entry.is_directory() &&
                   std::filesystem::exists(entry.path() / "assets" / "minecraft" / "textures" /
                                           "block")) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::filesystem::path pathFor(const std::filesystem::path& root, std::string_view name) {
    return root / name;
}

} // namespace resourcepacks

TextureAtlas TextureAtlas::create(std::span<const std::filesystem::path> packs) {
    gl::Texture2D texture;

    if (!packs.empty()) {
        std::vector<ResourcePack> opened;
        opened.reserve(packs.size());
        for (const std::filesystem::path& path : packs) {
            if (auto pack = ResourcePack::open(path)) {
                opened.push_back(std::move(*pack));
            } else {
                logError(std::format("resource pack '{}' is unreadable, skipping", path.string()));
            }
        }
        if (!opened.empty()) {
            const auto pixels = buildLayeredAtlas(opened);
            uploadTexture(texture.id(), pixels.data(), kAtlasPixels);
            return TextureAtlas{std::move(texture)};
        }
    }

    const char* path = "textures/atlas.png";
    if (std::filesystem::exists(path)) {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(1); // file rows are top-down; GL wants bottom-up
        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data{
            stbi_load(path, &width, &height, &channels, 4), &stbi_image_free};
        if (data != nullptr && width == height && width % TilesPerRow == 0) {
            uploadTexture(texture.id(), data.get(), width);
            logInfo(std::format("loaded texture atlas '{}' ({}x{})", path, width, height));
            return TextureAtlas{std::move(texture)};
        }
        logError(std::format("ignoring '{}': {}", path,
                             data == nullptr ? stbi_failure_reason() : "must be square, side % 8 == 0"));
    }

    const auto pixels = buildProceduralAtlas();
    uploadTexture(texture.id(), pixels.data(), kAtlasPixels);
    logInfo("using procedural texture atlas");
    return TextureAtlas{std::move(texture)};
}

void TextureAtlas::bind(unsigned unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_texture.id());
}

} // namespace cc
