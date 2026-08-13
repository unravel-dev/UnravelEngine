# Lessons

Patterns worth keeping, distilled from corrections during debugging sessions. Newest first.

## Shaders (vis-memo perf hunt, 2026-08-13)

- **HLSL's `?:` is a SELECT, not control flow - never put an expensive call in a ternary
  arm.** Both operands may be evaluated and the loser discarded; a memo whose fill march sat
  on the false arm classified perfectly (debug view solid green) while the pass paid
  march-every-face prices - the entire saving the memo existed for, silently burned. Use an
  explicit `BRANCH if/else` around any arm with texture reads or loops. The discriminating
  signature: an instrument showing the LOGIC correct while the COST matches the eliminated
  path means execution semantics, not algorithm.

## GI / SDF (sealed-box leak hunt, 2026-08-12)

- **An occlusion acceptance over probe-cage segments must be a conviction depth (inside
  geometry), never a proximity threshold.** Legitimate cage segments run parallel to the
  query's own surface at grazing height by construction: world lattices put probes in floor
  planes, and the DDGI self-shadow bias is view-dominant (0.2 normal share), so any positive
  acceptance convicts flat-ground cages wholesale. Measured as black rings/donuts on open
  ground. `GI_WORLD_PROBE_CAGE_VIS_ACCEPT_VOXELS` is negative for this reason.

- **Any consumer making point-verdicts of the global SDF clipmap must choose blended vs
  unblended sampling deliberately.** The cross-fade band exists for tracing continuity (one
  resolvable surface); inside it the coarse level contaminates readings both ways (thin walls
  float above conviction, on-surface queries dip below). Point verdicts (occlusion checks,
  burial tests, hit-health) should read the finest covering level unblended - each level
  alone is conservative.

- **Launch suppression tests must use the RAW field reading, never the expand-fattened one.**
  The expand is a hit-test artifice that fattens surfaces AHEAD of a ray; arming suppression
  on the expanded reading at a tier handover classifies "about to hit" as "launch overhang"
  and the walk then tunnels THROUGH the surface (steps grow with |d| inside solids). This was
  the camera-locked seam ring at exactly GI_MESH_SDF_TRACE_RANGE. The comment specified raw;
  the code drifted. When a comment and code disagree, treat the divergence itself as the bug
  lead.

- **Instrument, then fix. Debug lanes need provenance markers.** Rounds that shipped a
  discriminating debug view first (sun-tier alpha provenance, the blue hit-health lane)
  resolved in one iteration; rounds that shipped a theory-driven fix first were falsified by
  the next screenshot. A camera-locked artifact at a fixed distance is a *ray-t* or
  *handover* signature - check the trace's tier boundaries before suspecting readers.

- **Reader-side leak fixes must never fail toward the environment term.** In every GI read
  chain here, `return false` ultimately reaches the env SH (sky). "Sealed and dark" is a
  measurement and must return true-with-black; "no data" (all probes dead) must fall through
  to the next cascade, not to the sky.

- **A debug instrument must not depend on the machinery it exists to debug.** The sun-tier
  debug write sat behind a runtime uniform flag that provably left the CPU on two separate
  lanes yet never steered the kernel (never explained: current binaries, one camera,
  per-submit capture). Diagnostics that gate GPU behavior should be COMPILED PROGRAM
  VARIANTS selected on the CPU (gi_light_voxels_kernel.sh + two thin .sc entries) - program
  choice cannot be stomped by shared uniform names or stale constant buffers. Keep the dead
  uniform lanes as GPU-debugger telemetry.

- **Never bgfx::update() a buffer created with BGFX_BUFFER_COMPUTE_WRITE - it asserts at
  runtime ("Can't update GPU write buffer from CPU"), and the assert only fires when the
  code path runs, not at compile time.** The GI surface-list buffers already documented
  this exact constraint (global_sdf_clipmap_gpu.cpp: flags follow the composer; CPU
  uploads require COMPUTE_WRITE absent). Before seeding/uploading any buffer, check its
  creation flags. For undefined initial contents in a compute-written buffer, either
  (a) tolerate-and-discard the first frame's result CPU-side (auto_exposure_pass:
  histogram_bins_valid_ discards the first metering; the consumer pass zeroes bins after
  reading), or (b) seed it from a compute dispatch (GI bounce vis-memo pattern). Grep the
  repo for existing handling of a constraint before writing new code against it - this
  one was documented twice in-tree.
