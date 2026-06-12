# Save format

## World directories

Each world is a directory `saves/<name>/` containing a `world.meta` plus one
chunk file per modified chunk. `world.meta` is two text lines:

```
claudecraft-world 1
<seed>
```

`worldlist::list` scans `saves/` for directories with a parseable meta;
legacy pre-meta directories named `world_<seed>` are recognised by name.
Anything else (e.g. the menu backdrop's never-written `saves/.menu` path) is
ignored. Directory names are sanitised display names (`[A-Za-z0-9_-]`,
spaces → `_`, ≤24 chars) and deduplicated with `_2`, `_3`, … suffixes —
the name is the world's identity, the seed lives only in the meta.

## Chunk files

Only modified chunks touch disk: `c_<x>_<z>.bin` (chunk coords, may be
negative). Directories are created lazily on first save, so never-edited
worlds leave nothing behind.

## Chunk file layout (version 1)

All values little-endian (native x64; no cross-platform ambition).

```
offset  size  field
0       4     magic   0x4B484343  ("CCHK" in file order)
4       2     version 1
6       2     reserved (0)
8       ...   RLE runs until exactly 65536 blocks are emitted:
              u16 runLength   (1..65535, never 0)
              u8  blockId     (must be < BlockType::Count)
```

Blocks stream in chunk index order `(x·16 + z)·256 + y` — y fastest, which is
what makes RLE effective (long vertical runs of stone/air/water).

## Validation and failure policy

`WorldSave::tryLoad` returns `nullptr` (chunk regenerates from the seed) on:

- missing file
- magic or version mismatch
- zero-length run, run overflowing the 65536 total, or block id ≥ `Count`
- short read

A corrupt save is logged and *silently regenerated* — losing one chunk's
edits beats crashing the load. Saves are plain truncate-and-write; if
atomicity ever matters (crash mid-save), switch to write-temp-then-rename.

## When saves happen

- Chunk evicted from the streaming ring (`World::unloadDistant`)
- `World::saveModified` — on window close and in `World::~World`
- `markModified` is set only by real edits (`setBlock`), not by generation
  or by neighbour-triggered mesh invalidation

## Versioning rules

Any change to the layout above **must** bump `kVersion`. Old versions are
rejected (regenerate), not migrated — acceptable while the format is young;
write a migration path only when player worlds are worth preserving.

Adding a new `BlockType` is backward-compatible (ids are append-only —
**never reorder or remove enum values**, they're serialized as raw u8).
Removing one is not; that's a version bump plus a remap on load.

Block ids in use: Air 0, Stone 1, Dirt 2, Grass 3, Sand 4, Water 5, Wood 6,
Leaves 7, Plank 8, Snow 9, Bedrock 10, CoalOre 11, IronOre 12, GoldOre 13,
DiamondOre 14.

`world.meta` has its own header version (`claudecraft-world 1`), independent
of the chunk format version — bump it if the meta gains fields.
