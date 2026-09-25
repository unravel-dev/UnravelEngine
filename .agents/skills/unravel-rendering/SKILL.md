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
  texture. Symptom: the auto exposure history ring wrote into the irradiance SH through
  unit 0 (GL-only blue SSIL flash every 256 frames).
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
  derive or clamp it from another system's settings (e.g. the GI probe spacing, as Lumen's
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
- `layer_mask` - culling/filtering per pass

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
