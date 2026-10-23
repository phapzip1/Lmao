# 0007 — Sparse-set ECS and generational handles

- Status: Accepted
- Date: 2026-10-23

## Context
Scripting, streaming, networking and the editor all keep references to entities and resources. Raw pointers become dangling when objects are destroyed or moved. A deep `Node*` scene graph is also cache-unfriendly.

## Decision
- Entities, resources and assets are referenced by **handles**: index + generation counter. Looking up a stale handle fails detectably.
- Gameplay data lives in a **sparse-set ECS** (simpler to write than archetypes) with POD components where possible.
- The scene graph is `Transform` + `Parent` + `Children` components, updated by a dirty-flag pass in topological order.

## Consequences
- Scripts and network code hold only handles, so deleted entities can't be dereferenced.
- Archetype storage can be added later if iteration performance requires it.
- Influences: floooh "Handles are the better pointers", Bitsquid data-oriented entity system, EnTT, Overwatch GDC 2017.
