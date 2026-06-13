#pragma once

#include "gl/GlObjects.hpp"
#include "gl/Shader.hpp"
#include "render/TextureAtlas.hpp"
#include "world/ChunkCoord.hpp"
#include "world/ChunkMesher.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

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
    };

    void render(const FrameParams& params);

    struct DropDraw {
        glm::vec3 position;  // cube center
        BlockType type;
        float light;         // 0..1, sampled from the drop's cell
    };
    // Call after render(): reuses the frame's chunk-shader state.
    void drawDrops(std::span<const DropDraw> drops);

    // Debug wireframe: the cell grid on the four walls of one chunk column,
    // drawn depth-test-off so it reads through terrain.
    void drawChunkBorders(const glm::mat4& viewProj, ChunkCoord chunk);

    // Rebuilds the atlas from an ordered pack stack (highest priority first);
    // an empty span restores the built-in/procedural atlas. Main thread only.
    void setResourcePacks(std::span<const std::filesystem::path> packs) {
        m_atlas = TextureAtlas::create(packs);
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
    void detectGpu();

    gl::ShaderProgram m_chunkShader;
    gl::ShaderProgram m_lineShader;
    TextureAtlas m_atlas;
    std::unordered_map<ChunkCoord, ChunkMeshes, ChunkCoordHash> m_chunks;
    std::unordered_map<BlockType, GpuMesh> m_dropMeshes; // built lazily per type

    // Per-frame visible set; kept as a member so its capacity persists and the
    // render loop does no heap allocation after the first frame.
    std::vector<VisibleChunk> m_visible;

    gl::VertexArray m_highlightVao;
    gl::Buffer m_highlightVbo;
    gl::VertexArray m_chunkBorderVao;
    gl::Buffer m_chunkBorderVbo;

    enum class VramSource { None, Nvx, Ati };
    const char* m_gpuName = "unknown";
    VramSource m_vramSource = VramSource::None;
    int m_vramTotalKB = -1;

    std::size_t m_drawnLastFrame = 0;
};

} // namespace cc
