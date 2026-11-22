# GPU features and devices

How a rendering feature decides whether the device runs it, how to test fallbacks on a desktop, what WebGL 2 and
WebGPU change, and the shader buffers and skinned draws that keep vertex shaders within the weakest devices.

## Contents

- [Device snapshot](#device-snapshot)
- [Feature requirements](#feature-requirements)
- [Testing fallbacks on a desktop](#testing-fallbacks-on-a-desktop)
- [WebGL 2 and WebGPU](#webgl-2-and-webgpu)
- [Shader buffers](#shader-buffers)
- [Skinned draws](#skinned-draws)

## Device snapshot

- `gfx::get_device_caps()` (`engine/core/graphics/device_caps.h`) is taken once after bgfx starts, masked by
  `--gfx-disable`; features decide from it, never from `bgfx::getCaps()`. It answers `can_write_image(format,
  dimension)`, `can_read_image`, `can_read_write_image(format)` (R32 formats need compute only),
  `can_render_to(format, samples)`, `can_generate_mips(format)`, `can_filter(format)`, and the flags
  `has_compute`, `has_draw_indirect`, `has_dispatch_indirect` (follows `has_compute`: bgfx checks compute for
  it), `has_image_read_write`, `has_blend_independent`, `has_vertex_storage_buffers`, `has_depth_blit`, with
  `max_compute_bindings` and `max_storage_textures_per_stage`.
- Minimum requirements (compute, RGBA16F and D32F targets, 4 color attachments, R32F image writes) are checked on
  the real device at init (`get_missing_minimum_requirements`); a device without them gets a message box. On
  OpenGL ES (WebGL 2) compute is a feature, not a minimum (`gfx::is_compute_required`).
- Vulkan reports `has_draw_indirect` only with multi-draw indirect and a first instance in the arguments; many
  phones lack it, and the indirect paths then take their CPU fallback.

## Feature requirements

- `render_feature` (`engine/engine/rendering/render_feature_support.h`) lists what each feature needs: compute,
  indirect draw or dispatch, independent blend, compute bindings, storage images (format, dimension, access, a
  WebGPU format when it differs), render targets, the features it depends on and the compute shaders it owns.
  `get_render_feature_support()` holds the resolution with a reason sentence for each unavailable feature, and
  `runs_shader` drops the compute shaders of features the device does not run.
- An image declared `IMAGE*_RO` is a texture binding on D3D and needs no row entry; a read through a read-write
  binding is a typed UAV load on D3D (`image_access::read`).
- Where it applies: `apply_view_params` (`rendering_system.cpp`) ANDs the device limits into the project's
  `feature_limits` in every view; `pipeline::apply_feature_support` turns GTAO (ASSAO takes over), ASSAO and auto
  exposure off; the other features check `is_available` where they decide to run.
- A new compute pass lists its storage images and binding count in its row: suite "render feature shaders" reads
  the committed compiled shaders and fails a binding, format, dimension or access the row does not list, and an
  image at stage 8 or above. Keep every stage below 8 for a pass that must run at D3D11 feature level 11_0.
- The editor shows unavailable features under the Allowed Features rows, on components tagged with
  `COMPONENT_RENDER_FEATURE_ATTRIBUTE` and in the profiler's GPU Resources; MCP `editor_get_status` has a
  `graphics` block.

## Testing fallbacks on a desktop

- `--gfx-disable=<names>` masks the snapshot: `compute`, `draw_indirect`, `dispatch_indirect`, `image_rw`,
  `blend_independent`, `typed_image_loads`, `extended_storage_formats`, `vertex_storage_buffers`, `depth_blit`,
  `bindings<count>` (`bindings8`), `format:<name>:<write|read|render|msaa|mips|all>`. Compute programs fail to
  build under `compute`, as on a real device.
- Device profiles: D3D11 feature level 11_0 is `bindings8,image_rw,typed_image_loads`; a phone without
  multi-draw is `draw_indirect`; WebGL-like vertex shaders are `vertex_storage_buffers` (also with `-r opengl`).
- Run the Scene and Game views and the debug views with the mask, and check the unavailable features show their
  reason.

## WebGL 2 and WebGPU

- The web player ships both bgfx renderers; `--graphics webgpu|webgl2` picks one. The page chooses it
  (`game/web/wwwroot/graphics.js`, tested by `node --test game/web/tests/graphics.test.mjs`) from the manifest's
  `graphics` block and passes `--graphics-fallback <reason>` (`renderer::get_graphics_fallback_reason`, C#
  `Graphics.fallbackReason`); `?graphics=webgl2` or `?graphics=webgpu` in the page address forces one where the
  build ships it. `RenderFeature` and `GraphicsFallbackReason` in `Graphics.cs` mirror the engine's enums by value
  (static_asserts in `graphics_bindings.cpp`).
- WebGL 2 shaders are GLSL ES 3.00; bgfx's GL renderer treats a failed compile as fatal, so variants needing
  ES 3.1 and all compute shaders are cooked without code. Every vertex variant stays within 256 uniform vectors.
- ANGLE links every program at creation, at a cost growing faster than the shader: the deferred lighting loads
  only the light programs the effective limits allow (`pipeline::update_light_programs`, re-run when the limits
  change; `contact_shadows`, `subsurface`, `soft_shadows` and `max_shadow_filtering` decide which exist, and
  `limit_light` clamps lights to them).
- Depth copies go through `depth_copy_pass`: a blit where `device_caps::has_depth_blit`, else a draw that writes
  depth (bgfx's OpenGL ES blit copies through a color attachment, which fails for depth).
- OpenGL ES samples depth textures (without comparison) and 32-bit float textures (without
  `OES_texture_float_linear`) only point-filtered, else they read zero; bgfx forces point sampling for them.
- To debug a WebGL frame, read each frame buffer's color attachments back from the page (a browser init script
  wrapping `framebufferTexture2D` and the draw calls) rather than guessing from the final image.
- WebAssembly is 32-bit: `size_t` is 32 bits there, so code that aligns or masks a 64-bit value through `size_t`
  drops its top half. bgfx's shared dynamic vertex and index buffers carry their buffer handle in that top half of a
  64-bit address (bx aligns it in 64 bits); a 32-bit cast puts every CPU-updated dynamic buffer of the web player in
  handle 0's buffer (WebGL 2: `bufferSubData: buffer overflow`). Desktop and Android are 64-bit.
  `--gfx-check-vertex-streams` on the page (`?arg=--gfx-check-vertex-streams`) shows it.
- WebGL 2 through ANGLE on Direct3D 11 counts `gl_VertexID` from an indexed draw's lowest index when it streams the
  vertices (buffers the CPU updates); a shader that needs a vertex's index reads it from an attribute.
- A hidden browser pane renders no frames, so the engine never runs there: check a web build in headless Edge with a
  profile folder of its own (`--headless=new --user-data-dir=<scratch> --enable-logging=stderr`, the page's
  console lines arrive as `CONSOLE` lines); ANGLE's first frame there can take about a minute.
- WebGPU: storage-texture and workgroup limits are the core ones (shader-side checks:
  [shader-change.md](shader-change.md)); texture readback in the browser completes from the browser's event loop: without Asyncify, which the web
  player does not use, bgfx maps it with spontaneous callbacks instead of waiting.

## Shader buffers

- `shader_buffer` (`engine/engine/rendering/shader_buffer.h`, `shader_buffer.sh`) is data a vertex shader reads by
  index: a storage buffer, or without `render_feature::vertex_storage_buffers` (WebGL 2, GLES without vertex
  storage buffers) an rgba32f / rgba32u / r32u texture of `SHADER_BUFFER_TEXTURE_WIDTH` (2048) texels a row read
  with `texelFetch`. Bone palettes, baked animation rows and particle data use it.
- A shader declares `SHADER_BUFFER_FLOAT4` / `_UINT4` / `_UINT` and reads with `shader_buffer_*`. Its texture twin
  variant defines `SHADER_BUFFER_TEXTURES` and is named `<variant>_textures` (`textures` for `default`):
  `gpu_program` draws with the twin on texture storage, so programs name the plain variant (particles pick theirs
  with `get_shader_buffer_variant`; their storage is their own). Suite "eshader files" requires a twin for every
  vertex variant WebGL 2 cannot compile.
- Texture storage needs `shader_buffer::init()` before its programs are created (GL wires samplers at link time).
  A draw binds storage holding every row reserved so far; growth retires the old storage once the frame is done.
- Particles without `render_feature::particle_storage_buffers` (no compute, or no vertex storage buffers) read
  textures through `vs_particle.eshader`'s `*_textures` variants (`ps_soa::get_particle_data_storage`), and the
  CPU writes the records, culls and sorts.

## Skinned draws

- Skinned batching (`graphics.skinned_mesh_batching`) instances skinned submeshes by (mesh, material, LOD,
  submesh) in the G-buffer, velocity and shadow passes. Every skinned draw, batched or not (editor picking,
  outline and wireframe too), reads its bones from `skinning_palette_buffer`: one upload a frame, read by every
  view, `skinning_palette.sh` at stage 7. An instance carries its first row; a single draw gets it from
  `u_skin_rows` (`skinning_palette_buffer::bind(first, previous)`, rows acquired by `model_submit_extras::owner`).
- The G-buffer and velocity skinned shaders call the same `skin_palette_matrix` (EQUAL depth test).
  `u_world` / `u_prev_world` hold one matrix.
- `skinning_palette_buffer::acquire` reserves rows keyed by the owner entity; `pack_pending` fills them at the end
  of each pipeline run (`deferred::finish_run_batches`), before the editor can change the models, so the palettes
  stay unchanged until then. A draw outside a run uses `acquire_packed`.
- One palette per submesh, at most `gfx::get_max_blend_transforms()` (128) bones; `mesh::apply_skin_to_load_data`
  splits a submesh reaching more bones into pieces whose palettes fit.
- The skinned shaders take the blended bone matrix as affine (no divide by w), so `apply_skin_to_load_data` keeps
  each vertex's four strongest weights scaled to sum to one; a sum off one scales positions about the world
  origin.
- Skinned batching programs follow `batch_collector::is_skinned_mesh_batching_enabled()`; shadow programs are
  requested per run (`Programs::request` in `shadow.h`: VSM ones by soft shadows, skinned batching ones by
  batching, wished for over this and the last frame).
