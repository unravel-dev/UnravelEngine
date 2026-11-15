---
name: unravel-ecs-component
description: >-
  UnravelEngine ECS on EnTT: components, reflected fields, systems, scenes, entity
  destruction, transforms, frame events and play mode. Use when adding or changing a
  component or field, wiring a system into the frame, destroying entities, loading scenes
  (additive, persistent), writing per-entity parallel passes, or changing behavior between
  edit, splash, running and paused play.
---

# Unravel ECS and components

Components, their meta and serialization, systems, scenes and the frame and play-mode
contracts. Owns `engine/engine/ecs/`, `engine/engine/meta/ecs/`, `engine/engine/play_mode.*`,
`engine/engine/events.h` and the bootstrap in `engine/engine/engine.cpp`. Domain components
live with their area (`engine/engine/<area>/ecs/`, see [reference.md](reference.md)); prefab
statements and override paths belong to `unravel-prefabs`, C# mirrors to `unravel-scripting`,
inspectors and undo to `unravel-editor-panel`.

## Gotchas

### Components, meta, serialization

- A component is listed in up to three tuples in
  `engine/engine/meta/ecs/components/all_components.h`: `all_serializeable_components` (scene
  and prefab save/load, cloning, checkpoints), `all_inspectable_components` (inspector,
  hierarchy search, gizmos, component icons) and `all_addable_components` (the Add Component
  menu and the MCP component tools). Core components the user cannot add (id, tag, layer,
  transform, prefab links) stay out of the last one.
- `SAVE` / `LOAD` write every field with `try_save` / `try_load` under the exact name the
  field is reflected with (`entt::attribute{"name", ...}`): prefab overrides point at fields by
  that name. An empty body saves nothing; nothing is serialized from reflection on its own.
- Instantiate all four archives: `SAVE_INSTANTIATE` for `ser20::oarchive_associative_t` and
  `ser20::oarchive_binary_t`, `LOAD_INSTANTIATE` for `ser20::iarchive_associative_t` and
  `ser20::iarchive_binary_t`.
- The type's `"name"` attribute is the key a scene stores the component under (`<name>` and
  `has_<name>`, `component_keys` in `engine/engine/meta/ecs/entity.cpp`). Renaming it orphans
  the component in every saved scene and prefab. `"pretty_name"` is what the Add Component menu
  shows and what MCP tools address the component by; `"category"` is its menu path, nested with
  `/` (`Rendering/Post Processing`).
- A renamed field keeps its old key as `entt::attribute{"former_name", "old_key"}`: prefab
  statements (`engine/engine/meta/ecs/components/prefab_component.cpp`) and MCP property
  lookups map it through `entt::find_renamed_property`, and the component's `LOAD` reads the
  former key when the new one is missing (`load_property` in
  `engine/engine/meta/ecs/components/particle_emitter_component.cpp`).
- A nested struct field needs its own `REFLECT` and `SAVE` / `LOAD` (`named_anim` in
  `engine/engine/meta/ecs/components/test_component.cpp`).
- Reflection registers at static initialization of any translation unit that includes the
  `REFLECT_EXTERN` header; a component's meta `.hpp` is included from `all_components.h`, which
  is how the editor, the player and the tests all see it.
- A field type that needs `==`, `<` or a hash gets it on the type as a hidden friend, never as a
  special case in one caller. `math::color` compares its float channels through its own
  `operator==`; without it `color == color` compiles through the clamping 8-bit `uint32_t`
  conversion.
- An owned component (`component_crtp<T, owned_component>`) is stamped with its owner by an
  `on_construct` hook and an `on_update` hook: `replace`, `emplace_or_replace` and `patch`
  assign a whole object, owner included, and fire only `on_update`.
- Registry hooks are connected per registry in `scene::scene` (`engine/engine/ecs/scene.cpp`).
  Every scene (game, Scene panel, prefab mode, previews, the prefab diff scene) has its own
  registry, so a hook connected only to `ecs::get_scene()` misses the others. Hooks that only
  hold during play (audio) connect in `on_play_begin` and disconnect in `on_play_end`.
- A new dirty-flag consumer takes a slot in `dirty_ids`
  (`engine/engine/ecs/components/basic_component.h`): one numbering of 32 slots shared by every
  component, so a slot means the same consumer wherever it is read.
- `basic_component.h` includes `ecs.h`, which includes `scene.h`: `scene.h` must not include a
  component header (`component_crtp` ends up undefined). Types both need go in a
  dependency-free header such as `engine/engine/ecs/scene_group.h`.

### Entity lifetime

- Destroy scene entities with `scene::destroy_entity`. A raw `registry.destroy()` or
  `handle.destroy()` skips the `on_pre_destroy` announcement, so contacts, scripts and other
  per-entity state get no chance to settle. Bulk teardown (unload, play end, scratch entities)
  runs inside `scene::scoped_destroy_suppression`.
- An `on_destroy` hook touches only its own component: EnTT removes the dying entity's
  components in reverse pool-registration order (whichever storage was created first), and
  adding or removing components on it is undefined behavior. Cross-component teardown subscribes to `on_pre_destroy(registry)` (`scene.h`),
  which runs children before parents with everything intact. A subscriber mutates its own state
  before notifying anyone and is idempotent: a slot may destroy the announced entity again.
- An invariant readers rely on ("an entity with X is listed in Y") is written where Y is
  established. A cleanup gated on a state transition misses the entities that never made that
  transition, and their stale state stays.
- Memory a later reader uses (bgfx creates buffers on its render thread, a job runs after the
  frame) is shared, not copied and not referenced raw: `gfx::eviction::backing_buffer` with
  `make_backing_ref` (`engine/core/graphics/eviction.h`) hands bgfx a reference that its
  release callback drops. A copy is the fallback only where the owner cannot hold a shared
  buffer.

### Frame order and systems

- Frame-event order between systems is `frame_update_priority` in `engine/engine/events.h`,
  higher first: `play_mode` 10000, `script_draws_reset` 5000, `editing` 1000,
  `scene_systems` 0, `physics` -500, `scripts` -1000, `scripts_late` -100000,
  `physics_late` -200000. A real ordering need gets a named value there. Same-priority order
  is registration order, which depends on whether a system connects in its constructor
  (`ctx.add<>` order) or in `init()` (init order); the two orders differ.
- Scene systems (transform, camera, model, animation, reflection probes, skylight, rigs,
  particles) do not connect to the frame events. `rendering_system::on_frame_update(scene&, times)` and
  `on_frame_before_render(scene&, times)` call them for one scene, and the hosts call those per
  scene at `scene_systems`: `game_panel` and `scene_panel` in the editor, `runner` in the
  player. A new per-scene system is called from there.
- Frame listeners take `const frame_times&` (`engine/engine/frame_times.h`) and pick the
  clock: `scaled` for gameplay, rendering, physics, animation and particles; `unscaled` for
  script unscaled time and audio fades; `real` for editor UI and editor timers. `scaled` and
  `unscaled` are zero on the first running frame and while paused; a frame step advances them
  by `play_mode::FRAME_STEP_SECONDS`. Frames outside the loop use `frame_times::make_uniform`.
- Systems connect slots with their `sentinel_` (`std::shared_ptr<int>`), which disconnects
  them when the system goes.

### Transforms and parallel passes

- Local transform setters skip the dirty mark only for a bit-identical value
  (`is_same_placement` in `transform_component.cpp`); global setters early-out within
  `math::epsilon<float>()`; physics passes its own epsilon to
  `set_position_and_rotation_global`. Do not add an epsilon to the local path: an incremental
  writer (root motion) can move less than any epsilon per step and would stop. Noise filters
  belong to a caller whose writes are absolute.
- Global transforms resolve lazily on first read, unsynchronized. Before a parallel pass whose
  tasks read `get_transform_global()` of entities that may share an ancestor, call
  `transform_component::resolve_shared_ancestors()` for each of them on the calling thread
  (`model_system.cpp`, `particle_system.cpp`).
- Parallel loops include `engine/core/concurrency/parallel.h` and nothing else:
  `poolstl::for_each_entity_par(parallel, view, fn)` for an EnTT view (its iterators are
  forward-only, which poolstl would re-walk per chunk), `poolstl::for_each_par_if` for
  containers. Never name `std::execution` or include `<execution>`; a loop inside a parallel
  range passes `false`.

### Scenes

- Play start, play end and every script recompile save and reload every scene in
  `scene::get_all_scenes()` from a checkpoint (`editing_manager.cpp`), editor-private scenes
  included. A kept `entt::handle` into any scene names nothing afterwards: check handles on each
  use and rebuild (`material_graph_scene_preview.cpp`), or find entities by component or by
  `id_component` uuid.
- Scene groups (`engine/engine/ecs/scene_group.h`) tag roots: 0 main, 1 persistent, 2 and up
  additive, never reused; a child follows its root. `load_from` and `reload` keep persistent
  roots (`unload_except_persistent`); `unload_group` and partial teardown go root by root
  through `destroy_entity`, so survivors get their contact exits.
- Entities loaded or instantiated while the simulation runs get their play begin from
  `on_load_callback` in `scene.cpp` (`rendering_system::on_play_begin(entities, dt)` and
  `script_system::on_play_begin(entities)`). Per-entity play-begin work of a new system goes
  into that path as well as into `on_play_begin`.
- `unravel-tests` runs `engine::create` and initializes only `threader` and `asset_manager`:
  suites load and instantiate with `call_callbacks = false`.

### Play mode

- Gate simulation work on `play_mode::is_simulation_running()` and `is_simulation_paused()`,
  never `is_active()` or `is_paused()`: `is_active()` is true during the splash, and
  `is_paused()` stays true during a frame step.
- `on_play_end` fires only when play reached `running`; a stop during the splash goes straight
  to `on_play_after_end`. Cleanup that must always run belongs in `on_play_after_end`.
- Details, event order and the editor facade: [play-mode.md](play-mode.md).

## How it works

- Bootstrap (`engine/engine/engine.cpp`): `engine::create` adds every service to the context
  (constructors run, some connect events), `init_core` initializes threader, renderer, audio
  and assets, `init_systems` initializes the ECS and the systems, then `play_mode` and
  `defaults`. `deinit` unloads the game scene while every system is still up, then tears down
  roughly in reverse. Exact lists: [reference.md](reference.md).
- One frame (`engine::process`): threader jobs, simulation tick, `frame_times` built (zeroed on
  the first running frame and while paused unless a step is pending), OS events,
  `on_frame_begin`, `seq::update(scaled)`, `on_frame_update`, `seq::update(0)`,
  `on_frame_before_render`, `on_frame_render`, `on_frame_end`, then the play-mode frame
  counters and the end of a frame step.
- Every `scene` owns an `entt::registry` and registers itself in `scene::get_all_scenes()`; its
  constructor connects the component hooks. The ECS service holds the game scene (tag `game`,
  `ecs::get_scene()`); the editor adds its own (Scene panel, prefab mode, previews).
- Scene save walks `all_serializeable_components` and writes `has_<name>` and `<name>` per
  present component; load reads them back in the same order and runs the load callbacks
  (`on_load<T>` buses, and `on_load_callback` while the simulation runs).
- Destruction: `scene::destroy_entity` announces the subtree on `on_pre_destroy`, then
  destroys; `transform_component::on_destroy_component` recurses into children with a raw
  destroy, already announced. `scene::unload` clears joints, ragdolls, bodies, character
  controllers and scripts before the untyped `clear()`.

## Procedures

- New component end to end: [add-component.md](add-component.md).
- Behavior that differs between edit, splash, running and paused: [play-mode.md](play-mode.md).
- New system:
  1. Add it to the context in `engine::create`, initialize it in `init_systems` and
     deinitialize it in `deinit` in reverse order, and remove it in `engine::destroy`.
  2. A per-scene system: call it from `rendering_system::on_frame_update` or
     `on_frame_before_render`, and from `rendering_system::on_play_begin` when it has
     per-entity play-begin work.
  3. A global system: connect frame events with a named `frame_update_priority` value when
     its order matters, and its play events at a priority that places it against
     `script_system` (play begin -1000, play end 1000; see [reference.md](reference.md)).
  4. Hooks on its components go into `scene::scene`.

## Verify

- `unravel-tests --suite serialization` ("ecs serialization / prefabs / cloning") for any
  component, field or SAVE/LOAD change; `--suite "scene groups"` for scene loading and
  groups; `--suite "physics contacts / destroy funnel"` for destruction order;
  `--suite "transform math"` and `--suite "render change"` for transform and dirty-flag work.
- Build `engine` and `editor`; build RelWithDebInfo too when a `.cpp` was added (unity
  batches, see `unravel-build-verify`).
- Editor smoke: add the component from the Add Component menu, edit a field and undo it, save
  and reopen the scene, override the field on a prefab instance, enter and leave play (with
  the splash on and off) and check nothing leaks into edit mode.

## Entry points

| File | Why |
|------|-----|
| `engine/engine/meta/ecs/components/all_components.h` | the three component tuples |
| `engine/engine/meta/ecs/components/test_component.cpp` | template for REFLECT / SAVE / LOAD |
| `engine/engine/ecs/components/basic_component.h` | `component_crtp`, `owned_component`, `dirty_ids` |
| `engine/engine/ecs/scene.h` | scenes, `destroy_entity`, `on_pre_destroy`, groups |
| `engine/engine/ecs/scene.cpp` | per-registry hooks, `on_load_callback`, unload order |
| `engine/engine/ecs/components/transform_component.h` | transforms, lazy resolve |
| `engine/engine/events.h` | events and `frame_update_priority` |
| `engine/engine/play_mode.h` | phases and simulation queries |
| `engine/engine/engine.cpp` | bootstrap order and the frame loop |
| `engine/engine/rendering/ecs/systems/rendering_system.cpp` | per-scene system driver |
