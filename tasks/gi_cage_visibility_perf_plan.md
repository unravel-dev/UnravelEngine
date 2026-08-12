# GI cage-visibility performance plan

Reclaim the leak-defence overhead measured 2026-08-12 (tasks/gi_baselines.md, last section:
+0.81 ms FHD / +1.17 ms 4K vs the 2026-08-09 close-out) without reopening any leak. Three
items, ordered by value. Background: memory file `gi-sealed-box-leak.md` (the whole hunt),
`tasks/lessons.md` (the distilled rules - read both before touching anything).

## Hard invariants (every item must preserve these)

- The sealed-box contracts in `gi_world_probes.sh`: all-BLOCKED cage answers TRUE with black
  (never env-SH fallthrough); all-DEAD cage falls through to the coarser level (never black,
  never sky) - dead probes must not count toward `covered_sum`.
- The march semantics are pinned by `test_world_probe_cage_visibility_seals_box`
  (gi_tests.cpp): 702/702 exterior blocked, 135 interior + 18 flat-ground visible,
  under-floor blocked. The CPU transcription (`cage_visibility` /
  `cage_visibility_sample`) must stay in step with the shader BY HAND.
- Constants table parity (gi_constants.h <-> .sh) - the suite fails on any drift.
- Verification protocol per change: gi_tests both configs, editor + engine_data both trees,
  spirv spot-check on touched consumers, then in-editor: Probe Sky near-only view (27) over
  the sealed box stays dark green, and a Bistro profiler recapture appended to
  tasks/gi_baselines.md.

## Item 1 - variance gate as an inspector setting (small, do first)

Make `GI_WORLD_PROBE_CAGE_VIS_VARIANCE_GATE` the DEFAULT of a live setting instead of a
hard-coded compile-time value. 0 = march every probe (max quality, slowest, useful as a
debugging extreme); larger = trust the depth moments more (faster, wider leak margin on
small-mixture wedges). Keep 0.15 as the shipped default; the constant STAYS in both tables
as that default (the `reflection_temporal_frames` precedent: constant = default, setting =
knob).

Wiring (follow the reflection_temporal_frames pattern end to end):
- New float on the GI settings struct the inspector already shows (where
  `enable_screen_trace` / `reflection_temporal_frames` live): suggested name
  `probe_visibility_variance_gate`, meta range 0.0 - 0.5, serialized, tooltip explaining
  the quality/cost trade and that 0 forces the march everywhere.
- Check the cross-cutting trio for new reflected fields (unravel-triage skill list):
  meta, serialization, C# parity if the settings struct is script-visible.
- Transport: `u_gi_world_probe_params.w` is RESERVED and already set by every consumer of
  the world-probe readers - carry the gate there. Consumers to wire (each takes the value
  via run_params from the pipeline's resolved gi settings): gi_light_voxel_pass,
  gi_resolve_pass (trace + integrate programs), sdf_debug_pass. A pass that forgets the
  lane leaves it 0 = march-always = the safe-slow failure direction, never a leak.
- Shader: `gi_world_probes.sh` - both `moments_ambiguous` computations read the uniform
  lane (`#define u_world_probe_cage_vis_gate u_gi_world_probe_params.w`) instead of the
  constant. No new shader constant; parity suite unaffected.

## Item 2 - per-face visibility-mask memo for the bounce term (the big one, ~+0.5 ms back)

The bounce's cage verdicts are geometry-static per face; today every relit face re-marches
its ambiguous probes every rotation. Cache the FIELD verdicts in a persistent volume and
re-march only when something they depend on actually changed.

Design (decided - implement, don't re-derive):
- New R16U volume, exact light-volume addressing (`GiLightVoxelTexel`: attr_res^3, z stacks
  level x face slabs). Low byte = 8-bit visibility mask (bit i = cage corner i
  field-visible from this face's biased query). Bits 8-13 = a wrapping GENERATION tag,
  bits 14-15 = the probe LEVEL the mask was computed for. ~12.6 MB at resolution 128 -
  proportionate to the 50 MB light volume.
- A global generation counter (uniform), bumped by the C++ when EITHER the clipmap
  content_epoch changes OR any probe window scrolls (per-level window base cell changed -
  add a tiny tracker in the pipeline or clipmap_gpu if none exists). Memo texel with a
  stale generation or a mismatched probe level = recompute and restamp.
- Writer/consumer: cs_gi_light_voxels' bounce path ONLY (via the kernel body). On memo hit:
  use the mask bits as the per-probe field verdict - and note the nice property: with
  verdicts this cheap, the bounce can IGNORE the variance gate entirely (use the mask for
  every probe, strictly better leak margin than gated marching). On miss: march all 8
  probes once (the item-1 gate does NOT apply to the memo fill - fill is amortized),
  write mask + generation + level.
- The probe LEVEL stored is the answering cascade level computed exactly as
  GiWorldProbeIrradianceCascade chooses it; a camera moving within a cell can flip the
  answering level without any window scroll, which the level tag catches (mismatch =
  recompute), so camera motion needs no invalidation of its own.
- The debug variant (cs_gi_light_voxels_debug) takes the tier path before the bounce and
  never touches the memo - unchanged.
- The gather completions and integrate fallback do NOT use the memo (their query points are
  not lattice-quantized); they keep the item-1 gated march. This item reclaims the light
  voxel share specifically.
- Stage budget warning: cs_gi_light_voxels occupies ALL 16 bgfx stages (memory:
  gi-sun-shadowmap-tier). The memo needs an IMAGE stage (read+write). Candidates: merge
  b_surface_count (stage 10) into the surface-list buffer (the memory note names exactly
  this candidate), or time-share a stage. Resolve this FIRST - it is the one open risk of
  the item. Remember gfx::set_image_3d for any 3D image binding (GL layered-binding trap).
- Tests: extend gi_tests with a CPU transcription check "memo-fill verdict == fresh march"
  over the sealed-box fixture (same probe/query sets); invalidation behavior (epoch bump,
  window scroll, level flip) is editor-smoke via the Probe Sky view + the lit box after
  moving geometry (a wall dragged open must relight within a rotation, not stay sealed).

## Item 3 - cavity-visibility early-outs (exact, zero quality trade)

GiBounceCavityVisibility (light-voxel kernel) always takes 3 field samples (1/2/4 voxels).
Replace the heuristic "ambiguous face" idea from the close-out notes with EXACT sphere
bounds from the first sample d1 at t1 = 1 attr voxel (1-Lipschitz field):
- d1 >= 7 voxels  => every farther sample's contribution is provably zero => visibility 1,
  one sample total (7 = 2*t3 - t1 with t3 = 4).
- d1 <= 0 (buried) => occlusion saturates => early-out fully occluded.
- Otherwise take the remaining samples as today.
Open Bistro exteriors resolve in one sample; interiors/contact zones pay today's cost.
Expected ~0.1-0.2 ms flat. No constants change; add a comment deriving the 7.

## Acceptance

- gi_tests green both configs (including the new memo-parity check), 25+ shaders compile,
  spirv on touched consumers.
- Sealed box: Probe Sky near-only (27) dark green interior; lit box converges dark; Sun
  Tiers (26) shows no bright green / white / red inside.
- Bistro recapture appended to tasks/gi_baselines.md; targets: Light Voxels back to
  ~0.5-0.6 ms flat, GI totals within ~0.3 ms of the 2026-08-09 close-out (2.570 FHD /
  5.786 4K) with the gate at its 0.15 default.
- Geometry-edit smoke for the memo: move/delete a wall of the test box - the interior must
  relight/darken within a rotation (invalidation works), no stale seams.
