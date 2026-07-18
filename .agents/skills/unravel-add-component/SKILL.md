---
name: unravel-add-component
description: >-
  End-to-end procedure for adding a new ECS component to UnravelEngine including
  meta, serialization, inspector, and optional C# API. Use when creating a new
  component type from scratch.
disable-model-invocation: true
---

# Add Component Workflow

Complete checklist for a new ECS component. Follow in order.

## Prerequisites

- Read `unravel-ecs-component` skill
- Identify domain folder (core vs rendering/physics/audio/etc.)
- Decide: owned component? script-exposed? prefab-overridable?

## Step 1: C++ component struct

**File:** `engine/engine/<domain>/ecs/components/my_component.h`

```cpp
struct my_component : public component_crtp<my_component>  // or component_crtp<my_component, owned_component>
{
    // fields with sensible defaults
};
```

## Step 2: Meta extern header

**File:** `engine/engine/meta/ecs/components/my_component.hpp`

```cpp
SAVE_EXTERN(my_component);
LOAD_EXTERN(my_component);
REFLECT_EXTERN(my_component);
```

## Step 3: Meta implementation

**File:** `engine/engine/meta/ecs/components/my_component.cpp`

Copy from `test_component.cpp`:

- `REFLECT(my_component)` with factory type, category, pretty_name
- `.func<>` for component_exists/add/remove/save/load
- `.data<>` for each field with name attribute
- `SAVE` / `LOAD` with both archive instantiations

## Step 4: Register in all_components.h

**File:** `engine/engine/meta/ecs/components/all_components.h`

1. `#include "my_component.hpp"`
2. Add to `all_serializeable_components` tuple
3. Add to `all_inspectable_components` tuple

## Step 5: System hooks (if needed)

If component needs lifecycle logic:

- Add `on_create_component` / `on_destroy_component` static methods
- Register in relevant system's `init()` via `registry.on_construct/on_destroy`
- Or create new system if substantial per-frame logic

## Step 6: CMake

Ensure `my_component.cpp` is in engine target sources (glob or explicit list).

## Step 7: Inspector icon (optional)

**File:** `editor/editor/hub/panels/inspector_panel/inspectors/inspector_entity.cpp`

Add branch in `get_component_icon<T>()` with appropriate `ICON_MDI_*`.

## Step 8: C# API (if script-exposed)

**Files:** `engine_data/data/scripts/scene/components/`

- Mirror component fields as C# properties
- Native glue (when needed):
  1. Add `engine/engine/scripting/ecs/systems/bindings/<type>_bindings.cpp`
  2. Implement `internal_m2n_*` using helpers from `bindings/script_glue_common.h` (`safe_get_component`, etc.)
  3. Expose `void register_<type>_script_bindings()` and declare it in `bindings/script_bindings.h`
  4. Call that register from `script_glue.cpp` (`script_system::bind_internal_calls`)
  5. Match the C# `DllImport` / internal-call name strings exactly

## Step 9: Custom inspector (only if default meta UI insufficient)

Use `unravel-add-inspector` for complex types.

## Verification

Run full checklist from `unravel-ecs-component`:

- [ ] Add component via inspector menu
- [ ] Edit fields, undo works
- [ ] Save scene, reload, values persist
- [ ] Prefab instance override on a field
- [ ] Play mode enter/exit behavior correct
- [ ] Build succeeds (engine + editor)

## Done criteria

Component appears in inspector Add Component menu, serializes correctly, and behaves correctly in play mode.
