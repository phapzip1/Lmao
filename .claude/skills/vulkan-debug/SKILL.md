---
name: vulkan-debug
description: Diagnose Vulkan rendering bugs in the Lmao engine. Use for validation-layer errors, VK_ERROR_DEVICE_LOST, GPU crashes or hangs, flickering, corruption, black screens, swapchain/resize crashes, or bugs that show up on one GPU vendor only.
---

# Debugging Vulkan

## 1. Get information before changing code
- Run a **Debug** build with validation on. Turn on sync validation through vkconfig (Vulkan SDK), or via the environment:
  ```powershell
  $env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"
  $env:VK_KHRONOS_VALIDATION_VALIDATE_SYNC = "true"
  # GPU-assisted validation (slow; for OOB/descriptor bugs):
  $env:VK_KHRONOS_VALIDATION_GPUAV_ENABLE = "true"
  ```
  Setting names change between SDK versions. If one has no effect, check `vkconfig` or the layer's documentation for the installed SDK.
- Read the **first** validation message. Its VUID (e.g. `VUID-vkCmdDraw-None-08600`) can be looked up in the Vulkan spec. Object names in the message come from `vkSetDebugUtilsObjectNameEXT`; if a message shows unnamed handles, name those objects first.
- Classify the symptom using the table in [reference.md](reference.md).

## 2. Isolate
Toggle one thing at a time and re-run:
1. Insert a full `ALL_COMMANDS → ALL_COMMANDS` memory barrier between passes. If the bug disappears, it's synchronization: find the missing barrier in the render graph's declared reads/writes.
2. Disable render graph passes one by one.
3. Switch the job system to single-thread mode, to rule out command-pool or queue races.
4. Set frames-in-flight to 1, to rule out lifetime and use-after-free bugs.
5. Run on the Null backend, or a second backend, to separate engine bugs from Vulkan-backend bugs.

## 3. GPU crashes (`VK_ERROR_DEVICE_LOST`)
- Read the engine's breadcrumb buffer after the loss. The last marker written shows the pass that was executing.
- Enable `VK_EXT_device_fault` output if the driver supports it. Use Nsight Aftermath on NVIDIA or Radeon GPU Detective on AMD.
- Usual causes: a resource destroyed while still in flight (check the deletion queue's timeline value), out-of-bounds buffer or descriptor index (enable GPU-AV or `robustBufferAccess2`), an infinite shader loop.

## 4. Visual bugs
Capture a frame with RenderDoc (in-app capture hotkey). Check in order: Event Browser markers → pipeline state (viewport, cull mode, depth compare) → bound resources → mesh viewer → pixel history.

## 5. Fix and confirm
- Fix the root cause in the render graph, RHI or deletion queue, not with a workaround barrier in renderer code.
- Re-run with sync validation and confirm zero messages.
- If the bug was vendor-specific or a regression, add a golden-image test scene that reproduces it.
