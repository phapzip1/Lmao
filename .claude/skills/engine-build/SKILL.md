---
name: engine-build
description: Configure, build and fix build errors for the Lmao engine. Use when asked to build, compile, configure CMake, or when there are compiler or linker errors.
---

# Building the engine

## Commands
Presets live in `CMakePresets.json` (`windows-debug`, `windows-release`, `linux-debug`). If the file doesn't exist yet, create it before inventing ad-hoc flags.

```powershell
git submodule update --init --recursive      # after a fresh clone or a submodule bump
cmake --preset windows-debug                  # configure (Ninja, clang-cl or MSVC)
cmake --build --preset windows-debug          # build
ctest --preset windows-debug                  # tests
```

- On Windows, Ninja + MSVC needs a Developer environment. If `cl.exe` isn't found, run from a "x64 Native Tools" prompt or call `vcvars64.bat` first.
- If sccache or ccache is installed, the presets set `CMAKE_C_COMPILER_LAUNCHER` / `CMAKE_CXX_COMPILER_LAUNCHER` to it.
- Build output goes to `build/<preset>/`. Read compile errors from the build output, top to bottom; fix the **first** error first.

## Fixing errors
1. Errors inside `third_party/` almost always mean wrong options or a missing define in `third_party/CMakeLists.txt`. Fix the wrapper, **never the submodule sources**.
2. Unresolved externals for single-header libs (VMA, stb, miniaudio): check that exactly one `.cpp` defines the `*_IMPLEMENTATION` macro.
3. A layering error (e.g. `core` including `render`) is a design bug. Move the code down, or invert the dependency with an interface; don't add the include.
4. Vulkan symbols missing: functions come from **volk**. Call `volkInitialize()` / `volkLoadInstance()` / `volkLoadDevice()`; don't link `vulkan-1.lib`.
5. Engine code builds with `/W4 /WX` (or `-Wall -Wextra -Werror`). Fix warnings; don't suppress them unless they come from a third-party header, which should be included as `SYSTEM`.

## After a fix
Rebuild the affected preset. If a header used by many targets changed, run a full build before reporting success.
