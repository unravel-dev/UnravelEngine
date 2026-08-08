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
