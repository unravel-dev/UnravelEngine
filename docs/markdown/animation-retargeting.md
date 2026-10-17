# Animation retargeting

Retargeting plays a clip made for one skeleton on another: a clip from an animation pack on your
own character, a clip exported from another tool, a clip of a taller or shorter rig. This page
follows a clip from import to the pose on screen, then lists what to check when something looks
wrong. It is written for people working in the editor and for agents driving the editor over MCP;
the last section maps the code for engine work.

## Quick start

1. Import the character model and the animation files (FBX or glTF). Animation-only files (a clip
   saved without its mesh) import too.
2. Select the character entity. Its **Animation** component plays the clips.
3. Pick **Pose Retargeting**:
   - **Copy** (default) when the clips were exported with this model, or for this exact skeleton.
   - **Rest Aligned** for anything else: another rig, other bone names, other units, other axes,
     other proportions. Leave **Retargeting Mode** on **Name Based**.
4. Open **Retarget Check** in the Animation inspector. It shows how the clip playing (or one you
   pick) binds to this skeleton: channels matched and how, bones left unreached, the height ratio,
   the bone root motion moves, how far the two rest poses differ, and a warning for each problem
   it finds. **Humanoid Mapping** above it lists the bone found for each role; its pencil button
   sets a role by hand when the wrong bone was found.
5. When the clip's skeleton and the character rest in different poses (an A-pose clip on a T-pose
   character), set **Retarget Pose** to **T-Pose**: both skeletons are straightened before motion
   is carried over.
6. When a character of other proportions puts its limbs through its body (short arms beside a big
   head and a wide body), add **Retarget Pose Offsets**: raising both upper arms 20 to 30 degrees
   keeps them clear in every clip.
7. For clips that travel (walks, rolls, attacks that lunge), turn on **Apply Root Motion** and
   check each clip's root motion import settings.

## Concepts

- **Rest pose.** The pose a skeleton is built in, before any animation: usually a T-pose or an
  A-pose.
- **Retarget pose.** The pose Rest Aligned retargeting measures motion from on the clip's skeleton
  and applies it to on the character. By default each skeleton's rest pose, so both must rest in
  the same physical pose; with Retarget Pose T-Pose, both rests straightened to a T-pose; on the
  character, turned further by its Retarget Pose Offsets.
- **Channel.** One animated node of a clip: its name and its position, rotation and scale keys, in
  the node's local space (relative to its parent).
- **Humanoid roles.** Twenty-two bones every humanoid has in some form: hips, spine, chest, upper
  chest, neck, head, and per side shoulder, upper arm, lower arm, hand, upper leg, lower leg, foot
  and toes. The engine finds them from the skeleton's shape (two legs hang from the hips, the spine
  rises to where the arms branch, and so on), not from a table of names. Names only tell left from
  right; side-less names take their side from the way the toes point. Twist joints (`lShldrTwist`)
  and split segments (`upper_arm.L.001`) are passed over.

## 1. Import: what a clip records

Each animation in a model file becomes its own `.anim` asset named `<file>_<animation>`. A clip
holds:

- **Channels**, one per animated node.
- **The source rest pose.** For each channel: the node's rest transform in the file, the channel
  that animates its nearest animated ancestor, and the fixed transform of any unanimated nodes in
  between. The clip carries a description of the skeleton it was made on, so the character it was
  authored for never has to be imported.
- **Root-motion nodes.** The importer picks the shallowest animated node on the skeleton's trunk
  (from the scene root down to where the skeleton branches): a root bone when the rig has one, else
  the hips. Its travel and turn can move the entity instead of the bone.
- **Import settings** (in the clip's `.meta`, edited in the clip inspector): root motion options,
  plus the events and curves authored on the clip's timeline.

| Import setting | Default | Effect |
|---|---|---|
| Keep Position Y | on | Vertical motion stays in the pose (a jump lifts the hips, not the entity). |
| Keep Position XZ | off | Horizontal travel stays in the pose instead of moving the entity. |
| Keep Rotation | off | Turns stay in the pose instead of turning the entity. |
| Keep In Place | off | Travel is removed: the clip plays on the spot and moves nothing. |

Files without meshes take every animated node and its ancestors as their skeleton, and an FBX root
gets the same axis correction a file with meshes gets, so a clip-only export describes the same
skeleton as the skinned one.

## 2. Setup: the Animation component

| Setting | Values | Meaning |
|---|---|---|
| Retargeting Mode | **Name Based** (default), Index Based | How channels find bones. Name Based matches bone names; Index Based uses the node order in the file and only suits clips from the same file. |
| Pose Retargeting | **Copy** (default), Rest Aligned | How a pose reaches the bones. Copy writes the clip's local transforms as they are; Rest Aligned maps the motion from the clip's skeleton onto this one. Rest Aligned needs Name Based. |
| Retarget Pose | **Bind Pose** (default), T-Pose | Rest Aligned only. Bind Pose measures motion from each skeleton's rest pose. T-Pose first straightens both skeletons by role: upper and lower arms level along the character's sides, upper and lower legs straight down. Copies of one skeleton play the same either way. |
| Retarget Pose Offsets | role + rotation (degrees) | Rest Aligned only. Turns of this character's bones in its retarget pose, in its own space: about its side axis (towards its left), up and forward. Right-side roles are mirrored, so (0, 0, 20) on both upper arms raises both. Every clip keeps them. |
| Apply Root Motion | off / on | Moves the entity by the clip's root motion. With a Character Controller in play mode the move goes through it, so it collides. |

Copy is exact and fast when the skeletons really are the same. Across different skeletons it is
wrong in visible ways: positions copy the source's bone lengths (limbs stretch or shrink), and
rotations assume the same bone axes (a rig whose bones point along +Y twists on one whose bones
point along +X).

The target side of Rest Aligned is built from the model: its bone names, parents and bind pose,
and the humanoid roles found in that bind pose.

**Roles set by hand.** Where the finder picks the wrong bone (a rig that animates a hand bone below
the wrist it found), set the role in the model file's import settings: **Rig > Humanoid Overrides**,
a role and a bone name each, an empty name taking the role away. The pencil button in Humanoid
Mapping writes them for you. They are compiled into the model and into every clip of the file, so
they hold where the skeleton plays clips and where its clips play on others. The model is imported
again when they change.

## 3. Binding: done once per clip and skeleton

The first time a clip plays on a character, the player binds the clip to the skeleton and keeps
the result until the clip or the model's armature changes.

1. **Channels find bones**, first match wins:
   1. the same name;
   2. the same name without its FBX namespace (`mixamorig:Hips` and `mixamorig1:Hips`);
   3. Rest Aligned only: the same humanoid role. The clip's skeleton gets its roles from its
      recorded rest pose, so `upperarm_l`, `mixamorig:LeftArm` and `DEF-upper_arm.L` all land on
      the target's left upper arm. Roles set by hand win on both sides.

   Bones no channel reaches keep their bind pose.
2. **Retarget poses.** Each skeleton's rest pose, straightened to a T-pose when Retarget Pose asks
   for it; the character's then turned by its offsets.
3. **Frame fit.** The two skeletons may stand in different frames: a Z-up export against a Y-up
   one, or a model the importer turned to face forward. The rotation that best lays the source
   joint positions onto the target's (in the retarget poses, without the offsets) is that frame
   difference. Joint positions do not depend on how a rig orients its bone axes, so the fit
   captures the frame only. Two humanoids both stand upright, so between them only the fit's
   heading (its turn about the up axis) is kept: where their proportions differ (a leaning spine,
   a big head) the fit tilts, and every carried motion would lean with it.
4. **Height ratio.** The target's hip height over the source's. A shorter character takes shorter
   steps and jumps lower.
5. **Root-motion target.** The root channel is matched like any other. When the target lacks that
   bone (a clip with a floor-level root bone on a rig that starts at the hips), the motion reports
   through the root's first matched descendant, normally the hips.

## 4. Each frame: posing the target

**Copy** writes each channel's sampled local transform onto its bone.

**Rest Aligned** samples the whole source pose, unmatched channels included (a root the target
lacks still carries its children), builds it up to world space, and then poses each matched bone:

- **Rotation.** How far the source bone turned from its retarget pose, in world space, is carried
  into the target's frame by the frame fit and applied on top of the target bone's retarget pose.
  Each rig keeps its own bone axes; only the change is carried.
- **Position.** The target keeps its bone lengths. Only the source bone's move away from its rest
  (the hips bobbing, say) is carried, turned into the target's frame and scaled by the height
  ratio.
- **Scale.** A change of scale is carried along the same physical direction: a stretch along a
  source bone that points along +Y stretches a target bone that points along +X along its length.

Layers, crossfades, blend spaces and animation controllers all sample through the same binding.
A blend space whose clips share one source skeleton is blended in that skeleton and retargeted
once, which gives the pose retargeting each clip separately would, for less work.

## 5. Root motion

The root's move over the frame is measured in its parent's frame on the source skeleton. It is
carried into the target root's parent frame with the heading of the frame fit only (so the
character never tilts), scaled by the height ratio, then mapped through the target's bones to the
entity, which moves once per frame.

The first frame of a clip moves the entity too, so travel does not depend on the frame rate.
During a crossfade the moves of both clips blend by the crossfade weight.

## Troubleshooting

Start with the Animation inspector's **Retarget Check** (MCP: `animation_get_retarget`): its
warnings name most of the causes below.

| Symptom | Likely cause | Fix |
|---|---|---|
| Character lies down, twists or stretches | Copy with clips from another export or rig | Set Pose Retargeting to Rest Aligned. |
| Nothing moves | Index Based with clips from another file, or no names or roles match | Use Name Based; check Humanoid Mapping on both sides. |
| Arms held too high or too low through every clip | The rest poses differ (T-pose against A-pose); Retarget Check's Rest poses list shows the angle per limb | Set Retarget Pose to T-Pose. |
| Limbs pass through the body or the head | The character's proportions differ (short arms, wide body, big head): human angles land inside it | Add Retarget Pose Offsets, for example raise both upper arms 20 to 30 degrees. |
| A limb or a hand does not move, or moves the wrong bone | The finder gave the role to another bone (a hand role on the wrist while the clips animate the hand) | Set the role by hand: the pencil button in Humanoid Mapping, or the model file's Rig > Humanoid Overrides. |
| Fingers, tail or accessory bones stay still | They match neither by name nor by role (roles cover 22 body bones) | Give them the same names on both rigs; FBX namespaces are already ignored. |
| Left and right swapped | Side-less bone names read the wrong way | Check Humanoid Mapping; name the sides (`_l`, `.R`, `Left`). |
| Steps too long or too short, feet slide | Proportions differ; the hips found on one side are wrong | Check that both sides found their hips; add Foot IK for contact. |
| Character walks away from its entity and snaps back at the loop | Root motion is not applied | Turn on Apply Root Motion; check the clip's Keep Position XZ. |
| Character drifts upward or sinks | Vertical root motion moves the entity | Keep Position Y on (the default). |

## For agents (MCP)

Set up a character for clips from other rigs:

```json
{"name": "scene_set_component_properties_batch", "arguments": {"items": [
  {"entity_id": "<character>", "component": "Animation",
   "properties": {"retargeting_mode": "name_based", "pose_retargeting": "rest_aligned",
                  "apply_root_motion": true}}]}}
```

Check how a clip binds to the character. Without `clip` it checks the clip the base layer plays, else the one its controller starts in,
else the Animation clip;
`"channels": true` lists every channel with its bone and kind of match:

```json
{"name": "animation_get_retarget", "arguments": {"entity_id": "<character>",
  "clip": "app:/data/Characters/Pack/Pack_Walk.anim"}}
```

It returns the mode in effect, match counts per kind, the unmatched channels, the unreached bones,
`height_ratio`, the frame turn, the root-motion channel and bone, rest differences per limb in
degrees, and `warnings`; an empty `warnings` list is the goal.

Straighten both skeletons, raise a stocky character's arms, and set a role by hand (the model file
is imported again):

```json
{"name": "scene_set_component_properties_batch", "arguments": {"items": [
  {"entity_id": "<character>", "component": "Animation",
   "properties": {"retarget_pose": "t_pose", "retarget_pose_offsets": [
     {"role": "left_upper_arm", "rotation": [0, 0, 25]},
     {"role": "right_upper_arm", "rotation": [0, 0, 25]}]}}]}}
{"name": "assets_set_import_settings", "arguments": {"wait_ms": 120000, "items": [
  {"key": "app:/data/Characters/Pack/Pack.glb", "settings": {"rig": {"humanoid_overrides": [
    {"role": "left_hand", "bone": "hand.l"}, {"role": "right_hand", "bone": "hand.r"}]}}}]}}
```

Check the roles on both sides; every role the clip uses should exist on the character
(`overrides` lists the roles set by hand):

```json
{"name": "animation_get_humanoid", "arguments": {"entity_id": "<character>"}}
{"name": "animation_get_humanoid", "arguments": {"clip": "app:/data/Characters/Pack/Pack_Walk.anim"}}
```

Read and change a clip's root motion settings (the clip is imported again with them):

```json
{"name": "assets_get_import_settings", "arguments": {"keys": ["app:/data/Characters/Pack/Pack_Walk.anim"]}}
{"name": "assets_set_import_settings", "arguments": {"items": [
  {"key": "app:/data/Characters/Pack/Pack_Walk.anim", "settings": {"root_motion": {"keep_position_xz": false}}}]}}
```

Verify on screen, not from reads alone: enter play mode, pause, step with `play_skip_frame` and
capture with `viewport_capture_scene`; `animation_get_state` reports the clips and weights playing.
Compare a retargeted crouch, sit or landing against the floor, and the hips' height standing still
against the character's proportions.

## Code map

| Part | Where |
|---|---|
| Clip import, root-motion node, source rest pose | `engine/engine/assets/impl/importers/mesh/mesh_import_animation.cpp` |
| Mesh-less FBX root correction | `engine/engine/assets/impl/importers/mesh_importer.cpp` |
| Humanoid roles, roles set by hand, T-pose straightening, pose offsets | `engine/engine/animation/animation_humanoid.cpp` |
| Binding, frame fit, height ratio, sampling, retargeting, root motion | `engine/engine/animation/animation_player.cpp` (`find_binding`, `retarget_source_pose`, `convert_root_delta`) |
| Binding report | `animation_player::describe_binding` (engine), `editor/editor/editing/animation_retarget_check.cpp` (warnings; used by the inspector and the MCP tool) |
| Target skeleton, name lookup, moving the entity | `engine/engine/animation/ecs/systems/animation_system.cpp` |
| Retarget pose settings | `animation_component` (`retarget_pose`, `retarget_pose_offsets`), `animation_target_skeleton::compute_reference_pose` |
| Roles set by hand | model import settings `rig.humanoid_overrides`, compiled into the mesh and the clips (`asset_compiler.cpp`) |
| Tests | `tests/tests/suites/animation.cpp` (`test_rest_aligned_retargeting`, `test_humanoid_roles_of_common_rigs`, `test_binding_report_explains_a_retarget`, `test_retarget_pose_in_playback`, `test_clip_overrides_choose_the_source_bone` and the retargeting tests beside them) |
