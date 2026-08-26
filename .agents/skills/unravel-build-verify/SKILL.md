---
name: unravel-build-verify
description: >-
  Builds and verifies UnravelEngine: CMake targets, the unravel-tests runner,
  engine_data/editor_data copy, shaderc validation, .NET SDK / CoreCLR scripting
  dependency, sanitizers, and CI workflows. Use after code changes, before marking
  work complete, or when fixing build/CI failures.
---

# Build Verify Workflow

## Project structure

```
deps -> engine (lib) -> editor (exe)
                   -> game (exe)
                   -> tests (exe, output name unravel-tests)
```

Custom data targets: `engine_data`, `editor_data`

## Rule 1: build in the already-configured tree

**Locate existing configured trees first - never configure a scratch dir:**

```bash
ls build/*/CMakeCache.txt
```

Typical local layout: single-config Ninja trees at `build/Debug` and
`build/RelWithDebInfo` (there is **no** CMakeCache at `build/` itself). The user
iterates from those trees; artifacts written elsewhere prove nothing about what the
running editor loads, and a second configure wastes time. Build the tree(s) the user
actually runs - often both Debug and RelWithDebInfo.

```bash
# Build a target in a configured tree
cmake --build build/Debug --target editor

# Data copy only (see shader note below)
cmake --build build/Debug --target engine_data
cmake --build build/Debug --target editor_data
```

Only if no configured tree exists, configure one: `cmake -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`.

This extends to throwaway diagnostics: do not compile scratch repros with ad-hoc
g++/clang++ outside the tree. Put the probe in a test suite or a debug log in the real
code, built through the configured pipeline.

## Output directories

Two layouts exist - **locate, don't assume**:

| Generator | Runtime dir |
|-----------|-------------|
| Single-config Ninja tree (typical local) | `build/<Config>/bin/` (e.g. `build/Debug/bin/`) |
| Multi-config (Visual Studio) | `build/bin/<Config>/` |

Within the runtime dir: executables, `data/engine/` (via `engine_data`),
`data/editor/` (via `editor_data`), `clrpp/` managed payload, `lib/` alongside.
Do not assume the just-linked exe directory is launchable - launch from a bin that
has `data/` and `clrpp/`.

## Tests (unravel-tests)

ALL validation suites live in the `tests` target (exe name `unravel-tests`). It boots
the real engine headlessly (threading + assets, no renderer/audio), so asset handles
and prefab sources actually resolve. Exit code = number of failing checks.

```bash
cmake --build build/Debug --target tests
build/Debug/bin/unravel-tests.exe                              # all suites
build/Debug/bin/unravel-tests.exe --suite serialization        # substring match
```

| Suite name | Covers | Run when touching |
|------------|--------|-------------------|
| `ecs serialization / prefabs / cloning` | Save/load, prefab statements, cloning (`--bench` adds timings) | Serialization, meta, prefabs, components |
| `physics contacts / destroy funnel` | Contact events, entity-destroy funnel | Physics, entity lifecycle |
| `ik solvers` | IK correctness | Animation/IK |
| `animation` | Blend spaces, root motion, replay | Animation |
| `gi constants / reference oracle` | GI constants parity (C++ vs shader mirror), reference tracer | GI code or GI shaders |
| `gi bake / sdf / clipmap` | Bake pipeline; also **compiles every GI shader via shaderc** | GI shaders / SDF |

Suites self-register (`REGISTER_TEST_SUITE` in `tests/tests/suites/`); adding one is
adding a file.

### Launching the editor/game

1. Prefer a directory that already has `data/` and `clrpp/Clrpp.Managed.dll`.
2. If CMake wrote a newer exe to a different dir than the populated runtime bin,
   copy the exe into the populated bin and launch from there (or rebuild
   `engine_data` / `editor_data` into that output dir).
3. Set the process working directory to that bin folder.

## Requirements

- **CMake 3.16+**
- **C++20** compiler
- **.NET 9 SDK** for C# scripting (`dotnet` on `PATH`; CoreCLR via dotnetpp)
- **Git LFS** for large assets

## Build options (CMakeLists.txt)

- `UNRAVEL_UNITY_BUILD` - unity builds in Release
- `BUILD_ENGINE_SHARED` - static by default
- Sanitizers via `ECMEnableSanitizers`
- `compile_commands.json` exported for clang tooling

## CI workflows

`.github/workflows/`:

- `windows.yml`
- `linux.yml`
- `macos.yml`

When fixing CI: reproduce platform locally or read workflow logs via `gh` CLI.

## Verification checklist

After code changes:

- [ ] `engine` target compiles
- [ ] `editor` target compiles (if editor code touched)
- [ ] `game` target compiles (if runtime-only code)
- [ ] `tests` target compiles and the relevant `unravel-tests` suites pass
- [ ] No new warnings in touched files
- [ ] `engine_data` copied if shaders/assets changed (+ shaderc validation, see below)
- [ ] `editor_data` copied if editor shaders changed
- [ ] Editor launches without crash
- [ ] Game runner launches (if applicable)

## Shader rebuild

The `engine_data` target only **COPIES** `.sc`/`.sh` sources into
`<runtime dir>/data/engine/` (and syncs `bgfx_shader.sh` / `bgfx_compute.sh`) - it does
**not** compile shaders. The editor's asset importer invokes `shaderc` at import time,
so a shader syntax error shows up only as a runtime import failure, and a failed
compile silently keeps the stale binary (presents as wrong rendering, not an error).

After a shader change:

1. `cmake --build build/<Config> --target engine_data` in each active tree.
2. Validate offline: run the in-tree `build/<Config>/bin/shaderc.exe` against the
   copied tree for `s_5_0` and `spirv`, writing output inside the build tree - or run
   the `gi bake` test suite, whose shader-compile test does this for all GI shaders.
3. Launch the editor and confirm the import produced no errors.

## Scripting / .NET verify

If scripting touched:

- [ ] `dotnet --info` reports an SDK (not only a runtime)
- [ ] Project scripts compile from editor Recompile menu
- [ ] Hot-reload succeeds
- [ ] No managed assembly load errors in console
- [ ] `clrpp/` bridge payload present next to the exe when using CoreCLR

## Packaging

CPack configured for Windows/Linux/macOS ZIP. Test packaging only when release-related.

## Common build failures

| Error | Check |
|-------|-------|
| Missing meta symbol | Forgot to add `.cpp` to CMake |
| Shader compile fail | `.sc` syntax; rebuild engine_data |
| `dotnet` not found | Install .NET 9 SDK; ensure it is on `PATH` |
| Script compile fails | `dotnet --info` must show an SDK |
| Stale shader binary | Clean engine_data compiled output |
| LFS pointer file | `git lfs pull` |

## Done criteria

Clean build of affected targets in the configured tree(s) + relevant `unravel-tests`
suites green + smoke test (editor opens, scene loads).
