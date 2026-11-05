---
name: scripting
title: C# scripting workflow
description: The script lifecycle - list types, create from template, edit sources, wait for the build, attach by type name as component Script, and configure public fields with their exact C# types.
order: 60
---
# C# scripting workflow

Scripts are C# classes attached to entities as ScriptComponent instances.
They run their update logic in play mode only.

This skill covers the tool ordering. For the API itself - which types exist, what
their methods are, the script lifecycle, and the language traps - fetch the
`scripting-api` skill and read the engine sources it points at. Do not write a
script from memory of another engine's API.

## The lifecycle (order matters)

1. scripts_list_types - full type names currently compiled and attachable.
2. Create new scripts with scripts_create_batch (name + optional folder in the
   data folder, default scripts) - generates from the project template and
   recompiles once for the batch.
3. Edit source with scripts_get_source (read first) and
   scripts_set_sources_batch (atomic write; recompile defaults true). Address
   a script by key or by entity_id + type_name. Sources live in the project's
   data folder; engine scripts can be read, not written.
4. WAIT for the build before attaching a NEW type: scripts_get_compile_status
   {"wait":true} returns when the build that picks up your edit has finished
   and the editor has loaded it, with that build's errors (key, line,
   message) - fix those, write again, wait again. Attaching a type name that is
   not compiled yet fails, and play mode will not start while has_errors is
   true.
5. Attach with scene_add_components_batch {"component":"Script","type_name":...}
   and detach with scene_remove_components_batch the same way. The entity
   summary (scene_get_entities_batch detail summary) lists its scripts.
6. Configure public fields with component "Script" and the type_name:
   scene_get/set_component_properties_batch read and write public instance
   fields by name, at their exact C# type (byte, short, long, double ... with
   their ranges), Vector2/3/4, Color, LayerMask, Entity (an entity id, or
   null), assets (Prefab, Texture, Material, Mesh, AnimationClip, Scene,
   PhysicsMaterial, AudioClip, Font: a key such as app:/data/Prefabs/Enemy.pfb,
   or null), strings and enums by value name. Undo puts the fields back.
7. An entity can carry several scripts of one type. script_index (from 0, as
   the entity summary numbers them) picks one for the property tools and
   scene_remove_components_batch; it defaults to the first.
8. scripts_invoke calls a method on an attached script (testing a handler
   without play-testing it).

## Do

- Fetch `scripting-api` first, and copy from `engine:/data/scripts/samples/`
  rather than starting a gameplay script from scratch.
- Read existing source before editing it; write whole files, not diffs.
- Recompile once per batch of source edits (the default), not per file.
- Test behavior in play mode (verification skill); script update logic does
  not run in edit mode.

## Do not

- Do not guess type names - take them from scripts_list_types (the short name
  works when it is unique).
- Do not edit .cs files on disk directly; scripts_set_sources_batch writes
  atomically and triggers the recompile the editor expects.
- Do not rename a script file on disk: assets_move_batch renames the file and
  its class together.
