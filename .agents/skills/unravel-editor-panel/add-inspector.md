# Add an inspector

How a reflected type gets its rows in the inspector, which meta attributes shape them, and how
to write a custom inspector (or a gizmo) when attributes are not enough. `my_type` and
`inspector_my_type` are placeholders, `<name>` is the file name.

## Contents

- Attributes first
- Custom inspector
- Undo, overrides, search
- Gizmo for a component
- Verify

## Attributes first

The default inspector draws every reflected field of a type as a `property_layout` row, and
handles numbers, strings, enums, colors, vectors, asset handles, arrays, maps and nested
structs. Most needs are met by attributes on the field's `.data<>` registration
(`entt::attribute{"key", value}`):

| Attribute | Effect |
|-----------|--------|
| `name` | save key and override path segment; a rename keeps the old one in `former_name` |
| `pretty_name` | row label; falls back to `name` |
| `tooltip` | row tooltip, written in full sentences |
| `hint` | `property_predicate_t<std::string>`: a warning line drawn under the row while non-empty |
| `min`, `max`, `step` | numeric widget limits and drag speed |
| `format` | numeric display (`"size"` with `data_format` shows a byte size) |
| `predicate` | `property_predicate_t<bool>`: the row shows only while it returns true |
| `readonly_predicate` | `property_predicate_t<bool>`: the row is read-only while it returns true |
| `header` | a section title row above the property |
| `group`, `group_foldable`, `group_collapsed` | rows sharing a `group` form a titled section; either flag on any of them makes it a foldout, folded at first |
| `foldable`, `collapsed` | a struct field becomes a framed foldout instead of a titled section |
| `flattable` | a struct field's rows join the list untitled; a `bool` only, the string `"true"` is ignored |
| `element_label` | array elements read `<label> N` |
| `readonly_count` | the first N array elements are neither removed nor moved |
| `is_fixed_size_array` | no adding or removing elements |
| `empty_hint` | the text an empty array shows |
| `paired_property` | names a sibling array drawn under each element (one list; no add, remove or move) |
| `multiline`, `wrap` | string fields |
| `former_name` | the key a renamed field had; older files, prefab statements and MCP still find it |

On the type itself: `pretty_name` is the component header and menu name, `category` its Add
Component menu path. Readers: `editor/editor/hub/panels/inspector_panel/inspectors/inspectors.cpp`
(`inspect_property`), `inspector_array.cpp`, `inspector_coretypes.cpp`,
`engine/core/reflection/reflection.cpp`.

## Custom inspector

Write one only for compound widgets (curves, gradients, shapes), fields that must be edited
together, previews, or extra rows a component shows besides its fields.

1. Header `editor/editor/hub/panels/inspector_panel/inspectors/inspector_<name>.h`:

   ```cpp
   struct inspector_my_type : public crtp_meta_type<inspector_my_type, inspector>
   {
       auto inspect(rtti::context& ctx,
                    entt::meta_any& var,
                    const meta_any_proxy& var_proxy,
                    const var_info& info,
                    const entt::meta_custom& custom) -> inspect_result override;
   };

   REFLECT_INSPECTOR_INLINE(inspector_my_type, my_type)
   ```

   The macro registers the inspector with its `inspected_type`; `inspector_registry`
   (`inspectors.cpp`) collects every type derived from `inspector` when the editor starts.
   The header is included from the inspector's own `.cpp`, which is what runs the
   registration; a new `.cpp` needs a CMake re-configure (`unravel-build-verify`).
2. `inspect()` edits `var` in place and returns `inspect_result`: `changed` on every frame
   the value moved, `edit_finished` when the edit ends (`ImGui::IsItemDeactivatedAfterEdit`).
   Honor `info.read_only`.
3. A component-level inspector that adds to the default rows calls
   `inspect_var_properties(ctx, var, var_proxy, info, custom)` first and draws its extras
   after (`inspector_light.cpp`, `inspector_reflection_probe.cpp`).
4. Rows inside a custom inspector use `property_layout` (label, tooltip, the label column
   divider, the override gutter), so they line up with every other row.
5. Examples: `inspector_math.cpp` (vectors and quaternions), `inspector_physics_shape.cpp`,
   `inspector_curve.cpp`, `inspector_asset_picker.cpp` with `inspector_assets.cpp` (asset
   fields).

## Undo, overrides, search

- `inspect_property` holds the old value, and when the inspector reports `changed` it writes
  the new one through the proxy, records the undo step (`add_property_action`) and the prefab
  override for the current path. An inspector that writes the live object itself bypasses all
  three.
- Edits that must stay out of undo (import settings staged until Apply, documents with their
  own history) run inside `inspect_without_undo_scope`; `end_inspection` settles staged state
  when the inspector stops showing the object.
- Undo steps are named after their proxy (`make_property_proxy_name`), and undo and redo
  highlight the row whose proxy carries that name: rows built by hand name their proxy the
  same way.
- An inspector that draws rows of its own overrides `is_block()`, `get_rows_object()`,
  `holds_row_name()` and `has_overridden_rows()`, so the toolbar search and the
  overridden-only filter see those rows.
- Override paths come from the inspector context's path contexts (`property_path_generator`,
  `inspector_context` in `inspectors.h`); their model and the statement lists: skill
  `unravel-prefabs`.
- Asset fields read assets with `try_get()`; a thumbnail request
  (`asset_picker::make_preview`) is what starts loading an asset nothing else uses.

## Gizmo for a component

Same registry pattern in `editor/editor/hub/panels/scene_panel/gizmos/gizmos/`: a struct
deriving from `gizmo` with `draw` and `draw_billboard`, registered with
`GIZMO_REFLECT(gizmo_my_component, my_component)` and included from `gizmos.cpp`
(`gizmo_physics_component.h` is the model). `gizmo_entity` draws the gizmos of every
component in `all_inspectable_components` while component gizmos are on.

## Verify

- The inspector shows the rows for the target type, also in a narrow dock (labels stack over
  values below about 20 font units).
- An edit is one undo step; undo and redo restore it and highlight the row.
- On a prefab instance the edited row shows the override mark and reverts.
- The toolbar search finds the new rows; no ImGui ID collisions (hidden ids with `##`).
- Save and reopen: the value survives (if not, read the component's `SAVE` body before the
  inspector).
