---
name: unravel-editor-panel
description: >-
  Develops UnravelEngine editor UI: ImGui panels, menu bars, dockspace, gizmos,
  inspectors, undo/redo, and viewport overlays. Use for editor panels, hub
  integration, selection, editing actions, or ImGui layout bugs.
---

# Unravel Editor Panel

## Start here

| Purpose | Path |
|---------|------|
| Editor entry | `editor/editor/editor.h` |
| Hub orchestrator | `editor/editor/hub/hub.cpp` |
| Panel definitions | `editor/editor/hub/panels/panels_defs.h` |
| Scene viewport | `editor/editor/hub/panels/scene_panel/` |
| Game view | `editor/editor/hub/panels/game_panel/` |
| Inspector | `editor/editor/hub/panels/inspector_panel/` |
| Hierarchy | `editor/editor/hub/panels/hierarchy_panel/` |
| Content browser | `editor/editor/hub/panels/content_browser_panel/` |
| Profiler | `editor/editor/hub/panels/profiler_panel/` |
| Header / play controls | `editor/editor/hub/panels/header_panel/` |
| ImGui integration | `editor/editor/imgui/integration/` |
| Editing core | `editor/editor/editing/editing_manager.h` |
| Undo/redo | `editor/editor/editing/undo_redo_stack.h` |

## Panel lifecycle

1. Panels registered in hub / `imgui_panels`
2. View names from `panels_defs.h` (e.g. `SCENE_VIEW`, `INSPECTOR_VIEW`)
3. Frame callbacks: `on_frame_ui_render` in editor mirrors engine `events`
4. Docking via ImGui dockspace; layouts in `layout_panel/`, `layout_manager`

## Selection and editing

- `editing_manager` - active selection, snap settings, grid, gizmo state
- Selection is `entt::handle` based
- Undo via `editing_action_t` derivatives in `editor/editor/editing/actions/`
- Property changes should go through undo actions when user-visible

## Inspector system

- `inspector_registry` auto-discovers `inspector` subclasses
- Component icons in `inspector_entity.cpp` -> `get_component_icon<T>()`
- Property drawing uses EnTT meta reflection
- Prefab overrides: recorded as statements on the nearest instance root's `local`
  list (`prefab_property_override_data`, pretty + component paths) - see
  `unravel-prefabs`

Use `unravel-add-inspector` for custom type inspectors.

## Gizmos

`editor/editor/hub/panels/scene_panel/gizmos/`:

- Transform, physics shape, character controller gizmos
- ImGuizmo integration in scene panel
- Gizmo shaders in `editor_data/data/shaders/`

## ImGui conventions

- Custom widgets: `deps/3rdparty/imgui/imgui_widgets/`, `editor/editor/imgui/`
- Icons: Material Design Icons via `icons_material_design_icons.h`
- `ImGui::AlignedItem()` for horizontal alignment - include `FramePadding` in item width for buttons/menu items
- `ImGui::SetItemTooltipEx()` for tooltips (project wrapper)
- Menu bar items: account for `FramePadding` and `ItemSpacing` when right-aligning

## Panel toolbars

One module gives every panel toolbar the same look: `panel_toolbar`
(`editor/editor/hub/panels/panel_toolbar.h`). Two containers, one set of items:

- **bar** (`begin_bar` / `end_bar`): floats over a viewport - scene, game
- **strip** (`begin_strip` / `end_strip`): docked at the top of a panel - content, console,
  inspector - or of the editor itself (header, `strip_style::flat`: no card, the host is the
  band). `align_center()` / `align_right()` send the following items to the middle / the right
  end; `begin_field(width)` / `end_field()` wrap one framed ImGui widget (search input,
  slider); `calc_flexible_width(min, max)` lets a search field give way in a narrow dock;
  `begin_group()` / `end_group()` make several items one ImGui item (one tooltip, one
  `BeginDisabled`)

The scene, game, content, console and inspector panels have NO menu bar (`get_window_flags()`
returns no `MenuBar`).

### Floating bars (scene, game)

- `scene_panel.cpp` -> `draw_toolbar`: tools bar (left), view bar (right), prefab bar (centered,
  own row, prefab mode only)
- `game_panel.cpp` -> `draw_toolbar`: one bar on the right. While the game plays it fades out
  and is NOT submitted (an invisible bar would eat the game's clicks); the pointer at the top
  edge or an open dropdown brings it back (`update_toolbar_visibility`). The frame rate stays:
  `viewport_stats_overlay::draw_toolbar_readout` draws it as a passive
  `panel_toolbar::draw_readout` (draw list only, clicks pass through) in the exact place the
  statistics toggle shows it, so the two cross-fade
- Every bar is a child window, so clicks never reach picking / drag selection / ImGuizmo below;
  `end_bar()` hands the keyboard focus back to the panel so viewport shortcuts keep working
- Items: `button`, `toggle` (optional `active_color` for a state with its own color: playing,
  paused), `filter_toggle` (own color, dimmed when off - for rows of filters),
  `begin_dropdown` / `end_dropdown` (null text = caret half of a split button),
  `separator`, `path_separator` (breadcrumb), `label`; pass `width_text` for a label that
  changes (FPS, state names) or an anchored bar jitters
- Layout falls back full -> compact (icons only) -> stacked (view bar on row 2) from the
  measured bar widths (`panel_toolbar::layout_state` + `update_layout`); overlays that share
  the top edge (view cube, `viewport_stats_overlay::draw` `top_offset`) start below
  `panel_toolbar::get_rows_extent(rows)`
- Items shared by both panels live in their own module: `viewport_resolution::draw_toolbar_dropdown`,
  `visualization_menu::draw_toolbar_dropdown`, `viewport_stats_overlay::draw_toolbar_toggle`
- A popup whose content fills its height (entity inspector) needs an explicit size, not
  auto-resize; a `SetNextWindowSize*` right before `begin_dropdown` reaches the popup

### Docked strips (content, console)

- `content_browser_panel.cpp` -> `draw_explorer` = `draw_toolbar` (Add dropdown reusing the
  context menu's create / import entries, breadcrumb of buttons that are drop targets, search)
  + `draw_assets` + `draw_status_bar` (item count, icon scale)
- `console_log_panel.cpp` -> `draw_toolbar` (Clear + auto-clear options, search, level filters
  with counts), `draw_log_list` (single-line rows through `draw_log_line`, which the status bar
  reuses for the last log), `draw_details`
- `header_panel.cpp` -> `draw_play_toolbar`: flat strip - deploy | play / pause / step (one
  group, disabled by compile errors), script mode, splash | time scale, VSync, max FPS.
  `header_panel::calc_height()` tells `panel.cpp` how tall the header is
- `inspector_panel.cpp` -> `draw_toolbar` (lock, debug view); component headers come from
  `draw_component_header` in `inspector_entity.cpp` (a `CollapsingHeader("##...")` for the
  behaviour, title and flat settings button drawn over it - no space-padded labels)
- Unity builds merge anonymous namespaces: prefix file-local constants and helpers by feature
  (`CONSOLE_*`, `CONTENT_*`, `SCENE_TOOLBAR_*`), or a shifted batch boundary breaks the build

Other panels (inspector, hierarchy, ...) keep classic menu bars: horizontal `MenuItem` layout -
width exceeds `CalcTextSize` label.

## Play mode UI

- Header panel play controls: `header_panel.cpp`
- Game panel visibility toggled from scene panel
- Editor chrome remains during play; game view shows runtime output
- Use `unravel-play-mode-change` when behavior differs edit vs play

## Verification checklist

- [ ] Panel docks and persists in saved layout
- [ ] Selection syncs across scene/hierarchy/inspector
- [ ] Undo/redo works for the change
- [ ] Play mode enter/exit preserves panel state
- [ ] No ImGui ID collisions (use `##` prefixes for hidden IDs)
- [ ] Tooltips and icons match surrounding panels
- [ ] Right-aligned menu items not clipped at viewport edge

## Common mistakes

- Raw ImGui IDs causing state bleed between panels
- Editing without undo action (user cannot revert)
- Engine code in editor target only - keep editor logic in `editor/`
- Forgetting `ImGui::SameLine()` spacing when aligning items
- Using label text width only for `AlignedItem` on interactive widgets

## Deep reference

See [reference.md](reference.md) for panel map and editing patterns.
