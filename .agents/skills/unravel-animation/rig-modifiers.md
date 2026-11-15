# Rig modifiers (IK and spring bones)

Components: `engine/engine/animation/ecs/components/ik_components.h` (two-bone, chain,
look-at, foot IK, spring bones). Runner: `engine/engine/animation/ecs/systems/rig_system.h`.
Solvers: `engine/engine/rendering/ecs/systems/ik_solvers.h`. Editor:
`editor/editor/hub/panels/inspector_panel/inspectors/inspector_rig_components.cpp`,
`editor/editor/hub/panels/scene_panel/gizmos/gizmos/gizmo_rig_components.cpp`.

## Non-destructive solve

- `before_skinning` saves the local transform of every entity a modifier may write and solves
  on top of the animated pose; skinning reads the result.
- In edit mode, prefab and thumbnail scenes, `after_skinning` puts the input pose back right
  after skinning, so editing and saving never see a solved pose.
- In the scene being played the solved pose is held through the render, so attachments under
  solved bones (a weapon in the hand, a head camera) and plain entity rigs show it. It comes
  back at the start of the next frame (`restore_deferred`, right before the animation update),
  unless something moved the entity in between.
- Edit-mode reads (MCP queries) see the animated pose; play-mode reads before the animation
  update see the last rendered one. Entities no modifier changed are not touched.
- Per-scene rig state lives in the registry context and goes away with it.

## Order

- Modifiers run parents first: by the hierarchy depth of the highest joint each writes, ties
  in `rig_modifier` order (foot, two-bone, chain, look-at, spring). A look-at turning the chest
  runs before the hand IK on that arm, and the hand still lands.
- Chain walks go through `ik_get_chain_parent` in solvers, rig, gizmos and inspectors alike: a
  chain on a skinned bone ends below the armature container, plain entity hierarchies walk any
  parent.

## Threads

- `before_skinning` runs per character group on the thread pool once there are
  `MIN_PARALLEL_GROUPS` groups. A group is a character with every character nested in its
  bones; its jobs keep the parents-first order, and it writes only inside its own hierarchy.
- World transforms resolve lazily into caches without synchronization, so what groups read but
  none writes (the roots' ancestors, targets, poles, colliders) is resolved on the main thread
  first. Any new pool pass that reads world transforms of entities sharing an ancestor calls
  `transform_component::resolve_shared_ancestors()` first, as `model_system` does.
- A group whose target sits in another group's hierarchy runs after the parallel groups and
  sees that character's solved pose. A frame where a joint lies outside its group (above a
  root, in another character) runs serially in one shallowest-first list.
- Ground probes run from several threads: between steps both physics backends answer
  read-only queries (Box3D documents its spatial queries as read-only; Bullet is built with
  `BT_THREADSAFE`). Probes need the physics world, so foot IK grounds only in the scene being
  played.
- The hand back after skinning and the deferred restore run per group as well.
- A new modifier writes only its own joints and lists every entity it reads in
  `for_each_rig_reference`.

## Smoothing and weights

- Goals ease with `smoothing` (a time constant, frame-rate independent). Two-bone and chain IK
  keep an `ik_goal_state` that starts at the animated effector and follows the target, so a
  swapped or jumping target never snaps the limb; the weight eases with the same constant,
  and a lost target holds the last goal while the weight fades out. Look-at smooths its
  target and weight the same way.
- A modifier that stops running (inactive entity, no joints) resets and fades back in.
- A modifier at weight 0 whose output has faded out is left out of the frame entirely (no
  pose save, no solve, no ground probes), so switching rigs off by weight costs nothing.
- Springs step at a fixed 60 Hz (`SPRING_STEP`) and show their joints interpolated between
  steps.
- Goal, weight and spring state are runtime state, not serialized. New serialized fields go
  last in SAVE and LOAD (binary archives are positional).

## Verify

`unravel-tests --suite ik` ("ik solvers"). Prefer two-bone IK for limbs: CCD converges slowly
when a chain retracts.
