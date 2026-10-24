# Architecture

Overview of how the Lmao engine is structured and why. Individual decisions are recorded in [decisions/](decisions/README.md); the plan of work is in [milestones.md](milestones.md); debugging techniques are in [debugging.md](debugging.md).

## Principles
1. **Strict layering.** A lower layer never includes headers from a higher one.
2. **Handles, not pointers.** Resources, entities and assets are referenced by index + generation, so stale references are detected instead of becoming use-after-free.
3. **Data-oriented core.** Gameplay data lives in an ECS; the scene graph is a transform hierarchy built on top of it.
4. **Systems produce jobs.** Only the main thread (GLFW), audio, IO and network threads are dedicated; everything else runs on the job system.
5. **Write it yourself where you learn something; use a library where you don't** (image decoding, font rasterization, audio device output).

## Layers

```
L5  Tools / Editor      ImGui editor, asset cooker, profilers
L4  Gameplay            Scripting · AI · Cinematics · Game modes
L3  Engine systems      Renderer (2D/3D) · Physics · Audio · Animation · Networking · Localization
L2  Scene & resources   ECS · Transform hierarchy · Asset manager · Streaming
L1  RHI                 Backend-agnostic GPU interface (Vulkan, D3D12, Null, …)
L0  Core                Platform · Memory · Jobs · Math · Containers · Log/Assert · VFS · Reflection · Profiling
```

## Directory layout

```
engine/
  core/          platform/ (interfaces) + platform/platforms/{glfw,win32,posix}/ (backends)
                 memory/ jobs/ math/ containers/ log/ io/ reflect/ profile/
  rhi/           interface (no backend headers) + rhi/rhis/{vulkan,d3d12,null}/ (backends;
                 later metal/ webgpu/ gl/)
  render/        rendergraph/ renderer3d/ renderer2d/ materials/ debugdraw/ ui_text/
  scene/         ecs/ hierarchy/ components/ serialization/
  resource/      asset_db/ loaders/ streaming/
  physics/       collision/ broadphase/ solver/ queries/
  audio/         device/ mixer/ dsp/ spatial/
  anim/          skeleton/ clips/ blendtree/ statemachine/ ik/
  script/        lua_vm/ bindings/ hotreload/
  ai/            bt/ navmesh/ pathfinding/ perception/
  net/           transport/ reliability/ replication/ prediction/
  loc/           string_tables/ text_shaping/
  cinematics/    timeline/ tracks/ sequencer/
  app/           Application, main loop, config
tools/           asset_cooker/ editor/ shader_compiler/   (tools/bin/ = prebuilt, gitignored)
game/            sample projects
shaders/
third_party/     git submodules only
docs/
```

## Frame model
- Fixed-timestep simulation (e.g. 60 Hz) with interpolation for rendering.
- Rendering runs one frame behind simulation: an **extract** step copies a render snapshot, which is recorded in parallel while the next simulation frame runs.
- Frame order: `Input → Script → AI → Physics (fixed) → Animation → Transform update → Render extract → Render submit`.

## Core systems

| System | Design |
|---|---|
| Memory | Tagged tracking per subsystem. Frame linear allocator (double-buffered), pool allocators for fixed-size objects, TLSF for general use, scoped arenas for loading. Guard bytes and poisoning in debug. |
| Jobs | Work-stealing job system, N−1 workers, counter-based waits. Fibers only if needed later. Single-thread mode for debugging. |
| ECS | Sparse-set ECS; POD components. `Transform` + `Parent` + `Children` form the scene graph, updated by a dirty-flag pass in topological order. |
| Reflection | Built early (M4). Drives serialization, editor inspector, script bindings and network replication. |
| Resources | Asset DB with GUIDs; offline cooker turns source files into engine-ready binary blobs. States: `Unloaded → Loading → Loaded → Resident`. |
| Streaming | Dedicated IO thread, GPU uploads on the transfer queue, ref-counted handles, priority by distance and screen size, memory budget. |

## Engine systems

| System | Design |
|---|---|
| Physics | Own implementation: BVH broadphase, SAT/GJK+EPA narrowphase, sequential-impulse solver with warm starting, sleeping, CCD. Narrow API so another solver could be swapped in. |
| Audio | miniaudio for device output only; own mixer, voices, buses, DSP, 3D spatialization. The audio thread never locks, allocates or does IO. |
| Animation | Skeletons, compressed clips, blend trees, state machines, two-bone IK, root motion, GPU skinning. |
| Scripting | Lua with sol2; bindings generated from reflection; scripts hold only entity handles; hot reload. |
| AI | Behavior trees + blackboard, navmesh (Recast/Detour), time-sliced A* requests, perception. |
| Networking | UDP with own reliability layer; authoritative server; snapshot replication with delta compression; client prediction + reconciliation; interpolation. |
| Localization | UTF-8 everywhere; string tables keyed by ID; FreeType + HarfBuzz shaping; MSDF fonts; pseudo-locale for testing. |
| Cinematics | Timeline asset with tracks (transform, camera, animation, audio, script event, curves, subtitles), evaluated into the ECS; synced to the audio clock. |
| 2D | Same renderer and render graph; sprite batcher with instanced quads, tilemaps, orthographic camera. |

## RHI (render hardware interface)
Modeled on the common ground of Vulkan, D3D12 and Metal; simpler APIs emulate it.

- Public headers contain no backend types; backends live in `rhi/rhis/<api>/` and are selected **at runtime**.
- Resources are opaque handles created from plain descriptor structs (`BufferDesc`, `TextureDesc`, `GraphicsPipelineDesc`).
- Abstract enums (`Format`, `ResourceState`, …) with per-backend translation tables.
- Capabilities are queried (`device->caps()`), never assumed.
- Coarse virtual interface (`Device`, `CommandList`): no per-state setters; state lives in pipelines.
- Destruction is deferred until the GPU is done with the resource.
- **Null backend** from the start: dedicated servers, tests, CI without a GPU.
- The **render graph** declares per-pass reads/writes and emits abstract `ResourceState` transitions; backends map them to real barriers (or ignore them).

Sketch of the interface:

```cpp
namespace rhi {
class CommandList {
public:
    virtual void barrier(std::span<const ResourceTransition>) = 0;
    virtual void beginRendering(const RenderingDesc&) = 0;
    virtual void endRendering() = 0;
    virtual void setPipeline(PipelineHandle) = 0;
    virtual void pushConstants(const void* data, uint32_t size) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                             int32_t vertexOffset, uint32_t firstInstance) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
    virtual void beginMarker(const char* name) = 0;
    virtual void endMarker() = 0;
    // …
};

class Device {
public:
    virtual const DeviceCaps& caps() const = 0;
    virtual BufferHandle   createBuffer(const BufferDesc&) = 0;
    virtual TextureHandle  createTexture(const TextureDesc&) = 0;
    virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    virtual uint32_t       bindlessIndex(TextureHandle) = 0;
    virtual uint64_t       gpuAddress(BufferHandle) = 0;
    virtual CommandList*   beginCommandList(QueueType) = 0;
    virtual SubmitTicket   submit(std::span<CommandList*>, QueueType) = 0;
    // …
};

std::unique_ptr<Device> createDevice(Backend, const DeviceCreateInfo&);
}
```

### Backend mapping

| Concept | Vulkan | D3D12 | Metal | OpenGL |
|---|---|---|---|---|
| Command list | `VkCommandBuffer` | `ID3D12GraphicsCommandList` | command buffer + encoders | emulated (record, replay) |
| Pipeline | `VkPipeline` | PSO | pipeline state | emulated |
| Bindless | descriptor indexing | descriptor heaps (SM 6.6) | argument buffers | bindless ext / arrays |
| Barriers | synchronization2 | enhanced barriers | mostly automatic | mostly no-op |
| GPU sync | timeline semaphore | `ID3D12Fence` | `MTLSharedEvent` | `glFenceSync` |

## Vulkan backend
- **Vulkan 1.3 baseline:** dynamic rendering, synchronization2, timeline semaphores, descriptor indexing, buffer device address.
- **volk** loads functions (no `vulkan-1.lib` link); **VMA** for all memory; query `VK_EXT_memory_budget`.
- **2 frames in flight.** Each frame owns per-thread command pools, a linear upload ring and a deletion queue keyed on the timeline semaphore value.
- **Swapchain:** FIFO default, MAILBOX when available; recreate on `OUT_OF_DATE`/`SUBOPTIMAL`/resize; skip frames while minimized; one render-finished semaphore **per swapchain image**.
- **Bindless set 0:** sampled images, storage buffers, samplers, storage images (`UPDATE_AFTER_BIND | PARTIALLY_BOUND`); per-draw data in push constants; `nonuniformEXT` for varying indices.
- **Pipelines:** one `VkPipelineCache`, loaded at device creation and saved at shutdown to `DeviceDesc::PipelineCacheDirectory` (one file per vendor/device ID, written to a temporary file and renamed into place). The header's vendor ID, device ID and `pipelineCacheUUID` are checked before the data reaches the driver, and a stale cache is dropped with a logged reason. Hits are counted with `VkPipelineCreationFeedback`. Later: compiled on worker threads; layouts from SPIRV-Reflect.
- **Queues:** graphics+present first; transfer queue for streaming; async compute later.
- **Multithreaded recording:** one command pool per thread per frame; a single thread submits.
- Name every object (`vkSetDebugUtilsObjectNameEXT`) and label every pass.
- Later: GPU-driven rendering (mega-buffers, compute culling, `vkCmdDrawIndexedIndirectCount`).

## Shaders
Slang, compiled offline by the cooker to SPIR-V, DXIL, MSL, GLSL or WGSL per backend; runtime compilation only for hot reload. Fixed binding convention:

| Slot | Contents | Vulkan | D3D12 | Metal |
|---|---|---|---|---|
| Push constants (≤128 B) | draw/material indices, buffer addresses | push constants | root constants | `setBytes` |
| Set/space 0 | global bindless arrays | descriptor indexing | `ResourceDescriptorHeap[]` | argument buffer |
| Set/space 1 | per-frame constants | uniform buffer | CBV | buffer |

## Conventions
Right-handed, Y-up, column-major, depth 0..1, reverse-Z with `D32_SFLOAT`. Each backend fixes clip-space differences in one place (Vulkan: negative viewport height; GL: `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)`). See [decisions/0005](decisions/0005-coordinate-conventions.md).

## Platform layer
`core/platform/` defines Window, Input, FileSystem, Time, Threads, DynamicLibrary, Clipboard, Dialogs, CrashHandler and Sockets. `paths.h` returns well-known folders as UTF-8: `GetExecutableDirectory`, `GetUserDataDirectory` and `GetUserCacheDirectory` (`%LOCALAPPDATA%\<app>[\Cache]`, or the XDG data and cache folders). GLFW is one backend (Windows, Linux, macOS); native backends come later for mobile and consoles. The window exposes native handles (`HWND`, `wl_surface*`, `CAMetalLayer*`, …) for surface creation, and is created after the RHI backend is chosen (GLFW needs `GLFW_NO_API` except for GL).

Cross-platform rules: virtual paths with `/` and case-sensitive matching, UTF-8 internally, little-endian versioned asset formats, builds on MSVC/clang-cl and GCC/Clang from M0.

## Third-party libraries
All are git submodules ([decisions/0004](decisions/0004-deps-git-submodules.md)): GLFW, Vulkan-Headers, volk, VulkanMemoryAllocator, D3D12MemoryAllocator, DirectX-Headers, glm, Dear ImGui (docking), stb, cgltf, miniaudio, Lua, sol2, FreeType, HarfBuzz, msdfgen, Tracy, SPIRV-Reflect, Recast/Detour.

Not submodules: Slang/DXC (prebuilt, fetched into `tools/bin/`), the Vulkan SDK (validation layers and tools at development time only), the D3D12 Agility SDK (NuGet, later).

## Influences
- Jason Gregory, *Game Engine Architecture*: layered runtime.
- Unreal Engine: module layout, RHI and NullDrv, GenericPlatform pattern.
- Frostbite FrameGraph (GDC 2017), Granite render graph blog: render graph.
- NVRHI, The Forge, Diligent, bgfx, sokol_gfx: RHI design.
- Naughty Dog fibers (GDC 2015): job system. Destiny rendering (GDC 2015): extract step.
- floooh "Handles are the better pointers", Bitsquid blog, EnTT, Overwatch (GDC 2017): handles and ECS.
- Gaffer On Games, Valve Source networking, Gabriel Gambetta: timestep and networking.
- Erin Catto / Box2D: physics solver. Damian Isla (Halo 2): behavior trees.
- Arseny Kapoulkine, vkguide.dev: Vulkan practice. Nathan Reed: reverse-Z.
