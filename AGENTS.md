# UnravelEngine

C++20 game engine with an ImGui editor, a bgfx renderer and C# scripting (CoreCLR through
dotnetpp). Targets: Windows, Linux, macOS, Android and the browser (WebGPU / WebGL 2).

<!-- Maintainers: keep this file under ~150 lines. A line belongs here only if it holds for
all code and an agent would not learn it by reading the code. Area rules go to the owning
skill or a directory AGENTS.md; see .agents/README.md. -->

## Layout

| Path | Role |
|------|------|
| `engine/` | runtime library: `core/` utilities, `engine/` systems (ECS, rendering, assets, physics, audio, scripting, UI) |
| `editor/` | editor executable: `core/` (asset compiler, deploy, packages), `editor/` (hub, panels, MCP server) |
| `game/` | player runner (desktop, Android, browser) |
| `engine_data/`, `editor_data/` | shipped data: shaders, C# scripts, UI documents, MCP guides |
| `deps/` | first-party libraries (`ospp`, `threadpp`, `audiopp`, `dotnetpp`, `hpp`) and `deps/3rdparty/` |
| `tests/` | `unravel-tests`: every validation suite in one runner |

Skills in `.agents/skills/` hold each area's contracts, traps and procedures; area owners and
where each kind of knowledge goes: `.agents/README.md`.

## Build and test

- Build in the configured single-config Ninja trees `build/Debug` and `build/RelWithDebInfo`
  (there is no cache at `build/`). Never configure a new tree or compile outside them; the two
  exceptions (a first-party library's own suite, an A/B build of another commit) are in
  skill `unravel-build-verify`.
- `cmake --build build/<Config> --target <editor|game|tests|engine_data|editor_data>`; the
  `tests` target builds `bin/unravel-tests`.
  Ninja already uses all cores; pass no `-j`.
- Changed files under `engine_data/` or `editor_data/` reach the runtime only through their
  data target. A new `.cpp` needs a re-configure (sources are globbed).
- Outside Debug, `engine/engine` and `editor/editor` build as unity batches: names in
  anonymous namespaces must be unique across files. Check a RelWithDebInfo build.
- Run from `build/<Config>/bin/`, which holds `data/` and `clrpp/`.
- Tests: `build/<Config>/bin/unravel-tests --suite "<name>"` (substring match). Never run a
  suite beside a build or a busy editor, and put no hard timeout on shader suites.
- Details, suite list and CI: skill `unravel-build-verify`.

## Hard rules

- Code under `editor/` is never required at game runtime: `engine/` and `game/` never
  include from `editor/`.
- `deps/3rdparty/` is not edited (see `deps/AGENTS.md`).
- No new third-party library (submodule, FetchContent, vendored copy, package) unless the
  user names it. Ask first, naming the library and its license.
- Play mode: edit-only mutations never run while playing; respect the `play_mode` phases.
- ASCII only in every file written in the repository: code, comments, markdown, data.
- SDL is called only through `ospp` (`deps/ospp/`): add an ospp function when the engine
  needs something SDL offers.
- `try_load` (`engine/core/serialization/serialization.h`) is the one load API. When it
  misbehaves, fix it in place; never add a parallel load function.
- A platform quirk (long paths, file system behavior) gets one helper in
  `engine/core/filesystem/` that every caller uses, not a workaround in the feature.
- Slow derived data (distance fields, tables, shader programs) is computed when an asset
  is compiled or deployed, never on first use at run time.
- A deploy cooks every asset it ships for its profile (textures, audio, shaders, and graph
  materials at the profile's Material Quality) and ships those alone: the game never
  compiles a graph or picks among variants at load.
- A compile's by-products stay in the compiled cache beside the compiled file; `data/`
  folders hold only files people write.
- Engine version comparisons use all four parts: `1.2.0-320` is newer than `1.2.0-319`.
- Thread hand-overs never make either side wait. Use the project's concurrent queues
  (`engine/core/concurrency/`) before writing a new one.

## C++ conventions

- `snake_case` for types, functions, variables and files. Constants and macros: follow the
  file (`ALL_CAPS` or `k_` prefix).
- Use the `hpp::` types: `hpp::string_view` (`<hpp/string_view.hpp>`), `hpp::optional`,
  `hpp::variant`. The code builds on older standard libraries (Ubuntu 20.04: no
  `std::bit_cast`).
- Reflected types (`crtp_meta_type`) cast with `is<>`, `safe_cast<>` and
  `safe_pointer_cast<>` (`engine/core/reflection/registration.h`), never with
  `dynamic_cast`, `dynamic_pointer_cast` or an unchecked `static_pointer_cast`.
- A function that drives a sequence reads as a list of named steps: a long bool chain
  becomes a predicate named for its question, a lookup becomes a helper.
- Fix a missing operator or a wrong contract on the type or in the library, not around it
  at each call site. Fix a lifetime by sharing ownership, not by copying.
- Comments say what the code does and why, now: no history, no audit references, no
  measurements from past sessions, no other engine's names. Nothing that changes behavior
  goes into a comment.
- File formats use real directives and plain lists, not rule languages.
- Doxygen comments on public classes and functions.

## User-visible text

- Tooltips, meta `tooltip` attributes, inspector text, deploy problems and log lines a user
  reads are full sentences ending with a period; never "x; y".
- Never "Lumen", "Unreal", "UE" or another engine's name in anything a user can read:
  labels, pass and profiler names, log tags (use `[GI]`), MCP tool descriptions, commits.
- A field's value does not repeat its label.

## Changing a shared contract

When adding a component, reflected field, asset type or file format, check each:

- Serialization: ser20 SAVE/LOAD and `all_serializeable_components` in
  `engine/engine/meta/ecs/components/all_components.h`; a format change needs a version or a
  backward-compatible LOAD.
- Meta and inspector: `REFLECT` under `engine/engine/meta/`; inspectors under
  `editor/editor/hub/panels/inspector_panel/inspectors/`.
- Play mode: what runs in edit mode only, in play only, and what resets at play end.
- Prefab overrides: property paths through `serialization::path_context`.
- C# parity: `engine_data/data/scripts/` mirrors what scripts can reach, enums included.
- Assets: `.meta` uids stay stable; importers live in `editor/core/asset_compiler/importers/`.
- Editor MCP tools: they call the editor's own functions (`editor/AGENTS.md`).

Designs that span several areas: skill `unravel-architect`.

## Git and commits

- Change repository state (commit, merge, pull, push, stash, reset, rebase, restore,
  checkout of another ref) only when the user asks for that operation. Read-only git is fine.
- Before a commit, run `git diff --cached --stat`. The user stages their own work; when the
  index holds other changes, commit with a pathspec (`git commit -- <paths>`).
- Message: `[Area] What changed, as a sentence`, then `- ` bullets when needed. Areas in use:
  Rendering, Deploy, Scripting, Editor, Build, Engine, Assets, Physics, Core, Animation, UI,
  MCP, Audio. Never name a game, an agent, a model or an email.
- CI workflows (`.github/workflows/`) run by hand only (`workflow_dispatch`); never add push,
  pull_request or schedule triggers.

## Working here

- Reproduce on the user's scene, state and settings before choosing a cause, and measure a
  baseline in the same session before judging a change (skill `unravel-debugging`).
- When the user edits a tested set of changes, change only the items they name. A measured
  "no benefit" is data to report, not a reason to drop an item.
- Driving an editor over MCP: start your own on your own port; never call an editor you did
  not start (skill `unravel-editor-mcp`).
- Done means verified: the touched targets build in the configured trees and the relevant
  suites pass. Say what was not verified.
- When the user corrects you, fix the work and write the rule into the file that owns it
  (`.agents/README.md`, "When the user corrects an agent").
- `tasks/` is local scratch for plans and analyses (not in git); repository docs never link
  into it.
