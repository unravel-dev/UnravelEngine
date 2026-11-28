---
name: unravel-architect
description: >-
  Design checklist for UnravelEngine changes that cross areas: a new subsystem or system,
  a public C++ or C# API, a file or serialization format change, a feature spanning engine,
  editor, assets and scripts, or a new CMake target. Use before implementing such a change,
  to name its contracts, touch points, play-mode boundary, migration and commit order.
---

# Cross-area design

A read-only checklist for a change that reaches more than one area or adds a contract other
code will depend on. Each area's procedures stay in its skill (catalog:
`.agents/skills/README.md`); the per-change checks for components, fields and formats are in
AGENTS.md ("Changing a shared contract").

## Gotchas

- Services live in `rtti::context` (`ctx.add<T>()`, `ctx.get<T>()`,
  `engine/core/context/context.hpp`); a new global or singleton beside it is a second source
  of truth.
- Frame-event order is a contract: `frame_update_priority` in `engine/engine/events.h` names
  the bands (play mode, script draw reset, editing, scene systems, physics, scripts, late
  scripts, late physics). A consumer that needs an order takes a named priority there; order
  inside a band is registration order and not a contract.
- Service creation order is the order of `engine::create`, `init_core` and `init_systems` in
  `engine/engine/engine.cpp`; read it there before relying on it.
- The editor libraries under `editor/core/` (`asset_compiler`, `deploy`, `packages`,
  `https_client`, `update_check`, `sample_catalog`) link into the editor and the test runner
  only; nothing a player runs may need them.
- Rendering goes through `engine/core/graphics/`; bgfx changes only as patches
  (`deps/AGENTS.md`).
- A per-object variation reuses the object the engine already has (a material instance of a
  material), never a side channel or a new value type next to it (`unravel-materials`); a
  script API exposes those same objects (`unravel-scripting`).
- A feature has one system, used for the simple case and the advanced one alike. A better
  approach replaces the old one and upgrades old scenes, prefabs and assets on load (every
  material became a graph, with the Standard graph as the default); it never ships beside it.
- Work that takes longer than a frame is computed when an asset compiles or a deploy runs and
  is loaded like an imported asset; deferring it to first use moves the stall into play.
- A format change keeps old files loading (a version or a backward-compatible LOAD) and keeps
  asset uids and prefab override paths (`serialization_path` in
  `editor/editor/hub/panels/inspector_panel/inspectors/inspectors.h`) stable.
- A new third-party library, a new CI trigger or a relaxed version check is the user's
  decision; a design names it as a question.

## How it works

Layers, each depending only on the ones before it:

```text
deps/ (ospp, threadpp, audiopp, dotnetpp, hpp, 3rdparty)
  -> engine/core/     context, reflection, serialization, filesystem, graphics, concurrency
  -> engine/engine/   ECS, systems, rendering, assets, physics, audio, scripting, UI
  -> game/            the player
  -> editor/core/     asset compiler, deploy, packages, update check, sample catalog
  -> editor/editor/   hub, panels, gizmos, MCP server
```

The surfaces a feature can have, and the skill that owns each:

| Surface | What it holds | Owner |
|---------|---------------|-------|
| Runtime API | types in the context, events, systems | `unravel-ecs-component` |
| Persistence | ser20 SAVE/LOAD, meta under `engine/engine/meta/` | `unravel-ecs-component`, `unravel-prefabs` |
| Editor | inspectors, panels, undo, gizmos | `unravel-editor-panel` |
| MCP | tools that call the editor's own functions | `unravel-editor-mcp` |
| Scripts | C# under `engine_data/data/scripts/`, bindings under `engine/engine/scripting/` | `unravel-scripting` |
| Assets | importer, `.meta`, compiled output, loading | `unravel-assets` |
| Rendering | passes, shaders, pipeline steps | `unravel-rendering`, `unravel-materials` |
| Build and data | targets, data copies, suites | `unravel-build-verify` |

## Procedures

1. Write the contract before code: each surface above that the change touches, with its
   types, events, formats and owner.
2. Answer the play-mode boundary: what runs in edit mode only, in play only, what resets when
   play ends, what survives it (`unravel-ecs-component`, play-mode.md).
3. List the touch points per owning skill and run the shared-contract checks of AGENTS.md.
4. Plan the migration of existing projects and samples.
5. Order the commits by dependency - library, engine core, engine systems, editor, data and
   scripts - each one building and passing its suites on its own.
6. Name the verification of each step (`unravel-build-verify`).

Area procedures: a component - `unravel-ecs-component` (add-component.md); a render pass or
a shader - `unravel-rendering` (add-render-pass.md, shader-change.md); an inspector -
`unravel-editor-panel` (add-inspector.md); play-mode behaviour - `unravel-ecs-component`
(play-mode.md); a bug or a measurement - `unravel-debugging`; editor MCP tools -
`unravel-editor-mcp`.

## Verify

- [ ] The contract is written: types, events, formats, owners.
- [ ] Each touched surface followed its owning skill's procedure.
- [ ] The play-mode lifecycle is defined.
- [ ] Old files load; uids and override paths are unchanged.
- [ ] No public API beyond what the feature needs.
- [ ] Which frame pays for slow work is stated.
- [ ] Each commit builds in Debug and RelWithDebInfo and passes its suites.

## Entry points

| File | What it holds |
|------|---------------|
| `engine/engine/engine.cpp` | service creation order, the frame loop |
| `engine/engine/events.h` | frame events and their priorities |
| `engine/core/context/context.hpp` | the service context |
| `engine/engine/play_mode.h` | play-mode phases |
| `engine/engine/meta/ecs/components/all_components.h` | serializable component list |
| `editor/editor/hub/panels/inspector_panel/inspectors/inspectors.h` | inspectors, override paths |
| `engine/engine/assets/asset_handle.h` | asset handles and loading |
| `engine/engine/rendering/pipeline/deferred/pipeline.h` | the pipeline and its steps |
