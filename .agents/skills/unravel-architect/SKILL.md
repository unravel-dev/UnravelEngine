---
name: unravel-architect
description: >-
  Guides architectural decisions for UnravelEngine: API design, cross-subsystem
  refactors, and large features. Use for new subsystems, public API changes,
  serialization migrations, or multi-domain features spanning engine and editor.
disable-model-invocation: true
---

# Unravel Architect

Apply when a change affects more than one subsystem or introduces a new public contract.

## Decision framework

### 1. Prefer extension over invention

- Extend `pipeline`, `component_crtp`, `inspector`, `asset_compiler` patterns
- Register into existing registries (`all_components.h`, `inspector_registry`, meta factories)
- Wire through `rtti::context` and `events` - do not introduce parallel globals

### 2. Define the contract first

Document before implementing:

- **Runtime API** - C++ types, context access, event hooks
- **Persistence** - ser20 format, prefab override behavior, migration if breaking
- **Editor surface** - inspector fields, panels, undo actions
- **Script surface** - C# mirror in `engine_data/data/scripts/` if applicable
- **Asset surface** - importer, `.meta` schema, compiled output

### 3. Play mode boundary

Every feature must answer:

- What runs in edit mode only?
- What runs in play mode only?
- What resets on `on_play_end`?
- What survives play mode (scene dirty state)?

Hook via `events` in `engine/engine/events.h`. Use priority on `connect()` when order matters (see `physics_system`, `script_system`).

### 4. Serialization safety

Breaking changes require:

- Version field or backward-compatible LOAD path
- Prefab override path stability (`serialization_path` in inspector overrides)
- No silent UID changes for assets

### 5. Performance posture

- Rendering: minimize pass count and GPU state changes; respect `pipeline_steps` bitmasks
- ECS: prefer system iteration over per-entity polling
- Assets: async/deferred loading via `asset_manager`; no sync load on hot paths
- Editor: defer heavy work; use `threader` for background jobs

## Anti-patterns

| Avoid | Prefer |
|-------|--------|
| New singleton outside `rtti::context` | `ctx.add<T>()` / `ctx.get<T>()` |
| Editor code in `engine/` lib | Keep editor-only in `editor/` |
| Forking bgfx internals | Engine graphics abstraction in `engine/core/graphics/` |
| Hardcoded asset paths | `asset_handle<T>` + protocols |
| Giant all-in-one PR | Split by domain with clear dependency order |

## Review checklist

- [ ] Contract documented (types, events, serialization)
- [ ] Touch points in meta, inspector, assets identified
- [ ] Play mode lifecycle defined
- [ ] No unnecessary public API expansion
- [ ] Migration path for existing projects
- [ ] Build targets and data copy unaffected or updated

## When to escalate to plan mode

- 3+ subsystems touched
- Breaking serialization or asset format
- New CMake target or third-party dependency
- Shader pipeline restructuring

Use `unravel-triage` first to map domains, then domain skills for implementation.
