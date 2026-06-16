# CLAUDE.md

Read before writing or editing code. Re-read before each commit. This is the engineering standard, not a suggestion.

Refer to .claude\skills\minecraftimprov\SKILL.md When writing any new code

## Prime directive

Write clean, production-ready C++. No AI slop. This is a real codebase a senior engineer would sign off on. If a line doesn't earn its place, delete it.

## IMPORTANT NOTES
- **Textures** Whenever implementing new textures you must attempt to use one found in the minecraft texture packs before attempting to create your own (Fallbacks from packs are still needed but go for packs first)
## Forbidden — "AI slop"

- **Comments that restate code.** `i++; // increment i` is noise.
- **Dead or commented-out code.** Git remembers.
- **Stubs and deferrals.** No `// TODO: implement later`, no empty placeholder functions. Build it now or leave it out.
- **Over-engineering.** No interfaces, factories, or template metaprogramming for one concrete use. Abstract only with two real callers.
- **Defensive checks for impossible states.** Assert invariants instead of guarding what the type system already rules out.
- **Catch-all error swallowing.** No `catch (...) {}`. Handle, propagate, or crash loudly in debug.
- **Copy-paste boilerplate.** Repetition is a refactor signal.
- **Inconsistent naming or formatting** within a file or across the project.
- **Filler prose** in READMEs or comments.

## C++ conventions — non-negotiable

- **RAII for every resource.** Every GL handle (VAO/VBO/EBO, shader, texture, FBO) lives in a move-only wrapper that releases it. No bare `glDelete*` in game logic.
- **Explicit ownership.** `std::unique_ptr` or values for owning; raw pointers/references are non-owning views only. Zero manual `new`/`delete`.
- **Rule of five where it matters.** Resource-owning types `= delete` copy ops and implement move. Everything else follows rule of zero.
- **Const-correctness.** `const` by default — members, locals, params (`const T&` for non-trivial types). Non-mutating methods are `const`.
- **`[[nodiscard]]`** where the return must not be ignored; **`noexcept`** on moves, swaps, and functions that can't throw.
- **No globals, no singletons.** Inject dependencies via constructor or parameters.
- **Header/impl split.** Declarations in `.hpp`, definitions in `.cpp`. Forward-declare to cut compile times; include only what you use.
- **Everything namespaced** under the project namespace.

## Naming

- Types / structs / enums: `PascalCase` (`ChunkMesher`, `BlockType`)
- Functions / methods / locals: `camelCase` (`buildMesh`, `chunkCoord`)
- Members: `m_` + `camelCase` (`m_voxels`, `m_dirty`)
- Constants / enum values: `PascalCase` or `kPascalCase` — pick one, stay consistent
- Files: `PascalCase.hpp` / `.cpp` matching the primary type

## Comments

- Explain **why**, never **what**.
- Non-trivial algorithms (greedy meshing, ambient occlusion, DDA raycast, terrain noise) get a short note on approach and any non-obvious tradeoff.
- No ASCII banners, changelogs, or signatures.

## Concurrency

- All GL calls on the main thread — never from a worker.
- Terrain gen and mesh building run on the worker pool; the main thread only uploads finished vertex data.
- Shared state crosses threads via a thread-safe queue or explicit sync — never a bare shared mutable.
- Prefer `std::jthread`, `std::stop_token`, and `std::scoped_lock` over manual lock/lifetime management.

## Error handling

- Check every shader compile and program link; log the info log and fail.
- Enable `glDebugMessageCallback` in debug; treat GL errors as bugs.
- `assert` invariants. In release, invalid input degrades gracefully; impossible states stay asserts.
- Fail loudly in debug, never silently.

## Documentation

`docs/` holds one doc per subsystem; `docs/README.md` maps code areas to docs.

- **Every code change updates its matching doc in the same change.** If the doc's stated behavior, formats, flags, or invariants are no longer exactly true, fix them first.
- New subsystems get a new doc plus an index row.
- Docs state behavior, invariants, and the *why* — no filler, no tutorials, no restating code. Same bar as comments.
- Format changes (save files, vertex layouts, packed bitfields) must update the corresponding tables — those tables are the spec.

## Testing — required before declaring done

After any feature or visible behavior change, **stop and ask the user to test it in-game.** Do not declare the task complete based on build success or log output alone. Log output confirms compilation; only human eyes in the running game confirm correctness.

Ask the user to:
1. Build and run (**Provide a one line command**)
2. Exercise the specific feature or affected area.
3. Report what they see.

Wait for their confirmation before closing the task. If they find a problem, fix it and ask them to test again.

## Definition of done

A change is done only when:

- It compiles clean at `/W4 /permissive-` with zero warnings.
- No unused includes, variables, or functions.
- Every owned resource is RAII-managed.
- Every non-trivial algorithm has a rationale comment.
- Nothing on the forbidden list is present.
- The matching doc in `docs/` is still accurate.
- It runs without GL debug errors and holds 60+ FPS at a 12-chunk render distance.
- **The user has tested it in-game and confirmed it works.**

## Maintenance

If a convention here proves wrong during the build, update this file in the same change — don't let code and standard drift.