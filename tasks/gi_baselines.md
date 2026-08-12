# GI GPU baselines (plan section 9.5 protocol)

Record per phase, Bistro, editor profiler (view/encoder timing), 120-frame means, GPU execute.
Compare like against like: same viewpoint, same resolution, same settings unless the phase
changes them by design.

## Pre-Phase-1 baseline (2026-08-06, user-recorded, Bistro)

### Probe gather path (use_probe_gather = true), GI total 8.346 ms
| Pass | GPU ms | % |
|---|---|---|
| Cache Insert | 0.032 | 0.4 |
| Cache Update | 1.859 | 22.3 |
| Probe Trace | 2.555 | 30.6 |
| Probe Filter | 0.032 | 0.4 |
| Probe Integrate | 3.394 | 40.7 |
| Temporal | 0.029 | 0.3 |
| Denoise x4 | 0.408 | 4.9 |
| Upsample | 0.037 | 0.4 |

### Per-pixel gather path (use_probe_gather = false), GI total 8.671 ms
| Pass | GPU ms | % |
|---|---|---|
| Cache Insert | 0.031 | 0.4 |
| Cache Update | 1.911 | 22.0 |
| Resolve | 6.257 | 72.2 |
| Temporal | 0.043 | 0.5 |
| Denoise x4 | 0.392 | 4.5 |
| Upsample | 0.036 | 0.4 |

Reading: the two gathers cost the same total for different reasons. Per-pixel is pure trace
volume (Resolve 6.3 ms). The probe path's tracing is 2.6 ms - the win the architecture
promises - but Probe Integrate at 3.4 ms gives most of it back: that pass carries the
per-pixel traced fallback, the short contact rays and the integration itself. Cache Update
is a constant 1.9 ms unbudgeted 524k-slot sweep in both. The v2 plan attacks exactly these:
one gather path, world-probe completion instead of per-pixel fallback rays, contact AO
post-temporal instead of contact rays in integration, and budgeted voxel/probe updates.

Target after Phase 8 (R6): <= 4.5 ms total on this content.

## Phase 5 first v2 capture (2026-08-06, Bistro, resolution 128) - PROVISIONAL
Captured while the attributes shader was stale (black GI), so radiance content was empty;
re-capture after the fix. Pass structure and trace costs are still meaningful:

| Pass | GPU ms | % |
|---|---|---|
| Light Voxels | 0.403 | 10.9 |
| World Probe Trace | 0.543 | 14.7 |
| World Probe Convolve | 0.308 | 8.4 |
| Probe Trace (v2) | 1.470 | 39.8 |
| Probe Filter (v2) | 0.051 | 1.4 |
| Probe Integrate (v2) | 0.062 | 1.7 |
| Temporal | 0.044 | 1.2 |
| Denoise x4 | 0.736 | 20.0 |
| Upsample | 0.074 | 2.0 |
| **Total** | **3.691** | |

Versus the 8.35 ms v1 baseline: the hash cache passes are gone (v2 skips them), Probe
Integrate fell from 3.39 to 0.06 (no per-pixel fallback rays), and the whole world-side
stack (light voxels + both world-probe passes) costs 1.25 ms. Already under the 4.5 ms
R6 budget with Phase 6 quality features still to come.

## Phase 8 close-out (2026-08-06)

Teardown complete: radiance hash + gi_cache_pass + both v1 gathers deleted, 13 GI shaders
remain, settings collapsed to the 15-field v2 surface, orphaned constants removed and the
temporal defaults wired to gi_constants (max_accum_frames 48 -> GI_TEMPORAL_MAX_FRAMES = 10,
reprojection_tolerance 1.0 -> GI_TEMPORAL_DEPTH_TOLERANCE = 0.005; the old 1.0 disabled
depth rejection entirely). Gates: shaderc-all s_5_0 green, editor + gi_tests build clean,
suite 365 checks / 0 failures.

PENDING (user capture): final Bistro timing at resolution 128 on the torn-down build for
the R6 <= 4.5 ms check - the Phase 5 table above predicts ~3.7 ms but was provisional.

## Final v2 capture (2026-08-08, Bistro, resolution 128, user-recorded)

After the full stabilisation arc (energy-space fix, screen-trace tier, cone jitter +
24-frame window, multi-scale exposure gate, top-2 attribution, dead probes, beam shadows):

| Pass | GPU ms | % |
|---|---|---|
| Light Voxels | 1.022 | 25.9 |
| World Probe Trace | 0.107 | 2.7 |
| World Probe Convolve | 0.196 | 5.0 |
| Probe Trace (v2) | 1.920 | 48.7 |
| Probe Filter (v2) | 0.037 | 0.9 |
| Probe Integrate (v2) | 0.051 | 1.3 |
| Temporal | 0.039 | 1.0 |
| Denoise x4 | 0.524 | 13.2 |
| Upsample | 0.049 | 1.2 |
| **Total** | **3.943** | |

R6 budget (<= 4.5 ms): MET, 0.56 ms of headroom. Versus the 8.35 ms v1 baseline: 2.1x.

Shifts versus the provisional Phase 5 capture (3.69 ms):
- Light Voxels 0.40 -> 1.02: the price of correctness - the multi-scale exposure gate
  (3-sample cavity march per face, shared with the bounce weighting), the beam-clearance
  shadows, and the lift tunnel guard. Trim lever if ever needed: march only faces whose
  single-step reading is ambiguous.
- World Probe Trace 0.54 -> 0.11: the dead-probe gate skips buried probes' rays entirely.
- Probe Trace 1.47 -> 1.92: the trilinear light-voxel reads + the Hi-Z screen tier.
  Remaining levers: screen-commit rate (ray-tier debug view), mip-1 march start, adaptive
  probe placement (backlog).

## 4K capture (2026-08-08, Bistro, resolution 128, user-recorded)

Same build as the final v2 capture above, at 3840x2160 (denoise trimmed to 3 passes):

| Pass | GPU ms | % |
|---|---|---|
| Light Voxels | 0.484 | 6.2 |
| World Probe Trace | 0.080 | 1.0 |
| World Probe Convolve | 0.473 | 6.1 |
| Probe Trace (v2) | 5.128 | 65.9 |
| Probe Filter (v2) | 0.088 | 1.1 |
| Probe Integrate (v2) | 0.094 | 1.2 |
| Temporal | 0.111 | 1.4 |
| Denoise x3 | 1.173 | 15.0 |
| Upsample | 0.152 | 2.0 |
| **Total** | **7.783** | |

Reading: the split behaves exactly as the architecture predicts. The WORLD-scale passes
(light voxels, world probes) are resolution-independent and stay around a milliseconds
fraction; the SCREEN-scale passes grow with pixel count - Probe Trace at ~4x the 1080p-class
cost (probe count is proportional to screen area) now carries two thirds of the frame.
Consequence: at 4K the optimisation levers that matter are all on the gather side -
screen-trace commit rate (ray-tier debug view measures it), a mip-1 Hi-Z march start, and
adaptive probe placement / reduced-rate adaptive probes (backlog). Running the gather at
half resolution already halves this scaling once; quarter-res tracing with the bilateral
upsample is the built-in lever for 4K-class targets.

## Optimization arc close-out (2026-08-09, Bistro, user-recorded, reflections enabled)

Everything in one arc: the trace's saturation step boost, Hi-Z mip-1 gather march,
reflections at the shared trace resolution (joint-bilateral composite upsample), ADAPTIVE
PROBES (even-lattice base + coplanarity/radiance/own-history classification + 8-frame
revalidation + probe-space reconstruction), and traced-probe COMPACTION (classify pass ->
dense list -> indirect trace dispatch; dead/interpolated probes never launch a wavefront).
Quality A/B user-verified identical with adaptive on.

| Pass | 4K GPU ms | FHD GPU ms |
|---|---|---|
| Light Voxels | 0.492 | 0.490 |
| World Probe Trace | 0.079 | 0.079 |
| World Probe Convolve | 0.144 | 0.150 |
| ReflectionsTrace | 1.060 | 0.529 |
| ReflectionsTemporal + Composite | 0.251 | 0.104 |
| Probe Place + Classify + Args | 0.029 | 0.019 |
| Probe Trace (v2) | 2.033 | 0.733 |
| Probe Interp | 0.021 | 0.016 |
| Probe Filter + Integrate | 0.193 | 0.074 |
| Temporal | 0.120 | 0.028 |
| Denoise x3 | 1.206 | 0.312 |
| Upsample | 0.158 | 0.037 |
| **Total** | **5.786** | **2.570** |

Versus the start of the arc (10.103 ms 4K / 4.161 ms FHD, same content, reflections
included): 43% / 38% off the whole GI frame. The dominant pass fell 5.667 -> 2.033 ms at 4K
(adaptive off -> on + compaction, measured stepwise: coplanarity classifier 2.37, radiance +
revalidation quality gates included, compaction -0.34). Adaptive savings are content-scaled:
flat-heavy views interpolate more; lit-structure regions re-trace by design (the radiance
gate) - that is the quality contract, not a regression. Remaining no-quality-loss levers:
light-voxel ambiguous-face trim (~0.15-0.2 ms, flat), denoise variance early-out. Quality
trades if 4K budgets ever demand them: probe_spacing, quarter-res gather, second adaptive
hierarchy level (evens vs stride-4 grandparents).

## Sealed-box leak-defence capture (2026-08-12, Bistro, user-recorded)

After the leak hunt landed: field-visibility cage weighting (variance-gated to the
Chebyshev-ambiguous band, confident blocks zeroed, dead probes skipped), the buried-hit
guard in the gather, and the handover suppression fix (rays that used to suppress-walk at
the mesh<->clipmap handover now hit immediately - a step-count WIN on every near-geometry
ray).

| Pass | 4K GPU ms | FHD GPU ms |
|---|---|---|
| Light Voxels | 0.991 | 0.973 |
| World Probe Trace | 0.066 | 0.065 |
| World Probe Convolve | 0.151 | 0.151 |
| ReflectionsTrace | 1.093 | 0.535 |
| ReflectionsTemporal + Composite | 0.284 | 0.095 |
| Probe Place + Classify + Args | 0.053 | 0.018 |
| Probe Trace (v2) | 2.452 | 0.954 |
| Probe Interp | 0.021 | 0.009 |
| Probe Filter + Integrate | 0.390 | 0.154 |
| Temporal | 0.118 | 0.026 |
| Denoise x3 | 1.176 | 0.342 |
| Upsample | 0.165 | 0.054 |
| **Total** | **6.958** | **3.376** |

Versus the 2026-08-09 close-out (5.786 / 2.570): +1.17 ms 4K, +0.81 ms FHD - the price of
the leak defence, concentrated exactly in the three cage-read consumers and resolution-flat
where world-scale (Light Voxels +0.50, Probe Trace +0.42/+0.22, Integrate +0.20/+0.08;
reflections and denoise unchanged). The ungated march had cost 2.0 ms in Light Voxels ALONE;
the variance gate reclaimed ~70% of that. R6 (<= 4.5 ms FHD-class): still met with 1.1 ms
headroom. Remaining reclaim levers, in value order: (1) per-face visibility-mask cache for
the bounce term, keyed on content epoch + probe-window scroll - geometry-static verdicts
never re-march, expected to return most of the +0.5 ms flat cost; (2) variance gate 0.15 ->
0.2 spacings (cheaper trace completions, slightly wider leak margin on small-mixture
wedges); (3) the pre-existing ambiguous-face trim above.

## Cage-visibility perf reclaim (2026-08-13, tasks/gi_cage_visibility_perf_plan.md)

All three plan items landed:
1. Variance gate -> inspector setting (`probe_visibility_variance_gate`, default = the
   constant 0.15, 0 = march-always) riding u_gi_world_probe_params.w to the light-voxel,
   resolve (trace + integrate) and sdf-debug consumers.
2. Per-face R16U visibility-mask memo for the bounce: mask + 6-bit generation + probe-level
   tag per light-volume texel; generation bumps on clipmap content_epoch change or any
   probe-window cell change; memo hit replaces the gated march with stored verdicts applied
   to EVERY probe; miss marches all 8 corners once and restamps. Prerequisite landed with
   it: b_surface_count merged into the surface list (header cursors), freeing kernel stage 6
   for the memo image.
3. Exact 1-Lipschitz early-outs in GiBounceCavityVisibility (d1 >= 2*t_last - t1 = 7 attr
   voxels => one sample fully visible; d1 <= 0 => buried, fully occluded).

Gates run: gi_tests 636 checks / 0 failures both configs (incl. the new memo-parity test:
2592 corner verdicts, 0 mismatches), 26 shaders x (SM5.0 + GLSL440), spirv on all touched
consumers, editor + engine_data built and deployed in both trees.

### First recapture (2026-08-13, Bistro, user-recorded): Light Voxels REGRESSED

| Pass | 4K GPU ms | FHD GPU ms |
|---|---|---|
| Light Voxels | 1.480 | 1.535 |
| World Probe Trace | 0.065 | 0.066 |
| World Probe Convolve | 0.165 | 0.149 |
| ReflectionsTrace | 1.111 | 0.591 |
| ReflectionsTemporal + Composite | 0.257 | 0.134 |
| Probe Place + Classify + Args | 0.037 | 0.020 |
| Probe Trace (v2) | 2.449 | 0.868 |
| Probe Interp | 0.021 | 0.009 |
| Probe Filter + Integrate | 0.398 | 0.156 |
| Temporal | 0.115 | 0.027 |
| Denoise x3 | 1.185 | 0.291 |
| Upsample | 0.160 | 0.037 |
| **Total** | **7.441** | **3.884** |

Reading: everything EXCEPT Light Voxels held or improved slightly (Probe Trace 0.954 ->
0.868 FHD, Integrate share down - the item-1 gate wiring is live and healthy). Light Voxels
went 0.973 -> 1.535 FHD / 0.991 -> 1.480 4K: +0.5 ms flat, the OPPOSITE of the memo's
promise - and the magnitude matches the miss-every-rotation cost model almost exactly
(primary-level ungated 8-corner fill per relit face ~ the round-7 ungated march minus the
far-blend share and the item-3 cavity savings). A working memo converges to all-hits within
one rotation of a generation bump and cannot cost this much; conclusion: THE MEMO IS NOT
HITTING. Candidate mechanisms: (a) the generation churns every frame (CPU side - content
epoch or window-cell trackers unstable), (b) the R16U imageLoad silently returns zero
(16-bit typed UAV loads are an OPTIONAL cap; a zero load never matches a live generation and
the texel restamps forever), (c) the generation uniform never reaches the kernel (the
unexplained uniform-lane ghost of the sun-tier saga). Instrument round shipped per the
lessons file: generation flip LOG (discriminates a from b/c from the console alone), memo
volume R16U -> R32U (32-bit typed UAV loads are mandatory everywhere - kills b outright,
+12.6 MB), and a THIRD compiled kernel variant "SDF (Vis Memo)" (view 28) painting the live
memo transaction per face - green = hit, red = miss+restamp, blue = generation 0 at the
kernel, dark = no covering cage (categorical display via the sun-tiers nearest-fetch path).

Instrument verdict (2026-08-13, user screenshots + recapture on the instrument build): view
28 SOLID GREEN - the memo hits nearly everywhere (navy slivers = the usual gate-culled
faces) - yet Light Voxels stayed at 1.472 FHD / 1.482 4K (totals 3.809 / 7.335). Hits
classified correctly + march-shaped cost has exactly one mechanical reading, and it was in
the code: the fill sat on the false arm of a TERNARY -
`mask = hit ? stored : GiWorldProbeCageMask(...)` - and HLSL's ?: is a SELECT that may
evaluate BOTH operands, so the 8-corner ungated march executed on EVERY face and was
discarded on hits. (Also explains the R16U -> R32U swap changing nothing: the loads were
fine all along.) FIX: explicit BRANCH if/else around the fill. The instrument stays - it is
the standing memo-health view.

### Final capture (2026-08-13, Bistro, user-recorded, after the BRANCH fix)

| Pass | 4K GPU ms | FHD GPU ms |
|---|---|---|
| Light Voxels | 0.564 | 0.563 |
| World Probe Trace | 0.066 | 0.066 |
| World Probe Convolve | 0.151 | 0.160 |
| ReflectionsTrace | 1.090 | 0.543 |
| ReflectionsTemporal + Composite | 0.274 | 0.108 |
| Probe Place + Classify + Args | 0.041 | 0.019 |
| Probe Trace (v2) | 2.511 | 0.852 |
| Probe Interp | 0.024 | 0.009 |
| Probe Filter + Integrate | 0.407 | 0.155 |
| Temporal | 0.127 | 0.025 |
| Denoise x3 | 1.128 | 0.288 |
| Upsample | 0.161 | 0.037 |
| **Total** | **6.546** | **2.826** |

ACCEPTED. Light Voxels 0.563 / 0.564 - flat, dead centre of the 0.5-0.6 target, reclaiming
the leak defence's entire +0.5 ms in that pass (0.973/0.991 -> 0.56) and even edging under
the pre-defence 2026-08-09 value at 4K. Versus the leak-defence capture: -0.55 FHD / -0.41
4K total. Versus the 2026-08-09 close-out: FHD +0.256 (within the ~0.3 target); 4K +0.76,
of which ~0.7 sits in Probe Trace (+0.48) and Filter+Integrate (+0.21) - the GATHER-side
cage reads this plan explicitly scoped out (completions and the integrate fallback keep the
gated march; unchanged-to-better vs the leak-defence capture). The listed lever for that
share is now a slider: probe_visibility_variance_gate 0.15 -> 0.2 trades trace-completion
cost against leak margin, per scene.

Sealed-box smoke (user-verified): Probe Sky near-only (27) dark green over the sealed room,
and opening the door relights the interior correctly - the memo's content-epoch/window
invalidation works. R6 (<= 4.5 ms FHD-class): met with 1.7 ms headroom.
