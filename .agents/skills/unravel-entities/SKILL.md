---
name: unravel-entities
description: >-
  Creates and edits UnravelEngine scene entities via Editor MCP: hierarchy,
  coordinate system (X-right Y-up Z-forward), WORLD vs LOCAL transforms,
  components, ScriptComponent source, and scene_save. Use when spawning
  primitives, parenting, placing children, attaching C# scripts, or persisting
  .spfb scenes.
disable-model-invocation: true
---

# Unravel Entities & Scripts (MCP)

## Coordinate system

| Axis | Direction |
|------|-----------|
| **X** | Right |
| **Y** | Up |
| **Z** | Forward |

Right-handed. `rotation_euler` is degrees as `[pitch_x, yaw_y, roll_z]`.

**Primitives:** embedded `Cube` is **1×1×1**, origin at **center**. Non-uniform `scale` is size in world units. When assembling props (benches, signs), place parts in **local** space: seat up on **+Y**, width on **±X**, depth / facing along **±Z** (backrest typically **−Z** if the seat faces **+Z**).

## WORLD vs LOCAL (critical)

| API | Space |
|-----|--------|
| `scene_create_*` `position` | **WORLD** (global), even with `parent_id` |
| `scene_set_transform` default `space:"world"` | **WORLD** |
| `scene_set_transform` `space:"local"` | **LOCAL** (parent-relative) |
| `scene_list_entities` / entity JSON `position`, `rotation_euler`, `scale` | **WORLD** |
| Same JSON `position_local`, `rotation_euler_local`, `scale_local` | **LOCAL** |
| `scene_get_transform` | `space:"world"\|"local"`, or omit for both |

**Rule:** Parent a child, then set pose with `space:"local"`. Never pass local offsets to world setters — parts collapse to the origin.

When the parent is rotated, always set the child’s `rotation_euler:[0,0,0]` in local space together with position/scale. Omitting rotation can leave world identity, which becomes a compensating local rotation and cancels the parent (parts stick out sideways).

```json
// Child door under a house at world (-30,0,19)
{"entity_id":"<door>","space":"local","position":[0,0.85,1.6],"scale":[0.7,1.7,0.12]}
```

## Entity tools

| Tool | Purpose |
|------|---------|
| `scene_list_entities` | Hierarchy; world + local transforms |
| `scene_get_transform` | One entity pose |
| `scene_set_transform` | Set pose; `space` world\|local |
| `scene_set_parent` | Reparent / unparent (keeps world pose) |
| `scene_create_entity` / `_primitive` / `_mesh` / `_light` / `_camera` / `_from_prefab` | Spawn (`_mesh` = asset key e.g. `.fbx`) |
| `scene_add_component` / `scene_remove_component` | Engine components only |
| `scene_list_component_types` | Addable engine types |
| `scene_delete_entities` | Delete by ids |
| `scene_set_name` / `scene_set_active` | Meta |
| `scene_save` | Atomic `.spfb` write (`key`/`path` or overwrite `source`) |
| `scene_open` / `scene_new_from_preset` / `scene_list_presets` | See `unravel-projects` |
| `scene_inspect_entity` | Summary + optional `components_serialized`; omit `entity_id` for active selection |
| `selection_get` / `selection_set` / `selection_clear` | Editor entity selection |
| `play_get_state` / `play_set_active` / `play_set_paused` / `play_skip_frame` | Play mode control |
| `logs_get_recent` | Console log tail (`min_level`, `max_count`, `after_id`) |
| `panel_focus_scene` / `panel_focus_game` | Focus Scene or Game ImGui panel tabs |

## Script tools

`scene_add_component` **cannot** add `ScriptComponent`. Use:

| Tool | Purpose |
|------|---------|
| `scripts_list_types` | Addable C# type full names |
| `scene_add_script` / `scene_remove_script` | Attach/detach by `type_name` |
| `scene_list_scripts` | Types + `source_path` on an entity |
| `scripts_get_source` | Read `.cs` (`path`/`key` or `entity_id`+`type_name`) |
| `scripts_set_source` | Atomic write `.cs`; `recompile` default true |
| `scripts_create` | New file from `TemplateComponent.cs.in` under `folder` |

File writes use `asset_writer::atomic_write_file`. Scene saves use `asset_writer::atomic_save_to_file`.

## Code touch points

| Purpose | Path |
|---------|------|
| Scene/entity MCP | `editor/editor/system/mcp/mcp_tools_scene.cpp` |
| Play/selection/logs MCP | `editor/editor/system/mcp/mcp_tools_editor.cpp` |
| Editor facades | `editor/editor/editing/editor_actions.*`, `entity_inspect.*` |
| Script MCP | `editor/editor/system/mcp/mcp_tools_scripts.cpp` |
| Shared helpers | `editor/editor/system/mcp/mcp_tools_common.h` |
| ScriptComponent | `engine/engine/scripting/ecs/components/script_component.h` |
| Add/remove script actions | `editor/editor/editing/actions/entity_actions.*` |
| Atomic I/O | `engine/engine/assets/impl/asset_writer.h` |

## Rules of thumb

1. Create at world positions; compose hierarchies with `space:"local"`.
2. After large procedural builds, call `scene_save` with an `app:/data/...spfb` key.
3. Prefer `scripts_list_types` → `scene_add_script` over guessing type names.
4. Edit C# with `scripts_set_source`; wait for recompile before `scene_add_script` on new types.
