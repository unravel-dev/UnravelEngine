# Current GI system — audit synthesis (feature/gi @ d239573 + uncommitted probe work)

Sources: direct code read + recovered tasks/ docs (gi_audit_report.md, surface_cache_gi_design.md,
lessons.md, todo.md — all preserved in this scratchpad).

## Architecture as implemented ("USC-GI")

Scene representation:
- Per-submesh mesh SDFs baked at import (asset compiler, format v19): sparse 8^3-ish bricks,
  R8 unorm distance in [-4,+4] voxels, indirection texture, atlas (`sdf_atlas`, brick dim 72 —
  hardcoded). Unsigned "shell" fallback for open meshes (thickness floored at 1 voxel).
  Degenerate-triangle rejection, scattered-part refusal (spread > 32x), LOD-2 bake source.
  NO skinned mesh support (bind pose registered at model transform).
- Global SDF clipmap: 4 levels (compile-time), camera-centred, snapped origins, base_extent 16,
  level_scale 2 => ±64 m coverage. 128^3 runtime (GPU compose), toroidal-ish scroll, per-level
  staleness fingerprint, one level recomposed per frame budget. Cross-fade band (blend_voxels 4)
  between levels; conservative min-composite of instance fields.
- `sdf_instance_grid`: uniform world-space grid over all instance bounds, DDA-walked for the
  near-field per-instance tier (exact mesh fields, leak defence), cell-segment-clamped tracing.

Radiance store:
- World-space spatial hash `radiance_cache`: open addressing, probe length 8, capacity 2^19
  (hardcoded), key = PCG-hash(floor((P + face*cell/2)/cell), level, face6). 6-face normal
  quantisation (axis directions at bin centres — deliberate), distance-based level (base_distance,
  doubling, max_level 3, cells 0.25 m..2 m), level cross-fade band on key derivation.
- Payload 5 vec4s: RADIANCE(xyz)+sample_count(w), POSITION(xyz)+frame_touched(w),
  NORMAL(xyz)+level(w), ALBEDO, EMISSIVE. Stores OUTGOING radiance = albedo*E/pi + emissive.
- Addressing goes through `SdfResolveSurfacePoint` (Newton steps on clipmap gradient, 2 iters,
  pinned by parity tests) so writer/reader agree in FIELD space. Tangent-plane bilinear gather
  over 2x2 cells (`GiCacheGatherLevels`) + plane/facing weights + level cross-fade; ~87% bracket
  agreement well inside a level, ~20% inside cascade cross-fade band (OPEN DEFECT).

Passes per frame (deferred pipeline, after lighting):
1. `gi_clipmap_compose_pass` (GPU) — recompose stale clipmap level(s).
2. `gi_cache_pass`:
   - cs_gi_cache_insert: strided G-buffer pixel sampling registers visible surfaces into hash
     (position snapped to cell, real albedo from G-buffer).
   - cs_gi_cache_update: ONE THREAD PER SLOT over all 524k slots (unbudgeted; interval knob
     interleaves). Per resident entry: retirement check vs field, cell-scaled lift, direct
     lighting (gpu_lights buffer, one traced SDF shadow ray per light, near-field faded by entry
     level), N bounce rays (cosine, cache-fed => infinite bounce, self-plane + own-cell rejection,
     entry creation at bounce hits incl. per-instance albedo), EMA blend alpha = max(1/n, min_alpha),
     max_samples cap.
3. `gi_resolve_pass` (half-res typical):
   - EITHER per-pixel gather (fs_gi_resolve: 4 rays, adaptive 2 when settled) OR (new, default)
     screen probe path:
     a. cs_gi_probe_trace: probe per 16px tile, 2 LAYERS (majority/minority surface via median-of-5
        deterministic anchor candidates + buried-candidate refusal via SDF clearance + anchor
        hysteresis via prev-frame continuity + continuity candidate), 8x8=64 octahedral dirs
        = one thread each, FIXED directions, importance-allocated 1-4 sub-cone samples by history
        luminance (round-robin forced refresh every 8 frames), world-space reprojected per-texel
        history from 4 bilinear-validated prev probes, true-mean 1/count blend up to cap (32).
        Atlas alpha = encoded proximity 1/(1+t), sky floor 0.02, 0 = unmeasured.
     b. cs_gi_probe_filter: 3x3 probe-space filter (plane/facing weights + per-texel proximity
        ratio gate), then cosine convolution to 8x8 octahedral IRRADIANCE tile (E/pi at texel
        normal; resolved fraction convolves in alpha).
     c. fs_gi_probe_integrate: per pixel, 4 tiles x 2 layers, weights = bilinear * plane *
        facing^(dist-softened 8..2) * confidence(half-space cosine coverage); octahedral-wrapped
        manual bilinear at pixel normal; coverage ramp 0.10..0.35 blends to 2-ray per-pixel traced
        fallback; 2 short contact rays re-measure sub-tile occlusion (replacement not modulation,
        contact_occlusion dial); output RGB = radiance-mean, A = weight vs env probe.
   - Per-ray pipeline shared in gi_gather_common.sh: measured lift (voxels of answering level via
     SdfSampleClipmapEx, buried-origin escape via surface resolve), ray_start along own dir,
     SdfTraceRay (near-field instance tier + clipmap tier, cone relaxation 0.05, saturation-capped
     acceptance), resolve hit -> same-plane self-read rejection + exact own-key rejection (both
     blend levels), interpolated cache gather, occlude_on_cache_miss (beyond contact gate),
     miss => env SH radiance (resolved) at full range only.
4. fs_gi_temporal (screen-space): reprojection, count-driven mean (cap 48), Catmull-Rom history,
   3x3 clamp (history_clamp_sigma), luminance moments.
5. fs_gi_denoise x4: a-trous, variance/count-normalised luminance stop, plane+normal stops,
   low-count boost.
6. fs_gi_upsample: joint bilateral to full res.
7. Consumer: fs_pbr_lighting blends gather vs env SH probe by alpha.
8. sdf_debug_pass + probe debug modes (atlas/health/history) + ray-stage diagnostics.

GPU light access: `gpu_lights.sh` buffer of all active lights; shadows are TRACED per entry
(no shadow atlas — design doc Phase 0 atlas was never needed since shadow rays are SDF-traced).

## Measured performance (Bistro, fixed viewpoint, 120-frame means; before probe path)
- GI total ~14.4 ms (!!), ~90% of visible GPU cost. Resolve 8.9 ms (57-63%), Cache Update
  ~30-35%, denoise chain ~0.4 ms. Near-field instance tier dominates resolve (8.9 -> 1.0 ms
  without it). Clipmap compose ~0.5 ms GPU worst case. Probe path costs unmeasured in docs.
- Resolve cost nearly flat in scene complexity => fixed per-ray work dominates (trace, not
  addressing: resolve_surface_point cut 4->2 iters saved only 5%).

## Confirmed open defects / gaps (from audit + code)
- Cross-fade band addressing hole: writer/reader agreement ~20% in cascade blend band (band at
  each level boundary) => gather falls back to env probe in a camera-following shell.
- Skinned meshes: bind-pose field at model transform (doc comment lies). A4.
- frame_touched refreshed only by insert pass => offscreen entries are preferred eviction victims
  (A6) — exactly the population the cache exists for. CPU/GPU eviction divergence (A5, == vs >=),
  fresh claim does not init POSITION (frame_touched garbage window).
- Emissive self-illumination via receiver-side gather (A7, suspected).
- No invalidation on light/geometry change (D8): convergence after light toggle ~1-3 s est.
  min_alpha floor is the only reactivity mechanism. R4 unmet.
- White-noise sampling (PCG) for gather fallback/short rays and bounce (D6).
- Hardcoded: atlas_brick_dim, cache capacity 2^19, max_component_spread, level_count 4,
  GI_PROBE_* constants, coverage ramp 0.10/0.35, plane tolerances 0.05 (x3 places), facing
  power 8..2 over /40, proximity floor 0.02, contact gates 2.0*voxel, self-plane 2.0*voxel,
  clearance -0.35/-0.9, history luminance +0.02, importance cap 4, forced refresh &7, etc.
- gi_component::merge_into is a hand-maintained field-by-field lerp — drift-prone boilerplate.
- Settings sprawl: ~50 knobs across cache/resolve/clipmap; many trade against each other
  (documented in prose but enforced nowhere).
- Cells reachable only through cascade have default_albedo (grey) — cascade cannot attribute
  instance. Albedo is per-submesh base colour factor, not texture mean.
- max_distance 200 vs actual cascade coverage ±64 (B1 partially fixed; verify).
- History clamp is 3x3 range clamp; very bright indirect can clip (known limitation).
- No specular/reflection path from GI (SSR separate, no world fallback).
- No translucency/foliage handling, no volumetric.
- Temporal chain is screen-space history ON TOP of probe-space history (double smoothing latency).
- Two gather architectures + per-pixel fallback + short rays + full per-pixel diagnostic path all
  coexist in the resolve pass => the "mess" the user perceives. gi_resolve_pass.h is 713 lines,
  largely prose.

## What is genuinely good (keep the ideas, even in a rewrite)
- World-anchored spatial hash radiance cache with normal-in-key, field-space addressing,
  age eviction, no defrag. Simpler than Lumen cards, content-independent. Infinite bounce via
  cache feedback. Offscreen population via bounce-discovered entries.
- Per-submesh mesh SDF + global cascade two-tier tracing with cell-clamped DDA instance grid.
- Deterministic anchors/median/hysteresis thinking in probe trace; texel-proximity filter gate;
  half-space confidence vs resolved distinction; coverage-ramped traced fallback; short contact
  rays as replacement-not-modulation.
- Cosine-weighted mean => E/pi units convention throughout; albedo gain clamp < 1 closes the
  recursion; occlude-on-cache-miss for sealed-room convergence; retirement via field distance.
- Cross-fades everywhere a level/cascade boundary exists (field + cache level).
- Extensive test harness (gi_bake_tests: 45+ tests, CPU/GPU parity pinning, shown-to-fail
  discipline) + debug views isolating stages + GPU profiler with averaging.
- lessons.md discipline: units-of-the-thing thresholds, one-owner constants, claim-owns-init,
  conservative fields never over-report, measure-before-sizing.

## Platform constraints (hard)
- SM 5.0 floor (D3D11 cs_5_0), no wave ops, no bindless, no HWRT, 16 sampler slots, bgfx.
- Compute buffers readable from fragment shaders. IMAGE3D_RW available. Indirect dispatch OK.
- CPU mesh data retained (bake source). Tests: gi_tests.exe harness exists and is fast.
