---
name: unravel-entities
description: >-
  Creates and edits UnravelEngine scene entities via Editor MCP: hierarchy,
  coordinate system (X-right Y-up Z-forward), WORLD vs LOCAL transforms,
  components, ScriptComponent source, and scene_save. Use when spawning
  primitives, parenting, placing children, attaching C# scripts, or persisting
  .spfb scenes.
---

# Unravel Entities & Scripts (MCP)

## Coordinate system

| Axis | Direction |
|------|-----------|
| **X** | Right |
| **Y** | Up |
| **Z** | Forward |

Right-handed. `rotation_euler` is degrees as `[pitch_x, yaw_y, roll_z]`.

**Primitives:** embedded `Cube` is **1x1x1**, origin at **center**. Non-uniform `scale` is size in world units. When assembling props (benches, signs), place parts in **local** space: seat up on **+Y**, width on **+/-X**, depth / facing along **+/-Z** (backrest typically **-Z** if the seat faces **+Z**).

## WORLD vs LOCAL (critical)

| API | Space |
|-----|--------|
| `scene_create_*` / `*_batch` create `position` | **WORLD** (global), even with `parent_id` (unless item sets `space:"local"`) |
| `scene_set_transforms_batch` default `space:"world"` | **WORLD** |
| `scene_set_transforms_batch` `space:"local"` | **LOCAL** (parent-relative) |
| `scene_get_entities_batch` / pose JSON `position`, `rotation_euler`, `scale` | **WORLD** |
| Same JSON `position_local`, `rotation_euler_local`, `scale_local` | **LOCAL** |
| `scene_get_transforms_batch` | per-item `space:"world"\|"local"` |

**Rule:** Parent a child, then set pose with `space:"local"`. Never pass local offsets to world setters - parts collapse to the origin.

When the parent is rotated, always set the child's `rotation_euler:[0,0,0]` in local space together with position/scale. Omitting rotation can leave world identity, which becomes a compensating local rotation and cancels the parent (parts stick out sideways).

```json
// Child door under a house at world (-30,0,19)
{"entity_id":"<door>","space":"local","position":[0,0.85,1.6],"scale":[0.7,1.7,0.12]}
```

## Entity tools

Browse lean, then drill in:

| Tool | Purpose |
|------|---------|
| `editor_get_status` | Bootstrap: project + scene + play in one call (prefer over separate info tools) |
| `scene_get_hierarchy_batch` | Lean tree (`id`/`name`/`children`); optional `parent_id`, `max_depth`, `limit` |
| `scene_get_entities_batch` | `detail`: `pose` (default), `summary` (includes component names), `components` (+ typed `component_properties`; optional `components[]` filter) |
| `scene_get_children_batch` | Immediate children (`id`/`name`) for many entities |
| `scene_list_component_properties` | Schema for MCP-editable typed keys (optional `component` filter) |
| `scene_get_component_properties_batch` | Typed get: `{entity_id, component, script_type?, properties?[]}` |
| `scene_set_component_properties_batch` | Typed set via real setters (one undo). Supported: Light, Skylight, Audio Source, Camera, Volume, Script, Particle Emitter, Physics, Animation, Text, Reflection Probe, Bloom |
| `scene_find_entities_batch` | Find by `name_contains` / `name_exact` / `component_type` / `script_type` (AND); play-safe |
| `scene_get_transforms_batch` | Many entity poses (`items` with `entity_id`, optional `space`) |
| `scene_set_transforms_batch` | Many poses in one undoable action |
| `scene_set_parents_batch` | Reparent / unparent many (keeps world pose) |
| `scene_create_entities_batch` / `scene_create_primitives_batch` / `scene_create_meshes_batch` / `scene_create_from_prefab_batch` | Spawn batches (`items` array); returns lean `{id,name,parent_id}` |
| `scene_create_light` / `scene_create_camera` | Single light/camera spawn |
| `scene_duplicate_entities_batch` | Clone hierarchies (undoable) |
| `scene_get_bounds_batch` | World AABB for one or many entities (play-safe) |
| `scene_add_components_batch` / `scene_remove_components_batch` | Engine components only |
| `scene_list_component_types` | Addable engine types |
| `scene_delete_entities_batch` | Delete by ids |
| `scene_set_names_batch` / `scene_set_active_batch` | Meta batches |
| `scene_set_model_materials_batch` | Assign materials to many model slots (one undo) |
| `scene_save` | Atomic `.spfb` write (`key`/`path` or overwrite `source`) |
| `scene_open` / `scene_new_from_preset` / `scene_list_presets` | See `unravel-projects` |
| `selection_get` / `selection_set_batch` / `selection_clear` | Editor entity selection |
| `edit_undo` / `edit_redo` | Undo stack (Ctrl+Z / Ctrl+Y parity) |
| `play_get_state` / `play_set_active` / `play_set_paused` / `play_skip_frame` | Play mode control |
| `logs_get_recent` | Console log tail (`min_level`, `max_count`, `after_id`) |
| `panel_focus_scene` / `panel_focus_game` | Focus Scene or Game ImGui panel tabs |

Component names: use `scene_get_entities_batch` with `detail:"summary"`. Patch fields via typed property tools (`scene_list_component_properties` -> get -> set); do not round-trip ser20 JSON. Hierarchy parent/children stay on transform/parent tools. Component must already exist before set.

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
| Typed component properties | `editor/editor/system/mcp/mcp_component_utils.*` |
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

1. Start with `editor_get_status`, then `scene_get_hierarchy_batch` / `scene_find_entities_batch`.
2. Create at world positions; compose hierarchies with `space:"local"`.
3. Prefer `scene_create_primitives_batch` / `scene_set_transforms_batch` for procedural builds.
4. Inspect with `detail:"summary"` or typed `scene_get_component_properties_batch`; mutate with `scene_set_component_properties_batch` (not ser20 blobs / not source rewrites for tuning).
5. After large procedural builds, call `scene_save` with an `app:/data/...spfb` key.
6. Prefer `scripts_list_types` -> `scene_add_scripts_batch` over guessing type names.
7. Edit C# with `scripts_set_sources_batch`; wait for recompile before `scene_add_scripts_batch` on new types.
