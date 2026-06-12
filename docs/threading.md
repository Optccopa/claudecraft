# Threading model

One hard rule: **OpenGL is touched only on the main thread.** Workers produce
plain CPU data; the main thread integrates and uploads it. Everything below
exists to make that cheap and race-free.

## Pipeline

```
main thread                         worker pool (jthread × clamp(hw-2, 2..8))
───────────                         ──────────────────────────────────────────
World::update each frame:
  drain genResults  ◄──────────────  gen job: WorldSave::tryLoad || generate
  unload distant (save modified)
  submit gen jobs (≤16/frame,  ────►
    nearest-first)
  submit mesh jobs (≤16/frame, ────►  mesh job: ChunkMesher::build(snapshot)
    nearest-first)
  drain meshResults ◄──────────────
  return WorldUpdate{uploads, removals}
Application: renderer.uploadChunkMesh / removeChunkMesh   (GL here only)
```

Queues are `ConcurrentQueue<T>` (mutex + cv). Workers block in `waitPop`;
the main thread only ever `tryPop`s. `close()` wakes everyone and drains —
that's the entire pool shutdown protocol.

## Why mesh jobs get snapshots

A mesh job receives a `MeshInput`: an immutable 18×18×256 copy (~84 KB) of
the chunk plus a one-block border from its 8 lateral neighbours, taken on the
main thread (`World::makeMeshInput`, 324 column memcpys). Workers therefore
share **zero** state with the live world — no locks on chunk data, no torn
reads when the player edits mid-build. The copy cost is far below the cost of
fine-grained locking and only spikes during initial load.

A chunk is meshable only when all 8 neighbours are loaded (border faces and
AO need them). Corollary: a chunk whose neighbours aren't in yet stays
invisible — that's why generation distance effectively leads mesh distance.

## Stale-result protocol (mesh revisions)

Every chunk carries two counters:

- `meshRevision` — bumped by any mesh-relevant change: local `setBlock`, and
  edits in a neighbour within 1 block of the shared border (diagonals too,
  for AO).
- `scheduledRevision` — set to `meshRevision` when a mesh job is submitted.

Rules:

1. Schedule when `scheduledRevision != meshRevision` (and neighbours loaded).
2. A job result carries the revision it was built from. On drain, accept only
   if it equals the chunk's *current* `meshRevision`; otherwise drop — rule 1
   already guarantees a fresh job was (or will be) scheduled.
3. Results for unloaded chunks are dropped by the same check.

This makes "rebuild incrementally on block change" safe under any
interleaving of edits, scheduling, and job completion, with no locks.

## Shutdown ordering

The pool member is declared **before** the worlds in `Application`, so worlds
destruct first. But queued jobs capture `this` of their `World` — so
`World::~World` blocks on an in-flight counter:

- `submitTracked` increments the counter *at submit time* (not job start;
  a queued-but-unstarted job is just as dangerous).
- The wrapper decrements + notifies when the job finishes.
- The destructor waits for zero, then saves modified chunks.

If you add a new job type to `World`, route it through `submitTracked` —
never `m_pool.submit` directly.

## Determinism

`TerrainGenerator` and `Noise` are const after construction and safe from any
thread. Tree placement uses a coordinate hash, and trees keep a 2-block chunk
margin specifically so generation never writes across chunk borders — chunks
can generate in any order, in parallel, and produce identical worlds.

## Adding parallel work — checklist

- Results cross back via a `ConcurrentQueue`, drained in `World::update` (or
  an equivalent main-thread point). Never call into GL, GLFW, or `Renderer`
  from the job.
- Job inputs are copies or owned by the job. A `const&` to live world state
  is a race.
- Submit through `submitTracked` if the job touches any `World` member.
- If results can outlive their target (chunk unloads, data edited), version
  them like mesh revisions and drop stale ones on drain.
