---
name: unravel-physics
description: >-
  Works on UnravelEngine physics: Bullet3 and Box3D backends, rigid bodies, collision
  shapes, character controllers, layer filtering, and script collision callbacks. Use
  for physics bugs, collision detection, triggers, or character movement.
---

# Unravel Physics

## Start here

| Purpose | Path |
|---------|------|
| Physics system | `engine/engine/physics/ecs/systems/physics_system.h` |
| Backend interface | `engine/engine/physics/backend/physics_backend.h` (+ `physics_backend_factory.cpp`) |
| Bullet backend | `engine/engine/physics/backend/bullet/` |
| Box3D backend | `engine/engine/physics/backend/box3d/` |
| Physics component | `engine/engine/physics/ecs/components/physics_component.h` |
| Character controller | `engine/engine/physics/ecs/components/character_controller_component.h` |
| Meta | `engine/engine/meta/ecs/components/physics_component.hpp`, `character_controller_component.hpp` |
| Layer masks | `engine/engine/layers/layer_mask.h` |
| Editor gizmos | `editor/editor/hub/panels/scene_panel/gizmos/` (physics shapes) |
| Script bridge | `engine/engine/scripting/ecs/systems/script_glue.cpp` |

## Architecture

- Two backends behind `physics_backend`: **Box3D** (engine default) and **Bullet3**. The
  backend is a cold boot setting (`settings.physics.backend`, `boot_config.physics`,
  `physics_backend_type`: `auto_detect` (default, resolves to Box3D via
  `resolve_physics_backend`), `box3d`, `bullet`); it is persisted by value, so keep the
  enum order. Changing it requires a process restart
- Box3D specifics: entity scale is baked into shapes (a scale change rebuilds them),
  the authored mass overrides the density-derived mass, `solver_iterations` is Bullet-only
  (Box3D steps once per fixed step with a fixed solver sub-step count, `sub_step_count` in
  `box3d_backend.cpp`), concave meshes only collide on static bodies, and the character controller
  is a capsule mover (`b3World_CastMover` / `b3World_CollideMover` / `b3SolvePlanes`)
  with a kinematic proxy body so sensors and dynamic bodies still see it
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

## Contact exits on entity destroy (funnel)

Destroying an entity mid-contact must still deliver `on_collision_exit` /
`on_sensor_exit`. This works through the destruction funnel, not entt hooks (an entt
`on_destroy` hook cannot do it - cross-component access there is UB, pool order is
arbitrary):

- `scene::destroy_entity` announces the subtree on the `on_pre_destroy` bus
  (`engine/engine/ecs/scene.h`) while everything is still intact
- The backend's contact bookkeeping lives in
  `engine/engine/physics/backend/contact_graph.h` (see `contact_event_flags`)
- Never bypass the funnel with a raw `registry.destroy()`

Covered by the `physics contacts / destroy funnel` test suite.

## Editor tools

- Physics shape gizmos in scene panel
- Inspector compound shape editor: `inspector_physics_compound_shape`
- Debug draw via physics backend if available

## Verification checklist

- [ ] `unravel-tests --suite physics` green (contacts / destroy funnel)
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
