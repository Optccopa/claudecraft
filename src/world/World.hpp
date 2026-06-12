#pragma once

#include "core/ConcurrentQueue.hpp"
#include "world/Block.hpp"
#include "world/Chunk.hpp"
#include "world/ChunkCoord.hpp"
#include "world/ChunkMesher.hpp"
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

    [[nodiscard]] bool isChunkLoadedAt(const glm::vec3& worldPos) const noexcept;
    // Streaming adapts on the next update(): new ring loads, distant unloads.
    void setRenderDistance(int distance) noexcept { m_renderDistance = distance; }
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

    [[nodiscard]] Chunk* chunkAt(ChunkCoord coord) noexcept;
    [[nodiscard]] const Chunk* chunkAt(ChunkCoord coord) const noexcept;
    [[nodiscard]] bool neighborsLoaded(ChunkCoord coord) const noexcept;
    [[nodiscard]] std::shared_ptr<const MeshInput> makeMeshInput(const Chunk& center) const;

    void submitTracked(std::function<void()> job);

    TerrainGenerator m_generator;
    WorldSave m_save;
    ThreadPool& m_pool;
    int m_renderDistance;

    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> m_chunks;
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_pendingGen;
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
