# 0009 — OpenGL is last priority; WebGPU for the web

- Status: Accepted
- Date: 2026-10-23

## Context
OpenGL was listed as a possible backend. It fits a modern explicit RHI poorly: single-threaded, implicit state, no real pipeline objects.

## Decision
Backend order: Vulkan → D3D12 → Metal (MoltenVK as a stopgap) → WebGPU for the web → OpenGL only if a concrete need appears (old hardware), with a reduced feature tier.

## Consequences
- If an OpenGL backend is built, command lists and pipelines are emulated, and GL 4.5 `glClipControl` is required for the conventions in 0005.
- Web support goes through WebGPU, which matches the RHI model.
