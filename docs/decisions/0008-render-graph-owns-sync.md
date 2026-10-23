# 0008 — The render graph owns GPU synchronization

- Status: Accepted
- Date: 2026-10-23

## Context
Hand-written barriers are the most common source of Vulkan and D3D12 bugs: flickering, corruption, device loss, and bugs that only appear on one GPU vendor. Different backends also need different synchronization (Metal and GL mostly synchronize automatically).

## Decision
From M5, every render pass declares the resources it reads and writes. The render graph sorts passes, culls unused ones, and emits abstract `ResourceState` transitions; each backend maps them to real barriers or ignores them. Renderer code never writes barriers by hand.

First version: single queue, no memory aliasing. Async compute and transient aliasing come later.

## Consequences
- Synchronization bugs are fixed in one place.
- Requires discipline: every resource access must be declared.
- Influences: Frostbite FrameGraph (GDC 2017), Granite render graph.
