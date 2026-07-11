---
name: unravel-shader-change
description: >-
  Procedure for editing bgfx shaders in UnravelEngine: .sc source files,
  compilation to .asset blobs, and verification in scene viewport. Use when
  modifying or adding shaders in engine_data/data/shaders/.
disable-model-invocation: true
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

## Step 3: Rebuild

```bash
# From build directory — rebuild compiles shaders via CMake custom targets
cmake --build <build-dir> --target engine_data
```

Or full project rebuild. CMake syncs `bgfx_shader.sh` / `bgfx_compute.sh` headers.

## Step 4: Verify compilation

- Check build log for shader compile errors
- Confirm updated `.asset.{gl,dxbc,dxil,spirv}` timestamps in `engine_data/compiled/shaders/`
- No errors in bgfx shader compiler output

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
- Forgetting to rebuild `engine_data` target
- Mismatched varying names between VS and FS
- Modifying `deps/3rdparty/bgfx` instead of engine shaders

## Rollback

If shader breaks rendering: revert `.sc` change, rebuild `engine_data`, confirm recovery.
