---
name: unravel-prefabs
description: >-
  Covers UnravelEngine prefabs: .pfb and .spfb documents, prefab instances and nested instances,
  overrides and removals (statement lists), sync and replay, apply, revert, unlink, reparent and
  clone of instance content, prefab mode, and the deploy's nesting bake. Use when an instance loses
  or resurrects content, an override does not stick or leaks, or a change touches
  prefab_component, prefab ids or sync_prefab_instance.
---

# Unravel prefabs

Prefab documents (`prefab` and `scene_prefab`, `engine/engine/ecs/prefab.h`), the instance root
(`prefab_component`), entity identity inside documents (`prefab_id_component`), the statement lists
that hold overrides and removals, the sync that replays a document over its instances
(`engine/engine/meta/ecs/entity.cpp`), and the editor bookkeeping in `editor/editor/editing/prefab_*`.
Component serialization in general belongs to `unravel-ecs-component`; MCP prefab tools are in
`editor_data/data/mcp/skills/scene-building.md`.

## Gotchas

- Every prefab-space id names the document (asset uid) that issued it: `prefab_id_component::{id,
  document}` for an entity, `prefab_component::{instance_id, instance_document}` for a nested
  instance's slot. Both are issued at that document's save for an entity or instance without one and
  never changed by another document's save or by a replay. Never restamp a matched entity's document.
- A replay removes only entities and slots that carry its own document uid. Content an outer
  document added inside a nested instance carries the outer uid, so the nested asset's sync leaves it.
- One author per statement list, and the list lives with the author. `from_document` is written only
  by its document's load and replay (replaced whole); `local` is never touched by a replay.
  Scene-editing code writes the nearest instance root's `local`, never `from_document`. Never merge two
  authors' lists or attribute entries afterwards.
- A statement's key is (instance path of slot ids, qualified id, component path), and the component
  path is the serialization path (`transform_component/position/x`). Renaming a component type or a
  reflected field orphans every override on it; slot chains and component paths are file format.
- `prefab_statements::add_override` collapses along the path: an override on an ancestor path
  replaces or absorbs the narrower one.
- Use `has_override_on_or_above` to decide what a replay must not overwrite (a field override must not
  shield its siblings), and `has_override_touching` to decide whether a read may pass a level.
- `remove_entity` drops that entity's overrides with it, so a revert cannot bring them back without
  the entity.
- A nil `instance_id` is an instance placed where it stands: no document names it, so no replay
  removes it. A document only removes slots it named.
- Saving into a prefab file folds every `local` under the root into the document's list
  (`fold_document_statements`) and writes an empty `local`; a scene or a clone stream keeps `local`.
- `sync_prefab_instance` keeps the instance root's local position, rotation and parent always; scale
  and skew follow the asset unless overridden. The asymmetry is deliberate.
- The authoring root (prefab mode, the prefab inspector; `authoring_root_tag`) is never synced against
  its file and its own content is never recorded as overrides: its state is the file's next content.
- Clone: the clone root's slot goes nil (an addition to its container until that container is saved
  as a prefab). A nested slot keeps its id only when the document that placed it is an instance inside
  the clone; every other slot goes nil (`reconcile_cloned_slots`). Prefab ids are kept only at and below
  an instance root inside the clone.
- A copy of hierarchies (a clone, an undo snapshot, `save_clone_to_stream`) lists the entities it holds
  records for before its records (`load_context::record_list`). A reference to a listed entity lands on
  the copy's own, from any root; any other lands on the registry's entity with that id or uuid, or on
  nothing. Never resolve a reference by its flags alone in a copy: a link further down the copy then
  lands on the original and its record loads into the original. Copies in `clone_identity::uuids` load
  into any registry; `scene::clone_entity` uses entity ids.
- Edits that record or strip statements (remove, reparent, apply) keep a record for undo
  (`editor/editor/editing/prefab_removal_record.h`) and run as editor actions.
- A document in a layout newer than the build reads is refused (`find_document_problem`,
  `find_apply_problem`): never load and re-save it, the save would write the build's reading of it.
- A scene file in layout 1 repeats the `"entities"` key once per root: a JSON parser that keeps the
  last key (Python `json.load`) silently drops roots. Edit such files as text; layout 2 has a `"roots"`
  array.
- Nesting cycles are refused at expansion (`load_from_prefab_out` returns false) and reported by
  `validate_prefab_graph`.

## How it works

Identity:

| Field | Meaning |
|-------|---------|
| `prefab_id_component::id` + `document` | The entity within the document that introduced it |
| `prefab_component::instance_id` | Which slot of the containing document this instance is; scoped to that document |
| `prefab_component::instance_document` | The document that placed the instance (whose slot it is); nil = placed by hand |
| `id_component` | Global entity id, unrelated to prefab identity |

Statement lists, both `prefab_statements` on the instance root:

| List | Author | Replay |
|------|--------|--------|
| `from_document` | The instance's own document: statements about content nested inside it (paths never empty) | Replaced whole by every replay of that document |
| `local` | This scene, or the nearest root's editor (paths empty, except an authoring root that adopted its document's list) | Never touched |

Replay (`sync_prefab_instance`): the document loads over the existing instance with a load context
that matches entities by qualified id and consults every removal stated about the content. Order:
innermost content first, then each document's `from_document` outward, then the top root's `local`.
The sync holds `scoped_deferred_nested_sync` around its own load and cascades into nested instances
afterwards, once the removals about them are applied again. `collect_statements_about` and
`replay_statements` (`engine/engine/meta/ecs/entity.hpp`) gather what a replay must respect.

Saving a document: an entity without a prefab id, and an instance without a slot, under the save root
gets one issued by the saving document. Nested assets name their own content at their own save.

Documents are JSON with a numeric `"version"` first: `SCENE_DOCUMENT_VERSION` (2, a `"roots"` array)
and `PREFAB_DOCUMENT_VERSION` (1), both in `entity.hpp`. Older layouts load: ids without a document are
attributed to the instance they sit in, and flat override sets on an instance root load into `local`.

Deploy: `bake_prefab_nesting` (`editor/editor/editing/prefab_bake.h`) loads every prefab and scene
dependency-first, resolves its nested instances, and writes cooked copies with a "nesting resolved"
marker into a staging folder, never the compiled cache. Only a deployed build believes the marker; the
editor syncs nested instances on every load.

## Procedures

| Operation | Function | What it does to the lists |
|-----------|----------|---------------------------|
| Revert | `revert_local_changes` | Drops the instance's `local`; containing documents' statements and added entities stay |
| Apply | `apply_instance_to_prefab` | Statements about nested content become the document's; statements about own content become content; the nested root's own placement override stays. The caller syncs (`editing_manager::sync_after_override_change`) |
| Unlink | `unlink_from_prefab` | Removing the root's `prefab_component` strips prefab ids down its subtree |
| Reparent inside one instance | `prefab_override_context::mark_entity_reparented` | Parent and placement as overrides on the nearest root's `local` |
| Reparent out of the supplying instance | same | Removals on each instance that supplied a part; the moved subtree becomes scene content, ids and slots dropped |
| Reparent into an instance from outside | - | Nothing: it is an addition |
| Rewrite every document in the current layout | Build > Migrate Prefabs (`bake_prefab_asset` in place) | Re-saves nested prefabs first, scenes last |

Adding a field to `prefab_component`: SAVE and LOAD in
`engine/engine/meta/ecs/components/prefab_component.cpp`, deciding for each field what a save into a
prefab file writes (`is_saving_to_prefab`) and what a replay over an existing instance keeps
(`is_updating_prefab`).

## Verify

- `unravel-tests --suite prefabs` (suite `ecs serialization / prefabs / cloning`) is the regression net
  for identity, statements, replay, nesting and clone.
- The runner links the engine and `editor/core` only. Apply, revert, reparent bookkeeping, undo and
  prefab mode are checked in an editor: build `editor`, then over MCP open a prefab, save it, and check
  that an override survives save and reopen, revert restores the asset value, and entities added under a
  nested instance survive the outer document's resync.

## Entry points

| Purpose | Path |
|---------|------|
| Statement lists, instance root, prefab ids (read the comments) | `engine/engine/ecs/components/prefab_component.h` |
| Serialization of the lists and slots | `engine/engine/meta/ecs/components/prefab_component.cpp` |
| Sync, load context, document versions | `engine/engine/meta/ecs/entity.hpp`, `engine/engine/meta/ecs/entity.cpp` |
| Change collection, apply, revert, unlink | `editor/editor/editing/prefab_instance_changes.h` |
| Undo records for removals, reparents, overrides | `editor/editor/editing/prefab_removal_record.h` |
| Authoring root (prefab mode) | `editor/editor/editing/authoring_root.h` |
| Nesting graph check and deploy bake | `editor/editor/editing/prefab_bake.h` |
| Tests | `tests/tests/suites/ecs_serialization.cpp` |
