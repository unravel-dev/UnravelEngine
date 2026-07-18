---
name: unravel-play-mode-change
description: >-
  Guides changes that affect edit mode vs play mode behavior in UnravelEngine.
  Use when features behave differently during play, pause, splash, or resume, or when
  state must reset on play end.
disable-model-invocation: true
---

# Play Mode Change Workflow

## Core concepts

Play state lives in `play_mode` (`engine/engine/play_mode.h`), not in `events`.

`events` still owns signals (`on_play_before_begin`, `on_play_begin`, etc.) and delegates `set_play_mode` / `toggle_pause` to `play_mode`.

### Phases

| Phase | Meaning |
|-------|---------|
| `inactive` | Edit mode |
| `splash` | Play entered; splash visible; simulation not started |
| `running` | `on_play_begin` fired; game/simulation active |

### Key queries

| Query | Use when |
|-------|----------|
| `play.is_active()` | Editor chrome, save blocking, play button stop state |
| `play.is_splash()` | Skip scene render; show splash UI |
| `play.is_simulation_running()` | Physics, scripts, scene on_load, UI hot-reload |
| `play.is_paused()` | Pause/resume behavior |

## Event sequence

### Enter play

1. `on_play_before_begin` — editor prep (checkpoints, script domain reload)
2. `splash` phase (if `settings.splash.enabled`) — `splash_screen` service
3. `on_play_begin` — only when entering `running` phase

### Exit play

1. `on_play_end`
2. `inactive` phase
3. `on_play_after_end`

## Splash screen

- Service: `engine/engine/splash_screen.h`
- Default document: `engine_data/data/ui/splash.rhtml` + `splash.rcss`
- Settings: `settings.splash` in project settings (fade times, custom RML, Made with Unravel toggle)
- Rendering: dedicated RmlUi context (not a scene entity); seq-driven opacity in `on_frame_update`
- Editor display: `game_panel` renders splash texture during `is_splash()`
- Standalone: `runner::on_frame_render` + `splash_screen::present_to`

## Gating simulation

Use `is_simulation_running()`, not `is_active()`:

- `physics_system::on_frame_update`
- `script_system` update/create paths
- `scene::on_load_callback` entity play hooks
- `ui_system` document hot-reload

## System hook priorities

Connect with explicit priority when order matters (see `physics_system`, `script_system`).

## Verification

- [ ] Splash shows before game starts (when enabled)
- [ ] `on_play_begin` fires only after splash completes
- [ ] Stop during splash cancels cleanly
- [ ] Physics/scripts idle during splash
- [ ] Play/end cycles do not leak splash or simulation state
- [ ] Project settings persist splash configuration
- [ ] Standalone game shows splash before startup scene load

## Common mistakes

- Using `is_active()` where `is_simulation_running()` is required
- Firing gameplay logic in `on_play_before_begin` instead of `on_play_begin`
- Putting play flags back on `events` struct
- Loading splash as a scene entity (unnecessary; use `splash_screen` service)
