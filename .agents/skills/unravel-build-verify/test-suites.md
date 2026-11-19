# unravel-tests suites

Every engine validation suite runs in one runner, `unravel-tests` (target `tests`, sources in
`tests/tests/`). A first-party library's own suite is in [dependencies.md](dependencies.md);
the browser page's Node tests are in [players-and-ci.md](players-and-ci.md).

## Contents

- Running
- Parallel runs
- Slow suites
- Rendering and shaders
- Materials
- Assets, files and packs
- Deploy, settings and editor libraries
- ECS, scenes and math
- Physics, animation, audio
- Input and UI
- Threading and profiler
- Agent docs
- Adding a suite
- Writing checks
- CTest

## Running

```bash
cmake --build build/<Config> --target tests
build/<Config>/bin/unravel-tests                        # every suite, in parallel workers
build/<Config>/bin/unravel-tests --suite "fs mirror"    # suites whose name contains the text
build/<Config>/bin/unravel-tests --jobs 1               # every suite in this process, one after another
```

| Option | Effect |
|--------|--------|
| `--suite`, `-s` | run only suites whose name contains the text |
| `--case` | run only the cases whose name contains the text (`test_round_trip`) |
| `--jobs`, `-j` | worker processes at once; 0 (default) one per two cores; 1 runs in this process |
| `--list` | print every suite with its lane, idle timeout and folder name, then exit (no engine boot) |
| `--strict` | a missing tool or an expected failure that passes fails the suite (CI) |
| `--no-tools` | skip the checks that run shaderc or texturec (the graph and shader checks in seconds) |
| `--idle-timeout` | seconds without progress before a suite is stopped; -1 (default) the suite's own, 0 never |
| `--junit` | write the results to a file as JUnit XML, a testcase per case |
| `--repeat` | run the selection this many times (flaky checks) |
| `--shuffle` | in-process order shuffled with this seed (order dependence) |
| `--verbose` | print every suite's output in a parallel run, not only a failing one's |
| `--worker` | run one suite, named exactly, as a worker (what the parallel run and CTest start) |
| `--bench`, `-b` | benchmarks as well as checks (`ecs serialization` timings, `fs watcher` latency, `mesh shapes` compiles every shape) |
| `--bench-only` | benchmarks, no checks |
| `--network` | also the checks that reach the internet (`update check`, `sample catalog`) |
| `--single-threaded` | engine option: every job, parallel loop and frame runs on the main thread, as in the browser (`engine/core/concurrency/threading_mode.h`) |

- One suite, or `--jobs 1`, runs in this process and prints as it goes, as before. More than one
  runs in parallel workers (next section).
- The runner exits with 1 when a suite fails, times out or crashes; its last log line counts them
  ("N failing check(s); M suite(s) did not pass"). Skips and expected failures pass.
- Each suite gets an empty `app:` folder of its own, `<bin>/test_results/<suite>/app`; temp files
  go to `<temp>/unravel-tests/<process id>/`, so a Debug and a RelWithDebInfo run at once share
  nothing.
- `mesh import corpus digest` is opt-in: `UNRAVEL_MESH_IMPORT_CORPUS=<list>` names the models;
  without it the suite reports SKIP.

## Parallel runs

The same executable is the parent and every worker: the parent starts
`unravel-tests --worker "<suite>"` once per suite with the other options it was given, reads the
worker's protocol lines (`@@unravel-tests ...`) and prints one line per finished suite, the whole
output of a suite that did not pass, and a table, slowest first, at the end.

- Lanes (`tests/tests/harness.h`, `suite_traits`): `normal`; `tools` (shaderc or texturec on many
  threads, takes a quarter of the slots); `exclusive` (timing or thread pool checks: run alone,
  one after another, after the rest). `--list` shows each suite's lane.
- The longest suites start first, by the times in `<bin>/test_results/durations.txt`, which every
  parallel run rewrites. A tools suite with no recorded time starts first.
- A worker logs to `<bin>/test_results/<suite>/Log.txt`; its output is kept beside it in
  `output.txt`. The parent itself still logs to `<bin>/Log.txt`.
- Nothing waits forever: a watchdog stops a suite that makes no progress (no case starting or
  ending, no check) for its idle timeout (300 s; 900 s for tools) and prints the case it was in;
  the parent stops a silent worker 120 s after that. A slow suite that keeps checking is never
  stopped. A crash ends only its own worker, reported as CRASH with the exit code and the case.

## Slow suites

These run `shaderc` (or `texturec`) found beside the runner; never beside a build or a busy
editor, and never under a hard timeout. Times are a full parallel run, RelWithDebInfo on a quiet
32-thread machine (about 100 s in all).

- `eshader files` - 60 s: every `.eshader` the engine and editor ship, for every renderer; its
  longest jobs are fxc compiles on `.dxbc` (`cs_gi_probe_trace`, `cs_gi_integrate`,
  `fs_gi_scene_debug`: 11-16 s).
- `fs watcher` - 37 s, sleeps for quiet periods; the exclusive lane runs it alone.
- `material graphs` - 30 s: every example graph and every node, every tested renderer.
- `gi constants / reference oracle` - 14 s beside the others: every GI shader for SM 5.0,
  GLSL 440 and SPIR-V.
- `settings profiles` - 6 s: cooks web shaders and texture variants, step by step.
- The compiles of `eshader files`, `gi constants` and `material graphs` run together
  (`test::run_parallel`) and each prints its slowest jobs: a suite lasts at least as long as its
  slowest single compile. Under `--single-threaded`
  they run one by one and take hours: run the other suites one by one there.

## Rendering and shaders

| Suite | Covers |
|-------|--------|
| `eshader files` | `.eshader` parser and errors, variants, inputs, `gfx::shader::find_variant`, compiled files from packs, every shipped shader on every renderer |
| `render feature shaders` | the feature requirement table against the committed compiled compute shaders |
| `render feature support` | which rendering features a device runs, from its capabilities |
| `feature limits` | project feature limits and render scale (`pipeline::apply_feature_limits`) |
| `render view` | texture descs, `gfx::render_view` texture access, the transient texture pool |
| `texture mip skip` | a texture made from a parsed image without its top mips: sizes, bytes, zero-copy ranges, the tail level |
| `texture residency` | the eviction planner and registry: drops to mip tails, rises, read kinds, accounting, the frame pass |
| `render change` | render change tracking: what an edit changes, per aspect |
| `render culling / batching` | CPU culling and batching, shadow culling |
| `shadow atlas` | where each light's shadow map sits |
| `shadow caching` | what shadow map caching keys on outside the shadow pass |
| `pre-exposure` | the view pre-exposure's GPU share and uniform layout |
| `fog` | height fog document round trip, volume blending, fog pass parameters, froxel grids |
| `specular occlusion / GTSO table` | the occlusion table against its reference integral |
| `shader constant tables` | no shipped shader indexes a constant table at run time |
| `gi constants / reference oracle` | GI constants in C++ against the shader mirror, the reference tracer, GI shader compiles |
| `gi bake / sdf / clipmap` | surface cache bake, distance fields, clipmap invariants |
| `raytracing backends` | the engine's BVH and Embree (`engine/core/raytracing`) on every backend the build has |
| `mesh shapes` | the built-in shapes (`engine_data/data/meshes`, `engine/engine/rendering/mesh_shape.h`) and their compile |
| `mesh skinning` | baking a skin into mesh load data |
| `skinning palettes` | the CPU side of the skinning palette buffer |
| `instanced draws` | scripted instance draws (`Graphics.DrawMeshInstanced`) |
| `particle emitter desc` | which kernel features an emitter description bakes |
| `particle motion` | the closed-form particle motion against a numeric reference |

## Materials

| Suite | Covers |
|-------|--------|
| `material graphs` | graph editing, the JSON format, types and conversions, the generated shader, every shipped example compiled with shaderc |
| `material instances` | a model slot's overrides of its shared material |

## Assets, files and packs

| Suite | Covers |
|-------|--------|
| `asset handle` | `engine/engine/assets/asset_handle.h` |
| `asset writer` | atomic writes through a temp file and the sweep of stale temp files |
| `asset packs` | pack write, mount, read, patches, damaged packs, the compression wrapper |
| `asset references` | the reference scan a deploy walks scenes with |
| `generated assets` | files an import writes beside its source (a model's default materials) |
| `audio clip encoding` | compiled clips keep the source codec; the deploy's asset index filter |
| `mesh import heuristics` | the importer's deductions on synthetic Assimp data |
| `mesh import corpus digest` | imports a model corpus (opt-in) |
| `fs protocols` | `fs::add_path_protocol`, `resolve_protocol`, `convert_to_protocol`, long paths |
| `fs mirror` | `fs::mirror_directory`, `copy_file_if_changed`, shared reads during a replace |
| `fs syncer` | `fs::syncer` mapping of the meta and compiled trees |
| `fs watcher` | created / modified / removed / renamed reporting, pause, filters (`--bench` about 50 s) |
| `user protocols` | the `user:` and `cache:` protocols |

## Deploy, settings and editor libraries

| Suite | Covers |
|-------|--------|
| `deploy library` | `editor/core/deploy/`: content selection, packing, stamps, web and Android builds |
| `assembly references` | `editor/core/deploy/assembly_references.cpp`: the .NET assembly reader and framework trim |
| `settings profiles` | profiles, sparse overrides, texture variant cooks, cooked web shaders |
| `content packages` | `.unravelpkg` export, import, update, removal |
| `update check` | `editor/core/update_check/` and `editor/core/https/`; `--network` adds the live checks |
| `sample catalog` | `editor/core/sample_catalog/`: samples.json, which list wins, the shipped copy and its pictures, cache, schedule, copies of samples among projects and their versions; `--network` adds the live checks |
| `third party notices` | the notices compiled into the engine (`THIRD_PARTY_NOTICES.txt`) |
| `engine version` | `engine/core/version/version.h`: parse and compare by commits since the tag |
| `boot config peek` | the cold-boot settings read from a project's settings before systems start |
| `display options` | the player's window mode, size, display, vsync and frame cap |
| `resolution presets` | the editor views' resolution presets |
| `permission requests` | `engine/engine/permissions/permission_requests.h` |

## ECS, scenes and math

| Suite | Covers |
|-------|--------|
| `ecs serialization / prefabs / cloning` | save and load, prefabs, overrides, cloning (`--bench` adds timings) |
| `scene groups` | additive loads, group unloads, persistent roots |
| `transform math` | `math::transform_t` and the rotation helpers |
| `curve` | `math::curve` and `math::gradient` |

## Physics, animation, audio

| Suite | Covers |
|-------|--------|
| `physics contacts / destroy funnel` | contact events, the entity destroy funnel |
| `box3d backend smoke` | the Box3D backend on the real scene |
| `box3d joints and ragdolls` | joints, ragdolls and their body properties |
| `animation` | the animation player, pose blending, blend spaces |
| `baked animation` | baked animation stores and instanced draws with states |
| `ik solvers` | IK solver correctness |
| `audio voice pool` | the voice pool that owns every playing source |

## Input and UI

| Suite | Covers |
|-------|--------|
| `input states` | key and button states between frames, positions in scaled windows |
| `input touch` | touches frame by frame (`Input.GetTouch`) |
| `input cursor` | the cursor lock and visibility hold |
| `input virtual controls` | the virtual gamepad and pointer zones |
| `ui document order` | draw and input order of screen-space documents |
| `ui system lifecycle` | the UI system outside its initialised lifetime |

## Threading and profiler

| Suite | Covers |
|-------|--------|
| `threading mode` | the single-threaded mode and its frame clock |
| `parallel pools` | the engine's two parallel pools (`engine/core/concurrency/parallel.h`) |
| `thread cpu time` | scope CPU time never exceeds wall time; sleeps are not busy |
| `profiler threads` | the profiler's per-thread event rings |

## Agent docs

`agent docs` checks that every backticked repository path and relative link in `AGENTS.md`
files and `.agents/` exists, that nothing names a file under the tasks folder, that the text
is ASCII, and that each skill's frontmatter names its folder.

## Adding a suite

- One file in `tests/tests/suites/` with `REGISTER_TEST_SUITE("name", run_function)`, or
  `REGISTER_TEST_SUITE("name", run_function, {.lane = ::unravel::suite_lane::tools})` for a lane;
  the function takes `rtti::context&` and returns nothing. Re-configure: the sources are globbed.
- The file says `using namespace unravel::test;` after its includes, outside every `#if` (one
  inside an `#ifdef _WIN32` include block compiles here and fails on Linux and macOS), and calls
  each case as `RUN_CASE(test_round_trip(temp));`: the case is timed, named in reports, filtered
  by `--case`, and an exception it throws fails it without ending the suite.
- A Windows build compiles none of a suite's `#else` / `#ifndef _WIN32` branches. After an edit
  that reaches them, or an edit made by a script over many suites, the Linux CI job is the check.
- Suites do not initialise threadpp or the asset manager: the runner already owns them.
- Nothing the suite does may outlive the process state it changed: a protocol it adds, the
  single-threaded switch, bgfx started on the Noop renderer. Each suite has a process of its own
  in a parallel run, but `--jobs 1` runs them all in one.
- A test of a first-party library's behaviour goes in that library's suite (`deps/AGENTS.md`).

## Writing checks

Everything is in `tests/tests/harness.h`; there are no per-file counters.

- `check(condition, what)`; `check_near(actual, expected, tolerance, what)` and `check_eq(...)`
  print both values on failure. A file's own helper for its types calls `check()`.
- `require(condition, what)` ends the case when it fails: for what the rest needs.
- `check_xfail(condition, what, reason)` marks a known defect: XFAIL while it fails, XPASS (a
  failure under `--strict`) once it holds, so it can become a `check()`.
- `skip(reason)` ends the case as skipped; `skipped(reason)` records a part that did not run and
  goes on. `use_tool("shaderc")` / `require_tool("texturec")` skip when the tool is not built
  beside the runner or `--no-tools` is given.
- Waits are bounded: `wait_until(is_done, timeout, what)`, or a spin with a deadline that fails a
  check. `run_tool(arguments, timeout)` runs a program, never `std::system`.
- `temp_path(name)` is the place for temporary files, never a fixed name under the temp folder.
- `run_parallel` / `run_parallel_map` run independent slow steps (compiles) at once; check the
  results after, in order, so the report reads the same every run. `note_progress()` keeps the
  watchdog quiet through a long step without checks.

## CTest

Not wired in; the runner is built so that it can be. A CTest discovery script would read
`unravel-tests --list` (name, lane, idle timeout, folder name, tab separated) and add one test
per suite running `unravel-tests --worker "<name>"`: lane `exclusive` -> `RUN_SERIAL`, `tools` ->
`PROCESSORS`, the idle timeout -> `TIMEOUT`, `SKIP_RETURN_CODE 77`. A worker exits with 0 (pass),
1 (fail), 3 (watchdog timeout) or 77 (skipped).
