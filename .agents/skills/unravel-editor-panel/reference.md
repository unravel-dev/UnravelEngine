# Editor Panel Reference

## Major panels map

| Panel | Directory | Key class |
|-------|-----------|-----------|
| Scene | `scene_panel/` | `scene_panel` |
| Game | `game_panel/` | `game_panel` |
| Hierarchy | `hierarchy_panel/` | `hierarchy_panel` |
| Inspector | `inspector_panel/` | `inspector_panel` |
| Content Browser | `content_browser_panel/` | `content_browser_panel` |
| Console | `console_log_panel/` | `console_log_panel` |
| Animation | `animation_panel/` | animation editing |
| Profiler | `profiler_panel/` | GPU timeline, stats |
| Deploy | `deploy_panel/` | project deployment |
| Settings | `project_settings_panel/`, `editor_settings_panel/` | YAML settings |
| Header | `header_panel/` | menu, play toolbar |
| Layout | `layout_panel/` | dock layouts |

## Editing action pattern

Undoable edits inherit from `editing_action_t`:

1. Capture state before change
2. Apply change
3. `undo()` restores prior state
4. Push to `undo_redo_stack`

Examples in `editor/editor/editing/actions/`.

## ImGui backend

- `editor/editor/imgui/integration/imgui_impl_ospp.cpp` - SDL/ospp backend
- Custom fonts: `editor/editor/imgui/integration/fonts/`
- Shaders for ImGui rendering in integration folder
- Style presets: `imgui_style.cpp`

## Viewport overlays

- Stats: `viewport_stats_overlay.cpp` / `.h`
- Resolution menu: `viewport_resolution.cpp`
- Drawn inside scene/game panel menu bars and child overlays

## Project context

- `project_manager` - open project, paths, settings
- `rtti::context` passed to all panel methods
- `ctx.get_cached<T>()` for engine/editor services
