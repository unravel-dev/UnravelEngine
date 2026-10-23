---
name: unravel-materials
description: >-
  Creates and edits UnravelEngine PBR materials via Editor MCP tools or code.
  Use when changing .mat assets, model material slots, runtime material instances,
  or deciding asset vs instance edit paths.
---

# Unravel Materials (MCP)

## Asset vs instance

| Path | When | Persist |
|------|------|---------|
| **Material asset** (`.mat`) | Shared look used by many meshes; Content Browser / inspector material | Disk via `atomic_save_to_file` |
| **Model material slot** | Assign which asset a `model_component` uses | Scene/prefab only |
| **Material instance** | One entity's overrides of a slot's shared material; values not overridden follow it | Scene/prefab only - **never** writes `.mat` |

Create parity: content browser `get_asset_from_instance<material>(key, pbr_material)` + save.  
Save parity: inspector `inspector_asset_handle_material` on `edit_finished`.

## MCP tools

| Tool | Purpose |
|------|---------|
| `materials_create_batch` | New `.mat` files (`items`: `path` or `folder`+`name`) |
| `materials_get_batch` | Read props for many (`items`: `key` / `uid`) |
| `materials_set` | Mutate asset; `save` default `true`; optional `wait_ms` |
| `materials_list_properties` | Supported property schema |
| `scene_set_model_materials_batch` | Shared slot assignments (one undo) |
| `scene_set_model_material_instances_batch` | Override instance values (reports `shares_draw`); no `.mat` |
| `scene_clear_model_material_instances_batch` | Revert every override -> draws like the shared material |
| `assets_find_batch` | Find materials (and all other asset types) by `type`/`prefix`/`name_contains` |
| `window_request_focus` | Focus/raise editor OS window so asset watcher can import new `.mat` files |

When the editor is unfocused, filesystem watcher events may not run - call `window_request_focus` before `materials_create_batch` / `assets_wait_ready_batch` if creates do not land.

Single-property edits: one-key `properties` object. Bulk: multiple keys in one call.

## Example bulk asset set

```json
{
  "key": "app:/data/Materials/RobotBody.mat",
  "save": true,
  "properties": {
    "base_color": [0.8, 0.2, 0.1, 1.0],
    "roughness": 0.45,
    "metalness": 0.1,
    "color_map": "app:/data/Textures/albedo.etex"
  }
}
```

## Code touch points

| Purpose | Path |
|---------|------|
| PBR type / setters | `engine/engine/rendering/material.h` |
| Meta names | `engine/engine/meta/rendering/standard_material.cpp` |
| Model slots / instances | `engine/engine/rendering/model.h`, `engine/engine/rendering/material_instance.h` |
| MCP helpers | `editor/editor/system/mcp/mcp_material_utils.*` |
| MCP tools | `editor/editor/system/mcp/mcp_tools_materials.cpp` |
| Inspector save | `editor/editor/hub/panels/inspector_panel/inspectors/inspector_assets.cpp` |
| Content Browser create | `editor/editor/hub/panels/content_browser_panel/content_browser_panel.cpp` |

## Rules of thumb

1. Need a reusable material -> `materials_create_batch` / `materials_set` (save).
2. Only this entity should look different -> `scene_set_model_material_instances_batch`.
3. Point entities at existing `.mat` -> `scene_set_model_materials_batch`.
4. Play mode blocks scene mutators (`require_edit_scene`).

## Per-instance values and batching

Properties registered with the `per_instance` attribute (`standard_material.cpp`: base, subsurface and
emissive colors, emissive intensity, roughness, metalness, bumpiness) can differ per entity and still share
one instanced draw (`material_instance::shares_draw`, `instance_material_vertex_data`). Overriding any other
property (textures, tiling, alpha / cull mode) gives the entity a draw of its own. GI recaptures caused by
per-instance values are limited (`gi_surface_cache::gate_instance_values`).
