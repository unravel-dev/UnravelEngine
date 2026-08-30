---
name: assets
title: Assets and materials
description: Browsing and resolving assets, importing everything through assets_import_files (files or whole model directories), the glTF-vs-GLB texture rule, readiness caveats, and shared material assets vs per-entity material instances.
order: 50
---
# Assets and materials

## Browsing and resolving

- assets_find_batch: search by type (e.g. "mat", ".pfb", "emesh", "anim"),
  location prefix ("app:/data/Models/") and name_contains; optional protocol.
  Prefer it over assets_list_batch (which lists a whole protocol).
- assets_get_batch resolves key <-> uid; assets_list_types lists importable
  extensions; assets_create_folder makes "app:/..." folders;
  assets_get_mesh_info returns a mesh's local AABB.
- assets_reimport_batch re-runs import for changed sources or import settings.

## Importing: always through assets_import_files

The single import path for ALL assets. It copies absolute filesystem paths
(outside the project) into an "app:/..." folder and waits for the copy plus
initial compile. Entries in paths may be:

- files, for self-contained assets (.glb, .fbx, textures, audio), or
- directories, imported recursively - use one directory per multi-file model
  (a .gltf with its .bin and texture files) so relative references stay
  intact.

Never copy files into the project's data directory yourself; stage downloads
outside the project and import them with the tool (it rejects paths already
inside the project).

## Import gotchas

- Skinned mesh imports generate .anim clip assets next to the source (one per
  take) - those keys feed the Animation component.
- Imports extract one .mat per source material ("[0] Name.mat" ...).
- assets_wait_ready_batch polls loadability but can keep reporting ready:false
  after compilation finished. Treat it as advisory: check logs_get_recent for
  compile lines and simply try the spawn - if it succeeds and the AABB is
  sane, the asset is ready. Heavy meshes take extra time for distance-field
  baking.

## Materials: shared assets vs per-entity instances

- .mat files are SHARED assets: materials_set edits the asset and every
  entity using it changes. Workflow: materials_list_properties (schema) ->
  materials_get_batch (current values) -> materials_set.
- Per-entity tweaks use scene_set_model_material_instances_batch: it overrides
  slots on ONE entity at runtime and does NOT write .mat files;
  scene_clear_model_material_instances_batch reverts to the shared asset.
- Create new materials with materials_create_batch (folder + name), configure
  with materials_set (base_color, roughness, metalness, emissive_color +
  emissive_intensity, texture map keys), assign via material_key at spawn or
  scene_set_model_materials_batch.
- Useful emissive_intensity ranges: strips acting as area lights 25-35; small
  glowing props 5-10 (larger overwhelms bloom).

## Do

- Import once, then reuse the imported keys everywhere (find them again with
  assets_find_batch by prefix).
- Decide shared vs instance BEFORE editing: recoloring one prop means
  instances; retuning the look of a prop type means the shared asset.

## Do not

- Do not edit a shared .mat to tint a single entity - every instance changes.
- Do not re-import an asset that is already in the project to "refresh" it -
  use assets_reimport_batch on the existing key.
