#include "render/Renderer.hpp"

#include "render/Frustum.hpp"
#include "world/Chunk.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace cc {
namespace {

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

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
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
    m_chunkShader.setVec3("uCameraPos", params.cameraPos);
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
    std::sort(visible.begin(), visible.end(), [](const VisibleChunk& a, const VisibleChunk& b) {
        return a.distanceSq > b.distanceSq;
    });
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_chunkShader.setFloat("uAlpha", 0.65f);
    for (const VisibleChunk& chunk : visible) {
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
