---
name: unravel-shader-change
description: >-
  Procedure for editing bgfx shaders in UnravelEngine: .sc source files,
  compilation to .asset blobs, and verification in scene viewport. Use when
  modifying or adding shaders in engine_data/data/shaders/.
---

# Shader Change Workflow

## Prerequisites

- Read `unravel-rendering` skill
- Identify shader stage: vertex, fragment, compute
- Know which render pass uses the shader

## Step 1: Locate shader

| Type | Path |
|------|------|
| Deferred / lighting | `engine_data/data/shaders/deferred/` |
| Shadows | `engine_data/data/shaders/shadows/` |
| Post-process | `engine_data/data/shaders/post/` |
| Particles | `engine_data/data/shaders/particles/` |
| Atmospheric | `engine_data/data/shaders/atmospheric/` |
| Editor-only | `editor_data/data/shaders/` |

## Step 2: Edit .sc source

- Use bgfx shader language conventions
- Include shared headers from shader directory
- `$input` / `$output` declarations for varyings
- Use existing uniforms naming in the pass that binds them

**Do not edit** `engine_data/compiled/shaders/*.asset.*` directly.

## Step 3: Copy into the runtime tree

```bash
# In EACH active configured tree (build/Debug, build/RelWithDebInfo, ...)
cmake --build build/<Config> --target engine_data
```

**`engine_data` only COPIES** `.sc`/`.sh` into `<runtime dir>/data/engine/` and syncs
`bgfx_shader.sh` / `bgfx_compute.sh` headers. It does **not** compile anything -
compilation happens in the editor's asset importer, which invokes `shaderc` at import
time.

## Step 4: Verify compilation

A syntax error does NOT fail the build - it surfaces as a runtime import failure, and
a failed import silently keeps the stale compiled binary (wrong rendering, no error).
Validate before launching:

- Run the in-tree `build/<Config>/bin/shaderc.exe` against the copied shader for
  `s_5_0` and `spirv`, writing output inside the build tree
- For GI shaders: `unravel-tests --suite "gi bake"` compiles every GI shader via
  shaderc as part of the suite
- Then launch the editor and check the console for import errors; confirm updated
  compiled output timestamps

## Step 5: Runtime verification

- Launch editor, open scene view
- Trigger the pass that uses the shader
- Check for bgfx assert/log errors
- Visual comparison before/after

## Step 6: Cross-platform (if changing shared uniforms)

Test on target platforms via CI or local builds:

- Windows (DX11/DX12)
- Linux (GL/SPIRV)
- macOS (Metal via SPIRV)

## Uniform / binding checklist

When adding new uniforms:

- [ ] Declared in shader
- [ ] Set in C++ pass code (`gpu_program`, material, or pipeline)
- [ ] Consistent register slot across platforms
- [ ] Default values sensible when texture null

## Common mistakes

- Editing compiled blobs instead of `.sc`
- Forgetting to rebuild `engine_data` target (the copy step)
- Assuming the C++ build compiles shaders - it does not; only the editor import (or a
  manual shaderc run) does, so errors hide until runtime
- Mismatched varying names between VS and FS
- Modifying `deps/3rdparty/bgfx` instead of engine shaders
- On OpenGL: creating bgfx uniforms after programs - see GPU contracts in
  `unravel-rendering`

## Rollback

If shader breaks rendering: revert `.sc` change, rebuild `engine_data`, confirm recovery.
