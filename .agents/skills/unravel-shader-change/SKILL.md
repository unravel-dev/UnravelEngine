---
name: unravel-shader-change
description: >-
  Procedure for editing bgfx shaders in UnravelEngine: .eshader source files,
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

## Step 2: Edit the .eshader source

- Every engine and editor shader is a `.eshader`: variants, inputs, outputs and code in one file. A new shader is one too.
- Use bgfx shader language conventions
- Include shared headers from shader directory
- Use existing uniforms naming in the pass that binds them

**Do not edit** `engine_data/compiled/shaders/*.asset.*` directly.

`.eshader` is the only format the asset compiler reads. The `.sc` files in `engine/core/graphics/utils/debugdraw/`,
`engine/core/graphics/utils/font/` and `editor/editor/imgui/integration/shaders/` compile by makefile into the `.bin.h`
headers they embed, and never reach it.

### The format

shaderc cannot read the file: the asset compiler parses it, generates what shaderc needs and runs it once per variant.

```
[variants]
default
skinned   SKINNED

[inputs]
vec3 a_position
#if SKINNED
vec4 a_weight
vec4 a_indices
#endif

[outputs]
vec2 v_texcoord0
vec3 v_normal = vec3(0.0, 0.0, 1.0)

[code]
#include "common.sh"
...
```

- The stage comes from the file name: `vs_`, `fs_` or `cs_`. One file holds one stage.
- `[code]` is required, comes last and is normal shader code, without `$input` or `$output` lines.
- `[variants]` is one line per variant: a name, then `NAME` or `NAME=VALUE`. A name alone has no defines. It must list
  `default`, the variant a shader is when nothing names one. Without the section the file has only that one.
- A define is a macro in everything the code includes: `VSM` would rewrite the function of that name in
  `common_shadow.sh`, so pick a name nothing there uses.
- `[inputs]` and `[outputs]` are one name per line: `[flat] type name`. An integer varying must be `flat`. `#if` lines work
  as in code. There are no tags (`: TEXCOORD0`), no `;`, and no `.io` file: nothing else feeds the compile.
- **Vertex inputs** come from the mesh, so only bgfx's names are allowed, and the error lists them with what each maps to:
  `a_position`, `a_normal`, `a_tangent`, `a_bitangent`, `a_color0-3`, `a_indices`, `a_weight`, `a_texcoord0-15`,
  `i_data0-15` (`vec4` only).
- **Vertex outputs** start with `v_`. `= value` is what an output holds where the code does not write it, zero otherwise.
- **Fragment inputs** start with `v_` and must declare the same names, the same way, as the vertex shader's outputs. A
  fragment shader has no `[outputs]`, a compute shader has neither section.
- C++: `gpu_program(vs, fs)` and `gpu_program(cs)` use each shader's `default`; `gpu_program(vs, "skinned", fs)` names a
  variant, the `fragment_variant` argument after it names the other, and `gpu_program(cs, "res4")` names a compute variant.
  A name the file does not list logs one error naming what it has, and the program stays invalid.
- Every variant of a file compiles when the file does, loads with it and is a bgfx shader once it has loaded.

Varying tags are generated from the name (`v_uv` becomes `V_UV`), so the two stages always agree, and two names that would
share a tag in one file are refused. bgfx compares only names, and D3D12 crashes creating the pipeline state when the tags
or types of a varying differ, so `gpu_program` refuses with an error a pair that declares a varying differently.

The `eshader files` suite compiles every shipped `.eshader` on all renderers, and fails if two variants of a file are the
same binary everywhere, which is what a mistyped define looks like. Changing what a compiled file holds needs the compiled
files regenerated: the source fingerprint does not change, so nothing recompiles them by itself.

## Step 3: Copy into the runtime tree

```bash
# In EACH active configured tree (build/Debug, build/RelWithDebInfo, ...)
cmake --build build/<Config> --target engine_data
```

**`engine_data` only COPIES** `.eshader`/`.sh` into `<runtime dir>/data/engine/` and syncs
`bgfx_shader.sh` / `bgfx_compute.sh` headers. It does **not** compile anything -
compilation happens in the editor's asset importer, which invokes `shaderc` at import
time.

## Step 4: Verify compilation

A syntax error does NOT fail the build - it surfaces as a runtime import failure, and
a failed import silently keeps the stale compiled binary (wrong rendering, no error).
Validate before launching:

- `unravel-tests --suite "eshader files"` compiles every `.eshader` on all renderers; shaderc cannot read one itself
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

- Editing compiled blobs instead of the `.eshader`
- Forgetting to rebuild `engine_data` target (the copy step)
- Assuming the C++ build compiles shaders - it does not; only the editor import (or a
  manual shaderc run) does, so errors hide until runtime
- Mismatched varying names between VS and FS
- HLSL keywords as identifiers: `line`, `linear`, `point`, `sample`, `triangle`, `centroid`, `precise`, `shared`
  compile on GLSL and fail only `s_5_0` / `s_6_0` - compile-check D3D before launching
- Modifying `deps/3rdparty/bgfx` instead of engine shaders
- On OpenGL: creating bgfx uniforms after programs, naming an image like a sampler, or
  binding an image at stage 8+ (`python tasks/scan_gl_image_names.py` checks the last two) -
  see GPU contracts in `unravel-rendering`

## Rollback

If shader breaks rendering: revert the `.eshader` change, rebuild `engine_data`, confirm recovery.
