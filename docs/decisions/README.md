# Decision records

One file per decision: context, the decision, and its consequences. A decision is changed by adding a new record that supersedes the old one, not by editing history.

| # | Decision | Status | Date |
|---|---|---|---|
| [0001](0001-layered-architecture.md) | Strict layered architecture | Accepted | 2026-10-23 |
| [0002](0002-backend-agnostic-rhi.md) | Backend-agnostic RHI, Vulkan first | Accepted | 2026-10-23 |
| [0003](0003-vulkan-1.3-baseline.md) | Vulkan 1.3 baseline | Accepted | 2026-10-23 |
| [0004](0004-deps-git-submodules.md) | Dependencies as git submodules | Accepted | 2026-10-23 |
| [0005](0005-coordinate-conventions.md) | Coordinate conventions and reverse-Z | Accepted | 2026-10-23 |
| [0006](0006-slang-shaders.md) | Slang shaders with a fixed binding convention | Accepted | 2026-10-23 |
| [0007](0007-ecs-and-handles.md) | Sparse-set ECS and generational handles | Accepted | 2026-10-23 |
| [0008](0008-render-graph-owns-sync.md) | The render graph owns GPU synchronization | Accepted | 2026-10-23 |
| [0009](0009-opengl-deprioritized.md) | OpenGL is last priority; WebGPU for the web | Accepted | 2026-10-23 |

Template:

```markdown
# NNNN — Title

- Status: Proposed | Accepted | Superseded by NNNN
- Date: YYYY-MM-DD

## Context
## Decision
## Consequences
```
