#include "render/Renderer.hpp"

#include "core/Log.hpp"
#include "render/Frustum.hpp"
#include "world/Chunk.hpp"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

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

// Entity vertex layout: position(3) + normal(3) + uv(2), interleaved floats.
void setupEntityAttributes() {
    constexpr GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(6 * sizeof(float)));
}

void setupChunkAttributes() {
    constexpr GLsizei stride = sizeof(ChunkVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, stride,
                           reinterpret_cast<const void*>(offsetof(ChunkVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, stride,
                           reinterpret_cast<const void*>(offsetof(ChunkVertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, stride,
                           reinterpret_cast<const void*>(offsetof(ChunkVertex, data)));
}

// Unit cube centered on the origin for dropped items, in the chunk vertex
// format: full AO, light baked as block-channel 15 so the shader's sun and
// sky scaling don't apply — uLightScale carries the drop's sampled light.
// Unit cube spanning [0,1]^3 at full block light; the shader's uCenter=0.5
// recenters it so uScale shrinks/inflates it about its origin. Shared by the
// dropped items and the block-breaking overlay (one tile on every face).
[[nodiscard]] ChunkMeshData makeCubeMeshData(std::uint8_t top, std::uint8_t side,
                                             std::uint8_t bottom) {
    struct Face {
        std::array<glm::vec3, 4> corners;
        std::uint32_t normalIndex;
        std::uint8_t tile;
    };
    const std::array<Face, 6> faces{{
        {{glm::vec3{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, 0, side},
        {{glm::vec3{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, 1, side},
        {{glm::vec3{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, 2, top},
        {{glm::vec3{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, 3, bottom},
        {{glm::vec3{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, 4, side},
        {{glm::vec3{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}, 5, side},
    }};
    constexpr std::array<std::array<int, 2>, 4> kUvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};

    ChunkMeshData data;
    for (const Face& face : faces) {
        const auto base = static_cast<std::uint32_t>(data.vertices.size());
        for (std::size_t i = 0; i < 4; ++i) {
            const glm::vec3& c = face.corners[i];
            const std::uint32_t vdata = static_cast<std::uint32_t>(face.tile) | (3u << 8) |
                                        (face.normalIndex << 10) | (15u << 17);
            data.vertices.push_back(packChunkVertex(static_cast<int>(c.x) * 16,
                                                    static_cast<int>(c.y) * 16,
                                                    static_cast<int>(c.z) * 16, kUvs[i][0],
                                                    kUvs[i][1], vdata));
        }
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
            data.indices.push_back(base + i);
        }
    }
    return data;
}

[[nodiscard]] ChunkMeshData makeDropMeshData(BlockType type) {
    const BlockInfo& info = blockInfo(type);
    return makeCubeMeshData(info.topTile, info.sideTile, info.bottomTile);
}

// One box's 36 vertices of (position, normal, uv). `appendBox` lays out the UVs
// like a Minecraft model part: the box's pixel size unwraps into the entity
// texture's 64x32 space at `texOffset`, so a pack's cow/pig/sheep skin maps
// straight on. The body lies down at pitch 90. Flat-shaded boxes pass uv 0 and
// the shader ignores it.
struct EntityFace {
    glm::vec3 normal;
    std::array<glm::vec3, 4> corners; // TL, BL, BR, TR (texture upright)
    glm::vec4 uvRect;                 // u0, v0, u1, v1 in texels (top-down)
};

// texW/texH are the skin's actual dimensions: the classic layout is 64x32, but
// modern cow/pig skins are 64x64 (same texel offsets, taller sheet), so UVs
// must normalize by the real size or the lower faces sample the empty half.
void appendBox(std::vector<float>& out, const MobPart& part, int texW, int texH) {
    const float w = part.sizePx.x;
    const float h = part.sizePx.y;
    const float d = part.sizePx.z;
    // Geometry half-extents include the inflate; UVs below keep using w/h/d so
    // the dilated wool layer still maps onto its in-bounds texture island.
    const float a = (w * 0.5f + part.inflate) / 16.0f;
    const float b = (h * 0.5f + part.inflate) / 16.0f;
    const float c = (d * 0.5f + part.inflate) / 16.0f;
    const float tu = static_cast<float>(part.texOffset.x);
    const float tv = static_cast<float>(part.texOffset.y);
    const float rad = glm::radians(part.pitchDeg);
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);
    const auto rot = [&](const glm::vec3& v) {
        return glm::vec3{v.x, v.y * cs - v.z * sn, v.y * sn + v.z * cs};
    };

    const std::array<EntityFace, 6> faces{{
        {{0, 0, 1}, {glm::vec3{-a, b, c}, {-a, -b, c}, {a, -b, c}, {a, b, c}},
         {tu + d, tv + d, tu + d + w, tv + d + h}},
        {{0, 0, -1}, {glm::vec3{a, b, -c}, {a, -b, -c}, {-a, -b, -c}, {-a, b, -c}},
         {tu + 2 * d + w, tv + d, tu + 2 * d + 2 * w, tv + d + h}},
        {{1, 0, 0}, {glm::vec3{a, b, c}, {a, -b, c}, {a, -b, -c}, {a, b, -c}},
         {tu + d + w, tv + d, tu + 2 * d + w, tv + d + h}},
        {{-1, 0, 0}, {glm::vec3{-a, b, -c}, {-a, -b, -c}, {-a, -b, c}, {-a, b, c}},
         {tu, tv + d, tu + d, tv + d + h}},
        {{0, 1, 0}, {glm::vec3{-a, b, -c}, {-a, b, c}, {a, b, c}, {a, b, -c}},
         {tu + d, tv, tu + d + w, tv + d}},
        {{0, -1, 0}, {glm::vec3{-a, -b, c}, {-a, -b, -c}, {a, -b, -c}, {a, -b, c}},
         {tu + d + w, tv, tu + d + 2 * w, tv + d}},
    }};

    const auto push = [&](const glm::vec3& p, const glm::vec3& n, float u, float v) {
        const glm::vec3 wp = rot(p) + part.center;
        const glm::vec3 wn = rot(n);
        out.insert(out.end(), {wp.x, wp.y, wp.z, wn.x, wn.y, wn.z,
                               u / static_cast<float>(texW),
                               1.0f - v / static_cast<float>(texH)});
    };
    for (const EntityFace& f : faces) {
        const std::array<glm::vec2, 4> uv{{{f.uvRect.x, f.uvRect.y},
                                           {f.uvRect.x, f.uvRect.w},
                                           {f.uvRect.z, f.uvRect.w},
                                           {f.uvRect.z, f.uvRect.y}}};
        for (const int i : {0, 1, 2, 0, 2, 3}) {
            push(f.corners[static_cast<std::size_t>(i)], f.normal,
                 uv[static_cast<std::size_t>(i)].x, uv[static_cast<std::size_t>(i)].y);
        }
    }
}

// Flat-shaded fallback: a centred unit cube (uv unused) the shader colours per
// part. Texture dims are irrelevant here since the shader ignores uv.
[[nodiscard]] std::array<float, 36 * 8> buildEntityCube() {
    MobPart unit{glm::vec3{0.0f}, glm::vec3{16.0f}, glm::ivec2{0}, 0.0f, 0, glm::vec3{1.0f}};
    std::vector<float> verts;
    appendBox(verts, unit, kEntityTexW, kEntityTexH);
    std::array<float, 36 * 8> out{};
    std::copy(verts.begin(), verts.end(), out.begin());
    return out;
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
      m_entityShader{gl::ShaderProgram::fromFiles("shaders/entity.vert", "shaders/entity.frag")},
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

    const std::array<float, 36 * 8> cube = buildEntityCube();
    m_mobCubeVao.bind();
    glBindBuffer(GL_ARRAY_BUFFER, m_mobCubeVbo.id());
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube.data(), GL_STATIC_DRAW);
    setupEntityAttributes();
    glBindVertexArray(0);

    loadMobTextures({}); // build the per-type meshes; no skins until a pack loads

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
    m_frameViewProj = params.viewProj;
    m_frameFogColor = params.fogColor;
    m_frameSunDir = params.sunDirection;
    m_frameFogStart = params.fogStart;
    m_frameFogEnd = params.fogEnd;

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
    m_chunkShader.setFloat("uCenter", 0.0f);
    m_chunkShader.setFloat("uLightScale", 1.0f);
    m_chunkShader.setInt("uAtlas", 0);
    m_atlas.bind(0);

    m_visible.clear();

    for (const auto& [coord, meshes] : m_chunks) {
        const glm::vec3 origin{static_cast<float>(coord.x * Chunk::SizeX), 0.0f,
                               static_cast<float>(coord.z * Chunk::SizeZ)};
        const glm::vec3 size{static_cast<float>(Chunk::SizeX), static_cast<float>(Chunk::SizeY),
                             static_cast<float>(Chunk::SizeZ)};
        if (!frustum.intersectsAabb(origin, origin + size)) {
            continue;
        }
        const glm::vec3 toCamera = params.cameraPos - (origin + size * 0.5f);
        m_visible.push_back(VisibleChunk{&meshes, origin, glm::dot(toCamera, toCamera)});
    }
    m_drawnLastFrame = m_visible.size();

    // One sort, near-to-far. Opaque draws front-to-back so early-Z rejects
    // occluded fragments; water reuses the same order in reverse for the
    // back-to-front translucent pass.
    std::sort(m_visible.begin(), m_visible.end(),
              [](const VisibleChunk& a, const VisibleChunk& b) {
                  return a.distanceSq < b.distanceSq;
              });

    m_chunkShader.setFloat("uAlpha", 1.0f);
    for (const VisibleChunk& chunk : m_visible) {
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
    for (auto it = m_visible.rbegin(); it != m_visible.rend(); ++it) {
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
        // The unit-cube wireframe is scaled to the block's local box so it hugs
        // inset shapes (cactus); slight inflation avoids z-fighting the faces.
        const glm::vec3 size = params.highlightMax - params.highlightMin;
        const glm::mat4 model =
            glm::scale(glm::translate(glm::mat4{1.0f},
                                      pos + params.highlightMin - glm::vec3{0.002f}),
                       size + glm::vec3{0.004f});
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
    m_chunkShader.setFloat("uCenter", 0.5f); // recenter the 0..1 cube on its origin
    for (const DropDraw& drop : drops) {
        m_chunkShader.setFloat("uLightScale", drop.light);
        m_chunkShader.setVec3("uChunkOrigin", drop.position);
        const GpuMesh& mesh = dropMesh(drop.type);
        mesh.vao.bind();
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    m_chunkShader.setFloat("uScale", 1.0f);
    m_chunkShader.setFloat("uCenter", 0.0f);
    m_chunkShader.setFloat("uLightScale", 1.0f);
    glBindVertexArray(0);
}

namespace {

void uploadEntityTexture(GLuint id, const PackImage& img) {
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 img.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace

void Renderer::loadMobTextures(std::span<const std::filesystem::path> packs) {
    const auto uploadMesh = [](MobMesh& mesh, const std::vector<float>& verts) {
        mesh.vao.bind();
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo.id());
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_STATIC_DRAW);
        setupEntityAttributes();
        glBindVertexArray(0);
        mesh.vertexCount = static_cast<GLsizei>(verts.size() / 8);
    };
    const auto loadFirst = [&](std::span<const std::string_view> candidates) {
        for (const std::string_view stem : candidates) {
            if (auto img = loadPackImage(packs, stem)) {
                return img;
            }
        }
        return std::optional<PackImage>{};
    };
    // The mesh UVs depend on the skin's real size (64x32 vs 64x64), so the skin
    // is loaded before its layer mesh is built.
    for (std::size_t t = 0; t < m_mobGpu.size(); ++t) {
        const auto type = static_cast<MobType>(t);
        MobGpu& gpu = m_mobGpu[t];

        const auto buildMeshForLayer = [&](MobMesh& mesh, int layer, int texW, int texH) {
            std::vector<float> verts;
            for (const MobPart& part : mobModel(type)) {
                if (part.layer == layer) {
                    appendBox(verts, part, texW, texH);
                }
            }
            uploadMesh(mesh, verts);
        };

        gpu.hasBase = false;
        gpu.hasOverlay = false;
        const auto baseImg = loadFirst(mobTextureCandidates(type));
        buildMeshForLayer(gpu.base, 0, baseImg ? baseImg->width : kEntityTexW,
                          baseImg ? baseImg->height : kEntityTexH);
        if (baseImg) {
            uploadEntityTexture(gpu.baseTex.id(), *baseImg);
            gpu.hasBase = true;
        }
        const auto overlayImg = loadFirst(mobOverlayCandidates(type));
        buildMeshForLayer(gpu.overlay, 1, overlayImg ? overlayImg->width : kEntityTexW,
                          overlayImg ? overlayImg->height : kEntityTexH);
        if (overlayImg) {
            uploadEntityTexture(gpu.overlayTex.id(), *overlayImg);
            gpu.hasOverlay = true;
        }
        logInfo(std::format("mob '{}': skin {}{}", mobName(type),
                            gpu.hasBase ? "loaded" : "MISSING (flat fallback)",
                            gpu.hasOverlay ? " + overlay" : ""));
    }
}

// Box models. Each mob shares one transform (translate to feet, rotate by yaw);
// the geometry already bakes each part's offset and pitch. Textured mobs draw
// the pack skin; the rest fall back to flat-shaded coloured parts. Uses the
// frame params render() cached so the call site stays a single span.
void Renderer::drawMobs(std::span<const MobDraw> mobs) {
    if (mobs.empty()) {
        return;
    }
    m_entityShader.use();
    m_entityShader.setMat4("uViewProj", m_frameViewProj);
    m_entityShader.setVec3("uSunDir", m_frameSunDir);
    m_entityShader.setVec3("uFogColor", m_frameFogColor);
    m_entityShader.setFloat("uFogStart", m_frameFogStart);
    m_entityShader.setFloat("uFogEnd", m_frameFogEnd);
    m_entityShader.setInt("uTex", 0);
    glActiveTexture(GL_TEXTURE0);

    for (const MobDraw& mob : mobs) {
        const glm::mat4 model =
            glm::rotate(glm::translate(glm::mat4{1.0f}, mob.position),
                        glm::radians(mob.yaw), glm::vec3{0.0f, 1.0f, 0.0f});
        m_entityShader.setMat4("uModel", model);
        m_entityShader.setFloat("uLightScale", mob.light);
        m_entityShader.setFloat("uHurt", mob.hurt);

        const MobGpu& gpu = m_mobGpu[static_cast<std::size_t>(mob.type)];
        if (gpu.hasBase) {
            // Textured: one draw per layer, geometry pre-baked with UVs.
            m_entityShader.setInt("uTextured", 1);
            glBindTexture(GL_TEXTURE_2D, gpu.baseTex.id());
            gpu.base.vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, gpu.base.vertexCount);
            if (gpu.hasOverlay) {
                glBindTexture(GL_TEXTURE_2D, gpu.overlayTex.id());
                gpu.overlay.vao.bind();
                glDrawArrays(GL_TRIANGLES, 0, gpu.overlay.vertexCount);
            }
        } else {
            // Flat fallback: per-part coloured unit cube (offset/pitch baked).
            m_entityShader.setInt("uTextured", 0);
            m_mobCubeVao.bind();
            for (const MobPart& part : mobModel(mob.type)) {
                const glm::mat4 partModel = glm::rotate(
                    glm::translate(model, part.center), glm::radians(part.pitchDeg),
                    glm::vec3{1.0f, 0.0f, 0.0f});
                const glm::mat4 scaled =
                    glm::scale(partModel, part.sizePx / 16.0f);
                m_entityShader.setMat4("uModel", scaled);
                m_entityShader.setVec3("uColor", part.color);
                glDrawArrays(GL_TRIANGLES, 0, 36);
            }
        }
    }
    glBindVertexArray(0);
}

const Renderer::GpuMesh& Renderer::breakMesh(std::uint8_t tile) {
    if (const auto it = m_breakMeshes.find(tile); it != m_breakMeshes.end()) {
        return it->second;
    }
    return m_breakMeshes.emplace(tile, makeGpuMesh(makeCubeMeshData(tile, tile, tile)))
        .first->second;
}

// Crack overlay: a slightly inflated, full-bright cube of the stage tile drawn
// over the targeted block. The fragment discard keeps only the crack texels, so
// they blend dark onto every visible face. Reuses render()'s shader state.
void Renderer::drawBlockBreak(const glm::ivec3& block, int stage) {
    const auto tile = static_cast<std::uint8_t>(
        TextureAtlas::DestroyStage0Tile +
        std::clamp(stage, 0, TextureAtlas::DestroyStageCount - 1));
    const GpuMesh& mesh = breakMesh(tile);

    m_chunkShader.use();
    m_chunkShader.setVec3("uChunkOrigin", glm::vec3{block} + glm::vec3{0.5f});
    m_chunkShader.setFloat("uScale", 1.02f); // inflate to sit just proud of the faces
    m_chunkShader.setFloat("uCenter", 0.5f);
    m_chunkShader.setFloat("uAlpha", 0.75f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    mesh.vao.bind();
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);

    m_chunkShader.setFloat("uScale", 1.0f);
    m_chunkShader.setFloat("uCenter", 0.0f);
    m_chunkShader.setFloat("uAlpha", 1.0f);
}

} // namespace cc
