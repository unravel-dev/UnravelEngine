---
name: unravel-materials
description: >-
  Creates and edits UnravelEngine PBR materials via Editor MCP tools or code.
  Use when changing .mat assets, model material slots, runtime material instances,
  or deciding asset vs instance edit paths.
disable-model-invocation: true
---

# Unravel Materials (MCP)

## Asset vs instance

| Path | When | Persist |
|------|------|---------|
| **Material asset** (`.mat`) | Shared look used by many meshes; Content Browser / inspector material | Disk via `atomic_save_to_file` |
| **Model material slot** | Assign which asset a `model_component` uses | Scene/prefab only |
| **Material instance** | One entity’s override (prototype/tweak) | Scene/prefab only — **never** writes `.mat` |

Create parity: content browser `get_asset_from_instance<material>(key, pbr_material)` + save.  
Save parity: inspector `inspector_asset_handle_material` on `edit_finished`.

## MCP tools

| Tool | Purpose |
|------|---------|
| `materials_create` | New `.mat` (`path` or `folder`+`name`) |
| `materials_get` | Read props by `key` / `uid` |
| `materials_set` | Mutate asset; `save` default `true`; optional `wait_ms` |
| `materials_list_properties` | Supported property schema |
| `scene_set_model_material` | Shared slot: `entity_id`, `index`, `material_key` |
| `scene_set_model_material_instance` | Runtime instance props; no `.mat` |
| `scene_clear_model_material_instance` | Drop instance → use asset again |

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
| Model slots / instances | `engine/engine/rendering/model.h` |
| MCP helpers | `editor/editor/system/mcp/mcp_material_utils.*` |
| MCP tools | `editor/editor/system/mcp/mcp_tools_materials.cpp` |
| Inspector save | `editor/editor/hub/panels/inspector_panel/inspectors/inspector_assets.cpp` |
| Content Browser create | `editor/editor/hub/panels/content_browser_panel/content_browser_panel.cpp` |

## Rules of thumb

1. Need a reusable material → `materials_create` / `materials_set` (save).
2. Only this entity should look different → `scene_set_model_material_instance`.
3. Point an entity at an existing `.mat` → `scene_set_model_material`.
4. Play mode blocks scene mutators (`require_edit_scene`).
