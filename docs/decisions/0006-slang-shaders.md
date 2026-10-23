# 0006 — Slang shaders with a fixed binding convention

- Status: Accepted
- Date: 2026-10-23

## Context
Several backends need several shader formats (SPIR-V, DXIL, MSL, GLSL, WGSL). Cross-compilation only works if resource bindings follow a convention each backend can map mechanically.

## Decision
- Write shaders in **Slang**; compile offline in the asset cooker to every target backend; compile at runtime only for hot reload.
- Binding convention:
  - push/root constants (≤128 bytes): draw index, material index, buffer addresses
  - set/space 0: global bindless arrays (textures, storage images, samplers, buffers)
  - set/space 1: per-frame constants
- Pipeline layouts come from reflection.
- No `vec3` in GPU buffers; C++ mirrors of GPU structs have `static_assert`ed sizes and offsets.

## Consequences
- The Slang compiler is a large prebuilt dependency in `tools/bin/`, not a submodule (see 0004).
- Fallback if Slang becomes a problem: HLSL with DXC (DXIL, SPIR-V) plus SPIRV-Cross (MSL, GLSL).
