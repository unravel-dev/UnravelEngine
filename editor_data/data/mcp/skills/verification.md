---
name: verification
title: Verifying scenes - captures, play mode, logs, profiling
description: Scene viewport captures and framing helpers, debug views, the play-mode capture choreography for physics/animation/particles, frame stepping, log tailing and per-pass GPU profiling.
order: 70
---
# Verifying scenes: captures, play mode, logs, profiling

## Scene viewport captures (edit mode)

- viewport_set_camera poses the Scene panel camera: position plus
  rotation_euler (yaw 0 faces +Z, yaw 180 faces -Z, positive pitch looks down)
  or target, a world point to look at. It stops camera motion first, so the
  pose holds. viewport_get_camera reads the current pose.
- viewport_capture_scene captures a frame rendered after the call (scale 0.5
  is enough to judge composition); settle_frames lets temporal effects settle
  after a big change. A hidden Scene tab renders for the capture.
- Framing without math: viewport_focus {entity_ids} (or center + radius, or
  min + max) frames like the editor's F key - duration 0 is instant;
  viewport_orbit_camera orbits a pivot by yaw / pitch; viewport_reset_camera
  restores the default pose.
- viewport_set_debug_view switches visualization ("full", "base_color",
  "normals", "depth", "velocity", ...). Always restore "full" afterwards.
  viewport_list_debug_views lists every mode grouped, with what it shows and
  what its colors mean - call it instead of guessing a name.
  "particle_overdraw" shows how many particle layers each pixel shades (blue 1,
  green 4, yellow 8, red 16, white 32+): the view to judge an effect's fill
  cost before reaching for half_resolution, octagon or max_screen_size.

## Play mode testing (physics, animation, particles, scripts)

Mutating tools refuse play mode, so choreograph (the Scene panel still renders
during play - viewport_set_camera {target} and viewport_capture_scene film a
paused moment from any angle):

1. Move the game's Main Camera entity (scene_set_transforms_batch, WORLD pose)
   to frame the target - BEFORE entering play.
2. play_set_active {"active":true} (allow_splash:false skips the splash
   screen); editor_wait_for {"play_phase":"running"} returns once the
   simulation runs - capture after that.
3. viewport_capture_game (it renders the game view itself, even with the Game
   tab hidden). width + height (e.g. 1280 x 720) capture at that resolution
   and restore the previous size afterwards; viewport_set_render_size
   {"panel":"game",...} keeps the game at one size (0 x 0 to undo).
4. play_set_active {"active":false} - edit state is restored, including
   everything physics moved.
5. editor_get_status answers "am I in play?" when unsure; play_set_paused +
   play_skip_frame give deterministic frame stepping: each step runs scripts,
   physics and animation once over 1/60 s, so a capture after every step
   films script-driven motion frame by frame. play_skip_frame {"count":30}
   steps 30 frames in one call and returns once they ran.
6. scripts_invoke calls a public method of a running script with arguments
   (a spell's TryCast at [x,y,z], a door's Open()) - repeatable where clicks
   and key presses depend on timing and screen position. Paused, then
   play_skip_frame, it freezes the result at an exact frame.
7. particles_control_batch plays, stops or emits an emitter (edit or play
   mode) and reports num_particles and simulated: an emitter showing nothing
   with simulated false is frozen by renderer-based culling (no camera saw it).
8. Animation: animation_set_parameters {"entity_id", "triggers":["Attack"]} fires
   a controller move without input timing; paused, a capture after every
   play_skip_frame {"count":4} makes a frame strip of the move. Root motion
   is proven by the entity's position before and after; a bone that drifts
   away from its entity and slides back afterwards means motion stayed in the
   pose. IK and other rig modifiers only show in captures (see components).

Two captures a few seconds apart prove motion (dominoes mid-fall, animation
pose changes) better than one.

Wait for outcomes instead of sleeping or polling: editor_wait_for blocks until
one condition holds and returns met, elapsed_ms and the last value seen (a
timeout is met false, not an error):
- property: {"entity_id", "component":"Transform", "name":"position.1",
  "op":"<", "value":0} - a ball fell below the floor. Any component works the
  same, and script fields with component "Script" + type_name ("Score", name
  "value", op ">=", value 10); op "changed" waits for any change.
- entity: {"name_contains":"Enemy", "exists":false} - every enemy destroyed.
- log: {"contains":"Game Over"} - logged after the call (or after an after_id
  from logs_get_recent).

## Driving the game: synthesized input

Captures prove rendering. Only input proves gameplay - a player controller, a
jump, a trigger, a menu button.

1. input_get_state first. input_allowed false means the Game panel is not
   focused and NOTHING reaches the action map; the input tools focus it for you
   unless you pass focus_game_panel false.
2. input_send_keys {"keys":["w"],"frames":60} holds the keys for exactly that
   many frames and waits for them to elapse, so the effect is finished when the
   call returns; the default (one frame) is a tap, which is what
   Input.IsPressed sees. state down / up holds across several calls. Keys are
   always released at the end of a press, and when play stops.
3. Key names are the platform's, matched ignoring case, spaces and dashes:
   "w", "space", "left shift" or "lshift", "f1"; "ctrl+s" (or {"key":"s",
   "ctrl":true}) holds the modifier around the key as a real press does. A
   rejected name lists near misses.
4. Scripts only run in play mode - enter play first or nothing moves.
5. Pointer: input_send_mouse {x, y} (play mode, game pixels by default) moves
   a virtual pointer that the game, its UI and the editor all follow - the
   user's real cursor is not moved - and button / wheel click or scroll there.
   A point outside the Game panel is refused (the press would land on the
   editor). While it is set the user's mouse does not reach the editor, so clear
   it (clear:true) as soon as the test is done; it also clears itself when play
   ends. A virtual pointer also suspends a script's cursor lock; input_get_state
   reports the lock under cursor (lock_state, lock_active, hidden).
6. editor_ui_click {x, y} clicks the EDITOR's own interface (client pixels of
   the editor window, edit or play mode) - a panel control no tool covers, such
   as a node of the Animation Controller window. button, double, a drag with
   to_x / to_y and wheel are supported; the pointer is released afterwards.
   Prefer a dedicated tool whenever one exists.

Prove it with numbers, not only pixels: scene_find_entities_batch then
scene_get_bounds_batch (both allowed in play mode) before and after gives a
measurable delta. A displacement that matches speed x frames x frame time is
proof; a screenshot that "looks moved" is not.

Actions, not keys: scripts query action names (Input.GetAnalogValue("Horizontal")),
so a key only does something if an action is bound to it. input_list_actions
shows the bindings, input_map_action adds one. An action no binding points at
silently reads zero.

## Logs and profiling

- logs_get_recent (min_level "warn") after imports, scene loads and play
  sessions; contains filters by text. Pass the next_after_id of one call as
  after_id to the next to read only what was logged since (has_more says
  another page waits).
- stats_get: the frame as the statistics overlay shows it - fps, CPU submit
  and GPU ms, triangles and draw calls without the editor UI, GPU memory, and
  per view (scene_view, game) models, lights, shadow casters and particles
  drawn. The first check for "is this scene too heavy"; with vsync on the
  times include waiting for the display.
- profiler_get_passes returns per-render-pass CPU/GPU milliseconds. Pass
  enable:true once to switch the GPU profiler on; one frame is noisy, so
  sample several times and average. Use a prefix filter to watch one pass.

## Do

- Iterate visually: build a section, capture, fix scale/lighting/placement,
  recapture. Judging placement blind from numbers produces oversized props,
  floating meshes and lights aimed at the floor.
- Use scene_get_bounds_batch for cheap non-visual sanity checks between
  captures.

## Do not

- Do not screenshot at full scale by default - scale 0.5 is enough to judge
  composition and saves tokens.
- Do not leave a debug view or a paused play session active when you finish.
- Do not treat a quiet log as success for physics/animation - only a capture
  (or two) proves motion.
- Do not conclude a control scheme works without an unbound control case: hold
  the key with the action unmapped and confirm nothing moves, then bind it and
  confirm it does. Without that, a script that reacts to everything looks
  identical to one that works.
- Do not leave a virtual pointer set or keys held when you finish.
