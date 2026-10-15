---
name: assets
title: Assets and materials
description: Finding assets, importing everything through assets_import_files (files or whole model directories), moving / duplicating / deleting assets safely, import settings (texture compression, color space, mesh options), readiness, and shared material and physics material assets vs per-entity material instances.
order: 50
---
# Assets and materials

## Finding and resolving

- assets_find_batch: search by type (a name from assets_list_types - texture,
  mesh, material, prefab, scene, audio_clip, animation_clip, ... - which covers
  every format of it, or one extension such as "png"), key prefix ("Models/" or
  "app:/data/Models/"), name_contains and protocol (app | engine | editor |
  all). Results are [{key, uid, type}] up to limit; truncated says more
  matched.
- assets_get_batch resolves key <-> uid; assets_get_mesh_info returns a mesh's
  local AABB; assets_list_embedded_primitives lists the built-in meshes.
- assets_create_folder makes folders in the data folder.

## Importing: always through assets_import_files

The single import path for ALL assets. It copies absolute filesystem paths
(outside the project) into a folder of the data folder, then waits up to
wait_ms (default 15000; 0 does not wait) for the assets to load, each reported
with a state. Entries in paths may be:

- files, for self-contained assets (.glb, .fbx, textures, audio), or
- directories, imported recursively - use one directory per multi-file model
  (a .gltf with its .bin and texture files) so relative references stay
  intact.

A file imported over an existing one replaces it in place: it reports ready
once the reloaded asset has replaced the old one, so scene and UI show the new
content from then on. Files no asset type imports (a glTF's .bin, a license
.txt) are copied too and listed with asset:false.

Never copy files into the project's data directory yourself; stage downloads
outside the project and import them with the tool (it rejects paths already
inside the project).

## Moving, copying and deleting

- assets_move_batch [{from, to}] renames or moves files and folders as the
  Content Browser does: each asset keeps its id, so scenes, prefabs and
  materials that use it follow. A to that is a folder (or ends with /) takes
  from under its own name; a file's to may leave out the extension. A C# script
  whose class has the file's name is renamed along.
- assets_duplicate_batch copies files or folders next to themselves
  ("Name (0)"); each copy gets an id of its own.
- assets_delete_batch is recoverable: the assets move, each with its .meta,
  into one batch of the project's trash (editor/trash).
  assets_restore_deleted {"trash":<batch>} puts them back with the same ids;
  without trash it lists the batches. Until restored, entities that used them
  hold empty references.

## Import settings

- assets_get_import_settings {"keys":[...]} reads what the inspector's Import tab
  edits: textures (type, colorspace, generate_mipmaps, invert_normal_y,
  quality.max_size, quality.compression), meshes (model, sdf, cards, animations,
  materials), animations (root_motion), audio (force_to_mono). "schemas" gives the
  enum names and ranges; a loaded texture also reports "texture": its compiled
  format, size and mips - the proof a change took.
- assets_set_import_settings {"items":[{"key":"UI/atlas.png",
  "settings":{"quality":{"compression":"none"}}}]} writes the .meta and waits until
  the asset has been imported again. Partial objects merge into the current
  settings; an item with any rejected entry writes nothing.
- "project_default" follows the project's settings assets.texture
  (default_compression, default_max_size; settings_get {"group":"assets"}).
  Changing them recompiles nothing by itself: run assets_recompile_textures
  (prefix, default app:/data; wait_ms to wait for each).
- assets_reimport_batch imports keys again and waits until each has reloaded.
- Only app:/ textures are compressed. Use compression "none" for UI atlases and
  pixel art that must stay exact, colorspace "srgb" for color maps, "linear" for
  data maps.

## Readiness

- Skinned mesh imports generate .anim clip assets next to the source (one per
  take) - those keys feed the Animation component.
- Imports extract one .mat per source material ("[0] Name.mat" ...).
- Tools that create or import assets wait for them and report a state per key;
  assets_wait_ready_batch waits for any keys: ready, loading (still compiling
  at the deadline - wait again), failed (the load produced no asset: read
  logs_get_recent), or missing (no such file - check the key). timeout_ms 0
  reports the state now. Heavy meshes take extra time for distance-field
  baking.

## Materials and physics materials: shared assets

- .mat and .phm files are SHARED assets: every entity using one changes when
  it changes. assets_list_properties {"type":"material"|"physics_material"}
  lists the properties with ranges; assets_get_properties_batch {"assets":[...]}
  reads them; assets_set_properties_batch {"items":[{"asset":..,
  "properties":{..}}]} writes them - every item is checked first, one rejected
  value writes nothing - and saves the files (save:false keeps the change in
  memory).
- assets_create_batch {"items":[{"type":"material","path":"Materials/Brass",
  "properties":{"base_color":[1,0.8,0.4,1],"metalness":1}}]} makes new ones
  (properties checked first; a rejected value creates nothing) and waits until
  they load. Assign a material with scene_create_batch material or
  scene_set_model_materials_batch; a physics material with the Physics
  component's material property.
- Per-entity tweaks use scene_set_model_material_instances_batch: it gives ONE
  entity's slot its own copy of the material (starting from the slot's
  asset) and does NOT write .mat files; calls build on each other, undo puts
  the previous values back; scene_clear_model_material_instances_batch
  returns to the shared asset.
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
- Do not move or rename assets on disk yourself - assets_move_batch keeps their
  ids, a filesystem move outside the editor does not.
