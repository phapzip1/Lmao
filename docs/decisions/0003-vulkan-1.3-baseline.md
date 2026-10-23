# 0003 — Vulkan 1.3 baseline

- Status: Accepted
- Date: 2026-10-23

## Context
Vulkan 1.0-style code (render pass objects, framebuffers, binary semaphores, per-draw descriptor sets) is verbose and error-prone. Vulkan 1.3 makes the cleaner model core.

## Decision
Require Vulkan 1.3 with: dynamic rendering, synchronization2, timeline semaphores, descriptor indexing (bindless) and buffer device address. Use `VK_EXT_swapchain_maintenance1` and the maintenance extensions when available.

Supporting choices: volk as loader, VMA for memory, 2 frames in flight, deletion queues keyed on timeline values, one render-finished semaphore per swapchain image, pipeline cache on disk.

## Consequences
- Very old GPUs and drivers without 1.3 are not supported.
- Dynamic rendering and bindless map closely to D3D12 and Metal, which simplifies the RHI.
- Buffer device address and bindless prepare for GPU-driven rendering later.
