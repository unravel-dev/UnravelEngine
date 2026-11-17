---
name: unravel-editor-mcp
description: >-
  Running a live Unravel editor for engine work and driving it over its MCP server (own port,
  raw JSON-RPC, logs, captures, play mode), and changing that server: MCP tools, argument
  schemas, renames, the guides it serves. Use when a change needs checking in a running
  editor, when a tool call fails or answers unexpectedly, or when editing
  editor/editor/system/mcp, mcp_manager or editor_data/data/mcp.
---

# Editor MCP

The editor's MCP server (`editor/editor/system/mcp_manager.cpp`), its tools
(`editor/editor/system/mcp/`) and the guides it serves (`editor_data/data/mcp/skills/`), plus
running an editor of your own to check engine work. How to use the tools - scenes, components,
assets, materials, scripts, captures, play mode - is owned by those guides, which reach every
MCP client; this skill holds what an engine developer needs beside them.

## Gotchas

### Running an editor

- Other sessions run editors too, from this checkout and from others. Start your own editor
  on a port of your own (`--mcp-port`, never the default 27182), check that `/health` on that
  port fails before the start and answers after it, and never send a call to an editor you did
  not start. `scripts/editor_ctl.py` does these checks.
- The project passed with `-p` must exist: with a path that does not, the editor never
  answers on its port.
- Never start an editor from a `bin` folder whose editor still runs: both write the same
  shader cache and every reading from either is garbage. List `unravel-editor.exe` processes
  by executable path before starting ([drive-editor.md](drive-editor.md)).
- Start `unravel-editor.exe` (the editor target's output name). An `UnravelEditor.exe` in a
  `bin` folder is an out-of-date build that rejects `--mcp-port`.
- A running editor holds its executable and `bin/clrpp`: stop it before building that tree,
  or the link or the `clrpp_managed` copy fails. After a build, check that the exe's
  modification time is newer than your change before trusting a live run.
- Stop the editor you started when its checks are done, by the pid you recorded. A kill by
  process name can end the user's editor.
- Start without `--novsync` unless you measure GPU time: an uncapped editor takes the CPU and
  GPU from builds and suites running beside it (`editor_set_frame_pacing` changes it live).
- Opening a project writes `<project>/.agents/UNRAVEL.md` and adds `<project>/AGENTS.md` when
  missing (`project_manager.cpp`): a sample project's git status shows them after a run.
- A main-thread tool waits up to 15 s for the main thread to take it; "The main thread did not
  take the call" means it was not run (the editor was busy, a first start compiling shaders),
  so the same call is safe to send again.

### Logs and waiting

- Ask the editor for state - `editor_get_status`, the action's own result, `editor_wait_for` -
  instead of waiting for a line in `bin/Log.txt`. The logger flushes the file only when an
  error is logged (`flush_on` in `engine/core/logging/logging.cpp`), so a running editor's
  info lines can be missing from it for minutes, and a kill by pid loses them.
- `bin/Log.txt` is reliable for errors (each `[error]` / `[critical]` line flushes the file).
  The next program started from that `bin` folder rewrites it, `unravel-tests` included: read
  it after stopping the editor and before the next start.
- `logs_get_recent` reads the last 4096 lines logged, whatever the console cleared (play,
  recompile). A start that logs more than that pushes its first errors out: grep `Log.txt`
  for those.
- When the task is "errors in the log", fix every warning and error of that subsystem,
  including the ones that were there before, and check a whole run: project open, play, stop.

### Scenes and play mode

- After `scene_save`, a `scene_open` of the same key can load the content from before the
  save. Compare the `entity_count` it returns with what you saved and reopen when they differ;
  saving the stale state overwrites the good file.
- A script recompile and the end of play save every loaded scene (`scene::get_all_scenes`),
  the editor's private ones included (Material Graph preview, inspector previews), load them
  back into new entities and clear the undo history (`editing_manager::on_script_recompile`,
  `on_play_after_end`). Entity ids survive; an `entt::handle` kept across does not, so code
  that keeps one checks it on each use and rebuilds.
- Read the ids and poses a play test needs before `play_set_active`, and follow the
  `verification` guide's stop rule (wait for the `inactive` play phase, decide toggles from
  `editor_get_status`): a play toggle while the scenes still reload can take the editor down,
  so a test script never toggles play from its error handler.
- Scene file names repeat across projects. Before measuring a named scene, check the project
  path, the scene key and the entity count in `editor_get_status`, and take one capture.

### Captures and measurements

- MCP captures show only the Scene and Game views. A panel, an inspector or a popup needs a
  capture of the editor window ([drive-editor.md](drive-editor.md)).
- Two captures are many rendered frames apart, so an effect a temporal filter settles within
  a few frames has converged between them. Measure frame-to-frame stability with
  `viewport_measure_temporal` (developer profile) or a debug view of the filter's state, not
  by differencing captures.
- Keep capture sequences 150 ms or more apart; faster bursts time out waiting for the GPU
  readback.
- Before attributing a reading at a pixel to distance or a setting, capture the `normals`
  debug view at every pose: a recess, step or pedestal at the target puts the far pixel on
  another surface.

### Writing tools

- A tool calls the function the editor UI uses for the same action (`editor_actions`,
  `project_manager`, panels, inspectors, the actions in `editor/editor/editing/actions/`).
  When that code is inline in an ImGui handler, extract it into a function both call; when it
  lacks a result, an accessor or a check the tool needs, add it there so the UI gains it too.
  A second copy of logic or state (jobs, flags, validation) in the MCP layer is a bug.
- UI entry points that prompt (`editor_actions::open_scene`, `new_scene`) are not for tools:
  call the non-modal function under the prompt (`load_scene_from_asset`,
  `new_scene_from_preset`) after `release_unsaved_work`, which goes after every other check.
- Entity and component edits go through the editor actions, so undo, the unsaved flag and
  prefab overrides follow; component properties record overrides through
  `prefab_override_context::mark_changed_properties`.
- Reflected objects are read and written through `mcp_meta_properties.h` (reflected setters,
  ranges, enum names, tooltips); no hand-written tables of names or ranges.
- The input schema is the contract: `check_arguments` refuses wrong types, enum values,
  ranges and missing required arguments before the handler runs and turns undeclared ones
  into warnings, so declare every argument. No `anyOf` / `oneOf` / `allOf` / `not` at the top
  of a schema: clients that read schemas as the Claude API does refuse the whole tool list,
  and the registry logs it as an error.
- A renamed tool keeps its old name in `former_names`, a renamed argument its old path in
  `former_arguments` (`items[].index`); calls by them keep working with a warning.
- Use the argument names other tools use: `entity_id` / `entity_ids`, `key` (an asset key, a
  path relative to app:/data or a uid), `type_name` for a C# type, `items` for a batch,
  `wait_ms`, `space`, `force`, `save`.
- Set the flags: `mutates_scene` (with `requires_main_thread`, the call runs in an undo scope
  and is one undo step), `requires_main_thread = false` for action-and-wait tools (they hop to
  the main thread for the action and wait on the HTTP worker), `writes_project_files` (refused
  while a deploy runs), `effect` (`read_only`, `destructive`), `developer_only` for engine
  diagnostics.
- Scene-editing tools start with `require_edit_scene` (refuses play). A tool that may edit the
  running game uses `require_live_scene`, adds `temporary_field` to its result and
  `k_play_edit_note` to its description.
- Write locations come through `to_project_data_key` / `to_project_data_file_key` (app:/data
  only); an edit of an existing asset checks `require_project_data_asset`; files are written
  with `asset_writer::atomic_save_to_file` or `atomic_write_file`.
- A batch checks every item before changing anything: one rejected item rejects the call and
  nothing changes, worded by `make_batch_item_error` ("Item 2: <reason>. Nothing changed.").
  An error is one sentence that says what to do instead. Several edits of one call that must be
  one undo step share a `sequence_action_t` (skill `unravel-editor-panel`).
- A new or renamed tool is named in the shipped guide of its area, in that guide's voice
  (full sentences, ASCII, GI rather than another engine's feature names). A tool no guide
  mentions is one clients do not find.

## How it works

- `mcp_manager` serves `127.0.0.1:<port>` (default `k_default_port` 27182, `--mcp-port`):
  `GET /health` and `POST /mcp` with JSON-RPC 2.0 (`Content-Type: application/json`, loopback
  only). It keeps no session: `tools/call` works without `initialize`. `initialize` returns the
  server instructions (`k_server_instructions` in `mcp_tools_skills.cpp`).
- A `tools/call` is looked up by name or former name, refused when the tool is
  `developer_only` on the user profile, checked against its schema, then run on the main
  thread (15 s to be taken) or on the HTTP worker. A handler that throws becomes an error
  result saying what it changed stays changed.
- A result is `{"content":[...],"isError":...}`: the tool's text (JSON for most tools, with
  `warnings` joined into it), then an image block for captures.
- Profiles: Debug and RelWithDebInfo editors default to `developer`, Release to `user`
  (`UNRAVEL_DEVELOPER_BUILD`); `--mcp-profile` overrides. `editor_get_status` reports it.
- Each `mcp_tools_<area>.cpp` has a `register_<area>_tools`, declared in
  `mcp_tool_registry.h` and called in `mcp_manager::init`.
- Guides: `mcp_tools_skills.cpp` reads `editor:/data/mcp/skills/*.md` on every call (the copy
  `editor_data` makes in `bin/data/editor/`), serves them through `skills_list` /
  `skills_get` and as resources `unravel://skills/<name>`. Frontmatter: `name`, `title`,
  `description`, `order` (sort key), `include` (a file appended to the body, as `scripting-api`
  appends `editor_data/data/project/UNRAVEL.template`).
- The MCP Server window (`panel_focus {"panel":"mcp"}`) lists every call and refusal.

## Procedures

- Start, call and stop an editor with `scripts/editor_ctl.py` (start / wait / call / stop on
  your own port; it never stops an editor it did not start); capture the editor window by
  hand: [drive-editor.md](drive-editor.md).
- Add or change a tool:
  1. Find the UI's code path for the action; extract or extend it (Gotchas, Writing tools).
  2. Register the tool in its area's `mcp_tools_<area>.cpp` with schema, flags and a
     description that says what it does, what it refuses and what it returns. A new file
     adds its `register_<area>_tools` to `mcp_tool_registry.h` and `mcp_manager::init`.
  3. Name the tool in the shipped guide of its area.
  4. Verify (below).
- Change a shipped guide: keep its frontmatter lines and voice; one fact in one guide (name
  the other guide instead of repeating it); check every tool and argument name against the
  registrations. Then `cmake --build build/<Config> --target editor_data`: a running editor
  serves the new text at the next `skills_get`, no restart.

## Verify

- Build `editor` in both configured trees: the tool files define many helpers in anonymous
  namespaces, which collide only in the RelWithDebInfo unity batches.
- Logic added to a shared editor function is tested in that area's `unravel-tests` suite
  (`settings profiles`, `deploy library`, ...); there is no MCP suite, the tool is checked live.
- On your own editor: `tools/list` shows the tool on its profile; a valid call does what the
  UI does; `edit_undo` takes it back in one step; a wrong argument is refused with nothing
  changed; play mode is refused or reported `temporary`; a former name answers with a warning.
- After guide edits: `skills_list` shows each guide's title and description.
- Stop the editor.

## Entry points

| File | What |
|------|------|
| `editor/editor/system/mcp_manager.cpp` | HTTP server, JSON-RPC dispatch, argument check, main-thread hop, undo scope |
| `editor/editor/system/mcp/mcp_tool_registry.h` | `mcp_tool` fields, profiles, `check_arguments`, register functions |
| `editor/editor/system/mcp/mcp_tools_common.h` | argument readers, scene guards, play-mode note, transforms |
| `editor/editor/system/mcp/mcp_project_paths.h` | where tools may read and write |
| `editor/editor/system/mcp/mcp_meta_properties.h` | reflection bridge for typed properties |
| `editor/editor/system/mcp/mcp_component_utils.h` | component property reads and writes |
| `editor/editor/system/mcp/mcp_tools_skills.cpp` | guide loader, server instructions, skill resources |
| `editor/editor/editing/editor_actions.h` | the editor functions tools call |
| `editor/editor/hub/panels/inspector_panel/inspectors/inspectors.h` | `prefab_override_context` |
| `editor/editor/hub/panels/mcp_panel/mcp_panel.cpp` | MCP Server window, activity log |

## Shipped guides

Read them in `editor_data/data/mcp/skills/` or fetch them from a running editor with
`skills_get {"names":[...]}`: `getting-started` (bootstrap, hard rules, projects, deploy),
`finding-and-inspecting`, `scene-building` (coordinates, WORLD vs LOCAL, prefabs),
`components`, `assets` (imports, shared materials vs instances), `material-graphs`,
`scripting`, `scripting-api`, `animation`, `gameplay`, `ui`, `verification` (captures,
debug views, play-mode testing, input, logs, profiling).
