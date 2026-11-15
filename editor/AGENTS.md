# editor/

The editor executable: `core/` (asset compiler, deploy, packages, update check) and
`editor/` (hub and panels, editing actions, project handling, the MCP server).

- Nothing in `engine/` or `game/` depends on this folder. A feature the game needs at run
  time lives in `engine/`; only its editing UI and tooling live here.
- Editor MCP tools (`editor/editor/system/mcp/`) call the functions the UI already uses
  (`editor_actions`, `asset_actions`, panels, inspectors, `project_manager`); extract or
  extend a function instead of writing a second copy in the MCP layer. Skill
  `unravel-editor-mcp`.
- Edits the user can undo go through the actions in `editor/editor/editing/actions/`.
- The asset compiler (`editor/core/asset_compiler/`) is editor-only; the engine loads only
  compiled assets. Skill `unravel-assets`.
- Panels, inspectors and gizmos: skill `unravel-editor-panel`.
