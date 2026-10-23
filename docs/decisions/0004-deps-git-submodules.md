# 0004 — Dependencies as git submodules

- Status: Accepted
- Date: 2026-10-23

## Context
Third-party libraries can come from package managers (vcpkg, Conan), CMake FetchContent, or submodules. The project owner prefers to avoid vcpkg.

## Decision
Every third-party library is a git submodule in `third_party/`, pinned to a release tag and built through `add_subdirectory` or a wrapper target in `third_party/CMakeLists.txt`. No vcpkg, Conan or FetchContent. Submodule sources are never edited; patches go through a fork.

Exceptions, fetched as pinned prebuilt releases by `scripts/setup.ps1` into `tools/bin/`: Slang/DXC shader compilers. Development-time tools such as the Vulkan SDK's validation layers are installed separately.

## Consequences
- Exact versions are visible and reproducible from the repo alone.
- Everything builds from source: use Ninja and sccache/ccache to keep build times down.
- Libraries that download at configure time must have that disabled, with the extra dependency added as its own submodule (e.g. cgltf chosen over fastgltf for this reason).
- Workflow documented in the `add-dependency` skill.
