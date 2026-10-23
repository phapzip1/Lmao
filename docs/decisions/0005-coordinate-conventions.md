# 0005 — Coordinate conventions and reverse-Z

- Status: Accepted
- Date: 2026-10-23

## Context
Graphics APIs differ in clip-space Y direction, depth range and texture origin. Handling the differences in many places causes upside-down images and depth bugs.

## Decision
- Engine-wide: right-handed world, Y-up, column-major matrices, depth range 0..1, UV origin top-left.
- **Reverse-Z** with `D32_SFLOAT`: clear depth to 0, compare `GREATER`.
- Each backend corrects clip space in exactly one place: Vulkan uses negative viewport height; OpenGL uses `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)`; D3D12 and Metal need nothing.
- Projection matrices are computed once in engine math, never per backend.

## Consequences
- Much better depth precision at distance (Nathan Reed, "Depth Precision Visualized").
- The negative viewport flips triangle winding in Vulkan; cull mode and front face must account for it.
- Depth clear values and compare ops must never be hard-coded to the forward-Z defaults.
