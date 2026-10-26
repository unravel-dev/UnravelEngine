---
name: unravel-rendering
description: >-
  Works on UnravelEngine rendering: bgfx deferred pipeline, render passes, shaders,
  materials, post-processing volumes, GI, clouds, shadows, and GPU stats. Use for
  shader edits, pipeline changes, visual bugs, draw call issues, GI/probe work,
  or GPU memory/eviction work.
---

# Unravel Rendering

## Start here

| Purpose | Path |
|---------|------|
| Renderer service | `engine/engine/rendering/renderer.cpp` |
| Rendering system | `engine/engine/rendering/ecs/systems/rendering_system.h` |
| Deferred pipeline | `engine/engine/rendering/pipeline/deferred/pipeline.h` |
| Pipeline base | `engine/engine/rendering/pipeline/pipeline.h` |
| Pipeline stats | `engine/engine/rendering/pipeline/pipeline.h` (`pipeline_stats`) |
| Graphics core | `engine/core/graphics/` |
| Shader sources | `engine_data/data/shaders/` |
| Compiled shaders | `engine_data/compiled/shaders/` |
| Editor gizmo shaders | `editor_data/data/shaders/` |
| Post-process volumes | `engine/engine/rendering/ecs/components/*_component.h` (bloom, taa, ssil, etc.) |

## Deferred pipeline

`rendering::deferred` extends `pipeline`. Pass control via `pipeline_steps` bitmask in `run_params::pflags`:

| Flag | Purpose |
|------|---------|
| `geometry_pass` | G-buffer / main geometry |
| `shadow_pass` | Shadow maps |
| `reflection_probe` | Reflection probe capture |
| `atmospheric` | Sky / atmospheric scattering |
| `particles_pass` | GPU particles |
| `full` | All passes |

Implementation: `engine/engine/rendering/pipeline/deferred/pipeline.cpp`

Do not add passes without understanding stage ordering in `run_pipeline_impl`.

## Shader workflow

1. Edit `.sc` in `engine_data/data/shaders/`
2. `cmake --build build/<Config> --target engine_data` - this only **copies** sources
   into the runtime tree; the editor's asset importer compiles via `shaderc` at import
   time. Validate offline with the in-tree `shaderc.exe` (`s_5_0` + `spirv`) - a
   failed compile silently keeps the stale binary
3. Shader includes: `bgfx_shader.sh`, `bgfx_compute.sh` synced by CMake
4. Do **not** edit `deps/3rdparty/bgfx/` unless absolutely necessary

Use `unravel-shader-change` for step-by-step shader edits.

## GPU contracts (hard-won - violating these costs days)

- **GL uniform order:** on OpenGL, create bgfx uniforms BEFORE programs. Symptom of
  breaking it: "uniform not found" warnings, samplers silently landing on unit 0
  (black passes / invalid draws).
- **GL 3D image binds:** plain `setImage` binds 3D textures non-layered on GL (Mesa
  drops image3D stores, NVIDIA hides it). Always use `gfx::set_image_3d`
  (`engine/core/graphics/graphics.h`).
- **GL image names:** never name an image (`IMAGE*`) like any sampler uniform - use `i_`
  names. The GL backend uploads every registered uniform a program declares, so the image
  is rebound to that sampler's last stage (unit 0 if never set) and reads / writes another
  texture. It happened twice: the auto exposure history ring wrote into the irradiance SH
  through unit 0 (GL-only blue SSIL flash every 256 frames), and the GI gather / radiance
  cache images (`s_gi_*`, sampled under the same names by the next pass) turned the
  gather and the reflections to garbage, so the GL lit image went black.
  `python tasks/scan_gl_image_names.py` finds these and the next trap.
- **GL image units:** images bind at stages 0-7 only. Stage 8+ compiles offline and fails
  at runtime on GL; samplers and buffers may go higher.
- **GL rows and depth:** bgfx runs GL with `originBottomLeft` and homogeneous depth
  (`BGFX_CONFIG_GL_NORMALIZE_NDC_CONVENTIONS` is 0): view rects and scissors count rows from
  the bottom, and clip depth spans -1..1. A pass that rasterizes tiles and reads them back by
  texel (`texelFetch` / `imageLoad` at pixel coordinates) must mirror its rect or scissor and
  flip y (and map depth) in its projection on GL - see
  `gi_surface_cache_pass::compute_capture_view`. Screen passes sampled by UV need nothing.
- **GL depth range is not `gfx::is_homogeneous_depth()`:** that function is the engine's
  projection convention and always returns false - every projection is zero-to-one, so on GL
  a depth buffer holds 0.5 + 0.5 z and nothing in front of z = -w is clipped; shaders that
  read a camera depth undo this with `toClipSpaceDepth`. A projection whose depth is read raw,
  or that must clip at its near plane, takes the range from `bgfx::getCaps()->homogeneousDepth`.
  The card capture once keyed off the engine flag: GL card depths shifted toward the card's
  back, the lighting started its shadow and radiosity rays inside walls, and the GL GI scene
  read about half as bright as D3D11 (the lit image 13% darker).
- **Verify on GL:** after adding or renaming shader resources, or adding a pass that
  rasterizes into an atlas, run the sandbox with `--renderer=opengl` and compare the debug
  views with D3D11 (G-buffer views first, then lighting). `tasks/validate_gl.py` checks only
  the compiled GLSL's syntax, not runtime bindings.
- **D3D12:** allocates per texture update - batch updates into boxes. PSOs compile at
  first use - the disk cache is wired via `gfx::set_cache_directory`.
- **Vulkan:** scratch buffer is 32MB/frame - large per-frame uploads can exhaust it.

## bgfx calls vs gfx:: functions

- Call bgfx directly (`bgfx::setState`, `bgfx::setTexture`, `bgfx::dispatch`, ...). Do not add
  pass-through wrappers to `engine/core/graphics/graphics.h`.
- Use bgfx types directly too (`bgfx::TextureHandle`, `bgfx::ViewId`, `bgfx::Memory`,
  `bgfx::Access::Write`, ...); do not add `using` aliases for bgfx types to `gfx::`.
- `gfx::` free functions are engine code only: they add behavior or change the API, e.g.
  `gfx::init` / `gfx::frame` / `gfx::shutdown` (eviction bookkeeping), `gfx::set_image_3d`,
  `gfx::clip_quad`. The prefix tells whether a call is ours or bgfx's.
- Advance frames through `gfx::frame()` / `gfx::frames()` so eviction sees every frame.
- The last argument of `bgfx::submit` / `bgfx::dispatch` is a `BGFX_DISCARD_*` mask, not a
  bool: `true` compiles as `BGFX_DISCARD_BINDINGS` and `false` as `BGFX_DISCARD_NONE`. Model
  submesh callbacks map `preserve_state` to `BGFX_DISCARD_NONE`, otherwise `BGFX_DISCARD_ALL`.
- The `gfx::set_uniform` / `gfx::set_texture` overloads taking `gfx::program::uniform_ptr` live
  in `engine/engine/rendering/gpu_program.h`; raw bgfx handles go straight to bgfx.

## Indirect occlusion contract

Each indirect term takes only the occlusion it has not resolved itself
(`pbr_indirect` in `fs_pbr_lighting.sh`, helpers in `lighting.sh`):

- **Reflection buffers.** `PBUFFER` = the untraced layer (probes, then the GI rough tier),
  drawn unoccluded, rgb premultiplied and alpha the UNION coverage (alpha blend
  `(ONE, INV_SRC_ALPHA)`) - the GI reflection trace reads it as the open sky, and the uncovered
  rest is the environment SH (`CompleteProbeLayer`, UE's sky-light fill). `RBUFFER` = the traced
  layers (GI reflection hits, then SSR), cleared to (0,0,0,1) and blended with alpha
  `(ZERO, INV_SRC_ALPHA)`, so alpha is the share left to the probe layer. The indirect pass
  composes `SO.traced * RBUFFER.rgb + RBUFFER.a * SO.untraced * completed PBUFFER`
  (`ComposeIndirectSpecular`). A new traced layer blends into `RBUFFER` the same way; a new
  untraced source goes into `PBUFFER` with the same union alpha. Never bake occlusion into
  either buffer.
- **Specular occlusion** is GTSO: `SpecularOcclusionGTSO` reads the 32^3 table from
  `specular_occlusion_lut` (owned by `default_textures`; suite `specular occlusion`), with the
  multi-bounce fit on F0 (`ComputeIndirectSpecularOcclusion`; UE, HDRP and Filament do the same).
- **Diffuse:** the SH takes material x screen AO over the visibility cone
  (`eval_irradiance_sh_cone`); the GI resolve takes both (it resolves visibility only at its
  probe lattice); SSIL takes the material AO only (`u_indirect_params.y`). Multi-bounce applies
  once, to the combined visibility, on the uncapped diffuse albedo (`MultiBounceAOGain`).
  Direct light takes no AO.
- **The screen AO's range is the artist's** (GTAO / ASSAO radius and screen radius): never
  derive or clamp it from another system's settings (e.g. the GI probe spacing, as the GI's
  short-range AO does) - document the trade-off on the setting instead.

## GI subsystem

`engine/engine/rendering/gi/` - voxel/probe GI with SDF tracing, surface cache, and
its own shader constants mirror (`gi_constants.h` / `gi_constants.sh` must not drift -
the `gi constants` test suite enforces parity). Before GI work, read the plan docs:
`tasks/gi_perf_plan.md`, `tasks/gi_rewrite_plan.md`. Shadow bias model:
`tasks/shadow_bias_plan.md`. Clouds: `tasks/clouds_audit.md`.

Test suites: `unravel-tests --suite "gi constants"` and `--suite "gi bake"` (the
latter also shader-compiles every GI shader).

## Volume / post-processing components

Post-FX are ECS volume components with priority and blend:

- `bloom_component`, `tonemapping_component`, `fxaa_component`
- `taa_component`, `assao_component`, `gtao_component`, `ssr_component`, `ssil_component`
- `auto_exposure_component`

Each has meta registration in `engine/engine/meta/ecs/components/`.

## Materials and draws

- `material` - `engine/engine/rendering/material.h`
- `model_component` / `submesh_component` - mesh rendering
- `batch_collector` - static mesh batching stats in `pipeline_stats`
- Skinned mesh batching (`graphics.skinned_mesh_batching`): skinned submeshes instance by (mesh, material,
  LOD, submesh) in the G-buffer, velocity and shadow passes; the bones live in `skinning_palette_buffer`
  (one upload per frame, read by every view; shaders include `skinning_palette.sh`, buffer at stage 7).
  The G-buffer and velocity skinned shaders must call the same `skin_palette_matrix` (EQUAL depth test).
  Editor picking / outline / wireframe keep the per-draw `u_world` palette path.
- `layer_mask` - culling/filtering per pass

## Render change tracking

Caches decide what changed through one vocabulary, `engine/engine/rendering/render_change.h`. Plan and results:
`tasks/render_change_tracking_plan.md`.

- `render_change` aspects: object (placement, pose, geometry, membership) and material (coverage, sidedness,
  albedo, normal, specular, occlusion, emission). A consumer reads only its `render_interest` mask
  (`shadow_caster`, `gi_card_capture`, `gi_card_direct`, `gi_field`, `velocity`).
- Materials: `material_property_id` + `get_material_property_traits` (material.h) is the single table of each
  property's aspects and whether it is per-instance; the reflection attributes "material_property" and
  "per_instance" come from it (`make_material_property_attributes`). Setters compare first and call
  `mark_changed`; `get_revision(mask)`, `get_state_revision(mask)`, `get_instance_values_revision(mask)` sum
  only the asked aspects. Texture content (`asset_handle::version`) enters through `get_content_version(mask)` /
  `add_change_keys`.
- Models: `model_system` writes `model_component::get_change_stamps()` once per frame (the last frame of each
  aspect). Inputs: the transform `dirty_ids::render_change` slot (placement), `update_armature` (pose, only when
  matrices or mapping differ), `touch(reasons)` from the component setters, the LOD mesh identities, the material
  change keys. `set_model` diffs the swap (`model::get_changes_from`), so the editor's whole-model override path
  stamps only the aspects that moved.
- Adding a material property: add the enum value and its traits row, wrap its reflection attributes, call
  `mark_changed` from the setter. The "render change" test suite fails for a reflected property without traits.
- Adding a consumer: define its `render_interest` mask and read `get_last_change(mask)` /
  `has_changed_since` / `has_changed_within`, or key on `get_revision(mask)` for per-material caches.
- A change that reaches no setter (a new input outside the component) must `touch` the model component.

## Shadow map caching

`graphics.shadow_map_caching` (`shadowmap_generator`, `engine/engine/rendering/shadow.cpp`): a spot or point
light's map, or a cascade of the sun, renders again only when its key changes. Plan and results:
`tasks/shadow_caching_plan.md`.

- Every input of a map must reach a key: `compute_view_settings_key` (epoch, map size, light type and
  implementation, view / projection matrices, atlas placement serial) or `compute_caster_facts` (entity, LOD,
  LOD mesh, the model's last change in `render_interest::shadow_caster`). A new per-object input goes through
  render change tracking (above); a global change calls `shadow::invalidate_cached_maps()`.
- An atlas tile placed again, even on the same square, has a new placement serial: another light may have drawn
  there in between.
- The sun's reach test follows the camera at submesh level, so its cascade keys also take the collected batches'
  content hash; local lights key on caster facts alone.
- Static copy: a local light in the atlas with moving casters in reach restores its casters that hold still from a
  depth-only copy (`shadow_atlas::get_static_frame_buffer`, `fs_shadowmaps_restore.sc` rebuilds the R32F colour
  from depth, InvZ only) and draws only the movers. A caster is a mover while
  `has_changed_within(render_interest::shadow_caster, MOVER_SETTLE_FRAMES, frame)`.
- Verify a change with caching on vs off on a paused frame: the captures must match.

## Editor vs scene rendering

- Scene panel renders through deferred pipeline into viewport FBO
- Editor UI (ImGui) draws separately - excluded from scene primitive counts
- Stats overlay: `editor/editor/hub/panels/viewport_stats_overlay.cpp`
- Gizmo rendering: `editor/editor/hub/panels/scene_panel/gizmos/`

When fixing visual bugs, identify whether the issue is pipeline output, editor overlay, or play-mode game view.

## GPU memory / eviction

- Stats via `bgfx::getStats()` and `gfx::eviction::get_stats()`
- Profiler panel: `editor/editor/hub/panels/profiler_panel/`
- Use `unravel-profiler-debug` for GPU timeline investigation

## Verification checklist

- [ ] Shaders recompiled (check `engine_data/compiled/shaders/`)
- [ ] Scene and game view both correct
- [ ] Play mode rendering matches edit mode expectations
- [ ] No regression in draw calls / batching stats
- [ ] Layer mask culling still correct
- [ ] Post-process volumes compose in correct order

## Common mistakes

- Editing compiled `.asset.*` blobs directly instead of `.sc` source
- Breaking `pipeline_steps` bitmask semantics
- Scene draws leaking into wrong render target
- Forgetting meta registration for new volume component
- Modifying bgfx third-party instead of engine abstraction

## Deep reference

See [reference.md](reference.md) for pass flow and shader paths.
