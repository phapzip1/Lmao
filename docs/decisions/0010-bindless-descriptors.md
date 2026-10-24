# 0010 — Bindless descriptors: one global set, indexed by handle

- Status: Accepted
- Date: 2026-10-24 (fallbacks, typed indices and declared sampled inputs added the same day)

## Context
Decision 0006 fixed the binding convention (push constants ≤ 128 B, set 0 global bindless, set 1 per frame) but not its contents. Per-draw descriptor sets mean allocating, writing and binding sets for every material, and a layout per shader, which is a large source of CPU cost and bugs. Every GPU the engine targets (Vulkan 1.3 with descriptor indexing, decision 0003) can index large descriptor arrays dynamically.

## Decision
**One pipeline layout for every pipeline**, created by the device:

| Set / range | Contents | Updated |
|---|---|---|
| Push constants | 128 bytes, all stages: usually indices into set 0, plus small per-draw values | Per draw (`CommandList::PushConstants`) |
| Set 0, binding 0 | `Texture2D[]`: sampled images | When a texture is created |
| Set 0, binding 1 | `RWTexture2D<float4>[]`: storage images | When a texture is created |
| Set 0, binding 2 | `RWByteAddressBuffer[]`: storage buffers | When a buffer is created |
| Set 0, binding 3 | `SamplerState[]`: samplers | When a sampler is created |
| Set 1, binding 0 | Uniform buffer, 4 KiB: `FrameConstants` (camera, time, resolution) | Per frame (`CommandList::SetFrameConstants`) |

- **A resource's descriptor index is its handle's index** (`Rhi::DescriptorIndex(handle)`). The device writes the descriptor when the resource is created; shaders never see descriptor sets, only indices.
- **Indices are typed** on both sides: `Rhi::TextureIndex` / `BufferIndex` / `SamplerIndex` in C++ (4-byte structs, safe in push constants and buffers) and the same-named structs in Slang. The helpers only accept the matching type, so passing a buffer index as a texture is a compile error.
- **Every slot always holds a valid descriptor.** Index 0 of each array is a device-owned fallback: a 1x1 magenta texture (also the storage-image fallback, kept in `GENERAL`), a 64 KiB buffer and a linear sampler. The buffer holds quiet NaNs in Debug builds (loud: they spread through any math and show in RenderDoc) and zeros in Release (safe). At startup every slot points at them; a freed slot is pointed back at them before it can be reused; `DescriptorIndex` of an invalid handle is 0. A forgotten, failed, stale or never-used index therefore reads magenta or NaN/zeros instead of undefined memory.
- Set 0 bindings are `PARTIALLY_BOUND | UPDATE_AFTER_BIND | UPDATE_UNUSED_WHILE_PENDING`, so resources can be created while earlier frames using the same set are still on the GPU. (Partially bound is kept as a safety net; with the fallbacks no slot is ever actually unbound.)
- **A destroyed resource's slot is released only after the GPU has finished every frame that might read it** (deferred destruction). Its handle becomes invalid immediately, but its index can't be reused, so a new resource's descriptor never overwrites one an in-flight frame still reads.
- Sets 0 and 1 are bound once at the start of every command buffer, for graphics and compute.
- **Passes declare the textures they sample** (`RenderingDesc::SampledTextures`); the RHI moves them to `SHADER_READ_ONLY_OPTIMAL` (the layout their descriptor promises) with the barrier that orders them after whatever wrote them. Uploads leave sampled textures in that layout too. This is the first piece of decision 0008: the M5 render graph builds on the same declarations.
- Shaders `import common.bindless;` (`shaders/common/bindless.slang`) for the declarations and helpers: `GetTexture`, `GetSampler`, `SampleTexture`, `LoadBuffer<T>` / `StoreBuffer<T>`, `GetFrame()`. Every index goes through `NonUniformResourceIndex`.
- Storage buffers are untyped (`RWByteAddressBuffer`) so one binding serves every element type. `Load<T>` uses the C++ layout of `T`, so shared structs use no `float3` and are `static_assert`ed on the C++ side. The read-write array needs `vertexPipelineStoresAndAtomics` and `fragmentStoresAndAtomics`, which every desktop GPU has.

**Capacities**: 16,384 sampled images, 4,096 storage images, 16,384 storage buffers and 1,024 samplers. Each is clamped to the GPU's update-after-bind limits at startup and logged ("Bindless capacity: ..."). Creating a resource whose index is past the capacity fails with an error. Handle indices are reused, so the limit is on resources alive at the same time (plus those awaiting deferred destruction), not on the total created.

**Required features** (added to device selection): descriptor indexing with update-after-bind for sampled images, storage images and storage buffers; `descriptorBindingUpdateUnusedWhilePending`; non-uniform indexing for sampled images, storage images and storage buffers; `runtimeDescriptorArray`; `descriptorBindingPartiallyBound`.

## Consequences
- No per-material descriptor sets, layouts or pipeline layouts: materials are a few indices in a buffer or in push constants, and any pipeline can be bound with any resources.
- Indices are not validated by the API. Typed indices catch mixing kinds, the fallbacks make stale or invalid indices visible (magenta / zeros) instead of undefined, and an index that points at the *wrong live* resource, or past the array, is caught only by GPU-assisted validation: `DeviceDesc::GpuValidation` (sandbox: `--gpu-validation`) turns on the layer's `gpuav_enable`. It is slow and prints a few informational warnings at startup (it forces some features on and suggests disabling core validation).
- Image layouts must match the descriptor: a texture sampled without being declared in `SampledTextures` (or uploaded) may be in the wrong layout. GPU-assisted validation reports that too.
- Uniform buffers aren't in the bindless set; per-object data lives in storage buffers. Typed buffer arrays (`StructuredBuffer<T>[]`) would need one binding per type, so they are left out.
- **Portability costs, accepted for now:**
  - **WebGPU** (decision 0009) has no bindless arrays. That backend needs a different binding model behind the same Slang helpers (e.g. per-draw bind groups built from the indices in the push constants). The C++ and shader APIs (typed indices, `LoadBuffer`, `SampleTexture`) are meant to survive that; set 0 itself won't.
  - **Mobile GPUs** often lack `vertexPipelineStoresAndAtomics` and have small update-after-bind limits. If mobile becomes a target, split binding 2 into a read-only `ByteAddressBuffer[]` (all stages) and a read-write array (compute/fragment), with two descriptors per storage buffer; only `bindless.slang` and the descriptor writes change.
  - **More texture types** (cubemaps, arrays, 3D, integer storage images) each need another set 0 binding and fallback. Add them before shadows and environment maps (M5).
- Influences: Wicked Engine's bindless renderer; the Vulkan descriptor-indexing samples (Khronos).
