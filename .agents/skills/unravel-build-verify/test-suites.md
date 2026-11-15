# unravel-tests suites

Every engine validation suite runs in one runner, `unravel-tests` (target `tests`, sources in
`tests/tests/`). A first-party library's own suite is in [dependencies.md](dependencies.md);
the browser page's Node tests are in [players-and-ci.md](players-and-ci.md).

## Contents

- Running
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

## Running

```bash
cmake --build build/<Config> --target tests
build/<Config>/bin/unravel-tests                        # every suite
build/<Config>/bin/unravel-tests --suite "fs mirror"    # suites whose name contains the text
```

| Option | Effect |
|--------|--------|
| `--suite`, `-s` | run only suites whose name contains the text |
| `--bench`, `-b` | benchmarks as well as checks (`ecs serialization` timings, `fs watcher` latency, `mesh shapes` compiles every shape) |
| `--bench-only` | benchmarks, no checks |
| `--network` | also the checks that reach the internet (`update check`) |
| `--single-threaded` | engine option: every job, parallel loop and frame runs on the main thread, as in the browser (`engine/core/concurrency/threading_mode.h`) |

- The runner prints each suite's checks and failures, logs to `<bin>/Log.txt` and exits with 1
  when any check failed.
- `mesh import corpus digest` is opt-in: `UNRAVEL_MESH_IMPORT_CORPUS=<list>` names the models.

## Slow suites

These run `shaderc` (or `texturec`) found beside the runner; never beside a build or a busy
editor, and never under a hard timeout.

- `material graphs` - about 16 minutes: hundreds of shaderc runs.
- `eshader files` - compiles every `.eshader` the engine and editor ship, for every renderer.
- `gi constants / reference oracle` - compiles every GI shader.
- `settings profiles` - cooks web shaders and texture variants.
- Under `--single-threaded` these take hours; run the other suites one by one there.

## Rendering and shaders

| Suite | Covers |
|-------|--------|
| `eshader files` | `.eshader` parser and errors, variants, inputs, `gfx::shader::find_variant`, compiled files from packs, every shipped shader on every renderer |
| `render feature shaders` | the feature requirement table against the committed compiled compute shaders |
| `render feature support` | which rendering features a device runs, from its capabilities |
| `feature limits` | project feature limits and render scale (`pipeline::apply_feature_limits`) |
| `render view` | texture descs, `gfx::render_view` texture access, the transient texture pool |
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
| `update check` | `editor/core/update_check/`; `--network` adds the live checks |
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

- One file in `tests/tests/suites/` with `REGISTER_TEST_SUITE("name", run_function)`; the
  function takes `rtti::context&` and returns its failure count. Re-configure: the sources are
  globbed.
- Suites do not initialise threadpp or the asset manager: the runner already owns them.
- A test of a first-party library's behaviour goes in that library's suite (`deps/AGENTS.md`).
