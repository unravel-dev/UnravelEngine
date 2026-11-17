# Panel toolbars

One module gives every panel toolbar the same look and input rules: `panel_toolbar`
(`editor/editor/hub/panels/panel_toolbar.h`). Read this before adding an item, a bar or a strip.

## Contents

- Containers
- Items
- Input rules
- Floating bars: scene and game
- Docked strips: content, console, header, inspector

## Containers

- Bar (`begin_bar` / `end_bar`): floats over a viewport (scene, game). Every bar is a child
  window, so clicks never reach picking, drag selection or ImGuizmo below. `end_bar()` hands
  the keyboard focus back to the panel, so viewport shortcuts (W/E/R/F) keep working after a
  toolbar click. Always pair `begin_bar` with `end_bar`, whatever `begin_bar` returned.
- Strip (`begin_strip` / `end_strip`): docked at the top of a panel (content, console,
  inspector) or of the editor itself (the header, `strip_style::flat`: no card, the host is the
  band).
- Overlay card: `begin_overlay(id, top_right, width, max_height)` sizes its height to the
  content one frame late; `begin_overlay(id, rect)` takes an exact rectangle, for a card
  anchored to a bottom corner.
- The scene, game, content, console and inspector panels have no menu bar
  (`get_window_flags()` returns no `ImGuiWindowFlags_MenuBar`). Other panels keep classic
  menu bars; a horizontal `MenuItem` is wider than `CalcTextSize` of its label.

## Items

- `button`, `toggle` (optional `active_color` for a state with its own color: playing, paused),
  `filter_toggle` (own color, dimmed when off, for rows of filters), `begin_dropdown` /
  `end_dropdown` (null text = the caret half of a split button), `separator`,
  `path_separator` (breadcrumb), `label`.
- Pass `width_text` for any label that changes (FPS, state names, a dropdown face showing the
  current value): an anchored bar is placed from last frame's width and jitters otherwise.
- `align_center()` / `align_right()` send the following items to the middle or the right end.
- `begin_field(width)` / `end_field()` wrap one framed ImGui widget (search input, slider);
  `calc_flexible_width(min, max)` lets a search field give way in a narrow dock;
  `search_field` is the search box the hierarchy and the inspector share.
- `begin_group()` / `end_group()` make several items one ImGui item (one tooltip, one
  `BeginDisabled`); `begin_segment()` / `end_segment()` do the same on a rounded fill of
  their own: a segmented control (the header's play, pause and step).
- `draw_readout`: a passive pill drawn on the host's draw list only, with no window or item,
  so clicks pass through it.

## Input rules

- Dropdowns open on a press and on nothing else, never on hover.
- An open popup shields what lies under it. A press on another dropdown while one is open
  switches in one click: the popup blocks that button's hover, so
  `take_press_blocked_by_dropdown` hands the press over and claims the hover with
  `SetHoveredID`. Unclaimed, the bar's click blocker and the end-of-frame click on empty space
  take the same press and close the new popup.
- Never test a raw rectangle for input without checking `HoveredWindow`: the test reaches
  through popups.
- Debug dropdown and focus problems with ImGui's event log (`DebugLogFlags` `EventPopup`,
  `EventFocus`, `EventActiveId`) before changing input code.

## Floating bars: scene and game

- `scene_panel.cpp`, `draw_toolbar`: tools bar (left), view bar (right), prefab bar (centered,
  own row, prefab mode only).
- Layout falls back full -> compact (icons only) -> stacked (view bar on row 2) from the
  measured bar widths (`panel_toolbar::layout_state`, `update_layout`). Overlays that share the
  top edge (view cube, `viewport_stats_overlay::draw` with `top_offset`) start below
  `panel_toolbar::get_rows_extent(rows)`.
- `editor/editor/hub/panels/scene_panel/gizmos_menu.cpp`: the gizmos split button. `editing_manager::show_gizmos` is the
  master switch over every icon and gizmo (not the transform handles, not the grid). The popup
  is a table of object types against Icons (every object) and Gizmos (the selection). A new
  gizmo flag is a `ROWS` entry or a detail of one and gets a line in the serialize of
  `editor/editor/meta/project/project_editor_settings.cpp`; a new icon type is added to
  `billboard_components`, `billboard_gizmos::is_shown`, `ROWS` and
  `thumbnail_manager::get_gizmo_icon` (an icon in `editor_data/data/icons/` with its `.meta` and
  compiled asset); a `shows_billboard` overload leaves out objects of the type that draw none
  (decals spawned in play). The settings
  (`editor/editor/editing/gizmo_settings.h`) live in `editing_manager` while a project is open;
  a change on the toolbar also goes to the project's settings in `app:/editor/editor.cfg`.
  `viewport_set_overlays` (MCP) changes only the live values, so a capture is never saved.
- `game_panel.cpp`, `draw_toolbar`: one bar on the right. While the game plays the bar fades
  out and is not submitted (an invisible bar would eat the game's clicks); the pointer at the
  top edge or an open dropdown brings it back (`update_toolbar_visibility`). The frame rate
  stays: `viewport_stats_overlay::draw_toolbar_readout` draws it with `draw_readout` in the
  exact place the statistics toggle shows it, so the two cross-fade.
- Items both panels share live in their own module: `viewport_resolution::draw_toolbar_dropdown`,
  `visualization_menu::draw_toolbar_dropdown`, `viewport_stats_overlay::draw_toolbar_toggle`.
- A popup whose content fills its height (the entity inspector in the camera popup) needs an
  explicit size, not auto-resize; a `SetNextWindowSize*` right before `begin_dropdown` reaches
  the popup.
- The selected camera's preview
  (`editor/editor/hub/panels/scene_panel/camera_preview_overlay.cpp`, drawn from
  `scene_panel::draw_selected_camera`) is an overlay card: title row with align-with-view
  (Shift+F) and collapse, output fitted to its aspect, resolution and projection badge; hidden
  when the view is too small for it.

## Docked strips: content, console, header, inspector

- `content_browser_panel.cpp`, `draw_explorer` = `draw_toolbar` (Add dropdown reusing the
  context menu's create and import entries, breadcrumb of buttons that are drop targets,
  search) + `draw_assets` + `draw_status_bar` (item count, icon scale).
- `console_log_panel.cpp`: `draw_toolbar` (Clear and auto-clear options, search, level
  filters with counts), `draw_log_list` (single-line rows through `draw_log_line`, which the
  status bar reuses for the last log), `draw_details`.
- `header_panel.cpp`, `draw_play_toolbar`: flat strip. Play options dropdown (script mode and
  splash, locked while playing), preview profile dropdown (`draw_preview_profile`: the
  settings the editor runs with, the project's or a settings profile's; locked while playing)
  | play, pause and step as one segment (disabled by compile errors), then Deploy and Run in
  its own segment (shows progress while deploying; a click opens the Deploy window) | time
  scale dropdown, frame pacing dropdown (VSync and max FPS). `draw_title` centers project and
  scene on the menu bar and yields to the menus; `draw_mcp_status` sits at the menu bar's right
  end, aligned with `get_bar_margin()`; `draw_play_state_line` tints the bottom edge while
  playing or paused. `header_panel::calc_height()` tells `panel.cpp` how tall the header is.
- `inspector_panel.cpp`, `draw_toolbar`: lock, search | overridden-only filter, expand and
  collapse all (a segment), debug view. Component
  headers come from `draw_component_header` in `inspector_entity.cpp`: a
  `CollapsingHeader("##...")` for the behavior, with the title and a flat settings button drawn
  over it, no space-padded labels.
