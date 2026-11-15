# Root motion

The contract from import to the moved entity. Code: `find_root_motion_node` in
`editor/core/asset_compiler/importers/mesh/mesh_import_animation.cpp`, `root_motion_params`
in `engine/engine/animation/animation.h`, sampling and tracking in
`engine/engine/animation/animation_player.cpp`, the entity move in
`engine/engine/animation/ecs/systems/animation_system.cpp`. The user-facing settings table is
in `editor_data/data/mcp/skills/animation.md`, "Root motion".

## Root node (import)

- The importer picks the root node on the skeleton's trunk: the shallowest animated node from
  the scene root down to where the skeleton branches (the hips). A clip that animates nothing
  there gets the hips (no motion), never a limb or spine bone.
- A node keyed with one value throughout counts as animated: in-place clips key an all-zero
  root bone, which keeps the pelvis sway out of root motion.
- Under the scene root the trunk follows the rig with the most skeleton nodes.
- A file without meshes (animation-only export) takes every animated node and its ancestors as
  its skeleton, and a mesh-less FBX root gets the axis correction Assimp only gives files with
  meshes (`correct_meshless_fbx_root` in
  `editor/core/asset_compiler/importers/mesh_importer.cpp`). Such a file, or one imported with
  Import Meshes off, compiles to its clips, its materials and a model without geometry.

## Frames

- Keys live in the root node's parent frame, which need not be upright: FBX armatures stand
  Z-up or -Z-up below a rotated, x100-scaled node. The pose/entity split is made on the keys
  turned upright by the parent's rest rotation (`root_motion_frame::up`), never on the keys'
  own Y.
- Only the heading (twist about up) is extracted as rotation; lean and roll stay in the pose.
- Extracted axes hold a baseline in the pose and are written whole: bone weights are uniform,
  since per-axis weights would apply in a frame that may not be upright.
- Floor-level root bones lock to their rest pose, so combo clips start where the previous clip
  ended; hips lock to their first frame, which carries the stance.
- `keep_in_place` detrends the root's travel: the linear term, then every residual component
  along the horizontal travel direction (captured mocap changes speed within a loop, so a
  linear detrend alone leaves a lunge). Lateral sway and vertical bob survive
  (`test_keep_in_place_preserves_sway`).
- The clip-start baseline is pinned into the pose (position and heading) only while the
  extracted delta is applied: `extract_root_motion` is passed from
  `animation_component::get_apply_root_motion()` down to sampling. With root motion off the
  authored motion stays in the pose (`test_root_motion_unapplied_extraction_keeps_authored_pose`,
  `test_root_motion_extract_pose_is_blend_safe`).

## Moving the entity

- The delta lives in the root node's parent frame. `animation_system` maps it through the
  local transforms up to the model entity, moves the entity once per frame along its own axes
  (with its scale), then post-multiplies the turn.
- When rotation is extracted and the rotation root is not an ancestor of the position root, the
  step is rebased by the turn taken so far (`rebase_root_translation`); headings are absolute
  twists, so per-step turns telescope to the clip's total.
- A tracking that starts (state start, switch, crossfade target, seek, blend point entering, a
  culled character shown again) measures from where the time step began
  (`animation_state::step_begin_*`): the first step moves the entity, and travel does not
  depend on the frame rate. The jump of a seek itself moves nothing.
- A crossfade blends the deltas by the end-of-step weight; a frozen crossfade source is a
  static pose and moves the entity no further. Characters whose poses are skipped (culling)
  drop the motion they missed.
- A root the target lacks (a UE clip root on a Mixamo character) reports through its first
  mapped descendant; root motion converts by the heading of the frame fit only and scales by
  the hips' height ratio.
- An entity with a character controller in play mode moves through
  `character_controller_component::move`, so root motion collides. The controller moves on
  physics fixed steps (50 Hz by default) and the entity shows between them, trailing the move
  by a frame or two (`unravel-physics`).

## Measuring

- Probe travel with paused play and `play_skip_frame`, comparing the entity position before
  and after a move with root motion on against the pelvis with it off.
- Across a whole combo, the pelvis offset from the entity stays bounded: a bone that drifts
  from the entity and slides back on the next clip is a baseline bug.
- With a controller, compare against the controller's real position, not the interpolated
  transform.
