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
