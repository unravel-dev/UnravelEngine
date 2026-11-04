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
| Joint component | `engine/engine/physics/ecs/components/joint_component.h` |
| Ragdoll component | `engine/engine/physics/ecs/components/ragdoll_component.h` |
| Ragdoll poses (capture / write) | `engine/engine/physics/ecs/systems/ragdoll_poses.h` |
| Ragdoll auto setup | `engine/engine/physics/ragdoll_builder.h` |
| Joint / ragdoll inspectors, Auto Setup | `editor/editor/hub/panels/inspector_panel/inspectors/inspector_physics_components.h` |
| MCP physics tools | `editor/editor/system/mcp/mcp_tools_physics.cpp` (`physics_ragdoll`, `physics_world`) |
| Design notes | `tasks/physics_ragdoll_analysis_2026-11-04.md` |
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
- Box3D mass trap: `b3Body_SetType` and `b3Body_SetMotionLocks` (when a fixed rotation turns on
  or off) call `b3UpdateBodyMassData`, which recomputes the mass from the shape densities and drops
  the authored mass. Re-apply it after either (`update_rigidbody_mass_and_inertia`). Symptom: a
  1 kg rotation-frozen capsule weighs ~2 tons, impulse / force modes do nothing while acceleration
  / velocity-change modes still work
- Forces and torques reach DYNAMIC bodies only, on both backends (`apply_force` returns early
  otherwise). A kinematic body is moved through its ECS transform; velocity writes on it are
  discarded by the next sync
- `physics_system` owns simulation step in `on_frame_update`, at `frame_update_priority::physics`: after the
  scene-system band (animation, root motion) and before scripts. Kinematic bodies and ragdoll hitboxes follow this
  frame's pose, and ragdoll bone writes are not overwritten by the animation
- A paused or frozen game (dt 0) steps nothing, but `physics_system` still runs `capture_ragdoll_animations` and
  `show_ragdolls`: the animation poses bones every frame, so ragdolls must pose them again
- Ragdoll bones are written in their parents' space, so a script that moves the character after the steps (standing
  it where the ragdoll lies before a get-up) would carry the shown pose along for a frame. At
  `frame_update_priority::physics_late`, after LateUpdate, `show_moved_ragdolls` shows again every ragdoll whose
  model entity moved since `show_ragdolls`, without advancing weights. Paused MCP frame steps hide this class of bug
  (the paused frame after a step shows again); catch it in real-time play by reading bone positions every frame
- Box3D kinematic bodies never sleep, so whatever touches them stays awake and keeps counting towards sleep. A body
  a moving kinematic body touches gets its count restarted (`restart_sleep_count`: sleep off for one step, back on
  after it), else a crate a gate or platform lets go of falls asleep where it hangs. `set_body_awake(true)` (C#
  `WakeUp`) restarts it too
- Play mode lifecycle: `on_play_begin` / `on_play_end` / `on_pause` / `on_resume`
- Components use `component_crtp` + `owned_component` pattern

## Components

### physics_component

Rigid/static bodies, compound shapes, material properties. Serialized and editable in inspector.

### character_controller_component

Kinematic character movement, slope handling, step offset. Separate from raw rigidbody.

Characters show between fixed steps (`interpolate`, on by default; `fixed_step_runner` in
`physics_system.h`, no backend code). The transform holds the physics position during the
steps, so `to_physics_cc` / `from_physics_cc` work as before, and a position between the last
two steps afterwards, so scripts, a follow camera and attached bones all read a smooth motion.
`place_character` keeps `dirty_ids::physics` as it was: these writes are not teleports, and a
stale interpolated position must never reach the body. A transform not holding what was last
written (`character_motion::shown`) was moved by someone else and starts over there. Moves are
queued and each step takes its share by time (`take_step_move`): frame-sized moves would make
steps of uneven length, and interpolation would show that as a wobble. Moves made in a fixed
update go straight to the backend.

Dynamic rigid bodies interpolate only with `physics_component::interpolate` on (off by default, as
in Unity): `fixed_step_runner` keeps a `rigidbody_motion` (step start / end pose) and shows
position and rotation between them. A transform whose `dirty_ids::physics` flag is set at the
start of a frame or after the contacts was written by someone else: the backend teleports on
that flag, and the motion starts over there. `place_body` keeps the flag as it was. A camera that
follows a body (a car) stutters at any frame rate that beats against the fixed step unless the
body interpolates.

## Joints

- `joint_component` is an entity of its own (Unreal constraint actor / Godot Joint3D): its transform, `anchor` and
  `axis_rotation` give the joint frame, read once when the joint is made; the frame's X axis is the joint axis.
  `body_a` null = the world (Box3D `world.ground`), `body_b` null = the joint's entity.
- Box3D measures revolute / spherical joints about frame z and slides prismatic joints along x: the backend turns
  the engine frame (`get_joint_axis_rotation`). Wheels: spin z, suspension x in Box3D.
- Wheel steering is a Box3D spring (`steeringHertz`); `tune_wheel` sets it stiff (`wheel_steering_hertz`), since
  Box3D's 1 Hz default cannot turn a loaded tire against the ground's twist friction and the wheel stays straight.
- Box3D's steering term divides by the sine of the angle between the wheel's spin and suspension axes: a wheel
  turned towards its suspension axis (a script righting a chassis without its wheels) gives NaN velocities and the
  `b3IsValidVec3( v )` assert in `solver.c`, which breaks into the debugger. `sync_joint` checks it before every
  step (`apply_wheel_steering`): a wheel more than 45 degrees off square does not steer until its joint has turned it
  back (`test_car_righted_by_its_chassis_alone`). Scripts that teleport a vehicle should still move its wheels with it.
- Box3D has no true cylinder: a cylinder shape is a 16-sided hull (`cylinder_hull_sides`). A wheel on one bumps over
  its faces, which acts as drag that grows with speed and lifts the tire off the ground (wheelspin, little grip).
  Wheels that roll should collide as spheres; the cylinder stays the visual.
- The backend keeps the local frames (`box3d::joint`), so a body made again (type change, reload) gets the same
  joint back; `needs_rebuild` re-reads them from the scene.
- Breaking: thresholds go to `b3JointDef.forceThreshold` / `torqueThreshold`; `b3World_GetJointEvents` after the step
  breaks the joint, `dispatch_contacts` calls `script_system::on_joint_break`.
- Bullet does not implement joints or ragdolls: it warns once.

## Ragdolls

- `ragdoll_component` on the skinned model's entity; bodies by bone name, shapes in bone space (scaled by the bone's
  world scale at build). Joint frames come from the BIND pose (limits are relative to the rest pose).
- Frame order (`physics_system::on_frame_update`): animation already posed the bones -> `capture_ragdoll_animations`
  (a bone still holding what the ragdoll wrote keeps its last animated pose) -> fixed steps (backend syncs kinematic
  targets, drives, impulses; reads poses) -> `show_ragdolls` writes bones parents first, parent-local blend, only the
  root body takes its position, poses interpolated between steps.
- The armature root (root follower) moves with the root body so culling bounds follow the ragdoll.
- The owner's character controller ignores its own ragdoll (mover filter `own_ragdoll`, filter joints with the proxy).
- Contacts: ragdoll shapes carry the bone entity (queries report bones); contacts and sensors see the ragdoll entity
  (`contact_entity_from_shape`).
- Pushes: `add_impulse` (one body, at a point), `add_velocity` (every body), `add_angular_velocity` (every body turns
  as one about a pivot: rolls a lying body over), `add_explosion`.
- MCP `physics_world {"ragdoll_bodies":true}` lists each body's physics position against its drawn bone and the turn
  between them: the check that the drawn pose matches the simulation.
- Tests: `unravel-tests --suite joints` (synthetic mannequin skeleton, no mesh).

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
- [ ] `unravel-tests --suite box3d` green (backend smoke: forces, mass, kinematic, character)
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
- The cylinder primitive is 2 m tall (1 m across) while a physics cylinder defaults to length 1: a scaled cylinder
  primitive needs `{"type":"cylinder","length":2}`
- Judging a ragdoll from screenshots alone: compare `physics_world {"ragdoll_bodies":true}` (physics against drawn)
  before tuning, so a display problem is not mistaken for a physics one
