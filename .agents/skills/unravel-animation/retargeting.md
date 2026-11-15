# Humanoid retargeting

Engine-side contracts. The pipeline from import to posed bones, with troubleshooting and the
MCP calls, is user documentation: `docs/markdown/animation-retargeting.md`. The editor-side
workflow is `editor_data/data/mcp/skills/animation.md`, "Clips from another rig".

## Roles

`engine/engine/animation/animation_humanoid.h` finds the humanoid roles of any skeleton from
structure and rest pose:

- Hips = two legs down and a spine up; hips the legs hang from directly win over a bone
  holding them through a pelvis. Legs are the children reaching lowest, not a longer tail or
  skirt. Legs may hang from a pelvis child.
- Spine runs to where the arms branch; the head chain only climbs; a branch bone named neck is
  the neck.
- Shoulder = a first arm joint near the spine along the thigh-to-thigh axis. Twist segments
  (`.001`) and twist joints (`lShldrTwist`) are skipped.
- Names decide left and right; side-less names take it from the way the toes point. Standard
  Biped maps hips to the spine link its thighs hang from.
- Bones bind by exact name, then by name without FBX namespace (`mixamorig:`, `mixamorig1:`;
  `get_name_without_namespace`), then by role (Rest Aligned only).
- A rig that maps wrong gets added to `test_humanoid_roles_of_common_rigs` (Mixamo, Rigify,
  Biped with both pelvis kinds, VRM, Character Creator, Daz, UE5, the female UE mannequin,
  built from their real bone names); `test_ue_clip_on_a_mixamo_rig` covers a clip whose root
  the target lacks.

## Retarget pose

- Rotations (world delta, scale axis mapping) are measured from the source's retarget pose and
  applied to the target's (`source_reference_global`,
  `animation_target_skeleton::reference_global`).
- The frame fit uses the poses before the target's offsets (`aligned_global`). Between two
  humanoids (hips found on both sides, which needs upright frames) only its heading is kept,
  since differing proportions tilt it (`test_proportions_do_not_tilt_the_frame`).
- Positions, the height ratio and root-motion frames stay on the rest poses.
- The `animation_component` retarget pose t_pose straightens both sides
  (`align_humanoid_to_t_pose`, limbs only);
  offsets are character-space turns (`apply_humanoid_pose_offsets`, right side mirrored).
- Roles set by hand (`apply_humanoid_overrides`) come from the model's import settings
  `rig.humanoid_overrides`, compiled into the mesh (`mesh::get_humanoid_overrides`) and into
  every clip of the file (`animation_clip::humanoid_overrides`, part of the source skeleton
  hash); both are serialized last.
- The retarget skeleton and the bindings key on `animation_system::get_skeleton_version`
  (armature version + the loaded mesh object): a reimport that keeps the bones keeps the
  armature version, so the mesh is what brings changed roles in.
- Tests: `test_t_pose_alignment`, `test_pose_offsets_are_mirrored`,
  `test_retarget_pose_in_playback`, `test_clip_overrides_choose_the_source_bone`.

## Lengths through scaled frames

- Rest-aligned retargeting carries offsets and root motion as lengths through scaled frames
  (`transform_offset` in `animation_player.cpp`). `transform_normal` keeps the input length and
  silently drops an FBX x100 unit scale: a 0.45 m crouch would move 4.5 mm.
- Retargeted crouches, sits and landings are measured against the ground (pelvis and foot
  height above the entity), with x100 FBX sources among them.

## Baked retarget

- A clip that retargets linearly (`is_linear_retarget`: no scale animates, and every channel
  between a mapped channel and its mapped parent holds its rest pose) onto a skeleton whose
  frames are all plain uniform-scale transforms bakes: `bake_retarget` reads two rotations and
  an offset map per mapped bone from one full retarget of the source rest pose, and
  `sample_baked_retarget` samples only the mapped and root-motion channels. Anything else runs
  the full retarget every sample.
- A change to the full retarget keeps both equal: `test_baked_retarget_matches_the_full_retarget`
  compares them through a twin clip whose animated-scale leaf forces the full path. MCP
  `animation_get_retarget` reports `retarget_baked`.

## Reporting a binding

- `animation_player::describe_binding` reports a binding without playing it (match kind per
  channel, unreached bones, height ratio, frame turn, root-motion bones, rest differences per
  limb). It runs the player's own `find_binding`, so it cannot drift from playback.
- `animation_system::make_skeleton_binding` is the binding the system plays with.
- `editor/editor/editing/animation_retarget_check.cpp` adds the warnings and feeds both the
  inspector's Retarget Check and MCP `animation_get_retarget`; MCP `animation_get_humanoid`
  lists the roles of an entity or a clip.
