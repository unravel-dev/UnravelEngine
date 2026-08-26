---
name: unravel-ecs-component
description: >-
  Implements and modifies UnravelEngine ECS components, systems, scene lifecycle,
  and meta registration. Use when adding or editing components, entity systems,
  scene serialization, EnTT signals, or component ownership patterns.
---

# Unravel ECS Component

## Start here

| Purpose | Path |
|---------|------|
| Component definitions | `engine/engine/ecs/components/` |
| Domain components | `engine/engine/rendering/ecs/components/`, `physics/`, `audio/`, `animation/`, `ui/`, `scripting/` |
| Component base / CRTP | `engine/engine/ecs/components/basic_component.h` |
| Scene | `engine/engine/ecs/scene.h` |
| ECS service | `engine/engine/ecs/ecs.h` |
| Canonical component lists | `engine/engine/meta/ecs/components/all_components.h` |
| Meta registration | `engine/engine/meta/ecs/components/*.hpp` + `*.cpp` |
| Reference implementation | `test_component` in `engine/engine/ecs/components/test_component.h` |

## Component patterns

### Standard component

```cpp
struct my_component : public component_crtp<my_component>
{
    // fields...
};
```

Use `component_crtp<T, owned_component>` when the component needs `get_owner()` / `set_owner()`.

### System hooks

Register in system `init()`:

```cpp
entt::registry& r = ctx.get<ecs>().get_registry();
r.on_construct<T>().connect<&T::on_create_component>();
r.on_destroy<T>().connect<&T::on_destroy_component>();
```

Static callbacks live on `owned_component` or the component type.

**`on_destroy` caveat (hard rule):** an entt `on_destroy` hook must NOT touch other
components of the dying entity - pool destruction order is arbitrary and the access is
UB. For cross-component teardown, connect to the `on_pre_destroy` bus instead
(`engine/engine/ecs/scene.h`): `scene::destroy_entity` is the single destruction
funnel and announces the whole subtree - children before parents, everything still
intact - BEFORE `registry::destroy` runs. Never call `registry.destroy()` directly on
scene entities; it skips the announcement. Listeners must be idempotent.

### Component lists

After adding a component, update **both** tuples in `all_components.h`:

- `all_serializeable_components` - scene save/load
- `all_inspectable_components` - inspector add-component menu

Include the meta header in `all_components.h`.

## Meta registration (required for editor)

Each component needs three files:

1. `engine/engine/meta/ecs/components/my_component.hpp` - `SAVE_EXTERN`, `LOAD_EXTERN`, `REFLECT_EXTERN`
2. `engine/engine/meta/ecs/components/my_component.cpp` - `REFLECT`, `SAVE`, `LOAD` macros
3. Register `.data<>()` for each exposed field with `entt::attribute{"name", "field_name"}`

Factory must include component lifecycle funcs:

```cpp
.func<&component_meta<T>::exists>("component_exists"_hs)
.func<&component_meta<T>::add>("component_add"_hs)
.func<&component_meta<T>::remove>("component_remove"_hs)
.func<&component_meta<T>::save>("component_save"_hs)
.func<&component_meta<T>::load>("component_load"_hs)
```

Copy structure from `engine/engine/meta/ecs/components/test_component.cpp`.

## Serialization

- SAVE/LOAD bodies can be empty if all fields are reflectable primitives
- Instantiate both archive types: `ser20::oarchive_associative_t`, `ser20::oarchive_binary_t`
- Nested structs need their own `REFLECT` block (see `named_anim` in test_component)

## Scene and prefab impact

- Scene entities serialize through component meta `save`/`load`
- Prefab instances use `serialization::path_context` for override matching
- Do not break serialization paths for existing prefab overrides

## System bootstrap order

Systems init in `engine/engine/engine.cpp`. New systems:

1. Add to context: `ctx.add<my_system>()`
2. Call `init(ctx)` in correct order relative to dependencies
3. Connect `events` with explicit priority when order matters

## Frame ordering

`frame_update_priority` in `engine/engine/events.h` is the contract for
`on_frame_update` slot order (higher runs earlier): `play_mode` (10000) ->
`editing` (1000) -> `scene_systems` (0, incl. transforms/animation/physics/audio;
order within the band is deliberately non-contractual) -> `scripts` (-1000, so
script code sees the evaluated animation pose) -> `scripts_late` (-100000).
Anything with a real ordering requirement must use a named priority from that
struct - same-priority order depends on whether a system connects from its
constructor or `init()`, which no caller should have to reason about.

## Verification checklist

- [ ] Component in `all_serializeable_components` and `all_inspectable_components`
- [ ] Meta REFLECT/SAVE/LOAD registered
- [ ] `unravel-tests --suite serialization` green (covers save/load + prefabs + cloning)
- [ ] Scene save and reload preserves fields
- [ ] Inspector shows component (add/remove works)
- [ ] Prefab instance override works for changed fields
- [ ] `on_create_component` / `on_destroy_component` fire correctly
- [ ] Play mode enter/exit does not leak state

## Common mistakes

- Forgetting `all_components.h` tuple entry
- Meta `.hpp` not included in `all_components.h`
- `registry.destroy()` instead of `scene::destroy_entity` (skips `on_pre_destroy`)
- Touching sibling components from an `on_destroy` hook (UB - see caveat above)
- Using raw `entt::entity` instead of `entt::handle` for ownership
- System logic in component that belongs in a system
- Missing play mode reset in `on_play_end`

## Deep reference

See [reference.md](reference.md) for file checklist and domain paths.
