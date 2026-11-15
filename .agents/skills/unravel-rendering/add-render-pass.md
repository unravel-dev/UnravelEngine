# Adding a render pass

A new pass in the deferred pipeline, the settings that drive it (a post-process volume component), its device
requirements, its debug view and its stats.

## Contents

- [Place it in the frame](#place-it-in-the-frame)
- [The pass object](#the-pass-object)
- [Device requirements](#device-requirements)
- [Settings: a post-process volume component](#settings-a-post-process-volume-component)
- [Debug view and stats](#debug-view-and-stats)
- [Verify](#verify)

## Place it in the frame

1. Pick the step of `deferred::run_pipeline_impl` it belongs to (frame table in [SKILL.md](SKILL.md)) by what it
   reads and what reads it: G-buffer consumers go after `render_geometry`, lighting inputs before
   `render_lighting`, image effects in `render_post_processing` (`pipeline_post.cpp`), overlays in
   `render_overlays`.
2. Decide which runs it: camera runs only (`run_context::is_camera_run()`), or reflection probe captures too
   (each captured face is a nested run with `pflags` built from 0 and no post stack).
3. Enable it by presence, as the others: a `fill_<pass>_params` callback in `pipeline::run_params`
   (`engine/engine/rendering/pipeline/pipeline.h`) filled by `pipeline::create_run_params`. A new
   `deferred::pipeline_steps` bit only for a step callers switch per run, with a `run_context` accessor beside
   `runs_shadow_step()`.
4. A pass whose output another pass reads hands it over explicitly in that pass's run params (as `velocity`),
   not through a render view lookup inside the consumer.

## The pass object

- Files `engine/engine/rendering/pipeline/passes/<name>_pass.h/.cpp`, a member of `pipeline`; `init(ctx)` loads its
  shaders and returns whether its programs loaded; `pipeline::init` reports it with `report_pass`, or
  `report_feature_pass` when it belongs to a `render_feature`.
- Uniforms first (`uniforms_cache::cache_uniforms`), then the `gpu_program`s (the OpenGL order contract).
  Programs a setting may turn off are created unwanted (`gpu_program(..., false)`) and follow the setting through
  `set_wanted`. Each frame's work starts with `program->begin()` and is skipped when it returns false.
- Targets come from the camera's `gfx::render_view`: `tex_acquire(key, desc, on_create)` with one description per
  key, `fbo_acquire(key)` rebuilt when `gfx::needs_recreate(fbo, {textures})`, temporaries from
  `tex_acquire_transient(desc)`. A pass that stops running removes its targets (`tex_remove`) when a stale one
  would still be read; otherwise `release_unused` drops them after the idle window.
- Views: one `gfx::render_pass` per bgfx view, the same set every frame (touch an idle one). Bind or clear the
  view, then set uniforms, textures and state; within a view, compute runs before draws.
- Compute goes through `gfx::dispatch` / `gfx::dispatch_indirect`; draws through `bgfx::submit` (indirect:
  `gfx::submit_indirect`).
- Run on another resolution than the G-buffer's: point-sample the full-resolution inputs, one source pixel per
  texel.

## Device requirements

A pass that needs compute, storage images, indirect work, independent blend or a format the device may lack:

1. Add a `render_feature` value and its row in the requirements table (`render_feature_support.cpp`): compute,
   indirect, bindings, storage images (format, dimension, access), render targets, dependencies and the shader
   prefixes it owns.
2. Check `get_render_feature_support().is_available(feature)` where the pass is enabled; turn the pass's
   `fill_*_params` off in `pipeline::apply_feature_support` when another feature takes over.
3. Mirror the enum in C# (`RenderFeature` in `engine_data/data/scripts/rendering/Graphics.cs`; static_asserts in
   `graphics_bindings.cpp` check the values).
4. Tag the component's meta type with `COMPONENT_RENDER_FEATURE_ATTRIBUTE` (`component_disables.h`) so the editor
   shows when it is unavailable.
5. Suites "render feature support" and "render feature shaders" (the second reads the committed compiled shaders:
   refresh them, [shader-change.md](shader-change.md)). Details: [gpu-features.md](gpu-features.md).

## Settings: a post-process volume component

1. Component in `engine/engine/rendering/ecs/components/` holding `enabled`, the pass's `settings` and a static
   `merge_into(result, from, contribution, is_first)` like `gtao_component`.
2. Meta and serialization in `engine/engine/meta/ecs/components/`, listed in `all_components.h`.
3. A `has_<x>` / settings pair in `resolved_post_process_settings` (`volume_resolver.h`) and its accumulation in
   `resolve_post_process_volumes` (`volume_resolver.cpp`).
4. In `pipeline.cpp`: the camera-entity fallback and the resolved-volume branch of `create_run_params` both fill
   `fill_<x>_params`; `apply_feature_support` drops it on a device without the feature.
5. In the scene presets: `engine/engine/defaults/defaults.cpp` (`set_volume_effect_enabled`) when new scenes get it.
6. Inspector icon in `editor/editor/hub/panels/inspector_panel/inspectors/inspector_entity.cpp`.
7. C# component, MCP typed properties, prefab override paths: the cross-cutting checklist of `AGENTS.md` and
   skill `unravel-ecs-component`.

## Debug view and stats

- A debug view: a `debug_pass_*` id in `deferred` (`pipeline/deferred/pipeline.h`), drawn where the pipeline
  draws its debug views, and an entry in `visualization_modes.h` / `visualization_modes.cpp` (static_asserts pin
  the ids). The Scene and Game panel menus and MCP `viewport_set_debug_view` list that table.
- Measurable work: a field in `pipeline_stats` (`pipeline.h`), shown by
  `editor/editor/hub/panels/viewport_stats_overlay.cpp` when user-visible.

## Verify

- The pass runs when enabled and is skipped when not; in probe captures only when intended.
- Ordering against shadows, lighting and post is right in the debug views.
- Scene view and Game view, edit and play mode; no frame buffer format assert in the log.
- `-r opengl` against `-r dx11`; a feature with requirements also with `--gfx-disable` naming what it needs
  (it must turn off cleanly with its reason shown).
- GPU cost of the new rows (skill `unravel-debugging`), and no doubled cost from a view drawn twice.
- Suites of the touched areas ([SKILL.md](SKILL.md), Verify).
