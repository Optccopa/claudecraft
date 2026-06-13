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

Greedy quads land on block edges, so every coordinate is a small exact
integer — positions and UVs pack losslessly into two `uint32`s (`packChunkVertex`):

| Offset | Field | Notes |
|---|---|---|
| 0 | `uint32 pos` | bits 0–4 x, 5–9 z (both 0..16), 10–18 y (0..256); chunk-local, shader adds `uChunkOrigin` |
| 4 | `uint32 uv` | bits 0–15 u, 16–31 v — **block-space**, runs 0..width / 0..height across a merged quad |
| 8 | `uint32 data` | bits 0–7 atlas tile, 8–9 this corner's AO, 10–12 normal index, 13–16 sky light, 17–20 block light |

All three are consumed via `glVertexAttribIPointer` (integer attributes); the
vertex shader unpacks position/UV with shifts. The dropped-item cubes
(`makeDropMeshData`) reuse this format with a 0..1 unit cube and the shader's
`uCenter`=0.5 to recenter it before scaling.

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

## Water pass

Water quads go to the second mesh with AO forced to full (0xFF mask) — AO on
a transparent fluid reads as dirt stains. The renderer draws water after all
opaque geometry, sorted far-to-near by chunk distance, with blending on and
**depth writes off** (depth test stays on): near surfaces must not
depth-reject far ones behind them.
