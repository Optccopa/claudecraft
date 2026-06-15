#pragma once

#include "gl/GlObjects.hpp"
#include "gl/Shader.hpp"
#include "render/TextureAtlas.hpp"
#include "world/ChunkCoord.hpp"
#include "world/ChunkMesher.hpp"
#include "world/Mob.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace cc {

// Owns all chunk GPU state and the world-pass shaders. The world hands over
// finished CPU meshes; everything GL stays on this side (main thread only).
class Renderer {
public:
    Renderer();

    void uploadChunkMesh(ChunkCoord coord, ChunkMesher::Result&& mesh);
    void removeChunkMesh(ChunkCoord coord) noexcept;
    void clearChunkMeshes() noexcept { m_chunks.clear(); }

    struct FrameParams {
        glm::mat4 viewProj;
        glm::vec3 cameraPos;
        glm::vec3 fogColor;
        glm::vec3 sunDirection;
        float fogStart;
        float fogEnd;
        float skyLight; // 0..1 scale on the sky light channel
        std::optional<glm::ivec3> highlightedBlock;
        // Block-local selection box (0..1 per axis); non-cube shapes shrink it
        // so the wireframe hugs the rendered geometry instead of the full cell.
        glm::vec3 highlightMin{0.0f};
        glm::vec3 highlightMax{1.0f};
    };

    void render(const FrameParams& params);

    struct DropDraw {
        glm::vec3 position;  // cube center
        BlockType type;
        float light;         // 0..1, sampled from the drop's cell
    };
    // Call after render(): reuses the frame's chunk-shader state.
    void drawDrops(std::span<const DropDraw> drops);

    struct MobDraw {
        glm::vec3 position; // feet centre
        float yaw;          // degrees; model +z faces this heading
        MobType type;
        float light;        // 0..1, sampled from the mob's cell
        float hurt;         // 0..1 red-tint flash after taking damage
    };
    // Call after render(): uses the entity shader with the frame params cached
    // by the last render() call.
    void drawMobs(std::span<const MobDraw> mobs);

    // Block-breaking crack overlay on the targeted block (stage 0..9). Call
    // after render(); reuses the frame's chunk-shader state.
    void drawBlockBreak(const glm::ivec3& block, int stage);

    // Debug wireframe: the cell grid on the four walls of one chunk column,
    // drawn depth-test-off so it reads through terrain.
    void drawChunkBorders(const glm::mat4& viewProj, ChunkCoord chunk);

    // Rebuilds the atlas from an ordered pack stack (highest priority first);
    // an empty span restores the built-in/procedural atlas. Main thread only.
    void setResourcePacks(std::span<const std::filesystem::path> packs) {
        m_atlas = TextureAtlas::create(packs);
        loadMobTextures(packs);
    }

    [[nodiscard]] const TextureAtlas& atlas() const noexcept { return m_atlas; }
    [[nodiscard]] std::size_t meshCount() const noexcept { return m_chunks.size(); }
    [[nodiscard]] std::size_t drawnLastFrame() const noexcept { return m_drawnLastFrame; }

    struct GpuStats {
        const char* name;  // GL_RENDERER
        int usedVramMB;    // -1 if the driver doesn't report it
        int totalVramMB;   // -1 if unknown
        int freeVramMB;    // -1 if unknown
    };
    // Current VRAM via NVX_gpu_memory_info / ATI_meminfo when present. Queried
    // only when the extension exists so the GL debug callback stays quiet.
    [[nodiscard]] GpuStats gpuStats() const;

private:
    struct GpuMesh {
        gl::VertexArray vao;
        gl::Buffer vbo;
        gl::Buffer ebo;
        GLsizei indexCount = 0;
    };
    struct ChunkMeshes {
        GpuMesh opaque;
        GpuMesh water;
    };
    struct VisibleChunk {
        const ChunkMeshes* meshes;
        glm::vec3 origin;
        float distanceSq;
    };

    [[nodiscard]] static GpuMesh makeGpuMesh(const ChunkMeshData& data);
    [[nodiscard]] const GpuMesh& dropMesh(BlockType type);
    [[nodiscard]] const GpuMesh& breakMesh(std::uint8_t tile);
    void detectGpu();
    // Builds the per-type entity meshes (once) and loads their skins from the
    // pack stack. Mobs whose skin a pack supplies render textured; the rest
    // fall back to flat-shaded coloured boxes.
    void loadMobTextures(std::span<const std::filesystem::path> packs);

    gl::ShaderProgram m_chunkShader;
    gl::ShaderProgram m_lineShader;
    gl::ShaderProgram m_entityShader;
    TextureAtlas m_atlas;
    std::unordered_map<ChunkCoord, ChunkMeshes, ChunkCoordHash> m_chunks;
    std::unordered_map<BlockType, GpuMesh> m_dropMeshes; // built lazily per type
    std::unordered_map<std::uint8_t, GpuMesh> m_breakMeshes; // crack cubes, per stage tile

    // Per-frame visible set; kept as a member so its capacity persists and the
    // render loop does no heap allocation after the first frame.
    std::vector<VisibleChunk> m_visible;

    gl::VertexArray m_highlightVao;
    gl::Buffer m_highlightVbo;
    gl::VertexArray m_chunkBorderVao;
    gl::Buffer m_chunkBorderVbo;
    gl::VertexArray m_mobCubeVao; // unit cube (pos+normal+uv) for flat-shaded boxes
    gl::Buffer m_mobCubeVbo;

    // Per-mob-type textured model: one mesh per texture layer plus its skin.
    struct MobMesh {
        gl::VertexArray vao;
        gl::Buffer vbo;
        GLsizei vertexCount = 0;
    };
    struct MobGpu {
        MobMesh base;
        MobMesh overlay;
        gl::Texture2D baseTex;
        gl::Texture2D overlayTex;
        bool hasBase = false;
        bool hasOverlay = false;
    };
    std::array<MobGpu, static_cast<std::size_t>(MobType::Count)> m_mobGpu;

    // Frame params cached by render() so drawMobs (a separate shader) can reuse
    // them without re-plumbing the view/fog/sun through the call site.
    glm::mat4 m_frameViewProj{1.0f};
    glm::vec3 m_frameFogColor{0.0f};
    glm::vec3 m_frameSunDir{0.0f, 1.0f, 0.0f};
    float m_frameFogStart = 0.0f;
    float m_frameFogEnd = 0.0f;

    enum class VramSource { None, Nvx, Ati };
    const char* m_gpuName = "unknown";
    VramSource m_vramSource = VramSource::None;
    int m_vramTotalKB = -1;

    std::size_t m_drawnLastFrame = 0;
};

} // namespace cc
