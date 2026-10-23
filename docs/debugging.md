# Debugging

Tools to build early, and how to approach the hard bugs. The step-by-step Vulkan procedure lives in the `vulkan-debug` skill (`.claude/skills/vulkan-debug/`).

## Infrastructure (M0–M2)
- Validation layers in debug builds, with **synchronization validation**; GPU-assisted validation on demand.
- Debug-utils messenger that breaks into the debugger on errors.
- Names and labels on every GPU object and pass.
- RenderDoc in-app capture hotkey.
- Tracy: CPU and GPU zones, locks, memory.
- Crash dumps (`MiniDumpWriteDump`) and GPU crash diagnostics: Nsight Aftermath, Radeon GPU Detective, `VK_EXT_device_fault`, D3D12 DRED, and engine breadcrumbs (markers written to a host-visible buffer).
- ImGui debug panels: memory per tag, job timeline, render graph, physics debug draw, network stats.
- Input recording and replay with deterministic seeds.
- Testing on at least two GPU vendors.
- Golden-image tests: render fixed scenes headless on every backend and diff against reference images.

## Method for hard bugs
1. Reproduce deterministically (recorded input and seeds).
2. Bisect: `git bisect` on commits, runtime toggles for passes, systems and threads.
3. Shrink the surface: single-threaded jobs, full barriers, plain `malloc` allocator, 1 frame in flight.
4. Compare: other GPU vendor, other backend, Release vs Debug. Vendor- or Release-only bugs usually mean synchronization, uninitialized memory or undefined behavior.
5. Write the regression test before the fix.

## Rendering
See `.claude/skills/vulkan-debug/reference.md` for the Vulkan symptom → cause table. Cross-backend issues:
- Upside-down render targets: inconsistent clip-space handling. Fix only in the per-backend conversion.
- Constant buffer layout differs between HLSL cbuffers and std140: use explicit packing and `static_assert`.
- Format support differs (depth/stencil, BC on mobile): gate through `caps()`.
- Barrier bugs are hidden on Metal and GL, which synchronize automatically. Test Vulkan and D3D12 often.
- Use each API's own tools: RenderDoc (Vulkan, D3D12, GL), PIX (D3D12), Xcode (Metal), Nsight; enable the D3D12 debug layer + GPU-based validation, Metal validation, `GL_KHR_debug`.

## Engine systems

| Area | Typical bugs | Approach |
|---|---|---|
| Memory | Corruption far from the cause; custom allocators hide bugs from ASan | Guard bytes, fill patterns, ASan manual poisoning in allocators, a build flag routing all allocators to `malloc` |
| Threads | Races, deadlocks, false sharing, Release-only bugs | Thread sanitizer, owning-thread asserts, `alignas(64)` hot data, Tracy lock view, single-thread job mode |
| Physics | Jitter, explosions, tunneling, non-determinism | Fixed timestep, warm starting, sub-stepping, CCD; contact debug draw; single-frame replay; `/fp:precise` and stable iteration order |
| Animation | Mangled skeletons, twisting | Check row/column-major and inverse bind matrices; draw the skeleton in bind pose first |
| Audio | Pops, crackles, dropouts | No locks, allocations or IO on the audio thread; lock-free queues; log callback overruns |
| Scripting | Dangling entity references, GC spikes | Generational handles only; incremental GC with a per-frame budget; script stack traces |
| Streaming | Pop-in, hitches, stuck assets | Visualize asset states; per-frame upload budget; assert state transitions |
| Networking | Desync, rubber-banding, latency-only bugs | Simulated latency/loss/jitter; per-tick snapshot logs and diffs; packet stream replay |
| Localization | Missing glyphs, broken RTL, overflowing text | Pseudo-locale from the start |
