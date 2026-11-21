---
name: unravel-animation
description: >-
  UnravelEngine skeletal animation: clips, the animation player, controllers and blend spaces,
  root motion, humanoid retargeting, IK and spring bones, the Animation Controller editor. Use
  when clips play wrong, characters slide, float or sink, root motion drifts, retargeted rigs
  break, IK or springs misbehave, or crowds cost too much.
---

# Unravel animation

Covers `engine/engine/animation/` (clips, player, blending, controllers, humanoid roles, the
animation and rig systems) and the Animation Controller editor in
`editor/editor/hub/panels/animation_panel/`. Deep contracts:
[root-motion.md](root-motion.md), [retargeting.md](retargeting.md),
[rig-modifiers.md](rig-modifiers.md).

## Gotchas

- `animation_system` runs in the `scene_systems` band of `frame_update_priority`
  (`engine/engine/events.h`), before physics and scripts, and rewrites every animated bone's
  local transform each frame: a bone write made before it is lost the same frame. Procedural
  bone writes belong in scripts (OnUpdate, LateUpdate) or rig modifiers, and no animation
  consumer connects at an unnamed priority.
- Skinning reads bones in `model_system` during `on_frame_before_render`, after scripts; rig
  modifiers solve right before it (`rig_system::before_skinning`) and are non-destructive
  ([rig-modifiers.md](rig-modifiers.md)).
- Compiled clips are binary documents read by name (`engine/engine/meta/animation/animation.cpp`);
  their keys are packed lists, whose rows the key's SERIALIZE reads in the order it wrote them. A
  change to what a compiled clip holds bumps `get_format_version<animation_clip>` in
  `engine/engine/assets/impl/asset_extensions.h` (`test_clip_curves_survive_both_archives`).
- The clip loader resolves every channel's lazy rest transforms (`resolve_rest_transforms`)
  before the clip is shared: players on pool threads only read clips. Anything lazy added to a
  clip is resolved there too.
- An animation asset extension never starts with `.anim`: `is_format` matches by substring,
  so it would claim clip files (controllers are `.actrl`).
- Rest-aligned retargeting carries offsets and root motion as lengths through scaled frames
  (`transform_offset`). `transform_normal` keeps the input length and drops an FBX x100 unit
  scale, so a 0.45 m crouch moves 4.5 mm.
- Root motion splits pose and entity on keys turned upright by the parent's rest rotation
  (`root_motion_frame::up`), never on the keys' own Y: FBX armatures stand Z-up below a
  rotated x100 node.
- A sampler rewrite that depends on a consumer's setting receives that setting: the
  clip-start baseline is pinned into the pose only when root motion is applied
  (`extract_root_motion`, from `animation_component::get_apply_root_motion()`); with root
  motion off the authored motion stays in the pose.
- The baked retarget (`bake_retarget`, `sample_baked_retarget`) and the full retarget give the
  same pose: a change to one changes both
  (`test_baked_retarget_matches_the_full_retarget`).
- Unit tests build target skeletons by hand and miss the system's own skeleton description
  (`animation_target_skeleton::set_bones`, `animation_system::make_skeleton_binding`). After
  a change to the player, the system or bindings, play the Gallery scene of the animation
  sample (the sibling repository UnravelEngine-Sample-Animation) and read the hero's pelvis
  height: about 0.9 m above the entity. Pure parts of system code get a unit test.
- Animation is verified with numbers and frame strips, not one capture of a loop: pelvis and
  foot height above the entity in a crouch, the entity position before and after a root-motion
  move, the pelvis offset from the entity across a whole combo, captured with paused play and
  `play_skip_frame`.
- A character with a character controller moves on physics fixed steps and is shown between
  them, trailing the move by a frame or two; root-motion travel is compared against the
  controller's real position.
- Pool passes over animated entities group them by their topmost animated ancestor: a
  character nested in another's bones (a rider, an attachment) updates in its parent's group.
  World transforms resolve lazily without locks, so a new pool pass reading them calls
  `transform_component::resolve_shared_ancestors()` first.
- Pose and bounds math runs glm with intrinsics and aligned types
  (`engine/core/math/detail/glm_includes.h`); arm64 takes NEON paths x86 never runs. Pose,
  inverse or culling bugs seen only on Android are checked against glm's NEON code.
- Animation tests: asset handles made in a test get distinct uids
  (`set_internal_ids(generate_uuid())`), or handle equality treats two clips as one.

## How it works

- Assets: clips are `animation_clip` (`engine/engine/animation/animation.h`, `.anim`, made by
  the mesh importer, one per take); controllers are `animation_controller` (`.actrl`, a text
  archive): parameters, layers with bone masks, states and transitions. A state plays a clip
  or a blend space (`blend_1d`, `blend_2d_cartesian`, `blend_2d_directional`).
- `animation_component` holds the controller or clip, the player and the root-motion and
  retarget settings; `animation_controller_instance` decides what the player plays before time
  advances, so a transition it fires shows in this frame's pose.
- `animation_player` samples, blends, retargets and extracts root motion. Every blended node
  carries `coverage`, the share of the blend that animates the bone; `resolve_coverage`
  completes the rest against the bind pose in the base layer and against the base in an
  override layer, and coverage scales an additive layer's change. A bone that leaves the final
  pose gets one rest write (`reset_unwritten_bones`).
- Crossfades complete in the time step, so culled characters leave transitions. A crossfade in
  flight is frozen as the new source only when it is shown or the same motion restarts. A
  reversed crossfade starts from the weight on screen (`blend_state::start_weight`).
- Controller contract: entering a state restarts its motion; editing a playing controller
  rebuilds by name and keeps the player's motion; a trigger fires one transition per layer
  that tests it and resets after every layer evaluated; exit time with a zero fraction (1.0,
  2.0) on a loop fires only on crossing k + exit.
- `animation_system` runs each group on the thread pool: the controller update, the pose and
  the root-motion move of every entity in it (a group writes only inside its own hierarchy).
  Bindings and the retarget skeleton key on `animation_system::get_skeleton_version`.
- With renderer-based culling a character not drawn last frame skips its pose and drops the
  root motion it missed.

## Procedures

- Root motion work: [root-motion.md](root-motion.md).
- Retargeting, humanoid roles, binding reports: [retargeting.md](retargeting.md).
- IK, look-at, foot IK, spring bones: [rig-modifiers.md](rig-modifiers.md).
- Driving the editor (import settings, controllers, previews, checks): the MCP guide
  `editor_data/data/mcp/skills/animation.md`.
- A new serialized field on a component: the cross-cutting list in AGENTS.md, reflection in
  `engine/engine/meta/ecs/components/animation_component.cpp` or `ik_components.cpp`.

## Verify

- `unravel-tests --suite animation` runs "animation" and "baked animation"; `--suite ik` runs
  "ik solvers"; `--suite skinning` runs "mesh skinning" and "skinning palettes".
- Build both configured trees: file-local helper names in animation sources clash only in
  another configuration's unity batches (`unravel-build-verify`).
- Smoke: the animation sample's Gallery scene in play (pelvis about 0.9 m above the entity, no
  T-pose flash at play start), retargeted crouches and landings touching the ground.
- Cost per character: the sample's CrowdBenchmark scene, `SetCount` / `SetRigs` through MCP
  `scripts_invoke`, profiler scopes `Animation/*` and `Model/*`.

## Entry points

| Purpose | Path |
|---------|------|
| Player: sampling, blending, retargeting, root motion | `engine/engine/animation/animation_player.cpp` |
| Clips, channels, root-motion settings | `engine/engine/animation/animation.h` |
| Controllers and their runtime | `engine/engine/animation/animation_controller.h`, `engine/engine/animation/animation_controller_instance.h` |
| Humanoid roles | `engine/engine/animation/animation_humanoid.h` |
| Systems | `engine/engine/animation/ecs/systems/animation_system.h`, `engine/engine/animation/ecs/systems/rig_system.h` |
| Component | `engine/engine/animation/ecs/components/animation_component.h` |
| Clip import, root node | `editor/core/asset_compiler/importers/mesh/mesh_import_animation.cpp` |
| Controller editor | `editor/editor/hub/panels/animation_panel/animation_panel.h` |
| Clip timeline (events, curves) | `editor/editor/hub/panels/inspector_panel/inspectors/animation_clip_timeline.cpp` |
| Retarget check, MCP tools | `editor/editor/editing/animation_retarget_check.cpp`, `editor/editor/system/mcp/mcp_tools_animation.cpp` |
