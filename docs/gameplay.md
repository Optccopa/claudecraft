# Gameplay: modes, inventory, mining, drops

## Game modes

Chosen on the create-world screen, stored in `world.meta` v3, immutable per
world (`WorldInfo::mode`).

| | Creative | Survival |
|---|---|---|
| Breaking | instant (hold repeats on a cooldown) | hold to mine: progress vs `BlockInfo::hardness` seconds, bar above the crosshair |
| Drops | none | broken block spawns a drop (grass crumbles to dirt, rest drop themselves) |
| Placing | from hotbar stack, never consumes | consumes one from the selected stack |
| Fly (F) | yes | disabled |
| Inventory | full grid + infinite ALL BLOCKS palette | full grid only |

## Sprinting and crouching

Holding the sprint bind (default Left Shift) scales horizontal speed by 1.5×,
both on foot and in fly mode (`PlayerInput::sprint`, `kSprintFactor`). Crouch
takes precedence — a sneaking player stays slow even with sprint held.

Holding the descend bind (default Left Ctrl) on foot (it still flies down in
fly mode) crouches: walk speed drops to 30%, the AABB height shrinks from 1.8
to 1.5 m, and the eye follows. The box shrinks from the top, so `updateCrouch`
lowers the centre to keep the feet planted and only stands back up when
`headroomBlocked` clears. While crouched and grounded, each horizontal axis is
moved through `moveHorizontalAxis`, which reverts the step if it would leave no
ground under the footprint (`groundBelow`) — the classic sneak ledge-stop.

## Inventory

36 `ItemStack` slots (`player/Inventory`): 0–8 are the hotbar, the rest the
grid. Stacks cap at 64. `add` fills matching stacks before empty slots,
hotbar first. Persisted per world to `player.dat` (binary: u16 version, then
36 × {u8 type, u8 count}; unknown versions are discarded, corrupt files
clear). Both modes persist — creative keeps its arranged hotbar. Fresh
creative worlds pre-fill the hotbar with the classic palette.

E toggles the inventory UI (cursor released, movement and mining halt,
physics keeps running). Click moves stacks: pick up / drop / merge-same-type
/ swap; whatever the cursor still holds when the UI closes returns to its
source slot or re-merges. The creative palette rows above the grid are an
infinite source — clicking one puts a full stack on the cursor.

## Drops

`world/Drops` simulates dropped items at the fixed timestep: gravity,
point-collision settle (a 0.3-cube doesn't need swept AABB), magnet toward
the player inside 2.2 blocks, pickup inside 0.9, despawn after 5 minutes or
below the world. Drops are **not persisted** — they live and die with the
session, which keeps them out of the save format.

Mining drops `spawn` at the broken cell. The DROP key (default Q, rebindable)
`throwOut`s one item from the selected hotbar slot — launched along the look
vector with a brief pickup delay so it doesn't magnet straight back. Both
spawn paths share a `pickupDelay` that gates magnet and pickup.

Rendering reuses the chunk shader: per-type unit-cube meshes (built lazily)
drawn with `uScale 0.3` and the drop's cell light passed via `uLightScale`
(vertex light is baked full-bright block-channel so sun/sky scaling doesn't
double-apply). A small sine bob is added on the CPU.

## Adding a block type

1. Append to `BlockType` (never reorder — ids are serialized raw).
2. Add its `BlockInfo` row (tiles, emission, opacity, hardness) and name.
3. Paint its tile in `TextureAtlas` (`tilePixel`).
4. It's automatically minable, droppable, stackable and palette-visible
   (palette skips Air and Bedrock).
