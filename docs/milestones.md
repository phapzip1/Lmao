# Milestones

| # | Name | Status | Started | Done | Tag |
|---|------|--------|---------|------|-----|
| M0 | Foundation | ✅ Done | 2026-10-21 | 2026-11-08 | m0 |
| M1 | Vulkan bootstrap | 🚧 In progress | 2026-11-09 | | |
| M2 | Render core | ⏳ Planned | | | |

---

## M1 — Vulkan bootstrap 🚧

**Exit criteria** (the milestone is done only when all of these are checked)
- [x] Instance + debug messenger, validation breaks into the debugger on errors
- [x] Device selection (Vulkan 1.3 features required)
- [ ] Swapchain + 2 frames in flight, one render-finished semaphore per image
- [ ] Triangle rendered through `rhi::Device` (no Vulkan types outside `rhi_vulkan/`)
- [ ] Survives resize and minimize with **zero** validation or sync-validation messages

**Tasks**
- [x] volk + VMA wired in
- [ ] Deletion queue keyed on timeline semaphore
- [ ] RenderDoc in-app capture hotkey

**Notes / decisions**
- 2026-11-12: chose FIFO by default, MAILBOX when available.

**Demo:** `docs/demos/m1.gif`
