---
name: unravel-animation
description: >-
  Works on UnravelEngine animation: skeletal animation, clips, blend spaces,
  animation components, root motion, IK, and animation panel integration. Use for
  skeletal animation, blend trees, clip playback, root motion, IK, or skinned mesh
  animation bugs.
---

# Unravel Animation

## Start here

| Purpose | Path |
|---------|------|
| Animation core | `engine/engine/animation/` |
| Animation component | `engine/engine/animation/ecs/components/animation_component.h` |
| Bone component | `engine/engine/ecs/components/` (bone hierarchy) |
| Animation system | `engine/engine/animation/ecs/systems/animation_system.h` |
| Clips | `engine/engine/animation/animation.h` |
| Meta | `engine/engine/meta/ecs/components/animation_component.hpp` |
| Editor panel | `editor/editor/hub/panels/animation_panel/` |
| Skinned rendering | `engine/engine/rendering/` (skinned submeshes in pipeline_stats) |
| Player, sampling, root motion, retargeting | `engine/engine/animation/animation_player.cpp` |
| Controllers (.actrl) | `engine/engine/animation/animation_controller*.h/.cpp` |
| Clip import (root node, rest pose) | `engine/engine/assets/impl/importers/mesh/mesh_import_animation.cpp` |
| Rig modifiers (IK, springs) | `engine/engine/animation/ecs/components/ik_components.h`, `ecs/systems/rig_system.cpp` |
| Clip timeline (events, curves, preview) | `editor/editor/hub/panels/inspector_panel/inspectors/animation_clip_timeline.cpp` |
| Rig inspectors / gizmos | `inspector_rig_components.cpp`, `scene_panel/gizmos/gizmos/gizmo_rig_components.cpp` |

## Architecture

- Skeletal animation drives bone transforms
- `animation_component` references clips via `asset_handle<animation_clip>`
- `animation_system` updates bone poses each frame
- Skinned meshes submitted in deferred pipeline (separate draw path from static)

## Root motion (contract)

- The importer picks the root node on the skeleton's trunk: the shallowest
  animated node from the scene root down to where the skeleton branches (the
  hips). A clip that animates nothing there gets the hips (no motion), never a
  limb or spine bone. A node keyed with one value throughout counts as animated:
  in-place clips key an all-zero root bone, which keeps the pelvis sway out of
  root motion. Under the scene root the trunk follows the rig with the most
  skeleton nodes. A file without meshes (animation-only export) takes every
  animated node and its ancestors as its skeleton, and a mesh-less FBX root gets
  the axis correction Assimp only gives files with meshes
  (`correct_meshless_fbx_root`).
- Keys live in the root node's parent frame, which need not be upright (FBX
  armatures stand Z-up or -Z-up below a rotated, x100-scaled node). The
  pose/entity split is made with the keys turned upright by the parent's rest
  rotation (`root_motion_frame::up`), never on the keys' own Y.
- Only the heading (twist about up) is extracted as rotation; lean and roll stay
  in the pose.
- Extracted axes hold a baseline in the pose and are written whole (bone weights
  are uniform: per-axis weights would be applied in a frame that may not be
  upright). Floor-level root bones lock to their rest (UE's RefPose root lock):
  combo clips start where the previous clip ended. Hips lock to their first
  frame, which carries the stance.
- The delta lives in the root node's parent frame. `animation_system` maps it
  through the local transforms up to the model entity, moves the entity once per
  frame along its own axes (with its scale) and then post-multiplies the turn.
  When rotation is extracted and the rotation root is not an ancestor of the
  position root, the step is rebased by the turn taken so far
  (`rebase_root_translation`); headings are absolute twists, so per-step turns
  telescope to the clip's total.
- A tracking that starts (state start, switch, crossfade target, seek, blend point
  entering, culled character shown again) measures from where the time step
  began (`animation_state::step_begin_*`), so the first step moves the entity and
  travel does not depend on the frame rate; the jump of a seek itself moves
  nothing. A crossfade blends the deltas by the end-of-step weight (as UE does);
  a frozen crossfade source is a static pose and moves the entity no further.
  Characters whose poses are skipped drop the motion they missed.
- A root the target lacks (a UE clip root on a Mixamo character) reports through
  its first mapped descendant; root motion converts by the heading of the frame
  fit only and scales by the hips' height ratio.
- An entity with a character controller in play mode is moved through
  `character_controller_component::move`, so root motion collides. The controller
  moves on physics fixed steps (50 Hz by default), so at 60 Hz frames the entity
  lags a frame of root motion now and then; probe travel with paused play and
  `play_skip_frame`, comparing root motion on against the pelvis with it off.
- Rest-aligned retargeting carries offsets and root motion as LENGTHS through
  scaled frames (`transform_offset`); `transform_normal` keeps the input length
  and silently drops a x100 unit scale.
- A blend space whose clips share one source skeleton and retarget linearly
  (`is_linear_retarget`: no scale animates, and every channel between a mapped
  channel and its mapped parent holds its rest pose) is blended in the source
  skeleton and retargeted once; root motion still converts per clip. Anything
  else retargets each clip and blends after. A change to the retarget must keep
  the two equal (`test_blend_space_retargets_once_like_each_clip`).

## Humanoid retargeting

The whole pipeline, from import to the posed bones, with troubleshooting and MCP calls:
[docs/markdown/animation-retargeting.md](../../../docs/markdown/animation-retargeting.md).

`animation_humanoid.h` finds the 22 humanoid roles of any skeleton from structure
and rest pose (hips = two legs down + a spine up, and hips the legs hang from
directly win over a bone holding them through a pelvis; legs are the children
reaching lowest, not a longer tail or skirt; spine to where the arms branch; head
chain only climbs; shoulder = a first arm joint near the spine along the
thigh-to-thigh axis; twist segments `.001` and twist joints (`lShldrTwist`)
skipped; legs may hang from a pelvis child; a branch bone named neck is the
neck). Names decide left/right; side-less names take it from the way the toes
point. Standard Biped maps hips to the spine link its thighs hang from.
Bones bind by exact name, then by name without FBX namespace (`mixamorig:` and
`mixamorig1:`, `get_name_without_namespace`), then by role (rest-aligned only).
Tests build Mixamo, Rigify, Biped (both pelvis kinds), VRM, Character Creator,
Daz, UE5 and the female UE mannequin layouts from their real names
(`test_humanoid_roles_of_common_rigs`), and a UE clip on a Mixamo-shaped target
(`test_ue_clip_on_a_mixamo_rig`); add a rig there when one maps wrong. Tools:
inspector Humanoid Mapping, MCP `animation_get_humanoid` (entity or clip).

Retarget pose: rotations (world delta, scale axis mapping) are measured from the source's
retarget pose and applied to the target's (`source_reference_global`,
`animation_target_skeleton::reference_global`); the frame fit uses the poses
before the target's offsets (`aligned_global`); between two humanoids (hips found on both
sides, which needs upright frames) only its heading is kept, since differing proportions tilt
it (`test_proportions_do_not_tilt_the_frame`). Positions, the height ratio and
root-motion frames stay on the rest poses. `retarget_pose` t_pose straightens
both sides (`align_humanoid_to_t_pose`, limbs only); offsets are character-space
turns (`apply_humanoid_pose_offsets`, right side mirrored). Roles set by hand
(`apply_humanoid_overrides`) come from the model's import settings
`rig.humanoid_overrides`, compiled into the mesh (`mesh::get_humanoid_overrides`)
and into every clip of the file (`animation_clip::humanoid_overrides`, part of
the source skeleton hash); both are serialized LAST. The retarget skeleton and
the bindings key on `animation_system::get_skeleton_version` (armature version +
the loaded mesh object): a reimport that keeps the bones keeps the armature
version, so the mesh is what brings changed roles in. Tests:
`test_t_pose_alignment`, `test_pose_offsets_are_mirrored`,
`test_retarget_pose_in_playback`, `test_clip_overrides_choose_the_source_bone`.

`animation_player::describe_binding` reports a binding without playing it
(match kind per channel, unreached bones, height ratio, frame turn, root-motion
bones, rest differences per limb); it runs the player's own `find_binding`, so
it cannot drift from playback. `animation_system::make_skeleton_binding` is the
binding the system plays with. `editor/editing/animation_retarget_check` adds
the warnings and feeds both the inspector's Retarget Check and MCP
`animation_get_retarget`. The MCP-side workflow is the editor skill
`editor_data/data/mcp/skills/animation.md`.

## Serialization

Compiled clips are binary and binary archives are positional: every LOAD must
read fields in exactly the SAVE order (`test_clip_curves_survive_both_archives`).
The loader resolves the lazy caches of every channel's rest transforms
(`resolve_rest_transforms`) before the clip is shared: players on pool threads
then only read them.

## Rig modifiers

Non-destructive: `before_skinning` saves every local a modifier may write and
solves on top. In edit mode (and prefab / thumbnail scenes) `after_skinning` puts
the input pose back right after skinning, so editing and saving never see a
solved pose. In the scene being played the solved pose is held through the
render - attachments under solved bones (a weapon in the hand, a head camera)
and plain entity rigs show it - and comes back at the start of the next frame
(`restore_deferred`, right before the animation update). Edit-mode reads (MCP
queries) see the animated pose; play-mode reads before the animation update see
the last rendered one. Entities no modifier changed are not touched. Per-scene
rig state lives in the registry context.

Modifiers run parents first: by the hierarchy depth of the highest joint each
writes (ties: foot, two-bone, chain, look-at, spring), so a look-at turning the
chest runs before the hand IK on that arm and the hand still lands. Chain walks
go through `ik_get_chain_parent` (solvers, rig, gizmos, inspectors).

Goals ease with `smoothing` (time constant, frame-rate independent): two-bone and
chain IK keep an `ik_goal_state` that starts at the animated effector and follows
the target, so a swapped or jumping target never snaps the limb; the weight eases
with the same constant, and a lost target holds the last goal while the weight
fades out. Look-at smooths its target and weight the same way. A modifier that
stops running (inactive entity, no joints) resets and fades back in. A modifier
at weight 0 whose output has faded out is left out of the frame entirely (no pose
save, no solve, no ground probes), so switching rigs off by weight is free. Springs
step at 60 Hz and show the joints interpolated between steps. Runtime state, not
serialized; new serialized fields go LAST in SAVE / LOAD (binary archives are
positional).

## Frame ordering (contract)

Scene systems (incl. animation) run BEFORE script `OnUpdate`
(`frame_update_priority` in `engine/engine/events.h`): animation_system rewrites
every animated bone's local transform, so bone writes made before it (IK,
procedural offsets) are discarded the same frame. Script writes land before
model_system's skinning pass in `on_frame_before_render`. Do not connect animation
consumers at an unnamed priority.

Rig modifiers (`rig_system::before_skinning`) run per character group on the
thread pool once there are `MIN_PARALLEL_GROUPS` (32) groups: a group is a
character with every character nested in its bones, its jobs keep the
parents-first order, and it writes only inside its own hierarchy. World
transforms resolve lazily into caches, so what groups read but none writes (the
roots' ancestors, targets, poles, colliders) is resolved on the main thread
first. A group whose target sits in another group's hierarchy runs after the
parallel groups and sees that character's solved pose. A frame where a joint lies
outside its group (above a root, in another character) runs serially in one
shallowest-first list. Ground probes are safe from several threads: between steps
both physics backends answer read-only queries (Box3D documents its spatial
queries as read-only; Bullet is built with BT_THREADSAFE). The hand back after
skinning and the deferred restore run per group as well. Keep new modifiers
writing only their own joints, and list every entity they read in
`for_each_rig_reference`.

## Blend spaces

Blend space assets combine multiple clips by parameters (e.g. speed, direction). Defined in animation module - read existing blend space types before adding new ones.

## Pose blending (contract)

- Every blended node carries `coverage`: the share of the blend that animates the
  bone. The final pose completes the rest by `resolve_coverage`: against the bind
  pose in the base layer, against the base in an override layer (a bone the layer
  lacks keeps the base), and coverage scales the change of an additive layer.
- A bone that leaves the final pose (a crossfade ends, a blend space narrows, a
  layer stops) gets one rest write (`reset_unwritten_bones`, written-bones hash).
- A crossfade completes in the time step, so culled characters leave
  transitions. A crossfade in flight is frozen as the new source only when it is
  shown, or when the same motion restarts; another motion's source keeps playing.
- A reversed crossfade starts from the weight on screen (`blend_state::start_weight`)
  and eases from there with the reversal's own easing over its duration.

## Controller (contract)

- Entering a state always restarts its motion. Editing a playing controller
  rebuilds by name and keeps the player's motion (adopting it when unchanged).
- Triggers reset after every layer evaluated; exit time with a zero fraction
  (1.0, 2.0) on a loop fires only on crossing k + exit.

## ECS integration

- `animation_component` on animated entities
- `bone_component` / `submesh_component` for hierarchy
- `model_component` links mesh to skeleton

## Pipeline stats

`pipeline_stats` tracks:

- `drawn_skinned_models`, `drawn_skinned_submeshes`
- Separate from static `drawn_models` counts

## Editor

`animation_panel/` - clip preview, blend editing. Changes may require asset reimport for clip data.

## Verification checklist

- [ ] `unravel-tests --suite animation` green (blend spaces, root motion wraps,
      replay); `--suite ik` for IK solver work
- [ ] Clip plays correctly in play mode
- [ ] Bone transforms propagate to skinned mesh
- [ ] Blend space parameters interpolate smoothly
- [ ] Animation assets import and resolve via `asset_handle`
- [ ] No T-pose flash on play begin
- [ ] Performance acceptable (skinned draw count in stats)
- [ ] Root motion: entity position before / after matches the clip's travel,
      and no bone drifts from the entity then slides back on the next clip
- [ ] Retargeted crouches / sits / landings touch the ground (FBX x100 sources)
- [ ] Moves judged frame by frame (paused play + `play_skip_frame` captures)
- [ ] After player / system / binding changes: play the sample Gallery and read
      the hero's pelvis height (about 0.9 m above the entity); unit tests build
      target skeletons by hand and miss the system's own skeleton description
- [ ] Cost per character: the sample's CrowdBenchmark scene (SetCount / SetRigs
      through scripts_invoke, profiler scopes Animation/* and Model/*)

## Common mistakes

- Animating in edit mode when only play mode should run
- Missing bone hierarchy on skinned model
- Clip asset not loaded before playback start
- Forgetting meta registration for new animation fields
