# Lighting

Two 0..15 light channels per cell, packed into one byte per block in `Chunk`
(`sky | block << 4`): sky light falls from the open sky, block light comes
from emissive blocks (glowstone = 15, `BlockInfo::emission`). The fragment
result is `max(sky · uSkyLight · sunShading, block)` with a 0.03 floor —
caves bottom out near-black, glowstone lights them, and the sky channel has
a global scale ready for day/night.

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

`World::setLightPacked` bumps mesh revisions of the owning chunk and any
border-adjacent neighbours, so relighting automatically queues remeshes
through the existing revision protocol. BFS into an unloaded chunk is simply
dropped — the seam merge repairs it when that chunk loads.

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
