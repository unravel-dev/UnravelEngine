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

## Common mistakes

- Confusing editor ImGui with in-game RmlUi
- Missing RCSS companion file
- OS events not forwarded to ui_system
- Document asset path/protocol incorrect
