# Chunk meshing

`ChunkMesher::build` turns a padded block snapshot into two index-triangle
meshes per chunk: opaque and water (translucent, drawn in a separate pass).
All of it runs on worker threads against an immutable `MeshInput`.

## Greedy meshing

For each of the 6 face directions, sweep the chunk in slices perpendicular to
the normal. Each slice builds a mask of *visible* faces, then merges equal
mask cells into maximal rectangles (grow right, then grow down while the
whole row matches). One merged rectangle = one quad = 4 vertices.

Visibility rules:

- Opaque block (incl. leaves): face shown when the neighbour in the normal
  direction is **not** opaque (air or water — terrain is visible through
  water).
- Water: face shown only against **air**, so a lake is a single translucent
  surface instead of stacked interior faces.

Two cells merge only if their *entire* mask entry is equal — block type, the
four packed AO corner values, and the per-corner smoothed light. Merging
across different shading would smear it; this is the standard
correctness/coverage tradeoff for greedy meshing with baked lighting.

## Ambient occlusion

Per face corner, the classic 3-neighbour test (side1, side2, diagonal — all
sampled one step along the face normal):

```
ao = (side1 && side2) ? 0 : 3 - (side1 + side2 + diagonal)     // 0..3
```

Each quad is split along the diagonal joining the brighter corner pair
(`ao0 + ao2 >= ao1 + ao3` picks the split) so interpolation doesn't streak
across a dark corner — without this, identical geometry shades differently
depending on triangle orientation.

The padded snapshot exists for exactly these reads: border faces and corner
AO sample up to 1 block outside the chunk in x/z (y out of range reads Air).

## Face bases and winding

`kFaces` defines, per direction, the two in-plane axes chosen so
`uAxis × vAxis == normal` — emitted quads are CCW from outside, matching
`GL_CULL_FACE`/`GL_BACK`. It also picks which world axes feed the texcoords
(`texRight`, `texUp`) so side-face textures stand upright (v follows world
y). Negative x/z faces are horizontally mirrored as a consequence; invisible
with noisy tiles — revisit only if a directional texture is ever added.

## Vertex format (12 bytes, `ChunkVertex`)

Positions are in **sixteenths of a block** so non-full shapes (cactus, future
slabs) can place sub-block vertices; full-cube corners are multiples of 16.
Positions and UVs pack losslessly into two `uint32`s (`packChunkVertex`):

| Offset | Field | Notes |
|---|---|---|
| 0 | `uint32 pos` | bits 0–8 x, 9–17 z (both 0..256 = 0..16 blocks), 18–30 y (0..4096 = 0..256 blocks); chunk-local sixteenths, shader divides by 16 then adds `uChunkOrigin` |
| 4 | `uint32 uv` | bits 0–15 u, 16–31 v — **block-space**, runs 0..width / 0..height across a merged quad |
| 8 | `uint32 data` | bits 0–7 atlas tile, 8–9 this corner's AO, 10–12 normal index, 13–16 sky light, 17–20 block light |

All three are consumed via `glVertexAttribIPointer` (integer attributes); the
vertex shader unpacks position/UV with shifts. The dropped-item cubes
(`makeDropMeshData`) reuse this format with a 0..16 (one-block) cube and the
shader's `uCenter`=0.5 to recenter it before scaling.

Per-corner light is smoothed (4-cell average) and part of the mask equality —
see [lighting.md](lighting.md). Quads only merge where AO **and** both light
channels match at every corner. With the smooth-lighting setting off,
corners take the face cell's light with full AO instead — flat shading that
merges far better (the setting toggles `MeshInput::smoothLighting` and
remeshes the world).

## The fract() atlas trick

Greedy quads span many blocks but one atlas tile, so per-block tiling can't
come from wrap modes. Instead UVs are emitted in block units and the fragment
shader computes

```
atlasUv = tileOrigin + fract(blockUv) / tilesPerRow
```

This only works because the atlas is sampled **nearest with no mipmaps** —
mips would average across the `fract` discontinuity and across neighbouring
tiles (bleed). If mipmapping is ever wanted, the atlas must become a texture
array instead. Same constraint is why `TextureAtlas` never calls
`glGenerateMipmap`.

## Non-cube shapes (`BlockShape`)

A block's `BlockInfo::shape` decides how it meshes. Only `Cube` goes through
the greedy directional passes; everything else is skipped there (`isFullCube`)
and emitted by one final `meshSpecialShapes` scan into the **opaque** mesh.
All non-cube blocks are flat-lit from their own cell with full AO, packed via
the shared `emitShapeQuad`/`shapeData` helpers, and are non-opaque (`occludes`
is false) so neighbouring cubes still draw the faces they share with them.

- **`Cross`** (tall grass): two diagonal quads spanning the cell, each emitted
  **double-sided** (both index windings) so the blades show from every angle
  under back-face culling. A `+y` normal lets the sky term pick up overhead
  sun. The chunk fragment shader `discard`s texels with alpha < 0.5, so the
  transparent area of the `short_grass` tile cuts out cleanly with depth
  writes on and no sorting.
- **`Box`** (cactus): a full-height column inset `BlockInfo::inset` sixteenths
  on its four sides — the sub-block precision the sixteenth vertex grid exists
  for. Side faces always emit; the top/bottom cap is culled against an
  identical box or any opaque neighbour above/below, so a stacked column reads
  as one piece and the bottom never z-fights the ground it rests on. The
  selection wireframe scales to the box (`FrameParams::highlightMin/Max`);
  collision still treats `Box` blocks as a full cell.

## Water pass

Water quads go to the second mesh with AO forced to full (0xFF mask) — AO on
a transparent fluid reads as dirt stains. The renderer draws water after all
opaque geometry, sorted far-to-near by chunk distance, with blending on and
**depth writes off** (depth test stays on): near surfaces must not
depth-reject far ones behind them.
