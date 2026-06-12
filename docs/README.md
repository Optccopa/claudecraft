# docs/

Engineering docs. Each file owns one subsystem; update the matching doc in
the same change as the code (see "Documentation" in [CLAUDE.md](../CLAUDE.md)).

| Doc | Covers | Update when you touch |
|---|---|---|
| [architecture.md](architecture.md) | module map, state machine, frame loop, ownership, coordinates | `app/`, module boundaries, game states, member ordering |
| [build-system.md](build-system.md) | cl.exe tasks, flags, vendored deps, IntelliSense | `.vscode/`, `third_party/`, new source dirs |
| [threading.md](threading.md) | worker pipeline, snapshots, revision protocol, shutdown | `World`, `ThreadPool`, queues, any new parallel work |
| [meshing.md](meshing.md) | greedy meshing, AO, vertex format, fract atlas trick | `ChunkMesher`, `ChunkVertex`, chunk shaders |
| [terrain.md](terrain.md) | height/biomes, caves, ores, trees, determinism rules | `TerrainGenerator`, `Noise` |
| [rendering.md](rendering.md) | passes, shader contracts, atlas, HUD, GL debug | `Renderer`, `Hud`, `TextureAtlas`, `shaders/` |
| [save-format.md](save-format.md) | world dirs + meta, binary layout, validation, versioning | `WorldSave`, `WorldList`, `BlockType` enum |
