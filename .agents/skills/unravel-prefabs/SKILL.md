---
name: unravel-prefabs
description: >-
  Works on UnravelEngine prefabs: the unified statement model (qualified ids,
  per-author statement lists), nested instances, property overrides, removal
  records, replay/sync, and legacy migration. Use for prefab creation, instance
  overrides, nested prefab bugs, reparent/clone semantics, or prefab
  serialization.
---

# Unravel Prefabs

The prefab system uses the **unified statement model** (landed 2026-08-22). If you
remember a `property_overrides` / `removed_entities` flat-list model with
`placed_by` / `foreign_entities` provenance - that is the OLD model; only the legacy
loader still reads it.

## Start here

| Purpose | Path |
|---------|------|
| Prefab asset struct | `engine/engine/ecs/prefab.h` |
| Components + statement lists | `engine/engine/ecs/components/prefab_component.h` (extensively documented - read it) |
| Sync / replay logic | `engine/engine/meta/ecs/entity.hpp` / `entity.cpp` (`sync_prefab_instance`, `scoped_deferred_nested_sync`) |
| Scene load pass | `engine/engine/ecs/scene.cpp` (`qualify_legacy_prefab_ids`) |
| Meta | `engine/engine/meta/ecs/components/prefab_component.hpp` / `.cpp` |
| Design doc | `tasks/nested_prefabs_design.md` (esp. section 16), `tasks/unified_model_plan.md` |
| Tests | `tests/tests/suites/ecs_serialization.cpp` - suite `ecs serialization / prefabs / cloning` |

## Qualified identity

Every prefab-space id names the **document** (asset uid) that issued it:

| Field | Meaning |
|-------|---------|
| `prefab_id_component::{id, document}` | Entity identity within the document that introduced it. Issued at that file's save; never changed by another document's save |
| `prefab_component::instance_id` | Which slot of the containing document this instance is. Scoped to the container (unlike `id_component` = global, `prefab_id_component` = per-asset). Regenerated on clone - a copy is a different slot |
| `prefab_component::instance_document` | Whose slot it is - the document that placed the instance. Nil = hand-placed here |

Ownership drives cleanup: a document's replay removes only entities/slots carrying
**its own** document uid. Content an outer document added inside a nested instance
keeps the outer document's name, so the nested asset's sync leaves it alone.

## Per-author statement lists

A **statement** is an override, a removed entity, or a removed nested instance -
keyed `(instance_path slot chain, qualified id, property path)`. One author per list,
and the list lives with the author (`prefab_statements` on the instance root):

| List | Author | Replay behavior |
|------|--------|-----------------|
| `prefab_component::from_document` | The instance's own document (statements about content *nested* inside it - paths always non-empty) | Replaced wholesale by every replay of that document; never written by editing here |
| `prefab_component::local` | This scene / the nearest root's editor | Never touched by any replay |

Replay applies innermost content first, then each document's `from_document` outward,
then the top root's `local`. Nothing merges two authors into one set.

Recording: scene edits go to the **nearest** instance root's `local`; prefab-mode
edits to the authoring root's `local` (the document's list is adopted as local while
editing). Save folds `from_document U local` (and nested roots' locals) into the file.

Override semantics worth knowing (see `prefab_statements` doc comments):

- `add_override` collapses along the property path - a more specific path replaces a
  broader one
- `has_override_touching` = on/above/below (what lets a read reach the value);
  `has_override_on_or_above` = what shields a value from replay (a field override
  must not shield its siblings)
- `remove_entity` drops that entity's overrides too (revert must not resurrect them)

## Operations

| Operation | Semantics |
|-----------|-----------|
| Revert | Drop `local` entries |
| Apply-to-inner | Move entries targeting instance C into C's file |
| Reparent inside an instance | Transform-parent (+ position/rotation) override entries on the nearest root |
| Reparent OUT of the supplying instance | Removal entries on whichever instance supplied each part; the moved subtree becomes scene content - prefab ids dropped, slots cleared |
| Reparent INTO an instance from outside | No bookkeeping |
| Clone | Copies lists as-is; keeps prefab ids only inside instance roots; slots of documents outside the clone go nil; `instance_id` regenerated |

## Legacy migration

Old files (flat `property_overrides` / document-less ids) convert on load: nil
documents are attributed to the nearest containing instance
(`qualify_legacy_prefab_ids`); flat lists become the root's `local`.
**Build > Migrate Prefabs** re-saves every prefab and scene in dependency order.

## Verification

Run the suite - it is the regression net for this entire model (536+ checks):

```bash
build/Debug/bin/unravel-tests.exe --suite serialization
```

- [ ] Suite green (exit code 0)
- [ ] New instance matches prefab base; override persists save/reload; revert restores
- [ ] Entities/instances added under a NESTED instance survive the outer document's resync
- [ ] Unlink produces an independent subtree; clone follows the slot rules above
- [ ] Editor-side flows (reparent bookkeeping, undo un-recording, Apply-All) are
      build-verified + manual - the test runner links the engine only

## Common mistakes

- Reasoning in the old model (flat overrides, `placed_by`, `foreign_entities`) - those
  exist only in the legacy load path
- Editing `from_document` from scene-editing code - only a document's own save/replay
  owns that list
- Merging statement lists across authors, or attributing entries after the fact - the
  design's whole point is that neither is ever needed
- Restamping a matched entity's `prefab_id_component` document on replay - identity is
  written once, at the issuing document's save
- Changing statement key formats (slot paths, component paths) without a migration -
  files reference them
