---
name: animation
title: Animation workflow - clips, retargeting, root motion, controllers, IK
description: From an imported character to one that plays and reacts - clip assets and their import settings (root motion, events, curves), clips from other rigs (Copy vs Rest Aligned, humanoid roles, the retarget check animation_get_retarget), root motion, animation controllers (parameters, layers, masks, blend spaces, transitions) and how scripts drive them, IK and spring bones, and how to verify each on screen.
order: 45
---
# Animation workflow

The pieces, in the order a character needs them:

1. a skinned model and its clips (import);
2. an entity with Model + Animation components (scene);
3. retargeting settings when the clips come from another rig;
4. root motion settings per clip;
5. a controller for anything that reacts to gameplay;
6. IK / spring bones on top of the pose;
7. proof on screen.

Component property names below are the reflected snake_case names that
scene_set_component_properties_batch takes; component names are the pretty
names ("Animation", "Two Bone IK").

## 1. Import

- assets_import_files brings in a model (FBX, glTF) and animation files.
  Every animation becomes its own clip "<file>_<animation>.anim" next to the
  model. Files with clips only (no mesh) import too.
- assets_find_batch {"type":"animation_clip","prefix":"<folder key>"} lists
  the clips.
- A clip's import settings (assets_get_import_settings /
  assets_set_import_settings on the .anim key) hold root_motion, events and
  curves. Setting them reimports the clip; pass wait_ms so the call returns
  once it is ready.

```json
{"name":"assets_set_import_settings","arguments":{"wait_ms":60000,"items":[
 {"key":"app:/data/Characters/Hero/Hero_Walk.anim","settings":{
  "root_motion":{"keep_in_place":true},
  "events":[{"name":"Footstep","position":0.44,"string_parameter":"right"},
            {"name":"Footstep","position":0.94,"string_parameter":"left"}],
  "curves":[{"name":"HandIK","curve":[{"progress":0,"value":0},
            {"progress":0.4,"value":1},{"progress":1,"value":0}]}]}}]}}
```

- events: name, position (0..1 of the clip), string_parameter /
  float_parameter / int_parameter, min_weight (the smallest share of the
  final pose at which it fires: 0.5 reports a crossfade from one side only). Scripts get them in OnAnimationEvent(AnimationEvent), and a public
  method named exactly like the event is called too (taking nothing, the
  AnimationEvent, or the event's string, float or int).
- curves: named float tracks blended with the pose. Read with
  animation.GetCurve(name); a float controller parameter with the same name
  takes the curve's value; IK components take one as weight_curve.

## 2. The character entity

scene_create_batch {"kind":"mesh","asset":"<model key>"} creates the model
with its bones as child entities; a skinned model gets an Animation component
then (scene_add_components_batch adds one otherwise). scene_get_hierarchy_batch
{"parent_id":<character>,"max_depth":30} lists the bone entities - IK targets,
foot bones and attached props need their ids.

Animation properties:

| Property | Use |
|---|---|
| animation | One clip key; with auto_play true it loops. |
| controller | An animation controller asset (.actrl). While set it drives the animation and the clip is ignored. |
| auto_play | Start playing on its own (play mode). |
| speed | Playback rate. |
| apply_root_motion | Move the entity by the clip's root (section 4). |
| retargeting_mode | name_based (default) or index_based (same file only). |
| pose_retargeting | copy (default) or rest_aligned (section 3). |
| retarget_pose | bind_pose (default) or t_pose (section 3). |
| retarget_pose_offsets | [{role, rotation}] turns of this character's bones (section 3). |
| culling_mode | always_animate or renderer_based (skip off-screen). |

## 3. Clips from another rig (retargeting)

Copy writes the clip's local transforms as they are: right only for clips
exported with this model. A clip from any other rig, export, unit or
proportion needs pose_retargeting "rest_aligned" (with retargeting_mode
"name_based"): bones match by name, then by name without the FBX namespace
("mixamorig:Hips" = "mixamorig1:Hips"), then by humanoid role (22 body bones
found from each skeleton's shape: Mixamo, Rigify, VRM, Biped, Character
Creator, Daz, KayKit, mannequin rigs). Motion scales by hip height.

After assigning clips, run the retarget check - the same report as the
Animation inspector's Retarget Check section:

```json
{"name":"animation_get_retarget","arguments":{"entity_id":"<character>",
 "clip":"app:/data/Packs/Locomotion/Locomotion_Walk.anim"}}
```

Without clip it checks the clip the base layer plays, else the one the
controller starts in, else the animation property. channels:true lists every channel with its bone and match kind;
otherwise only the channels that reached no bone are listed. Read:

- rest_aligned: false while pose_retargeting is copy, or when the clip
  recorded no rest pose (reimport it).
- retarget_baked: the rest-aligned retarget is baked into constants per bone,
  so a frame costs about what copy costs. False when a channel between two
  mapped bones moves, a scale animates or a frame is non-uniformly scaled; the
  clip still plays right, through the full retarget every frame.
- matches: channels per kind (index, name, name without namespace, role,
  none). Role matches mean the names differ; that is fine.
- unreached_bones: bones that keep the bind pose. Fingers, tails and
  accessories match by name only.
- height_ratio: this skeleton's hip height over the clip's. Far from the
  real ratio means the hips were found wrongly on one side.
- root_motion.position.bone: the bone the clip's root motion moves; null with
  a root channel means its root motion is lost.
- rest_differences: degrees between the two retarget poses per limb. Above 20
  the skeletons rest in different poses (T-pose against A-pose) and every pose
  keeps that offset - set retarget_pose "t_pose" to straighten both first.
- warnings: each problem in words, with the fix. An empty list is the goal.

animation_get_humanoid {"entity_id"} or {"clip"} lists the roles each side
found, when the check points at a role, and the roles set by hand.

Three fixes, smallest first:

- A role on the wrong bone (the clips animate a hand below the wrist that was
  found): set it in the model FILE's import settings; it holds for the model
  and every clip of the file, and the file is imported again.
  assets_set_import_settings {"items":[{"key":"<model key>","settings":
  {"rig":{"humanoid_overrides":[{"role":"left_hand","bone":"hand.l"}]}}}],
  "wait_ms":120000}. An empty bone takes the role away.
- Skeletons resting in other poses: Animation retarget_pose "t_pose"
  straightens both (arms level, legs down) before motion is carried.
- Limbs through the body on a character of other proportions: Animation
  retarget_pose_offsets [{"role":"left_upper_arm","rotation":[0,0,25]},
  {"role":"right_upper_arm","rotation":[0,0,25]}] - degrees about the
  character's side, up and forward axes; right-side roles are mirrored, so
  equal values move both sides alike. Captures judge the amount.

## 4. Root motion

apply_root_motion true moves the entity by the clip's root node (the
skeleton's root bone, or the hips without one). With a Character Controller
the move goes through it in play mode, so it collides. Per clip, root_motion
import settings choose what stays in the pose:

| Setting | Default | Effect |
|---|---|---|
| keep_position_y | true | Vertical motion stays in the pose (a jump lifts the hips). |
| keep_position_xz | false | Horizontal travel stays in the pose. |
| keep_rotation | false | Turns stay in the pose. |
| keep_in_place | false | Travel is dropped: plays on the spot, moves nothing. |

Loops that a script moves (walk, run, jump) want keep_in_place; attacks,
rolls, climbs want their travel extracted. Seated or kneeling poses whose
pelvis sits off the root want keep_position_xz true. Only the turn about the
up axis is ever extracted. Leave apply_root_motion false for showcase loops.

## 5. Controllers

Create one, then write it with assets_set_properties_batch. A list takes
the array's length, and each element merges the fields it names into the
element already at that index: when states or transitions move, name every
field of each element (or read the controller back with
assets_get_properties_batch first).

```json
{"name":"assets_create_batch","arguments":{"items":[
 {"type":"animation_controller","folder":"Animation","name":"Hero"}],"wait_ms":10000}}
{"name":"assets_set_properties_batch","arguments":{"items":[
 {"asset":"app:/data/Animation/Hero.actrl","properties":{
  "parameters":[{"name":"Speed","type":"float_value","default_float":0},
                {"name":"Attack","type":"trigger"}],
  "layers":[{"name":"Base","weight":1,"blend_mode":"override_pose",
   "default_state":"Locomotion","mask":[],
   "states":[
    {"name":"Locomotion","motion":"blend_1d","parameter_x":"Speed","loop":true,
     "points":[{"clip":"<idle>","position":[0,0]},{"clip":"<walk>","position":[1.6,0]},
               {"clip":"<run>","position":[3.6,0]}],"editor_position":[0,0]},
    {"name":"Attack","motion":"clip","clip":"<attack>","loop":false,"tag":"Busy",
     "editor_position":[280,0]}],
   "transitions":[
    {"source":"Locomotion","destination":"Attack","duration":0.1,
     "conditions":[{"parameter":"Attack","mode":"is_true","threshold":0}]},
    {"source":"Attack","destination":"Locomotion","duration":0.25,
     "has_exit_time":true,"exit_time":0.85,"conditions":[]}]}],
  "preview_model":"<model key>"}}]}}
{"name":"animation_controller_validate","arguments":{"asset":"app:/data/Animation/Hero.actrl"}}
```

- parameters: type float_value | int_value | bool_value | trigger, with
  default_float / default_int / default_bool.
- states: motion clip | blend_1d | blend_2d_cartesian | blend_2d_directional;
  blend states take parameter_x (and parameter_y) and points (clip +
  position). speed, speed_parameter, loop, cycle_offset, tag (scripts test
  IsInTag("Busy")).
- transitions: source "" is Any State. conditions: mode greater | less |
  equals | not_equals (with threshold) | is_true | is_false. has_exit_time +
  exit_time (0..1), duration (seconds), offset, phase_sync, interruptible,
  can_transition_to_self (Any State transitions that may restart a state).
  Without conditions or exit time a transition never fires.
- layers: blend_mode override_pose | additive, weight, mask entries {bone,
  weight, include_children} (an upper-body layer masks "spine_01" with
  children), default_state. Layers stack bottom to top.
- Validate after every write: problems lists what would not work at runtime.
- Then set the Animation component's controller to the key.

Scripts drive it with animation.SetFloat / SetInt / SetBool / SetTrigger /
ResetTrigger, read it with GetCurrentStateName / IsInState / IsInTag /
IsInTransition / GetStateTime, and jump with CrossFade / PlayState. Without
a controller: Blend(clip, seconds, loop, phaseSync), BlendLayer, StopLayer,
SetLayerWeight. From MCP, in play mode: animation_set_parameters
{"entity_id","values":{"Speed":3},"triggers":["Attack"]} or {"state":...}
to crossfade straight to a state; animation_get_state reports each layer's
state, normalized time, transition and the blended curves - the proof a
trigger took.

## 6. IK and spring bones

Rig modifiers run after the pose, before skinning. Add each to the bone entity
it solves (Foot IK goes on the character):

| Component | On | Key properties |
|---|---|---|
| Two Bone IK | hand / foot | target, pole (elbow / knee direction), weight, rotation_weight, soften |
| Chain IK | chain tip | target, pole, chain_length, solver fabrik / ccd, iterations, tolerance |
| Look At IK | head | target, target_offset, chain_length (head, neck, chest), root_share, max_bone_angle, max_turn_angle |
| Foot IK | character | left_foot, right_foot, pelvis, ground_layers, max_pelvis_drop, align_to_ground |
| Spring Bone | chain root | stiffness, damping, gravity, radius, chain_length, colliders (spheres: entity, radius, offset) |

- target / pole / bones are entity ids (a bone, or an empty you move).
- smoothing (seconds) eases the goal and the weight: set it when a script
  swaps or clears the target, or the limb snaps.
- weight 0 fades a modifier out and it then costs nothing. weight_curve names
  a clip curve the weight is multiplied by (reach only while the pick-up clip
  reaches).
- Modifiers are non-destructive: scene_get_entities_batch reads the ANIMATED
  bone pose, not the solved one. Verify IK with captures only.

## 7. Verify on screen

Reads alone do not prove animation. The reliable recipe:

1. play_set_active {"active":true}, then play_set_paused {"paused":true}.
2. Drive it: animation_set_parameters (values, triggers, state).
3. play_skip_frame {"count":N} steps a fixed 1/60 s per frame, so travel and
   timing repeat exactly; capture with viewport_capture_scene (or
   viewport_capture_game) between steps for a frame strip.
4. animation_get_state for the state and weights; scene_get_bounds_batch or
   transforms for the entity's travel (root motion).
5. Stop play before editing: mutating tools refuse play mode.

Check against the world: feet on the floor standing, crouching and landing;
hips at the character's height; a turn ends facing where it should; hands
reach their targets.

## Troubleshooting

| Symptom | Check / fix |
|---|---|
| Character lies down, twists, limbs stretch | Copy on clips from another rig: pose_retargeting rest_aligned. animation_get_retarget warns. |
| Nothing moves | animation_get_retarget: matched 0? index_based with another file? No clip playing (auto_play, controller default_state)? |
| Arms too high or low in every clip | rest_differences above 20: retarget_pose t_pose. |
| Arms or legs through the body | retarget_pose_offsets on the upper arms / legs. |
| A hand or limb does not move | The role is on another bone: rig.humanoid_overrides on the model file. |
| Fingers / tail stay still | They match by name only; see unmatched channels. |
| Left and right swapped | animation_get_humanoid on both sides; side-less names. |
| Feet slide, steps too long | height_ratio; root motion on with keep_in_place clips, or off with travelling clips. |
| Walks away and snaps back at the loop | apply_root_motion false on a travelling clip. |
| Drifts up or sinks | keep_position_y false on a clip that should keep it. |
| Trigger does nothing | animation_get_state in play mode; transition conditions; validate the controller. |
| Event never arrives | min_weight above the clip's blend weight; method name must match exactly. |
| IK looks wrong in reads | Reads show the animated pose; capture instead. |
