---
name: finding-and-inspecting
title: Finding and inspecting entities
description: The query toolbox - hierarchy walks, name/component/script searches, entity detail levels, transforms, bounds and selection. Query before creating or mutating anything.
order: 20
---
# Finding and inspecting entities

Query before you create or mutate. Guessed ids and duplicate re-creations are
the top source of broken scenes.

## The query toolbox

- scene_get_hierarchy_batch: lean id/name/children tree. Scope with parent_id,
  max_depth, limit; the response says "truncated" when the limit hit - narrow
  the scope rather than raising the limit blindly.
- scene_find_entities_batch: by name_contains, name_exact, component_type
  (pretty name like "Light"), script_type (C# full name). Filters AND
  together; optional parent_id scope; allowed in play mode.
- scene_get_entities_batch detail levels:
  - "pose" (default): transforms only - cheapest.
  - "summary": pose + component names - use to learn what an entity has.
  - "components": summary + typed property bags for supported components;
    narrow with components:["Light",...] to keep responses small.
- scene_get_children_batch: immediate children only, cheaper than a deep
  hierarchy walk.
- scene_get_transforms_batch: many poses; per item space "world" or "local".
- scene_get_bounds_batch: world AABBs - overlap, containment and floor-height
  checks without a screenshot.
- selection_get: the user's current selection (active entity + list).
- scene_get_info / editor_get_status: scene identity, entity count, play phase.

## Do

- Resolve entities by id from your own earlier "created" responses; fall back
  to scene_find_entities_batch by exact name.
- Use detail "summary" first; fetch "components" only for the entities you are
  about to edit.
- Verify parent/child structure with the hierarchy tools after reparenting -
  do not assume.

## Do not

- Do not pull detail "components" for the whole scene - it is huge and slow.
- Do not iterate the full hierarchy to find one entity - that is what
  scene_find_entities_batch is for.
- Do not cache names as identity; names can repeat. Ids are the identity.
