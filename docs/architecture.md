# Architecture

Top-level data flow, module boundaries, and the rules that keep them apart.
For threading details see [threading.md](threading.md); for meshing see
[meshing.md](meshing.md).

## Module map

```
app/         Window (GLFW/GLAD lifetime), Application (composition root + state machine)
core/        ThreadPool, ConcurrentQueue, logging — no game knowledge
gl/          RAII handles, ShaderProgram, KHR_debug hookup — no game knowledge
input/       Input — polled wrapper over GLFW callbacks
player/      Camera (pure math), Player (physics, no GL, no GLFW)
render/      Renderer, TextureAtlas, Frustum, Hud — owns ALL chunk GPU state
world/       Chunk, World, ChunkMesher, TerrainGenerator, WorldSave, Raycast — no GL
```

Dependency rules (enforced by review, not tooling):

- `world/` never includes anything from `gl/` or `render/`. Finished meshes
  leave `World::update` as CPU data (`WorldUpdate`) and the renderer uploads
  them. This is what keeps GL on the main thread for free.
- `core/` and `gl/` know nothing about blocks, chunks, or players.
- Only `Application` wires modules together. No module reaches for another
  via globals — there are none.
- `Player` takes `const World&` per call; it holds no world reference.

## Application state machine

`Application::run` drives three states; `Menu` has two screens:

| State | World updated | Physics | Cursor | UI |
|---|---|---|---|---|
| `Menu`/Main | menu world (random seed, distance 8) | none | free | title + PLAY/QUIT |
| `Menu`/Worlds | menu world | none | free | name field + CREATE + world list + BACK |
| `Playing` | selected world (settings distance) | fixed 60 Hz | captured | crosshair + hotbar |
| `Paused` | selected world (streaming only) | frozen | free | overlay + RESUME/SETTINGS/QUIT TO MENU |
| `Settings` | backdrop of wherever it was opened from | frozen | free | category tabs + value rows + BACK |

ESC transitions are handled once at the top of the frame, *before* the state
runs — a single press must not be consumed by two states in one frame (pause
then instantly resume). ESC walks back one level: Settings → where it was
opened from; Worlds → Main → quit; Playing ⇄ Paused. Keep any new global
hotkeys in that same block.

`Settings` is reachable from both the main menu and pause; `m_settingsFrom`
remembers the return state and picks the backdrop (panning menu world vs the
frozen game world). Values are defined in `app/Settings` (persisted to
`settings.txt`, clamped on load, saved on leaving the screen) and applied
live when clicked: render distance restreams the loaded world, FOV updates
the camera, vsync/fullscreen go straight to GLFW, sensitivity/invert-Y feed
the look-input path. A new setting needs: a `Settings` field + load/save
line, a `settingRow` in `updateSettings`, and its live-apply call.

World identity comes from `world/WorldList`: the Worlds screen lists
`saves/` (newest first) and creates named worlds with a random seed
(see [save-format.md](save-format.md)). `startGame` and `enterMenu` are the
only world-lifecycle functions: each resets the outgoing `World` (its
destructor blocks until worker jobs finish, saving modified chunks) and
calls `Renderer::clearChunkMeshes()`, so two worlds never coexist and the
renderer never holds meshes for a dead world.

The menu world is a throwaway: random seed each launch (and again on quit-
to-menu), never modified, so it writes nothing to disk. Text input for the
name field comes from `Input::typedText()` (GLFW char callback, printable
ASCII, key-repeat-aware backspace).

## Frame loop

```
beginFrame (input edge reset) → glfwPollEvents
ESC state transition
state update:
    gameplay input → fixed-step physics (accumulator) → World::update
    → renderer uploads/removals → 3D render → HUD batch
hud.flush (single draw)
swapBuffers → title-bar stats
```

Physics runs at a fixed 1/60 s; rendering is uncapped and interpolates the
player position by `accumulator / dt` (`Player::eyePosition(alpha)`).
Frame gaps are clamped to 0.25 s so a debugger pause doesn't spiral the
accumulator. Look (yaw/pitch) updates at render rate for latency; movement
intent is sampled into `PlayerInput` and applied in the fixed step.

## Ownership and lifetime

- Every GL object lives in a move-only RAII wrapper (`gl/GlObjects.hpp`,
  `gl/ShaderProgram`). Constructors require a current context; the moved-from
  state is id 0 and destruction of id 0 is a no-op.
- `Application`'s member declaration order is load-bearing: `m_pool` is
  declared before the worlds so destruction (reverse order) tears down each
  `World` — whose destructor waits for its in-flight jobs — while the pool
  still exists. Don't reorder those members.
- `Window` is pinned (copy and move deleted): it owns process-wide GLFW
  init/terminate.

## Coordinates

- World space: +y up, chunk (cx, cz) spans x ∈ [16·cx, 16·cx+16).
- Chunk-local indexing `(x·16 + z)·256 + y` — y contiguous, so a vertical
  column is one `memcpy` (terrain fill, mesh snapshots, RLE saves all lean
  on this).
- `wx >> 4` / `wx & 15` for chunk coord / local coord — arithmetic shift
  floors correctly for negatives where `/ 16` wouldn't.
- HUD space: pixels, origin bottom-left, y up. Cursor coords from GLFW are
  window-relative y-down and get flipped + DPI-scaled in
  `Application::mouseUiPosition`.
