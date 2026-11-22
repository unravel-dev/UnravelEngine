# Changing a shader

Editing or adding an engine or editor shader: the `.eshader` format, how it compiles, the edit loop with a
running editor, the traps of each shading language, and how to debug a shader.

## Contents

- [The .eshader format](#the-eshader-format)
- [How shaders compile](#how-shaders-compile)
- [Edit loop](#edit-loop)
- [Language traps](#language-traps)
- [Bindings checklist](#bindings-checklist)
- [OpenGL checks](#opengl-checks)
- [Debugging a shader](#debugging-a-shader)
- [Verify](#verify)

## The .eshader format

Every engine and editor shader is a `.eshader`: variants, inputs, outputs and code in one file, one stage per
file. shaderc cannot read it: the asset compiler parses it, generates what shaderc needs and runs shaderc once
per variant. The `.sc` files in `engine/core/graphics/utils/debugdraw/`, `engine/core/graphics/utils/font/` and
`editor/editor/imgui/integration/shaders/` compile by makefile into the `.bin.h` headers that embed them and never
reach the asset compiler.

```text
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

- The stage comes from the file name: `vs_`, `fs_` or `cs_`.
- `[code]` is required, comes last and is plain shader code, without `$input` / `$output` lines.
- `[variants]`: one line per variant, a name, then `NAME` or `NAME=VALUE` defines. It lists `default`, the variant
  used when nothing names one. Without the section the file has only `default`.
- A define is a macro in everything the code includes: `VSM` would rewrite the function of that name in
  `common_shadow.sh`, so pick a name no include uses.
- `[inputs]` / `[outputs]`: one `[flat] type name` per line; an integer varying is `flat`; `#if` lines work as in
  code. No semantics (`: TEXCOORD0`), no `;`, no varying file.
- Vertex inputs are bgfx's names only (`a_position`, `a_normal`, `a_tangent`, `a_bitangent`, `a_color0-3`,
  `a_indices`, `a_weight`, `a_texcoord0-15`, `i_data0-15` as `vec4`); the error lists them.
- Vertex outputs start with `v_`; `= value` is what the output holds where the code does not write it (zero
  otherwise). Fragment inputs repeat the vertex outputs' names and declarations; a fragment shader has no
  `[outputs]`, a compute shader neither section.
- Varying semantics are generated from the name (`v_uv` -> `V_UV`), and two names sharing one in a file are
  refused. D3D12 crashes creating a pipeline whose stages declare a varying differently, so `gpu_program`
  refuses such a pair with an error (`describe_varying_mismatches`).
- C++: `gpu_program(vs, fs)` and `gpu_program(cs)` use `default`; `gpu_program(vs, "skinned", fs, "variant")` and
  `gpu_program(cs, "res4")` name variants. An unknown variant logs one error naming the file's variants and the
  program stays invalid. Every variant compiles and loads with its file.

## How shaders compile

- The editor's asset compiler (`editor/core/asset_compiler/asset_compiler.cpp`, `run_shaderc`) compiles each
  variant per renderer: D3D11 `s_5_0`, D3D12 `s_6_0` (compute at `-O 1`, the rest at `-O 3`), Vulkan `spirv`,
  OpenGL `430`, Metal, WebGPU `wgsl` (tint), WebGL 2 `300_es` on shaderc's asm.js platform. Compute shaders and
  variants needing OpenGL ES 3.1 (`binding = `, `CubeArray`) are cooked without code for WebGL 2.
- Output: one file per renderer beside the source's compiled path (`<name>.eshader.asset.dxbc`, `.dxil`, `.spirv`,
  `.gl`) with a `.manifest`: a fingerprint over the source, its `.meta` and its includes, the format version
  (`get_format_version<gfx::shader>`) and the renderer's compile parameters (`get_shader_compile_parameters`).
  Change the compile parameters string when shaderc's output for a renderer changes, or old cooks stay.
- The `engine_data` target copies all of `engine_data/` (sources and the committed binaries of
  `engine_data/compiled/shaders/`) into `<runtime dir>/data/engine/`; `editor_data` does the same for the editor.
  The editor compiles that deployed copy, never the repository's. The copy never deletes: a renamed or removed
  shader stays in the runtime tree until deleted there.
- The editor compiles the renderers of its platform; deploys cook the rest a profile ships (WGSL and GLSL ES
  for the web) into the profile's cache (`editor/core/deploy/shader_cook.h`).
- The committed binaries are what a fresh checkout starts from and what suites "render feature shaders" and
  "shader constant tables" read. A change that alters compiled output without touching the source (compile
  parameters, a format bump) needs them regenerated: the fingerprint does not change, so nothing recompiles.

## Edit loop

1. Edit the `.eshader` / `.sh` under `engine_data/data/shaders/` (or `editor_data/data/shaders/`).
2. Check offline: `unravel-tests --suite "eshader files"` compiles every shipped shader on every renderer of this
   platform plus WGSL and GLSL ES, and fails on two variants of a file compiling to the same binary everywhere (a
   mistyped define). An offline compile is a check only with the asset compiler's flags; use the suite rather
   than a hand-run shaderc (fxc without `-O` is stricter: a signed `%` fails X3556, an early `return` in an `if`
   trips X4026).
3. Deploy. Editor stopped: `cmake --build build/<Config> --target engine_data` in every tree you launch from.
   Editor running: copy only the changed files into `<runtime dir>/data/engine/data/shaders/...` and set their
   write time to now. The asset watcher recompiles a changed shader; for a changed include the dependency
   watcher touches every dependent (`[DEPWATCH] ... touched N dependent` in the log); `gpu_program::begin()` then
   rebuilds from the new binary. The whole `engine_data` target into a running editor recompiles every asset and
   restores the committed binaries for a while; restart the editor after it.
4. Confirm the binary changed before judging a picture: the compiled file's time and size, and no
   `Failed compilation of` in the editor log (a failed compile keeps the old binary). A sanity variant whose
   effect cannot be missed (a constant colour) proves the program is live.
5. New shader: the editor's import writes its `.meta`; copy it back beside the source in the repository.
6. Refresh the committed binaries: copy the recompiled files and manifests from
   `<runtime dir>/data/engine/compiled/shaders/` into `engine_data/compiled/shaders/` (same relative path).
7. Roll back a broken shader by restoring the source and deploying again; the committed binaries of an
   unchanged source need no compile.

## Language traps

HLSL (fxc, D3D11; D3D12 compiles `s_6_0`):

- `&&`, `||` and `?:` evaluate both sides: an expensive call behind a flag still runs. Use an explicit `if`.
- A `continue` or `break` after an inner loop inside a branched block skips the outer loop's remaining iterations
  under DXBC: set a flag and test it at the outer loop's level.
- A barrier under varying flow (after `if(!active) return;`) is error X4026: keep barriers unconditional and
  guard the work per phase.
- Two loops reusing one counter name at function scope trip the front end: name counters per loop.
- Group-shared atomics are integer only: accumulate radiance as fixed point with a per-sample cap.
- Keywords that compile on GLSL and fail on `s_5_0` / `s_6_0`: `line`, `linear`, `point`, `sample`, `triangle`,
  `centroid`, `precise`, `shared`.
- fxc inlines every call site: one call site per large helper (select parameters, call once). Branches that differ
  only by the texture they read choose it inside the helper's loop, and a fixed run of calls is a loop
  (`fs_gi_scene_debug.eshader` reads every mode's card value in one card loop over the object grid's entries).
- fxc optimizes the whole inlined `main` at once, at any `-O` level, and its time grows faster than the code: a
  shader that picks one of several large paths by a uniform (debug views) takes a variant per path, which also
  compile in parallel (`fs_gi_scene_debug.eshader`: four views in one took fxc about twice as long as each alone
  added up). A debug kernel that would run several production kernels' stages in one splits as they do
  (`cs_gi_probe_trace.eshader#visualize` / `#visualize_far`). Each variant's slowest job prints in suite
  "eshader files".
- A shader printing many lines prints from loops over its rows, the text a `switch`
  (`cs_gi_visualize_rc_stats.eshader`): each `ShaderPrint*` call site is inlined with its atomics. fxc must unroll a
  loop around a `switch` on its counter (a `LOOP` there is error X3531): hint only the outer loop.
- fxc reports one diagnostic and stops: fixing it can reveal the next. Decide pre-existing vs new by compiling
  the unchanged file (a copy of HEAD's file, not a git operation).
- After a per-pixel `discard` or early return, no derivative of a `texelFetch` result: fxc may move the fetch
  and the derivative below the discard and the driver reads dead lanes. Implicit-LOD `sample` stays valid.

GLSL (OpenGL 4.3; shaderc writes it without validating, so the driver rejects it at run time):

- Identifiers that are GLSL keywords: `packed`, `patch`, `sample`, `filter`, `input`, `output`, `common`,
  `partition`, `active`, `precision`, `resource` (glslang accepts `packed`; NVIDIA's driver does not).
- `rsqrt` (write `inversesqrt`), an expression in a layout id (`NUM_THREADS` takes literals), an image at stage 8+.
- Matrix elements by index: GLSL reads `m[column][row]` of the memory HLSL reads `m[row][column]`.

GLSL ES (WebGL 2, validated only by the browser):

- No variant with code needs ES 3.1, and each vertex variant stays within 256 uniform vectors (suite "eshader
  files"); large data goes into shader buffers ([gpu-features.md](gpu-features.md)).
- shaderc keeps shaders using bit encoders (`floatBitsToUint`) at ES 3.00, but every shader compiles for
  WebGL 2 without relying on it: write integer-vector selects per component under
  `#if BGFX_SHADER_LANGUAGE_ESSL`.
- After an ESSL-affecting change, deploy a Web profile, open the page with `?graphics=webgl2` and search the
  browser console for `Failed to compile shader`: a failed program drops its pass silently.
- bgfx rewrites every GLSL and ESSL vertex shader's `main` to normalize clip conventions, and on Emscripten once
  more to write `gl_PointSize`; patch 0008 folds the second into the first. A further rewrite of `main` in bgfx
  shows on WebGL 2 alone ("'main' : macro redefined"). On desktop OpenGL with ARB_clip_control the rewrite leaves
  clip z alone (patch 0009), so a depth bug that shows on WebGL 2 but not on desktop OpenGL may be precision.

SPIR-V and WGSL (Vulkan, OpenGL text and WebGPU all come from SPIR-V or the same HLSL path):

- A `static const` table indexed at run time becomes a per-pixel copy (suite "shader constant tables"): a
  per-pixel index becomes an exact integer formula, a loop with a compile-time count a `switch`, a run-time index
  a uniform array.
- WebGPU shaders compile with `BGFX_SHADER_LANGUAGE_WGSL` through the HLSL path: every language check naming
  SPIR-V names WGSL too (the suite checks).
- shaderc writes WGSL loops without `continuing` blocks (bgfx patch 0007; Safari 18's WebGPU reads zeros from a
  loop entering one through `continue`; the suite checks). A shader-form problem on one browser is fixed in the
  converter, not shader by shader.
- WebGPU core limits hold for every engine shader: 256 invocations per workgroup, 4 storage textures per stage
  (suite "eshader files"; GI's own limits: [gi.md](gi.md)).

## Bindings checklist

- Uniforms and samplers created (`cache_uniforms`) before the program; set by the pass after its view is bound.
- The same stage on every backend; images at stages 0-7, every stage below 8 for a pass that runs on D3D11
  feature level 11_0.
- Image names unlike any sampler uniform; image access in `setImage` matching `_RO` / `_WO` / `_RW`.
- A sensible value when a texture is missing (the default textures, `default_textures.h`).
- A compute pass's storage images and binding count listed in its `render_feature` row
  ([gpu-features.md](gpu-features.md)).

## OpenGL checks

shaderc writes GLSL without validating it, and bgfx's OpenGL backend binds by name: the traps above pass every
other backend and fail on OpenGL at run time, where a program that does not link drops its pass silently. Two
scripts in `scripts/` find them offline (Python 3, standard library; run from the repository root, exit code 1
on a finding):

- `python .agents/skills/unravel-rendering/scripts/scan_gl_image_names.py` reads the shader sources
  (`engine_data/data/shaders/`, `editor_data/data/shaders/`, or folders given) and lists an image named like any
  sampler uniform - bgfx rebinds it to that sampler's stage and its writes land in another texture - and an image
  at stage 8 or above (OpenGL has image units 0-7). Images take `i_` names. Run it after adding or renaming an
  image or a sampler.
- `python .agents/skills/unravel-rendering/scripts/validate_gl.py` runs `glslangValidator` (Vulkan SDK: found
  through `VULKAN_SDK`, `PATH` or `--validator`) over the desktop GLSL of every variant in compiled `.gl` files:
  `engine_data/compiled/shaders/` by default, or a runtime tree's `bin/data/engine/compiled/shaders/` for what an
  editor compiled since; `--filter` narrows to matching paths. It also flags `packed` used as a name, which
  glslang accepts and NVIDIA's driver rejects. Run it after a deploy compiled the change, before committing the
  binaries.

## Debugging a shader

- Measure intermediates on screen (a temporary debug output, deployed as above) instead of re-reading source;
  bisect with stamped variants: visited, entered, a neutral edit, a flag form.
- Compute shaders print values with `shader_print.sh` (`ShaderPrintBegin`, `ShaderPrintFloat`, bound by
  `shader_print::bind`); the text draws over the finished image.
- A debug view or instrument leaves production shaders byte-identical: debug paths live in the debug shader or
  behind a define, and shared helpers keep their production shape (fxc grows every includer when one is reshaped,
  a struct return included). Prove it by diffing every production includer's compiled binary against the
  unchanged source (`shaderc --disasm`, size).
- D3D11 debug-layer messages name the failing binding; run a Debug editor on `-r dx11`.
- A misbehaviour only on one platform's driver (Mesa, a phone, Safari) points at out-of-spec usage other drivers
  tolerate: read the spec for the construct before rewriting the shader.

## Verify

- Suite "eshader files"; compute shaders also "render feature shaders" and "shader constant tables"; material
  graph includes "material graphs"; GI shaders "gi constants".
- The editor log has no `Failed compilation of` after the deploy; the compiled binaries are newer than the edit.
- Both [OpenGL checks](#opengl-checks) report nothing.
- Shared uniforms or resources changed: run `-r opengl` against `-r dx11` and compare the debug views; a web
  shader change also on `?graphics=webgl2` and `?graphics=webgpu`.
- The committed binaries under `engine_data/compiled/shaders/` are refreshed with the change.
