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
- **D3D12:** allocates per texture update - batch updates into boxes. PSOs compile at
  first use - the disk cache is wired via `gfx::set_cache_directory`.
- **Vulkan:** scratch buffer is 32MB/frame - large per-frame uploads can exhaust it.

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
- `taa_component`, `assao_component`, `ssr_component`, `ssil_component`
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

- Stats via `gfx::get_stats()` and `gfx::eviction::get_stats()`
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
