---
name: getting-started
title: Getting started + hard rules
description: Session bootstrap, project/scene management, where MCP may write, response conventions, undo, selection and focus etiquette, and the hard rules every MCP client must follow (never hand-edit serialized files, add components before setting them, play-mode restrictions). Read first.
order: 10
---
# Getting started with the Unravel editor MCP

## Hard rules (read first)

1. NEVER create or edit scene (.spfb), prefab (.pfb) or material (.mat) files
   directly on disk. They are serialized engine state: hand-authored payloads
   silently fall back to defaults on load or corrupt runtime state. Always go
   through the MCP tools, which run the editor's real setters and validation.
2. If a workflow seems impossible with the available tools, say so instead of
   working around the tools through the filesystem.
3. Mutating tools refuse play mode. Stop play (play_set_active {"active":false})
   before editing, or make edits first and then enter play mode.
4. Components must exist before their properties are set: add them with
   scene_add_components_batch (a C# script is component "Script" with its
   type_name), then configure them with scene_set_component_properties_batch.
5. MCP writes only inside the project's data folder. Name a location as a key
   ("app:/data/Props/Crate.mat") or as a path relative to it ("Props/Crate");
   anything else is refused. The exceptions are project_create / project_open
   (whole project folders), the deploy folder, and the source files
   assets_import_files copies in from outside.

## Session bootstrap

1. editor_get_status - project, scene, prefab mode and play state in one call.
   Always start here. modal names a dialog the editor waits on (a save prompt
   the user's own action opened, for instance); while one is open the editor
   takes no other input - editor_answer_modal {"button":...} presses one of
   its buttons. answerable false means a window only the user can close.
2. No project open? project_list_recent, then project_open with recent_index
   or an absolute path. project_close closes it; project_create makes and opens
   a new one.
3. Open a scene with scene_open {"key":"Levels/Main"} (the extension may be
   left out), or start one with scene_new_from_preset (names from
   scene_list_presets: low | medium | high | showcase - they seed a camera, sun
   + sky, a global reflection probe and a global post volume).
4. Save with scene_save {"key":"Levels/Main"}; without a key it overwrites the
   scene's own file (an error for a scene never saved).
5. scene_open, scene_new_from_preset, project_open, project_create and
   project_close refuse while there are unsaved edits (scene or prefab mode):
   save first, or pass force:true to discard them.

## From a scene to a game

Building a scene is not shipping a game. The rest of the chain:

- settings_list_properties / settings_get / settings_set configure the project.
  standalone.startup_scene (the scene the built game loads) is required before
  the project can be deployed at all.
- Settings profiles are the targets a project builds for: each has a platform
  (desktop or android) and overrides only what differs from the project
  settings. settings_profiles_list lists them (a project starts with Desktop);
  settings_profiles_create / _rename / _delete manage them. settings_get and
  settings_set with profile read and override a profile's values (overridden
  lists what it changes), settings_revert stops overriding paths. Layers,
  input, resolutions and the physics backend are shared by every profile.
  editor_set_preview_profile runs the editor with a profile's settings; then
  settings_set without profile reports as shadowed the paths that profile
  overrides, whose values the editor shows instead.
- input_list_actions / input_map_action define the action names scripts query.
- deploy_get_state reports the deploy profile and its settings, what still blocks
  a build (one sentence each) and the progress of a running deploy;
  deploy_set_settings sets the destination folder (default .deploy in the
  project; relative paths are in the project folder, outside the folders the
  editor manages) and the deploy settings: scope shared (default) writes what
  every profile deploys with, scope profile writes overrides of profile
  (default the deploy profile), and revert stops overriding settings named as
  its arguments. location is always shared; patch_release and android_* always
  belong to the profile. deploy_profile selects the profile deploys build, as
  the Deploy window's picker does; profile alone never changes it. The result
  says what was written where and which shared values a profile shadows.
  project_deploy (profile: which one) builds the profile into
  <location>/<profile> once running compiles finish. editor_open_deploy opens
  the Deploy window for a user who should see it. A profile's textures are cooked for its platform (Android: ASTC or
  ETC2) into cache/profiles/<profile>, each recompiled in place when a setting
  changes. An asset that failed to compile fails
  the deploy (failed_steps names it) before the folder is touched. Compiled
  assets ship in data/<engine|app>/content.pak (pack_assets false ships them
  loose). Deploying again into the same folder updates it in place and only
  copies what changed. To ship updates as small patches: deploy, then
  deploy_keep_release (named by app.version), then deploy_set_settings
  patch_release to that name - later deploys ship the release's packs
  unchanged plus content_p1.pak with every change since; deploy_remove_release
  deletes a kept release. Releases are kept per profile. To ship only what the game uses: deploy_set_settings
  only_used_assets true, scenes (the scenes scripts load besides the startup
  scene) and include (folders or assets scripts load by a path built at run
  time); keys that name nothing come back as warnings. deploy_get_content lists
  every project asset with whether it ships and why, named_folders points at
  folders scripts build asset paths in (with the scripts that do), and
  orphan_rules lists rules on paths that are gone. While a deploy runs, project and settings changes are refused.
- Android: project_deploy with an Android profile (settings_profiles_create
  platform android makes one) builds <project>.apk; with run_after_deploy it is
  installed and started on android_device, and the game's log reaches the
  editor log as [Device] lines (logs_get_recent include_device_system adds the device system's own
  [Device System] lines). android_device_action acts without a new deploy: run_last_build,
  follow_log, clear_data, uninstall.
- Driving and verifying the game needs synthesized input - see the
  verification skill.

## Conventions

- scene_create_batch lists new entities under "created" (id, name, parent_id).
  Batch setters return {"ok":true,"count":N} on success; a rejected item
  rejects the whole call and nothing changes.
- Entity ids are stable uuids that survive save/reopen - cache them instead of
  re-finding by name.
- Asset keys: "app:/data/..." project data, "engine:/..." engine data (read
  only). Tools that take an asset accept a key, a path relative to app:/data,
  or the asset's uid.
- Units: meters, seconds, degrees. Colors are RGBA 0..1 arrays. Numbers come
  back exactly as stored, so writing back what a read gave changes nothing.
- One tool call = one undo step (edit_undo / edit_redo mirror Ctrl+Z/Y and
  take a count).
- Unknown arguments are reported in "warnings" next to the result - read them,
  they are usually a typo.

## Working alongside the user

- The user sees the same editor. selection_get shows what they have selected -
  useful context for "this/the selected one" requests. When you finish a task,
  selection_set {"entity_ids":[...]} on the result and viewport_focus
  {"entity_ids":[...]} point the user at what you built; selection_set
  {"entity_ids":[]} clears the selection, selection_set {"asset":...} selects
  an asset in the Content Browser, and assets_reveal {"key":...} opens its
  folder there and scrolls to it.
- panel_focus {"panel":"scene"|"game"} switches the center panel ("mcp"
  opens the MCP Server window);
  window_request_focus raises the editor window.
- After imports, loads and play sessions check logs_get_recent (min_level
  "warn") - compile problems and runtime errors land there. Page with after_id.

## Do

- Prefer batch tools; group related edits into one call for clean undo.
- Give entities meaningful names at creation - scene_find_entities_batch works
  by name and future sessions depend on it.
- Fetch the other skills (skills_list / skills_get) before scene building,
  scripting, animation, asset or verification work.

## Do not

- Do not bypass tools with filesystem writes into the project.
- Do not churn the user's selection mid-build; set it once at the end.
- Do not assume a tool failed silently: every tool returns isError with a
  reason - read it and adapt instead of retrying the identical call.
