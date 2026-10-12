# Rendering Reference

## Pipeline execution flow (simplified)

```
rendering_system
  -> deferred::run_pipeline(scene, camera, render_view, dt, run_params, layer_mask)
    -> run_pipeline_impl(output, ...)
      -> geometry_pass (G-buffer)
      -> shadow_pass (if flagged)
      -> reflection_probe (if flagged)
      -> lighting / deferred resolve
      -> atmospheric (if flagged)
      -> post-process volumes (bloom, TAA, SSIL, etc.)
      -> particles_pass (if flagged)
      -> tonemapping / FXAA
```

## Key shader directories

| Directory | Contents |
|-----------|----------|
| `engine_data/data/shaders/deferred/` | G-buffer, lighting |
| `engine_data/data/shaders/shadows/` | Shadow map passes |
| `engine_data/data/shaders/post/` | Post-processing |
| `engine_data/data/shaders/particles/` | GPU particles |
| `engine_data/data/shaders/atmospheric/` | Sky / fog |
| `editor_data/data/shaders/` | Grid, outline, gizmo |

## Render view and framebuffers

- `gfx::render_view` - per-viewport render state
  - Named textures: `rview.tex_acquire(key, gfx::texture_desc{.size = s, .format = f, .flags = fl})`
    builds the texture and rebuilds it when the desc changes; `on_create` seeds contents or resets
    per-view state. Every call site of a key must pass the same desc, or the texture is rebuilt
    on every call.
  - Lookups: `tex_find` / `fbo_find` (null when missing), `tex_get` / `fbo_get` (asserted to exist).
  - Within-pass temporaries: `tex_acquire_transient(desc)` lends a pooled texture until the caller
    drops it; contents are undefined.
  - Framebuffers: `fbo_acquire(key)`, rebuilt with `gfx::needs_recreate(fbo, {textures})`, so a
    texture rebuilt at the same size is not left behind in the fbo.
- `gfx::frame_buffer` - render targets
- `camera` / `camera_component` - view/projection matrices
- `render_pass` - pass ID tracking (`gfx::render_pass::get_last_frame_max_pass_id()`)

## pipeline_stats fields (debugging)

From `rendering::pipeline_stats`:

- `drawn_models`, `drawn_static_submeshes`, `drawn_skinned_*`
- `drawn_lights`, `drawn_lights_casting_shadows`
- Shadow model/mesh counts (can exceed main pass due to cascades)
- `drawn_particles`, `drawn_particles_batches`
- `batching_stats` - efficiency, draw_calls_saved

Displayed in `viewport_stats_overlay.cpp` and profiler panel.

## Adding a new post-process volume

1. Component struct in `engine/engine/rendering/ecs/components/`
2. Meta in `engine/engine/meta/ecs/components/`
3. Pipeline integration in deferred pass chain
4. Shader if custom effect needed
5. Inspector icon in `inspector_entity.cpp`

Use `unravel-add-render-pass` for pipeline-level changes.
