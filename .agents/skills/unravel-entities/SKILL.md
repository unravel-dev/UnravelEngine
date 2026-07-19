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
| `scene_create_*` / `*_batch` create `position` | **WORLD** (global), even with `parent_id` (unless item sets `space:"local"`) |
| `scene_set_transforms_batch` default `space:"world"` | **WORLD** |
| `scene_set_transforms_batch` `space:"local"` | **LOCAL** (parent-relative) |
| `scene_list_entities_batch` / entity JSON `position`, `rotation_euler`, `scale` | **WORLD** |
| Same JSON `position_local`, `rotation_euler_local`, `scale_local` | **LOCAL** |
| `scene_get_transforms_batch` | per-item `space:"world"\|"local"` |

**Rule:** Parent a child, then set pose with `space:"local"`. Never pass local offsets to world setters — parts collapse to the origin.

When the parent is rotated, always set the child’s `rotation_euler:[0,0,0]` in local space together with position/scale. Omitting rotation can leave world identity, which becomes a compensating local rotation and cancels the parent (parts stick out sideways).

```json
// Child door under a house at world (-30,0,19)
{"entity_id":"<door>","space":"local","position":[0,0.85,1.6],"scale":[0.7,1.7,0.12]}
```

## Entity tools

| Tool | Purpose |
|------|---------|
| `scene_list_entities_batch` | Hierarchy; world + local transforms |
| `scene_find_entities_batch` | Find by `name_contains` / `name_exact` (optional `parent_id`) |
| `scene_get_transforms_batch` | Many entity poses (`items` with `entity_id`, optional `space`) |
| `scene_set_transforms_batch` | Many poses in one undoable action |
| `scene_set_parents_batch` | Reparent / unparent many (keeps world pose) |
| `scene_create_entities_batch` / `scene_create_primitives_batch` / `scene_create_meshes_batch` / `scene_create_from_prefab_batch` | Spawn batches (`items` array) |
| `scene_create_light` / `scene_create_camera` | Single light/camera spawn |
| `scene_duplicate_entities_batch` | Clone hierarchies (undoable) |
| `scene_get_bounds_batch` | World AABB for one or many entities |
| `scene_add_components_batch` / `scene_remove_components_batch` | Engine components only |
| `scene_list_component_types` | Addable engine types |
| `scene_delete_entities_batch` | Delete by ids |
| `scene_set_names_batch` / `scene_set_active_batch` | Meta batches |
| `scene_set_model_materials_batch` | Assign materials to many model slots (one undo) |
| `scene_save` | Atomic `.spfb` write (`key`/`path` or overwrite `source`) |
| `scene_open` / `scene_new_from_preset` / `scene_list_presets` | See `unravel-projects` |
| `scene_inspect_entities_batch` | Summary + optional `components_serialized` for many ids |
| `selection_get` / `selection_set_batch` / `selection_clear` | Editor entity selection |
| `edit_undo` / `edit_redo` | Undo stack (Ctrl+Z / Ctrl+Y parity) |
| `play_get_state` / `play_set_active` / `play_set_paused` / `play_skip_frame` | Play mode control |
| `logs_get_recent` | Console log tail (`min_level`, `max_count`, `after_id`) |
| `panel_focus_scene` / `panel_focus_game` | Focus Scene or Game ImGui panel tabs |

## Script tools

`scene_add_components_batch` **cannot** add `ScriptComponent`. Use:

| Tool | Purpose |
|------|---------|
| `scripts_list_types` | Addable C# type full names |
| `scene_add_scripts_batch` / `scene_remove_scripts_batch` | Attach/detach by `type_name` |
| `scene_list_scripts_batch` | Types + `source_path` for many `entity_ids` |
| `scripts_get_source` | Read `.cs` (`path`/`key` or `entity_id`+`type_name`) |
| `scripts_set_sources_batch` | Atomic write many `.cs`; `recompile` default true |
| `scripts_create_batch` | New files from `TemplateComponent.cs.in` under `folder` |

File writes use `asset_writer::atomic_write_file`. Scene saves use `asset_writer::atomic_save_to_file`.

## Code touch points

| Purpose | Path |
|---------|------|
| Scene/entity MCP | `editor/editor/system/mcp/mcp_tools_scene.cpp` |
| Batch/find/bounds MCP | `editor/editor/system/mcp/mcp_tools_scene_batch.cpp` |
| Ops batch MCP | `editor/editor/system/mcp/mcp_tools_ops_batch.cpp` |
| Play/selection/logs/undo MCP | `editor/editor/system/mcp/mcp_tools_editor.cpp` |
| Editor facades | `editor/editor/editing/editor_actions.*`, `entity_inspect.*` |
| Script MCP | `editor/editor/system/mcp/mcp_tools_scripts.cpp` |
| Shared helpers | `editor/editor/system/mcp/mcp_tools_common.h` |
| ScriptComponent | `engine/engine/scripting/ecs/components/script_component.h` |
| Add/remove script actions | `editor/editor/editing/actions/entity_actions.*` |
| Atomic I/O | `engine/engine/assets/impl/asset_writer.h` |

## Rules of thumb

1. Create at world positions; compose hierarchies with `space:"local"`.
2. Prefer `scene_create_primitives_batch` / `scene_set_transforms_batch` for procedural builds.
3. After large procedural builds, call `scene_save` with an `app:/data/...spfb` key.
4. Prefer `scripts_list_types` → `scene_add_scripts_batch` over guessing type names.
5. Edit C# with `scripts_set_sources_batch`; wait for recompile before `scene_add_scripts_batch` on new types.
