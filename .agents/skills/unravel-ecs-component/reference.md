# ECS Component Reference

## New component file checklist

| # | File | Action |
|---|------|--------|
| 1 | `engine/engine/ecs/components/my_component.h` | Define struct |
| 2 | `engine/engine/meta/ecs/components/my_component.hpp` | Extern macros |
| 3 | `engine/engine/meta/ecs/components/my_component.cpp` | REFLECT/SAVE/LOAD |
| 4 | `engine/engine/meta/ecs/components/all_components.h` | Add to both tuples + include |
| 5 | CMakeLists (engine) | Add `.cpp` if not globbed |
| 6 | `editor/.../inspector_entity.cpp` | Add icon in `get_component_icon<T>()` if custom icon wanted |
| 7 | `engine_data/data/scripts/` | C# mirror if script-exposed |

## Domain component locations

| Domain | Components path | Systems path |
|--------|-----------------|--------------|
| Rendering | `engine/engine/rendering/ecs/components/` | `engine/engine/rendering/ecs/systems/` |
| Physics | `engine/engine/physics/ecs/components/` | `engine/engine/physics/ecs/systems/` |
| Audio | `engine/engine/audio/ecs/components/` | `engine/engine/audio/ecs/systems/` |
| Animation | `engine/engine/animation/ecs/components/` | `engine/engine/animation/ecs/systems/` |
| UI | `engine/engine/ui/ecs/components/` | `engine/engine/ui/ecs/systems/` |
| Scripting | `engine/engine/scripting/ecs/components/` | `engine/engine/scripting/ecs/systems/` |

## Key types

- `entt::handle` / `entt::const_handle` - entity references
- `scene` - owns `entt::registry`, load/save
- `component_meta<T>` - meta factory helpers for add/remove/save/load
- `layer_mask` - render/collision filtering (`engine/engine/layers/layer_mask.h`)

## Event hooks used by systems

From `engine/engine/events.h`:

- `on_frame_update`, `on_frame_fixed_update`
- `on_frame_before_render`, `on_frame_render`
- `on_play_begin`, `on_play_end`, `on_pause`, `on_resume`
- `on_script_recompile`, `on_project_opened`

Connect with sentinel for auto-disconnect on system destroy.

## Prefab components

- `prefab_component` - instance root: asset link, slot identity (`instance_id` +
  `instance_document`), and the two per-author statement lists (`from_document`,
  `local`)
- `prefab_id_component` - entity identity within the document that introduced it
  (`{id, document}`)
- Details: skill `unravel-prefabs` (unified statement model)
