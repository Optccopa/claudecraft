#pragma once

#include "core/ConcurrentQueue.hpp"
#include "world/Block.hpp"
#include "world/Chunk.hpp"
#include "world/ChunkCoord.hpp"
#include "world/ChunkMesher.hpp"
#include "world/FluidSim.hpp"
#include "world/LightEngine.hpp"
#include "world/TerrainGenerator.hpp"
#include "world/WorldSave.hpp"

#include <glm/vec3.hpp>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cc {

class ThreadPool;

struct MeshUpload {
    ChunkCoord coord;
    ChunkMesher::Result mesh;
};

struct WorldUpdate {
    std::vector<MeshUpload> uploads;
    std::vector<ChunkCoord> removed;
};

// Owns chunk storage and the streaming pipeline. Generation and meshing run
// on the worker pool; results cross back through concurrent queues and are
// integrated here on the main thread. No GL — finished meshes are handed out
// via WorldUpdate for the renderer to upload.
class World {
public:
    World(std::uint32_t seed, int renderDistance, ThreadPool& pool,
          std::filesystem::path saveDirectory);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    [[nodiscard]] WorldUpdate update(const glm::vec3& playerPos);

    [[nodiscard]] BlockType blockAt(const glm::ivec3& worldPos) const noexcept;
    // False when the chunk isn't loaded or the edit is rejected (e.g. bedrock).
    bool setBlock(const glm::ivec3& worldPos, BlockType type);

    // Packed light (sky | block << 4). Above the world reads as full sky;
    // below it and in unloaded chunks as darkness.
    [[nodiscard]] std::uint8_t lightPackedAt(const glm::ivec3& worldPos) const noexcept;

    // Non-owning chunk access for the light engine's hot loops, which cache
    // the pointer per chunk instead of paying a hash lookup per cell.
    [[nodiscard]] Chunk* chunkAt(ChunkCoord coord) noexcept;
    [[nodiscard]] const Chunk* chunkAt(ChunkCoord coord) const noexcept;

    [[nodiscard]] bool isChunkLoadedAt(const glm::vec3& worldPos) const noexcept;
    // Streaming adapts on the next update(): new ring loads, distant unloads.
    void setRenderDistance(int distance) noexcept {
        m_renderDistance = distance;
        m_allLoaded = false; // a larger ring needs a fresh generation scan
    }

    // Queues a chunk for remeshing: records it in the dirty set and bumps its
    // mesh revision so scheduleMeshing actually rebuilds it (the revision is
    // what the schedule/stale-result protocol keys on). Public so the light and
    // fluid engines can report the chunks they touched.
    void markMeshDirty(ChunkCoord coord) {
        m_dirtyMesh.insert(coord);
        if (Chunk* chunk = chunkAt(coord)) {
            chunk->bumpMeshRevision();
        }
    }
    // Remeshes every loaded chunk when the value actually changes.
    void setSmoothLighting(bool smooth) noexcept;
    [[nodiscard]] std::size_t loadedChunkCount() const noexcept { return m_chunks.size(); }
    [[nodiscard]] const TerrainGenerator& generator() const noexcept { return m_generator; }

    void saveModified();

    [[nodiscard]] static constexpr ChunkCoord chunkCoordOf(int wx, int wz) noexcept {
        return ChunkCoord{wx >> 4, wz >> 4}; // arithmetic shift floors negatives
    }

private:
    struct GenResult {
        ChunkCoord coord;
        std::unique_ptr<Chunk> chunk;
    };
    struct MeshJobResult {
        ChunkCoord coord;
        std::uint32_t revision = 0;
        ChunkMesher::Result mesh;
    };

    void integrateGenerated(WorldUpdate& out, ChunkCoord center);
    void scheduleGeneration(ChunkCoord center);
    void scheduleMeshing(ChunkCoord center);
    void unloadDistant(WorldUpdate& out, ChunkCoord center);
    void drainMeshResults(WorldUpdate& out);

    void bumpMeshRevisionsAround(Chunk& chunk, int lx, int lz);
    [[nodiscard]] bool neighborsLoaded(ChunkCoord coord) const noexcept;
    [[nodiscard]] std::shared_ptr<const MeshInput> makeMeshInput(const Chunk& center);
    // Reuses a pooled MeshInput whose worker has finished (use_count back to 1)
    // instead of allocating ~166 KB per mesh job during load waves.
    [[nodiscard]] std::shared_ptr<MeshInput> acquireMeshInput();

    void submitTracked(std::function<void()> job);

    TerrainGenerator m_generator;
    WorldSave m_save;
    LightEngine m_lightEngine;
    FluidSim m_fluidSim;
    // Liquid advances one ring per this many update()s (~0.25 s at 60 fps),
    // matching Minecraft's 1 block / 5 game ticks.
    static constexpr int kFluidUpdateInterval = 15;
    int m_fluidTickCounter = 0;
    ThreadPool& m_pool;
    int m_renderDistance;
    bool m_smoothLighting = true;

    // Generation-scan throttle: when the player chunk is unchanged and either
    // the ring is fully loaded or the gen pipeline is saturated, the radius
    // rescan is pure waste, so skip it.
    ChunkCoord m_lastGenCenter{};
    bool m_genCenterValid = false;
    bool m_allLoaded = false;

    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> m_chunks;
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_pendingGen;
    // Chunks awaiting a mesh build, so scheduleMeshing iterates only these
    // instead of every loaded chunk. An entry lingers (e.g. neighbours not yet
    // loaded) until it is successfully scheduled or its chunk unloads.
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_dirtyMesh;
    // Round-robin pool of MeshInput snapshots; a slot is reused once its worker
    // releases it (see acquireMeshInput).
    std::vector<std::shared_ptr<MeshInput>> m_meshInputPool;
    ConcurrentQueue<GenResult> m_genResults;
    ConcurrentQueue<MeshJobResult> m_meshResults;

    // Tracks submitted-but-unfinished jobs; the destructor waits for zero so
    // workers never touch a dead World (the pool outlives us and would
    // otherwise still run queued jobs that capture `this`).
    std::mutex m_flightMutex;
    std::condition_variable m_flightCv;
    int m_inFlight = 0;
};

} // namespace cc
