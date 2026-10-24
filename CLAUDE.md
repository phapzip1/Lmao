# Lmao Engine

C++20 game engine. Vulkan first (D3D12, Metal, WebGPU later), GLFW as one platform backend. Windows primary, Linux in CI.

## Layering (lower never includes higher)
`core` → `rhi` → `scene`/`resource` → systems (`render`, `physics`, `audio`, `anim`, `net`, `loc`) → gameplay (`script`, `ai`, `cinematics`) → `tools`.

## Hard rules
- **Dependencies are git submodules in `third_party/`.** No vcpkg, Conan or FetchContent. Never edit files inside a submodule. Use the `add-dependency` skill.
- **No backend types outside their backend.** Backends live in `engine/rhi/rhis/<api>/`: `vulkan.h` only in `rhis/vulkan/`, `d3d12.h` only in `rhis/d3d12/`. The renderer talks to `Rhi::Device` / `Rhi::CommandList` only.
- **Handles, not pointers** for resources, entities and assets (index + generation).
- Render passes declare reads and writes; the render graph inserts barriers. No hand-written barriers in renderer code.
- The audio thread never locks, allocates or does IO.

## Conventions
- Naming: namespaces, types, public functions, public members and public constants are PascalCase (`Lmao::Log::SetLevel`, `Config::FilePath`, `Math::Pi`). Private members, file-local (anonymous-namespace) functions and constants, parameters and locals are snake_case. Macros are `LMAO_UPPER_CASE`.
  - Classes: member functions follow the same rule (public PascalCase, private/protected snake_case). Private/protected data members use `m` + PascalCase (`mInstance`). Static data members of any access use `s` + PascalCase (`sInstance`). Public non-static data members stay PascalCase. Plain structs don't use the `m`/`s` prefixes.
- Right-handed world, Y-up, column-major matrices, depth range 0..1, **reverse-Z** (clear 0, compare GREATER), `D32_SFLOAT`.
- Vulkan Y flip via negative viewport height; GL uses `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)`.
- Strings are UTF-8 everywhere; convert only at OS API boundaries.
- Shaders: Slang. Binding convention: push constants (≤128 B), set0 = global bindless, set1 = per-frame.
- No `vec3` in GPU buffers; `static_assert` struct sizes and offsets.
- Name every GPU object and wrap passes in debug markers.

## Build
See the `engine-build` skill. CMake ≥ 3.25 + Ninja, presets in `CMakePresets.json`.

## Docs
- `docs/architecture.md`: overall design, RHI, Vulkan backend, platform layer, libraries.
- `docs/decisions/`: one record per decision. Add a new record instead of rewriting an accepted one.
- `docs/milestones.md`: current milestone, exit criteria, status. Keep it in sync with real progress.
- `docs/debugging.md`: debugging infrastructure and techniques.
