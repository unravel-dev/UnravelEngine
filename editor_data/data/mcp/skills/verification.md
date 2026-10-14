---
name: verification
title: Verifying scenes - captures, play mode, logs, profiling
description: Scene viewport captures and framing helpers, debug views, the play-mode capture choreography for physics/animation/particles, frame stepping, log tailing and per-pass GPU profiling.
order: 70
---
# Verifying scenes: captures, play mode, logs, profiling

## Scene viewport captures (edit mode)

- viewport_set_camera positions the Scene panel camera; yaw 0 faces +Z, yaw
  180 faces -Z; positive pitch looks down. viewport_get_camera reads the
  current pose - useful before relative moves.
- The camera interpolates: set the pose, wait ~0.5s, set it again, then
  viewport_capture_scene (wait_ms 800-1500, scale 0.5). A capture taken too
  early can show the previous pose.
- Framing without math: viewport_focus_entities_batch (entities),
  viewport_focus_bounds (world sphere/box), viewport_look_at (aim the camera
  at a point or entity), viewport_orbit_camera (orbit a pivot by yaw/pitch),
  viewport_reset_camera (default pose). Use duration 0 for instant moves
  before captures.
- viewport_set_debug_view switches visualization ("full", "base_color",
  "normals", "depth", "velocity", "gi_light_voxels", ...). Always restore
  "full" afterwards. viewport_list_debug_views lists every mode grouped, with
  what it shows and what its colors mean - call it instead of guessing a name.
  "particle_overdraw" shows how many particle layers each pixel shades (blue 1,
  green 4, yellow 8, red 16, white 32+): the view to judge an effect's fill
  cost before reaching for half_resolution, octagon or max_screen_size.

## Play mode testing (physics, animation, particles, scripts)

Mutating tools refuse play mode, so choreograph (the Scene panel still renders
during play while its tab shows - panel_focus_scene, then viewport_look_at and
viewport_capture_scene film a paused moment from any angle):

1. Move the game's Main Camera entity (scene_set_transforms_batch, WORLD pose)
   to frame the target - BEFORE entering play.
2. play_set_active {"active":true}; wait 2-4s real time (the first frame is a
   splash screen - never capture immediately).
3. viewport_capture_game (it renders the game view itself, even with the Game
   tab hidden). width + height (e.g. 1280 x 720) capture at that resolution
   and restore the previous size afterwards; viewport_set_render_size
   {"panel":"game",...} keeps the game at one size (0 x 0 to undo).
4. play_set_active {"active":false} - edit state is restored, including
   everything physics moved.
5. play_get_state answers "am I in play?" when unsure; play_set_paused +
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

Two captures a few seconds apart prove motion (dominoes mid-fall, animation
pose changes) better than one.

## Driving the game: synthesized input

Captures prove rendering. Only input proves gameplay - a player controller, a
jump, a trigger, a menu button.

1. input_get_state first. input_allowed false means the Game panel is not
   focused and NOTHING reaches the action map; the input tools focus it for you
   unless you pass focus_game_panel false.
2. input_hold_keys {"keys":["w"],"frames":60} holds the keys for exactly that
   many frames and waits for them to elapse, so the effect is finished when the
   call returns. input_send_keys with state down/up holds across several calls;
   state "press" taps for one frame, which is what Input.IsPressed sees.
3. Key names are the platform's, matched ignoring case, spaces and dashes:
   "w", "space", "left shift" or "lshift", "f1". A rejected name lists near
   misses.
4. Scripts only run in play mode - enter play first or nothing moves.
5. Pointer: input_set_mouse_position (play mode) drives a virtual pointer that
   the game, its UI and the editor all follow - the user's real cursor is not
   moved - then input_send_mouse clicks or scrolls. While it is set the user's
   mouse does not reach the editor, so clear it (clear:true) as soon as the test
   is done; it also clears itself when play ends.

Prove it with numbers, not only pixels: scene_find_entities_batch then
scene_get_bounds_batch (both allowed in play mode) before and after gives a
measurable delta. A displacement that matches speed x frames x frame time is
proof; a screenshot that "looks moved" is not.

Actions, not keys: scripts query action names (Input.GetAnalogValue("Horizontal")),
so a key only does something if an action is bound to it. input_list_actions
shows the bindings, input_map_action adds one. An action no binding points at
silently reads zero.

## Logs and profiling

- logs_get_recent (min_level "warning") after imports, scene loads and play
  sessions; use after_id to tail new entries only.
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
