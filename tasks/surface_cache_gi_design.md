# Unravel Surface Cache GI (USC-GI) — Analysis and Design

Status: design proposal, not implemented.
Author: agent design pass.
Target: real-time diffuse GI with world-space stability, offscreen contribution,
no baking, and Lumen-class (or better) dynamic-scene response.

---

## 0. Scope

**In scope:** indirect diffuse (multi-bounce), sky occlusion, emissive bounce,
offscreen contribution, dynamic geometry and dynamic lights.

**Out of scope for v1 (explicit extension points noted):** indirect specular
(SSR keeps its current role, but gains a world-space fallback in Phase 6),
volumetric / participating media, translucency GI.

**Hard requirements from the request, restated as testable criteria:**

| # | Requirement | Testable criterion |
|---|---|---|
| R1 | World stable | Rotating the camera 360 deg with a static scene produces zero change in the lit result outside the temporal ramp-in of *newly disoccluded* pixels. No screen-edge boil. |
| R2 | Offscreen geometry | A red wall behind the camera bounces red onto the visible floor, with the same intensity whether it is on screen or not. |
| R3 | Minimal light leaking | A 10 cm wall between a lit and an unlit room shows < 2% of the lit room's irradiance on the dark side. |
| R4 | Dynamic scene changes | Moving an object or a light converges the affected GI within a bounded, artist-controllable latency (target: 90% within 8 frames near field, 30 frames far field), with no persistent ghost. |
| R5 | No bugs | Enforced by a validation harness (Section 10), not by assertion. |

---

## 1. Analysis of the current rendering path

### 1.1 Pipeline order

From [pipeline.cpp:651-772](engine/engine/rendering/pipeline/deferred/pipeline.cpp#L651-L772):

```
build_reflections -> build_shadows
  -> G-Buffer -> ASSAO -> reflection probes (RBUFFER)
  -> Hi-Z -> SSR (reads LAST frame's resolved output)
  -> direct lighting (LBUFFER)
  -> SSIL   (reads LBUFFER + Hi-Z + last frame's SSIL + last frame's IRRADIANCE_SH)
  -> indirect lighting (blends SH probe vs SSIL, adds reflections + emissive)
  -> atmospherics -> particles -> TAA -> exposure -> bloom -> tonemap -> FXAA
```

G-Buffer is 4 colour targets + depth
([pipeline.cpp:144-189](engine/engine/rendering/pipeline/deferred/pipeline.cpp#L144-L189)):
`tex0` base colour + AO, `tex1` normal/metal/roughness, `tex2` emissive,
`tex3` subsurface, `tex4` depth.

### 1.2 What SSIL actually does

[ssil_pass.cpp](engine/engine/rendering/pipeline/passes/ssil_pass.cpp) runs
trace -> temporal resolve -> a-trous spatial denoise (mixed full/half res) ->
bilateral upsample. The trace
([fs_ssil_trace.sc](engine_data/data/shaders/ssil/fs_ssil_trace.sc)) shoots
`max_rays` (default 4) cosine-weighted hemisphere rays, marches them through the
Hi-Z pyramid starting at mip 0, and on a validated hit reads direct lighting +
emissive + an SH ambient term + the previous frame's SSIL at the hit pixel.

The engineering quality here is high — spatial strata
([fs_ssil_trace.sc:62-63](engine_data/data/shaders/ssil/fs_ssil_trace.sc#L62-L63)),
IGN blue-noise scrambling, source-level firefly capping, SVGF moments, per-axis
resolution scaling, metalness stripping of the bounce source. The comments
document real bugs that were found and fixed. **None of the limitations below
are implementation defects; they are all consequences of the screen-space
formulation.**

### 1.3 Structural limitations

**L1 — Offscreen geometry contributes nothing (blocks R2).**
Every ray that leaves the frustum or walks off the depth pyramid falls back to
`eval_radiance_sh(s_irradiance, dir)`
([fs_ssil_trace.sc:320-326](engine_data/data/shaders/ssil/fs_ssil_trace.sc#L320-L326)),
which is a *single global environment SH* computed from the skylight
([pipeline.cpp:1108+](engine/engine/rendering/pipeline/deferred/pipeline.cpp#L1108)).
There is exactly one such probe for the whole world. A red wall behind the
camera is indistinguishable from open sky.

**L2 — Not world stable (blocks R1).**
The history is a screen-space ping-pong (`SSIL_HISTORY_A/B_COLOR`) reprojected
with `u_prev_view_proj` and `PREV_DEPTH`. Anything that reprojects outside the
previous frame, or fails the depth/normal disocclusion gates
([ssil_pass.h:44-65](engine/engine/rendering/pipeline/passes/ssil_pass.h#L44-L65)),
resets to a 4-ray estimate. Camera rotation therefore produces a permanently
un-converged band at the leading screen edge. Even *within* the frame, the
answer changes when geometry enters or leaves the view: the same floor point is
lit differently depending on whether the bouncing wall is on screen.

**L3 — Occlusion is a thickness heuristic (blocks R3).**
The depth buffer stores one surface per pixel with no thickness. `u_thickness`
([ssil_pass.h:71-77](engine/engine/rendering/pipeline/passes/ssil_pass.h#L71-L77))
widens the hit-acceptance band with distance, and the header comment states the
trade explicitly: too small over-rejects far hits and *leaks the environment
fallback through occluders*; too large over-occludes. There is no setting that
is correct for both a 10 cm wall and a 10 m hill.

**L4 — Multi-bounce is screen-space and lossy.**
`s_prev_ssil` feedback
([fs_ssil_trace.sc:139-149](engine_data/data/shaders/ssil/fs_ssil_trace.sc#L139-L149))
is sampled at the *hit pixel*, so bounce 2+ inherits L1 and L2, and is
additionally weighted by `prev.a` (the temporal blend weight), which
under-counts energy on recently disoccluded surfaces.

**L5 — The bounce source is direct-only lighting.**
SSIL runs before the indirect pass, so `s_color` (LBUFFER) contains direct light
only. The shader compensates by re-adding a Lambertian SH ambient at the hit
([fs_ssil_trace.sc:132-137](engine_data/data/shaders/ssil/fs_ssil_trace.sc#L132-L137)).
That is a reasonable patch, but energy is approximate and is the reason the
default `brightness` is 0.5 rather than the physical 1.0
([ssil_pass.h:78-82](engine/engine/rendering/pipeline/passes/ssil_pass.h#L78-L82)).

**L6 — Firefly cap biases bright-source scenes.**
`SSIL_FIREFLY_RATIO 8.0` against the *global* SH luminance
([fs_ssil_trace.sc:219-229](engine_data/data/shaders/ssil/fs_ssil_trace.sc#L219-L229))
clips legitimate energy when the true bounce source is much brighter than the
scene average (small bright window, emissive sign in a dark alley).

**L7 — Forced mip-0 Hi-Z start.**
`BASE_LOD 0` ([fs_ssil_trace.sc:79](engine_data/data/shaders/ssil/fs_ssil_trace.sc#L79))
costs ~30% of the trace because coarser start mips snap hits to tiles and
produce blocky low-frequency blotches the denoiser cannot remove. This is
paid because the *only* signal available is the depth pyramid.

**L8 — Global illumination is one SH probe.**
`IRRADIANCE_SH` is a single 9-coefficient RGB SH for the whole scene. The
indirect pass blends it against SSIL
([fs_pbr_lighting.sh:522-532](engine_data/data/shaders/fs_pbr_lighting.sh#L522-L532)).
Interior spaces get outdoor ambient; there is no spatial variation of the
fallback.

### 1.4 Platform constraints that shape the design

These are hard and non-negotiable:

- **No hardware ray tracing.** The vendored bgfx exposes no acceleration
  structure or ray query API. All tracing must be software compute.
- **Shader Model 5.0 floor.** D3D11 compiles `cs_5_0`, D3D12 `cs_6_0`, GL 4.30,
  SPIR-V, Metal ([asset_compiler.cpp:679-731](engine/engine/assets/impl/asset_compiler.cpp#L679-L731)).
  No wave intrinsics, no 64-bit atomics, no bindless in the portable path.
- **16 sampler slots per draw** (`BGFX_CONFIG_MAX_TEXTURE_SAMPLERS`), 256 views.
- **Available:** `BUFFER_RO/RW/WO` structured buffers with `atomicAdd` /
  `atomicMin` / `atomicCompSwap`, `IMAGE3D_RW`, 3D textures, indirect dispatch,
  and — importantly — compute buffers are usable from *fragment* shaders too
  ([asset_compiler.cpp:660-677](engine/engine/assets/impl/asset_compiler.cpp#L660-L677)).
- **CPU mesh data is retained** (`system_vb_` / `system_ib_` in
  [mesh.h:1275-1285](engine/engine/rendering/mesh.h#L1275-L1285)) — SDF baking
  from the asset compiler is possible.
- **No GPU light buffer and no shadow atlas.** Lights are drawn as one
  fullscreen pass each, with that light's shadow map bound
  ([pipeline.cpp:1339+](engine/engine/rendering/pipeline/deferred/pipeline.cpp#L1339)).
  A compute shader cannot currently evaluate direct lighting at an arbitrary
  world point. **This is the single biggest prerequisite.**

---

## 2. Architecture overview

USC-GI is a three-structure system. The naming maps to the two references:
what Lumen calls *mesh cards + surface cache + screen probes*, and what Unity's
Surface Cache calls *patches + ring buffer*, is here *voxel surface attributes +
spatial hash radiance cache + screen probes*.

```
                       [ scene: meshes, lights, sky ]
                                   |
   offline / async   +-------------+-------------+
   (asset compiler)  |                           |
                     v                           v
            MESH SDF BRICKS              MESH MATERIAL BRICKS
            (distance, R8)               (albedo + emissive, RGBA8)
                     |                           |
   per frame, CPU    +------------+--------------+
                                  v
                        GLOBAL SDF CLIPMAP           <- dynamic instance
                        (4 levels, camera centred)      composite
                                  |
                                  |  software sphere tracing
                                  v
   +--------------------------------------------------------------+
   |            SPATIAL HASH RADIANCE CACHE (world space)          |
   |   key = hash(quantised world pos, level, normal octant)       |
   |   value = L1 SH radiance + sample count + last-touched frame   |
   +--------------------------------------------------------------+
        ^                                          |
        | cache update pass                        | read on ray hit
        | (rays -> hit -> direct light -> EMA)     v
        +------------------------------------ SCREEN PROBES
                                              (octahedral, adaptive)
                                                     |
                                                     v
                                        per-pixel SH integration
                                                     |
                                                     v
                                        indirect lighting pass
```

**Why the radiance lives in a world-space hash and not in mesh cards.**
Lumen's cards require an offline per-mesh card-generation step, and they are the
source of most of Lumen's content-dependent artifacts (meshes that do not
decompose into axis-aligned rectangles get poor coverage). A world-anchored
spatial hash — the structure NVIDIA ships as SHARC and the one Unity's Surface
Cache describes as "patches mapped by surface position and normal direction" —
needs no per-asset preprocessing, works identically for skinned, procedural,
terrain, and instanced geometry, and is *world stable by construction*: the key
is a function of world position only.

Unity's document notes their ring buffer "requires maintenance through
defragmentation iterations per frame". The design below uses open-addressed
hashing with age-based eviction and therefore has no defragmentation step at
all (Section 5.3).

---

## 3. Phase 0 prerequisite — GPU light and shadow access

Nothing else can be built until a compute shader can answer
"how much direct light reaches world point P with normal N?".

### 3.1 Light buffer

```cpp
// engine/engine/rendering/gpu_light_buffer.h
struct gpu_light            // 64 bytes, std430-compatible
{
    math::vec4 position_range;      // xyz world pos, w = 1/range   (dir: xyz = -direction, w = 0)
    math::vec4 direction_angle;     // xyz direction, w = cos(outer)
    math::vec4 color_intensity;     // rgb linear colour, a = intensity
    math::vec4 shadow_params;       // x = atlas slot (-1 = none), y = type, z = spot inner, w = flags
};
```

Populated each frame from the same `light_component` iteration the direct
lighting pass already performs, uploaded to a `dynamic_index_buffer` created
with `BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_ALLOW_RESIZE`.

### 3.2 Shadow atlas

`shadowmap_generator` currently owns per-light `ShadowMapRenderTargets`
([shadow.h:108](engine/engine/rendering/shadow.h#L108)). Add an atlas mode: one
`D32F` texture (default 4096x4096, artist-settable) partitioned by a simple
shelf allocator, plus a parallel `gpu_shadow_slot` buffer holding the
world-to-shadow matrix, atlas rect, and depth bias per slot. CSM cascades occupy
4 consecutive slots.

The existing per-light fullscreen passes keep working unchanged — they just
sample a sub-rect. The win is that `eval_direct_lighting(P, N)` becomes callable
from any compute shader, which the cache update pass (Section 6) requires.

**Risk:** this touches the shadow path used by every existing scene. It ships as
its own PR with a `use_shadow_atlas` toggle defaulting to off until pixel-parity
against the current path is confirmed on the test scenes.

### 3.3 Diffuse-only direct lighting target

Add a second LBUFFER attachment holding diffuse-only direct radiance. The SSIL
trace comment already identifies this as the eventual refactor target
([fs_ssil_trace.sc:110-120](engine_data/data/shaders/ssil/fs_ssil_trace.sc#L110-L120)).
USC-GI's screen-space near-field tier needs it for the same reason, and it
removes the `(1 - metalness)` approximation entirely.

---

## 4. Scene representation

### 4.1 Mesh signed distance fields

**Why SDF rather than a triangle BVH.** Unity's Surface Cache traces a BVH
through `UnifiedRayTracing`. On SM5.0 without wave intrinsics, BVH traversal is
stack-heavy and badly divergent, and every dynamic object forces a TLAS refit.
Sphere tracing a distance field has bounded, uniform cost, no stack, no
divergence, and — critically — a rigid instance transform is *free*: a moving
object needs its matrix updated and nothing else. This is exactly why Lumen's
software path (the one that shipped on last-generation consoles) is SDF-based.

**Baking.** A new asset compiler output alongside the mesh, produced on the
`threader` pool:

- Voxel size = `clamp(max_extent / 64, min_voxel, max_voxel)`, artist-overridable
  per asset via `.meta`.
- Distance computed by triangle-to-point queries over a bounded neighbourhood,
  sign from generalised winding number (robust to non-watertight meshes, unlike
  ray-parity).
- Stored **sparse**: 8^3 bricks, R8 unorm encoding distance in
  `[-4, +4]` voxels, with a 3D indirection texture (R16U) mapping brick coords
  to atlas slots. Empty-space bricks are not allocated; their indirection entry
  encodes a conservative "distance is at least N voxels" constant so tracing can
  take a large step without a memory fetch.
- A per-asset `is_two_sided` / `thickness_bias` flag for foliage cards and
  single-quad geometry, matching Lumen's "mostly two-sided" handling.

**Skinned and deforming meshes.** SDFs are rigid. Skinned meshes get a
**proxy SDF driven by the skeleton**: the bake produces one SDF per bone-dominant
segment (the mesh already stores per-bone influenced vertex bounds in
`skin_bind_data::bone_influence::bounds`,
[mesh.h:78-82](engine/engine/rendering/mesh.h#L78-L82)), and at runtime each
segment is an independently transformed instance. A character therefore casts
correct, animated indirect shadows without any per-frame revoxelisation. Fully
procedural meshes fall back to an OBB proxy.

### 4.2 Material voxels

The reason Lumen needs cards at all is that an SDF hit gives position and
gradient-normal but **not albedo**. USC-GI solves this by baking a second brick
set with the same indirection structure:

- `RGBA8` — albedo (rgb) + opacity/coverage (a), at 4^3 per 8^3 distance brick.
- `R11G11B10F` — emissive, same resolution.

Sampled by 4-tap trilinear at the hit point. This costs roughly 1.5x the
distance atlas and eliminates card generation, card atlas allocation, card
capture passes, and card defragmentation — three of Lumen's most artifact-prone
subsystems.

### 4.3 Global SDF clipmap

Four levels centred on the camera, each 128^3 voxels covering
`{ 8 m, 32 m, 128 m, 512 m }` (artist-settable). Composited on the GPU from the
mesh SDF instance list:

- Only clipmap *pages* whose bounds intersect a moved/added/removed instance are
  re-composited. Static scenes cost zero.
- Camera motion scrolls the clipmap toroidally; only the newly entered slab is
  composited.
- Level 0 additionally holds a `R8` "coverage" channel used to detect thin
  geometry that the coarse levels merged, so tracing knows to fall back to
  per-instance tracing there (Section 5.2, leak control).

Dense cost: 4 x 128^3 x R8 = 8 MB. Sparse page allocation typically brings this
under 3 MB.

---

## 5. Ray tracing — three tiers

A single `trace_gi_ray(origin, dir, tmax)` in a shared header
`engine_data/data/shaders/gi/gi_trace.sh`, tiered by distance so each tier is
used only where it is both cheapest and most accurate.

### 5.1 Tier 1 — screen space, 0 to ~1.5 m

Reuses the existing Hi-Z marcher in
[hiz_trace.sh](engine_data/data/shaders/hiz_trace.sh). Highest resolution
occlusion available, catches contact darkening and small-scale detail that no
voxel structure at 5-10 cm can represent. On a validated hit, reads the
diffuse-only LBUFFER (Section 3.3).

Unlike current SSIL, a tier-1 *miss* is not a fallback to the sky — it
continues into tier 2. This removes L3 entirely: the thickness heuristic no
longer decides between "occluded" and "open sky", only between "resolve here" and
"resolve one tier down".

### 5.2 Tier 2 — per-instance mesh SDF, ~1.5 m to ~30 m

The near field where leaking matters. Instances are culled into a froxel grid
(the same 32x32x32 clustered structure used for the light buffer), and the ray
sphere-traces each candidate instance's SDF in *object space* — exact
per-instance geometry, no clipmap merging of thin walls.

**Thin-wall guarantee (R3).** Because tier 2 traces the per-instance SDF, a
10 cm wall is represented at its own asset's voxel size (typically 2-5 cm), not
at the clipmap's. The wall occludes. The coarse-level merge that causes leaking
in pure voxel-cone-tracing approaches never occurs inside 30 m.

### 5.3 Tier 3 — global SDF clipmap, ~30 m to `max_distance`

Sphere tracing the clipmap, stepping up a level as `t` grows. Cheap, low
frequency, and correct at the scale where a 50 cm voxel is an accurate
representation. This is the tier that delivers R2: geometry behind the camera,
outside the frustum, or kilometres away is present in the clipmap.

### 5.4 On hit

```
hit -> world position P, gradient normal N
    -> read spatial hash cache at (P, N)
       -> HIT:  return cached radiance   (multi-bounce, already converged)
       -> MISS: return albedo(P) * sky_and_direct_estimate(P, N)   (single bounce,
                conservative), and append (P, N) to the cache-seed list so the
                update pass populates it for next frame
```

**Miss returns sky.** A ray that reaches `max_distance` returns
`eval_radiance_sh(dir)` — the same sky SH used today. That fallback is now
*correct* rather than a stand-in, because it is only reached when the ray
genuinely escaped the scene.

---

## 6. Spatial hash radiance cache

### 6.1 Key

```
level  = log2 clamp( distance_to_camera * level_scale / base_cell_size )
cell   = floor( P / cell_size(level) )                    // world anchored
octant = normal_to_index(N)                               // 24 quantised directions
key    = pcg_hash( cell.x, cell.y, cell.z, level, octant )
```

Three properties follow directly:

- **World anchored** => the key of a given surface point never changes with the
  camera. This is R1 at the data-structure level, not at the filter level.
- **Normal in the key** => the lit side and the dark side of a wall occupy
  different entries even when their cells coincide. This is the first of three
  leak defences (R3).
- **Distance-dependent level** => cells are ~5 cm near the camera and metres far
  away, so cache size is bounded independent of world size.

Level transitions are the classic cache-LOD popping risk. Mitigation: cells are
allocated at *two* adjacent levels within a transition band and blended by
distance, exactly as clipmap levels are blended.

### 6.2 Entry layout

Two parallel buffers, the standard split that avoids atomics on float data:

```
// accumulation (atomicAdd, u32 fixed point)   32 B/entry
struct gi_cache_accum { uint sh[9]; uint sample_count; uint pad[2]; };

// resolved (read by tracing)                  32 B/entry
struct gi_cache_entry {
    uint  checksum;          // full 32-bit key hash, detects probe collisions
    uint  frame_touched;     // for age-based eviction
    uint  sh_rgb_packed[6];  // L1 SH (4 coeffs x RGB) as packed fp16 pairs
};
```

L1 SH per cell (not a single irradiance value) is what lets reconstruction apply
the **per-pixel shaded normal** — the same property Unity calls out as how they
"reconstruct per-pixel irradiance by filtering the patch data". A flat per-cell
colour would lose all normal-map detail in the indirect term.

Default 2^19 entries = 32 MB resolved + 32 MB accumulation. Configurable to
2^21 for large open worlds.

### 6.3 Insertion and eviction — no defragmentation

Open addressing with linear probing over 8 slots. Insert:

1. `slot = key & mask`
2. For 8 probes: `atomicCompSwap(checksum[slot], EMPTY, key)`. Success => own it.
   Existing value equals `key` => own it.
3. All 8 occupied by other keys => steal the one with the oldest
   `frame_touched`. Because the eviction victim is chosen by age and the
   structure is fixed-size and open-addressed, there is **no free list, no
   fragmentation, and no compaction pass**.

This is the concrete improvement over the ring buffer Unity's document says
needs per-frame defragmentation.

### 6.4 Cache update pass (the radiosity step)

Per frame, a bounded budget of cells (default 8192) is selected and refreshed:

- **Selection priority:** cells touched by this frame's screen probes (so what
  is visible converges first), then cells by descending age. Selection is a
  compute pass writing an indirect dispatch arg buffer, so cost is fixed
  regardless of cache occupancy.
- **Per selected cell:** `N` rays (default 32) cosine-distributed about the
  cell's octant normal, jittered per frame with a world-space-seeded blue noise
  (seeded from the *cell key*, not screen position — this is required for R1:
  the same cell must not get a different sample pattern because the camera
  moved). Each ray runs tier 2/3 tracing.
- **At each hit:** `albedo(P) * ( direct_lighting(P, N) + cache_lookup(P, N) ) +
  emissive(P)`.
  The `cache_lookup` term is what makes this **infinite-bounce**: the cache
  already contains last frame's converged radiance, so bounce N is free. Same
  mechanism as Lumen's surface cache radiosity, without the card indirection.
- **Accumulation:** exponential moving average with an adaptive rate —
  `alpha = max(1 / sample_count, min_alpha)` where `min_alpha` derives from the
  artist's "Temporal Smoothing" (Unity's parameter name; keep it for
  familiarity). Fast-moving scenes set `min_alpha` high and accept noise; slow
  scenes set it low and get stability.

**Dynamic response (R4).** Two mechanisms:
1. **Invalidation on change.** When a light's transform/intensity changes or an
   instance moves, the affected world region's cells have `frame_touched` forced
   old and `sample_count` reset to a small value, which both raises them in the
   selection priority and raises their EMA alpha. Convergence is fast where it
   matters and untouched cells cost nothing.
2. **Priority feedback.** Cells whose new sample deviates from the stored value
   by more than a threshold self-promote in the next frame's selection.

This is strictly better than a fixed round-robin: a light switching on in one
room does not force the whole world to re-converge.

---

## 7. Final gather — screen probes

Reading the cache directly per pixel (Unity's approach) is cheap but limited by
cell size: it cannot produce sharp contact shadows or fine indirect gradients.
Lumen's screen probes are what buy the extra quality, and they cost little
because they run at 1/256th of the pixels.

### 7.1 Placement

- Uniform probe every 16x16 pixels.
- **Adaptive probes** inserted where the four surrounding uniform probes all
  fail a plane test against the pixel's depth/normal (thin geometry, foliage,
  silhouettes). Budget-capped; allocated from an append buffer.
- Each probe stores world position, plane, and an octahedral radiance map
  (default 4x4 = 16 directions, 8x8 for the high preset) in an atlas.

### 7.2 Tracing

Rays are cosine-distributed over the probe's hemisphere and importance-guided
by the *reprojected previous-frame probe* — bright directions get more samples.
The ray budget per probe is fixed, so cost is deterministic.

### 7.3 Temporal reprojection — world space, cache-seeded

This is the mechanism that delivers R1 in the *screen* domain:

- Probes reproject by their stored **world position**, not by screen motion
  vectors. Camera rotation reprojects a probe exactly; only translation-induced
  parallax and true disocclusion invalidate it.
- **On disocclusion, seed from the spatial hash cache rather than resetting to
  zero.** The world-space cache already holds converged radiance for that
  location. This is the single most important difference from current SSIL,
  where disocclusion drops to a 4-ray estimate and produces the leading-edge
  boil described in L2. A newly disoccluded pixel starts at roughly the right
  answer and refines, instead of starting at noise and converging.

### 7.4 Spatial filter and integration

- Filter across neighbouring probes with plane-distance and normal weights
  (the existing a-trous edge-stop formulation in
  [cs_ssil_spatial_denoise.sc](engine_data/data/shaders/ssil/cs_ssil_spatial_denoise.sc)
  ports directly).
- Project each probe's octahedral map to 3-band SH.
- Per pixel, bilinearly interpolate the four surrounding probes' SH with plane
  weights, and evaluate with the **full-resolution G-buffer normal**. Detail
  comes from the normal map and from tier-1 occlusion, not from probe density.

### 7.5 Third leak defence

Interpolation weights include a **short SDF visibility check** between the pixel
and each contributing probe (a handful of steps at clipmap level 0). A probe on
the other side of a wall gets weight zero. Together with the normal-in-key
(6.1) and per-instance tier-2 tracing (5.2), this is the third of the three
defences behind R3.

---

## 8. Composition

`fs_deferred_indirect_light` changes minimally. The current line
([fs_pbr_lighting.sh:532](engine_data/data/shaders/fs_pbr_lighting.sh#L532)):

```glsl
vec3 indirect_diffuse = mix(irradiance, ssil_sample.rgb * PI, ssil_sample.a);
```

becomes a read of the integrated GI SH buffer, with the global sky SH kept only
as the `validity == 0` fallback (off-screen-history first frame, GI disabled,
or a pixel outside every clipmap level). Because USC-GI returns full
hemispherical irradiance including sky, there is no double counting and no
`brightness` fudge factor — `brightness` becomes a pure artistic multiplier
defaulting to 1.0.

**SSIL is retained, not deleted.** It becomes the low-end / fallback GI mode
selected by quality tier, and its Hi-Z tracing code is shared with USC-GI tier 1.

---

## 9. Contracts (per `unravel-architect`)

### 9.1 Runtime API

New pass `gi_pass` in
`engine/engine/rendering/pipeline/passes/gi_pass.{h,cpp}`, mirroring the
`ssil_pass` shape (settings struct, `run_params`, `run`, `release_resources`).
New service `surface_cache_service` registered in `rtti::context`, owning the
SDF instance list, clipmap, and hash buffers — these outlive any single
`render_view` because they are world state, not per-camera state.

New pipeline step: `pipeline_steps::gi_pass = 1u << 7`.

### 9.2 ECS surface

`gi_component` (volume component, same `merge_into` blend pattern as
[ssil_component.h](engine/engine/rendering/ecs/components/ssil_component.h)):

| Field | Default | Meaning |
|---|---|---|
| `enabled` | true | |
| `quality` | `medium` | low / medium / high / ultra — drives ray and probe counts |
| `intensity` | 1.0 | artistic multiplier |
| `temporal_smoothing` | 0.7 | 1.0 = slow-paced, 0.0 = fully dynamic (Unity's parameter, same semantics) |
| `cache_cells_log2` | 19 | hash capacity |
| `cache_update_budget` | 8192 | cells refreshed per frame |
| `near_field_distance` | 1.5 | tier 1 -> tier 2 crossover, metres |
| `far_field_distance` | 30.0 | tier 2 -> tier 3 crossover, metres |
| `max_distance` | 500.0 | ray termination |
| `probe_spacing` | 16 | pixels |
| `spatial_filter_radius` | 2 | probe-space a-trous radius |

Per-model opt-out: `model_component::contributes_to_gi` (default true), so
first-person arms and editor-only geometry stay out of the SDF instance list.

Per-asset in `.meta`: `sdf_voxel_size`, `sdf_two_sided`, `sdf_thickness_bias`,
`generate_sdf` (default true for static-flagged meshes).

### 9.3 Persistence

`gi_component` serialises through the existing ser20 volume-component path with
a `version` field from day one. SDF/material bricks are a **compiled asset
output** keyed by the mesh UID + bake settings hash — never committed, always
regenerable, so no migration burden and no UID churn.

### 9.4 Editor surface

- Inspector for `gi_component` via the standard registry.
- Debug visualisations added to `run_debug_visualization_pass`: global SDF
  slice, cache cell occupancy heat map, cache age heat map, probe placement
  (uniform vs adaptive), per-tier ray-hit attribution, GI-only lighting.
- A "GI Cache" profiler section: entries live, insert failures, evictions/frame,
  cells updated/frame, rays/frame per tier, bake queue depth.

### 9.5 Script surface

C# mirror of `gi_component` in `engine_data/data/scripts/`, matching the
existing volume-component parity convention.

---

## 10. Play mode lifecycle

| Question | Answer |
|---|---|
| Edit mode only | SDF bake scheduling on asset import/change. |
| Play mode only | Nothing — GI runs identically in both, which is required for WYSIWYG. |
| Resets on `on_play_end` | Cache contents and probe history are cleared, so a scene restored to its authored state does not inherit play-mode radiance. SDF bricks persist (they are asset data). |
| Survives play mode | Baked SDF/material bricks; the SDF instance list is rebuilt from the restored scene. |

Edit-mode mutation guard: instance add/remove/transform goes through a single
`surface_cache_service::mark_instance_dirty` entry point, which is safe to call
in either phase. No edit-only mutation path exists, so the `play_mode` hazard
the project guards against does not arise.

---

## 11. Validation harness (this is how R5 is met)

"No bugs" is not achievable by care alone on a system this size. The design
includes its own falsification tooling, built *before* the features it validates:

1. **Offline path-traced reference.** A CPU path tracer over the same scene data,
   run headless, producing ground-truth irradiance buffers. Every quality claim
   is an RMSE number against it, not an opinion.
2. **Golden-image scenes**, each targeting one requirement:
   - `gi_cornell` — energy and colour bleed correctness vs reference.
   - `gi_offscreen` — R2. Red wall behind camera; asserts floor tint matches the
     on-screen case within 3%.
   - `gi_rotation` — R1. 360 deg orbit of a static scene; asserts frame-to-frame
     delta of the lit buffer stays under threshold outside disocclusion masks.
   - `gi_thinwall` — R3. 10 cm wall between bright and dark rooms; asserts dark
     side < 2% of bright side.
   - `gi_dynamic_light` — R4. Light toggles at frame N; asserts 90% convergence
     by N+8 near field / N+30 far field, and zero residual at N+120.
   - `gi_dynamic_geometry` — moving occluder; asserts no ghost after motion stops.
   - `gi_skinned` — animated character; asserts indirect shadow tracks the pose.
3. **Cache invariant checks** behind a debug define: no duplicate keys in a probe
   chain, `sample_count` monotonic between invalidations, no NaN/Inf in resolved
   entries, insert-failure rate below a threshold.
4. **Determinism check.** Same scene, same frame index, same camera => bit-identical
   cache state. Catches the class of bug where screen-space state leaks into a
   world-space structure — the exact failure mode that would silently break R1.
5. **Backend matrix.** All golden scenes on D3D11, D3D12, Vulkan, OpenGL. SM5.0
   is the binding constraint and it must be tested first, not last.

---

## 12. Budgets

Targets at 1080p, mid-range 2024 discrete GPU, `medium` preset.

| Pass | Budget | Notes |
|---|---|---|
| Clipmap composite | 0.2 ms | zero when nothing moved |
| Cache selection + update (8192 cells x 32 rays) | 1.8 ms | bounded, independent of scene size |
| Screen probe trace (8160 probes x 16 rays) | 2.2 ms | tier 1 dominates, cheap |
| Probe filter + SH projection | 0.4 ms | |
| Per-pixel integration | 0.3 ms | full res, 4 SH taps |
| **Total** | **~5 ms** | vs current SSIL ~3.5 ms at full-res 4 rays |

| Memory | Size |
|---|---|
| Global SDF clipmap (sparse) | ~3 MB |
| Mesh SDF brick atlas | 128 MB cap, artist-settable |
| Material brick atlas | 192 MB cap |
| Hash cache (2^19) | 64 MB |
| Screen probe atlas + history | ~12 MB |

Brick atlases participate in the existing `gfx::eviction` accounting.

---

## 13. Delivery phases

Each phase is independently shippable and independently verifiable. Nothing
after Phase 1 is started until Phase 1's golden images pass.

| # | Deliverable | Verified by |
|---|---|---|
| 0 | GPU light buffer + shadow atlas + diffuse-only LBUFFER | Pixel parity with current direct lighting on all test scenes |
| 1 | Mesh SDF bake (asset compiler + threader) + brick atlas + debug slice view | Visual SDF slice matches mesh; bake determinism |
| 2 | Global SDF clipmap + composite + `trace_gi_ray` tier 3 | Ray-hit debug view; offscreen geometry visible in trace |
| 3 | Spatial hash cache + update pass, cache visualised directly (no probes yet) | `gi_cornell`, `gi_offscreen`, `gi_thinwall` |
| 4 | Screen probes + world-space temporal + cache-seeded disocclusion | `gi_rotation` |
| 5 | Spatial filter, SH integration, composition into indirect pass | Full RMSE vs path-traced reference |
| 6 | Dynamic invalidation, priority feedback, skinned proxies | `gi_dynamic_light`, `gi_dynamic_geometry`, `gi_skinned` |
| 7 | Quality presets, editor inspector, profiler, C# parity | Backend matrix |
| 8 | *(extension)* SSR world-space fallback: SSR misses trace the SDF and read the cache | Reflections no longer break at screen edges |

---

## 14. Comparison

| | Lumen (SW mode) | Unity Surface Cache | USC-GI |
|---|---|---|---|
| Scene structure | Mesh SDF + global SDF | BVH (UnifiedRayTracing) | Mesh SDF + global SDF |
| Surface attributes | Mesh cards (offline gen) | Implicit, from BVH hit | Material voxel bricks (offline, no card layout) |
| Radiance store | Card atlas, per-mesh | Patch ring buffer, needs defrag | World hash, age-evicted, no defrag |
| World stable | Yes | Yes | Yes (world-anchored key + world-space probe reprojection) |
| Offscreen | Yes | Yes | Yes (global SDF) |
| Final gather | Screen probes | Direct patch filtering | Screen probes + cache-seeded disocclusion |
| Multi-bounce | Infinite (cache feedback) | Yes | Infinite (cache feedback) |
| Skinned geometry | Proxies | Yes (BVH refit) | Skeleton-driven segment SDFs |
| HW ray tracing | Optional | Optional | Not available in bgfx; SW only |
| Per-asset preprocessing | Cards + SDF | None | SDF + material bricks |

The two places USC-GI should be genuinely better than both references:
**no defragmentation** (Section 6.3) and **cache-seeded disocclusion**
(Section 7.3), which removes the class of temporal artifact that current SSIL
exhibits and that screen-probe systems generally handle by simply converging
slowly.

The place it is worse: no hardware ray tracing path, so the near-field detail
ceiling is set by SDF voxel resolution rather than by triangles. Tier 1
screen-space tracing covers most of that gap; the residual shows up as slightly
soft contact darkening on geometry finer than the asset's voxel size.

---

## 15. Open questions for the user

1. **Platform floor.** Is D3D11 / SM5.0 genuinely required, or can USC-GI
   require SM6.0 (D3D12/Vulkan)? Wave intrinsics would cut the probe trace and
   cache update cost meaningfully, and would allow a much better sparse
   allocator.
2. **Bake budget.** SDF baking is per-asset and async, but a large project's
   first import will be long. Acceptable, or does it need a cloud/prebaked path?
3. **Memory ceiling.** The 320 MB default for brick atlases is generous. What is
   the actual target platform budget?
4. **Phase 0 appetite.** The shadow atlas refactor is the riskiest single change
   and touches every existing scene. Confirm before starting it.
