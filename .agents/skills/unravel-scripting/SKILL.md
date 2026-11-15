---
name: unravel-scripting
description: >-
  Covers UnravelEngine C# scripting: script_system, the C++/C# glue and bindings, the engine's C#
  API in engine_data/data/scripts, script builds and reloads, script member serialization, async
  script code, and dotnetpp/clrpp (CoreCLR bridge, icall weaver). Use when exposing an engine
  feature to C#, when scripts do not compile, reload, keep their values or leak a domain, or when
  editing deps/dotnetpp.
---

# Unravel scripting

The engine side of C# scripting (`engine/engine/scripting/`: `script_system`, glue, per-area
bindings), the engine's C# API (`engine_data/data/scripts/`), script member serialization
(`engine/engine/meta/ecs/components/script_component.cpp`), the script library build (the editor's
`compile<script_library>`) and the embedding library `deps/dotnetpp/` (`dotnet::` API, the clrpp
CoreCLR bridge and its managed side). Writing game scripts and the MCP script workflow are in
`editor_data/data/mcp/skills/scripting-api.md` and `editor_data/data/mcp/skills/scripting.md`.

## Gotchas

Glue and API:

- A binding that reaches engine state registers with `script_internal_call(f)`
  (`engine/engine/scripting/ecs/systems/bindings/script_glue_common.h`): off the script thread it
  raises `InvalidOperationException` in C# and returns a default. `dotnet_internal_call(f)` is only
  for calls safe on any thread (logging).
- Engine code reaches the runtime through `dotnet::` (`<dotnetpp/dotnetpp.h>`); converter
  specializations live in `dotnetpp_backend::managed_interface`. The build forces
  `DOTNETPP_BACKEND=coreclr` (`deps/dotnetpp/CMakeLists.txt`) and monopp is not built; the backend
  `#if`s that exist stay where they are.
- Blittable payloads mirror a managed struct field for field (`script_system::frame_update_data`,
  `ui_event_payload`, the POD converters in `script_interop.h`): change both sides together.
- A script API extends the engine's own object instead of adding parallel value types or
  per-property parameters: per-draw material values are a `MaterialInstance` made from a material
  (`new MaterialInstance(material)`, passed as `materialInstance:`). Before building an API the user
  described in words, show the two or three lines a script would write and confirm them. Material
  instance semantics: `unravel-materials`.
- Public C# under `engine_data/data/scripts/` (outside `samples/` and `test/`) carries XML docs:
  `summary`; `param` and `returns` on methods; a summary per enum value. `scripts_api_search` serves
  them (`ApiIndex.cs`). A public signature change breaks user projects.
- Never expose a raw native pointer to C#: pass entity ids, asset uids or handles the glue resolves
  per call (`get_entity_from_id`).

Script members:

- A script's saved members are its public fields and public properties (not static, no `[Hide]`). A
  public get-only property fails on every load ("did not load and keeps its current value"): make it
  internal or protected, or give it a setter.
- Member loads stay on `try_load`; `load_mono_member` catches only what fails around it (assigning
  the value), so one bad member keeps its value and the entity's other members and scripts load.
- Arrays and lists load as the element type the member declares (`try_load_mono_collection`), and
  dictionaries into the dictionary the member holds; a type name saved in the file is never resolved.
- Type lookup by name takes full names only (`FindType` in
  `deps/dotnetpp/dotnetpp/clrpp/managed/Bridge.Runtime.cs`, `script_system::get_type_by_fullname`):
  CoreLib's `Vector2/3/4`, `Quaternion`, `Random` and `Range` collide with the engine's global types.
  Never add a simple-name fallback.
- A new member type (an asset type, a math type) needs a save and a load entry in
  `script_component.cpp`, a widget in `editor/editor/hub/panels/inspector_panel/inspectors/inspector_script.cpp`
  and a conversion in `editor/editor/system/mcp/mcp_component_utils.cpp`.

Builds and reloads:

- The engine owns no compiler: the editor installs its script build through
  `script_system::set_library_builder`; the player only loads prebuilt libraries.
- `library_files_mutex` (`script_system.cpp`) guards every build's fixed temp outputs and the copies
  into place, for all protocols. `copy_compiled_lib` runs under it: never lock inside it.
- A protocol whose build is still running is queued again, never stacked; a newer
  `compilation_version` drops the reload of an older build.
- A scheduled build waits until the asset manager holds every `.cs` the asset database lists
  (`is_waiting_for_sources`, at most `SOURCE_WAIT_LIMIT`): each file arrives after its own compile job.
- Play start skips the app build when `is_library_current`: the last build succeeded in the same debug
  mode, from the same source generation and sources stamp (paths and write times), and its dll exists.
  Any other build input bumps the generation through `set_needs_recompile` (an engine change also
  bumps the app's).
- A library moves with its pdb, mdb and xml (`copy_compiled_lib`), and a companion the source lacks is
  removed at the destination. Exceptions name the script line only when the pdb lies beside the dll.
- The editor's build always writes a pdb (`params.debug_symbols` in
  `editor/core/asset_compiler/asset_compiler.cpp`): `get_script_source_file` finds a type's file by
  `[ScriptSourceFile]`, then the pdb (`ScriptSources.cs`), then the protocol's only script named after
  the type. Deploy drops pdbs unless `deploy_debug_symbols`.
- A recompile reloads every open scene through a checkpoint (`editing_manager::on_script_recompile`):
  undo history is cleared, scripting is unloaded before the domains, and every script's values
  round-trip through save and load. Scene handles taken before it are stale (`unravel-ecs-component`).
- A static in a surviving assembly that holds script instances, script `Type`s or delegates pins the
  unloaded app context: mark its class `[AutoStaticsCleanup]`. When an unload reports a leak, read the
  roots the report names before adding a cleanup. Details: [domain-unload.md](domain-unload.md).

Async script code:

- Never block the script thread on async code (`.Result`, `.Wait()`, `GetAwaiter().GetResult()`): the
  continuation needs that thread, so the game freezes. On CoreCLR the wait is logged after 3 s
  (`BlockedWaitReportMilliseconds`).
- Awaitables are not pooled: a kept one can be cancelled or awaited again, and token callbacks arrive
  from other threads after the wait ended. Model and rules: [async-scripts.md](async-scripts.md).

dotnetpp:

- The icall weaver (`deps/dotnetpp/dotnetpp/clrpp/managed/Weaver.cs`, Mono.Cecil) rewrites the dll and
  pdb inside the script build, never at load: a game with prebuilt libraries does not need Cecil. It
  strips the pdb's TypeDefinitionDocuments records, which Cecil's round trip corrupts, and any other
  per-type debug info Cecil reads lazily during Write lands on another type.
- `Clrpp.Managed.dll` and the native bridge check each other's export table at start (`clr_bridge.cpp`,
  "bridge export count mismatch"): a new export redeploys both.

## How it works

- Script thread: `script_system::init` records it (`get_script_thread_id`). Updates run in the frame
  events at `frame_update_priority::scripts` and `scripts_late` (`engine/engine/events.h`);
  `on_frame_begin` sets the managed `Time` before physics runs FixedUpdate and collision callbacks.
  Play hooks: `on_play_begin` -1000, `on_play_end` 1000, `on_pause` 100, `on_resume` -100.
- Domains: the engine library (`engine:` scripts, `Unravel.Core`) and the app library (`app:`
  scripts) load into separate collectible contexts; an app type shadows an engine sample of the same
  name (`append_engine_scriptable_component_types`).
- Build: a `.cs` change calls `set_needs_recompile`; `check_for_recompile` schedules
  `create_compilation_job` on the pool; a successful build raises `events::on_script_recompile`, and the
  editor reloads the domains and the scenes (above).
- Native components reach C# through `native_comp_lut::register_native_component` in
  `bindings/scene_bindings.cpp` and one `register_*_script_bindings()` per area
  (`bindings/script_bindings.h`). Not exposed on purpose: `tag_component` / `layer_component`
  (`Entity.name`, `Entity.tag`, `Entity.layers`), `prefab_component` / `prefab_id_component` (`Prefab`
  assets and `Scene.Instantiate`), `root_component` / `active_component` (active is on `Entity`),
  `script_component` (hosts user `ScriptComponent` subclasses), `test_component`.

## Procedures

- Expose an engine feature, a component or a value type to C#: [add-script-api.md](add-script-api.md).
- Domain unload, `[AutoStaticsCleanup]`, leak reports, bridge files: [domain-unload.md](domain-unload.md).
- Async model, cancellation and the pump: [async-scripts.md](async-scripts.md).
- Requirements: a .NET SDK at or above `CLRPP_DOTNET_VERSION` (9.0,
  `deps/dotnetpp/dotnetpp/clrpp/CMakeLists.txt`) on `PATH`; `dotnet --info` lists it.

## Verify

- Build `engine` and `editor`; the engine tree's build also builds and deploys `bin/clrpp/`.
- No `unravel-tests` suite drives scripts. Smoke in an editor over MCP: write a script
  (`scripts_set_sources_batch`), wait for the build (`scripts_get_compile_status` with `wait`), attach
  it, set a member, save, reopen the scene in a fresh editor and read the member back, enter and leave
  play, then recompile and check the log for an unload leak report.
- dotnetpp changes: its own suite, [domain-unload.md](domain-unload.md) "Testing dotnetpp".

## Entry points

| Purpose | Path |
|---------|------|
| Script system: domains, builds, events, callbacks | `engine/engine/scripting/ecs/systems/script_system.h` |
| Glue entry, internal call registration | `engine/engine/scripting/ecs/systems/script_glue.cpp`, `engine/engine/scripting/ecs/systems/bindings/` |
| POD converters | `engine/engine/scripting/ecs/systems/script_interop.h` |
| Script member save and load | `engine/engine/meta/ecs/components/script_component.cpp` |
| Script component | `engine/engine/scripting/ecs/components/script_component.h` |
| C# component base, async scope | `engine_data/data/scripts/scene/components/ScriptComponent.cs` |
| Script library build | `editor/core/asset_compiler/asset_compiler.cpp` (`compile<script_library>`) |
| `dotnet::` API | `deps/dotnetpp/dotnetpp/dotnetpp/dotnetpp/dotnetpp.h` |
| CoreCLR bridge, managed side | `deps/dotnetpp/dotnetpp/clrpp/clrpp/clr_bridge.cpp`, `deps/dotnetpp/dotnetpp/clrpp/managed/` |
