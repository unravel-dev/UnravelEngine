---
name: unravel-editor-panel
description: >-
  UnravelEngine editor UI in ImGui: panels, docking, toolbars, the start page, inspectors,
  property rows, gizmos, selection, editing actions and undo. Use when adding or changing a
  panel, toolbar, inspector, meta attribute for the inspector, gizmo or undoable edit, fixing
  ImGui layout, input, focus or ID bugs, or writing editor UI text.
---

# Unravel editor panels

The editor's ImGui layer and its editing model. Owns `editor/editor/hub/` (hub, start page,
panels), `editor/editor/imgui/` (ImGui integration, theme, loading page) and
`editor/editor/editing/` (`editing_manager`, actions, picking, thumbnails). Inside those
folders other skills own their areas: the material graph panel (`unravel-materials`), the
animation panel (`unravel-animation`), the profiler panel (`unravel-debugging`), the MCP
window (`unravel-editor-mcp`), prefab editing and override paths (`unravel-prefabs`).

## Gotchas

### Editing and undo

- An action that changes only editor state (opens a project or prefab, selects, starts a
  rename, moves the editor camera, writes a file) is an `untracked_editor_state_action_t` or
  `tracked_editor_state_action_t`, or overrides `modifies_scene_content()` to return false:
  the default true marks the scene or prefab unsaved.
- An inspector edits the value it is handed and returns `inspect_result`; `inspect_property`
  writes it through the proxy and records the undo step and the prefab override. Writing the
  live object from inside an inspector bypasses both. Edits that must stay out of undo run in
  `inspect_without_undo_scope`.
- Undo steps are named after their proxy; undo and redo highlight and scroll to the row whose
  proxy carries the same name (`make_property_proxy_name`).
- A drag is one undo step: actions merge while the `merge_session` epoch in
  `editing_manager.cpp` is unchanged (it advances on mouse press, release and focus loss),
  through `is_mergeable` / `merge_with`. A merge keeps the earliest undo state.
- Several dependent edits that make one undo step go in a `sequence_action_t`
  (`composite_action.h`): each step's factory runs after the step before it, so a later step can
  edit an entity an earlier one creates. A step with nothing to do returns null, and a sequence
  of only such steps records no undo step and leaves the scene saved. A function other callers
  may group exposes an `add_..._step(sequence)` beside the call that runs it alone
  (`editor_actions::add_post_process_material_step`).
- Editor code that rewrites scene content returns while `play_mode::is_active()`: at play end
  the game scene reloads from its play-start checkpoint, and every scene reloads after a
  script recompile, so handles kept into any scene go stale (`unravel-ecs-component`).
- Editor timers, highlights and ImGui's `DeltaTime` use `frame_times::real`; the game clocks
  stop while paused and scale with the time scale.

### Inspectors

- A walk over reflected properties (search, filters, counts) stops where drawing stops: at a
  type with its own inspector and at enums. EnTT registers an enum's enumerators as data of
  the enum type, each a value of the enum again, so recursing into them fans out.
- A row's override gutter is drawn in `property_layout::pop_layout`: pop the layout before
  popping the prefab override segment, or the row loses its mark.
- Asset fields and asset inspectors read with `try_get()`; `is_ready()` never starts a load.
  A thumbnail request (`asset_picker::make_preview`) is what starts loading an asset nothing
  else uses.
- `asset_manager::get_assets_with_predicate` returns the empty handle first: that is the
  picker's None tile; do not add another.
- `flattable` is read as a `bool`; the string `"true"` is ignored. The attribute list:
  [add-inspector.md](add-inspector.md).
- A value that does not survive save and reload is usually missing from the component's
  `SAVE` body; read it before changing the inspector.

### ImGui conventions

- Colors come from the theme: the accent from `imgui_style::get_accent_color()`, muted text
  from `imgui_style::get_muted_text_color()`. `ImGuiCol_TabSelected` is the theme's window
  color, not the accent.
- Icon glyphs are merged into the Regular, SemiBold, Bold and ExtraBold fonts only
  (`editor/editor/imgui/integration/imgui.cpp`); an icon drawn in Medium, Light, Black or Mono
  shows `?`.
- An icon-only button is a label-less `ImGui::Button("##id", ImVec2(side, side))` with
  `side = ImGui::GetFrameHeight()`, or `container_widgets::draw_icon_button`, and the glyph is
  drawn by `ImGui::RenderIconCentered` at the item rect's center. As a button label an MDI
  glyph sits off center: ImGui centers its advance, which is wider than its ink. Look for the
  helper the editor already uses for a widget before drawing one.
- A button narrower than its label plus `2 * FramePadding.x` needs `FramePadding.x` pushed to
  0, or ImGui draws the label from the left of the remaining room.
- Icons at a pixel size: `ImGui::PushWindowFontScale(size / ImGui::GetFontSize())`, never
  `PushFont(font, pixels)`, which applies the UI scale a second time.
- A child with `ImGuiChildFlags_Borders` gets `WindowPadding` only while the theme's
  `ChildBorderSize` is not 0. Size its content from inside (`GetContentRegionAvail`), never
  from the parent's width, or push the padding the layout assumes before `BeginChild`.
  Sizes are in font units, not pixels.
- A fixed-height host of stacked children (the header: menu bar and toolbar strip) counts
  `ItemSpacing.y` between them or pulls the cursor back by it; otherwise the last child is
  pushed down and cropped.
- Style pushed for a `Begin` / `BeginChild` is popped right after it: left on the stack, every
  tooltip, popup and child of the content inherits it. A push inside a `Begin` / `End` scope
  popped after the `End` fails ImGui's stack check, shown as a red outline around the window.
- Never test a raw rectangle for input without checking `HoveredWindow`: it reaches through
  popups. Dropdowns open on a press only ([panel-toolbars.md](panel-toolbars.md)).
- An item laid over a tree node with `ImGuiTreeNodeFlags_AllowOverlap` gets the hover only
  when it is submitted after the node (`TableSetColumnIndex` can go back to an earlier
  column).
- A tool that uses Alt claims it with `ImGui::SetKeyOwner(ImGuiMod_Alt, id)` while active:
  with keyboard navigation on, an Alt release otherwise toggles the menu layer
  (`scene_panel.cpp`, orbit).
- `ImStricmp` returns `str2 - str1`, the opposite sign of `strcmp`; sort case-insensitively
  with `std::lexicographical_compare` over lowered characters.
- The view name in `panels_defs.h` is the ImGui window name: `imgui.ini` and the saved layouts
  key the panel's dock place by it, so renaming it loses that place everywhere.
- Release and RelWithDebInfo build `editor/editor` as unity batches: file-local names that
  another file may also use get a feature prefix (`CONSOLE_*`, `CONTENT_*`,
  `SCENE_TOOLBAR_*`), and helpers of new files in `editor/editor/imgui/` and
  `editor/editor/system/` sit in a `<file>_detail` namespace. A wrapper namespace with a
  `using namespace` at `unravel` scope isolates nothing.

### UI text

- The full-sentence rule (AGENTS.md) covers ImGui tooltips (`ImGui::SetItemTooltipEx`),
  notes, the meta `"tooltip"` attribute (the inspector row's tooltip), problems and log lines
  users read. MCP tool descriptions are for agents and keep their own style.
- A field reads as label plus value: the value names the choice in a word or two
  (`Default Desktop`, `Copy of Android`); the explanation goes in a note under the field or a
  tooltip.

### Windows, loading, pages

- Renderer state derived from the window (surface size, backbuffer) is polled every frame,
  loading frames included (`imgui_interface::render_loading_frame` calls
  `renderer::sync_main_surface`): hosts consume window events before the renderer sees them.
- Every full-window ImGui window pins itself to the main viewport with
  `ImGui::SetNextWindowViewport` (`dockspace.cpp`), or it can open as an OS window of its own.
- New full-window pages draw on `screen_card`; start page views go in their own
  `editor/editor/hub/start_page/start_page_*.cpp`.
- The Game panel sets `rendering_system`'s game view device only while it shows a device
  preset (`game_panel::apply_view_size`); the UI dp ratio and the scripts' screen follow that
  device, otherwise the editor window's display scale.
- A phone or tablet preset makes the game behave as that device at every render size, a forced
  capture size included (user rule): never fall back to a desktop because the size differs.
  Play start sets the device before the scripts start (`game_panel::on_play_before_begin`).

## How it works

- `imgui_panels` (`editor/editor/hub/panels/panel.h`) constructs every panel, calls their
  `init` / `deinit`, forwards the frame events to the viewport panels and draws all of them
  from `on_frame_ui_render` (the editor's `ui_events`, `editor/editor/events.h`). `hub` shows
  the start page while no project is open and the panels once one is.
- Most panels derive from `panel_base`: `render_panel` opens the window with
  `get_window_flags()` (a menu bar by default) and calls `draw_ui`; `set_visible_force` /
  `hold_visible_force` keep a hidden view rendering for captures. Default docking is in
  `dockspace.cpp`; saved layouts in `layout_manager`.
- The viewports drive scenes: `scene_panel` updates its own scene (editor camera and
  editor-only entities) and the prefab scene while visible; `game_panel` updates the game
  scene every frame and renders it while visible or forced.
- Picking (`picking_manager`): a click draws entity ids around the cursor (models' geometry,
  text and UI bounds, the icons of `billboard_components`) and takes the id seen most. The first
  click on a nested entity selects its logical top-level entity, the next the entity itself
  (`get_logical_top_level_entity`). A plain click selection whose ray hits a model under a decal
  takes the decal (`pick_decal_over`, the engine's `find_decal_at`); a click on a selected decal
  passes through to the surface. Area picks and drag-and-drop picks (a callback) keep what they hit.
- `editing_manager` holds the selection (`entt::meta_any` objects: entities, assets), focus,
  gizmo, snap and grid settings, prefab mode and the `undo_redo_stack`. `do_action` runs an
  action now; `queue_action` runs it at `execute_actions` during the frame update.
- Inspectors: `inspector_registry` maps each type registered with `REFLECT_INSPECTOR` /
  `REFLECT_INSPECTOR_INLINE` to its inspector. A type without one is drawn property by
  property (`inspect_var_properties`), each property a `property_layout` row: one label
  column divider per window, labels stacked over values below 20 font units, the override
  gutter at the window's edge. Arrays and maps: `inspector_array.cpp`,
  `inspector_associative.cpp`. Gizmos use the same pattern (`GIZMO_REFLECT`).
- Theme: `imgui_style::set_theme(imgui_style::theme::unravel_dark)` at startup
  (`editor/editor/imgui/imgui_interface.cpp`); each theme in `imgui_style.cpp` is a name, a
  palette and the sizes it changes from Unravel Dark's.

## Procedures

- New inspector, attribute reference, gizmo: [add-inspector.md](add-inspector.md).
- Toolbar items, bars, strips, the header and the viewport overlays:
  [panel-toolbars.md](panel-toolbars.md).
- New panel:
  1. A class in its own folder under `editor/editor/hub/panels/`, deriving from `panel_base`
     with `draw_ui` (or a window class drawn with its view name, like `mcp_panel`).
  2. Its view name in `panels_defs.h`; a member in `imgui_panels`, constructed, initialized
     and drawn in `panel.cpp`.
  3. A default dock spot in `dockspace.cpp` if it opens docked, and a Windows menu entry in
     `header_panel.cpp`.
  4. If agents should reach it, `panel_focus` support (`unravel-editor-mcp`).
- New gizmo flag or icon type: [panel-toolbars.md](panel-toolbars.md), "Floating bars".

## Verify

- Build the `editor` target; build RelWithDebInfo as well when a `.cpp` was added (unity
  batches, re-configure first: `unravel-build-verify`).
- In the editor (driven through `unravel-editor-mcp`, captured per attempt):
  - the panel docks, survives a layout save and restore, and works in a narrow dock;
  - every edit undoes and redoes and marks the scene unsaved only when it changed content;
  - selection stays in sync across scene, hierarchy and inspector;
  - entering and leaving play keeps the panel's state;
  - no red outline (ImGui error) and no ID collisions (hidden ids with `##`);
  - icons look centered in a zoomed capture.
- Window or surface changes: open a project from the start page, with `-p`, with `--window`
  geometry, and run the deployed player.

## Entry points

| File | Why |
|------|-----|
| `editor/editor/hub/panels/panel.cpp` | panel construction, frame dispatch |
| `editor/editor/hub/panels/panel_base.h` | panel window base |
| `editor/editor/hub/panels/panel_toolbar.h` | toolbars, strips, dropdowns, overlays |
| `editor/editor/editing/editing_manager.h` | selection, actions, undo, play transitions |
| `editor/editor/editing/actions/editing_action.h` | action contract |
| `editor/editor/hub/panels/inspector_panel/inspectors/inspector.h` | inspector base, `property_layout` |
| `editor/editor/hub/panels/inspector_panel/inspectors/inspectors.cpp` | property drawing, attributes, registry |
| `editor/editor/hub/panels/inspector_panel/inspectors/inspector_entity.cpp` | component headers, icons, Add Component |
| `editor/editor/imgui/integration/imgui_style.cpp` | themes and theme colors |
| `editor/editor/imgui/integration/imgui.h` | fonts and editor ImGui helpers |
