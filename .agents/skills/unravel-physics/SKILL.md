---
name: unravel-physics
description: >-
  UnravelEngine physics on Box3D and Bullet: rigid bodies, shapes, layers, casts, character
  controllers, joints, wheels, ragdolls, collision and sensor callbacks, interpolation between
  fixed steps. Use when bodies ignore forces or fall through, contacts or exits go missing,
  characters or follow cameras stutter, or joints, wheels and ragdolls misbehave.
---

# Unravel physics

Covers the physics system, its two backends and the physics components under
`engine/engine/physics/`, with their inspectors, gizmos and MCP tools in the editor. Joints,
wheels and ragdolls in depth: [joints-and-ragdolls.md](joints-and-ragdolls.md).

## Gotchas

- The backend is a cold boot setting (`settings.physics.backend`, `boot_config.physics`):
  `physics_backend_type` `auto_detect` (Box3D, through `resolve_physics_backend`), `box3d` or
  `bullet`. It is persisted by value, so new values go last, and a change needs a restart.
- Bullet builds no joints and no ragdolls: play start warns once per feature and the
  inspectors say so. Joint and ragdoll work is Box3D work.
- Forces and torques reach dynamic bodies only, on both backends (`apply_force` returns early
  otherwise). A kinematic body moves through its transform; a velocity written to it is
  discarded by the next sync.
- Box3D's `b3Body_SetType` and `b3Body_SetMotionLocks` (when a fixed rotation turns on or off)
  recompute the mass from the shape densities and drop the authored mass: call
  `update_rigidbody_mass_and_inertia` after either. Symptom: an impulse barely moves a
  rotation-frozen body while acceleration and velocity-change modes still work. Read
  `b3Body_GetMass` first when a Box3D body ignores impulses.
- Box3D bakes entity scale into shapes (a scale change rebuilds them) and gives concave meshes
  contacts on static bodies only. `settings.physics.solver_iterations` is Bullet's; Box3D
  solves with `settings.physics.sub_steps` (4, clamped to 1..16) per fixed step.
- Box3D kinematic bodies never sleep. A body a moving kinematic body touches gets its sleep
  count restarted (`restart_sleep_count`), as `set_body_awake(true)` (C# `WakeUp`) does;
  without that, a crate a platform lets go of falls asleep in the air.
- Casts and overlaps go through `physics_system`: `make_cast_query` makes the direction unit
  length, cuts the length to `MAX_QUERY_DISTANCE` (where C#'s default `Mathf.Infinity` lands)
  and drops NaN or zero input. Box3D asserts on a non-finite translation, so no caller runs a
  backend cast directly.
- `RaycastHit.point` is the contact on the surface hit, for shape casts too (not the shape's
  centre), and `distance` is how far the ray or shape travelled before it touched.
- Collision and sensor exits for a destroyed entity come from the `scene::destroy_entity`
  funnel, which announces the subtree on `on_pre_destroy` while everything is intact. An entt
  `on_destroy` hook cannot do it (cross-component access there is undefined, pool order is
  arbitrary). Nothing destroys an entity with a raw `registry.destroy()`.
- A new collision event type is dispatched in both backends (`dispatch_contacts`) and
  forwarded through `script_system` and the C# glue in
  `engine/engine/scripting/ecs/systems/script_glue.cpp`.
- A body's collision category is its entity's `layer_component` (the default layer without
  one) and its mask the component's collision mask; Box3D needs each side to accept the other.
  Layer constants come from `layer_reserved`, never literal bits.
- Poses the physics system writes to show movers between steps go through `place_pose`, which
  keeps `dirty_ids::physics` as it was. A write that raises the flag is a teleport to the
  backend, so an interpolated pose never raises it.
- Characters interpolate by default (`character_controller_component` `interpolate`), dynamic
  bodies only with `physics_component` `interpolate` on (off by default, as in Unity). A camera
  following a body that does not interpolate stutters at any frame rate that beats against the
  fixed step.
- A character move made outside a fixed update is queued and every step takes its share by
  time (`take_step_move`); a move made inside a fixed update goes straight to the backend.
  Frame-sized moves per step make the steps uneven, which interpolation shows as a wobble.
- In C#, an interpolated character's `transform.position` trails `CharacterController.position`
  (where the last step left it) by up to one step. Scripts place a character with `Warp`.
- The cylinder primitive mesh is 2 m tall and 1 m across; a physics cylinder is 1 m long by
  default, so a scaled cylinder primitive needs `{"type":"cylinder","length":2}`.
- A Box3D assert breaks into the debugger. Cone limits and wheel steering are guarded in the
  backend ([joints-and-ragdolls.md](joints-and-ragdolls.md)); any new call into Box3D with
  angles, lengths or directions validates them before the call.
- A ragdoll is judged with MCP `physics_world {"ragdoll_bodies":true}` (drawn bone against
  interpolated pose) before tuning, so a display problem is not taken for a physics one.

## How it works

- Frame order (`frame_update_priority` in `engine/engine/events.h`): `physics_system` steps in
  `on_frame_update` at `physics`, after the scene-system band (animation, root motion) and
  before scripts, so kinematic bodies and ragdoll hitboxes follow this frame's pose and
  scripts see the stepped world. At `physics_late`, after script LateUpdate,
  `show_moved_ragdolls` shows again every ragdoll whose model a script moved since.
- Nothing steps unless `play_mode::is_simulation_running()`. The first running frame and a
  paused simulation run with dt 0: no steps, but ragdolls are still captured and shown,
  because the animation poses their bones every frame. `on_play_begin` resets the step runner
  and every mover's motion, then calls the backend's own `on_play_begin`.
- `fixed_step_runner` (`physics_system.h`) runs the steps a frame's game time pays for
  (`settings.physics.fixed_timestep`, at most `max_fixed_steps`; time beyond is dropped).
  During the steps a mover's transform holds where it really is, which is what the backend
  syncs; afterwards it is placed between the last two step poses by `get_step_fraction()`.
  Each mover keeps a `stepped_motion` (`physics_types.h`, runtime only): a transform whose
  local pose changed since it was drawn was written (a teleport, the motion starts over
  there); the same local pose somewhere else was carried by a moving parent (the step poses
  move along). A body shows its rotation between steps; a character's rotation is the
  scripts'.
- Box3D's character controller is a capsule mover (`b3World_CastMover`,
  `b3World_CollideMover`, `b3SolvePlanes`) with a kinematic proxy body, so sensors and
  dynamic bodies still see it.
- Contact bookkeeping lives in `engine/engine/physics/backend/contact_graph.h`;
  `contact_event_flags` on the component choose what a body reports, sensor exit on destroy
  included. `dispatch_contacts` hands events to `script_system` (`on_collision_enter`,
  `on_sensor_enter`, `on_joint_break`, ...) after the step.
- Components follow `component_crtp` + `owned_component`; their reflection is in
  `engine/engine/meta/ecs/components/` (`physics_component.hpp`,
  `character_controller_component.hpp`, `joint_component.hpp`, `ragdoll_component.hpp`), and
  physics materials in `engine/engine/meta/physics/physics_material.cpp`.

## Procedures

- Joints, wheels, ragdolls, Auto Setup: [joints-and-ragdolls.md](joints-and-ragdolls.md).
- A body ignores forces: check its type (dynamic only), then on Box3D its mass
  (`b3Body_GetMass` against the authored mass), then its motion locks.
- A new backend feature: implement it in both backends, or have the other warn once at play
  start and the inspector say so, as Bullet does for joints.
- A stutter: compare the mover's `interpolate` flag, then whether something writes its
  transform every frame (a write restarts the motion), then the camera's update order.

## Verify

- `unravel-tests --suite physics` runs "physics contacts / destroy funnel";
  `--suite box3d` runs "box3d backend smoke" (forces, mass, kinematic, characters,
  interpolation on both backends) and "box3d joints and ragdolls"; `--suite joints` runs the
  latter alone.
- Play-mode smoke: bodies collide and come to rest, sensors fire enter and exit, the
  character walks and steps, and ending play leaves no bodies (MCP `physics_world`).
- Profiler scopes `Physics/*` (`Physics/Box3D/Simulate Step`, `Physics/Fixed Update`,
  `Physics/Ragdolls/Show`) for cost.

## Entry points

| Purpose | Path |
|---------|------|
| System, fixed steps, interpolation | `engine/engine/physics/ecs/systems/physics_system.h` |
| Backend interface and factory | `engine/engine/physics/backend/physics_backend.h`, `engine/engine/physics/backend/physics_backend_factory.cpp` |
| Box3D backend | `engine/engine/physics/backend/box3d/box3d_backend.cpp` |
| Bullet backend | `engine/engine/physics/backend/bullet/bullet_backend.cpp` |
| Components | `engine/engine/physics/ecs/components/` |
| Shared types, `stepped_motion` | `engine/engine/physics/physics_types.h` |
| Ragdoll capture and show | `engine/engine/physics/ecs/systems/ragdoll_poses.h` |
| Inspectors | `editor/editor/hub/panels/inspector_panel/inspectors/inspector_physics_components.h`, `editor/editor/hub/panels/inspector_panel/inspectors/inspector_physics_shape.h` |
| Gizmos | `editor/editor/hub/panels/scene_panel/gizmos/gizmos/gizmo_physics_component.h` |
| MCP tools (`physics_world`, `physics_ragdoll`) | `editor/editor/system/mcp/mcp_tools_physics.cpp` |
