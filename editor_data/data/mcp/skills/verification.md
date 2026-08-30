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
  "normals", "depth", "velocity", ...). Always restore "full" afterwards.

## Play mode testing (physics, animation, particles, scripts)

Mutating tools refuse play mode and the Scene panel does not render during
play, so choreograph:

1. Move the game's Main Camera entity (scene_set_transforms_batch, WORLD pose)
   to frame the target - BEFORE entering play.
2. play_set_active {"active":true}; wait 2-4s real time (the first frame is a
   splash screen - never capture immediately).
3. panel_focus_game, then viewport_capture_game.
4. play_set_active {"active":false} - edit state is restored, including
   everything physics moved.
5. play_get_state answers "am I in play?" when unsure; play_set_paused +
   play_skip_frame give deterministic frame stepping.

Two captures a few seconds apart prove motion (dominoes mid-fall, animation
pose changes) better than one.

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
