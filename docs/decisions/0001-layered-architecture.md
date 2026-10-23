# 0001 — Strict layered architecture

- Status: Accepted
- Date: 2026-10-23

## Context
The engine has 13+ subsystems (rendering, physics, audio, animation, scripting, AI, networking, streaming, memory, threads, localization, scene graph, cinematics). Without dependency rules they become tangled, and every change ripples across the codebase.

## Decision
Organize the engine into layers, and forbid a lower layer from including a higher one:

`core` → `rhi` → `scene`/`resource` → systems (`render`, `physics`, `audio`, `anim`, `net`, `loc`) → gameplay (`script`, `ai`, `cinematics`) → `tools`.

Each subsystem is its own CMake target, linking only to targets in its own or lower layers.

## Consequences
- CMake target dependencies enforce the rule at build time.
- When a lower layer needs something from a higher one, the fix is to move the code down or invert the dependency through an interface or callback.
- Follows the runtime architecture in Jason Gregory's *Game Engine Architecture*.
