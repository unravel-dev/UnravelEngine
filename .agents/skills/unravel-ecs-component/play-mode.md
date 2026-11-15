# Play mode

Edit mode, the splash, running play, pause and frame steps: what each phase means, the order
events fire in, and what a system must gate on. Source: `engine/engine/play_mode.h` / `.cpp`,
`engine/engine/splash_scene.h` / `.cpp`, `editor/editor/editing/editing_manager.cpp`.

## Contents

- Gotchas
- Phases and queries
- Event order
- Splash
- Editor facade
- Verify

## Gotchas

- Play state and its transitions live on `play_mode` (`ctx.get_cached<play_mode>()`).
  `events` only holds the signals; the play methods declared on `events` (`set_play_mode`,
  `toggle_pause`, `skip_next_frame`, ...) have no definition, and a call fails to link.
- Gate simulation on `is_simulation_running()` (phase `running`) and pause on
  `is_simulation_paused()` (paused and not stepping). `is_active()` is also true during the
  splash; `is_paused()` stays true during a frame step, so a system gated on it skips the step.
- `on_play_end` fires only when play reached `running`. A stop during the splash raises
  `on_play_after_end` alone. Cleanup that must run after every play session goes in
  `on_play_after_end`.
- Gameplay setup goes in `on_play_begin`, never in `on_play_before_begin`: in the editor the
  checkpoint reload that follows `on_play_before_begin` replaces every scene's entities.
- `script_system` handles `on_play_begin` at -1000, after the engine systems, and
  `on_play_end` at 1000, before them: engine state is ready when scripts start and still there
  when they end. A new system that scripts rely on keeps that relation.
- With the splash enabled, the editor loads the game scene inside `on_play_begin` at priority
  2000 (`editing_manager`); the player's `runner` loads the startup scene at -100000, after the
  scripts. Handlers below 2000 in the editor see the game scene; the player's systems see it
  through `on_load_callback`, which gives the loaded entities their play begin.
- The splash is not a separate renderer path: `splash_scene::setup` unloads the active game
  scene and creates a camera and a screen-space UI document entity in it. Systems that run on
  the game scene during the splash see those two entities.
- The first running frame and every paused frame have zero game time (`frame_times::scaled`
  and `unscaled`); a frame step runs one paused frame with `play_mode::FRAME_STEP_SECONDS`, in
  the usual system order. Code that must not integrate a hitch on start reads
  `frames_running() == 0`.
- `toggle()` and `toggle_pause()` are queued on the `play_mode` seq scope and apply at the next
  seq update; `set_active()` and `set_paused()` apply at once.
- Edits made in the editor while playing are dropped: `on_play_after_end` reloads the game
  scene from the checkpoint taken at play start. Editor code that rewrites scene content
  returns while `play_mode::is_active()` (`editing_manager::on_prefab_updated`).

## Phases and queries

| Phase | Meaning |
|-------|---------|
| `inactive` | edit mode |
| `splash` | play entered, splash entities shown, no simulation |
| `running` | `on_play_begin` raised, simulation runs (possibly paused) |

| Query | Use for |
|-------|---------|
| `is_active()` | editor chrome, save blocking, the play button's stop state |
| `is_splash()` | splash-only display |
| `is_simulation_running()` | physics, scripts, load callbacks, UI document hot reload |
| `is_paused()` | pause state shown to the user (true during a step) |
| `is_simulation_paused()` | holding simulation work still (false during a step) |
| `is_stepping()` | the current paused frame is a frame step |
| `frames_running()` | frames since `running`, paused ones included; 0 on the first |
| `frames_simulated()` | frames that advanced: unpaused frames and steps |

## Event order

Enter (`play_mode::set_active(ctx, true)`):

1. `on_play_before_begin`. In the editor `editing_manager` (1000) waits for script jobs, saves
   prefab-mode edits, clears undo, checkpoints every scene, unloads scripting, reloads the
   script domains with a recompile and reloads every scene from its checkpoint - except the
   game scene when `settings.splash.enabled`.
2. Splash when `settings.splash.enabled` and there is a logo to show
   (`splash_scene::has_content`): phase `splash`, `splash_scene::setup`. Each frame
   `play_mode::on_frame_update` (priority `play_mode`) starts the logo sequence once the
   document has loaded and enters `running` when it has finished.
3. `enter_running`: phase `running`, frame counters reset, `simulation::reset_delta_clock()`,
   splash teardown, then `on_play_begin` (subscriber priorities in
   [reference.md](reference.md)).

Exit (`play_mode::set_active(ctx, false)`):

1. Unpause when paused (`on_resume`).
2. `on_play_end`, only from `running`.
3. Phase `inactive`, counters and pending steps reset, splash teardown.
4. `on_play_after_end`. In the editor `editing_manager` (-1000) checkpoints every scene but the
   game scene, reloads the script domains, reloads every scene from its checkpoint (the game
   scene from play start), restores the unsaved-changes flag and sets the time scale to 1.

## Splash

- Settings: `settings.splash` in `engine/engine/settings/settings.h` (`enabled`,
  `show_made_with`, fade times, `logos`).
- Document: `SPLASH_DOCUMENT_KEY` (`engine/engine/defaults/engine_asset_keys.h`), the files
  `engine_data/data/ui/splash.rhtml` and `splash.rcss`; logo timing is a seq sequence on the
  `splash_scene` scope.
- Editor: the Game panel shows the splash like any game frame. Player: `game/game/game.cpp`
  activates play, and `runner` loads the startup scene in `on_play_begin`.

## Editor facade

UI and MCP enter, pause and step play through `editor_actions`
(`editor/editor/editing/editor_actions.h`), not through `play_mode` directly:

| Function | Role |
|----------|------|
| `can_enter_play` | the compile-error gate |
| `toggle_play`, `set_play_active` | enter or leave play |
| `set_play_paused`, `skip_play_frame` | pause, frame step |
| `get_play_state` | phase, pause, frame counters |

MCP tools `play_set_active`, `play_set_paused`, `play_skip_frame`, with the state in
`editor_get_status` (skill `unravel-editor-mcp`).

## Verify

- Splash on: logos show, `on_play_begin` follows them; a stop during the splash raises no
  `on_play_end` and restores the scene.
- Paused, one frame step: physics, scripts and animation advance exactly one frame
  (`frames_simulated` grows by one).
- Several play and stop cycles: the edit scene keeps its entity count and values, and nothing
  of the session (sounds, bodies, script objects) is left.
- The player shows the splash before the startup scene.
