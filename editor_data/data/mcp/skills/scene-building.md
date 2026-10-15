---
name: scene-building
title: Building scenes procedurally
description: Coordinate system, WORLD vs LOCAL transforms, build order with scene_create_batch, reparenting, duplication and prefab reuse, AABB-based mesh placement, naming and housekeeping.
order: 30
---
# Building scenes procedurally

## Coordinate system

X right, Y up, Z forward (left handed). rotation_euler is degrees as
[pitch_x, yaw_y, roll_z]. The embedded cube is 1x1x1 with its origin at the
center; non-uniform scale is its size in meters. plane is 1x1 in XZ, sphere is
1m diameter, cylinder is 1m tall with 0.5m radius, capsule_1m / capsule_2m are
named by height. assets_list_embedded_primitives lists every built-in shape.

## WORLD vs LOCAL (the number one source of broken layouts)

- scene_create_batch poses are WORLD for every kind, also with parent_id,
  unless the item passes space:"local".
- scene_set_transforms_batch defaults to WORLD; pass space:"local" for
  parent-relative poses. Its undo restores only the channels it set.
- Recipe: create a parent entity at the area's world position, then create
  children with space:"local" poses. When the parent is rotated, always set
  the child's rotation_euler explicitly in local space - omitting it keeps a
  world rotation that cancels the parent.
- scene_set_parents_batch reparents while KEEPING world pose (omit parent_id
  to detach); set a local pose afterwards if you want a clean local offset.
  A parent inside the entity's own subtree is refused.

## Recommended build order

scene_create_batch makes everything the hierarchy's Create menu makes, in one
undo step, each item checked before anything is created: kind empty,
primitive (primitive: an embedded mesh name), mesh / prefab (asset: key or
uid), light (light_type), reflection_probe (probe_type), camera, volume,
audio_source, particle_emitter, text, ui_document.

1. Group parents (kind empty, one per room / area).
2. Architecture - floors, walls, pedestals (kind primitive). Pass material
   (and material_index for other slots) to assign materials at creation.
3. Imported models (kind mesh, asset) and prefab instances (kind prefab).
4. Lights (kind light) + Light properties.
5. scene_add_components_batch + scene_set_component_properties_batch for
   behavior (see the components skill).
6. scene_save.

## Reuse: duplicate and prefabs

- scene_duplicate_entities_batch clones whole configured hierarchies - build
  one finished pedestal-with-prop or domino, then duplicate and move the
  copies. Far better than re-creating and re-configuring each one.
- prefabs_create_from_entities_batch saves a hierarchy as a .pfb asset in the
  data folder (attach:true makes the entity an instance of it) and waits until
  it loads; scene_create_batch kind prefab instantiates it with a pose. Use
  prefabs for anything placed more than a couple of times or shared across
  scenes.
- Edit a prefab with prefab_open {key}: every scene_* tool then works on the
  prefab's contents. prefab_save writes it and every instance updates;
  prefab_close {"save":true|false} returns to the scene (with unsaved edits it
  requires the choice instead of the editor's dialog). editor_get_status reports
  prefab_mode.
- Edits on an instance become prefab overrides, recorded the way the inspector
  records them: names, active, parents, transforms, layers, component
  properties, script fields, materials and mesh swaps. A later prefab save
  keeps them, and the properties you left alone still follow the prefab - an
  instance with its own light intensity still takes the prefab's new light
  color. Writing back a value unchanged makes no override. Undo removes the
  override along with the edit.
- prefab_get_overrides {entity_id} lists what an instance (and every instance
  nested in it) changes: overrides (entity_id, path, label), removed entities
  and entities added under it. inherited ones are stated by a prefab that
  contains the instance - change those in that prefab.
- prefab_apply_overrides {entity_id} writes the instance into its prefab file,
  so every other instance follows (an instance nested in another writes the
  nested prefab). prefab_revert_overrides {entity_id} puts it back to the
  prefab - everything, or only the listed overrides / removals. Both are one
  edit_undo step, the file included.
- prefab_unlink_batch {entity_ids} turns instances into plain entities that no
  longer follow their prefab (undoable).
- Components and scripts added to or removed from an instance stay that way
  through prefab saves, with several scripts of one type told apart; undo takes
  a removal back. A component removed and later added back is the instance's own
  copy from then on and no longer follows the prefab.
- Removing something from a nested prefab's content while editing the outer
  prefab reaches every instance of the outer prefab, existing ones included. A
  prefab that removes its own component or script reaches new instances only;
  existing ones keep theirs.

## Placing meshes correctly

Imported meshes have arbitrary pivots and units. Before placing, call
assets_get_mesh_info for the local AABB, then:

- ground objects: y = floor_top - min_y * scale
- centered pivots (min_y negative): raise by -min_y * scale
- source units in centimeters (AABB in the hundreds): scale 0.01-0.02
- on uneven ground or on top of other objects: scene_raycast straight down
  ({"origin":[x, high, z], "direction":[0,-1,0]}) gives the surface point and
  its normal; place at point.y - min_y * scale. ignore_entity_ids leaves out
  the object being placed.

scene_get_bounds_batch gives the world AABB of placed entities (an entity with
nothing to measure reports its position as a point).

## Changing what an entity is

- scene_set_models_batch swaps the mesh an entity shows (asset), keeping its
  transform and other components; the new model gets the mesh's own materials,
  as at creation. A skinned mesh is reported - add Animation to play its clips.
- scene_set_model_materials_batch assigns material assets to slots;
  scene_set_model_material_instances_batch gives a slot a per-entity copy to
  tweak (the .mat file stays as it is).
- scene_set_layers_batch puts entities on layers (names from the project's
  layer.layers setting); physics queries and rendering filter on them, and
  detail summary reports them.

## Housekeeping

- scene_set_names_batch: meaningful names, set them early (find depends on
  them). scene_set_active_batch toggles entities without deleting.
- scene_remove_components_batch removes components (Script with type_name);
  scene_delete_entities_batch deletes hierarchies. Both are undoable.

## Do

- Interleave viewport captures with construction (see verification skill);
  do not build the whole scene blind.
- Check results with scene_get_bounds_batch / scene_get_hierarchy_batch as you
  go.

## Do not

- Do not pass local offsets to world-space setters - parts collapse to the
  origin or stack at the parent position.
- Do not rebuild an existing entity to change it - find it and mutate.
- Do not leave generated names like "Cube (37)" on anything you may need to
  find again.
