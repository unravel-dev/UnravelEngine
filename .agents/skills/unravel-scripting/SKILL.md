---
name: unravel-scripting
description: >-
  Works on UnravelEngine managed scripting: C# API, script glue, hot-reload,
  ScriptComponent lifecycle, and C++/C# interop via dotnetpp (CoreCLR). Use for
  game scripts, scripting backend, OnCreate/OnUpdate hooks, or physics/UI events
  forwarded to C#.
disable-model-invocation: true
---

# Unravel Scripting

## Start here

| Purpose | Path |
|---------|------|
| Script system | `engine/engine/scripting/ecs/systems/script_system.h` |
| Script glue | `engine/engine/scripting/ecs/systems/script_glue.cpp` |
| Interop | `engine/engine/scripting/ecs/systems/script_interop.h` |
| Script component | `engine/engine/scripting/ecs/components/script_component.h` |
| Meta | `engine/engine/meta/ecs/components/script_component.hpp` |
| C# API scripts | `engine_data/data/scripts/` |
| Embedding API | `deps/dotnetpp/dotnetpp/dotnetpp/` |

## Architecture

- **dotnetpp** embedding API (`dotnet::`) over CoreCLR (`DOTNETPP_BACKEND=coreclr`)
- Requires **.NET 9 SDK** (`dotnet` on `PATH`) for script compile and hot-reload
- **Dual script domains** — engine scripts + per-project scripts
- **Hot-reload** — `events::on_script_recompile` triggers recompile and reload
- **ScriptComponent** — attaches C# class to entity
- **POD interop** — layout-compatible value types in `dotnetpp_backend::managed_interface`,
  converter specializations in `dotnetpp_backend::managed_interface`, registered with
  `dotnet_register_converter_for_pod`

## C# API layout (`engine_data/data/scripts/`)

| Area | Path |
|------|------|
| Components | `scene/components/` |
| Physics callbacks | `scene/physics/` |
| Math | `math/` |
| UI | `ui/` |
| Gizmos | `gizmos/` |
| Entry | `system/` |

Namespace: `Unravel.Core` (and project-specific assemblies).

## Lifecycle hooks

C# scripts implement:

- `OnCreate` — entity created
- `OnStart` — play mode start
- `OnUpdate` / fixed update variants
- Collision/sensor callbacks forwarded from physics

Script system connects to `events` in `script_system.cpp`:

- `on_frame_update`, `on_frame_fixed_update`, `on_frame_late_update`
- `on_play_begin`, `on_play_end`, `on_pause`, `on_resume`

## C++ <-> C# glue

When exposing new engine features to scripts:

1. Add C++ callable from glue (`script_glue.cpp`, `script_interop.cpp`)
2. Add C# wrapper in `engine_data/data/scripts/`
3. Register internal call with `dotnet::add_internal_call` / `dotnet::internal_call_registry` and `dotnet_internal_call()`
4. For POD types, add layout-compatible managed struct + `dotnet_register_converter_for_pod` in `script_interop.h`
5. Maintain API parity — breaking C# API breaks user projects

## Editor MCP (scripts)

For attach/edit/save via MCP, use skill `unravel-entities`:

| Tool | Purpose |
|------|---------|
| `scripts_list_types` / `scene_add_script` / `scene_remove_script` | Type attach (not `scene_add_component`) |
| `scripts_get_source` / `scripts_set_source` | Read/write `.cs` (atomic write) |
| `scripts_create` | New file from `TemplateComponent.cs.in` |

## Hot-reload

Triggered by `on_script_recompile` event. Script system:

- Recompiles changed assemblies via `dotnet::compile`
- Reloads domain (project scripts)
- Re-attaches ScriptComponents

Test: edit C# script -> recompile menu -> scripts reload without full restart.

Domain unload runs `[AutoStaticsCleanup]` and verifies collection with a leak report
(see reference.md "Domain unload, statics cleanup, leak detection"). Mark any class
whose statics hold script instances, script `Type`s, or delegates with `[AutoStaticsCleanup]`.

## Verification checklist

- [ ] C# API mirrors C++ behavior
- [ ] ScriptComponent add/remove in inspector works
- [ ] Lifecycle hooks fire in correct order (create -> start -> update)
- [ ] Hot-reload preserves or correctly resets state
- [ ] Physics events reach C# (`on_collision_enter`, etc.)
- [ ] No managed handle leaks after reload
- [ ] Project scripts compile (`.NET 9 SDK`; `dotnet --info` reports an SDK)

## Common mistakes

- Exposing raw pointers to C# without lifetime management
- Missing glue registration for new native method
- C# API change without migration note
- Script logic in C++ that belongs in `script_system` dispatch
- Forgetting play-mode-only guards on runtime script state
- Using backend-specific embedding APIs directly — use `dotnet::` and `<dotnetpp/dotnetpp.h>`
- Putting converter specializations in the wrong namespace — use
  `dotnetpp_backend::managed_interface` (no backend ifdefs in user code)

## Deep reference

See [reference.md](reference.md) for glue patterns.
