---
name: scene-building
title: Building scenes procedurally
description: Coordinate system, WORLD vs LOCAL transforms, build order, reparenting, duplication and prefab reuse, AABB-based mesh placement, naming and housekeeping.
order: 30
---
# Building scenes procedurally

## Coordinate system

X right, Y up, Z forward (right handed). rotation_euler is degrees as
[pitch_x, yaw_y, roll_z]. The embedded Cube is 1x1x1 with its origin at the
center; non-uniform scale is its size in meters. Plane is 1x1 in XZ, Sphere is
1m diameter, Cylinder is 1m tall with 0.5m radius, Capsule 1m / 2m are named
by height. assets_list_embedded_primitives lists every built-in shape.

## WORLD vs LOCAL (the number one source of broken layouts)

- scene_create_* positions are WORLD even when parent_id is set (unless the
  item passes space:"local").
- scene_set_transforms_batch defaults to WORLD; pass space:"local" for
  parent-relative poses.
- Recipe: create a parent entity at the area's world position, then create
  children with space:"local" poses. When the parent is rotated, always set
  the child's rotation_euler explicitly in local space - omitting it keeps a
  world rotation that cancels the parent.
- scene_set_parents_batch reparents while KEEPING world pose (omit parent_id
  to detach); set a local pose afterwards if you want a clean local offset.

## Recommended build order

1. scene_create_entities_batch for group parents (one per room / area).
2. scene_create_primitives_batch for architecture - floors, walls, pedestals.
   Pass material_key per item to assign materials at creation.
3. scene_create_meshes_batch for imported models (asset_key required).
4. scene_create_light + typed Light properties for lighting.
5. scene_add_components_batch + scene_set_component_properties_batch for
   behavior (see the components skill).
6. scene_save.

## Reuse: duplicate and prefabs

- scene_duplicate_entities_batch clones whole configured hierarchies - build
  one finished pedestal-with-prop or domino, then duplicate and move the
  copies. Far better than re-creating and re-configuring each one.
- prefabs_create_from_entities_batch saves a hierarchy as a .pfb asset
  (optional attach keeps the scene entity linked to it);
  scene_create_from_prefab_batch instantiates prefabs by asset_key with a
  pose. Use prefabs for anything placed more than a couple of times or shared
  across scenes.

## Placing meshes correctly

Imported meshes have arbitrary pivots and units. Before placing, call
assets_get_mesh_info for the local AABB, then:

- ground objects: y = floor_top - min_y * scale
- centered pivots (min_y negative): raise by -min_y * scale
- source units in centimeters (AABB in the hundreds): scale 0.01-0.02

While an asset is still compiling the AABB can read as +-3.4e38 - treat that
as "not ready", wait and retry.

## Housekeeping

- scene_set_names_batch: meaningful names, set them early (find depends on
  them). scene_set_active_batch toggles entities without deleting.
- scene_remove_components_batch removes engine components;
  scene_delete_entities_batch deletes hierarchies.

## Do

- Interleave viewport captures with construction (see verification skill);
  do not build the whole scene blind.
- Check results with scene_get_bounds_batch / hierarchy queries as you go.

## Do not

- Do not pass local offsets to world-space setters - parts collapse to the
  origin or stack at the parent position.
- Do not rebuild an existing entity to change it - find it and mutate.
- Do not leave generated names like "Cube (37)" on anything you may need to
  find again.
