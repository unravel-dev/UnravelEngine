---
name: unravel-viewport
description: >-
  Controls the UnravelEngine Scene panel editor camera and viewport captures
  via MCP. Use when framing entities, looking at points, orbiting, resetting the
  viewport camera, or capturing Scene/Game PNGs.
---

# Unravel Viewport Camera (MCP)

The Scene panel camera lives in a **private panel scene** (`scene_panel`), not the edit scene. Resolve it via `hub -> get_scene_panel().get_camera()`.

World axes match entities: **X-right, Y-up, Z-forward**. Camera `position` / `look_at` `target` are world-space.

## Tools

| Tool | Purpose |
|------|---------|
| `viewport_get_camera` | Read pose / FOV / ortho |
| `viewport_set_camera` | Set `position` / `rotation_euler`; `relative:true` = local offset |
| `viewport_look_at` | Aim at world `target` (optional `position`, `up`) |
| `viewport_focus_entities_batch` | Dolly along current forward to fit entities (same as F / double-click) |
| `viewport_focus_bounds` | Focus sphere (`center`+`radius`) or box (`min`+`max`) |
| `viewport_orbit_camera` | Orbit around `pivot` by `yaw`/`pitch` degrees |
| `viewport_reset_camera` | UI "Reset Camera" parity |
| `viewport_capture_scene` / `viewport_capture_game` | PNG capture; `wait_ms` (default 500), `scale` (default 1, bimg linear resize) |
| `panel_focus_scene` | Focus Scene panel tab (editing camera) |
| `panel_focus_game` | Focus Game panel tab (game camera) |

Prefer `panel_focus_game` before play-mode verification / game captures when the Game tab may be buried. Prefer `panel_focus_scene` before scene camera / selection workflows.

For agent loops, prefer `scale:0.5` to cut token cost. Default `wait_ms` is 500. Scaling/encoding uses bimg.

Mutation tools return lean `{ok:true}` (plus small args). Full pose only from `viewport_get_camera`.

## Focus notes

- `defaults::focus_*` **keeps current rotation** and dollies along forward to fit bounds.
- To face then frame: `viewport_look_at` then `viewport_focus_*` (do not reimplement look_at inside focus).
- `duration` default `0.4` (UI); use `0` for instant (better before captures).
- Timed focus uses seq scope `"camera_focus"`; set/look/orbit/reset cancel it first.

## Examples

```json
// Face then frame, instant
// 1) viewport_look_at {"target":[0,1,0]}
// 2) viewport_focus_entities_batch {"entity_id":"<uuid>","duration":0}

// Move then look
{"position":[0,2,-8],"target":[0,1,0]}

// Orbit 45 deg around a pivot
{"pivot":[0,1,0],"yaw":45,"pitch":-10}
```

## Code

| Purpose | Path |
|---------|------|
| Focus APIs | `engine/engine/defaults/defaults.h` |
| Scene panel camera | `editor/editor/hub/panels/scene_panel/scene_panel.*` |
| MCP tools | `editor/editor/system/mcp/mcp_tools_viewport.cpp` |
| Panel focus MCP | `editor/editor/system/mcp/mcp_tools_editor.cpp` (`panel_focus_*`) |
| Panel focus API | `editor_actions::focus_scene_panel` / `focus_game_panel` -> `panel_base::focus()` |
