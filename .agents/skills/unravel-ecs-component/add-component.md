# Add a component

From an empty struct to a component that saves, loads, shows in the inspector, takes prefab
overrides and behaves in play mode. `my_component` (the type), `<name>` (its file name) and
`<area>` below are placeholders; copy
the structure of `test_component` (`engine/engine/ecs/components/test_component.h`,
`engine/engine/meta/ecs/components/test_component.hpp`, `.cpp`).

## Decide first

- Area folder: `engine/engine/<area>/ecs/components/` (table in [reference.md](reference.md)).
- Owned or not: `component_crtp<my_component, owned_component>` when the component needs its
  entity (`get_owner()`); plain `component_crtp<my_component>` otherwise.
- Which tuples: serializable (almost always), inspectable, addable from the menu.
- Per-frame work: none, a per-scene system (called from `rendering_system`) or a global one
  connected to `events`.
- Play behavior: does it act only while the simulation runs ([play-mode.md](play-mode.md))?
- Script-exposed or not (skill `unravel-scripting`).

## Steps

1. Struct in `engine/engine/<area>/ecs/components/<name>.h`, fields with defaults.
   Setters that change what other systems read set the relevant `dirty_ids` flags.
2. Meta header `engine/engine/meta/ecs/components/<name>.hpp`: include the component
   header, `<reflection/reflection.h>` and `<serialization/serialization.h>`, then
   `SAVE_EXTERN(my_component)`, `LOAD_EXTERN(my_component)`, `REFLECT_EXTERN(my_component)`.
3. Meta source `engine/engine/meta/ecs/components/<name>.cpp`:
   - `REFLECT(my_component)` with `.type("my_component"_hs)` and the attributes `name`
     (the save key: renaming it orphans the component in saved scenes), `category` (menu path, `/` nests) and
     `pretty_name` (menu and MCP name).
   - The five lifecycle functions: `.func<&component_meta<my_component>::exists>("component_exists"_hs)`,
     and likewise `add` / `component_add`, `remove` / `component_remove`, `save` /
     `component_save`, `load` / `component_load`.
   - One `.data<&my_component::field>("field"_hs)` per exposed field with
     `entt::attribute{"name", "field"}` plus the inspector attributes it needs (`tooltip`
     in a full sentence, `min` / `max` / `step`, ...; the list is in
     `unravel-editor-panel`, add-inspector.md). A field behind a getter and setter uses
     `.data<&my_component::set_x, &my_component::get_x>`.
   - `SAVE(my_component)` / `LOAD(my_component)`: `try_save` / `try_load` of every field under
     the same name as its `"name"` attribute.
   - `SAVE_INSTANTIATE` for `ser20::oarchive_associative_t` and `ser20::oarchive_binary_t`,
     `LOAD_INSTANTIATE` for `ser20::iarchive_associative_t` and `ser20::iarchive_binary_t`.
   - Nested structs get their own `REFLECT`, `SAVE` and `LOAD` in the same file.
4. `engine/engine/meta/ecs/components/all_components.h`: include `<name>.hpp` and add
   the type to `all_serializeable_components`, `all_inspectable_components` and, when users
   may add it, `all_addable_components`.
5. Hooks, when the component reacts to being created or destroyed: static
   `on_create_component(entt::registry&, entt::entity)` / `on_destroy_component` connected in
   `scene::scene` (`engine/engine/ecs/scene.cpp`). An owned component also connects
   `on_update` to `owned_component::on_create_component<my_component>`. Teardown that reads
   other components goes on `on_pre_destroy`, not `on_destroy`.
6. Systems: per-scene work is called from `rendering_system::on_frame_update` /
   `on_frame_before_render`; a global system connects events with a `frame_update_priority`
   value (SKILL.md, "New system").
7. New `.cpp` files are picked up by the source glob only after a CMake re-configure
   (`unravel-build-verify`).
8. Editor: an icon branch in `get_component_icon<T>()`
   (`editor/editor/hub/panels/inspector_panel/inspectors/inspector_entity.cpp`). The default
   inspector draws every reflected field; a custom inspector or a gizmo only when that is not
   enough (`unravel-editor-panel`, add-inspector.md).
9. C# mirror and native bindings, when scripts use it: skill `unravel-scripting`
   (`engine_data/data/scripts/scene/components/`,
   `engine/engine/scripting/ecs/systems/bindings/`).
10. A round-trip check for the new fields in `tests/tests/suites/ecs_serialization.cpp`.

## Done when

- The component is in the Add Component menu under its category, with its icon.
- Editing a field records one undo step; undo and redo restore it.
- Save, close and reopen the scene: every field keeps its value (look at the saved file: each
  field appears under its reflected name).
- On a prefab instance, editing a field marks it overridden, and reverting restores the
  prefab's value (`unravel-prefabs`).
- Entering and leaving play leaves no state behind in edit mode; with the splash on, nothing
  of the component runs during the splash.
- `unravel-tests --suite serialization` passes; `engine` and `editor` build.
