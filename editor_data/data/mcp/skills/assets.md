---
name: assets
title: Assets and materials
description: Browsing and resolving assets, importing everything through assets_import_files (files or whole model directories), import settings (texture compression, color space, mesh options), the glTF-vs-GLB texture rule, readiness caveats, and shared material assets vs per-entity material instances.
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
- assets_reimport_batch re-runs import for changed sources or import settings and
  waits until each key has reloaded (state per key, wait_ms default 15000).

## Importing: always through assets_import_files

The single import path for ALL assets. It copies absolute filesystem paths
(outside the project) into an "app:/..." folder and waits for the copy plus
initial compile. Entries in paths may be:

- files, for self-contained assets (.glb, .fbx, textures, audio), or
- directories, imported recursively - use one directory per multi-file model
  (a .gltf with its .bin and texture files) so relative references stay
  intact.

A file imported over an existing one replaces it in place: the call reports it
ready once the reloaded asset has replaced the old one, so scene and UI show the
new content from then on. Files no asset type imports (a glTF's .bin, a license
.txt) are copied too and listed with asset:false; they are not waited for.

Never copy files into the project's data directory yourself; stage downloads
outside the project and import them with the tool (it rejects paths already
inside the project).

## Import settings

- assets_get_import_settings {"keys":[...]} reads what the inspector's Import tab
  edits: textures (type, colorspace, generate_mipmaps, invert_normal_y,
  quality.max_size, quality.compression), meshes (model, sdf, cards, animations,
  materials), animations (root_motion), audio (force_to_mono). "schemas" gives the
  enum names and ranges; a loaded texture also reports "texture": its compiled
  format, size and mips - the proof a change took.
- assets_set_import_settings {"items":[{"key":"app:/data/UI/atlas.png",
  "settings":{"quality":{"compression":"none"}}}]} writes the .meta and waits until
  the asset has been imported again. Partial objects merge into the current
  settings; an item with any rejected entry writes nothing.
- "project_default" follows the project's settings assets.texture
  (default_compression, default_max_size; settings_get {"group":"assets"}).
  Changing them recompiles nothing by itself: run assets_recompile_textures
  (prefix, default app:/; wait_ms to wait for each).
- Only app:/ textures are compressed. Use compression "none" for UI atlases and
  pixel art that must stay exact, colorspace "srgb" for color maps, "linear" for
  data maps.

## Import gotchas

- Skinned mesh imports generate .anim clip assets next to the source (one per
  take) - those keys feed the Animation component.
- Imports extract one .mat per source material ("[0] Name.mat" ...).
- assets_wait_ready_batch reports a state per key: ready, loading (still
  compiling at the deadline - wait again), failed (the load produced no asset:
  read logs_get_recent), or missing (no such file - check the key). It starts
  loads nothing else has asked for yet, so ready means usable. Heavy meshes
  take extra time for distance-field baking.

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
