# Vulkan symptom → cause table

| Symptom | Likely cause | Where to look |
|---|---|---|
| `VK_ERROR_DEVICE_LOST`, random | Resource destroyed while in flight; OOB shader access; infinite loop | Deletion queue timeline values; GPU-AV; breadcrumbs; Aftermath / RGD |
| Flickering, intermittent corruption | Missing or wrong barrier (stage or access mask) | Sync validation; render graph pass declarations |
| Works on NVIDIA, broken on AMD/Intel | Wrong image layout (NVIDIA ignores many); missing `nonuniformEXT`; alignment assumptions | Validation; `minUniformBufferOffsetAlignment`, `minStorageBufferOffsetAlignment`; bindless index usage in shaders |
| Garbage uniform/storage values | `vec3` padding; std140/std430 vs C++ layout | `static_assert` on struct size/offsets vs SPIRV-Reflect output |
| Black screen, no errors | Winding flipped by the negative-viewport Y flip; reverse-Z with clear 1 or compare LESS; zero-size viewport; descriptor never written | RenderDoc pipeline state + mesh viewer |
| Crash on resize/minimize | Swapchain recreated with frames in flight; 0×0 extent | Wait on frame fences before recreating; skip frames while minimized |
| Semaphore validation error at present | Render-finished semaphore reused per frame instead of per swapchain image | Swapchain code: one render-finished semaphore per image |
| Hitch on first use of a material | Pipeline compiled on first draw | Pipeline cache on disk; prewarm; async compilation |
| Upload data occasionally stale | Staging ring overwritten before the copy executed | Ring allocator keyed on the frame's timeline value |
| Errors only at shutdown | Wrong destruction order; no `vkDeviceWaitIdle` | Shutdown path: wait idle, flush deletion queues, destroy in reverse creation order |
| Crash when recording from jobs | Command pool shared between threads; concurrent `vkQueueSubmit2` | One pool per thread per frame; single submitting thread |
