# claudecraft

A Minecraft-style voxel game in modern C++20. OpenGL 3.3 core via GLFW + GLAD, GLM math, stb_image textures. No engine, no CMake — the build is two `cl.exe` invocations driven by `.vscode/tasks.json`.

## Setup

1. Install Visual Studio (Community is fine) with the "Desktop development with C++" workload.
2. Open **x64 Native Tools Command Prompt for VS** (so `cl.exe` is on `PATH`), `cd` to this folder, run `code .`.
3. Build: `Ctrl+Shift+B` (debug) or run the `build (release)` task.
4. Run/debug: `F5` (builds first, launches under the MSVC debugger).

Output lands in `build/debug/claudecraft.exe` / `build/release/claudecraft.exe`. The exe must run with the project root as working directory (it loads `shaders/`); `launch.json` already sets that.

If your MSVC version differs, update `compilerPath` in `.vscode/c_cpp_properties.json` — the build tasks don't care, they use whatever `cl` is on `PATH`.

## Controls

| Input | Action |
|---|---|
| WASD + mouse | Move / look |
| Space | Jump (walk) / ascend (fly) |
| Left Shift | Descend (fly) |
| F | Toggle fly |
| Left / right click | Break / place block (hold to repeat) |
| 1–8, scroll wheel | Select hotbar slot |
| Esc | Pause menu (resume / save and quit) |

The game opens on a main menu with a randomly seeded terrain fly-over in the background; Play starts the persistent world (fixed seed), Quit exits. The window title shows FPS, position, and loaded/drawn chunk counts.

## Architecture

```
src/
  Main.cpp            entry point; catches and logs init failures
  app/                Window (GLFW+GLAD RAII), Application (composition root, game loop)
  core/               ThreadPool (jthread), ConcurrentQueue, logging
  gl/                 move-only RAII wrappers (Buffer/VertexArray/Texture2D),
                      ShaderProgram (compile/link checked), KHR_debug hookup
  input/              Input (polled view over GLFW callbacks)
  player/             Camera (matrices), Player (swept-AABB physics, fly/walk)
  render/             Renderer (chunk GPU meshes, frustum culling, water pass,
                      block highlight), TextureAtlas, Frustum, Hud (immediate-
                      mode overlay: rects/icons + stb_easy_font text, one
                      batched draw per frame)
  world/              Chunk (16x16x256, contiguous), World (streaming pipeline),
                      ChunkMesher (greedy meshing + AO), TerrainGenerator (Perlin
                      fBm, biomes, trees), WorldSave (RLE, versioned), Raycast (DDA)
shaders/              GLSL 330: chunk, hud, lines
third_party/          vendored GLFW 3.4 (+ glfw3.lib), GLAD, GLM 1.0.1, stb_image
```

### Threading model

All OpenGL stays on the main thread. Each frame, `World::update`:

1. integrates finished chunks from the generation queue,
2. saves + evicts chunks outside `renderDistance + 2`,
3. submits missing chunks (nearest first) to the worker pool — workers load the chunk from disk or generate it,
4. submits mesh jobs for dirty chunks whose 8 lateral neighbours are loaded; the job input is an immutable 18x18x256 snapshot copied on the main thread, so workers share nothing,
5. drains finished meshes and returns them as `WorldUpdate` for the renderer to upload.

Stale results are handled with a per-chunk mesh revision: every edit bumps it, every mesh job carries the revision it was built from, and mismatched results are dropped (the mismatch also keeps the chunk scheduled for a rebuild). `World`'s destructor waits for its in-flight jobs before the members they reference die.

### Meshing

Greedy meshing per face direction: each slice builds a mask of visible faces (face culled when the neighbour is opaque), then merges equal rectangles. Equality includes the 4-corner ambient-occlusion values, so merged quads keep correct shading; quads are split along the brighter diagonal to avoid AO anisotropy. Texcoords are emitted in block units and wrapped with `fract()` in the fragment shader — that's what lets one greedy quad tile a single atlas cell across many blocks (nearest filtering, no mips, no bleed). Water goes into a second translucent mesh, drawn back-to-front with depth writes off.

### World persistence

Only modified chunks are written: `saves/world_<seed>/c_<x>_<z>.bin`, an 8-byte magic+version header followed by RLE runs. Corrupt or version-mismatched files are ignored and the chunk regenerates. Saving happens on eviction and on exit.

### Textures

`render/TextureAtlas` loads `textures/atlas.png` (square, side divisible by 4, 4x4 tile grid) if present; otherwise it paints a deterministic procedural atlas so the repo needs no binary assets.
