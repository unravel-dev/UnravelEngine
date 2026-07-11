---
name: unravel-build-verify
description: >-
  Builds and verifies UnravelEngine: CMake targets, engine_data/editor_data copy,
  Mono dependency, sanitizers, and CI workflows. Use after code changes, before
  marking work complete, or when fixing build/CI failures.
disable-model-invocation: true
---

# Build Verify Workflow

## Project structure

```
deps → engine (lib) → editor (exe)
                   → game (exe)
```

Custom data targets: `engine_data`, `editor_data`

## Build commands

```bash
# Configure (example)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Full build
cmake --build build --config Release

# Data only (shaders/assets copy)
cmake --build build --target engine_data
cmake --build build --target editor_data
```

## Output directories

| Output | Path |
|--------|------|
| Executables | `build/bin/` |
| Libraries | `build/lib/` |
| Engine runtime data | `build/bin/data/engine/` |
| Editor runtime data | `build/bin/data/editor/` |

## Requirements

- **CMake 3.16+**
- **C++20** compiler
- **Mono 6.12+** for scripting (editor and script tests)
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

## Mono / scripting verify

If scripting touched:

- [ ] Project scripts compile from editor Recompile menu
- [ ] Hot-reload succeeds
- [ ] No Mono assembly load errors in console

## Packaging

CPack configured for Windows/Linux/macOS ZIP. Test packaging only when release-related.

## Common build failures

| Error | Check |
|-------|-------|
| Missing meta symbol | Forgot to add `.cpp` to CMake |
| Shader compile fail | `.sc` syntax; rebuild engine_data |
| Mono not found | Mono 6.12+ in PATH |
| Stale shader binary | Clean engine_data compiled output |
| LFS pointer file | `git lfs pull` |

## Done criteria

Clean build of affected targets + smoke test (editor opens, scene loads).
