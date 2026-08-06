# Unravel GI v2 - ground-up redesign plan

Status: APPROVED PLAN - implementation follows this document phase by phase.
Inputs: full audit of the current system (13.7k lines read; recovered design/audit/lessons
docs), plus three literature deep-dives with primary-source math:
- Lumen (SIGGRAPH 2021+2022 decks with speaker notes, Epic docs, UE 5.4.4 cvar defaults)
- Probe systems (DDGI JCGT 2019 + 2021, RTXGI SDK source, Godot SDFGI source, AMD GI-1.0,
  EA SEED surfel GI, Radiance Cascades, VXGI/LPV)
- SM 5.0 tracing/denoising (SDF robustness, octahedral/SH/ZH3 math, SVGF, ReSTIR feasibility,
  pre-exposure, typed-UAV constraints, shipped D3D11 precedents)

Research reports are archived in `tasks/research/` (`research_lumen.md`,
`research_probe_systems.md`, `research_sm5_tracing_denoising.md`,
`current_system_analysis.md`). The previous effort's `gi_audit_report.md`,
`surface_cache_gi_design.md` and `lessons.md` are recoverable from git history
(deleted on `feature/gi`, present at HEAD~ of this branch). Key claims below cite those.

---

## 0. Requirements (testable)

| # | Requirement | Test |
|---|---|---|
| R1 | Camera stability | 360-deg orbit + translation of a static scene: frame delta of the GI buffer below threshold everywhere outside true disocclusion masks. No boil, no tile stepping, no cascade pop. |
| R2 | Offscreen energy | Red wall behind camera tints the visible floor within 3% of the on-screen case. |
| R3 | No leaks | 10 cm wall between lit and dark rooms: dark side < 2% of lit side. |
| R4 | Reactivity | Light toggles at frame N: 90% converged by N+10 near field, N+30 far field; zero residual ghost at N+120. Moving occluder leaves no imprint after `light_voxel_update_interval` frames. |
| R5 | Correct energy | RMSE vs a path-traced reference on golden scenes under agreed thresholds; sealed room converges to black; sky occlusion survives to the lit image. |
| R6 | Performance | Full GI (diffuse) <= 4.5 ms at 1080p on a mid-range 2024 dGPU on Bistro. (Current system: ~14 ms.) |
| R7 | Platform | SM 5.0 / D3D11 floor. No wave ops, no bindless, no HWRT, 16 sampler slots, bgfx. LDS reductions replace wave ops. |
| R8 | Diagnosability | Every stage has a debug view that isolates it; per-pass GPU timing recorded via the existing profiler. |
| R9 | No magic numbers | Every constant is (a) derived and documented at its definition, (b) sourced to a published system, or (c) an exposed setting with units in its name. Cross-pass constants have one owner; a test parses the shader headers and asserts agreement with C++. |

---

## 1. Verdict on the current system

**Keep (proven substrate, outside the rewrite):**
- Mesh SDF bake pipeline (asset compiler, format v19), sparse brick atlas, unsigned-shell
  handling, degenerate/scatter rejection, LOD bake source, the 45-test parity harness.
- Global SDF clipmap: GPU compose, per-level staleness fingerprints, toroidal snap,
  cascade cross-fade, conservative min-composition.
- `sdf_instance_grid` DDA culling with per-cell segment clamping.
- `gpu_lights` buffer + traced SDF shadow rays.
- Debug-view and GPU-profiler discipline; `lessons.md` rules (units-of-the-thing
  thresholds, one-owner constants, claim-owns-init, conservative fields never over-report,
  measure before sizing, regression tests must fail on the old behaviour).

**Remove (the runtime GI stack)** - and where each job goes:

| Removed | Its job | Replacement |
|---|---|---|
| Surface-addressed radiance hash (`radiance_cache.*`, `radiance_cache.sh`) | offscreen energy, multi-bounce, hit radiance | Light voxels (hit radiance, bounce) + world probes (offscreen, distant) - both POSITIONAL |
| `SdfResolveSurfacePoint` addressing + own-key/self-plane rejection stack | writer/reader agreement | Not needed: nothing is addressed by isosurface resolve any more |
| `cs_gi_cache_insert` / `cs_gi_cache_update` (524k-slot sweep) | populate + light the store | Budgeted light-voxel update over a surface-voxel list (indirect dispatch) |
| Per-pixel gather (`fs_gi_resolve`) + probe path's anchor-hysteresis/2-layer machinery | final gather | One screen-probe gather, Lumen recipe (jitter + adaptive placement + plane-weighted integration) |
| Probe-space per-texel history + screen temporal double stack | stability | Single full-res temporal with depth rejection + fast-update mode; probe-space is filtered spatially only |

Rationale for the central removal: the hash's writer/reader agreement is structural
fragility - measured 87% inside a cascade level, ~20% inside cross-fade bands, and it
generated most historical bugs (audit A1, A5, A6, D7, D10, the band hole). Positional
stores (voxel grid, probe lattice) have no agreement problem: the address IS the position.
Every production system surveyed made the same choice for the world store (Lumen voxel
lighting + world probe clipmaps; SDFGI light voxels + probe cascades; DDGI probe grids).
AMD GI-1.0's hash cache survives only by making the query and the insert the same ray
event; our G-buffer-insert/gather-read split cannot.

---

## 2. Architecture

```
                     [ meshes -> mesh SDFs (kept) ]      [ gpu_lights (kept) ]
                                   |                             |
   GI SCENE (per frame, budgeted)  v                             |
   +---------------------------------------------------------------------------+
   | global SDF clipmap (kept)  +  NEW: albedo/emissive voxels + surface-voxel  |
   |   list, emitted by the same compose pass (winning instance attributes)    |
   +---------------------------------------------------------------------------+
          |                              |
          | sphere trace                 | budgeted re-light (indirect dispatch)
          v                              v
   +---------------------+      +----------------------------------------------+
   | WORLD PROBES        |<-----| LIGHT VOXELS (6-axis radiance per surface     |
   | oct radiance+depth, |      | voxel, 4 cascades): direct (traced shadows)   |
   | cascade lattice,    |----->| + albedo * world-probe irradiance + emissive  |
   | windowed mean,      | feed |  (feedback loop => infinite bounce)           |
   | Chebyshev interp    |      +----------------------------------------------+
   +---------------------+                     ^ read at every SDF ray hit
          ^ completes shortened rays           |
          |                                    |
   +---------------------------------------------------------------------------+
   | SCREEN PROBE GATHER: uniform 16px + adaptive probes ON G-buffer pixels,   |
   | jittered; 8x8 octahedral; product importance sampling; trace =            |
   | HZB screen -> mesh SDF (<=2 m) -> global SDF -> world probe completion    |
   | -> sky; probe-space 3x3 filter (hitT-clamped); SH conversion;             |
   | plane-weighted integration with plane-constrained jitter                  |
   +---------------------------------------------------------------------------+
          |
          v
   full-res temporal (depth rejection, fast-update mode) -> bilateral upsample
          + short-range AO composited AFTER temporal (zero-lag contact)
          -> indirect diffuse into fs_pbr_lighting
```

Latency tiers by design (Lumen's principle): contact AO = zero frames; screen probes =
temporal accumulation (<= `temporal_max_frames`); world probes = windowed mean
(`world_probe_history_frames`); light voxels = `light_voxel_update_interval`; sky = slow.
Camera ROTATION touches none of the world-space structures - R1 by construction.

---

## 3. Subsystems

### 3.1 Scene representation (changes to kept code)

1. **Albedo/emissive voxels + surface-voxel list.** The clipmap compose pass already finds,
   per voxel, the instance whose field wins the min(). Extend it to write, for voxels whose
   |distance| <= `SURFACE_VOXEL_BAND` (1.0 voxel - the band inside which a voxel represents
   surface), the winning instance's albedo and emissive (RGBA8 / R11G11B10F, half the SDF
   resolution like SDFGI) and append the voxel to a per-cascade surface-voxel list
   (`solid_cell` pattern, SDFGI). This removes the "cascade cannot attribute a material"
   limitation and the `default_albedo` grey for free.
2. **Trace robustness rework** (`sdf_common.sh` successor, from Lumen p.47-51):
   - Surface expand grows linearly with t from ZERO at origin: `expand(t) =
     min(t * EXPAND_SLOPE, half_voxel_diagonal)`; `EXPAND_SLOPE` derived: reaches full
     expand at `half_voxel_diagonal / EXPAND_SLOPE`; keeps contact shadows without origin
     bias wars. Replaces `step_relaxation` + most of the bias knobs.
   - Step-budget exhaustion is a HIT at current t (over-occlude, never launder exhaustion
     into lit - the audit's A1c lesson, now Lumen-confirmed). Step cap 64 (Lumen).
   - Mesh-SDF (near-field) tier capped at `MESH_SDF_TRACE_RANGE = 2.0 m` (Lumen p.44),
     replacing the 5 m + 24 m fade. Beyond: global SDF only.
   - Keep: saturation-capped acceptance, per-cell DDA segment clamping.
3. **Skinned meshes:** excluded from SDF instances (they still receive GI and appear via
   screen traces); doc comment states this honestly. Per-bone-segment proxies = backlog.

### 3.2 Light voxels (surface lighting at SDF hits)

- **Layout:** per cascade (4), a 3D texture at half SDF resolution (64^3 at runtime
  128^3 SDF), 6 axis directions per voxel (+/-X, +/-Y, +/-Z), R11G11B10F per direction
  (UAV-write, SRV-read - no typed-load dependency), ping-pong pair for feedback.
  Two sides of a wall occupy different axis slots: the positional replacement for
  normal-in-key leak defence. L0 voxel = 0.25 m (matches the old cache's base cell).
- **Update pass** (indirect dispatch over the surface-voxel list, budget
  `LIGHT_VOXEL_UPDATE_FRACTION = 1/4` of the list per frame, SDFGI's
  frames_to_update_light=4 pattern; list offset by frame index so work is even):
  For each of the voxel's occupied axes (axis occupied when the SDF gradient projects
  onto it above 1/sqrt(3) - the 6-way analogue of dominant-axis):
  `L_axis = albedo/pi * (direct_irradiance + bounce_irradiance) + emissive`
  - direct: `gpu_lights` + one traced SDF shadow ray per light (kept machinery), origin
    lifted `1.0 voxel` along the axis (unit: this cascade's voxel; the audit's
    units-of-the-thing rule).
  - bounce: world-probe irradiance evaluated at the voxel + axis direction (8-probe
    Chebyshev-weighted interpolation, section 3.3). This closes the infinite-bounce loop:
    light voxels <- world probes <- light voxels, one bounce per update, exactly Lumen's
    surface-cache radiosity shape with probes instead of card tiles.
  - Blend: `alpha = max(1/n, LIGHT_VOXEL_MIN_ALPHA)` running mean with
    `LIGHT_VOXEL_MIN_ALPHA = 0.25` = 1/update-fraction-denominator (converges within one
    full list rotation; derivation, not tuning).
  - Albedo gain clamped to `MAX_ALBEDO = 0.9` (closes the recursion below 1 - kept rule).
- **Retirement is free:** recomposing a clipmap region rewrites its albedo band and
  surface list; a moved emitter cannot leave an imprint (fixes audit's moved-emitter class).
- **Read at a hit:** trilinear per axis, blend the <=3 axes facing the hit normal weighted
  by `max(0, n dot axis)`, normalized (Lumen p.80 voxel-lighting sampling).

### 3.3 World probes (offscreen + distant + importance fallback)

- **Layout:** probes on the SDF cascade lattice every `PROBE_DIVISOR = 16` SDF voxels
  => 9x9x9 probes per cascade (SDFGI), 4 cascades = 2916 probes. Spacing: 2 m at L0
  (DDGI's recommended 1-2 m), doubling per cascade. Probes are world-anchored; the lattice
  scrolls with the clipmap in WHOLE-CELL steps only (camera rotation: no-op).
- **Per probe:** octahedral radiance 16x16 RGBA16F (rgb radiance, a = hitT for parallax +
  filter clamping), octahedral depth 16x16 RG16F (mean, mean^2; cos^k lobe blend with
  `DEPTH_SHARPNESS = 50` - RTXGI), irradiance 8x8 R11G11B10F (cosine-convolved, for
  cheap evaluation by light voxels and ray-miss completion). 1-texel octahedral gutter,
  mirror-copy border update in the same dispatch (DDGI fig. 9 index map).
- **Trace:** `WORLD_PROBE_RAYS = 64` per updated probe, spherical-Fibonacci directions,
  frame-index rotation; rays start at `PROBE_RAY_START = 0.5 * probe_spacing` (beyond own
  interpolation footprint, Lumen s71) and read light voxels at hits, sky SH at miss
  (prefiltered - mip'd sky, not analytic, against 1-ray fireflies).
- **Integrator: windowed mean, not EMA** (SDFGI's ring-buffer running sum; the single most
  stability-relevant finding: exact convergence in `world_probe_history_frames` frames and
  ZERO steady-state flicker). History `WORLD_PROBE_HISTORY = 16` frames of int16
  fixed-point (x2^10) SH... stored per octahedral texel (not SH): ring buffer + int32
  running sum per texel. Response accelerators (DDGI 2021 4.3): per-texel change > 0.25 of
  max => drop the window to the last 4 entries; > 0.80 => reseed the window with the new
  value. A global light-change event (section 8) triggers the same reseed volume-wide.
- **Update budget:** `WORLD_PROBE_TRACE_BUDGET = 256` probes/frame (2916 probes => full
  refresh ~11 frames; Lumen ships 300/frame at much larger counts). Priority queue:
  (1) probes newly scrolled in, (2) probes marked by screen-probe interpolation this frame
  (LastUsed - LastUpdated, Lumen's priority), (3) round-robin. Scrolled-in probes are
  seeded from the parent cascade (trilinear) replicated into every history slot (SDFGI) -
  no black pop, no convergence cliff.
- **Interpolation (the DDGI weight chain, verbatim - research report section 1.3):**
  trilinear x wrap-shading `((dot(dir,n)+1)/2)^2 + 0.2` x Chebyshev
  `sigma^2/(sigma^2+(d-mu)^2)` cubed, floored at 0.05, x perception crush (w < 0.2 =>
  w *= w^2/0.04); self-shadow bias `(0.2 n + 0.8 w_o) * 0.75 * spacing * 0.3`.
  Cascade blend band tightened by one cell and camera-centred (DDGI 2021 7.4 - the fix
  for scroll pop).
- **Sphere-parallax completion** (Lumen s73): a shortened screen-probe ray that misses
  intersects the world-probe sphere at radius hitT and reads the octahedral texel at the
  intersection direction - positional gap removed, directional error accepted.

### 3.4 Screen probe gather

- **Placement:** uniform probe every `SCREEN_PROBE_SPACING = 16` trace-resolution pixels,
  anchored ON a G-buffer pixel of the tile chosen by per-frame Halton jitter (Lumen:
  probes are pixels - no probe-to-pixel visibility gap, no anchor selection heuristics).
  Adaptive pass: where the 4 surrounding probes fail the pixel plane test
  (`PLANE_TOLERANCE_FACTOR`, section 5), append adaptive probes at half then quarter
  spacing, budget `ADAPTIVE_PROBE_FRACTION = 0.5` of uniform count (Lumen default);
  exhausted budget => flood fill.
- **Directions:** 8x8 octahedral (64), world-space direction indexing shared across
  probes, per-frame rotation jitter.
- **Importance sampling** (Lumen s44-52, phase 5): product of BRDF PDF (accumulated from
  the pixels that will interpolate the probe) x lighting PDF (reprojected previous-frame
  screen probes; world-probe irradiance on reprojection failure). Uniform 64 set; sort by
  PDF; for every 3 rays below cull threshold, subdivide the top ray one octahedral mip
  (3 fund 1). Only the BRDF may cull. Groupshared scan, no wave ops.
- **Trace pipeline per ray:** half-res HZB screen trace first (reuses the existing Hi-Z;
  step-back handoff on occlusion) -> mesh SDF <= 2 m -> global SDF -> shortened stop at
  `2 * local_world_probe_spacing` -> world-probe sphere-parallax completion -> sky.
  Hits read light voxels. Radiance clamped at `MAX_RAY_RADIANCE = 40` pre-exposed
  (Lumen MaxRayIntensity).
- **Probe-space filter:** 3x3 probes, same-texel gather (shared direction indexing),
  weights = depth only; leak guard = reproject neighbour hit position toward own probe
  with neighbour hitT clamped to own hitT (the contact-shadow-preserving fix, Lumen s61),
  reject when angle error > `FILTER_ANGLE_LIMIT = pi/50` (GI-1.0's published constant).
- **Integration:** per probe convert filtered octahedral radiance to SH L2 (research:
  Lumen integrates SH3/L2 analytically per pixel; cheaper and smoother than per-pixel
  octahedral fetches). Per pixel: bilinear 4 probes, plane-distance weights, interpolation
  offset jittered +/- one tile constrained to the pixel plane (widens TAA acceptance -
  Lumen s39); evaluate irradiance with the full-res normal (Ramamoorthi A0=pi, A1=2pi/3,
  A2=pi/4). Output rgb = irradiance/pi, a = resolved fraction (kept convention, consumer
  unchanged).
- **Short-range AO:** full-res bent-normal/short-ray term over `SCREEN_PROBE_SPACING`
  pixels of screen distance, composited AFTER the temporal pass (zero-lag contact,
  Lumen s93-95 + p.167). Replaces the pre-temporal contact rays.

### 3.5 Temporal + upsample (one history, not two)

- Trace/gather at `trace_resolution` (half res default). Full-res temporal on the
  integrated irradiance: world-position reprojection (kept math), 2x2 validated bilinear
  taps with 3x3 rescue (SVGF 4.1), depth-only rejection at
  `TEMPORAL_DEPTH_TOLERANCE = 0.005 * view_distance` (Lumen's DistanceThreshold),
  NO neighbourhood clamp (fights probe jitter - Lumen s98; the clamp was the current
  system's brightness-clipping known-issue), true mean `alpha = 1/n` capped at
  `TEMPORAL_MAX_FRAMES = 10` (Lumen).
- **Fast-update mode** (anti-ghost, Lumen s99): probes record the fraction of rays that
  hit surfaces with nonzero velocity (SDF instances carry a moved-this-frame flag from the
  existing fingerprint machinery); pixels whose interpolated lighting is > 10% from moving
  hits halve `TEMPORAL_MAX_FRAMES` and double the spatial filter radius.
- Joint bilateral upsample to full res (kept, SVGF w_z/w_n weights with sigma_n = 128,
  plane-based depth weight).
- All GI buffers pre-exposed (Frostbite rule); history rescaled by
  exposure_now/exposure_prev on read.

### 3.6 Composition and future specular

- `fs_pbr_lighting` consumption unchanged: mix(env SH, gi.rgb * pi, gi.a).
- Extension phase: roughness-tiered reflections - r > 0.4 resamples screen-probe SH/oct,
  0.3-0.4 shortened traces completing from world probes, < 0.3 SSR with probe fallback
  blend by confidence. Not in the initial rewrite; the data structures already serve it.

---

## 4. Constants policy (R9 made concrete)

Single header pair owns every cross-pass constant: `gi_constants.h` (C++) and
`gi_constants.sh` (shader), generated-comment table with NAME | VALUE | UNIT | DERIVATION
| SOURCE. A `gi_tests` case parses `gi_constants.sh` for `#define`s and asserts equality
with C++ (closes the audit's B4 family permanently). The named constants above each carry
one of three justifications - derived (e.g. LIGHT_VOXEL_MIN_ALPHA = 1/update rotation),
published (e.g. DEPTH_SHARPNESS = 50, RTXGI), or exposed setting with unit suffix.
Anything failing all three is a defect.

## 5. Settings surface (what remains visible)

`gi_component` shrinks to:

| Setting | Default | Notes |
|---|---|---|
| enabled | true | |
| quality | high | low/medium/high/ultra: drives trace_resolution (1/2..1), screen_probe_spacing (32/16), world_probe_history (8..32), light_voxel_update_fraction (1/8..1/2), adaptive budget |
| intensity | 1.0 | artistic multiplier on scene bounce |
| max_distance_m | derived | = outermost cascade half-extent; displayed read-only (audit B1 fixed structurally) |
| sky_leaking_suppression | 0 | optional dial where art wants darker interiors |
| clipmap (kept block) | 128 / 16 m / x2 / 4 | unchanged |
| debug_view | off | enum, section 7 |

Everything else in today's ~50-knob surface becomes a derived constant or dies with its
subsystem. `merge_into` is regenerated for this struct once - and the plan is one struct
of continuous values + one discrete preset, so the hand-rolled lerp chain shrinks to a
dozen lines.

## 6. Memory budget

| Structure | Size |
|---|---|
| Light voxels: 64^3 x 4 cascades x 6 axes x 4 B x2 (ping-pong) | 50 MB |
| Albedo+emissive voxels: 64^3 x 4 x (4+4) B | 17 MB |
| World probes: 2916 x (16^2 x 8 B + 16^2 x 4 B + 8^2 x 4 B) + history 2916 x 16^2 x 16 x 8 B int16 | ~9 MB + 95 MB history -> history at 8x8 texels: ~24 MB. Decision: window the 8x8 IRRADIANCE, not the 16x16 radiance (radiance uses DDGI hysteresis + reseed instead). ~12 MB total |
| Screen probes (1080p half-res trace): 8100 probes x 8x8 x 8 B x (radiance+filtered) + SH buffer | ~9 MB |
| Screen temporal history + moments | ~18 MB |
| Total new | ~105 MB (vs 64 MB hash + ~12 MB probes today; mesh SDF atlas unchanged) |

## 7. Debug views (kept discipline, new stages)

sdf slices/normals/steps (kept); albedo voxels; emissive voxels; light voxels per axis;
surface-voxel list occupancy; world probe spheres (radiance / irradiance / depth mean /
Chebyshev weight to camera); world probe age + budget heatmap; screen probe placement
(uniform vs adaptive vs flood-filled); probe atlas in place; importance allocation;
per-stage ray attribution (screen/meshSDF/GSDF/world-probe/sky); integration weight sum;
temporal rejection mask; fast-update mask; final GI only. Each new pass lands WITH its
views in the same PR (non-negotiable - it is how every audit bug was found).

## 8. Reactivity design (R4)

- Light change: `gpu_light_buffer` gains a per-light fingerprint (position/intensity/
  color/range hash). On change: affected cascades' surface-voxel entries within the
  light's range get their update priority raised to front-of-list; world probes get the
  DDGI event drop (window reseed) scoped to probes whose cells intersect the range.
  Cost: bounded by existing budgets - reaction latency is the budgets, ~4+11 frames
  worst case, within R4.
- Geometry change: existing clipmap fingerprints already recompose the region; the new
  albedo band + surface list rewrite with it; light voxels re-light at update cadence;
  screen fast-update mode covers the visible transient.
- Emissive change: emissive voxels rewrite on recompose; material animation on static
  geometry triggers a cheap per-instance dirty flag -> region recompose.

## 9. Validation harness (R5, before features - Phase 0)

1. Golden scenes as `gi_tests` fixtures + headless image harness: `gi_cornell`,
   `gi_offscreen` (R2), `gi_rotation` (R1 - orbit script, delta assertion),
   `gi_thinwall` (R3), `gi_light_toggle` (R4), `gi_moved_emitter` (no imprint),
   `gi_sealed_room` (converges to black), `gi_sky_occlusion`.
2. CPU reference path tracer over the SAME SDF + light data (not triangles): validates
   transport given the representation; representation error is validated separately by
   the existing SDF accuracy tests. RMSE thresholds recorded per scene, ratcheted.
3. Shader-constant parser test (section 4). CPU/GPU parity tests for: octahedral
   encode/decode + border map, DDGI weight chain, windowed-mean integrator, light-voxel
   axis selection. Each shown to FAIL on a seeded wrong constant (lessons.md rule).
4. Determinism: same scene, camera, frame index => bit-identical light voxels and world
   probes across runs (no screen-space state leaks into world structures).
5. GPU timing recorded per phase on Bistro + a simple scene, 120-frame means, BEFORE and
   AFTER each phase lands (audit D5 protocol) - no unmeasured perf claims.

## 10. Delivery phases (each shippable, each verified)

| # | Deliverable | Verification | Deletes |
|---|---|---|---|
| 0 | Harness: golden scenes, image diff runner, constant-parser test, timing protocol. `gi/v2/` module + `gi_constants.{h,sh}` scaffold | harness red/green demo on current system | - |
| 1 | Scene rep: compose pass emits albedo/emissive voxels + surface-voxel list; trace rework (expand ramp, exhaustion=hit, 2 m mesh-SDF cap) | voxel-albedo debug view matches materials; SDF parity tests still green; trace cost measured | old bias knobs |
| 2 | Light voxels: budgeted update, direct only; debug views | light-voxel view vs direct lighting parity on golden scenes; cost | - |
| 3 | World probes: trace vs light voxels + sky, windowed mean, Chebyshev interp, scroll+seed, budgets; debug views | `gi_offscreen`, probe-view sanity, rotation no-op check, cost | - |
| 4 | Bounce closure: light-voxel update samples world-probe irradiance | `gi_cornell` multibounce RMSE, `gi_sealed_room`, moved-emitter | - |
| 5 | Screen probe gather v1: uniform placement, fixed 64 dirs, trace pipeline, probe filter, SH integration, full-res temporal, upsample; A/B switch vs old system | `gi_rotation`, RMSE vs reference, side-by-side captures, cost | - |
| 6 | Gather v2: adaptive placement, importance sampling, plane-constrained jitter, short-range AO post-temporal, fast-update mode | thin-geometry scenes, `gi_thinwall`, `gi_light_toggle` | per-pixel gather modes |
| 7 | Reactivity: light fingerprints, event reseeds, priority bumps | `gi_light_toggle` frame counts vs R4 | - |
| 8 | Surface: new `gi_component`, inspector, C# parity, presets; REMOVE legacy (hash cache, insert/update passes, resolve-pass old paths, dead shaders); docs | full golden suite, backend matrix (D3D11 first), Bistro budget vs R6 | the mess |
| 9 | (ext) Reflections tiering; skinned proxies; translucency probes | - | - |

Rules: no phase starts until the previous phase's golden checks are green and its GPU
numbers are recorded. The old system stays runnable behind the A/B switch until Phase 8.

## 11. Risks

| Risk | Mitigation |
|---|---|
| Light-voxel resolution (0.25 m L0) too coarse for near-field colour detail | mesh-SDF tier + screen traces carry near-field visibility; per-instance albedo is already flat today; evaluate in Phase 5 A/B before judging |
| 6-axis voxels leak on thin walls at coarse cascades | same limit as today's cell-size defence; content rule (Lumen: walls >= 10 cm at L0), expand ramp never leaks, Chebyshev guards probe side; `gi_thinwall` is the gate |
| Windowed-mean memory for world probes | window the 8x8 irradiance only (decision in section 6) |
| Perf regression vs plan | budgets are all fixed-size dispatches; per-phase timing protocol catches drift early; worst case ships fewer probes/frame |
| Scope | phases 0-5 give a complete, better system; 6-8 are quality/reactivity/cleanup; the plan survives a pause after any phase |

## 12. Sources

Lumen SIGGRAPH 2022 (Wright, Narkowicz, Kelly) + 2021 (Wright) decks w/ notes; Epic Lumen
docs + UE 5.4.4 cvars; Narkowicz "Journey to Lumen"; DDGI JCGT 2019 + "Scaling DDGI" JCGT
2021; RTXGI SDK source; Godot SDFGI (Linietsky) source + docs; AMD GI-1.0 (Boisse 2022);
EA SEED surfel GI (SIGGRAPH 2021); Sannikov Radiance Cascades; Cigolle et al. octahedral
JCGT 2014; Schied SVGF HPG 2017; Ouyang ReSTIR GI 2021; Karis TAA 2014; Salvi variance
clipping 2016; Lagarde/de Rousiers pre-exposure 2014; ZH3 i3D 2024; The Division GDC 2016;
Dagor GDC 2020; CRYENGINE SVOGI docs. Full reports with exact quotes: session scratchpad.
