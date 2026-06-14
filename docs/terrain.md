# Terrain generation

`TerrainGenerator` is const after construction and runs on worker threads.
Everything derives from the world seed — same seed, same world, regardless of
chunk generation order (see determinism notes at the bottom).

## Noise instances

Six `Noise` tables, all seeded from the world seed XOR a fixed salt:

| Field | Use |
|---|---|
| `m_heightNoise` | 2D fBm height map |
| `m_biomeNoise` | 2D continental field: mountains high, ocean basins low |
| `m_moistureNoise` | 2D field splitting lowlands into desert/plains/forest |
| `m_temperatureNoise` | 2D field carving out taiga (cold) and cherry grove (warm+wet) |
| `m_caveNoiseA/B` | two independent 3D fields for cave carving |
| `m_oreNoise` | one shared 3D field for all ore types |

## Biomes and surface

One continental value `c = fbm(wx·0.0011, 2 oct)` drives both height
extremes, so mountains and oceans can never overlap and coastlines pass
through plains height for free:

```
mountain = smoothstep(0.05, 0.42, c)
ocean    = smoothstep(0.16, 0.40, -c)
height   = lerp(lerp(66 + n·5, 84 + n·52, mountain), 46 + n·5, ocean)
                                          n = fbm(wx·0.008, 4 oct)
```

Classification (`classify`, used by `biomeAt` and generation): ocean if
`ocean > 0.5`, else mountains if `mountain > 0.5`. Lowlands then split by
temperature first — taiga (`t < −0.28`), cherry grove (`t > 0.30` and
`m > 0.05`) — and finally moisture: desert (`m < −0.22`), forest
(`m > 0.28`) or plains. Thresholds are the tuning knobs; the smoothstep
windows control how wide the blend zones are.

Per-biome differences:

| Biome | Surface | Trees per 1000 columns | Tree |
|---|---|---|---|
| Plains | grass | 8 | oak |
| Forest | grass | 55 | oak |
| Taiga | grass | 45 | spruce: tall conical skirt |
| Cherry grove | grass | 30 | cherry: broad radius-3 pink canopy |
| Desert | sand top + sand to `h−4` | 0 | — |
| Mountains | grass, snow ≥ 108 | 0 | — |
| Ocean | sand floor (~y 42–51), water to 62 | 0 | — |

Sea level 62. Column layering: bedrock at y 0, stone up to `h−4`, dirt (or
sand) to `h−1`, then the biome top block; beaches stay sand for `h ≤ 63`
regardless of biome. Water fills (h, 62] where the ground dips below sea
level.

## Caves

Carved in `carveAndSeed` after the column is filled: a cell becomes air when
**both** 3D fields are near zero:

```
|fbm3_A(x·0.030, y·0.045, z·0.030, 2 oct)| < 0.085   &&  same for B
```

One field's zero set is a 2D surface through space; intersecting two gives 1D
winding tunnels ("spaghetti caves") rather than blobs. The second field is
only evaluated when the first passes — half the cost on most cells.

Constraints:

- y range [6, surface]; bedrock and water never carved.
- Ocean floors keep ≥4 solid blocks (`surface ≤ 63` caps carving at
  `surface−4`): a pocket under a water column has no fluid sim to drain it
  and renders as a floating water ceiling.
- Carving may remove the surface block — that's a cave entrance, intended.
  Tree placement runs later and checks the grass block still exists, so no
  floating trees.

Tuning knobs: threshold 0.085 ≈ tunnel radius; frequency 0.030 ≈ tunnel
spacing; the 1.5× vertical frequency squashes tunnels flatter than tall.

## Ores

Stone cells (only stone — never dirt/sand) sample one shared field
`fbm3(·0.16, 2 oct)` and pass nested gates, rarest first:

| Ore | Field > | Below y | Effect of sharing one field |
|---|---|---|---|
| Diamond | 0.70 | 16 | vein cores at depth |
| Gold | 0.64 | 34 | shells around diamond |
| Iron | 0.58 | 64 | shells around gold |
| Coal | 0.52 | 110 | outermost, most common |

One noise lookup per stone cell buys clustered veins with natural rarity
ordering — no per-ore noise, no scatter pass. Raising a threshold makes that
ore rarer; the depth gate moves where it appears.

## Trees

Densities and species per the biome table, on intact grass, gated by
`hash(wx, wz, seed) % 1000`. Trees keep a margin inside the chunk so the
canopy never crosses a chunk border (2 blocks, 3 for cherry's wider crown) —
the price of order-independent parallel generation (no cross-chunk structure
writes, no pending-block queues).

## Surface scatter (cactus, tall grass)

A second pass over every column (no margin needed — both are one cell wide and
grow straight up) places, on the cell above an above-water surface that the
tree pass left empty:

- **Cactus** on desert sand: a 1–3 block column, ~6 per 1000 columns. Hash
  salt `seed ^ 0x000CAC75`; the high hash bits pick the height so it's
  independent of the placement roll.
- **Tall grass** (`TallGrass`, a cross-billboard plant) on any grass surface,
  per 1000 columns: forest 130, cherry grove 90, plains 60, taiga 45,
  mountains 25, else 40. Hash salt `seed ^ 0x6A557000`.

Both use independent `hashCoords` salts so they don't correlate with trees or
each other, keeping the same-seed-same-world guarantee.

## Determinism rules

When extending generation, preserve these or saves/multiworld break:

- No RNG state across calls — only pure functions of (coords, seed). `Noise`
  tables are fixed after construction; one-off decisions use `hashCoords`.
- A chunk's blocks depend only on its own columns. Anything that would span
  chunks (bigger structures) needs margins like trees, or a real
  cross-chunk seeding pass — don't write into neighbour chunks from a
  generator running on a worker.
- `surfaceHeight` is the pre-cave height: spawn placement and the menu
  camera use it, and caves may legitimately open right under spawn.
