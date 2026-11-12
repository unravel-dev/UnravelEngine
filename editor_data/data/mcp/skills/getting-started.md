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
3. Tools that edit the scene, prefabs, components, project settings or scripts
   refuse play mode. Stop play (play_set_active {"active":false}) before
   editing, or make edits first and then enter play mode. Three change the
   running game instead, as the Inspector does while playing:
   scene_set_component_properties_batch, scene_set_transforms_batch and
   scene_set_active_batch. Their result says temporary: true, edit_undo takes
   such a change back until play stops, and stopping play drops it. Reads,
   captures, waits, play controls, input and UI tools, scripts_invoke,
   animation_set_parameters, asset imports and the deploy tools work while
   playing; input_send_mouse, input_send_touch, input_send_gamepad,
   ui_click_element and scripts_invoke need it.
4. Components must exist before their properties are set: add them with
   scene_add_components_batch (a C# script is component "Script" with its
   type_name), then configure them with scene_set_component_properties_batch.
5. MCP writes assets only inside the project's data folder. Name a location
   as a key ("app:/data/Props/Crate.mat") or as a path relative to it
   ("Props/Crate"); anything else is refused. Besides assets, the settings
   tools write the project settings and its profiles, deploy_set_settings the
   deploy settings, and deploy_keep_release / deploy_remove_release the kept
   releases, each in its own project folder; project_create / project_open
   write whole project folders, the deploy tools the deploy folder, and
   assets_import_batch reads its source files from outside.
6. Arguments are checked against each tool's input schema before it runs: a
   wrong type, an unknown enum value, a number out of range or an empty batch
   refuses the call with the reason, and nothing changes. Unknown argument
   names come back as warnings.

## Session bootstrap

1. editor_get_status - project, scene (with unsaved_changes), prefab mode,
   play state and pacing (time scale, vsync, max fps, script debug mode) in
   one call. Always start here. modal names a dialog the editor waits on (a save prompt
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
  (desktop, android or web) and overrides only what differs from the project
  settings. settings_profiles_list lists them (a new project starts with
  Desktop, Android and Web, and deploys Desktop);
  settings_profiles_create / _rename / _delete manage them. settings_get and
  settings_set with profile read and override a profile's values (overridden
  lists what it changes), settings_profiles_revert stops overriding paths. Layers,
  input, resolutions and the physics backend are shared by every profile.
  settings_profiles_set_preview runs the editor with a profile's settings; then
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
  its arguments. location is always shared; patch_release, desktop_icon,
  desktop_graphics, steam_*, android_* and web_* always belong to the profile.deploy_profile selects the profile deploys build, as
  the Deploy window's picker does; profile alone never changes it. The result
  says what was written where and which shared values a profile shadows.
  deploy_start (profile: which one) builds the profile into
  <location>/<profile> once running compiles finish. editor_open_deploy opens
  the Deploy window for a user who should see it. A profile's textures are cooked for its platform (Android: ASTC,
  ETC2, or BC, which ships the editor's own files) into cache/profiles/<profile>, each recompiled in place when a setting
  changes. An asset that failed to compile fails
  the deploy (failed_steps names it) before the folder is touched. Compiled
  assets ship in data/<engine|app>/content.pak (pack_assets false ships them
  loose). Deploying again into the same folder updates it in place and only
  copies what changed. To ship updates as small patches: deploy, then
  deploy_keep_release (named by app.version), then deploy_set_settings
  patch_release to that name - later deploys ship the release's packs
  unchanged plus content_p1.pak with every change since; deploy_remove_release
  deletes a kept release. Releases are kept per profile, with the build's
  executable and the player's debug symbols (to read crash dumps of it). To
  upload a desktop profile to Steam: deploy_set_settings steam_app_id,
  steam_depot_id, steam_account (logged in once in a terminal with steamcmd
  +login <account>) and optionally steam_branch, deploy it, then
  deploy_upload_steam. To ship only what the game uses: deploy_set_settings
  only_used_assets true, scenes (the scenes scripts load besides the startup
  scene) and include (folders or assets scripts load by a path built at run
  time); keys that name nothing come back as warnings. deploy_get_content lists
  every project asset with whether it ships and why, named_folders points at
  folders scripts build asset paths in (with the scripts that do), and
  orphan_rules lists rules on paths that are gone. While a deploy runs, project and settings changes are refused;
  deploy_cancel stops it. A deploy builds the project's scripts optimized (Compiling Scripts), whatever mode the
  editor's scripts run in. A deploy that failed or was cancelled leaves its folder marked as unfinished (build.stamp):
  deploy_keep_release and deploy_upload_steam refuse it until the profile is deployed again.
  Beyond the engine's own files: extra_files names a folder of the project whose files ship as they are (a library
  beside the executable, an index.html of the game's own on the web), post_command a command run once the build is
  whole (sign, archive, upload), android_additions a folder with Java, resources, libraries and a manifest the
  package takes. desktop_graphics limits a desktop build to some graphics APIs, which drops the other APIs' shaders.
  trim_framework leaves out the .NET framework libraries no script and no engine library references (smaller
  desktop, Android and web builds); a library a script loads by a name it builds at run time must then be named in
  kept_assemblies, or the game throws FileNotFoundException where it loads it.
- Android: deploy_start with an Android profile (a new project has one;
  settings_profiles_create platform android makes more) builds <project>.apk; with run_after_deploy it is
  installed and started on android_device, and the game's log reaches the
  editor log as [Device] lines (logs_get_recent include_device_system adds the device system's own
  [Device System] lines). deploy_android_device acts without a new deploy: run_last_build,
  follow_log, clear_data, uninstall.
- Web: deploy_start with a web profile builds a folder for a web server; with
  run_after_deploy the editor serves it on this computer and opens the page.
  deploy_share_web shares the profile's last deploy at a public HTTPS link (a
  Cloudflare quick tunnel; needs cloudflared) for a phone or a tester, until
  action stop or the editor closes.
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
  they are usually a typo. A tool or argument called by a name it had before a
  rename still works, with a warning naming the current one.
- The editor's Build and Developer menu commands run through
  editor_run_command (reflection captures, prefab graph validation, prefab and
  color space migrations, recompiling the project's shaders, meshes, UI,
  scripts or everything, the agent files); scripts_set_debug_mode switches the
  scripts between debug and release builds.
- The skills are also MCP resources (resources/list, resources/read at
  unravel://skills/<name>) for clients that read resources.

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
  editor_focus_os_window raises the editor window.
- After imports, loads and play sessions check logs_get_recent (min_level
  "warn") - compile problems and runtime errors land there. Page with after_id.

## Do

- Prefer batch tools; group related edits into one call for clean undo.
- Give entities meaningful names at creation - scene_find_entities works
  by name and future sessions depend on it.
- Fetch the other skills (skills_list / skills_get) before scene building,
  scripting, animation, asset or verification work.

## Do not

- Do not bypass tools with filesystem writes into the project.
- Do not churn the user's selection mid-build; set it once at the end.
- Do not assume a tool failed silently: every tool returns isError with a
  reason - read it and adapt instead of retrying the identical call.
