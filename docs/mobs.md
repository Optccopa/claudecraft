# Mobs

Passive overworld animals: cow, pig, sheep. The subsystem is built to grow —
adding a species is a row in each `Mob.cpp` table plus a model — so it carries
its own type/stat tables rather than hard-coding three animals.

## Data (`Mob.hpp` / `Mob.cpp`)

`MobType` enumerates the species. Three parallel tables key off it:

- `mobInfo(type)` → `MobInfo`: health, hitbox half-width and height, wander
  speed, and a two-slot drop table. Health and hitbox are the
  minecraft.wiki values; walk speed is an approximation of the in-game
  movement-speed attribute in blocks/second (the attribute's raw units don't
  map 1:1 to blocks/s). Sources are noted inline.
- `mobName(type)` → display/id string.
- `mobModel(type)` → `std::span<const MobPart>`: the blocky model as a list of
  axis-aligned boxes in model space (feet-centred origin, +z forward). Each
  `MobPart` carries its centre, pixel size, texture offset, pitch (the torso
  lies down at 90°), layer, a flat fallback colour, and an `inflate` (geometry
  dilation in pixels that leaves UVs unchanged — the sheep wool layer).
- `mobTextureCandidates(type)` / `mobOverlayCandidates(type)` → ordered pack
  asset stems for the skin and its overlay layer. The loader takes the first a
  pack supplies, covering the current biome-variant names
  (`entity/cow/temperate_cow`), the plain modern name, and the flat legacy
  path. The overlay is the sheep's wool (`entity/sheep/sheep_wool`).

| Mob | Health | Hitbox (w×h) | Drops |
|---|---|---|---|
| Cow | 10 | 0.9 × 1.4 | raw beef ×1–3, leather ×0–2 |
| Pig | 10 | 0.9 × 0.9 | raw porkchop ×1–3 |
| Sheep | 8 | 0.9 × 1.3 | raw mutton ×1–2, wool ×1 |

Drop items are `BlockType` ids (`Leather`, `RawBeef`, `RawPorkchop`,
`RawMutton`, and the placeable `Wool` block). The meats and leather are flagged
by `isItem()` so block placement is suppressed — they exist only as drops and
inventory stacks. Counts are the wiki ranges, rolled inclusively on death.

## Simulation (`Mobs.hpp` / `Mobs.cpp`)

`Mobs` owns a flat `vector<MobEntity>`. State per entity: position (feet
centre), velocity, yaw, health, an AI wander timer, ground flag, and a
hurt-flash timer. It **persists** to `mobs.dat` in the world directory
(`saveTo`/`loadFrom`): a small flat record per mob (type, pose, health);
velocity and AI timers are transient and reset on load. `saveTo` removes the
file when no mobs remain.

`update(dt, world, rng)` runs each fixed step:

- **AI:** when the wander timer expires, the mob either idles or picks a random
  heading and walks for a few seconds. A passed-by-reference xorshift32 stream
  (`rng`) keeps decisions deterministic from the world seed without per-call
  `<random>`.
- **Physics:** gravity plus axis-separated AABB collision against solid cells.
  A blocked horizontal axis attempts a one-block auto step (gated on being
  grounded and headroom) so herds climb gentle terrain instead of jamming.
- Mobs that fall through the world floor despawn; everything else is distance-
  culled by `Application`.

`spawnHerd(world, center, type, count, rng)` scatters up to `count` of one
species around `center`, dropping each onto the first grass block with two
cells of clearance (the vanilla passive-spawn requirement). It is a no-op once
the global cap (`kMaxMobs`) is reached.

`attack(origin, dir, reach, damage, rng, loot)` is the melee entry point: it
ray-tests every mob's AABB (slab method), applies `damage` and knockback to the
nearest hit within `reach`, and on a kill rolls the drop table into `loot`
(item type, count, world position) and removes the mob. Returns whether
anything was hit.

## Integration (`Application`)

- **Tick:** `m_mobs.update` runs inside the fixed-step accumulator next to the
  player and drops.
- **Spawning:** `updateMobSpawning` fires on an interval — if the nearby
  population is thin it culls distant mobs and tries one herd ~40 blocks out in
  a random direction. The seed for all mob randomness is `m_mobRng`, reseeded
  per world from its seed.
- **Combat:** a fresh left-click that ray-hits a mob calls `attack` and
  consumes the click (so it doesn't also mine). Hand damage is tuned for
  playable fights, not vanilla's single point. Loot spawns as `Drops` at the
  body, costs attack exhaustion, and is picked up like any other drop.
- **Render:** `drawMobs` samples each mob's cell light and hands the renderer a
  `MobDraw` list after the drops pass.

## Rendering (`Renderer::drawMobs`, `shaders/entity.*`)

The entity shader is separate from the chunk path. `render()` caches the
frame's view-projection, fog and sun so `drawMobs` can reuse them without
re-plumbing the call site. Each mob applies one transform (translate to feet,
rotate by yaw).

Textured mobs use a per-type mesh built once in `loadMobTextures`: each box
unwraps into the pack skin like a Minecraft model part (pixel size and texture
offset over the skin's real dimensions). Skins are loaded *before* the mesh is
built because UVs normalize by the actual size — the classic layout is 64x32,
but current cow/pig skins are 64x64 (same offsets, taller sheet), so a wrong
assumption samples the empty half and the lower faces vanish. The sheep draws
two layers: the `sheep.png` skin and the dilated `sheep_wool.png` wool overlay.

Mobs whose skin no pack supplies fall back to flat-shaded coloured boxes (the
shared centred unit cube, `uColor` per part). Lighting is a lambert against the
sun direction plus ambient, times the mob's sampled **torso-cell** light (the
feet cell is the solid block underfoot and would render the mob black); day/
night arrives only through that cell light, not double-applied. Fog matches the
world pass; the hurt flash lerps toward red in the fragment shader.
