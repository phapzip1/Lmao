# 0002 — Backend-agnostic RHI, Vulkan first

- Status: Accepted
- Date: 2026-10-23

## Context
The engine should eventually run on several graphics APIs (Vulkan, D3D12, Metal, WebGPU, possibly OpenGL) and platforms. Abstracting late means rewriting the renderer; abstracting early but testing on only one API produces an interface shaped exactly like that API.

## Decision
- Define the RHI interface (`rhi::Device`, `rhi::CommandList`) from M1, modeled on the common ground of Vulkan, D3D12 and Metal.
- Implement Vulkan first, plus a Null backend from M2.
- Implement D3D12 right after the render graph (M5b) to prove the abstraction.
- Backend types never appear outside `rhi_<api>/`. Backends are chosen at runtime.
- Opaque handles, descriptor structs, abstract enums with translation tables, capability queries, coarse virtual calls.

## Consequences
- Some up-front design cost in M1–M2.
- Expect to refactor the RHI during M5b; that is the point of doing it early.
- The Null backend enables headless tests, CI without a GPU and dedicated servers.
- Influences: Unreal RHI, NVRHI, The Forge, Diligent, bgfx.
