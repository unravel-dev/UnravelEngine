---
name: unravel-build-verify
description: >-
  Builds and verifies UnravelEngine: the configured CMake trees, engine_data and
  editor_data, the unravel-tests runner and its suites, the web and Android player
  templates, first-party library suites, bgfx pin moves and patches, the GitHub workflows
  and packages. Use after a code or data change, before calling work done, or for a
  configure, compile, link, test or CI failure.
---

# Build and verify

Building the configured trees, the data targets, the test runner, the player templates, the
first-party library suites and the GitHub workflows. Owns `CMakeLists.txt`,
`CMakePresets.json`, `cmake/`, `.github/` and `tests/`. AGENTS.md ("Build and test") holds the
tree, target and job-count rules; this skill holds the detail behind them. Shader compile
checks belong to `unravel-rendering`, editor smoke checks over MCP to `unravel-editor-mcp`,
measurements and A/B runs to `unravel-debugging`.

## Gotchas

Trees and targets

- The test runner's CMake target is `tests`; `unravel-tests` is only its output name.
- `--target editor` (or `game`, `tests`) copies no data. `engine_data`, `editor_data`,
  `web_player` and `android_player` are in the default build: a plain
  `cmake --build build/<Config>` or the target's name builds them.
- A desktop deploy ships `<bin>/unravel-player.exe` as it was last built, and `--target editor`
  does not rebuild it. After an engine change, build `game` before a deploy: an older player
  compiles graph materials differently from the editor that cooked their shaders (surfaces
  drawn black, "do not make a valid program" in the game's log).
- `engine_data` copies the whole `engine_data/` folder, the committed `engine_data/compiled/`
  included, over `<bin>/data/engine/`, replacing local compiles there. Built while an editor
  runs from that bin, it makes the editor recompile every asset and can leave it broken (no
  Scene panel camera): stop that editor first, or copy only the changed file (shader edits:
  `unravel-rendering`, shader-change.md).
- `engine_data` copies bgfx's `bgfx_shader.sh` and `bgfx_compute.sh` into
  `engine_data/data/shaders/` in the source tree on every build; edits to those two files
  vanish.
- Undefined symbols for every member of a new class mean the tree was not re-configured after
  adding the file; that failed link also removes the old executable from `<bin>`. A host
  configure re-configures the web and Android player sub-builds as well.
- Unity batches (CMake's default of 8 files, `UNITY_BUILD_UNIQUE_ID ANONYMOUS`) merge the
  anonymous namespaces of their files, and an added file shifts every later batch, so old
  pairs can meet. A wrapper namespace plus `using namespace` does not isolate names; give
  file-local names a feature-specific spelling or keep them in the feature's header. The core
  libraries `engine/core/graphics`, `engine/core/math`, `engine/core/seq` and
  `engine/core/serialization` are unity builds in Debug too.
- Windows trees build with clang++ in GNU-driver mode against the Microsoft STL (the release
  preset `windows-x64-clang-msvc-gnu` does the same): CMake's `MSVC` is false there. Test the
  capability (`check_cxx_source_compiles`) instead of `if(MSVC)`.
- With clang and the Microsoft STL, `std::find`, `count`, `remove` or `replace` on a type with a
  defaulted `operator==` whose size is not 1, 2, 4 or 8 bytes fails the `static_assert`
  "unexpected size" in `_Find_vectorized`; use the `_if` form with a lambda.
- The desktop and Android player templates are built in the editor's configuration; the web
  player sub-build is always Release. Do not pin another configuration for speed; state the
  cost and let the user choose.
- An editor started with `DOTNET_ROOT` set looks for its .NET runtime only there: start it
  from a shell without the web SDK's `DOTNET_ROOT`.
- A shaderc run that fails with no error text crashed: run `<bin>/shaderc` on that file and
  read the exit code (0xC00000FD is a stack overflow; shaderc links with the 8 MB reserve of
  `unravel_set_default_thread_stack` in `CMakeLists.txt`).
- An out-of-memory build: look at what else was running before lowering the job count, and
  ask first. The player sub-builds have their own job counts (`UNRAVEL_WEB_PLAYER_JOBS`,
  `UNRAVEL_ANDROID_PLAYER_JOBS`).

Running and testing

- Build in the foreground, or wait for the build's own notification, and confirm the link
  line and the executable's time stamp before any run that depends on the change. A build
  chained into a background run hides its failure, and the run measures the old binary.
- The runner exits with 1 when a suite fails, times out or crashes; its last log line counts them
  ("N failing check(s); M suite(s) did not pass"). With more than one suite it runs them in
  worker processes and prints a failing suite's whole output at its end; `--jobs 1` runs them in
  one process as they go.
- A runner whose own CPU time stays flat while `shaderc` children run is working, not hung.
- A test that asserts a duration waits on the clock it checks (loop on `steady_clock::now()`
  with short sleeps); one `sleep_for` can end early by part of a timer tick (15.6 ms on
  Windows).
- Shipped editors are always Release; Debug and RelWithDebInfo trees exist only to develop
  the engine. Deploy and store safeguards cover what users of a shipped editor can reach, not
  states only engine developers are in.

CI and tools

- Run a workflow only when the user asks (runner minutes):
  `gh workflow run <file> --ref <branch>`. Fix what a run does, never when it runs: the
  commented `#on: [push, pull_request]` line records that choice. `samples.yml`'s daily
  schedule is the one exception the user chose; add no other. When a subagent touched a
  workflow, diff its `on:` block against HEAD.
- A comment or a CI pin is not proof of a tool limit: run that tool version before writing a
  limit into docs, or call it unverified.
- Version safety checks stay strict during A/B and bisect work; ask before relaxing one to
  stop its prompts.

Shell ([shell.md](shell.md) has the recipes)

- The sources mix CRLF and LF files; `sed -i` rewrites a CRLF file as LF. Edit with the Edit
  tool or Python on bytes.
- Backslashes in Bash heredocs and non-raw Python strings are unescaped before they reach the
  file; patch scripts use raw strings in a file written with the Write tool.
- Long drivers start with the Bash tool's `run_in_background`, never a trailing `&`.

## How it works

- Targets: `deps` -> `engine` (static) -> `editor` (`unravel-editor`), `game`
  (`unravel-player`; `main` on Android) and `tests` (`unravel-tests`, which also links the
  editor libraries `deploy`, `asset_compiler`, `packages`, `update_check` and
  `sample_catalog`). The default
  build also builds the data targets and, when their tools are found, the player templates,
  after the editor while `UNRAVEL_PLAYERS_AFTER_EDITOR` is on.
- The runtime folder is `<tree>/bin` (`<tree>/bin/<Config>` with a multi-config generator):
  the executables, `shaderc` / `texturec` / `geometryc`, `data/engine`, `data/editor`,
  `clrpp/`, `players/web`, `players/android-arm64`, `Log.txt` (rewritten at each start) and
  `CrashLog.txt` / `CrashLog.dmp`.
- `engine/engine`, `editor/editor` and `tests/tests` glob their sources when CMake configures
  (`file(GLOB_RECURSE)`).
- The runner boots the engine as a service (`tests/tests/tests.cpp`) with threading and the
  asset manager only: no window, renderer, audio, scripting or ECS systems; a suite that needs
  a renderer starts bgfx's Noop one. Each suite's test assets live under
  `<bin>/test_results/<suite>/app` (protocol `app:`), emptied when the suite starts. Shader and
  data suites read the source tree through compile definitions
  (`GI_TESTS_SHADER_DIR`, `ENGINE_DATA_DIR`, `EDITOR_DATA_DIR` in `tests/tests/CMakeLists.txt`)
  and run the `shaderc` beside the runner.
- bgfx is fetched with FetchContent at the pin of the bgfx.cmake fork and patched at every
  configure ([dependencies.md](dependencies.md)).

## Procedures

Build and check a change:

1. `ls build/*/CMakeCache.txt` lists the configured trees.
2. A source file was added or removed: `cmake build/<Config>` first.
3. `cmake --build build/<Config> --target <targets>`, with `engine_data` / `editor_data`
   when files under those folders changed. New engine or editor files: build
   `build/RelWithDebInfo` too (unity batches).
4. Run the suites of the touched area ([test-suites.md](test-suites.md)).
5. Launch from the runtime folder that holds `data/` and `clrpp/`, with it as the working
   directory. When a fresh executable was linked elsewhere, copy it into that folder.

Reference files:

- [test-suites.md](test-suites.md) - runner options and every suite with what it covers
- [players-and-ci.md](players-and-ci.md) - player templates, the browser page's Node tests,
  workflows, release packages
- [dependencies.md](dependencies.md) - first-party library suites, bgfx pin moves and patches
- [shell.md](shell.md) - Git Bash on this tree: line endings, escapes, background jobs,
  processes

## Verify

| Changed | Build | Check |
|---------|-------|-------|
| engine code | `engine` and its users (`editor`, `game`, `tests`) | the area's suites |
| editor code | `editor`; `tests` when `editor/core/` changed | the area's suites, a clean editor start |
| a new `.cpp` or `.h` | re-configure, then RelWithDebInfo as well | the unity batches compile |
| `engine_data/`, `editor_data/` | `engine_data` / `editor_data` | the editor imports them without errors |
| `game/web/wwwroot/` scripts | none | `node --test game/web/tests/*.mjs` |
| CMake files | re-configure both trees | `cmake -DRUNTIME_DIR=build/<Config>/bin -P cmake/check_package.cmake` |
| a first-party library | the library alone ([dependencies.md](dependencies.md)) | its `<lib>_test` |
| skills, `AGENTS.md` | `tests` | `unravel-tests --suite "agent docs"` |

A clean editor start: `<bin>/Log.txt` holds no `[error]` or `[critical]` line; how to start
and stop an editor and read its log is in `unravel-editor-mcp`.

## Entry points

| File | What it holds |
|------|---------------|
| `CMakeLists.txt` | options, data targets, shaderc stack, install layout |
| `engine/engine/CMakeLists.txt`, `editor/editor/CMakeLists.txt` | source globs, unity settings |
| `tests/tests/tests.cpp`, `tests/tests/tests.h` | runner boot, options, the three run modes |
| `tests/tests/harness.h` | what suites check with, suite registration and lanes |
| `tests/tests/runner.h` | results, the watchdog, worker protocol, scheduler and report |
| `tests/tests/CMakeLists.txt` | runner libraries, source-tree paths |
| `cmake/web_player.cmake`, `cmake/android_player.cmake` | player sub-builds and their options |
| `cmake/check_package.cmake` | what a runtime folder or a package must hold |
| `deps/3rdparty/bgfx/CMakeLists.txt` | bgfx pin, patch step, build options |
| `.github/workflows/release.yml` | release packages, Linux images and compilers |
