# Prompt: deep audit of the USC-GI surface cache system

Paste the section below to a fresh agent. It is deliberately self-contained: it names every path,
constraint and deliverable, because the agent will not see the conversation that produced it.

---

You are a principal graphics engineer auditing **USC-GI**, the world-space surface cache global
illumination system in this repository (UnravelEngine, C++20, bgfx, `snake_case`). It is
functionally complete end to end and shipping-adjacent, not a prototype. Your job is to find what
is **wrong**, what is **inconsistent**, and what can realistically be **improved** to reach AAA
quality — not to rewrite it.

## Read these first, in this order

1. `tasks/surface_cache_gi_design.md` — the intended architecture, requirements (R1-R4) and the
   named limitations (L1-L4) of the screen-space system it replaces.
2. `tasks/todo.md` — what is done, what is open, and the known limitations as they stand today.
3. `tasks/lessons.md` — bugs that already happened here, with the rule that prevents each.
4. `AGENTS.md` — project rules you must work within.

`lessons.md` is load-bearing for this task. **Do not report anything in it as a new finding.** Do
check whether any of them have REGRESSED, and say so explicitly if one has. Several describe
failure modes that are invisible in the final image, and those are the class of defect this audit
is most valuable for.

## The system, by file

**Bake (offline, per mesh asset)**
- `engine/engine/rendering/gi/mesh_sdf.h` — sparse brick field format, encoding, indirection.
- `engine/engine/rendering/gi/mesh_sdf_baker.{h,cpp}` — BVH, pseudonormals, winding, open-surface
  fallback, brick classification and fill.
- `engine/engine/rendering/gi/mesh_sdf_source.{h,cpp}` — extraction of a submesh's geometry, incl.
  the LOD-sourced path.
- `engine/engine/assets/impl/asset_compiler.cpp` — the per-submesh bake loop and its threading.
- `engine/engine/assets/asset_storage.h` — importer settings (`sdf_meta`).
- `engine/engine/assets/impl/asset_extensions.h` — mesh format version; bump rules.
- `engine/engine/meta/rendering/gi/mesh_sdf.{hpp,cpp}` — serialization.

**Residency and atlas**
- `engine/engine/rendering/gi/sdf_atlas.{h,cpp}` — brick allocation, indirection rewrite, headers.

**Instances and culling**
- `engine/engine/rendering/gi/surface_cache_service.{h,cpp}` — per-frame instance list, materials,
  GPU buffers, grid upload, orchestration.
- `engine/engine/rendering/gi/sdf_instance_grid.{h,cpp}` — world-space broad phase (CSR + DDA).

**Global cascade**
- `engine/engine/rendering/gi/global_sdf_clipmap.{h,cpp}` — composition, staleness, sampling.
- `engine/engine/rendering/gi/global_sdf_clipmap_gpu.{h,cpp}` — GPU mirror.

**Radiance cache**
- `engine/engine/rendering/gi/radiance_cache.{h,cpp}` — CPU reference for keys, levels, insertion.
- `engine/engine/rendering/gi/radiance_cache_gpu.{h,cpp}` — GPU storage.
- `engine_data/data/shaders/gi/radiance_cache.sh` — the GPU transcription.

**Lighting**
- `engine/engine/rendering/gpu_light_buffer.{h,cpp}`, `engine_data/data/shaders/gi/gpu_lights.sh`,
  `engine_data/data/shaders/gi/gi_lighting.sh`.

**Passes**
- `engine/engine/rendering/pipeline/passes/gi_cache_pass.{h,cpp}` — insert + update dispatches.
- `engine/engine/rendering/pipeline/passes/gi_resolve_pass.{h,cpp}` — gather, temporal, denoise,
  upsample.
- `engine/engine/rendering/pipeline/passes/sdf_debug_pass.{h,cpp}` — debug views (modes 0-11).
- `engine/engine/rendering/pipeline/deferred/pipeline.cpp` — where the passes are sequenced, and
  the `PREV_DEPTH` / Hi-Z lifetime.

**Shaders** (`engine_data/data/shaders/gi/`)
- `sdf_common.sh` — field sampling, instance tracing, cascade sampling, surface resolve.
- `cs_gi_cache_insert.sc`, `cs_gi_cache_update.sc` — cache population and lighting/bounce.
- `fs_gi_resolve.sc`, `fs_gi_temporal.sc`, `fs_gi_denoise.sc`, `fs_gi_upsample.sc` — the
  screen-space chain.
- `fs_sdf_debug.sc` — diagnostics.

**Tests**
- `engine/engine/rendering/gi/tests/gi_bake_tests.cpp` — CPU harness, ~4200 checks. Build with
  `cmake --build <build-dir> --target gi_tests` (it is `EXCLUDE_FROM_ALL`, so a plain build does
  NOT rebuild it) and run `<build-dir>/bin/gi_tests`.

## Method — this part is not optional

This system has repeatedly punished reasoning that was not checked against a measurement. Recent
examples, all of which cost real time: a closest-point query predicted as `O(log T)` that measured
`~O(sqrt(T))`; a culling fix predicted at 100-300x that measured 9.7x; a "stale binary" theory that
was wrong. Conversely, three separate defects were found only by instrumenting.

Therefore:

- **Cite `file:line` for every claim.** A finding without a citation is a hypothesis; label it as
  one.
- **Separate CONFIRMED from SUSPECTED.** Confirmed means you read the code path end to end, or you
  measured it, or a test demonstrates it. Everything else is suspected, and say what would settle
  it.
- **Prefer a measurement to an estimate.** `gi_bake_tests.cpp` is a CPU harness you can extend
  cheaply; `APP_SCOPE_PERF` / `APP_SCOPE_PERF_THREAD` exist throughout. If you assert a cost, say
  how you know.
- **A scope that wraps `poolstl::par` measures the caller BLOCKING, not the work.** High idle there
  means nothing; put a marker inside the parallel body.
- **Beware helpers named like predicates.** `is_valid()`-style functions here have been linear and
  called in inner loops. Read a helper before trusting it in a hot path.
- **Timestamps before theories.** If behaviour contradicts the source, compare source mtime against
  `build/*/bin` and `build/*/lib` before explaining it. `gi_tests` and the editor are built by
  different targets.

## What to produce

Four sections. Rank within each by (impact / effort), highest first. Be concrete and specific to
this codebase — generic graphics advice is not useful here.

### A. Correctness findings
For each: what is wrong, `file:line`, the **observable symptom** (what a user would see, which is
often not "an error"), severity, whether CONFIRMED or SUSPECTED, a fix sketch, and **the test or
debug view that would have caught it**. Pay particular attention to:
- CPU reference vs GPU shader divergence (the CPU is the reference the tests pin).
- Values duplicated across two sites that must agree (keys, strides, thresholds, units).
- Conservativeness of the distance fields (an over-estimate anywhere lets traces pass through
  geometry).
- Anything scaled to "a voxel" or "a cell" that does not name WHICH.
- Lifetime and ordering: resources produced conditionally, consumed unconditionally.
- Energy conservation and double-counting in the bounce/emissive path.
- Skinned meshes: `asset_storage.h` claims per-segment proxies; verify what actually happens.

### B. Inconsistencies and design smells
Duplicated ownership of a parameter, naming that has drifted from behaviour (e.g. `near_field_*`
meaning different things in the design doc and the code), stale comments, settings that exist but
are never read, debug views that no longer match their documentation.

### C. Comparison with production systems
Compare against **UE5 Lumen** (surface cache on mesh cards, world-space radiance cache, screen
traces then mesh SDF then global SDF, hardware RT fallback, DDC-cached bakes), and where relevant
**DDGI/RTXGI** (probe irradiance + Chebyshev visibility), **Godot SDFGI**, and **ReSTIR GI**.

For each meaningful difference produce a row: what they do, what we do, **why it matters here**,
and whether adopting it is worth the effort. Explicitly mark anything that is a large, hard,
easy-to-get-wrong feature (hardware ray tracing, full card rasterisation, spatiotemporal
resampling) as **out of scope for now** with a one-line reason — do not pad the backlog with them.

### D. Improvement backlog
Every item must state: the axis it improves, the expected win **and how you would measure it**,
the effort (hours/days), the risk, and what could regress. Cover all of:

1. **Bake time** — currently ~seconds for a city block after recent work; what is left.
2. **Runtime cost** — the whole surface cache update is ~2.75 ms on a 1591-submesh scene; the GPU
   passes have not been profiled at all. Start there.
3. **Quality at equal cost** — better sampling (blue noise / low-discrepancy vs hashing), better
   importance sampling of the bounce, ray budget allocation, cache level selection.
4. **Temporal stability** — the reprojection and history path in `fs_gi_temporal.sc`, interaction
   with TAA sub-pixel jitter, accumulation caps, disocclusion behaviour.
5. **Temporal artefacts** — fireflies, ghosting, boiling, light leaking, popping at cascade and
   cache-level boundaries, and what happens when lights or geometry change.

Prefer changes that are bounded, testable, and independently verifiable. A good item is one a
competent engineer could land in a day with a test that proves it.

## Constraints

- `snake_case`, ASCII only, comments explain WHY. Minimal diffs; no drive-by refactors.
- Shaders must compile for **`s_5_0`, `spirv` AND `430`** — each backend rejects a different subset.
  Verify with `build/*/bin/shaderc` before claiming a shader change is done.
- `gi_tests` must stay at zero failures. Bump `get_format_version<mesh>()` whenever bake OUTPUT
  changes, not only when the layout does.
- Code under `editor/` must not be required at game runtime.
- Do not commit, amend, or push.

## Definition of done

A written report with sections A-D as above, every claim cited or explicitly labelled a hypothesis,
and a clearly marked **"start here"** shortlist of the three items with the best value-to-effort
ratio. Do not implement anything as part of the audit unless asked; if you write throwaway
instrumentation to answer a question, say so and leave the tree building and green.
