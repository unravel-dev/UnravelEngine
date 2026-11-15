# ECS reference

## Contents

- Bootstrap order
- Event subscribers and priorities
- Where components and systems live
- Scenes in a running editor
- Key types

## Bootstrap order

From `engine/engine/engine.cpp`. Check there before relying on a position; it is the source.

| Phase | Order |
|-------|-------|
| `engine::create` (`ctx.add<>`, constructors run) | logging, loading_screen, simulation, events, play_mode, threader, renderer, audio_system, asset_manager, ecs, rendering_system, surface_cache_system, transform_system, camera_system, reflection_probe_system, skylight_system, model_system, animation_system, rig_system, particle_system, physics_system, input_system, script_system, ui_system |
| `engine::init_core` | threader, renderer, audio_system, asset_manager |
| `engine::init_systems` | ecs, rendering_system, surface_cache_system (a failure is not fatal), transform_system, camera_system, reflection_probe_system, skylight_system, model_system, animation_system, rig_system, physics_system, particle_system, input_system, script_system, ui_system, play_mode, defaults |
| `engine::deinit` | defaults, play_mode, `ecs::unload_scene()`, ui_system, script_system, input_system, physics_system, rig_system, animation_system, model_system, skylight_system, reflection_probe_system, camera_system, transform_system, surface_cache_system, rendering_system, ecs, particle_system, asset_manager, audio_system, renderer, threader |

The add order puts particle_system before physics_system and the init order puts it after:
two slots at the same priority run in a different order depending on where each system
connects. That is why ordering contracts use `frame_update_priority`.

## Event subscribers and priorities

`hpp::event` runs slots by descending priority; `connect` without a priority uses 0.
Priorities below are what the code connects today (grep `\.connect(` to refresh).

| Event | Subscribers (priority) |
|-------|------------------------|
| `on_frame_begin` | renderer::frame_begin, script_system, ui_system (all 0) |
| `on_frame_update` | play_mode 10000, rendering_system::reset_script_draws 5000, editing_manager 1000, hub / runner (scene systems per scene) 0, audio_system 0, physics_system -500, script_system -1000, script late update -100000, physics late update -200000 |
| `on_frame_fixed_update` | script_system; raised once per fixed physics step with a game-time `delta_t` |
| `on_frame_before_render` | hub / runner -> `rendering_system::on_frame_before_render` per scene |
| `on_frame_render` | picking_manager 850 (editor), hub / runner 0, imgui_interface -100000 (editor UI) |
| `on_frame_end` | rendering_system 1000, renderer::frame_end 0, thumbnail_manager -10000 (editor) |
| `on_play_before_begin` | editing_manager 1000 (checkpoints, script domain reload), hub -998 |
| `on_play_begin` | editing_manager 2000 (loads the game scene when the splash is on), model_system 1000, animation / audio / physics 10, renderer 0, hub -999, script_system -1000, runner -100000 (the player loads its startup scene) |
| `on_play_end` | runner 100000, script_system 1000, input_system 0, animation / audio / physics / rendering -10 |
| `on_play_after_end` | renderer 0, hub -999, editing_manager -1000 (reloads every scene from its checkpoint) |
| `on_pause` / `on_resume` | script_system 100 / -100, animation / audio / physics 10 / -10 |
| `on_script_recompile` | editing_manager 1000 (checkpoint reload of every scene) |

Other events in `engine/engine/events.h`: `on_project_opened`, `on_os_event`,
`on_app_enter_background`, `on_app_enter_foreground`, `on_app_low_memory` (on Android no frame
runs between background and foreground). The editor's UI event `on_frame_ui_render` is in
`editor/editor/events.h`.

## Where components and systems live

| Area | Components | Systems |
|------|------------|---------|
| Core (id, tag, layer, transform, prefab) | `engine/engine/ecs/components/` | `engine/engine/ecs/systems/` |
| Rendering | `engine/engine/rendering/ecs/components/` | `engine/engine/rendering/ecs/systems/` |
| Physics | `engine/engine/physics/ecs/components/` | `engine/engine/physics/ecs/systems/` |
| Audio | `engine/engine/audio/ecs/components/` | `engine/engine/audio/ecs/systems/` |
| Animation, IK, rigs | `engine/engine/animation/ecs/components/` | `engine/engine/animation/ecs/systems/` |
| UI documents | `engine/engine/ui/ecs/components/` | `engine/engine/ui/ecs/systems/` |
| Scripting | `engine/engine/scripting/ecs/components/` | `engine/engine/scripting/ecs/systems/` |
| Meta and serialization of all of them | `engine/engine/meta/ecs/components/` | - |

`surface_cache_system` lives in `engine/engine/rendering/gi/`. `root_component` and
`active_component` are declared in `engine/engine/ecs/components/transform_component.h`.

## Scenes in a running editor

| Scene (tag) | Owner | Driven by |
|-------------|-------|-----------|
| `game` | `ecs::get_scene()` | `game_panel::on_frame_update` every frame |
| `scene_panel` | `scene_panel` (editor camera and editor-only entities) | `scene_panel::on_frame_update` while visible |
| `prefab_scene` | `editing_manager` (prefab mode) | `scene_panel` while in prefab mode |
| `prefab_diff_scene` | `inspector_context` | the inspector, for prefab comparisons |
| previews | material graph preview, thumbnail generation | their owners |

All of them are in `scene::get_all_scenes()` and all of them are reloaded from checkpoints on
play start, play end and script recompile.

## Key types

- `entt::handle` / `entt::const_handle`: entity plus registry. Prefer them over a bare
  `entt::entity`, which does not say which scene's registry it belongs to.
- `component_meta<T>` (`engine/engine/ecs/components/basic_component.h`): the `exists`, `add`, `remove`,
  `save`, `load` functions every component type registers.
- `layer_mask` (`engine/engine/layers/layer_mask.h`): render and collision filtering.
- `prefab_component`, `prefab_id_component`: instance roots and entity identity inside a
  prefab document. Their model and override paths: skill `unravel-prefabs`.
