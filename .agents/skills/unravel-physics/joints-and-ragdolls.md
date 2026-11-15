# Joints, wheels and ragdolls

Box3D only: the Bullet backend builds none of them. Code: `joint_component`,
`ragdoll_component` and `engine/engine/physics/ecs/systems/ragdoll_poses.h`, the Box3D
backend, `engine/engine/physics/ragdoll_builder.h`.

## Joints

- `joint_component` is an entity of its own. Its transform, `anchor` and axis rotation make
  the joint frame, read once when the joint is made (play start, or the component appearing
  during play); the frame's X axis is the joint axis. `body_a` none = the world (Box3D
  `world.ground`), `body_b` none = the joint's own entity, which then needs a physics
  component.
- Box3D measures revolute and spherical joints about frame z and slides prismatic joints along
  x: the backend turns the engine frame (`get_joint_axis_rotation`). Wheels: spin about z,
  suspension along x in Box3D's frame.
- The backend keeps the local frames (`box3d::joint`), so a body made again (type change,
  reload) gets the same joint back; `needs_rebuild` re-reads them from the scene.
- Breaking: thresholds go to `b3JointDef.forceThreshold` / `torqueThreshold`;
  `b3World_GetJointEvents` after the step breaks the joint, and `dispatch_contacts` calls
  `script_system::on_joint_break`.
- A ball joint's `cone_angle` is at most `MAX_CONE_ANGLE` (90 degrees):
  `b3SphericalJoint_SetConeLimit` asserts above pi/2, so every cone limit passes
  `clamp_cone_angle`.

## Wheels

- Steering is a Box3D spring (`steeringHertz`). `tune_wheel` sets it stiff
  (`wheel_steering_hertz`): Box3D's 1 Hz default cannot turn a loaded tire against the
  ground's twist friction, and the wheel stays straight.
- Box3D's steering term divides by the sine of the angle between the wheel's spin and
  suspension axes. A wheel turned towards its suspension axis (a script righting a chassis
  without its wheels) gives NaN velocities and the `b3IsValidVec3( v )` assert in `solver.c`.
  `apply_wheel_steering` runs before every step: a wheel more than 45 degrees off square does
  not steer until its joint has turned it back (`test_car_righted_by_its_chassis_alone`).
  Scripts that teleport a vehicle move its wheels with it.
- Box3D has no true cylinder: a cylinder shape is a `cylinder_hull_sides`-sided hull. A wheel
  on one bumps over its faces, which acts as drag growing with speed and lifts the tire off
  the ground. Rolling wheels collide as spheres; the cylinder stays the visual.

## Ragdolls

- `ragdoll_component` sits on the skinned model's entity: bodies by bone name, shapes in bone
  space (scaled by the bone's world scale at build). Joint frames come from the bind pose, and
  limits are relative to the rest pose.
- A body's `swing_limit` (the ball's cone half angle) is at most 90 degrees, as for joints.
  `swing_offset` turns the centre of the swing cone on the parent's side, away from the bone's
  rest direction, so arms can hang from a T-pose bind pose inside their cone.
- Auto Setup (`build_humanoid_ragdoll`, through `editor_actions::auto_setup_ragdoll`, the
  inspector button and MCP `physics_ragdoll`): capsules fitted to the skin, boxes on the soles
  of the feet. The action refuses play mode and a total mass not above 0 kg, and records one
  undo step only when the bodies or pairs change.
- Frame order inside `physics_system::on_frame_update`: the animation has posed the bones ->
  `capture_ragdoll_animations` (a bone still holding what the ragdoll wrote keeps its last
  animated pose) -> fixed steps (the backend syncs kinematic targets, drives, impulses; reads
  poses) -> `show_ragdolls` writes bones parents first, parent-local blend, poses interpolated
  between steps; only the root body takes its position, the others keep their bone lengths.
- Bones are written in their parents' space, so a script that moves the character after the
  steps (standing it where the ragdoll lies before a get-up) would carry the shown pose along
  for a frame. `show_moved_ragdolls` at `physics_late` shows again every ragdoll whose model
  moved since `show_ragdolls`, without advancing weights. Paused MCP frame steps hide this
  class of bug (the paused frame after a step shows again): catch it in real-time play by
  reading bone positions every frame.
- The armature root follows the root body, so culling bounds follow the ragdoll.
- The owner's character controller ignores its own ragdoll (mover filter `own_ragdoll`, filter
  joints with the proxy body).
- Ragdoll shapes carry the bone entity, so queries report bones; contacts and sensors report
  the ragdoll entity (`contact_entity_from_shape`).
- Pushes: `add_impulse` (one body, optionally at a point), `add_velocity` (every body),
  `add_angular_velocity` (every body turns as one about a pivot: rolls a lying body over),
  `add_explosion`.

## Reading a ragdoll

MCP `physics_world {"ragdoll_bodies":true}` lists, per body, the last step position
(`physics`), the pose drawing interpolates to (`interpolated`, by the ragdoll's
`shown_step_fraction`), the drawn bone and the turn between drawn and interpolated. Compare
drawn with interpolated: `physics` runs up to one step ahead by design. At weight 1 a
simulating body's bone turns as interpolated, and only the root bone also takes its position.

## Verify

`unravel-tests --suite joints` ("box3d joints and ragdolls": a synthetic mannequin skeleton,
no mesh). A new wheel or joint guard gets a test there that fails without it.
