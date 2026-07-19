---
name: unravel-viewport
description: >-
  Controls the UnravelEngine Scene panel editor camera and viewport screenshots
  via MCP. Use when framing entities, looking at points, orbiting, resetting the
  viewport camera, or capturing Scene/Game screenshots.
disable-model-invocation: true
---

# Unravel Viewport Camera (MCP)

The Scene panel camera lives in a **private panel scene** (`scene_panel`), not the edit scene. Resolve it via `hub → get_scene_panel().get_camera()`.

World axes match entities: **X-right, Y-up, Z-forward**. Camera `position` / `look_at` `target` are world-space.

## Tools

| Tool | Purpose |
|------|---------|
| `viewport_get_camera` | Read pose / FOV / ortho |
| `viewport_set_camera` | Set `position` / `rotation_euler`; `relative:true` = local offset |
| `viewport_look_at` | Aim at world `target` (optional `position`, `up`) |
| `viewport_focus_entities_batch` | `defaults::focus_camera_on_entities` (same as F / double-click) |
| `viewport_focus_bounds` | Focus sphere (`center`+`radius`) or box (`min`+`max`) |
| `viewport_orbit_camera` | Orbit around `pivot` by `yaw`/`pitch` degrees |
| `viewport_reset_camera` | UI “Reset Camera” parity |
| `viewport_screenshot_scene` / `_game` | PNG capture |
| `panel_focus_scene` | Focus Scene panel tab (editing camera) |
| `panel_focus_game` | Focus Game panel tab (game camera) |

Prefer `panel_focus_game` before play-mode verification / game screenshots when the Game tab may be buried. Prefer `panel_focus_scene` before scene camera / selection workflows.

## Focus notes

- `defaults::focus_*` **keeps current rotation** and dollies along forward to fit bounds.
- Pass `aim:true` to `look_at` the bounds center **before** focusing (frame + face).
- `duration` default `0.4` (UI); use `0` for instant (better before screenshots).
- Timed focus uses seq scope `"camera_focus"`; set/look/orbit/reset cancel it first.

## Examples

```json
// Frame an entity and aim at it, instant
{"entity_id":"<uuid>","aim":true,"duration":0}

// Move then look
{"position":[0,2,-8],"target":[0,1,0]}

// Orbit 45° around a pivot
{"pivot":[0,1,0],"yaw":45,"pitch":-10}
```

## Code

| Purpose | Path |
|---------|------|
| Focus APIs | `engine/engine/defaults/defaults.h` |
| Scene panel camera | `editor/editor/hub/panels/scene_panel/scene_panel.*` |
| MCP tools | `editor/editor/system/mcp/mcp_tools_viewport.cpp` |
| Panel focus MCP | `editor/editor/system/mcp/mcp_tools_editor.cpp` (`panel_focus_*`) |
| Panel focus API | `editor_actions::focus_scene_panel` / `focus_game_panel` → `panel_base::focus()` |
