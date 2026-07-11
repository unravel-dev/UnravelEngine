---
name: unravel-prefabs
description: >-
  Works on UnravelEngine prefabs: prefab assets, scene instances, property
  overrides, and path-aware serialization. Use for prefab creation, instance
  overrides, prefab unlinking, or deserialization path matching.
disable-model-invocation: true
---

# Unravel Prefabs

## Start here

| Purpose | Path |
|---------|------|
| Prefab struct | `engine/engine/ecs/prefab.h` |
| Prefab component | `engine/engine/ecs/components/prefab_component.h` |
| Prefab ID component | `engine/engine/ecs/components/prefab_id_component.h` |
| Meta | `engine/engine/meta/ecs/components/prefab_component.hpp` |
| Override paths | `editor/editor/hub/panels/inspector_panel/inspectors/inspectors.h` |
| Path context | `engine/core/serialization/` (`serialization::path_context`) |
| Hierarchy UI | `hierarchy_panel` — prefab icons, unlink |

## Prefab model

- **Prefab asset** — serialized entity subtree as binary/associative buffer
- **Instance** — entity with `prefab_component` + `prefab_id_component`
- **Overrides** — per-instance property changes tracked separately from base

## Property overrides

Inspector stores two paths per override (`prefab_property_override`):

- `pretty_path` — display: `"Transform/Position/X"`
- `serialization_path` — matching: `"entities[0]/components/Transform/..."`

Deserialization matches overrides by `serialization_path`. **Never change serialization paths** for existing fields without migration.

## Workflow

### Create prefab

From hierarchy context menu → save selection as prefab asset.

### Instantiate

Drag prefab into scene or create from content browser.

### Override

Edit instance property in inspector → stored as override, not written to base prefab.

### Apply / revert

Inspector supports reset to prefab default for overridden properties.

## Verification checklist

- [ ] New instance matches prefab base
- [ ] Override persists save/reload
- [ ] Revert override restores base value
- [ ] Unlink produces independent entity subtree
- [ ] Nested prefabs resolve correctly
- [ ] UID references intact after reimport

## Common mistakes

- Modifying base prefab when intending instance-only change
- Breaking `serialization_path` format in refactor
- Missing prefab components on instantiated entities
- Confusing `prefab_component` vs `prefab_id_component` roles
