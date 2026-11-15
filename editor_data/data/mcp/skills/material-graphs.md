---
name: material-graphs
title: Material graphs
description: Authoring graph materials (node materials) through material_graph_describe_nodes / get / set / compile / open - the JSON format, links and components, parameters and textures material instances set by name, reading what the compile found, and checking the generated code with shaderc.
order: 55
---
# Material graphs

Every material is a .mat whose look is a graph of nodes flowing into one Surface
Output; the engine generates its shader from the graph. A new material (and every
import) is the Standard graph: parameters named Base Color, Roughness, Metallic,
Bumpiness, Emissive Color, Emissive Intensity, Subsurface Color, Tiling, Dither
Threshold (Alpha Cutoff when masked; Specular Color and Glossiness in the specular
workflow) and texture nodes named Color Map, Normal Map, Roughness Map, Metallic
Map, Emissive Map, AO Map (Specular Map). assets_create_batch,
assets_set_properties_batch and scene_set_model_material_instances_batch take
those as the old PBR keys too (base_color, color_map, ...; colors sRGB;
assets_list_properties {"type":"material"} lists them). Assign it with
scene_set_model_materials_batch, override its values per entity with
scene_set_model_material_instances_batch (parameters, textures).

## Workflow

1. material_graph_describe_nodes - every node type in a line; pass types (or a
   category) for the pins and settings of the ones you use.
2. New material: material_graph_set {"key":"app:/data/Materials/Rust.mat",
   "create":true, "graph":{...}}. Existing: material_graph_get, edit the graph
   it returns, material_graph_set the whole graph back.
3. Read the result's compile: errors and warnings name their node and pin. A
   graph with errors is saved too and draws defaults where they are - fix them.
4. material_graph_compile {"graph":...} checks a graph without saving it;
   gpu:true also runs shaderc for the editor's renderer.
5. material_graph_open shows the material in the Material Graph window for the
   user (refused while the window holds unsaved untitled work).

material_graph_set is not undoable with edit_undo: keep the material_graph_get
result to go back.

## The format

```json
{"nodes": [
  {"id": "output", "type": "surface_output", "inputs": [
    {"pin": "base_color", "link": "tinted"},
    {"pin": "roughness", "link": "rough"}]},
  {"id": "tint", "type": "color_parameter",
   "inputs": [{"pin": "value", "value": [0.8, 0.3, 0.2, 1.0]}],
   "settings": [{"name": "name", "value": "Tint"}]},
  {"id": "albedo", "type": "texture_sample", "texture_key": "app:/data/textures/rust.png"},
  {"id": "tinted", "type": "multiply",
   "inputs": [{"pin": "a", "link": "albedo.rgb"}, {"pin": "b", "link": "tint.rgb"}]},
  {"id": "rough", "type": "constant", "inputs": [{"pin": "value", "value": [0.6]}]}
], "comments": []}
```

- A node: id (unique, letters digits underscores), type, inputs, settings,
  texture_key for texture nodes. Leave positions out: the window lays the graph
  out when it opens.
- A link is written on the input it feeds: "node" reads its first output,
  "node.pin" a named output, "node.pin.rg" some of its components. An input
  entry "base_color.g" feeds one component of a vector input.
- An unlinked input takes "value" or its default: as many numbers as its type
  has components (a Vector 2 scale is [8, 8]; [8] alone sets x and leaves y 0).
  Inputs marked unlinked_reads (a uv) read their natural source when left alone.
- A vector never goes into a float: link a component ("color.rgba.r") or
  reduce it first. A loop is an error.
- Surface Output pins: base_color, metallic, roughness, normal (tangent space),
  emissive, ambient_occlusion, opacity_mask (values below the opacity_mask_clip
  pin, 0.5 by default, are cut out, from its shadow too), opacity_mask_clip,
  subsurface_color, subsurface_opacity, camera_fade_distance (world units within
  which the surface dithers out near the camera; 0 none), world_position_offset;
  setting two_sided "on" draws both faces; cull_front_faces "on" hides front
  faces and draws back ones (a mesh seen from inside). Link a parameter into
  opacity_mask_clip or camera_fade_distance for material instances to change it.
- Setting workflow "specular": base_color is the diffuse color, and the specular
  (color) and glossiness pins replace metallic and roughness; the shader converts
  them per pixel. Links into the other workflow's pins are ignored.
- Setting translucent "on": the surface is see-through, dithered out by
  opacity_mask read as an alpha (not clipped); it casts no shadow and the GI
  lets light through it.
- world_position_offset (vec3, world units) moves each vertex: wind, waves,
  pulses. It is computed per vertex in a vertex shader of its own; its shadow
  and motion vectors follow. Set max_world_position_offset (Max Offset) to how
  far it moves a vertex, so meshes are not culled while moved out of their
  bounds. A Normal Map cannot feed it (an error); Screen Position reads the
  middle of the view there. Material instances of such a graph draw on their
  own. The compile report says uses_world_position_offset,
  world_position_offset_animated and max_world_position_offset.

## Flow nodes

- static_switch: setting value "true" / "false" picks the input compiled in;
  the other branch costs nothing. With a name setting the material's inspector
  shows it as a checkbox (compile report: static_switches).
- Per-pixel conditions (a condition is a float, 1 true, 0 false):
  if (UE style: a, b floats, greater / equal / less inputs, threshold for
  equality), compare (a, b floats, setting operation ">", ">=", "<", "<=",
  "==", "!=": 1 or 0), branch (condition above 0.5 picks true, else false).
  Both sides of if / branch are computed; static_switch is the free one.
- discard: pixels where value is below threshold (0.5) are not drawn, in the
  shadow neither. It needs no link: every discard node acts. It masks the
  material (uses_opacity_mask), as opacity_mask does.
- named_reroute_declaration (setting name, input in) and named_reroute
  (setting name): the usage reads the declaration without a link - for values
  used far apart on the canvas.
- custom: settings output ("float", "vec2", "vec3", "vec4") and code, the body
  of a function of a, b, c, d (each as wide as its link) and i (MaterialInputs:
  i.uv0, i.world_position, i.vertex_normal, i.view_direction, i.time ...), in
  bgfx shader language: "return vec3_splat(a) * 0.5;". material_graph_compile
  with gpu:true checks it.
- triplanar_sample: a texture read along the world axes (no UVs needed); inputs
  position (world position), normal, scale, sharpness.

## Material functions

- A function is a .mgraph file in the project's data folder: function_input
  nodes (settings name, type "float" / "vec2" / "vec3" / "vec4"; value-only
  input default) and function_output nodes (setting name, input in), no
  surface_output. Write it as a file (the Content Browser's Create > Material
  Function makes a starter), material_graph_compile {"graph":...} checks it.
- Call it with a material_function node: setting function = its key
  ("app:/data/functions/Rim.mgraph"); pins in0, in1, ... are its inputs in the
  file's order, out0, out1, ... its outputs. An unlinked pin takes the
  function's default; a value set on it goes into every component.
- Parameters inside functions are shared by name; problems inside one are
  reported on the call. Editing the function recompiles every material calling
  it.

## Parameters and textures

- Scalar, Vector and Color Parameter nodes are what material instances, scripts
  (Material / MaterialInstance SetFloat("name", v)) and the instance inspector
  set; the name setting names them (default: the node id). The compile result
  lists them with their register: those with shares_draw true can differ per
  entity and still draw together. A Scalar Parameter's slider_min / slider_max
  settings give the range the inspectors edit it with (none while max <= min).
- The graph material's own inspector edits its parameters' defaults and texture
  nodes' textures; neither changes the generated code, so nothing recompiles.
- Texture nodes are set per instance by their name setting (default: the node
  id); that gives the entity a draw of its own. Parameters and texture nodes
  take a description setting: the hint of their inspector row.
- A parameter's per_instance setting ("on" by default, "off") places it among
  the first registers, which overrides change without a draw of their own, or
  after them.
- At most 15 textures and 64 parameter registers per graph.
