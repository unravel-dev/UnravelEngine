---
name: unravel-add-inspector
description: >-
  Procedure for adding a custom ImGui inspector for a reflected type in
  UnravelEngine. Use when default meta property drawing is insufficient for
  a component or asset type.
---

# Add Inspector Workflow

## Prerequisites

- Read `unravel-editor-panel` skill
- Type must be registered with EnTT meta (`REFLECT`)
- Understand whether editing needs undo support

## Step 1: Create inspector struct

**File:** `editor/editor/hub/panels/inspector_panel/inspectors/inspector_my_type.h`

```cpp
struct inspector_my_type : public crtp_meta_type<inspector_my_type, inspector>
{
    static auto draw(rtti::context& ctx, entt::meta_any& instance, const inspector_draw_context& draw_ctx) -> bool;
};
```

Follow existing inspectors: `inspector_math.h`, `inspector_physics_shape.h`.

## Step 2: Implement draw()

**File:** `inspector_my_type.cpp`

- Use ImGui widgets matching project style
- Read/write via `entt::meta_any` and meta data handles
- Return `true` if value changed
- For entity/component edits: route changes through undo actions

## Step 3: Auto-registration

`inspector_registry` discovers types derived from `inspector` via reflection at startup. Ensure:

- Inspector type is registered with meta/reflection system
- Linked in CMake for editor target

Check `inspectors.cpp` for registration patterns.

## Step 4: Wire to target type

Meta factory for the inspected type must map to this inspector. Follow how `inspector_vec3` maps to `math::vec3` etc.

## Step 5: Prefab override support

If inspector edits component properties on prefab instances:

- Use `property_path_generator` for serialization paths
- Record overrides via inspector panel prefab override API
- Pretty path for display, serialization path for matching

## Step 6: Icons and labels

- Component icons: `inspector_entity.cpp` -> `get_component_icon<T>()`
- Use `ICON_MDI_*` constants from material design icons header

## Verification

- [ ] Inspector renders for target type
- [ ] Edit triggers undo/redo
- [ ] Prefab override recorded when editing instance
- [ ] Reset to default works
- [ ] No ImGui ID collisions
- [ ] Serialization round-trip after edit

## When NOT to add custom inspector

Default meta drawing handles most primitives, enums, asset handles, and vectors. Only add custom inspector for:

- Compound shapes, curves, gradients
- Multi-field coupled UI
- Asset preview with special picking
