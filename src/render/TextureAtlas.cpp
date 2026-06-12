#include "render/TextureAtlas.hpp"

#include "core/Log.hpp"

#include <glad/glad.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
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

} // namespace

TextureAtlas TextureAtlas::create() {
    gl::Texture2D texture;

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
                             data == nullptr ? stbi_failure_reason() : "must be square, side % 4 == 0"));
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
