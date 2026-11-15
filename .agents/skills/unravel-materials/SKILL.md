---
name: unravel-materials
description: >-
  UnravelEngine materials: material graphs (every material is a node graph; the Standard graph is the default),
  .mat assets, the G-buffer shaders generated from graphs, model material slots and per-renderer material
  instances. Use when changing a .mat, a graph node, the graph compiler or the Material Graph window, material
  instances and their batching, or choosing between an asset edit and an instance override.
---

# Unravel materials

Materials as node graphs: the graph model, node library and compiler (`engine/engine/rendering/material_graph/`),
the shared shader side (`engine_data/data/shaders/material_graph/`), the Material Graph window
(`editor/editor/hub/panels/material_graph_panel/`), the shipped example graphs
(`editor_data/data/material_graphs/`), and the material assets, slots and instances that use them
(`engine/engine/rendering/material.h`, `engine/engine/rendering/material_instance.h`). The deferred pipeline that
draws them: skill `unravel-rendering`. Driving materials through the editor's MCP tools: the editor's guide
`editor_data/data/mcp/skills/material-graphs.md` (MCP `skills_get`, name `material-graphs`).

## Gotchas

- Every material is a `graph_material`; there is no other material type. A `.mat` holding a `pbr_material`
  loads as `legacy_pbr_material` and becomes the Standard graph right after the load (`upgrade_legacy_material`,
  in `load_from_file`); the material format version (`get_format_version<material>`) recompiles such files. The
  engine defaults `standard.mat` / `fallback.mat` keep the ids of the keys `engine:/embedded/standard` /
  `engine:/embedded/fallback`, which still resolve (`asset_manager::resolve_default_material_key`). The name is
  "Metallic" everywhere (parameter, map, C#, MCP keys, shaders); only `pbr_material` files and the instances
  saved over them read "metalness".
- A vector never links into a float, directly or through a dynamic node the link widens: the editor refuses the
  link with the reason and the compiler reports an error. Components are read explicitly (R/G/B/A outputs, split
  pins, `"node.pin.r"` links, Swizzle). Conversions that keep data stay implicit (float broadcast, wider vector
  into a narrower one).
- A compile's by-products stay in the compiled cache: a graph material's shader is one file per renderer beside
  the compiled material (`compiled/<name>.mat.asset.dxil`, ...), removed and renamed with it, cooked per renderer
  by deploys. The data folder holds only the `.mat` (what a person or agent writes); generated files in data are
  for imports a user edits afterwards, such as a model's default materials.
- A per-renderer look goes through a material instance, never a side channel next to the material system (a
  per-renderer tint or scale carried outside the material). A batching cost of instances is fixed in the
  instance or renderer path, not by a second API. Scripts get the same object: a `MaterialInstance` made from a
  material, never parallel value types.
- A material instance never writes the `.mat`; it lives in the scene or prefab. A reusable look is a `.mat` asset;
  which asset a model uses is its slot.
- Generated code never names a texture: samplers are named by index (`MATERIAL_GRAPH_MAX_TEXTURES` of them,
  registered before any graph program for the OpenGL uniform order) on stages clear of the skinning palette's.
  Picking a texture recompiles nothing; a Normal Map reads its texture's channel layout at draw time
  (`u_mg_normal_channels` from `gfx::texture::get_normal_map_channels`).
- The generated shader writes the Surface Output's defines (`MATERIAL_TWO_SIDED`, `MATERIAL_OPACITY_MASK`,
  `MATERIAL_SPECULAR_WORKFLOW`, ...) before its includes: `material_graph.sh` and the mains test them with `#if`.
- A value-only edit keeps the code hash and runs no shaderc (`has_same_material_graph_code` ignores values and
  editor layout). Materials with the same code share one shader file content, one loaded `gfx::shader`
  (`graph_shader_cache` in `asset_reader.cpp`) and one set of programs: bgfx holds a fixed number of programs.
- The compiler never fails: problems are reported per node and faulty values replaced by defaults. Until a
  shader exists, or when it fails to compile, the material draws the default surface (`fs_deferred_geom.eshader`
  with the Standard defaults).
- At most `MATERIAL_GRAPH_MAX_TEXTURES` (15) textures and `MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS` (64) parameter
  registers per graph.
- Overrides of parameters in registers 0..3 keep the shared instanced draw (`material_instance::shares_draw`);
  textures, later registers and World Position Offset graphs draw on their own. Overrides that feed nothing
  `render_interest::shadow_caster` reads cast with the shared material (`model::get_shadow_material`), so they keep
  one shadow batch.
- World Position Offset hooks into the shared G-buffer and velocity mains (`deferred_geom/vs_deferred_geom.sh`,
  `velocity/vs_velocity.sh`, one `MATERIAL_WORLD_POSITION_OFFSET` hook each); their position math stays identical
  for the velocity pass's EQUAL depth test: change both or neither.
- An Opacity Mask (or a Discard node) clips the shadow too; a mask that reads time, the camera or the screen keeps
  its caster out of cached shadow maps (`is_shadow_animated`).
- Translucent (the Surface Output setting) dithers by alpha, casts no shadow and occludes nothing for the GI:
  there is no blended pass.
- The GI treats a material as a light source from `graph_material::get_emissive_estimate`: the constants and
  parameters along Multiply nodes into Emissive, a texture reading white; anything else there is unknown and no
  light source.
- A material depends on the `.sh` and `.mgraph` files it reads: editing `material_graph.sh` or a material function
  recompiles every material using it.
- Editor-private scenes (the graph window's 3D preview, inspector previews) are unloaded and reloaded into new
  entities on a script recompile and at play end: code keeping entity handles into one checks them at every use
  and rebuilds (`material_graph_scene_preview`).

## How it works

The Standard material is a graph (`make_standard_material_graph` over a `standard_surface`,
`standard_material.h`): its values are parameters named as the old PBR inspector named them ("Base Color",
"Roughness", ...), its maps named texture nodes ("Color Map", "Normal Map", "Roughness Map", "Metallic Map",
"Emissive Map", "AO Map"; "Specular Map" in the specular workflow), and one map packing roughness and metallic is
the named Static Switch "Packed Metal Roughness". New materials, imports, MCP and C# all build it
(`make_standard_material`). `find_standard_property` maps the PBR property keys (`base_color`, `color_map`) to
those names and `make_standard_parameter_value` converts values (picker colors are sRGB, parameters linear);
`resolve_standard_property` answers a script's `MaterialProperty` on either workflow (Roughness on a specular
material is 1 - Glossiness), and a property the graph lacks warns once per material. Instances saved over a PBR
material load their overrides as Standard parameter and texture overrides, and a whole PBR material kept per slot
by older saves becomes its differences (`material_instance::set_legacy_copy`). PBR Blend becomes the Surface
Output's Translucent setting; an opaque base color below alpha 1 dithers at Opacity Mask Clip 0.25. The cull type
is read only (Two Sided and Cull Front Faces settings); `for_each_material_property` skips read-only data.

Specular workflow: the Surface Output's Workflow setting (`MATERIAL_WORKFLOW_SETTING`). Specular shows Diffuse
(the base_color pin), Specular and Glossiness and hides Metallic / Roughness; the generated shader defines
`MATERIAL_SPECULAR_WORKFLOW` and `fs_material_graph_gbuffer.sh` converts per pixel with `mg_specular_to_metallic`
(`material_graph.sh`, in sRGB-encoded space; C++ twin `convert_specular_to_metallic`). One G-buffer serves both.
The importer keeps KHR_materials_pbrSpecularGlossiness in this workflow with its own maps; a Phong material
imports metallic (metallic 0, roughness from shininess), its specular color a highlight tint.

Compile and draw:

- A `.mat` (`graph_material`, polymorphic name `"graph_material"`) holds the graph. Its compile
  (`compile_graph_material_shader` in `editor/core/asset_compiler/asset_compiler.cpp`) generates the G-buffer
  fragment shader (the shared main is `fs_material_graph_gbuffer.sh`), records the hash of the generated code and
  compiles it beside the compiled material for the renderer the editor draws with; deploys cook every renderer
  they ship (`editor/core/deploy/shader_cook.h`). The loader gives the material its renderer's shader
  (`attach_graph_material_shader`, `asset_reader.cpp`).
- `graph_material_programs` makes one program per shader object and variant pair at its first draw and releases
  unused ones; `submit_geometry_material` (`pipeline_geometry.cpp`) draws with it.
- A masked graph gets four shadow caster variants (`fs_material_graph_shadow.sh`); its overrides share the
  G-buffer draw because its shadow casters are keyed by the resolved material.
- The Surface Output's Opacity Mask Clip and Camera Fade Distance are inputs (a parameter linked there is per
  instance); the generated shader writes them into `MaterialSurface`, which the G-buffer and shadow mains read.
  Older files' `opacity_mask_clip` setting is upgraded on load (`upgrade_material_graph`).

Per-instance values and batching:

- A parameter's Per Instance setting (default on) puts it among the first registers, vectors first, then scalars
  four to a register; the others follow. The Standard graph lays out Base Color, Subsurface Color, Emissive Color,
  then Emissive Intensity / Roughness / Metallic / Bumpiness there (Tiling, Alpha Cutoff, Dither Threshold and the
  specular color are the material's).
- The generated shader reads registers 0..3 through `MG_PARAM0..3`: its `instance_material` variant takes them
  from `MaterialInputs::instance_register0..3` (`graph_material::write_instance_constants`,
  `instance_material_vertex_data`).
- `graph_material::set_parameter` / `set_texture` set values by name over the graph's (scripts' shared edits,
  restored after play); `material_instance::set_graph_parameter` / `set_graph_texture` override them per
  renderer, saved as `"graph_parameters"` / `"graph_textures"` objects in the instance. A values-only `set_graph`
  marks only what the changed defaults feed, and instances sync changed overrides by name.
- Each parameter and texture carries the `render_change` aspects its Surface Output channels feed
  (`material_graph_parameter::changes`), so an emissive edit leaves cached shadows alone. GI recaptures caused by
  per-instance values are rate-limited (`gi_surface_cache::gate_instance_values`).
- `copy_material_values` copies a graph material's graph, values and shader through
  `material::copy_unreflected_values`, so a resolved instance and the play restore follow the shared material.
- C#: `SetFloat(string, ...)` and the other named accessors on `Material` and `MaterialInstance`; a
  `MaterialProperty` key goes through `resolve_standard_property`.

Editing surfaces:

- The inspector lists Parameters, Textures and Switches in graph order: a row is named by the node's Parameter
  Name (an id without one reads in words, "color_map" -> "Color Map") and hinted by its Description. It edits the
  graph's defaults and textures (`set_material_graph_parameter_default`, `set_material_graph_node_texture`), one
  undo step per finished edit, with no recompile; a named Static Switch is a checkbox
  (`set_material_graph_static_switch`) whose change recompiles. Scalar parameters take Slider Min / Max
  (`find_material_parameter_slider`). The instance block lists the graph's parameters and texture nodes
  (`inspect_graph_overrides`).
- The Material Graph window (Windows > Material Graph; double-click a `.mat`, or Open in Material Graph in its
  inspector) saves every finished edit; values show in the scene at once, new code after the
  shader compiles. Previews: every node shows a swatch and the side pane a 3D preview, drawn with shaders the
  window compiles in the background for the editor's renderer (`material_graph_previews`,
  `material_graph_scene_preview`; swatches compile with `is_preview`, `evaluate_preview`, main
  `fs_material_graph_preview.sh`). A node's context menu hides its swatch (`"preview_hidden": true`).

Flow and reuse nodes: Static Switch (compile-time branch), Named Reroute / Usage (the compiler links the usage's
hidden input), Custom (user code as a function, `material_setting_kind::code`), Triplanar Sample
(`mg_triplanar`). Conditions are floats (1 / 0): If, Compare, Branch; Discard nodes are pixel-stage roots
(`mg_discard_if`) that mask like the Opacity Mask (shadow casters clip too; a GI card capture keeps every texel).
Material functions are `.mgraph` files of Function Input / Output nodes called by Material Function nodes: the
compiler flattens calls (`expand_function_calls`) and the compiled material keeps copies
(`graph_material::copy_functions`), since a game ships no `.mgraph`.

World Position Offset (Surface Output `world_position_offset`, setting `max_world_position_offset`): the compiler
emits what it reads again as `evaluate_vertex` in a generated vertex shader (`result.vertex_shader`, with the
G-buffer, `velocity_*` and `position_only*` variants for the editor's picking and outline,
`graph_material_programs::get_position_only_variant`), stored as `vertex_variants` in the same compiled file and set
with `graph_material::set_vertex_shader`. Shadows use the graph's own casters (`draws_own_shadow`); an animated
offset keeps the shadow live and the mesh in the velocity pass; bounds grow by Max Offset
(`model_component::grow_world_bounds_by_offsets`).

## Procedures

- Add a node type: one entry in `material_graph_library.cpp` (pins, settings, code template); suite "material
  graphs" compiles a graph using every node type with shaderc on every renderer.
- Create a material in code: `asset_actions::create_material` (`editor/editor/assets/asset_actions.cpp`:
  `get_asset_from_instance<material>(key, make_standard_material({}))`, then `asset_writer::atomic_save_to_file`);
  Content Browser Create > Material makes the Standard graph, Create > Material Graph a starter graph
  (`create_material_graph`). Inspector edits save on `edit_finished` (`inspector_asset_handle_material`).
- Write a graph by hand: a `.mat` is `{"material": {"polymorphic_id": 2147483649, "polymorphic_name":
  "graph_material", "ptr_wrapper": {"id": 2147483649, "data": {"graph": {"nodes": [...]}}}}}`; each node
  `{"id", "type", "inputs": [{"pin", "link" | "value"}], "settings": [{"name", "value"}]}`; a link is
  `"node_id.output_pin"` on the input it feeds, `"node.pin.r"` picks components, an input entry `"base_color.g"`
  feeds one component; a texture node takes `"texture_key": "app:/..."`; leave positions out (the editor lays the
  graph out). `.mgraph` files hold `{"material_graph": {...}}`, open as untitled graphs, and paste into the window
  with Ctrl+V (one bringing a `surface_output` wires the graph's own output). Examples:
  `editor_data/data/material_graphs/examples/`.
- Edit materials, slots and instances through MCP: the editor's `material-graphs` guide (above).

## Verify

- Suite "material graphs" (slow: shaderc per example and renderer) for the model, compiler, node library and
  shader side; "material instances" for overrides, batching and `pbr_material` loads; "render change" for property
  traits; "eshader files" after editing a `material_graph/*.sh` include.
- Live in an editor: change a parameter (no recompile, the scene updates), pick a texture (no recompile), toggle a
  Static Switch (recompiles), override a value on one entity and confirm the draw count stays (registers 0..3),
  enter and leave play mode with the graph window open.

## Entry points

| Purpose | Path |
|---------|------|
| Graph model, ids, links, copy/paste | `engine/engine/rendering/material_graph/material_graph.h` |
| Node library | `engine/engine/rendering/material_graph/material_graph_library.cpp` |
| Compiler: graph to shader, diagnostics, limits | `engine/engine/rendering/material_graph/material_graph_compiler.h` |
| The material type, binding, programs | `engine/engine/rendering/material_graph/graph_material.h`, `engine/engine/rendering/material_graph/graph_material_programs.h` |
| Standard graph, property table, `pbr_material` upgrade | `engine/engine/rendering/material_graph/standard_material.h` |
| Instances and slots | `engine/engine/rendering/material_instance.h`, `engine/engine/rendering/model.h` |
| JSON format | `engine/engine/meta/rendering/material_graph.hpp`, `engine/engine/meta/rendering/graph_material.hpp` |
| Shader side | `engine_data/data/shaders/material_graph/material_graph.sh`, `engine_data/data/shaders/material_graph/fs_material_graph_gbuffer.sh` |
| Editor window, previews | `editor/editor/hub/panels/material_graph_panel/material_graph_panel.h` |
| Engine default materials | `engine_data/data/materials/standard.mat`, `engine_data/data/materials/fallback.mat` |
| Inspector, create actions | `editor/editor/hub/panels/inspector_panel/inspectors/inspector_assets.cpp`, `editor/editor/assets/asset_actions.cpp` |
| MCP tool code | `editor/editor/system/mcp/mcp_tools_materials.cpp`, `editor/editor/system/mcp/mcp_material_utils.h` |
