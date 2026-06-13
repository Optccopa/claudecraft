# Lighting

Two 0..15 light channels per cell, packed into one byte per block in `Chunk`
(`sky | block << 4`): sky light falls from the open sky, block light comes
from emissive blocks (glowstone = 15, `BlockInfo::emission`). The fragment
result is `max(sky · uSkyLight · sunShading, block)` with a 0.03 floor —
caves bottom out near-black, glowstone lights them, and the day/night cycle
scales the whole sky channel through `uSkyLight`.

## Day/night cycle

World time is a [0,1) day fraction (0 sunrise, 0.25 noon, 0.5 sunset, 0.75
midnight), one full day per 10 real minutes. It advances only in the Playing
state, persists per world in `world.meta` v2, and the menu backdrop pins a
fixed late-morning time. `render/Sky::skyStateAt` derives everything frame
by frame — no stored state beyond the time fraction:

- sun direction orbits with elevation `sin(2π·t)`; the shader's diffuse term
  follows it, so faces relight as the sun moves without any remeshing,
- `skyLight` ramps `smoothstep(-0.06, 0.25, elevation)` from a 0.12
  moonlight floor to 1.0 — dawn light precedes the sunrise and midday is
  stable full bright,
- sky/fog colour blends night → day with a horizon glow that peaks as the
  sun crosses the horizon (squared falloff on |elevation|).

Stored light values never change with time of day; only the global scale
does. That's the entire reason the sky/block split exists as two channels.

## Propagation rules

BFS flood fill, identical for both channels except for sunlight's free fall:

- A step into a transparent cell costs 1; into water costs 3; opaque blocks
  stop light entirely.
- Sky light at full strength (15) moving straight down through air costs 0 —
  this single rule is why columns under open sky are bright at any depth and
  anything under cover is not.
- Emissive cells hold their emission in the block channel even though the
  block itself is opaque; light spreads out of them but never into other
  opaque cells.

## Threading split

Mirrors the chunk pipeline (see [threading.md](threading.md)):

| Where | What |
|---|---|
| worker (gen/load job) | `LightEngine::initializeChunkLight` — sky columns top-down, frontier scan, in-chunk BFS for both channels. Pure function of the chunk's blocks; borders treated as lightless. |
| main thread, on integrate | `onChunkLoaded` — compares the 4 lateral seams against loaded neighbours, queues whichever side is ≥2 brighter, floods world-wide to exhaustion. |
| main thread, on edit | `onBlockChanged` — two-phase: darken (removal BFS collecting brighter survivors as re-seeds), then re-flood from seeds, neighbours, and new emission. |

The frontier scan exists for cost: on open terrain whole columns are already
uniformly lit, so only cells bordering darker space (cave mouths, overhangs)
seed the BFS instead of all ~48k lit cells.

The main-thread passes are budgeted — per-cell chunk-map lookups here once
froze the frame whenever several chunks integrated at once:

- All BFS reads/writes go through a one-entry chunk-pointer cache
  (`LightEngine::cachedChunk`), invalidated at the start of each pass
  because chunks may have unloaded since the last one.
- The seam scan accesses the two chunks of each face directly.
- Mesh revisions bump once per touched chunk (plus neighbours) in
  `flushTouched` at the end of a pass, not per BFS write.
- `World::integrateGenerated` paces itself (`kMaxIntegrationsPerFrame`),
  since integration is the one pipeline stage doing main-thread work per
  chunk.

BFS into an unloaded chunk is simply dropped — the seam merge repairs it
when that chunk loads.

## Light is derived data

Chunk files store blocks only (no save-format change): light fully
reconstructs from `initializeChunkLight` + seam merges on load. Player-placed
glowstone relights on load because initialization seeds every emissive cell
it finds.

## Smooth lighting

The mesher bakes per-vertex light next to AO: each face corner averages the
sky and block levels of the up-to-4 non-opaque cells in the face plane
around it (the same cells the AO test reads — opaque ones are skipped so
darkness isn't double-counted with AO). Corner values join the greedy-mesh
mask equality, so quads only merge where light matches; light gradients
fragment meshes exactly as much as they need to.

Vertex packing and the shader contract live in [meshing.md](meshing.md) and
[rendering.md](rendering.md).
