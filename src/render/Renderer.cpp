#include "render/Renderer.hpp"

#include "core/Log.hpp"
#include "render/Frustum.hpp"
#include "world/Chunk.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <vector>

namespace cc {
namespace {

// VRAM query enums, absent from the core GL headers.
constexpr GLenum kGpuMemTotalNvx = 0x9048;   // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
constexpr GLenum kGpuMemCurrentNvx = 0x9049; // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
constexpr GLenum kTextureFreeMemAti = 0x87FC; // GL_TEXTURE_FREE_MEMORY_ATI

void setupChunkAttributes() {
    constexpr GLsizei stride = sizeof(ChunkVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(ChunkVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, stride,
                           reinterpret_cast<const void*>(offsetof(ChunkVertex, data)));
}

// Unit cube centered on the origin for dropped items, in the chunk vertex
// format: full AO, light baked as block-channel 15 so the shader's sun and
// sky scaling don't apply — uLightScale carries the drop's sampled light.
[[nodiscard]] ChunkMeshData makeDropMeshData(BlockType type) {
    struct Face {
        std::array<glm::vec3, 4> corners;
        std::uint32_t normalIndex;
        std::uint8_t tile;
    };
    const BlockInfo& info = blockInfo(type);
    const float h = 0.5f;
    const std::array<Face, 6> faces{{
        {{glm::vec3{h, -h, -h}, {h, h, -h}, {h, h, h}, {h, -h, h}}, 0, info.sideTile},
        {{glm::vec3{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}, 1, info.sideTile},
        {{glm::vec3{-h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}}, 2, info.topTile},
        {{glm::vec3{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}, 3, info.bottomTile},
        {{glm::vec3{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}, 4, info.sideTile},
        {{glm::vec3{-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {h, -h, -h}}, 5, info.sideTile},
    }};
    constexpr std::array<std::array<float, 2>, 4> kUvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};

    ChunkMeshData data;
    for (const Face& face : faces) {
        const auto base = static_cast<std::uint32_t>(data.vertices.size());
        for (std::size_t i = 0; i < 4; ++i) {
            data.vertices.push_back(ChunkVertex{
                face.corners[i].x, face.corners[i].y, face.corners[i].z,
                kUvs[i][0], kUvs[i][1],
                static_cast<std::uint32_t>(face.tile) | (3u << 8) | (face.normalIndex << 10) |
                    (15u << 17),
            });
        }
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
            data.indices.push_back(base + i);
        }
    }
    return data;
}

// 12 cube edges as line segments, unit cube at the origin.
constexpr std::array<float, 72> kCubeEdges{
    0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1,
    0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1,
};

} // namespace

Renderer::Renderer()
    : m_chunkShader{gl::ShaderProgram::fromFiles("shaders/chunk.vert", "shaders/chunk.frag")},
      m_lineShader{gl::ShaderProgram::fromFiles("shaders/lines.vert", "shaders/lines.frag")},
      m_atlas{TextureAtlas::create()} {
    m_highlightVao.bind();
    glBindBuffer(GL_ARRAY_BUFFER, m_highlightVbo.id());
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeEdges), kCubeEdges.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          reinterpret_cast<const void*>(0));
    glBindVertexArray(0);

    m_chunkBorderVao.bind();
    glBindBuffer(GL_ARRAY_BUFFER, m_chunkBorderVbo.id());
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          reinterpret_cast<const void*>(0));
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    detectGpu();
}

void Renderer::drawChunkBorders(const glm::mat4& viewProj, ChunkCoord chunk) {
    const float x0 = static_cast<float>(chunk.x * Chunk::SizeX);
    const float z0 = static_cast<float>(chunk.z * Chunk::SizeZ);
    const float x1 = x0 + Chunk::SizeX;
    const float z1 = z0 + Chunk::SizeZ;
    const float yTop = static_cast<float>(Chunk::SizeY);

    std::vector<float> verts;
    const auto line = [&](float ax, float ay, float az, float bx, float by, float bz) {
        verts.insert(verts.end(), {ax, ay, az, bx, by, bz});
    };
    // Cell grid on each of the four chunk walls: vertical lines at every block
    // column, horizontal rings every 16 blocks of height.
    for (int i = 0; i <= Chunk::SizeX; ++i) {
        const float x = x0 + static_cast<float>(i);
        line(x, 0.0f, z0, x, yTop, z0);
        line(x, 0.0f, z1, x, yTop, z1);
    }
    for (int i = 0; i <= Chunk::SizeZ; ++i) {
        const float z = z0 + static_cast<float>(i);
        line(x0, 0.0f, z, x0, yTop, z);
        line(x1, 0.0f, z, x1, yTop, z);
    }
    for (int y = 0; y <= Chunk::SizeY; y += Chunk::SizeX) {
        const float fy = static_cast<float>(y);
        line(x0, fy, z0, x1, fy, z0);
        line(x0, fy, z1, x1, fy, z1);
        line(x0, fy, z0, x0, fy, z1);
        line(x1, fy, z0, x1, fy, z1);
    }

    m_chunkBorderVao.bind();
    glBindBuffer(GL_ARRAY_BUFFER, m_chunkBorderVbo.id());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STREAM_DRAW);

    m_lineShader.use();
    m_lineShader.setMat4("uMvp", viewProj);
    m_lineShader.setVec4("uColor", glm::vec4{0.2f, 0.9f, 1.0f, 1.0f});
    glDisable(GL_DEPTH_TEST); // read through terrain — it's a debug aid
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 3));
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void Renderer::detectGpu() {
    if (const GLubyte* renderer = glGetString(GL_RENDERER); renderer != nullptr) {
        m_gpuName = reinterpret_cast<const char*>(renderer);
    }
    // Core profile drops the old GL_EXTENSIONS string; enumerate indexed.
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (ext == nullptr) {
            continue;
        }
        const std::string_view name{ext};
        if (name == "GL_NVX_gpu_memory_info") {
            m_vramSource = VramSource::Nvx;
            glGetIntegerv(kGpuMemTotalNvx, &m_vramTotalKB);
            return; // prefer the NVIDIA path: it reports total + current
        }
        if (name == "GL_ATI_meminfo") {
            m_vramSource = VramSource::Ati;
        }
    }
    const char* source = m_vramSource == VramSource::Nvx   ? "NVX_gpu_memory_info"
                         : m_vramSource == VramSource::Ati ? "ATI_meminfo"
                                                           : "no VRAM query";
    logInfo(std::format("gpu: {} ({})", m_gpuName, source));
}

Renderer::GpuStats Renderer::gpuStats() const {
    GpuStats stats{m_gpuName, -1, -1, -1};
    if (m_vramSource == VramSource::Nvx) {
        GLint currentKB = 0;
        glGetIntegerv(kGpuMemCurrentNvx, &currentKB);
        stats.totalVramMB = m_vramTotalKB / 1024;
        stats.freeVramMB = currentKB / 1024;
        stats.usedVramMB = (m_vramTotalKB - currentKB) / 1024;
    } else if (m_vramSource == VramSource::Ati) {
        GLint info[4] = {0, 0, 0, 0}; // [0] = total free texture memory, KB
        glGetIntegerv(kTextureFreeMemAti, info);
        stats.freeVramMB = info[0] / 1024;
    }
    return stats;
}

Renderer::GpuMesh Renderer::makeGpuMesh(const ChunkMeshData& data) {
    GpuMesh mesh;
    if (data.empty()) {
        return mesh;
    }
    mesh.vao.bind();
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo.id());
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.vertices.size() * sizeof(ChunkVertex)),
                 data.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo.id());
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.indices.size() * sizeof(std::uint32_t)),
                 data.indices.data(), GL_STATIC_DRAW);
    setupChunkAttributes();
    glBindVertexArray(0);
    mesh.indexCount = static_cast<GLsizei>(data.indices.size());
    return mesh;
}

void Renderer::uploadChunkMesh(ChunkCoord coord, ChunkMesher::Result&& mesh) {
    ChunkMeshes meshes{makeGpuMesh(mesh.opaque), makeGpuMesh(mesh.water)};
    m_chunks.insert_or_assign(coord, std::move(meshes));
}

void Renderer::removeChunkMesh(ChunkCoord coord) noexcept {
    m_chunks.erase(coord);
}

void Renderer::render(const FrameParams& params) {
    glClearColor(params.fogColor.r, params.fogColor.g, params.fogColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Frustum frustum = Frustum::fromMatrix(params.viewProj);

    m_chunkShader.use();
    m_chunkShader.setMat4("uViewProj", params.viewProj);
    m_chunkShader.setVec3("uFogColor", params.fogColor);
    m_chunkShader.setFloat("uFogStart", params.fogStart);
    m_chunkShader.setFloat("uFogEnd", params.fogEnd);
    m_chunkShader.setFloat("uSkyLight", params.skyLight);
    m_chunkShader.setVec3("uSunDir", params.sunDirection);
    m_chunkShader.setFloat("uScale", 1.0f);
    m_chunkShader.setFloat("uLightScale", 1.0f);
    m_chunkShader.setInt("uAtlas", 0);
    m_atlas.bind(0);

    struct VisibleChunk {
        const ChunkMeshes* meshes;
        glm::vec3 origin;
        float distanceSq;
    };
    std::vector<VisibleChunk> visible;
    visible.reserve(m_chunks.size());

    for (const auto& [coord, meshes] : m_chunks) {
        const glm::vec3 origin{static_cast<float>(coord.x * Chunk::SizeX), 0.0f,
                               static_cast<float>(coord.z * Chunk::SizeZ)};
        const glm::vec3 size{static_cast<float>(Chunk::SizeX), static_cast<float>(Chunk::SizeY),
                             static_cast<float>(Chunk::SizeZ)};
        if (!frustum.intersectsAabb(origin, origin + size)) {
            continue;
        }
        const glm::vec3 toCamera = params.cameraPos - (origin + size * 0.5f);
        visible.push_back(VisibleChunk{&meshes, origin, glm::dot(toCamera, toCamera)});
    }
    m_drawnLastFrame = visible.size();

    // One sort, near-to-far. Opaque draws front-to-back so early-Z rejects
    // occluded fragments; water reuses the same order in reverse for the
    // back-to-front translucent pass.
    std::sort(visible.begin(), visible.end(), [](const VisibleChunk& a, const VisibleChunk& b) {
        return a.distanceSq < b.distanceSq;
    });

    m_chunkShader.setFloat("uAlpha", 1.0f);
    for (const VisibleChunk& chunk : visible) {
        if (chunk.meshes->opaque.indexCount == 0) {
            continue;
        }
        m_chunkShader.setVec3("uChunkOrigin", chunk.origin);
        chunk.meshes->opaque.vao.bind();
        glDrawElements(GL_TRIANGLES, chunk.meshes->opaque.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    // Translucent water: back-to-front with depth writes off so distant
    // surfaces show through near ones instead of being depth-rejected.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_chunkShader.setFloat("uAlpha", 0.65f);
    for (auto it = visible.rbegin(); it != visible.rend(); ++it) {
        const VisibleChunk& chunk = *it;
        if (chunk.meshes->water.indexCount == 0) {
            continue;
        }
        m_chunkShader.setVec3("uChunkOrigin", chunk.origin);
        chunk.meshes->water.vao.bind();
        glDrawElements(GL_TRIANGLES, chunk.meshes->water.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    if (params.highlightedBlock.has_value()) {
        const glm::vec3 pos{*params.highlightedBlock};
        // Slight inflation keeps the wireframe from z-fighting the block faces.
        const glm::mat4 model = glm::scale(
            glm::translate(glm::mat4{1.0f}, pos - glm::vec3{0.002f}), glm::vec3{1.004f});
        m_lineShader.use();
        m_lineShader.setMat4("uMvp", params.viewProj * model);
        m_lineShader.setVec4("uColor", glm::vec4{0.05f, 0.05f, 0.05f, 1.0f});
        m_highlightVao.bind();
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(kCubeEdges.size() / 3));
    }
    glBindVertexArray(0);
}

const Renderer::GpuMesh& Renderer::dropMesh(BlockType type) {
    if (const auto it = m_dropMeshes.find(type); it != m_dropMeshes.end()) {
        return it->second;
    }
    return m_dropMeshes.emplace(type, makeGpuMesh(makeDropMeshData(type))).first->second;
}

// Uniform state (view-projection, fog, atlas binding) carries over from
// render() on the same program; only origin, scale and light change per drop.
void Renderer::drawDrops(std::span<const DropDraw> drops) {
    if (drops.empty()) {
        return;
    }
    m_chunkShader.use();
    m_chunkShader.setFloat("uScale", 0.3f);
    for (const DropDraw& drop : drops) {
        m_chunkShader.setFloat("uLightScale", drop.light);
        m_chunkShader.setVec3("uChunkOrigin", drop.position);
        const GpuMesh& mesh = dropMesh(drop.type);
        mesh.vao.bind();
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    m_chunkShader.setFloat("uScale", 1.0f);
    m_chunkShader.setFloat("uLightScale", 1.0f);
    glBindVertexArray(0);
}

} // namespace cc
