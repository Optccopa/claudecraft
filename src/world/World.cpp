#include "world/World.hpp"

#include "core/ThreadPool.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace cc {
namespace {

constexpr int kMaxGenSubmitsPerFrame = 16;
constexpr int kMaxMeshSubmitsPerFrame = 16;
// Integration runs main-thread work per chunk (light seam merge + BFS), so
// it is the one pipeline stage that must be paced to protect frame time.
constexpr int kMaxIntegrationsPerFrame = 4;
constexpr int kUnloadSlack = 2; // hysteresis so chunks don't thrash at the edge

[[nodiscard]] int distanceSq(ChunkCoord a, ChunkCoord b) noexcept {
    const int dx = a.x - b.x;
    const int dz = a.z - b.z;
    return dx * dx + dz * dz;
}

} // namespace

World::World(std::uint32_t seed, int renderDistance, ThreadPool& pool,
             std::filesystem::path saveDirectory)
    : m_generator{seed},
      m_save{std::move(saveDirectory)},
      m_pool{pool},
      m_renderDistance{renderDistance} {}

World::~World() {
    {
        std::unique_lock lock(m_flightMutex);
        m_flightCv.wait(lock, [this] { return m_inFlight == 0; });
    }
    saveModified();
}

void World::submitTracked(std::function<void()> job) {
    {
        const std::scoped_lock lock(m_flightMutex);
        ++m_inFlight;
    }
    m_pool.submit([this, job = std::move(job)] {
        job();
        {
            const std::scoped_lock lock(m_flightMutex);
            --m_inFlight;
        }
        m_flightCv.notify_all();
    });
}

Chunk* World::chunkAt(ChunkCoord coord) noexcept {
    const auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}

const Chunk* World::chunkAt(ChunkCoord coord) const noexcept {
    const auto it = m_chunks.find(coord);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}

BlockType World::blockAt(const glm::ivec3& p) const noexcept {
    if (p.y < 0 || p.y >= Chunk::SizeY) {
        return BlockType::Air;
    }
    const Chunk* chunk = chunkAt(chunkCoordOf(p.x, p.z));
    if (chunk == nullptr) {
        return BlockType::Air;
    }
    return chunk->at(p.x & 15, p.y, p.z & 15);
}

// Edits within one block of a border change the neighbour's face culling,
// AO and light sampling, so its mesh must rebuild too (diagonals included).
void World::bumpMeshRevisionsAround(Chunk& chunk, int lx, int lz) {
    chunk.bumpMeshRevision();
    markMeshDirty(chunk.coord());
    const int dxLo = (lx == 0) ? -1 : 0;
    const int dxHi = (lx == Chunk::SizeX - 1) ? 1 : 0;
    const int dzLo = (lz == 0) ? -1 : 0;
    const int dzHi = (lz == Chunk::SizeZ - 1) ? 1 : 0;
    for (int dx = dxLo; dx <= dxHi; ++dx) {
        for (int dz = dzLo; dz <= dzHi; ++dz) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            const ChunkCoord nc{chunk.coord().x + dx, chunk.coord().z + dz};
            if (Chunk* neighbor = chunkAt(nc)) {
                neighbor->bumpMeshRevision();
                markMeshDirty(nc);
            }
        }
    }
}

bool World::setBlock(const glm::ivec3& p, BlockType type) {
    if (p.y < 0 || p.y >= Chunk::SizeY) {
        return false;
    }
    Chunk* chunk = chunkAt(chunkCoordOf(p.x, p.z));
    if (chunk == nullptr) {
        return false;
    }
    const int lx = p.x & 15;
    const int lz = p.z & 15;
    const BlockType oldType = chunk->at(lx, p.y, lz);
    if (!blockInfo(oldType).breakable && type == BlockType::Air) {
        return false;
    }
    chunk->set(lx, p.y, lz, type);
    // A placed liquid is a source; any other block clears the cell's level.
    chunk->setFluid(lx, p.y, lz, type == BlockType::Water ? FluidSim::Source : std::uint8_t{0});
    chunk->markModified();
    bumpMeshRevisionsAround(*chunk, lx, lz);
    m_lightEngine.onBlockChanged(*this, p, oldType);
    m_fluidSim.onBlockChanged(p);
    return true;
}

std::uint8_t World::lightPackedAt(const glm::ivec3& p) const noexcept {
    if (p.y >= Chunk::SizeY) {
        return Chunk::packLight(LightEngine::MaxLevel, 0);
    }
    if (p.y < 0) {
        return 0;
    }
    const Chunk* chunk = chunkAt(chunkCoordOf(p.x, p.z));
    if (chunk == nullptr) {
        return 0;
    }
    return chunk->lightAt(p.x & 15, p.y, p.z & 15);
}


bool World::isChunkLoadedAt(const glm::vec3& p) const noexcept {
    return chunkAt(chunkCoordOf(static_cast<int>(std::floor(p.x)),
                                static_cast<int>(std::floor(p.z)))) != nullptr;
}

bool World::neighborsLoaded(ChunkCoord coord) const noexcept {
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if ((dx != 0 || dz != 0) && chunkAt({coord.x + dx, coord.z + dz}) == nullptr) {
                return false;
            }
        }
    }
    return true;
}

// Snapshot the chunk plus a one-block border from its eight neighbours.
// Column-contiguous storage makes this 18x18 column copies, ~84 KB — cheap
// enough to take on the main thread, and it keeps workers lock-free.
void World::setSmoothLighting(bool smooth) noexcept {
    if (smooth == m_smoothLighting) {
        return;
    }
    m_smoothLighting = smooth;
    for (const auto& [coord, chunk] : m_chunks) {
        chunk->bumpMeshRevision();
        markMeshDirty(coord);
    }
}

std::shared_ptr<MeshInput> World::acquireMeshInput() {
    // The control-block refcount is atomic; use_count() == 1 means only the
    // pool still references the slot, so its worker has finished and the buffer
    // is safe to overwrite. makeMeshInput rewrites every cell, leaving no stale
    // data from the prior job.
    for (const auto& slot : m_meshInputPool) {
        if (slot.use_count() == 1) {
            return slot;
        }
    }
    auto fresh = std::make_shared<MeshInput>();
    m_meshInputPool.push_back(fresh);
    return fresh;
}

std::shared_ptr<const MeshInput> World::makeMeshInput(const Chunk& center) {
    std::shared_ptr<MeshInput> input = acquireMeshInput();
    input->coord = center.coord();
    input->revision = center.meshRevision();
    input->smoothLighting = m_smoothLighting;

    // The padded region only ever spans the 3x3 chunks around the centre, and
    // those are fixed for the whole copy — fetch them once instead of hashing
    // a chunk coord for all 324 columns.
    const ChunkCoord base = center.coord();
    std::array<std::array<const Chunk*, 3>, 3> neighbors{};
    for (int bx = 0; bx < 3; ++bx) {
        for (int bz = 0; bz < 3; ++bz) {
            neighbors[static_cast<std::size_t>(bx)][static_cast<std::size_t>(bz)] =
                chunkAt(ChunkCoord{base.x - 1 + bx, base.z - 1 + bz});
        }
    }

    for (int px = -1; px <= Chunk::SizeX; ++px) {
        const int bandX = px < 0 ? 0 : (px < Chunk::SizeX ? 1 : 2);
        for (int pz = -1; pz <= Chunk::SizeZ; ++pz) {
            const int bandZ = pz < 0 ? 0 : (pz < Chunk::SizeZ ? 1 : 2);
            const Chunk* chunk =
                neighbors[static_cast<std::size_t>(bandX)][static_cast<std::size_t>(bandZ)];
            const int wx = base.x * Chunk::SizeX + px;
            const int wz = base.z * Chunk::SizeZ + pz;
            const std::size_t dst =
                static_cast<std::size_t>(((px + 1) * MeshInput::PaddedZ + (pz + 1)) *
                                         Chunk::SizeY);
            std::memcpy(input->blocks.data() + dst, chunk->column(wx & 15, wz & 15),
                        Chunk::SizeY * sizeof(BlockType));
            std::memcpy(input->light.data() + dst, chunk->lightColumn(wx & 15, wz & 15),
                        Chunk::SizeY);
            std::memcpy(input->fluid.data() + dst, chunk->fluidColumn(wx & 15, wz & 15),
                        Chunk::SizeY);
        }
    }
    return input;
}

WorldUpdate World::update(const glm::vec3& playerPos) {
    const ChunkCoord center = chunkCoordOf(static_cast<int>(std::floor(playerPos.x)),
                                           static_cast<int>(std::floor(playerPos.z)));
    WorldUpdate out;
    integrateGenerated(out, center);
    unloadDistant(out, center);
    if (++m_fluidTickCounter >= kFluidUpdateInterval) {
        m_fluidTickCounter = 0;
        m_fluidSim.tick(*this);
    }
    scheduleGeneration(center);
    scheduleMeshing(center);
    drainMeshResults(out);
    return out;
}

void World::integrateGenerated(WorldUpdate& out, ChunkCoord center) {
    (void)out;
    GenResult result;
    for (int n = 0; n < kMaxIntegrationsPerFrame && m_genResults.tryPop(result); ++n) {
        m_pendingGen.erase(result.coord);
        // Player may have moved away while the job was in flight.
        if (distanceSq(result.coord, center) >
            (m_renderDistance + kUnloadSlack) * (m_renderDistance + kUnloadSlack)) {
            continue;
        }
        m_chunks.emplace(result.coord, std::move(result.chunk));
        markMeshDirty(result.coord);
        m_lightEngine.onChunkLoaded(*this, result.coord);
        m_fluidSim.onChunkLoaded(*this, result.coord);
    }
}

void World::scheduleGeneration(ChunkCoord center) {
    // The radius rescan is ~450 cells of hashing per frame. Once the player
    // chunk stops moving it only pays off while there is still room in the gen
    // pipeline AND holes left to fill; skip it otherwise.
    const bool saturated = static_cast<int>(m_pendingGen.size()) >= kMaxGenSubmitsPerFrame;
    if (m_genCenterValid && center == m_lastGenCenter && (m_allLoaded || saturated)) {
        return;
    }
    m_lastGenCenter = center;
    m_genCenterValid = true;

    std::vector<ChunkCoord> missing;
    for (int dx = -m_renderDistance; dx <= m_renderDistance; ++dx) {
        for (int dz = -m_renderDistance; dz <= m_renderDistance; ++dz) {
            if (dx * dx + dz * dz > m_renderDistance * m_renderDistance) {
                continue;
            }
            const ChunkCoord coord{center.x + dx, center.z + dz};
            if (chunkAt(coord) == nullptr && !m_pendingGen.contains(coord)) {
                missing.push_back(coord);
            }
        }
    }
    m_allLoaded = missing.empty();
    std::sort(missing.begin(), missing.end(), [center](ChunkCoord a, ChunkCoord b) {
        return distanceSq(a, center) < distanceSq(b, center);
    });

    const int count = std::min(static_cast<int>(missing.size()), kMaxGenSubmitsPerFrame);
    for (int i = 0; i < count; ++i) {
        const ChunkCoord coord = missing[static_cast<std::size_t>(i)];
        m_pendingGen.insert(coord);
        submitTracked([this, coord] {
            auto chunk = m_save.tryLoad(coord);
            const bool generated = chunk == nullptr;
            if (generated) {
                chunk = m_generator.generate(coord);
                // Loaded chunks already carry their fluid (restored or primed by
                // tryLoad); only freshly generated terrain needs sources primed.
                chunk->primeFluidSources();
            }
            LightEngine::initializeChunkLight(*chunk);
            m_genResults.push(GenResult{coord, std::move(chunk)});
        });
    }
}

void World::scheduleMeshing(ChunkCoord center) {
    std::vector<Chunk*> dirty;
    for (auto it = m_dirtyMesh.begin(); it != m_dirtyMesh.end();) {
        Chunk* chunk = chunkAt(*it);
        if (chunk == nullptr || chunk->scheduledRevision() == chunk->meshRevision()) {
            it = m_dirtyMesh.erase(it); // unloaded or already up to date
            continue;
        }
        if (neighborsLoaded(*it)) {
            dirty.push_back(chunk);
        }
        ++it; // keep until meshed; a missing neighbour may load next frame
    }
    std::sort(dirty.begin(), dirty.end(), [center](const Chunk* a, const Chunk* b) {
        return distanceSq(a->coord(), center) < distanceSq(b->coord(), center);
    });

    const int count = std::min(static_cast<int>(dirty.size()), kMaxMeshSubmitsPerFrame);
    for (int i = 0; i < count; ++i) {
        Chunk& chunk = *dirty[static_cast<std::size_t>(i)];
        const auto input = makeMeshInput(chunk);
        chunk.setScheduledRevision(chunk.meshRevision());
        submitTracked([this, input] {
            m_meshResults.push(
                MeshJobResult{input->coord, input->revision, ChunkMesher::build(*input)});
        });
    }
}

void World::drainMeshResults(WorldUpdate& out) {
    MeshJobResult result;
    while (m_meshResults.tryPop(result)) {
        const Chunk* chunk = chunkAt(result.coord);
        // Stale results (chunk unloaded or edited since the snapshot) are
        // dropped; the revision mismatch keeps the chunk scheduled for a
        // fresh build.
        if (chunk == nullptr || chunk->meshRevision() != result.revision) {
            continue;
        }
        out.uploads.push_back(MeshUpload{result.coord, std::move(result.mesh)});
    }
}

void World::unloadDistant(WorldUpdate& out, ChunkCoord center) {
    const int limit = m_renderDistance + kUnloadSlack;
    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
        if (distanceSq(it->first, center) > limit * limit) {
            if (it->second->isModified()) {
                m_save.save(*it->second);
            }
            m_dirtyMesh.erase(it->first);
            m_allLoaded = false; // a hole opened; let the gen scan run again
            out.removed.push_back(it->first);
            it = m_chunks.erase(it);
        } else {
            ++it;
        }
    }
}

void World::saveModified() {
    for (const auto& [coord, chunk] : m_chunks) {
        if (chunk->isModified()) {
            m_save.save(*chunk);
            chunk->clearModified();
        }
    }
}

} // namespace cc
