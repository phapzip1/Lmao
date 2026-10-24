# 0011 — Shader hot reload by running slangc

- Status: Accepted
- Date: 2026-10-24

## Context
Shaders are compiled at build time and embedded as SPIR-V (0006). Editing one means rebuilding and restarting the app, which is slow when tuning a shader. Development builds should pick up a saved `.slang` file within a second, without the caller doing anything, and shipping builds must not change.

## Decision
- **Switch:** the CMake option `LMAO_SHADER_HOT_RELOAD` (default ON) defines `LMAO_SHADER_HOT_RELOAD=1` on every target except in Release. With 0, only the embedded SPIR-V is used, and neither the slangc path nor source paths are compiled into the binary.
- **Who remembers sources:** `Render::PipelineLibrary`, not the RHI. `CreateGraphicsPipeline(desc, Shaders::<Name>Source)` keeps an owning copy of the desc and the source path, which `lmao_add_shaders` puts in the generated header. The RHI only gains `Device::ReplacePipeline(handle, desc)`. It builds the new pipeline, swaps it into the same slot and destroys the old one deferred, so callers keep their handles. On failure the old pipeline stays.
- **Change detection:** `PipelineLibrary::Update()` reads the modification times of the watched `.slang` files about twice a second. No OS file-watch API is needed, and this is plenty for a handful of shader files.
- **Imports:** each runtime compile writes a depfile, and its file list decides which sources a change affects. Editing `common/bindless.slang` recompiles every shader that imports it. Before a source's first runtime compile its imports are unknown, so any watched change recompiles it.
- **Compiling:**
  - The library runs `tools/bin/slang` slangc as a child process (`Lmao::RunProcess`) on a background thread (`std::async`, until the M3 job system exists).
  - It uses the same flags as the build: `LMAO_SLANGC_FLAGS` in `cmake/LmaoShaders.cmake` is shared through a generated header.
  - The main thread swaps the new pipelines in during the next `Update()`.
  - On a compile error, the old pipelines stay and slangc's message is logged with file and line.

## Alternatives considered
- **Link the Slang library (`slang.dll` / `libslang.so`) and compile in-process.** Faster: no process start, and the session can be reused. But it adds a large runtime dependency to every development build. Compiler crashes or leaks would also land inside the engine, and the build-time and runtime compilers could drift apart in flags. Worth revisiting if compile latency becomes a problem; the `PipelineLibrary` interface wouldn't change.
- **OS file-watch APIs** (`ReadDirectoryChangesW`, inotify). Instant, but three implementations with their own quirks (editors that save via rename, network drives) for no visible benefit at a 500 ms poll.
- **Reload in the RHI** (the device stores sources). The RHI would have to know about files and the compiler, which it must not (0002).

## Consequences
- A reload takes about 0.4–0.6 s (mostly slangc start-up) plus one pipeline creation on the main thread.
- Only pipelines created through `PipelineLibrary` reload. The ImGui backend still creates its pipeline on the device directly.
- Stages from different source files in one pipeline aren't supported: both stages come from the source given.
- A changed binding layout or push constant struct in C++ still needs a rebuild. Only shader code reloads.
