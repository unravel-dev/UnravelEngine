---
name: material-graphs
title: Material graphs
description: Authoring graph materials (node materials) through material_graph_describe_nodes / get / set / compile / open - the JSON format, links and components, parameters and textures material instances set by name, decal and post-process materials, reading what the compile found, and checking the generated code with shaderc.
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
- Setting blend_mode: "opaque" (default; opacity_mask cuts out), "dithered"
  (see-through by a dither of opacity_mask, read as the Opacity, in the
  G-buffer: lit, shadowed and reflected like opaque surfaces), or one of the
  translucency pass: "translucent" (blends by its Opacity: glass, water,
  fades), "premultiplied" (adds its colour as it is while the Opacity covers
  what is behind), "additive" (adds its light: glows, holograms) and
  "modulate" (multiplies the scene by emissive, unlit: tints). Those four draw
  after the lighting, sorted far to near among themselves and the particles,
  lit by the sun with its shadow, up to 4 nearby lights with their shadow maps
  (the translucent_lights feature limit, 2 on phones and the web), the sky and
  the reflection probe covering them. Where the view runs the GI, a lit one
  whose graph reads at most 5 textures (scene reads included) takes the GI's
  light for its diffuse and rough reflections, from the GI's radiance cache
  (the room's bounce light instead of the sky's); sharp reflections stay the
  probe's. They get no SSR or screen-space AO and take no decals (what they
  refract is the lit scene, GI included). Their graphs read at most 7 textures
  (13 unlit), one fewer for each scene read.
  Older files' translucent "on" loads as dithered. The GI lets light through
  every see-through mode. Only Model components draw them: mesh particles and
  scripted instanced draws (Graphics.DrawMeshInstanced) draw such a material as
  the opaque stand-in.
- depth_prepass "on" (sorted translucent, premultiplied): the depth draws
  before the colour, so only the mesh's nearest layer shows where it overlaps
  itself (a see-through character); its depth then hides what draws after it
  behind it. write_velocity "on" (translucency pass): the surface's motion
  replaces the scene's behind it in the velocity buffer, over the whole
  surface, so TAA follows moving, mostly covering glass.
- cast_shadow (translucency pass, default "off"): "dithered" casts a shadow
  that leaves out texels by the Opacity, softened by the shadow filter into a
  fainter shadow. Opaque and dithered surfaces cast theirs as always.
- order_independent "on" (translucent, premultiplied): the surface blends with
  the other order-independent ones by weight instead of by sort, so crossing or
  nested surfaces (foliage cards, a glass inside a glass) do not pop as the
  camera moves; one surface alone looks the same as sorted. It does not refract.
  Together they sort among the sorted surfaces and particles as one object (the
  box around them all, its lowest sort_priority): a sorted surface inside a
  spread-out group of them sorts against the group's middle.
- refraction pin (translucent, premultiplied; 1 bends nothing, glass 1.5,
  water 1.33): the surface shows the lit scene behind it bent through its
  normal; a lit one blurs it by its roughness (frosted glass, from 0.03 up; a
  quarter-size blurred copy, one more texture stage). Translucency-pass surfaces may read the scene with scene_texture
  (scene_color: what is behind, before any translucency; scene_depth) and
  depth_fade (0 where the surface meets what is behind it, 1 at distance or
  further: soft water edges, fog cards); opaque and dithered surfaces read
  scene_texture's defaults. The colour copy follows the
  translucent_scene_color feature limit: without it the scene reads black and
  nothing refracts. Imported glTF transmission makes a surface translucent
  with its index of refraction.
- Setting shading_model "unlit": the surface shows its emissive alone, with no
  light, shadow or reflection on it (the GI still sees the emissive as light);
  only emissive, opacity_mask, normal (translucency pass), camera_fade_distance
  and world_position_offset are read. preserve_specular (translucent, default
  "on") keeps highlights and reflections at full strength under a low Opacity,
  as clear glass shows them; taa_response (0-1, default 0.5) lowers TAA's
  history under translucency-pass surfaces so moving ones do not smear.
- Translucency-pass surfaces sort by object: where a large surface surrounds
  smaller ones (water under a boat's windows), set the Model's sort_priority
  (lower draws first, behind; the scale particle emitters use).
- world_position_offset (vec3, world units) moves each vertex: wind, waves,
  pulses. It is computed per vertex in a vertex shader of its own; its shadow
  and motion vectors follow. Set max_world_position_offset (Max Offset) to how
  far it moves a vertex, so meshes are not culled while moved out of their
  bounds. A Normal Map or a node marked pixel_stage_only (derivatives: ddx,
  ddy, fwidth, the shapes, normal_from_height, parallax_occlusion) cannot feed
  it (an error); Screen Position reads the middle of the view there. Material
  instances of such a graph draw on their own. The compile report says
  uses_world_position_offset, world_position_offset_animated and
  max_world_position_offset.
- material_attributes takes a whole surface (make_material_attributes,
  blend_material_attributes): it gives every channel, and only the channels
  linked on the Surface Output besides replace its own; opacity_mask_clip,
  camera_fade_distance and world_position_offset stay the output's.

## Vertex stage and the object

- object_position (the pivot in world space), object_scale and object_random
  (Per Instance Random, 0..1 from the pivot: a tint or phase per copy, new
  values while an object moves), and transform (settings from / to: tangent,
  object, world, view; kind: direction, position, normal) read the object's
  transform. In World Position Offset they cost nothing; read by pixels, the
  graph gets a vertex shader of its own that passes them (compile report:
  reads_object_data). Instances still share the draw.
- vertex_interpolator: its input is computed once per vertex and blended across
  the triangle for the pixels - cheaper for what varies smoothly. At most four
  per graph (two with the object's transform read by pixels, to fit phones and
  browsers); compile report: vertex_interpolators. Instances overriding
  parameters of such a graph draw on their own.
- rotate_about_axis (setting result offset / position) is the building block of
  wind and spinning in World Position Offset; link object_position as its pivot.

## Textures as values

- texture_object (a 2D texture) and cubemap_object (a cube texture) hold a
  texture without reading it; link their output into the texture pin of
  texture_sample, normal_map, triplanar_sample, sample_cubemap,
  parallax_occlusion or normal_from_texture, into a custom node's a..d (a
  sampler2D parameter: read it with mg_texture2D(a, uv, i.mip_bias)), or into a
  material function. With texture linked, a node reads that texture, not its
  own. A texture never goes into a number (an error).
- function_input type "sampler2D" / "samplerCube" takes a texture; unlinked by
  the caller it reads the input's own texture_key. Type "MaterialSurface" takes
  material attributes (unlinked: the default surface).
- Texture nodes' settings: address ("texture" keeps the texture's own, "wrap",
  "clamp", "mirror"), filter ("anisotropic", "trilinear", "point") - bound with
  the draw, no recompile; texture_sample and sample_cubemap take mip_mode
  ("auto", "bias", "level") with the mip input.
- parallax_occlusion: the UV a height map (channel setting) shows along the view,
  height_scale in UV units, min_steps / max_steps, reference_plane (1: the tops
  stay on the surface); link its uv into the other texture reads.

## Quality

- quality_switch passes high on, or low where the material is compiled for low
  quality. Each profile picks it with the project setting
  graphics.material_quality ("high" or "low"; settings_set with profile): new
  Android and Web profiles start at low, Desktop at high. A deploy cooks every
  graph material at its profile's quality, the material and its shaders
  together. The other branch is not in the shader. The editor draws High.

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
- color_ramp: the colour of a gradient at value; setting count (2 to 8 stops),
  interpolation ("linear", "smooth", "constant"); stop k is the hidden inputs
  color<k> (RGBA, linear) and position<k> (0..1), in any order.
- Math, colour, UV and procedural nodes cover what shader code writes in a line:
  trigonometry (tan, asin, acos, atan, atan2), exp / log, round / truncate / sign,
  reflect / refract, derivatives, random (a hash of a value of any width), wave,
  noise and noise_3d (setting type value / gradient / simplex, octaves for fBm),
  voronoi / voronoi_3d (distance, edges, cell), the colour blend modes, HSV,
  polar coordinates, twirl, flipbook, bump_offset, sphere_mask and the shape
  masks. material_graph_describe_nodes lists them all.

## Domains: surface, decal, post process

The output node says what a material draws: surface_output (a mesh's surface),
decal_output (what a Decal component's box lays onto the surfaces in it) or
post_process_output (the colour of the whole view). The compile result's
domain says which; material_graph_describe_nodes lists each node's domains,
and a node of another domain is an error that reads its default. Each
domain is a starter in the Content Browser (Create > Decal Material, Post
Process Material) and in the Material Graph window's New menu.

Decal materials:

- decal_output pins: base_color, opacity, normal, metallic, roughness,
  emissive, normal_opacity and surface_opacity (how much of Opacity the normal
  and the metallic / roughness take). Settings: normal_blend (detail adds the
  decal's bumps to the surface's, replace swaps them), edge_fade (the share of
  the box's depth its ends fade over). A channel is written when its pin is
  linked or holds a value other than its default ({"pin":"roughness",
  "value":[0.9]} writes roughness); one left at its default keeps the surface's.
- texcoord reads the decal's UV: 0 to 1 across its face, v down. decal_data
  gives fade, age, normalized_age, lifetime (fade a blood splat by
  normalized_age); decal_position the pixel's place in the box, -0.5 to 0.5.
- scene_texture (source scene_depth, world_normal, base_color, metallic,
  roughness, ambient_occlusion) reads the surface the decal lies on: wetness
  darkens base_color and lowers roughness.
- Writing only base_color, opacity and emissive is the cheap path; normal,
  metallic or roughness take the decal buffers. The feature limit
  decal_surface_channels (Decal Normals And Roughness) turns them off; new
  Android and Web profiles draw colour only.
- At most 12 textures.

```json
{"nodes": [
  {"id": "output", "type": "decal_output", "inputs": [
    {"pin": "base_color", "link": "tint.rgb"}, {"pin": "opacity", "link": "mask.a"}]},
  {"id": "mask", "type": "texture_sample", "texture_key": "app:/data/textures/splat.png"},
  {"id": "tint", "type": "color_parameter",
   "inputs": [{"pin": "value", "value": [0.4, 0.02, 0.02, 1.0]}],
   "settings": [{"name": "name", "value": "Tint"}]}
]}
```

Post Process materials:

- post_process_output pins: color (the pixel's new linear colour; unlinked the
  scene's), opacity (its strength; the entry's weight scales it). Settings:
  location (before_transparent: before particles and TAA, for outlines and
  edge effects; before_bloom: after TAA, what it adds blooms; after_tonemapping:
  the display colour 0 to 1), priority (higher runs later within a location),
  run_where_mask (1 to 255: only over the renderers with that Post Process
  Mask; 0 everywhere).
- scene_texture sources: scene_color, scene_depth, world_normal, base_color,
  metallic, roughness, ambient_occlusion, emissive, velocity, custom_mask (a
  renderer's Post Process Mask, 0 to 255, seen through walls) and custom_depth
  (that renderer's depth). Its uv input picks the pixel (unlinked: this one);
  screen_position and screen_size (texel) build neighbour offsets for edges.
- An unlinked dynamic input takes one number for every component: a vector
  constant goes through a vector3 or color node.
- Run one with scene_set_post_process_materials_batch (below); at most 7
  textures.

```json
{"nodes": [
  {"id": "output", "type": "post_process_output", "inputs": [
    {"pin": "color", "link": "graded"}, {"pin": "opacity", "link": "strength"}],
   "settings": [{"name": "location", "value": "before_bloom"}]},
  {"id": "scene", "type": "scene_texture", "settings": [{"name": "source", "value": "scene_color"}]},
  {"id": "graded", "type": "desaturate", "inputs": [{"pin": "color", "link": "scene.rgb"}]},
  {"id": "strength", "type": "scalar_parameter", "inputs": [{"pin": "value", "value": [0.8]}],
   "settings": [{"name": "name", "value": "Strength"}]}
]}
```

Placing them:

- scene_place_decals_batch {"items": [{"material": key, "screen": [x, y]}]}
  (or origin + direction, or point + normal, per item) makes a Decal entity on
  the surface each ray hits, all in one undo step; size, depth, rotation and
  parent_id are optional. Edit them afterwards as the Decal component
  (components guide).
- scene_set_post_process_materials_batch {"items": [{"material": key}]} runs a
  post material on the scene's global volume (made when there is none); an
  item's entity_id names a volume (it fades with the volume's contribution) or
  a camera (always applies). weight, enabled, and parameters / textures
  override the material's values for that entry only.
  scene_remove_post_process_materials_batch takes entries off. Read them with
  scene_get_component_properties_batch component "Post Process Materials".
- material_graph_open on a decal or post material shows it in the window's
  3D preview: projected onto the shape, or over the preview's view.

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
- At most 15 textures (12 for decals, 7 for post-process materials) and 64
  parameter registers per graph.
