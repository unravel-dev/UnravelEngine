---
name: scripting
title: C# scripting workflow
description: The script lifecycle - list types, create from template, edit sources, wait for recompile, attach by type name, and configure public fields through the typed Script component.
order: 60
---
# C# scripting workflow

Scripts are C# classes attached to entities as ScriptComponent instances.
They run their update logic in play mode only.

## The lifecycle (order matters)

1. scripts_list_types - full type names currently compiled and attachable.
2. Create new scripts with scripts_create_batch (name + optional folder,
   default "app:/data/scripts") - generates from the project template and
   recompiles once for the batch.
3. Edit source with scripts_get_source (read first) and
   scripts_set_sources_batch (atomic write; recompile defaults true). Address
   a script by path/key or by entity_id + type_name.
4. WAIT for the recompile before attaching a NEW type: poll scripts_list_types
   until the type appears (and check logs_get_recent for compile errors).
   Attaching a type name that is not compiled yet fails.
5. Attach with scene_add_scripts_batch (type_name per entity); inspect with
   scene_list_scripts_batch; detach with scene_remove_scripts_batch.
6. Configure public fields through the typed component API: component
   "Script" with script_type set to the full type name -
   scene_get/set_component_properties_batch read and write public instance
   fields by name.

## Do

- Read existing source before editing it; write whole files, not diffs.
- Recompile once per batch of source edits (the default), not per file.
- Test behavior in play mode (verification skill); script update logic does
  not run in edit mode.

## Do not

- Do not add scripts through scene_add_components_batch - Script is not an
  engine component type there; only scene_add_scripts_batch works.
- Do not guess type names - they are full names (namespace included) from
  scripts_list_types.
- Do not edit .cs files on disk directly; scripts_set_sources_batch writes
  atomically and triggers the recompile the editor expects.
