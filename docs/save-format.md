# Save format

## Data directory

All game data lives under `%LOCALAPPDATA%/.claudecraft/` (`core/Paths`,
resolved from `LOCALAPPDATA`, then `USERPROFILE/AppData/Local`, then the
working directory): `saves/`, `settings.txt` and `texture_packs/`. On startup
`paths::migrateLegacy` moves any of those three left in the working directory
(older builds wrote there) into the data dir, so existing worlds survive the
move. Only `shaders/` is still read from the working directory.

## World directories

Each world is a directory `saves/<name>/` (under the data dir) containing a
`world.meta`, an optional `player.dat`, plus one chunk file per modified
chunk. `world.meta` is line-oriented text, version 3:

```
claudecraft-world 3
<seed>
<timeOfDay>          (day fraction 0..1; v1 files omit it, defaulting 0.05)
<mode>               ("creative" | "survival"; pre-v3 defaults creative)
```

Readers accept v1–v3; writers emit v3. The meta is rewritten on quit (it
carries the world clock), not on every save.

`player.dat` holds the inventory: u16 version (1), then 36 × {u8 blockId,
u8 count} in slot order (hotbar first). Unknown versions or corrupt entries
discard the file — the inventory resets rather than corrupting (see
[gameplay.md](gameplay.md)).

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

## Chunk file layout (version 2)

All values little-endian (native x64; no cross-platform ambition).

```
offset  size  field
0       4     magic   0x4B484343  ("CCHK" in file order)
4       2     version 2
6       2     reserved (0)
8       ...   block RLE until exactly 65536 blocks are emitted:
              u16 runLength   (1..65535, never 0)
              u8  blockId     (must be < BlockType::Count)
...     ...   fluid RLE until exactly 65536 levels are emitted (v2+ only):
              u16 runLength   (1..65535, never 0)
              u8  level       (0 none, 1..7 flowing, 8 source; must be ≤ 8)
```

Blocks stream in chunk index order `(x·16 + z)·256 + y` — y fastest, which is
what makes RLE effective (long vertical runs of stone/air/water). The fluid
section uses the same order and is almost all zeros (only water columns carry
levels), so it RLE-compresses to near nothing.

Light is never stored: it's derived data, recomputed on load (see
[lighting.md](lighting.md)). Fluid **levels are** stored as of v2 so flowing
water (level 1–7) survives a reload instead of draining back to its sources.
Version 1 files (no fluid section) still load — their water blocks are primed
to sources and the sim re-derives any flow.

## Validation and failure policy

`WorldSave::tryLoad` returns `nullptr` (chunk regenerates from the seed) on:

- missing file
- magic or version mismatch (accepts versions 1..2)
- zero-length run, run overflowing the 65536 total, block id ≥ `Count`, or
  fluid level > 8
- short read

A corrupt save is logged and *silently regenerated* — losing one chunk's
edits beats crashing the load. Saves are plain truncate-and-write; if
atomicity ever matters (crash mid-save), switch to write-temp-then-rename.

## When saves happen

- Chunk evicted from the streaming ring (`World::unloadDistant`)
- `World::saveModified` — on window close and in `World::~World`
- `markModified` is set only by real edits (`setBlock`), not by generation
  or by neighbour-triggered mesh invalidation

Beyond chunk files, a world directory holds `world.meta` (seed, mode, player
pose + vitals), `player.dat` (inventory), and `mobs.dat` (the live herd —
type/pose/health per mob, written on logout, removed when empty). Mobs are
otherwise transient: spawning repopulates them and they aren't tied to chunks.

## Versioning rules

Any change to the layout above **must** bump `kVersion`. Old versions are
rejected (regenerate), not migrated — acceptable while the format is young;
write a migration path only when player worlds are worth preserving.

Adding a new `BlockType` is backward-compatible (ids are append-only —
**never reorder or remove enum values**, they're serialized as raw u8).
Removing one is not; that's a version bump plus a remap on load.

Block ids in use: Air 0, Stone 1, Dirt 2, Grass 3, Sand 4, Water 5, Wood 6,
Leaves 7, Plank 8, Snow 9, Bedrock 10, CoalOre 11, IronOre 12, GoldOre 13,
DiamondOre 14, Glowstone 15, CherryWood 16, CherryLeaves 17, SpruceWood 18,
SpruceLeaves 19, Cactus 20, TallGrass 21, Wool 22, Leather 23, RawBeef 24,
RawPorkchop 25, RawMutton 26.

Leather/RawBeef/RawPorkchop/RawMutton (23–26) are item-only ids (`isItem`):
they are never placed in the voxel grid, so they never appear in a chunk save —
they exist only as drops and inventory stacks. Mobs themselves are not
persisted at all (see [mobs.md](mobs.md)).

`world.meta` has its own header version (`claudecraft-world 1`), independent
of the chunk format version — bump it if the meta gains fields.
