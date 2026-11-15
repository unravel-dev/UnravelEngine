---
name: unravel-materials
description: >-
  Creates and edits UnravelEngine materials (material graphs; the Standard graph is the default) via Editor MCP
  tools or code.
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

Create parity: content browser `get_asset_from_instance<material>(key, make_standard_material({}))` + save.  
Save parity: inspector `inspector_asset_handle_material` on `edit_finished`.

## MCP tools

| Tool | Purpose |
|------|---------|
| `materials_create_batch` | New `.mat` files (`items`: `path` or `folder`+`name`) |
| `materials_get_batch` | Read props for many (`items`: `key` / `uid`) |
| `materials_set` | Mutate asset; `save` default `true`; optional `wait_ms` |
| `materials_list_properties` | The Standard properties (keys, names, kinds) and the reflected ones |
| `scene_set_model_materials_batch` | Shared slot assignments (one undo) |
| `scene_set_model_material_instances_batch` | Override instance values (reports `shares_draw`); no `.mat` |
| `scene_clear_model_material_instances_batch` | Revert every override -> draws like the shared material |
| `assets_find` | Find materials (and all other asset types) by `type`/`prefix`/`name_contains` |
| `editor_focus_os_window` | Focus/raise editor OS window so asset watcher can import new `.mat` files |

When the editor is unfocused, filesystem watcher events may not run - call `editor_focus_os_window` before `materials_create_batch` / `assets_wait_ready_batch` if creates do not land.

Single-property edits: one-key `properties` object. Bulk: multiple keys in one call.

## Example bulk asset set

```json
{
  "key": "app:/data/Materials/RobotBody.mat",
  "save": true,
  "properties": {
    "base_color": [0.8, 0.2, 0.1, 1.0],
    "roughness": 0.45,
    "metallic": 0.1,
    "color_map": "app:/data/Textures/albedo.etex"
  }
}
```

## Code touch points

| Purpose | Path |
|---------|------|
| Standard material (description, builder, property table, specular solve twin) | `engine/engine/rendering/material_graph/standard_material.h` |
| Legacy PBR `.mat` loader (load only, polymorphic name `"pbr_material"`) | `engine/engine/meta/rendering/standard_material.cpp` |
| Engine default materials | `engine_data/data/materials/standard.mat`, `fallback.mat` (ids of the old `engine:/embedded/standard` / `fallback`) |
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

## One material type: the graph

Every material is a `graph_material`. The Standard material is a graph (`make_standard_material_graph` over a
`standard_surface`): its values are parameters named as the PBR inspector named them ("Base Color", "Roughness", ...),
its maps named texture nodes ("Color Map", "Normal Map", "Roughness Map", "Metallic Map", "Emissive Map", "AO Map";
"Specular Map" in the specular workflow), one map packing roughness and metallic is the named Static Switch "Packed
Metal Roughness". New materials, imports, MCP and C# all go through it. `find_standard_property` maps the old PBR
property keys (`base_color`, `color_map`) to those names, `make_standard_parameter_value` converts the values (picker
colors are sRGB, parameters linear). Old `.mat` files holding a `pbr_material` load as `legacy_pbr_material` and become
the Standard graph right after the load (`upgrade_legacy_material`, in `load_from_file`); material format 7 recompiles
them. Material instances saved over a PBR material load their overrides as Standard parameter / texture overrides, and a
whole PBR material kept per slot by older saves becomes its differences (`material_instance::set_legacy_copy`).
Blend becomes the Surface Output's Translucent setting (dithered by its alpha, no shadow, no GI occluder); an opaque
base color below alpha 1 dithers at Opacity Mask Clip 0.25.

Specular workflow: the Surface Output's Workflow setting (`MATERIAL_WORKFLOW_SETTING`). Specular shows Diffuse (the
base_color pin), Specular and Glossiness and hides Metallic / Roughness; the generated shader defines
`MATERIAL_SPECULAR_WORKFLOW` and `fs_material_graph_gbuffer.sh` converts per pixel with `mg_specular_to_metallic`
(`material_graph.sh`, the solve the importer once baked, in sRGB-encoded space; C++ twin `convert_specular_to_metallic`).
One G-buffer for both. The importer keeps KHR_materials_pbrSpecularGlossiness in this workflow with its own maps (no
bake); legacy Phong stays metallic (metallic 0, roughness from shininess), its specular color is a highlight tint.

## Per-instance values and batching

A parameter's Per Instance setting (default on) puts it among the first registers, vectors first, then scalars; the
others follow. Overrides of parameters in registers 0..3 keep the shared instanced draw (`material_instance::shares_draw`,
`instance_material_vertex_data`); the Standard graph lays out Base Color, Subsurface Color, Emissive Color, then
Emissive Intensity / Roughness / Metallic / Bumpiness there (Tiling, Alpha Cutoff, Dither Threshold and the specular
color are the material's). A masked graph's overrides share the G-buffer draw too: its shadow casters are keyed by the
resolved material. Textures and World Position Offset graphs draw on their own. Each parameter and texture carries the
render_change aspects its Surface Output channels feed (`material_graph_parameter::changes`), so an emissive edit does
not invalidate cached shadows. GI recaptures caused by per-instance values are limited
(`gi_surface_cache::gate_instance_values`); the GI's emissive light sources come from `graph_material::get_emissive_estimate`.

## Material graphs (node materials)

A material authored as nodes flowing into a Surface Output; the compiler generates its G-buffer fragment shader. A graph
material is a `.mat` (`graph_material`, polymorphic name `"graph_material"`), and the `.mat` is the only
file in the data folder. Its compile generates the shader and compiles it into the compiled cache beside the compiled
material, one file per renderer (`compiled/<name>.mat.asset.dxil`, ...), each marked with the hash of the code the
compiled material records; the loader gives the material its renderer's shader and the deferred pipeline draws it.
The editor compiles the renderer it draws with; deploys cook every renderer they ship (`shader_cook.cpp`). A value-only
edit keeps the hash, so it runs no shaderc. Design and roadmap: `tasks/material_graph_design_2026-11-14.md`.

| Purpose | Path |
|---------|------|
| Graph model, ids, links, copy/paste | `engine/engine/rendering/material_graph/material_graph.h` |
| Node library (add a node = one entry) | `engine/engine/rendering/material_graph/material_graph_library.cpp` |
| Graph -> `.eshader`, diagnostics, binding limits | `engine/engine/rendering/material_graph/material_graph_compiler.h` |
| The material type (binding: registers, textures, sidedness) | `engine/engine/rendering/material_graph/graph_material.h` |
| JSON format | `engine/engine/meta/rendering/material_graph.hpp`, `graph_material.hpp` |
| Shader compiled with the `.mat` | `compile_graph_material_shader` in `editor/core/asset_compiler/asset_compiler.cpp`; loaded by `attach_graph_material_shader` in `engine/engine/assets/impl/asset_reader.cpp` |
| Drawing (programs per shader and variant; until compiled, `fs_deferred_geom.eshader`'s default surface) | `submit_geometry_material` in `engine/engine/rendering/pipeline/deferred/pipeline_geometry.cpp` |
| Shader side | `engine_data/data/shaders/material_graph/` |
| Editor window | `editor/editor/hub/panels/material_graph_panel/` (Windows > Material Graph, MCP `panel_focus` `material_graph`) |
| Examples | `editor_data/data/material_graphs/examples/*.mgraph` |
| Tests | `unravel-tests --suite "material graphs"` (compiles every example and every node type with shaderc) |

Creating one: Content Browser Create > Material (the Standard graph) or Material Graph (a starter graph), or a `.mat`
written by hand:
`{"material": {"polymorphic_id": 2147483649, "polymorphic_name": "graph_material", "ptr_wrapper": {"id": 2147483649,
"data": {"graph": {"nodes": [...]}}}}}`. Double-click it (or Open in Material Graph in its inspector) to edit it; the
window saves every finished edit, values show in the scene at once, new code after the shader compiles. Until then
the material draws the default surface (`fs_deferred_geom.eshader`, the Standard defaults).

A graph material's inspector lists Parameters, Textures and Switches in graph order: a row is named by the node's
Parameter Name (texture nodes and Static Switches take one too; an id without one reads in words, "color_map" ->
"Color Map") and hinted by its Description setting. It edits the parameters' defaults and texture nodes' textures in
its graph (`set_material_graph_parameter_default`, `set_material_graph_node_texture`), one undo step per finished edit;
neither changes the generated code, so no shader recompiles. A named Static Switch is a checkbox
(`set_material_graph_static_switch`); changing it compiles the material again. Scalar parameters take Slider Min / Max
settings (`find_material_parameter_slider`), the range the inspector and the graph window edit them in.

Writing a graph: each node `{"id", "type", "inputs": [{"pin", "link" | "value"}], "settings": [{"name", "value"}]}`; a
link is `"node_id.output_pin"` on the input it feeds, `"node.pin.r"` picks components, an entry `"base_color.g"` feeds
one component; a texture node takes `"texture_key": "app:/..."`; leave positions out (the editor lays the graph out).
A vector never links into a float (refused). At most 15 textures and 64 parameter registers. The Surface Output's
Opacity Mask Clip and Camera Fade Distance are inputs (a parameter linked there is per instance); the generated shader
writes them into `MaterialSurface`, which `fs_material_graph_gbuffer.sh` / `fs_material_graph_shadow.sh` read. Older
files' `opacity_mask_clip` setting is upgraded on load (`upgrade_material_graph`).
Conditions are floats (1 / 0): If, Compare, Branch; Discard nodes are pixel-stage roots (`mg_discard_if`) that mask
the material like the Opacity Mask (shadow casters clip too; a GI card capture keeps every texel). A Normal Map reads its
texture's channel layout at draw time (`u_mg_normal_channels`, set from `gfx::texture::get_normal_map_channels`), as
platforms compress normal maps differently; the generated code never names a texture, so picking one recompiles nothing. `.mgraph` files hold
`{"material_graph": {...}}` and open as untitled graphs; Ctrl+V in the window pastes such text, and one that brings a
`surface_output` wires the graph's own output.

An Opacity Mask clips the shadow too: a masked graph's shader gets four shadow caster variants
(`fs_material_graph_shadow.sh`), and a mask that reads time, the camera or the screen keeps its caster out of cached
shadow maps.

Per-instance graph values: `graph_material::set_parameter` / `set_texture` give values by name over the graph's
(scripts' shared edits, restored after play); `material_instance::set_graph_parameter` / `set_graph_texture` override
them per renderer, saved as `"graph_parameters": {"name": {"x",...}}` / `"graph_textures"` objects in the instance. The
generated shader reads registers 0..3 through `MG_PARAM0..3`: its `instance_material` variant takes them from
`MaterialInputs::instance_register0..3` (v_material0..3, `graph_material::write_instance_constants`), so parameter
overrides there keep the shared draw; textures, later registers and World Position Offset graphs do not
(`material_instance::shares_draw`). `copy_material_values` copies a graph material's graph, values and shader through
`material::copy_unreflected_values`, so a resolved instance and the play restore follow the shared material. Inspector:
the instance block lists the graph's parameters and texture nodes (`inspect_graph_overrides`); C#: `SetFloat(string)`
and the other named accessors on `Material` and `MaterialInstance`; MCP: `parameters` / `textures` in
`scene_set_model_material_instances_batch`.

Flow and reuse nodes: Static Switch (compile-time branch), Named Reroute / Usage (the usage's hidden input is linked by
the compiler), Custom (user code as a function, `material_setting_kind::code`), Triplanar Sample (`mg_triplanar`), and
material functions - .mgraph files of Function Input / Output nodes called by Material Function nodes; the compiler
flattens calls (`expand_function_calls`), compiled graph materials keep copies (`graph_material::copy_functions`, format
6) and `.mgraph` is a material dependency format.

World Position Offset (Surface Output `world_position_offset`, setting `max_world_position_offset`): the compiler emits
what it reads again as `evaluate_vertex` in a generated vertex shader (`result.vertex_shader`, 20 variants: G-buffer,
`velocity_*`, and `position_only*` for the editor's picking and outline, `graph_material_programs::get_position_only_variant`),
stored as `vertex_variants` in the same compiled file and set with `graph_material::set_vertex_shader`.
The G-buffer and velocity mains are shared files (`deferred_geom/vs_deferred_geom.sh`, `velocity/vs_velocity.sh`) with
one `MATERIAL_WORLD_POSITION_OFFSET` hook each, so their position math stays identical for the velocity pass's EQUAL
depth test - change both or neither. Shadows use the graph's own casters (`draws_own_shadow`), an animated offset keeps
the shadow live (`is_shadow_animated`) and still meshes in the velocity pass; bounds grow by Max Offset in
`model_component::grow_world_bounds_by_offsets`.

Previews: every node shows a swatch of its value and the side pane a 3D preview, drawn with shaders the window compiles
itself in the background for the editor's renderer (`material_graph_previews`, `material_graph_scene_preview`); the
generated swatch shader is `compile_material_graph` with `is_preview` (`evaluate_preview(i, node)`, main in
`fs_material_graph_preview.sh`). A node's context menu hides its swatch (`"preview_hidden": true` in the graph).
