---
name: unravel-rendering
description: >-
  UnravelEngine rendering on bgfx: the deferred pipeline, render passes, .eshader shaders, render targets,
  shadows, post-processing volumes, GPU feature support, WebGL 2 / WebGPU limits and GI. Use when adding or
  changing a pass or shader, fixing a visual bug, a black or backend-only image or a shader compile error,
  working on draw calls or GPU cost, or touching engine/core/graphics.
---

# Unravel rendering

The renderer on bgfx: the deferred pipeline and its passes (`engine/engine/rendering/pipeline/`), the graphics
layer over bgfx (`engine/core/graphics/`), the engine and editor shaders (`engine_data/data/shaders/`,
`editor_data/data/shaders/`), shadows, particle drawing, GPU feature support and GI. Material graphs and `.mat`
files: skill `unravel-materials`. Builds and test runs: `unravel-build-verify`. Measuring GPU cost and A/B
protocol: `unravel-debugging`.

## Gotchas

Backends (OpenGL breaks first; the other backends hide these):

- Create a pass's uniforms (`uniforms_cache::cache_uniforms`) before its `gpu_program`. OpenGL wires a program
  only from uniforms registered before it links, and bgfx dedupes programs by shader pair, so a relink cannot
  repair it. Tell: "User defined uniform '...' is not found" in the log; samplers then sit on unit 0.
- An image (`IMAGE*`) never shares its name with a sampler uniform of any program (`i_*` or `*_out` names): the
  GL backend rebinds every registered uniform a program declares, so the image lands on that sampler's stage
  (unit 0 if never set) and reads or writes another texture, often in a different pass.
- Images bind at stages 0-7 (OpenGL has eight image units; a higher stage compiles and fails at runtime).
  Samplers and buffers may sit higher; stages are unique across a dispatch's images, samplers and buffers.
- A shader's image declaration and its `bgfx::setImage` access agree: `_RW` ReadWrite, `_RO` Read, `_WO` Write.
  D3D11 binds Read as a shader resource view, so a `_RW` slot bound Read reads zeros with no error. After
  changing a declaration, grep every `setImage` of that stage.
- 3D images bind with plain `bgfx::setImage`, without a layer range: with one, the GL backend binds a single slice
  of a one-mip volume and stores to the other slices are undefined (NVIDIA hides it; test another driver).
- Never sample a texture attached to the bound frame buffer: D3D11 unbinds the view silently and every read
  returns 0 (why the velocity pass has a color-only `VELOCITY_FBO_CAMERA`).
- Every backend renders with clip depth 0..1 and render targets whose first row is the top: OpenGL and WebGL 2
  get both from bgfx's `BGFX_CONFIG_GL_NORMALIZE_NDC_CONVENTIONS` (`deps/3rdparty/bgfx/CMakeLists.txt`), and the
  renderer refuses to start on a backend reporting others. A depth buffer holds clip depth, readbacks arrive top
  row first, and no engine code or shader branches on OpenGL for either. bgfx keys GL program binaries by the
  shaders before it rewrites them, so a change to that rewrite renames the GL cache folder (`renderer.cpp`).
- `gfx::render_pass::bind()` and `clear()` call `bgfx::touch`, which discards every uniform, texture, state and
  buffer set since the last submit: bind or clear the pass first, then set per-draw state.
- A bgfx view runs its blits, then all its compute dispatches, then its draws; `bgfx::update` of a dynamic buffer
  lands at frame start for every view. Work that must interleave takes separate views, and per-view CPU data
  takes a buffer per view.
- The last argument of `bgfx::submit` / `gfx::dispatch` is a `BGFX_DISCARD_*` mask: `true` compiles as
  `BGFX_DISCARD_BINDINGS`.
- `bgfx::makeRef` without a release callback needs the memory alive for the frame or two the render thread reads
  it; data an owner may free sooner goes through `gfx::eviction::backing_buffer` and the backing constructors of
  `vertex_buffer` / `index_buffer`. bgfx frees memory from `bgfx::alloc`, `copy` or `makeRef` only through the call
  it is handed to, so code given such memory always passes it on: a path that skips the create leaks it.
- A shader that samples a texture by UV with hardware mip selection binds it with `gfx::texture_read::any_level`
  (graph materials do, `graph_material_programs.cpp`): an evicted texture then binds the mips it keeps on the GPU
  and the draw never allocates. Any other read is `full` and, on the graphics API thread, brings the texture
  back to full size at once; other threads bind the copy there is, live until the end of the frame being built.
  `texture::info` always describes the full texture (eviction: `engine/core/graphics/eviction.h`).
- `is_valid()` on a texture or buffer stays true while paging holds it without a GPU copy (`has_gpu_copy()`
  says whether the copy exists): code that skips a draw on `!is_valid()` still reads it, and the read brings it
  back. A draw whose program samples a texture binds one or is not submitted: bgfx's Vulkan backend leaves an
  unbound sampler's descriptor unwritten, and NVIDIA loses the device on it.
- A loaded texture with a tail starts whole only while that keeps the GPU under the soft budget, else at its
  tail (`texture::admit_load_start`); the frame pass sharpens it when draws read it and there is room. A new
  load site asks `gfx::eviction::admit_allocation` and hands the reserved bytes to `register_resource`, so
  loads on several threads never pass the budget together.
- A readback is `bgfx::blit` into a `BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK` texture in a later view, then
  `bgfx::read`; every read drains the GPU on desktop backends, so none runs every frame.
- D3D12 creates a resource per texture update (batch into boxed region updates) and compiles pipelines at first
  use (cache dir: `gfx::set_cache_directory`, set in `renderer.cpp`). Vulkan stages uploads in 32 MB a frame.
- bgfx has no frame buffer without attachments (asserts), pixel-shader UAVs only from D3D11 feature level 11.1,
  and image atomics only as `imageAtomicAdd` (`bgfx_compute.sh`); other atomics go on buffers.
- An attribute in two vertex streams of one draw comes from the first stream on D3D11 and D3D12 and from the last
  on Vulkan and OpenGL. A second buffer that repeats attributes binds under a layout that skips them
  (`VertexLayout::skip`, the layout argument of `bgfx::setVertexBuffer`). `--gfx-check-vertex-streams` checks
  multi-stream draws, compute-written vertex buffers and CPU-updated ones on the device and logs the result.
- A morphed model draws from deform streams (`morph_deformer.h`, once a frame per scene after `model_system`):
  stream 0 its deformed position and basis, stream 1 its mesh under the rest layout, and in the velocity pass
  stream 2 last frame's positions as `a_texcoord7` for the `morph` variants of `vs_velocity` and of a graph
  material's velocity variants (`velocity_morph`, `velocity_skinned_morph`; `deferred::get_model_velocity_program`,
  `begin_model_offset_velocity_program`). Only the first LOD's mesh deforms: a draw of another LOD mesh binds no
  streams, so a program reading `a_texcoord7` must not draw it (`morph_deform_binding::deforms`). Every model draw
  gets them through `model_component::get_submit_extras`; a new draw path that builds its own extras draws the rest
  shape. Batches bind only the mesh buffer, so morphed models leave static, skinned and shadow batching. A model no
  view drew last frame (`was_used_last_frame`) is not deformed: it takes its shape the frame it enters a camera's
  frustum (`model_system::is_in_any_view`, as skinning's stale-pose entry does), with no motion of its own, and a
  frame late when only a shadow sees it. A mesh past 2^24 deltas, slots or vertices deforms on the CPU even with
  compute: the compute shaders read their counts from float uniforms. Vertex
  pulling (the editor's selection wireframe) reads positions from stage 2: the deform stream when a shader can read
  it (`deforms_readable`: compute wrote it), else the mesh's own buffer; the CPU path's streams have no shader view.
- A mesh's bounds hold each morph target alone at weight 1. While weights are active a model's world bounds grow by
  `get_morph_bounds_padding` (each target's reach times its weight's size) and its submeshes are culled by that box
  alone (`get_submit_extras` withholds the proxies), as for World Position Offset. The editor's raycasts
  (`scene_raycast.cpp`) test the morphed positions through `apply_morph_position_deltas`, the CPU deform's own sums.
- A compute shader reads or writes a vertex buffer through `BUFFER_RAW_*` and `rawLoad*` / `rawStore*`: D3D11 gives a
  vertex buffer only a float4 typed view. bgfx updates no `BGFX_BUFFER_COMPUTE_WRITE` buffer from the CPU.

Pipeline, targets and draws:

- Feature code decides from `gfx::get_device_caps()`, never `bgfx::getCaps()`: a `--gfx-disable` mask reaches only
  the snapshot. Details: [gpu-features.md](gpu-features.md).
- Every caller of a `render_view` key passes the same `texture_desc` and `auto_collect`, or the texture is rebuilt
  on every call. Owners purge their views (`camera_component`, `reflection_probe_component` call
  `release_unused`); a target whose absence its reader cannot detect takes `auto_collect = false`. A render view
  is one camera's isolation boundary: no global render target pool.
- G-buffer reads go through the `DecodeGBuffer*` functions and structs of `lighting.sh` (Texel / Lod variants,
  `ConstructGBuffer`), never raw fetches of the G-buffer targets.
- The velocity buffer is the layout `velocity_encoding.sh` states (RG total uv motion, B object-only motion in
  pixels, A last frame's view depth). Consumers get it through their run params (a valid texture is the
  enable), classify object motion from B, and never rebuild camera motion from their own matrices.
- The G-buffer and velocity mains are shared (`vs_deferred_geom.sh`, `vs_velocity.sh`) and must raster the same
  position for the velocity pass's EQUAL depth test: change both or neither. Movers leave static batching while
  the velocity buffer runs.
- A reduced-resolution pass reads the full-resolution G-buffer point-sampled, depth and normal from one source
  pixel; a linear fetch averages normals and depths across edges.
- A pass submits a fixed set of views every frame (touched empty when idle): bgfx reports a view's GPU time by
  view id from an older frame, so a changing layout shifts every later row.
- `math::vec3` is 16 bytes (`GLM_FORCE_DEFAULT_ALIGNED_GENTYPES`): hashing or copying it with `sizeof` takes the
  padding (a "changed" detector then fires every frame). Hash the components; GPU records use `float[3]`.
- The engine is left-handed: view-space z is positive into the screen (`computeViewSpacePosition`,
  `screenSpaceToViewSpaceDepth`), materials cull CCW, so a front face winds clockwise on screen. A debug
  primitive takes its cull state from that rule and is checked from both sides.
- A fade or confidence built from view-space dot products normalizes its vectors first.
- A temporal accumulator's composite shows the running mean; it never mixes the current frame back in by the
  history count.
- A light's position and axes come from `light_component::get_light_frame(global)`: it shines along the frame's +z,
  its source lies along +y. Every reader (lighting, shadows, fog, the GPU light list, the sun, culling, the editor's
  gizmo) goes through it. Never `reset_scale()` a light's transform: the decomposition keeps a mirror as negative
  scale, and resetting it turns the light around.
- Shadow biases are in texels (`light.h`). The cascade a receiver reads (the smallest crop containing it,
  `fs_pbr_lighting.sh`) and the nested-cascade caster culling (`shadow.cpp`) are one contract: change both.
- An overlay that must win over coplanar geometry (the editor grid) biases by a few ulps plus a fraction of a
  pixel of depth slope (`max(abs(dFdx(d)), abs(dFdy(d)))`), not a constant in device depth; check D3D and GL.
- The World Normal debug view writes the normal unremapped: surfaces facing -x, -y or -z read black.
- `particle_eval.h` mirrors `particle_common.sh` (suite "particle motion" compares the constants); change both.

Shaders (procedure and per-language traps: [shader-change.md](shader-change.md)):

- The C++ build compiles no shader. The editor compiles the copy deployed into the runtime tree, and a failed
  compile keeps the old binary with no visible error: check with suite "eshader files" and the editor log.
- A matrix element read by index differs per language: GLSL and GLSL ES read `m[column][row]` of the same memory
  that HLSL, SPIR-V, Metal and WGSL read `m[row][column]`; split with `#if` as `screenSpaceToViewSpaceDepth`
  (`shaderlib.sh`) does. Diagonal elements read the same.
- fxc evaluates both sides of `&&`, `||` and `?:`: guard an expensive call with an explicit `if`.
- No derivative of a `texelFetch` result after a per-pixel `discard` or early return (fxc moves the fetch below
  it and drivers read dead lanes): difference explicit neighbour fetches (`GeometricSpecularAATexel`).
- No constant table indexed at run time: SPIR-V, GLSL and WGSL copy it per pixel (suite "shader constant tables").
- A debug view leaves production shader code byte-identical: debug paths live in the debug shader or behind a
  define, never in a reshaped shared helper.
- OpenGL rejects what every other backend accepts, only at run time and only by dropping the pass: an image
  named like any sampler uniform, an image at stage 8+, GLSL keywords as names, `rsqrt`. Two scripts find them
  offline: `scripts/scan_gl_image_names.py` (sources) and `scripts/validate_gl.py` (compiled GLSL), see
  [shader-change.md](shader-change.md#opengl-checks).

GI ([gi.md](gi.md) holds the contracts, layouts, quality rules and measuring):

- `gi_constants.h` / `gi_pass_constants.h` own the GI constants; their `.sh` mirrors match row for row (suite
  "gi constants").
- A GI view owns its reflections and AO: SSR steps aside for the GI reflections, GTAO / ASSAO for the GI
  short-range AO, and the indirect pass takes `GI_RESOLVE` over SSIL by its presence alone.
- `camera::get_frustum()` is TAA-jittered: a cache keyed on it never holds with TAA on; key on the unjittered
  projection.

## How it works

Frame (`deferred::run_pipeline_impl`, `engine/engine/rendering/pipeline/deferred/pipeline.cpp`): each step is a
member taking the `run_context`; the steps live in `pipeline_geometry.cpp`, `pipeline_lighting.cpp`,
`pipeline_post.cpp`, `pipeline_shadows.cpp` and `pipeline_gi.cpp`.

```text
begin_camera_frame            stats, instanced sources (camera runs only)
capture_reflection_probes     each due probe face is a nested run of the pipeline
prepare_passes                shadow maps, pre-exposure, GI scene, targets, TAA jitter
render_geometry               G-buffer of the decal receivers, decals, the other renderers; velocity buffer,
                              screen-space AO, the post-process mask
render_reflection_layers      probe layer (PBUFFER), Hi-Z, SSR (RBUFFER)
render_lighting               cloud shadow, irradiance SH, direct lights, GI or SSIL, indirect
render_sky_fog_and_particles  sky, fog, particles, GI world views
finish_camera_image           post (TAA, exposure, bloom, tonemapping, FXAA) with the post-process materials at
                              their Locations, overlays, upscale, PREV_DEPTH
                              (a probe capture blits its lit image to the output instead)
finish_run_batches            packs the skinning palette rows the run reserved
```

- A probe face is a whole nested run, dozens of render passes, and a frame has 4095 views (`render_pass`
  flushes the frame part-way when it runs out). `reflection_probe_system::bake_due_probes` drives every bake:
  faces come from one budget every camera of the frame spends (`probe_face_budget`, refilled each frame by the
  renderer from `graphics.reflection_probe_faces_per_frame`), probes without a capture first, then whole bakes,
  then refreshes, and a face starts only while the frame has render passes left for one
  (`render_pass::get_passes_left`). A view on the engine's defaults (thumbnails, the graph preview:
  `run_params::shares_probe_face_budget` off) bakes its probes whole within that room, so its picture has
  resolved reflections. Where the GI reflections own a view, only Environment probes are baked and drawn there
  (`is_used_in_view`); a probe some view left out and none used is set aside as if disabled (capture released,
  no bake pending, "Not used" in the inspector) and bakes whole when a view uses it again.
- `run_params` (`engine/engine/rendering/pipeline/pipeline.h`) carries the run: `run_type` (camera or probe
  capture), `pflags` (`deferred::pipeline_steps`; probe captures build theirs from 0), `limits`
  (`feature_limits`) and one `fill_<pass>_params` callback per optional pass, whose presence enables it.
  `pipeline::create_run_params` fills them from the post-process volumes (`resolve_post_process_volumes`) or the
  camera entity's components, then `apply_feature_limits` and `apply_feature_support` turn off what the project
  or the device does not allow; `apply_view_params` (`rendering_system.cpp`) applies the view's limits.
- The passes are members of `pipeline` (`engine/engine/rendering/pipeline/passes/`), loaded in `pipeline::init`
  and reported per `render_feature`. Each `gfx::render_pass` is one bgfx view; views run in id order.
- Render targets live in the camera's `gfx::render_view`: `tex_acquire(key, desc, on_create)` builds and
  rebuilds by description, `tex_find` / `fbo_find` return null when missing, `tex_get` / `fbo_get` assert,
  `tex_acquire_transient(desc)` lends a pooled texture with undefined contents, and
  `gfx::needs_recreate(fbo, {textures})` rebuilds a frame buffer when an attachment's identity changed.
- Calls into bgfx go to bgfx directly (`bgfx::setState`, `bgfx::setTexture`, `bgfx::setImage`, bgfx types too);
  `gfx::` free functions exist only where the engine adds behavior: `gfx::frame` / `gfx::frames` (eviction sees
  every frame), `gfx::dispatch`, `gfx::dispatch_indirect` and `gfx::submit_indirect` (skip what the device
  snapshot cannot run; bgfx checks only in debug builds). The `gfx::set_uniform` / `gfx::set_texture` overloads
  taking `gfx::program::uniform_ptr` are in `gpu_program.h`.
- Programs of features the settings may turn off build only while wanted (`gpu_program::set_wanted`; create
  them unwanted with `gpu_program(..., false)`); the object stays, so pointers to it stay valid.
  `gpu_program::begin()` rebuilds from reloaded shaders: gate a frame's work on it, not on `is_valid()`.
- Decals (`decal_pass`) draw between the two G-buffer phases (`g_buffer_phase`): renderers that do not receive
  decals draw after them, into the same G-buffer without a clear, and the velocity pass reads both phases' batches.
  The colour path blends into RT0 and RT2; the full path accumulates DBUFFER_A/B/C and resolves them over the
  decals' rectangle (the octahedral normal cannot blend in hardware). Decals read a depth copy, never the bound
  depth. Receiver Layers draw the receivers' layers through `mask_pass` (visible surfaces only).
- Post-process materials (`post_material_pass`) run per Location from the run's `post_process_materials`
  (`resolve_post_process_materials`: volume entries weighted by contribution, entries of one material blended,
  camera entries last), ping-ponging between two targets of the location's format; the last writes back through a
  colour-only frame buffer, since the location's own may hold the depth the materials read.
- Post-processing settings are ECS volume components (`engine/engine/rendering/ecs/components/`) merged by
  priority and blend weight in `volume_resolver.cpp`; a camera without volumes uses its own components.
- The editor's Scene and Game panels run the same pipeline into viewport frame buffers; ImGui, gizmos and the
  stats overlay draw outside it and are not in the scene's counts. Classify a visual bug first: pipeline
  output, editor overlay, or play-mode Game view.

Indirect occlusion contract (each indirect term takes only the occlusion it has not resolved itself;
`pbr_indirect` in `fs_pbr_lighting.sh`, helpers in `lighting.sh`):

- `PBUFFER` holds the untraced layer (probes, then the GI rough tier) unoccluded: rgb premultiplied, alpha the
  union coverage (blend `ONE, INV_SRC_ALPHA`); the uncovered rest is the environment SH (`CompleteProbeLayer`).
  `RBUFFER` holds the traced layers (GI reflection hits, then SSR), cleared to (0,0,0,1) and blended with alpha
  `ZERO, INV_SRC_ALPHA`, so alpha is the share left to the probe layer. The indirect pass composes
  `SO.traced * RBUFFER.rgb + RBUFFER.a * SO.untraced * completed PBUFFER` (`ComposeIndirectSpecular`). A new
  traced layer blends into `RBUFFER` the same way; a new untraced source goes into `PBUFFER` with the union
  alpha. Neither buffer ever holds occlusion.
- Specular occlusion is GTSO (`SpecularOcclusionGTSO`, the table of `specular_occlusion_lut`; suite "specular
  occlusion") with the multi-bounce fit on F0 (`ComputeIndirectSpecularOcclusion`).
- Diffuse: the SH takes material x screen AO over the visibility cone (`eval_irradiance_sh_cone`); the GI
  resolve takes both; SSIL takes the material AO only. Multi-bounce applies once, to the combined visibility
  (`MultiBounceAOGain`). Direct light takes no AO. Screen AO is GTAO or ASSAO, never both
  (`run_screen_ao_pass`), read through `get_screen_ao_inputs`, and never written into the G-buffer.
- The screen AO's radius is the artist's setting: no other system derives or clamps it.

## Procedures

- Add a pass, a post-process volume or a debug view: [add-render-pass.md](add-render-pass.md).
- Edit or add a shader, deploy it into a running editor, refresh the committed binaries:
  [shader-change.md](shader-change.md).
- Device capabilities, `--gfx-disable`, WebGL 2 and WebGPU, shader buffers, skinned draws:
  [gpu-features.md](gpu-features.md).
- Render change tracking and shadow map caching: [change-tracking.md](change-tracking.md).
- GI changes: [gi.md](gi.md).
- Update bgfx: move the bgfx.cmake fork (`deps/3rdparty/bgfx/bgfx`, whose `CMakeLists.txt` pins bgfx, bx and bimg)
  to upstream bgfx.cmake first; move its three pins together only for a fix newer than upstream's. The fork's
  commit and push and the engine's submodule pointer are the user's. Patches re-apply at the next configure and
  stop it when one fails (rules: `deps/AGENTS.md`). Bump `get_format_version<gfx::shader>`
  (`engine/engine/assets/impl/asset_extensions.h`) when `BGFX_SHADER_BIN_VERSION` changes.

## Verify

| Change | Suites (`unravel-tests --suite <name>`) |
|--------|------------------------------------------|
| Any shader | "eshader files"; compute shaders also "render feature shaders", "shader constant tables" |
| Feature requirements, device caps | "render feature support", "render feature shaders", "feature limits" |
| Render targets, `render_view` | "render view" |
| Culling, batching, instancing | "render culling", "instanced draws", "skinning palettes" |
| Material properties, change keys | "render change", "shadow caching", "material instances" |
| Shadows | "shadow atlas", "shadow caching", "render culling" |
| Exposure, specular occlusion, fog | "pre-exposure", "specular occlusion", "fog" |
| Particles | "particle motion", "particle emitter desc" |
| GI | see [gi.md](gi.md) |

- Build `engine`, `editor` and `tests` in the configured tree (`unravel-build-verify`), deploy `engine_data`.
- Check the Scene and the Game view, edit and play mode, and a reflection probe capture when the pass runs in
  probe runs.
- New or renamed images, samplers or uniforms, or a shader change that reaches OpenGL: run both OpenGL check
  scripts ([shader-change.md](shader-change.md#opengl-checks)).
- New or renamed shader resources, or a pass that rasterizes into an atlas: run the editor with `-r opengl` and
  compare the debug views with `-r dx11` (G-buffer views first, then lighting). `-r` takes dx11, dx12, vulkan or
  opengl.
- A feature with a fallback: run with `--gfx-disable=<names>` ([gpu-features.md](gpu-features.md)).
- A swap chain, fence or frame-pacing change: run the editor on that backend with panels floated into their own
  windows (several swap chains), enter and leave play mode, resize. A report names what was not run.
- Draw counts and batching: the viewport stats overlay (`pipeline_stats`).

## Entry points

| Purpose | Path |
|---------|------|
| Frame steps, run context | `engine/engine/rendering/pipeline/deferred/pipeline.h`, `engine/engine/rendering/pipeline/deferred/pipeline_internal.h` |
| Run params, passes, volumes | `engine/engine/rendering/pipeline/pipeline.h`, `engine/engine/rendering/pipeline/volume_resolver.h` |
| Passes | `engine/engine/rendering/pipeline/passes/` |
| Programs, uniform order | `engine/engine/rendering/gpu_program.h` |
| bgfx layer: targets, views, caps | `engine/core/graphics/render_view.h`, `engine/core/graphics/render_pass.h`, `engine/core/graphics/device_caps.h`, `engine/core/graphics/graphics.h` |
| Feature requirements | `engine/engine/rendering/render_feature_support.h` |
| Shadows | `engine/engine/rendering/shadow.h`, `engine/engine/rendering/light.h` |
| Renderer, launch options | `engine/engine/rendering/renderer.cpp` |
| Shared shader code | `engine_data/data/shaders/lighting.sh`, `engine_data/data/shaders/fs_pbr_lighting.sh`, `engine_data/data/shaders/shaderlib.sh` |
| Debug views, stats overlay | `editor/editor/hub/panels/visualization_modes.h`, `editor/editor/hub/panels/viewport_stats_overlay.cpp` |
