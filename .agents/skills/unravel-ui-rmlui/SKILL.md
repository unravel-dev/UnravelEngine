---
name: unravel-ui-rmlui
description: >-
  Works on UnravelEngine in-game UI using RmlUi: HTML/CSS documents, ui_document
  component, world-space UI, and UI debugger. Use for game UI screens, RML/RCSS
  assets, UI rendering, or UI input handling.
---

# Unravel UI (RmlUi)

## Start here

| Purpose | Path |
|---------|------|
| UI system | `engine/engine/ui/ecs/systems/ui_system.h` |
| UI document component | `engine/engine/ui/ecs/components/ui_document_component.h` |
| RmlUi backend | `engine/engine/ui/rmlui/` |
| Meta | `engine/engine/meta/ecs/components/ui_document_component.hpp` |
| Game panel debugger | `game_panel.cpp` - UI Debugger menu toggle |
| Templates | `engine_data/data/` (UI templates if present) |

## Architecture

- **RmlUi** renders HTML+CSS to game viewport
- `ui_document_component` attaches `.rml` document to entity
- `ui_system` processes OS events, updates documents, renders layer stack
- Backend interfaces: `RmlUi_Backend_Engine`, `RmlUi_RenderInterface`, `RmlUi_FileInterface`, `RmlUi_SystemInterface`

## Document assets

UI documents are assets - follow `unravel-assets` for import/meta. RML + RCSS file pairs.

## World-space UI

Documents can render in world space (3D positioned UI). Transform linkage via entity `transform_component`.

## Screen sizes

One stylesheet fits every screen. The recipe for writing one is in the MCP skill
`editor_data/data/mcp/skills/ui.md`, "Fitting every screen": lengths in `dp`, `@media` on width
and height for the room there is, a `touch` class only for what differs by input.

- `dp` is density-independent, the same unit as a CSS px or an Android dp.
  `ui_system::get_screen_dp_ratio` gives a screen-space context the display scale
  (`get_display_scale`, SDL's pixel density x content scale), which is what RmlUi's own SDL
  platform feeds. A Game panel device preset gives `dpi / game_view_device::BASE_DPI`, so it
  matches the device. A world-space context stays at 1: its document is measured against its
  render target.
- RmlUi: `@media (max-width: 500dp)` compares context pixels against the computed length, so it
  works in dp. `resolution` compares the ratio. Queries take `and` but no comma list.
- The ratio is checked by the suite `ui system lifecycle`. Density on macOS and Wayland, where a
  window's size is in points, is not tested: the backbuffer is sized from `window.get_size()`.
- Never scale `dp` from a script. Multiplying by `Screen.dpi` or `pixelsPerDp` counts the density
  twice, and that is how a HUD comes to cover a phone's whole screen. A player-facing interface
  size can multiply the body's `font-size`, with the layout in `rem`.

## UI debugger

Toggle in game panel menu bar:

- `ui_system::is_debugger_enabled()`
- Highlights element bounds for debugging layout

## Input

`ui_system` connects to `on_os_event` with priority 100. Coordinate mapping via `input::zone` for window-relative coords.

## Verification checklist

- [ ] Document loads and displays
- [ ] CSS styles apply correctly
- [ ] Input events reach focused elements
- [ ] World-space UI follows entity transform
- [ ] UI debugger shows correct bounds
- [ ] Play mode only behavior where appropriate
- [ ] No conflict with ImGui editor overlay
- [ ] Fits every shape: phone 16:9, 19.5:9, 20:9, tablet 4:3, desktop (rects inside the screen, no overlaps)

## Common mistakes

- Confusing editor ImGui with in-game RmlUi
- Scaling `dp` from a script, or making a layout fit with `body.touch`: a phone is `@media (max-height: 500dp)`
- Missing RCSS companion file
- OS events not forwarded to ui_system
- Document asset path/protocol incorrect
- `focus: none` on a clickable element or its ancestor (focus is inherited): RmlUi sends
  `click` to the nearest element that can take focus (`Context::FindFocusElement`), so the
  listener never fires; `mousedown` / `mouseup` still do. `ui_bindings.cpp` warns when a
  click listener is added to such an element
