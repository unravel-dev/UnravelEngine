# Editor panel reference

## Panels

All under `editor/editor/hub/panels/`, owned by `imgui_panels` (`panel.h` / `panel.cpp`).

| Panel | Folder | Class | Notes |
|-------|--------|-------|-------|
| Scene | scene_panel/ | `scene_panel` | editor camera, gizmos, picking; own scene |
| Game | game_panel/ | `game_panel` | drives and renders the game scene |
| Hierarchy | hierarchy_panel/ | `hierarchy_panel` | clipped rows, search, cells in `hierarchy_cells.*` |
| Inspector | inspector_panel/ | `inspector_panel` | inspectors in its inspectors/ folder |
| Content | content_browser_panel/ | `content_browser_panel` | asset browser |
| Console | console_log_panel/ | `console_log_panel` | also the log sink |
| Header | header_panel/ | `header_panel` | menu bar, play toolbar, MCP status |
| Footer | footer_panel/ | `footer_panel` | status bar, job card |
| Layouts | layout_panel/ | `layout_panel` | saved dock layouts (`layout_manager`) |
| Project / Editor Settings | project_settings_panel/, editor_settings_panel/ | | both on `settings_view` |
| Deploy | deploy_panel/ | `deploy_panel` | deploy window |
| Undo history | undo_redo_panel/ | `undo_redo_panel` | |
| Style | style_panel/ | `style_panel` | theme switcher |
| Profiler | profiler_panel/ | `profiler_timeline_panel` | skill `unravel-debugging` |
| Animation Controller | animation_panel/ | `animation_panel` | skill `unravel-animation` |
| Material Graph | material_graph_panel/ | `material_graph_panel` | skill `unravel-materials` |
| MCP Server | mcp_panel/ | `mcp_panel` | skill `unravel-editor-mcp` |
| Packages | packages_panel/ | `packages_panel` | content packages |

Outside the panels: `editor/editor/hub/start_page/` (no project open),
`editor/editor/hub/about_window/`, `editor/editor/imgui/loading_page.*` (loading screen),
`editor/editor/imgui/screen_card.*` (backdrop and card both pages use).

Shared panel pieces: `panel_toolbar` ([panel-toolbars.md](panel-toolbars.md)),
`panel_section` (folding section header with an open state in window storage),
`settings_view` (sidebar and pages of the settings windows), `viewport_resolution`,
`visualization_menu` / `visualization_modes` (debug views; the modes file has no ImGui
dependency because MCP includes it), `viewport_stats_overlay`.

## Editing actions

In `editor/editor/editing/actions/`, all deriving from `editing_action_t`
(`editing_action.h`): `do_action`, `undo_action`, `is_undoable`, `is_mergeable` /
`merge_with`, `modifies_scene_content`, `detach`.

| File | Actions |
|------|---------|
| `property_actions.h` | `property_action_t`: one reflected property through a proxy |
| `entity_actions.h` | create / delete entities, add / remove / reset components, active, name, tag, layers, static, materials, script components |
| `transform_actions.h` | move, rotate, scale, skew, set parent |
| `selection_action.h` | selection changes (not a scene edit) |
| `lambda_actions.h` | `untracked_action_t`, `untracked_editor_state_action_t`, `tracked_lambda_action_t`, `tracked_editor_state_action_t` |
| `composite_action.h` | `composite_action_t`, `sequence_action_t`: several steps as one |
| `undo_redo_stack.h` | the stack, merge handling, last action name and age |

Running them: `editing_manager::do_action` executes now, `queue_action` at
`execute_actions` in the frame update; both take a name, which is the undo step's label.

## ImGui integration

- `editor/editor/imgui/integration/backend/imgui_impl_ospp.cpp`: platform backend on ospp.
- `editor/editor/imgui/integration/imgui.h` / `.cpp`: fonts (`ImGui::Font::*`), texture ids,
  editor helpers (`RenderIconCentered`, `PushReadonly`, ...).
- `editor/editor/imgui/integration/imgui_style.cpp`: themes, `get_accent_color`,
  `get_muted_text_color`.
- `editor/editor/imgui/integration/imgui_notify.h`: toasts.
- `editor/editor/imgui/integration/fonts/`: Inter, Roboto Mono and the icon fonts
  (`editor/editor/imgui/integration/fonts/icons/icons_material_design_icons.h`
  for `ICON_MDI_*`).
- `editor/editor/imgui/imgui_interface.cpp`: frame begin and end, theme at startup, loading
  frames (`render_loading_frame`).
- Generic widget helpers (`ImGui::AlignedItem`, `ImGui::SetItemTooltipEx`, `ItemBrowser`) are
  in `deps/3rdparty/imgui/imgui_widgets/`, under the `deps/` rules.

## Project and context

- Panel methods take `rtti::context&`; services come from `ctx.get_cached<T>()`.
- `project_manager` (`editor/editor/system/project_manager.h`): open project, paths,
  project and editor settings.
- `editor_actions` (`editor/editor/editing/editor_actions.h`): the operations UI and MCP
  share (play, scenes, panels, deploy).
