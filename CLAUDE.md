# CLAUDE.md

Read this before writing or editing any code. Re-read it before each commit. It is the engineering standard for this project, not a suggestion.

## Prime directive

Write clean, production-ready C++. No AI slop.

This is a real codebase a senior engineer would sign off on — not a demo, not a tutorial, not a wall of generated filler. If a line doesn't earn its place, delete it.

## What "AI slop" means here — forbidden

- **Comments that restate the code.** `i++; // increment i` is noise. Delete it.
- **Dead or commented-out code.** If it's not used, it doesn't exist. Git remembers.
- **Stubs and deferrals.** No `// TODO: implement later`, no empty functions that "will be filled in." Build it now or don't add it.
- **Over-engineering.** No interfaces, factories, or template metaprogramming for a single concrete use case. Abstract only when there are two real callers.
- **Defensive checks for impossible states.** Don't guard against conditions the type system or invariants already rule out. Assert invariants instead.
- **Catch-all error swallowing.** No `catch (...) {}`. Handle the error, propagate it, or let it crash loudly in debug.
- **Copy-paste boilerplate.** Repetition is a refactor signal, not a shortcut.
- **Inconsistent naming or formatting** within the same file or across the project.
- **Filler prose in READMEs/comments** ("In this section, we will explore...").

## C++ conventions — non-negotiable

- **RAII for every resource.** Every OpenGL handle (VAO/VBO/EBO, shader, texture, FBO) lives in a move-only wrapper with a destructor that releases it. No bare `glDelete*` in game logic.
- **Ownership is explicit.** `std::unique_ptr` or value types for owning; raw pointers/references are non-owning views only. Zero manual `new`/`delete`.
- **Rule of five where it matters.** Resource-owning types `= delete` their copy ops and implement move. Everything else stays trivially copyable or follows rule of zero.
- **Const-correctness.** `const` by default — members, locals, parameters (`const T&` for non-trivial types). Methods that don't mutate are `const`.
- **`[[nodiscard]]`** on any function whose return must not be ignored (factories, getters, error codes). **`noexcept`** on moves, swaps, and functions that genuinely can't throw.
- **No globals.** No mutable global state, no singletons. Dependencies are passed in (constructor injection or parameters).
- **Header/impl split.** Declarations in `.hpp`, definitions in `.cpp`. Forward-declare to cut compile times. Include only what you use — no transitive-include reliance.
- **Everything namespaced** under the project namespace.

## Naming

- Types / structs / enums: `PascalCase` (`ChunkMesher`, `BlockType`)
- Functions / methods / locals: `camelCase` (`buildMesh`, `chunkCoord`)
- Member variables: `m_` prefix, `camelCase` (`m_voxels`, `m_dirty`)
- Constants / enum values: `PascalCase` or `kPascalCase` — pick one, stay consistent
- Files: `PascalCase.hpp` / `PascalCase.cpp` matching the primary type

## Comments

- Explain **why**, never **what**. The code says what.
- Non-trivial algorithms (greedy meshing, ambient occlusion, DDA raycast, terrain noise) get a short comment explaining the approach and any non-obvious tradeoff.
- No section-banner ASCII art, no changelog comments, no signatures.

## Concurrency rules

- All OpenGL calls happen on the main thread. Never touch GL from a worker.
- Terrain generation and mesh building run on the worker pool; the main thread only uploads finished vertex data.
- Shared state crosses threads through a thread-safe queue or explicit synchronization — never a bare shared mutable.
- Prefer `std::jthread`, `std::stop_token`, and scoped locks (`std::scoped_lock`) over manual lifetime/lock management.

## Error handling

- Check every shader compile and program link; log the info log and fail.
- Enable `glDebugMessageCallback` in debug builds and treat GL errors as bugs.
- `assert` invariants the code relies on. In release, invalid input degrades gracefully; impossible states stay asserts.
- Fail loudly in debug, never silently.

## Definition of done

A change is done only when:

- It compiles clean at `/W4 /permissive-` with **zero warnings**.
- There are no unused includes, variables, or functions.
- Every owned resource is RAII-managed.
- Every non-trivial algorithm has a rationale comment.
- Nothing on the forbidden list above is present.
- It runs without GL debug errors and holds the 60+ FPS target at a 12-chunk render distance.

## Maintenance

If a convention here proves wrong during the build, update this file in the same change — don't let the code and the standard drift apart.