---
name: getting-started
title: Getting started + hard rules
description: Session bootstrap, project/scene management, response conventions, undo, selection and focus etiquette, and the hard rules every MCP client must follow (never hand-edit serialized files, add components before typed sets, play-mode restrictions). Read first.
order: 10
---
# Getting started with the Unravel editor MCP

## Hard rules (read first)

1. NEVER create or edit scene (.spfb), prefab (.pfb) or material (.mat) files
   directly on disk. They are serialized engine state: hand-authored payloads
   silently fall back to defaults on load or corrupt runtime state. Always go
   through the MCP tools, which run the editor's real setters and validation.
2. If a workflow seems impossible with the available tools, say so instead of
   working around the tools through the filesystem.
3. Mutating tools refuse play mode. Stop play (play_set_active {"active":false})
   before editing, or make edits first and then enter play mode.
4. Components must exist before typed property writes: add engine components
   with scene_add_components_batch, C# scripts with scene_add_scripts_batch
   (Script can NOT be added through scene_add_components_batch), then configure
   with scene_set_component_properties_batch.

## Session bootstrap

1. editor_get_status - project, scene and play state in one call. Always start
   here; it replaces separate project/scene/play queries.
2. No project open? project_list_recent then project_open with recent_index
   (preferred over guessing paths). project_get_info / project_close manage
   the current one.
3. Open a scene with scene_open (key like "app:/data/MyScene.spfb"), or create
   one with scene_new_from_preset (names from scene_list_presets: low | medium
   | high | showcase - they seed camera, sun + sky, a global reflection probe
   and a global post volume).
4. Persist with scene_save {"key":"app:/data/MyScene.spfb"}; without a key it
   overwrites the scene's current source (errors on an unsaved new scene).

## Conventions

- Create-tool responses list new entities under "created" (id, name,
  parent_id). Batch setters return {"ok":true,"count":N} on success and
  per-item errors only on failure.
- Entity ids are stable uuids that survive save/reopen - cache them instead of
  re-finding by name.
- Asset keys: "app:/..." project data, "engine:/..." engine data.
- Units: meters, seconds, degrees. Colors are RGBA 0..1 arrays.
- One batch call = one undo step (edit_undo / edit_redo mirror Ctrl+Z/Y).

## Working alongside the user

- The user sees the same editor. selection_get shows what they have selected -
  useful context for "this/the selected one" requests. When you finish a task,
  selection_set_batch on the result and viewport_focus_entities_batch point
  the user at what you built; selection_clear empties the selection.
- window_request_focus raises the editor window; the filesystem watcher
  processes asset changes faster with focus. panel_focus_scene /
  panel_focus_game switch the center panel.
- After imports, loads and play sessions check logs_get_recent (min_level
  "warning") - compile problems and runtime errors land there.

## Do

- Prefer batch tools; group related edits into one call for clean undo.
- Give entities meaningful names at creation - scene_find_entities_batch works
  by name and future sessions depend on it.
- Fetch the other skills (skills_list / skills_get) before scene building,
  scripting, asset or verification work.

## Do not

- Do not bypass tools with filesystem writes into the project.
- Do not churn the user's selection mid-build; set it once at the end.
- Do not assume a tool failed silently: every tool returns isError with a
  reason - read it and adapt instead of retrying the identical call.
