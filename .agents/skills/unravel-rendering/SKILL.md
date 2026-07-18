---
name: unravel-rendering
description: >-
  Works on UnravelEngine rendering: bgfx deferred pipeline, render passes, shaders,
  materials, post-processing volumes, and GPU stats. Use for shader edits, pipeline
  changes, visual bugs, draw call issues, or GPU memory/eviction work.
disable-model-invocation: true
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
2. Rebuild project (CMake compiles shaders to `engine_data/compiled/shaders/`)
3. Shader includes: `bgfx_shader.sh`, `bgfx_compute.sh` synced by CMake
4. Do **not** edit `deps/3rdparty/bgfx/` unless absolutely necessary

Use `unravel-shader-change` for step-by-step shader edits.

## Volume / post-processing components

Post-FX are ECS volume components with priority and blend:

- `bloom_component`, `tonemapping_component`, `fxaa_component`
- `taa_component`, `assao_component`, `ssr_component`, `ssil_component`
- `auto_exposure_component`

Each has meta registration in `engine/engine/meta/ecs/components/`.

## Materials and draws

- `material` — `engine/engine/rendering/material.h`
- `model_component` / `submesh_component` — mesh rendering
- `batch_collector` — static mesh batching stats in `pipeline_stats`
- `layer_mask` — culling/filtering per pass

## Editor vs scene rendering

- Scene panel renders through deferred pipeline into viewport FBO
- Editor UI (ImGui) draws separately — excluded from scene primitive counts
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
