# Unravel game project - agent instructions

Always-on rules for AI coding agents working in this project. The editor rewrites this file
each time it opens the project, so this project's own rules belong in `AGENTS.md`.

## Layout

- `app:/` is this project root. Source content and scripts live under `app:/data/`.
- Asset keys look like `app:/data/Foo/Bar.ext` - never use OS absolute paths for assets.
- Put all game `.cs` scripts under `data/` (any subfolder). Scripts outside `data/` are not compiled.
- Do **not** edit `compiled/`, or change `.meta` UIDs, unless the user explicitly asks.
- A new project may have no saved scene yet; scenes persist as `.spfb` under `data/` after save.

## Editor MCP tools

- While the Unravel editor runs it serves MCP tools for scenes, entities, components, assets,
  materials, scripts, play mode and viewport captures. Its MCP panel shows how to connect.
- With the tools connected, call `skills_list` before any non-trivial task and fetch the
  relevant guides with `skills_get`, starting with `getting-started`. They match the running
  engine and give the correct tool order.
- Search the engine's C# API with `scripts_api_search` and `scripts_api_get`.
- Mutating tools refuse play mode: stop it with `play_set_active` before editing.
- Without the tools, scripts and UI files can still be edited as text. Everything under
  Assets below waits for the editor.

## Engine API

- Engine script sources live in the parent `data` folder of the `engine-script.dll`
  referenced by the project's `.csproj`. Read those before inventing helpers.
- Use any public function and class defined there.
- Prefer provided engine utilities: math (`Vector2`/`Vector3`/`Quaternion`/`Mathf`/...),
  `Random`, `Physics`, `Input`, `Assets`, `Scene`, components, UI, gizmos, logging, etc.

## Scripts

- Inherit `ScriptComponent` and keep `[ScriptSourceFile]` on the class.
- Lifecycle: `OnCreate` -> `OnEnable` -> `OnStart` (play mode) -> `OnUpdate` /
  `OnFixedUpdate` / `OnLateUpdate` -> `OnDisable` / `OnDestroy`.
- Load assets with `Assets.GetAsset<T>("app:/data/...")` (or by `Guid`), not a Unity-style Load API.
- Prefabs: `Scene.Instantiate("app:/data/....pfb")`. Scenes: `Scene.LoadScene(...)`.
- Hot-reload: avoid static caches of `Type`, delegates, or script instances across recompile;
  use `[AutoStaticsCleanup]` when statics must be cleared.

## Entities

- `Entity` is a **struct** and cannot be compared to `null`.
- Use `entity.IsValid()`, or the bool conversion: `if (entity)` / `if (!entity)`.

## Assets

- Do **not** write asset files by hand (scenes, prefabs, materials, `.meta`, compiled
  assets): create and change them through the editor - its MCP tools or the Content
  Browser - which run the real setters and keep ids and sidecars right.
- Import through the editor (MCP `assets_import_files` or the Content Browser) rather
  than hand-copying files into the project without sidecars.

## Working style

- Gameplay hooks (`OnStart` / `OnUpdate`) run in play mode; do not assume edit-time
  mutations apply while playing.

## RCSS / RmlUi

- Do not use the `solid` keyword.
- Only use RmlUi-supported properties:
  - https://mikke89.github.io/RmlUiDoc/pages/rcss/property_index.html
  - https://mikke89.github.io/RmlUiDoc/pages/rcss/font_effects.html
  - https://mikke89.github.io/RmlUiDoc/pages/rcss/filters.html
- Color alpha is **0-255**, not 0-1.
- In `.rcss` files measure in **`dp`**, never `px`. A `dp` is the same apparent size on every screen, like a CSS pixel, so `16dp` is readable on a desktop and on a phone. A phone has less room (about 850 x 380 dp held sideways): answer it with `@media (max-height: 500dp)` and `(max-width: 600dp)`, and never scale from a script. The recipe is in the MCP skill `ui`, section "Fitting every screen".
