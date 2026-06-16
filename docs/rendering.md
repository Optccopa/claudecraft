# Rendering

GL 3.3 core, five shader pairs (`shaders/`): `chunk` (world geometry),
`entity` (mobs + viewmodel arm), `cloud` (sky layer), `hud` (2D overlay),
`lines` (block highlight). All GL state changes funnel through `Renderer` and
`Hud` on the main thread.

## Frame composition

```
clear (sky color == fog color, so fogged geometry vanishes seamlessly)
opaque chunk pass     cull back, depth test+write, near-to-far (early-Z)
water chunk pass      blend, depth write OFF, far-to-near chunk order
clouds                camera-centred quad, blend, depth test on / write off
drops + mobs          entity boxes / drop cubes, depth test+write
block highlight       inflated line cube (no z-fight against faces)
viewmodel             held block / arm in view space, depth cleared first
HUD                   one batched draw; depth + cull OFF, blend ON
```

`drawClouds` is a single large quad the `cloud` shader recentres on the camera
each frame; coverage is a hashed blocky field drifting on the wind, fading into
fog at the rim so the finite quad has no edge (height 192, like vanilla).

`drawViewmodel` renders the first-person hand in view space (camera at the
origin, so the projection alone is the MVP) after clearing depth so it overlays
the scene. Holding an item draws its drop cube offset to the lower-right —
perspective off-centre reveals three faces without an explicit tilt; empty-
handed draws a skin-toned arm box (the entity shader's flat path). The block
replaces the hand, matching Minecraft.

Mobs use a separate `entity` shader (`Renderer::drawMobs`). `render()` caches
the frame's view-projection, fog, sun and sky-light; `drawMobs` reuses them so
the call site stays a single span. Each mob is a set of flat-shaded coloured
boxes (a shared centred unit cube with position+normal), transformed by one
base matrix (translate to feet, rotate by yaw) plus a per-part centre offset
and scale. See [mobs.md](mobs.md).

Per-chunk frustum culling: Gribb/Hartmann plane extraction from the
view-projection matrix, p-vertex AABB test against the full-height chunk box
(16×256×16). Title bar's "N drawn" is the post-cull count. Visible chunks are
sorted near-to-far once; the opaque pass walks that order forward (front-to-
back, so depth test early-rejects occluded fragments) and the water pass walks
it in reverse (back-to-front for correct blending).

G toggles a chunk-border wireframe (`Renderer::drawChunkBorders`): the cell
grid on the four walls of the player's current chunk column, streamed each
frame and drawn through the line shader with depth test off so it reads
through terrain.

## Chunk shader contract

Vertex input is the 12-byte `ChunkVertex` — packed `pos`/`uv`/`data` uint32s
(see [meshing.md](meshing.md) for the bit layout and the `fract()` atlas
trick), all unpacked in the vertex shader. Uniforms:

| Uniform | Source |
|---|---|
| `uViewProj` | `Camera::projection() * Camera::view(eye, yaw, pitch)` |
| `uChunkOrigin` | chunk world origin — vertices stay chunk-local for float precision |
| `uFogStart/uFogEnd` | `fogEnd = renderDistance·16 − 8`, start = 0.65·end; fog distance is planar `gl_Position.w` (no per-vertex sqrt) |
| `uSkyLight` | 0..1 scale on the vertex sky-light channel (`FrameParams::skyLight`) |
| `uSunDir` | unit sun direction from `render/Sky` (`FrameParams::sunDirection`) |
| `uAlpha` | 1.0 opaque pass, 0.65 water pass |
| `uScale` / `uCenter` / `uLightScale` | 1 / 0 / 1 for chunk passes; `Renderer::drawDrops` shrinks the per-type one-block cube to 0.3, recenters it with `uCenter`=0.5 (block units, after the shader's ÷16), and feeds each drop's sampled cell light (see [gameplay.md](gameplay.md)) |

Sky colour, sun direction and the sky-light scale all come from one
`skyStateAt(timeOfDay)` call (see the day/night section of
[lighting.md](lighting.md)); the clear colour and fog track it so distant
geometry keeps vanishing into the sky at night.

Per-vertex light comes baked from the mesher (two 4-bit channels, see
[lighting.md](lighting.md)). The vertex shader computes
`max(sky · uSkyLight · (0.45 + 0.55·diffuse), block)` with a 0.03 floor,
then multiplies by the AO curve `[0.45, 0.65, 0.85, 1.0]` (non-linear so
single-occluder corners stay subtle). The sun term only scales the sky
channel — glowstone-lit cave walls ignore sun direction. No shadow maps.

## Texture atlas

8×8 tiles, sampled `GL_NEAREST`, **no mipmaps** (hard requirement — see
meshing doc). Tile ids live in `Block.cpp`'s table; tile 0 is bottom-left of
the texture. 25 tiles are assigned (0–20 cubes, 21–23 cactus side/top/bottom,
24 `short_grass` for the tall-grass billboard); unassigned tiles render magenta
on purpose. The chunk fragment shader `discard`s texels with alpha < 0.5 so the
transparent cutout tiles (tall grass) render as billboards; opaque cube tiles
ship alpha 1 and are never clipped.

`TextureAtlas::create` source order:

1. **Minecraft resource pack stack** — an ordered list of `.zip`s or
   extracted directories under the data dir's `texture_packs/`
   (`%LOCALAPPDATA%/.claudecraft/`, see [save-format.md](save-format.md)),
   highest priority first.
   `core/ZipArchive` reads each zip (stored + deflate, the latter via stb's raw
   inflate, so no new dependency). Each atlas tile takes
   `assets/minecraft/textures/{block,blocks}/<stem>.png` from the **topmost
   pack that supplies it** (stems match `Block.cpp`'s Minecraft ids), so packs
   layer like Minecraft's. A tile no enabled pack provides (or that fails to
   decode everywhere) stays **magenta** — load counts are logged. Per tile the
   loader nearest-samples to 16 px (HD packs downscale), takes the **top square
   frame** of animated strips (e.g. `water_still`), and applies a fixed
   multiply tint to the greyscale tiles Minecraft colours at runtime
   (grass/foliage/water, plains+evergreen constants — no per-biome colormap).
   The grass side composites the tinted `grass_block_side_overlay` over the
   dirt-ish base.

   The stack is edited at runtime in **Settings → PACKS** (enable/disable,
   reorder); the order persists to `settings.txt` as `pack <name>` lines and
   `Renderer::setResourcePacks` rebuilds the atlas in place. `Application`
   applies the saved stack on startup.
2. **`textures/atlas.png`** — a prebuilt 8×8 atlas (square, side divisible by
   8, rows flipped on load because GL's origin is bottom-left).
3. **Procedural** — deterministic painted fallback so the game runs with no
   assets at all.

Because tiles, not block ids, are the source, faces that share a tile share a
texture (oak/cherry/spruce log tops all use `oak_log_top`).

## HUD

Immediate mode: primitives accumulate into one CPU vertex vector between
`beginFrame`/`flush`, then one `GL_STREAM_DRAW` upload + one draw call.
Vertex = pos(px) + uv + packed `tile | flags<<8` (bit 0 textured, bit 1 dim);
the fragment shader switches between atlas sample, dim black, and bright
white on the flags.

Text is stb_easy_font: authored y-down, flipped into HUD y-up space at emit
time — which reverses winding, so `flush` disables `GL_CULL_FACE` for the
overlay pass rather than special-casing text quads.

Buttons (`Application::button`) are immediate-mode too: draw + hit-test in
one call, hover ring drawn behind, click = `wasMousePressed` edge. Cursor
hit-testing converts window coords → framebuffer coords (y flip + DPI scale)
in `mouseUiPosition`.

F3 toggles the debug overlay (`Application::drawDebugOverlay`): per-line
dim-backed text down the top-left with fps/frame time, world + seed,
position/chunk/facing, velocity + ground state, raycast target, chunk/mesh
counts, worker count, and machine stats — CPU load + RAM (`core/SystemStats`,
Win32 `GetSystemTimes`/`GlobalMemoryStatusEx`/`GetProcessMemoryInfo`, sampled
twice a second) and GPU name + VRAM. VRAM comes from `Renderer::gpuStats`,
which reads `GL_NVX_gpu_memory_info` or `GL_ATI_meminfo` only when the
extension is present (so the GL debug callback stays quiet); absent both, just
the renderer name shows. Lines accumulate in a `std::vector<std::string>`, so
adding one is another `push_back`.

## Debug output

Debug builds request a GL debug context and install `glDebugMessageCallback`
when `GL_KHR_debug` is present (works on 3.3 contexts with modern drivers).
Notifications are filtered; high-severity messages hit an `assert`. Treat any
GL debug error as a bug — the release build runs the same call sequence
without the safety net.
