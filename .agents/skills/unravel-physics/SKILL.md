---
name: unravel-physics
description: >-
  Works on UnravelEngine physics: Bullet3 backend, rigid bodies, collision shapes,
  character controllers, layer filtering, and script collision callbacks. Use for
  physics bugs, collision detection, triggers, or character movement.
disable-model-invocation: true
---

# Unravel Physics

## Start here

| Purpose | Path |
|---------|------|
| Physics system | `engine/engine/physics/ecs/systems/physics_system.h` |
| Backend | `engine/engine/physics/backend/bullet/` |
| Physics component | `engine/engine/physics/ecs/components/physics_component.h` |
| Character controller | `engine/engine/physics/ecs/components/character_controller_component.h` |
| Meta | `engine/engine/meta/ecs/components/physics_component.hpp`, `character_controller_component.hpp` |
| Layer masks | `engine/engine/layers/layer_mask.h` |
| Editor gizmos | `editor/editor/hub/panels/scene_panel/gizmos/` (physics shapes) |
| Script bridge | `engine/engine/scripting/ecs/systems/script_glue.cpp` |

## Architecture

- **Bullet3** backend behind physics abstraction
- `physics_system` owns simulation step in `on_frame_update`
- Play mode lifecycle: `on_play_begin` / `on_play_end` / `on_pause` / `on_resume`
- Components use `component_crtp` + `owned_component` pattern

## Components

### physics_component

Rigid/static bodies, compound shapes, material properties. Serialized and editable in inspector.

### character_controller_component

Kinematic character movement, slope handling, step offset. Separate from raw rigidbody.

## Layer filtering

`layer_mask` + `layer_component` control:

- Which layers collide with which
- Render visibility (separate from physics but same mask type)

Reserved layers in `layer_reserved` enum. Do not hardcode magic layer numbers.

## Collision callbacks

Physics events forward to:

1. Native listeners in physics system
2. `script_system` for C# (`on_collision_enter`, `on_sensor_enter`, etc.)

When adding new collision event types, update physics backend dispatch **and** script glue.

## Editor tools

- Physics shape gizmos in scene panel
- Inspector compound shape editor: `inspector_physics_compound_shape`
- Debug draw via physics backend if available

## Verification checklist

- [ ] Collisions work in play mode (not edit mode unless explicitly supported)
- [ ] Layer mask filtering correct
- [ ] Character controller moves and collides correctly
- [ ] Play/end resets simulation state
- [ ] Sensor vs solid collision distinction preserved
- [ ] C# callbacks fire with correct entity references
- [ ] No bodies left in world after entity destroy

## Common mistakes

- Physics simulation running in edit mode unintentionally
- Mismatched collision shape vs visual mesh
- Forgetting to sync transform after physics step
- Layer mask not set on new entities (default layer only)
- Missing `on_play_end` cleanup
