---
name: unravel-build-verify
description: >-
  Builds and verifies UnravelEngine: CMake targets, engine_data/editor_data copy,
  .NET SDK / CoreCLR scripting dependency, sanitizers, and CI workflows. Use after
  code changes, before marking work complete, or when fixing build/CI failures.
disable-model-invocation: true
---

# Build Verify Workflow

## Project structure

```
deps → engine (lib) → editor (exe)
                   → game (exe)
```

Custom data targets: `engine_data`, `editor_data`

Prefer building Debug builds.

## Build commands

```bash
# Configure (example)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Full build
cmake --build build --config Debug

# Data only (shaders/assets copy)
cmake --build build --target engine_data
cmake --build build --target editor_data
```

## Output directories

CMake sets `CMAKE_RUNTIME_OUTPUT_DIRECTORY` to `build/bin` (see root `CMakeLists.txt`).
On multi-config generators (Visual Studio), that becomes `build/bin/<Config>/`.

| Output | CMake default path |
|--------|--------------------|
| Executables | `build/bin/<Config>/` (e.g. `RelWithDebInfo`) |
| Libraries | `build/lib/<Config>/` |
| Engine runtime data | `build/bin/<Config>/data/engine/` (via `engine_data`) |
| Editor runtime data | `build/bin/<Config>/data/editor/` (via `editor_data`) |
| Clrpp managed | `build/bin/<Config>/clrpp/` |

**Legacy / local layout:** some machines also have a populated `build/<Config>/bin/`
(e.g. `build/RelWithDebInfo/bin/`) with a full runtime. Do not assume the just-linked
exe directory is launchable.

### Launching the editor/game

1. Prefer a directory that already has `data/` and `clrpp/Clrpp.Managed.dll`.
2. If CMake wrote a newer exe to `build/bin/<Config>/` but runtime data lives under
   `build/<Config>/bin/`, copy the exe into the populated bin and launch from there
   (or rebuild `engine_data` / `editor_data` into the CMake output dir).
3. Set the process working directory to that bin folder.

## Requirements

- **CMake 3.16+**
- **C++20** compiler
- **.NET 9 SDK** for C# scripting (`dotnet` on `PATH`; CoreCLR via dotnetpp)
- **Git LFS** for large assets

## Build options (CMakeLists.txt)

- `UNRAVEL_UNITY_BUILD` — unity builds in Release
- `BUILD_ENGINE_SHARED` — static by default
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
- [ ] No new warnings in touched files
- [ ] `engine_data` copied if shaders/assets changed
- [ ] `editor_data` copied if editor shaders changed
- [ ] Editor launches without crash
- [ ] Game runner launches (if applicable)

## Shader rebuild

Shader changes require `engine_data` target rebuild — not just C++ link.

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

Clean build of affected targets + smoke test (editor opens, scene loads).
