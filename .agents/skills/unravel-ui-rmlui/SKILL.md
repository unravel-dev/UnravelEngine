---
name: unravel-ui-rmlui
description: >-
  UnravelEngine in-game UI on RmlUi: ui_document components, .rhtml documents and .rcss style
  sheets, the dp unit and screen fitting, pointer and touch input, world-space documents, data
  models and script bindings. Use when game UI does not show, swallows or misses clicks, fits
  a screen wrong, flickers, aliases in 3D, or when editing engine/engine/ui.
---

# Unravel UI (RmlUi)

Covers the in-game UI under `engine/engine/ui/` (system, document component, RmlUi backend,
element queries) and the engine's own documents in `engine_data/data/ui/`. Writing a game's
documents and style sheets (dp, media queries, data models, controls) is the MCP guide
`editor_data/data/mcp/skills/ui.md`; the editor's own UI is ImGui (`unravel-editor-panel`).

## Gotchas

- Documents reload only outside play mode (`update_ui_document_common` checks the play
  state), while changed textures refresh during play (`refresh_changed_textures`): an atlas
  re-packed mid-play shows its new pixels at the old rectangles. UI changes are pushed with
  play stopped.
- `dp` is the display scale in screen space: `ui_system::get_screen_dp_ratio` gives a
  screen-space context the window's display scale (`get_display_scale`, SDL's pixel density
  x content scale, what RmlUi's own SDL platform feeds), or `dpi /
  rendering_system::game_view_device::BASE_DPI` under a Game panel device preset. A
  world-space context stays at 1: its document is measured against its render target.
- Nothing scales `dp` from a script: multiplying by `Screen.dpi` or `pixelsPerDp` counts the
  density twice (a HUD covering a phone's whole screen). A player-facing interface size
  multiplies the body's `font-size` with the layout in `rem`.
- RmlUi `@media (max-width: 500dp)` compares context pixels against the computed length, so it
  works in dp; `resolution` compares the dp ratio. Queries take `and` but no comma list.
- `focus: none` on a clickable element or an ancestor (focus is inherited): RmlUi sends
  `click` to the nearest element that can take focus, so the listener never fires while
  `mousedown` / `mouseup` still do. `ui_bindings.cpp` warns when a click listener is added to
  such an element.
- The UI keeps a pointer press from the game only over content (`is_ui_content_at` in
  `ui_element_queries.h`): something painted (text, background, border, decorator, backdrop
  filter, inside rounded corners, not `pointer-events: none`), or an element under the point
  or an ancestor that takes presses (form control, draggable, scrolling element, a press
  handler in markup or from a script). Empty space belongs to the game even inside a
  full-screen container, and the document element's own handlers do not count: scripts use
  them for delegation, and counting them would block the whole screen.
- Which handlers count as presses is `is_ui_pointer_press_event` (click, dblclick, mousedown,
  mouseup, drag events; hover events do not).
- RmlUi text elements have a zero-size box: text is hit through `GetLines()` and the font's
  ascent and descent at the element's offset, never the element box. RmlUi opacity is
  inherited, not multiplied: an element's own computed opacity is its paint factor.
- RmlUi's `Colour::ToPremultiplied` casts to bytes without a clamp, so an opacity animated past
  1 by an overshooting easing (`back-out`, elastic) wraps colour channels for a few frames
  (gold turns green, white turns cyan). Keyframes with opacity use non-overshooting easings
  (`cubic-out`); a pop animates scale. `deps/3rdparty` stays unchanged.
- A red "!" at the top right of a game capture is the RmlUi debugger beacon, lit by any logged
  RmlUi error until the editor restarts.
- A world-space document reads from its entity's local -z (`get_document_to_world`): an
  entity that faces a camera has its +z pointing away from it. Document pixel (0, 0) sits at
  the quad's local (-0.5, 0.5); `pixels_per_world_unit` is 100 by default.
- World-space documents draw after TAA and tonemapping with the unjittered projection
  (`pipeline::run_ui_pass`, `get_projection_unjittered`) and bring their own edge AA
  (`RmlUi_WorldMesh`, `engine_data/data/shaders/rmlui_world/`). A jittered projection makes
  them shake.
- A world-space document draws straight into the scene (`draws_into_scene`) until it draws
  something only a render target of its own can: a clip mask (overflow clipping with
  border-radius, or a transform with overflowing content), a backdrop filter or a saved layer.
  Then `needs_render_target` holds until the document loads again.
- World meshes live in shared pages (`RmlUi_WorldMeshPool`) and a content-hashed cache
  (`RmlUi_WorldMeshCache`), never in bgfx buffers per geometry: a few hundred nameplates
  exhaust bgfx's vertex buffer handles, silently in release builds.
- Fingers reach documents as RmlUi touches (`dispatch_touch_event`); the platform's mouse copy
  of a finger is cleared only when it lands on content (`consume_touch_mouse_press`). Touch
  is tested with finger events, not mouse events.

## How it works

- Each `ui_document_component` owns an RmlUi context and its document (`.rhtml`, a `ui_tree`
  asset, with `.rcss` style sheets as dependencies; the engine's base sheet is
  `engine_data/data/ui/rml.rcss`). `ui_system::update_ui_documents` loads, reloads and shows
  or hides documents; the render path then updates and draws screen-space documents through
  one layer stack per view, and world-space documents straight into the scene or into their
  own framebuffer.
- Screen-space overlays draw from the lowest `sort_order` up, and the highest takes pointer
  and keyboard input first; equal orders keep no particular order.
- `ui_system` takes OS events on `on_os_event` at priority 100. Keys reach a document only
  while one of its text fields has focus; world-space documents take the pointer through the
  scene's first active camera (`find_pointer_camera`, `project_to_quad`).
- Scripts reach documents through `engine/engine/scripting/ecs/systems/bindings/ui_bindings.cpp`;
  data models are created per context, and script bindings into them are made again when
  `context_generation` changes. Element queries shared by scripts and MCP live in
  `engine/engine/ui/ui_element_queries.h`.
- RmlUi interfaces (render, file, system) are in `engine/engine/ui/rmlui/`; RmlUi shaders are in
  `engine_data/data/shaders/rmlui/` (rendering skill for shader work).

## Procedures

- A new screen layout: lengths in dp, `@media` on width and height for the room there is, a
  `touch` class only for what differs by input (recipe in
  `editor_data/data/mcp/skills/ui.md`, "Fitting every screen").
- A UI input bug: MCP `ui_query_elements` at the game-pixel point (path, visibility, rects,
  computed properties), then `ui_click_element`, whose `covered_by` names what took the click.
- A data-model bug: MCP `ui_get_model` shows the C# side's variables; stale in C# means the
  script did not `Dirty`, stale only in the view means the binding.

## Verify

- `unravel-tests --suite "ui system lifecycle"` (resource release without RmlUi, the dp
  ratio) and `--suite "ui document order"` (sort order).
- Smoke in the UI sample (the sibling repository UnravelEngine-Sample-UI: MainMenu,
  01_Elements, 02_DataModels, 03_WorldSpace): documents load, clicks land, world-space panels
  stay sharp at distance and grazing angles.
- Every shape fits: Game panel presets phone 16:9, 19.5:9, 20:9, tablet 4:3 and desktop, with
  element rects inside the screen and no overlaps (MCP `ui_query_elements`).
- Not covered by tests: density on macOS and Wayland, where window sizes are points and the
  backbuffer is sized from `window.get_size()`.

## Entry points

| Purpose | Path |
|---------|------|
| System | `engine/engine/ui/ecs/systems/ui_system.h` |
| Input dispatch | `engine/engine/ui/ecs/systems/ui_system_input.cpp` |
| Document component | `engine/engine/ui/ecs/components/ui_document_component.h` |
| Hit tests and element queries | `engine/engine/ui/ui_element_queries.h` |
| RmlUi render interface, world meshes | `engine/engine/ui/rmlui/RmlUi_RenderInterface.cpp`, `engine/engine/ui/rmlui/RmlUi_WorldMesh.h` |
| UI pass in the pipeline | `engine/engine/rendering/pipeline/pipeline.cpp` (`run_ui_pass`) |
| Reflection | `engine/engine/meta/ui/ecs/components/ui_document_component.hpp` |
| Script bindings | `engine/engine/scripting/ecs/systems/bindings/ui_bindings.cpp` |
| Game panel UI debugger toggle | `editor/editor/hub/panels/game_panel/game_panel.cpp` |
| MCP UI tools | `editor/editor/system/mcp/mcp_tools_ui_inspect.cpp`, `editor/editor/system/mcp/mcp_tools_ui.cpp` |
