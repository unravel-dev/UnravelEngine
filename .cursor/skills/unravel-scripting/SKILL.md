---
name: unravel-scripting
description: >-
  Works on UnravelEngine Mono scripting: C# API, script glue, hot-reload,
  ScriptComponent lifecycle, and C++/C# interop. Use for game scripts, scripting
  backend, OnCreate/OnUpdate hooks, or physics/UI events forwarded to C#.
disable-model-invocation: true
---

# Unravel Scripting

## Start here

| Purpose | Path |
|---------|------|
| Script system | `engine/engine/scripting/ecs/systems/script_system.h` |
| Script glue | `engine/engine/scripting/ecs/systems/script_glue.cpp` |
| Interop | `engine/engine/scripting/ecs/systems/script_interop.cpp` |
| Script component | `engine/engine/scripting/ecs/components/script_component.h` |
| Meta | `engine/engine/meta/ecs/components/script_component.hpp` |
| C# API scripts | `engine_data/data/scripts/` |
| Mono wrapper | `deps/monopp/`, `deps/monort/` |

## Architecture

- **Mono embedded** via `monopp` / `monort`
- **Dual script domains** — engine scripts + per-project scripts
- **Hot-reload** — `events::on_script_recompile` triggers recompile and reload
- **ScriptComponent** — attaches C# class to entity

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

## C++ ↔ C# glue

When exposing new engine features to scripts:

1. Add C++ callable from glue (`script_glue.cpp`, `script_interop.cpp`)
2. Add C# wrapper in `engine_data/data/scripts/`
3. Register type/method with Mono if needed
4. Maintain API parity — breaking C# API breaks user projects

## Hot-reload

Triggered by `on_script_recompile` event. Script system:

- Recompiles changed assemblies
- Reloads AppDomain (project scripts)
- Re-attaches ScriptComponents

Test: edit C# script → recompile menu → scripts reload without full restart.

## Verification checklist

- [ ] C# API mirrors C++ behavior
- [ ] ScriptComponent add/remove in inspector works
- [ ] Lifecycle hooks fire in correct order (create → start → update)
- [ ] Hot-reload preserves or correctly resets state
- [ ] Physics events reach C# (`on_collision_enter`, etc.)
- [ ] No Mono handle leaks after reload
- [ ] Project scripts compile (Mono 6.12+ installed)

## Common mistakes

- Exposing raw pointers to C# without lifetime management
- Missing glue registration for new native method
- C# API change without migration note
- Script logic in C++ that belongs in `script_system` dispatch
- Forgetting play-mode-only guards on runtime script state

## Deep reference

See [reference.md](reference.md) for glue patterns.
