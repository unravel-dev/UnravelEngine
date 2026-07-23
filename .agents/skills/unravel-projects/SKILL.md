---
name: unravel-projects
description: >-
  Opens/closes UnravelEngine editor projects, lists recent projects, and opens
  or creates scenes from defaults::scene_preset via Editor MCP. Use when
  switching projects, loading .spfb scenes, or bootstrapping a new preset scene.
disable-model-invocation: true
---

# Unravel Projects & Scenes (MCP)

## When to use

| Goal | Tool |
|------|------|
| See recent project folders | `project_list_recent` |
| Open a project | `project_open` |
| Close current project | `project_close` |
| Current project info | `project_get_info` |
| Open an existing `.spfb` | `scene_open` |
| Save active scene (atomic) | `scene_save` |
| New scene from quality preset | `scene_new_from_preset` |
| List preset names | `scene_list_presets` |
| Entity spawn / transforms / scripts | see `unravel-entities` |

All mutating tools refuse play mode. Scene/project switches use `force:true` by default (discard unsaved) — no ImGui save/create modals.

## Project tools

```json
// Most recent
{"recent": true}

// By index from project_list_recent
{"recent_index": 0}

// Absolute directory
{"path": "C:/Games/MyProject"}

// Optional when open would create a new scene (no last/startup scene):
{"path": "...", "preset": "medium"}
```

`project_open` calls `project_manager::open_project` on the main thread (long timeout). If the editor would show the create-scene modal, MCP completes it with `preset` (`low`|`medium`|`high`|`showcase`).

Recent list lives in `editor.cfg` → `projects.recent_projects` (see `project_manager`).

## Scene tools

Presets map to `defaults::scene_preset` in `engine/engine/defaults/defaults.h`:

| Preset | Intent |
|--------|--------|
| `low` | Minimal FX / cheaper shadows |
| `medium` | Standard (UI default) |
| `high` | Richer volume FX |
| `showcase` | Max quality demo setup |

```json
// Open by asset key
{"key": "app:/data/Levels/Main.spfb"}

// Save / save-as (atomic_save_to_file)
{"key": "app:/data/Village.spfb"}

// New scene
{"preset": "high", "force": true}
```

Open/new/save require an open project. Paths may be absolute; keys are normalized to `.spfb`.
`scene_save` without `key`/`path` overwrites `scene.source` (error if unsaved new scene).

## Code touch points

| Purpose | Path |
|---------|------|
| Open/close/recent | `editor/editor/system/project_manager.*` |
| Scene open (UI) | `editor/editor/editing/editor_actions.cpp` |
| Create-scene modal (headless complete) | `editor/editor/editing/create_scene_modal.*` |
| Preset builders | `engine/engine/defaults/defaults.cpp` |
| MCP project tools | `editor/editor/system/mcp/mcp_tools_projects.cpp` |
| MCP scene open/new | `editor/editor/system/mcp/mcp_tools_scene.cpp` |

## Rules of thumb

1. Prefer `project_list_recent` → `project_open` with `recent_index` over guessing paths.
2. After open, use `scene_get_info` / `scene_get_hierarchy_batch` before spawning content.
3. Do not call UI `editor_actions::new_scene` / `open_scene` from automation — they show ImGui modals.
4. Asset work under `app:/` needs an open project; `engine:/` / `editor:/` do not.
