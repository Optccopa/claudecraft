# Build system

No CMake. Two `cl.exe` invocations in `.vscode/tasks.json`, run from a shell
where `cl` is on `PATH` (open VS Code from a Developer Command Prompt, or the
tasks fail with "'cl' is not recognized").

## Anatomy of the build task

```
1. mkdir build\{debug|release}            (cmd "if not exist" guard)
2. cl /w /O2 /c glad.c → build\glad.obj   (only if the obj is missing)
3. cl <all src wildcards> build\glad.obj → claudecraft.exe
```

Step 2 exists because glad.c is C and third-party — it compiles once at /w
(silent) and links forever after. Delete `build/glad.obj` to force a rebuild.

Key flags and why they're there:

| Flag | Why |
|---|---|
| `/std:c++20 /EHsc /permissive- /W4` | project standard; zero warnings is the bar |
| `/MD` (both configs) | the prebuilt `glfw3.lib` is the /MD static flavor; mixing /MDd would emit CRT-mismatch link noise |
| `/DNOMINMAX` | glad.h includes `<windows.h>`, whose `min`/`max` macros break `std::min`/`std::max` |
| `/external:I third_party\... /external:W0` | third-party headers (glm, stb, glad, glfw) are exempt from /W4; our code is not |
| `/MP` | parallel compile across TUs |
| `/Zi /Od` vs `/O2 /DNDEBUG` | debug vs release; debug also requests a GL debug context at runtime |

Sources are wildcarded **per directory** (`src\*.cpp src\app\*.cpp ...`).
cl errors on a wildcard with zero matches, so:

- **Adding a `.cpp` in an existing dir**: nothing to do.
- **Adding a new source dir**: append its wildcard to *both* tasks in
  `tasks.json` and the include path to `c_cpp_properties.json`.
- A dir that loses its last `.cpp` must be removed from the wildcards.

## Dependencies (`third_party/`, vendored, no package manager)

| Lib | Form | Notes |
|---|---|---|
| GLFW 3.4 | headers + `lib/glfw3.lib` | static /MD build from the official `lib-vc2022` binaries; needs `gdi32 user32 shell32` at link |
| Win32 system libs | OS | `Psapi.lib` for `GetProcessMemoryInfo` (the F3 overlay's `core/SystemStats`); the rest of its calls live in kernel32 |
| GLAD | gl 4.5 compat + `GL_KHR_debug`, C source | generated loader; `glDebugMessageCallback` available when the driver exposes KHR_debug even on a 3.3 context |
| GLM 1.0.1 | header-only | include root is `third_party/` itself (`<glm/...>`) |
| stb_image, stb_easy_font | single-header | `STB_IMAGE_IMPLEMENTATION` lives in `TextureAtlas.cpp` only; `core/ZipArchive.cpp` links its raw-inflate (`stbi_zlib_decode_noheader_buffer`) for resource-pack zips |

Adding a header-only dep: drop it in `third_party/<name>/`, add an
`/external:I` entry to both tasks + `c_cpp_properties.json`.

## Run requirements

The exe loads `shaders/*.vert|.frag` relative to the working directory —
run from the project root. `launch.json` sets `cwd` accordingly; if a shader
is missing the program exits with the path in the error message. All *writable*
data (saves, settings, resource packs) instead lives under
`%LOCALAPPDATA%/.claudecraft/` (`core/Paths`), so the working dir only needs
the read-only `shaders/`.

## IntelliSense

`c_cpp_properties.json` pins `compilerPath` to the installed MSVC. The build
doesn't read that file (it uses `PATH`), so a toolset version bump only
breaks squiggles, not builds — update the path when VS updates.

Do **not** build via the C/C++ extension's "Run C++ file" play button — it
generates a single-file task with no include paths. `Ctrl+Shift+B` (default:
build (debug)) or `F5` are the only supported entry points.
