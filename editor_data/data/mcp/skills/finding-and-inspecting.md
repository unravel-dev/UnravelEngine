---
name: finding-and-inspecting
title: Finding and inspecting entities
description: The query toolbox - hierarchy walks, name/component/script searches, entity detail levels, bounds and selection. Query before creating or mutating anything.
order: 20
---
# Finding and inspecting entities

Query before you create or mutate. Guessed ids and duplicate re-creations are
the top source of broken scenes.

## The query toolbox

- scene_get_hierarchy_batch: lean id/name/children tree. Scope with parent_id,
  max_depth (1 = a parent's children only), limit; the response says
  "truncated" when the limit hit - narrow the scope rather than raising the
  limit blindly.
- scene_find_entities_batch: by name_contains, name_exact, component (a name
  from scene_list_components, like "Light"), type_name (C# type). Filters AND
  together; parent_id searches only below that entity; limit up to 5000 with
  truncated when more matched; allowed in play mode.
- scene_get_entities_batch detail levels:
  - "pose" (default): world and local transforms only - cheapest.
  - "summary": pose + component names, layers, scripts (type and
    script_index) and child_count - use to learn what an entity has.
  - "components": summary + every component's property bag; narrow with
    components:["Light",...] to keep responses small.
- scene_get_component_properties_batch reads one component, optionally only
  some names - the cheapest way to check a value.
- scene_get_bounds_batch: world AABBs (meshes, particles, world-space UI
  documents) - overlap, containment and floor-height checks without a
  screenshot.
- scene_raycast: what a ray hits on the models' triangles, nearest first
  (entity_id, point, normal, distance). The ray is origin + direction, or
  screen [x,y] - a pixel of viewport_capture_scene at scale 1 - to ask what is
  under that pixel. Works while editing, no physics needed.
- viewport_world_to_screen: where world points land in the Scene view (pixel,
  depth, visible) - to check something is in frame before capturing.
- selection_get: the user's current selection (active entity + list).
- editor_get_status: project, scene identity and entity count, prefab mode,
  play phase.

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
