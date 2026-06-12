# Rendering

GL 3.3 core, three shader pairs (`shaders/`): `chunk` (world geometry),
`hud` (2D overlay), `lines` (block highlight). All GL state changes funnel
through `Renderer` and `Hud` on the main thread.

## Frame composition

```
clear (sky color == fog color, so fogged geometry vanishes seamlessly)
opaque chunk pass     cull back, depth test+write
water chunk pass      blend, depth write OFF, far-to-near chunk order
block highlight       inflated line cube (no z-fight against faces)
HUD                   one batched draw; depth + cull OFF, blend ON
```

Per-chunk frustum culling: Gribb/Hartmann plane extraction from the
view-projection matrix, p-vertex AABB test against the full-height chunk box
(16×256×16). Title bar's "N drawn" is the post-cull count.

## Chunk shader contract

Vertex input is the 24-byte `ChunkVertex` (see [meshing.md](meshing.md) for
the packed `data` bitfield and the `fract()` atlas trick). Uniforms:

| Uniform | Source |
|---|---|
| `uViewProj` | `Camera::projection() * Camera::view(eye, yaw, pitch)` |
| `uChunkOrigin` | chunk world origin — vertices stay chunk-local for float precision |
| `uCameraPos` | fog distance reference |
| `uFogStart/uFogEnd` | `fogEnd = renderDistance·16 − 8`, start = 0.65·end |
| `uAlpha` | 1.0 opaque pass, 0.65 water pass |

Lighting is baked in the vertex shader: fixed sun direction, half-lambert
(`0.45 + 0.55·diff`), multiplied by the AO curve `[0.45, 0.65, 0.85, 1.0]`
(non-linear so single-occluder corners stay subtle). No shadow maps; AO plus
face-dependent diffuse is what sells the depth.

## Texture atlas

4×4 tiles, sampled `GL_NEAREST`, **no mipmaps** (hard requirement — see
meshing doc). `TextureAtlas::create` prefers `textures/atlas.png` (square,
side divisible by 4, rows flipped on load because GL's origin is
bottom-left) and falls back to a deterministic procedural atlas. Tile ids
live in `Block.cpp`'s table; tile 0 is bottom-left of the texture. Unassigned
tiles render magenta on purpose.

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
counts, and worker count. It renders through the same HUD batch — adding a
line is just another `std::format` entry in its `lines` array.

## Debug output

Debug builds request a GL debug context and install `glDebugMessageCallback`
when `GL_KHR_debug` is present (works on 3.3 contexts with modern drivers).
Notifications are filtered; high-severity messages hit an `assert`. Treat any
GL debug error as a bug — the release build runs the same call sequence
without the safety net.
