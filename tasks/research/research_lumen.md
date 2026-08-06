All research is complete — both SIGGRAPH decks read in full (199-page 2022 PDF with speaker notes, 118-slide 2021 PPTX with notes), Epic's technical docs and performance guide fetched, Narkowicz's development history, and engine defaults verified against the UE 5.4.4 cvar dump. The full report follows.

---

# Lumen GI Architecture — Technical Deep-Dive Report

**Primary sources** (cited inline as [S22 pN] / [S21 sN] / [Docs-Tech] / [Docs-Perf] / [KN-blog] / [CVar544]):
- **[S22]** Wright, Narkowicz, Kelly — *"Lumen: Real-time Global Illumination in Unreal Engine 5"*, SIGGRAPH 2022 Advances in Real-Time Rendering, 199-page slide deck with speaker notes ([PDF](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf))
- **[S21]** Wright — *"Radiance Caching for Real-time Global Illumination"*, SIGGRAPH 2021 Advances, 118 slides with full speaker notes ([PPTX](https://advances.realtimerendering.com/s2021/index.html))
- **[KN-blog]** Narkowicz — [*"Journey to Lumen"*](https://knarkowicz.wordpress.com/2022/08/18/journey-to-lumen/) (development history, abandoned approaches, prototype perf numbers)
- **[Docs-Tech]** [Epic: Lumen Technical Details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine); **[Docs-Perf]** [Epic: Lumen Performance Guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine)
- **[CVar544]** [UE 5.4.4 cvar defaults wiki](https://indxzero.github.io/ue544cvarwiki/articles/r.lumen.screenprobegather/) (engine-source-extracted defaults, individually verified)

---

## 1. Overall architecture

Lumen decomposes GI into three "fundamental problems" [S22 p4-13]: (1) how to trace rays, (2) how to solve the full multi-bounce path, (3) how to solve noise at <1 ray/pixel when quality indoor GI needs "hundreds of effective samples" (outdoors ~100 rpp, indoors 500+ rpp [S21 s12-13]).

**Data flow, frame-level:**

```
Mesh import (offline):  mesh SDF build + card (surfel-cluster) generation
        │
LumenScene update (runtime, amortized):
  card capture (ortho raster → albedo/normal/depth/opacity/emissive atlas)
  → direct lighting on surface cache (shadowmap sample + offscreen shadow rays)
  → radiosity/indirect on surface cache (hemisphere probes, reads OWN previous
    frame output + current direct = n+2 bounce feedback loop) [S22 p11, p76]
  → voxel lighting clipmaps (merged card radiance, for Global-SDF hit shading) [S22 p80]
  → global SDF clipmap update (modified bricks only) [S22 p41]
        │
Per-pixel gather:
  screen probe placement (uniform 16px grid + adaptive refinement)
  → product importance sampling (BRDF × reprojected last-frame radiance)
  → hybrid trace per probe ray: Screen(HZB) → [Mesh SDF → Global SDF] or
    [HWRT near-field → far-field] → skylight on miss; each stage writes
    {TraceDistance, bHit}, next stage resumes the ray [S22 p22]
  → rays shortened to ~2 m…; misses interpolate World Radiance Cache probes
    (4 clipmaps, 32×32 octahedral, persistent) [S21 s69-74]
  → probe-space spatial filter (3×3) → per-probe SH3 conversion
  → full-res interpolate + integrate (plane-weighted, jittered)
  → full-res temporal accumulation (depth-rejection, max 10 frames)
  → short-range AO / bent-normal composited AFTER temporal filter [S22 p167]
        │
Reflections (separate path): GGX VNDF ray gen → same trace pipeline →
  spatial reuse → temporal accumulation → bilateral filter; roughness>0.4
  resamples screen probes instead of tracing [S22 p175-186]
```

The **first bounce** gets dedicated machinery (Final Gather for diffuse, stochastic denoised reflections for specular); **all later bounces** come "free" from the surface cache feedback loop — the texture-space gather reads the surface cache which reads itself, propagating one extra bounce per update [S22 p11]. There are three final-gather **domains**: opaque (contiguous 2.5D screen space), translucency/fog (contiguous 3D froxel volume), and surface cache (noncontiguous 2D texture space) — all instances of the same probe-gather archetype [S22 p14, p147].

---

## 2. Surface cache

### Card generation (offline, at mesh import)
Cards are **axis-aligned orthographic projections** ("uniform rectangular clusters of surfels"); free orientation was tried and rejected as not worth it [S22 p60, KN-blog]. Build ≈ **0.2 ms for a 1.5M-triangle mesh** [S22 p60]. Pipeline:
1. **Voxelize to surfels**: cast **64 rays per 2D cell** through the object per axis; surfel coverage = % of ray hits; store previous hit position for near-plane visibility checks [S22 p61].
2. **Discard interior surfels**: 64 hemisphere rays per surfel; mostly-backface hits ⇒ inside geometry; surfel occlusion = avg distance to hits (importance weight) [S22 p61].
3. **Cluster growth**: seed at unused surfel; weight candidates by distance-to-cluster-bounds, cluster aspect (prefer square), surfel occlusion, and visibility from the cluster near-plane; re-grow from centroid until stable; accept above coverage threshold [S22 p62].
4. **Global optimization**: repeatedly re-grow all clusters in parallel from centroids, delete bad clusters, insert new ones in gaps; finally sort by coverage and keep the top **N cards (default 12, "Max Lumen Mesh Cards")** [S22 p63, Docs-Tech]. Failure fallback: 6-side cubemap-style projection [S22 p60]. Skeletal meshes get 6 cards around pre-skinned bounds (material only — cards don't follow animation) [Docs-Tech, S22 p83]. Runtime **card merging** for aggregates (distant buildings, pebble scatters): auto-group small overlapping instances or user tags, 6-card cubemap capture of the whole group [S22 p71].

### Virtualization and atlas
- **Physical atlas 4096×4096, 128×128 physical pages**. Cards ≥128² are split across pages (0.5-texel borders); smaller cards are **sub-allocated inside one page** via a 2D allocator with 0.5-texel borders around each sub-allocation. Page table entry: 4b log2 atlas-scale-X | 4b log2 scale-Y | 12b bias-X | 12b bias-Y (biases in multiples of 8). Lookups are **flattened**: unmapped high-res pages point directly at the low-res always-resident page — single-indirection sampling, no fallback walk [S22 p67].
- **Two allocation classes**: low-res *always-resident* pages allocated around the camera by distance/size (small cards culled = LOD), and *on-demand* high-res pages driven purely by GPU feedback for reflections; oldest deallocated on pressure (heap keyed by last-used) [S22 p64-65].
- **GPU feedback**: each ray hit stochastically selects one of the blended card samples, bumps the page's last-used time, writes requested mip to a feedback buffer; merged on GPU via hash table → compacted array of {page, hitcount} → CPU downloads, sorts, maps/unmaps [S22 p66].

### Capture
Runtime re-rasterization of one mesh with an ortho camera per card. **Fixed budget: 512×512 texels/frame**, prioritized by camera distance + feedback; a small number of oldest pages also recaptured each frame for animated materials. Nanite makes capture **~10× faster** (all cards in one vis-buffer draw, one dispatch per material, ideal LOD cluster selection) [S22 p68]. Non-Nanite meshes make capture expensive; foliage/ISM only supported with Nanite [Docs-Tech].

**Stored attributes (per 4k×4k atlas), BC-compressed at runtime** [S22 p69]:

| Attribute | Format | Size |
|---|---|---|
| Albedo | RGB8 → BC7 | 16 MB |
| Opacity | R8 → BC4 | 8 MB (any-hit shading) |
| Depth | R16 (uncompressed) | 32 MB (validity + projection test) |
| Normal | hemisphere-encoded RG8 → BC4 | 16 MB |
| Emissive | RGB FP16 → BC6H | 16 MB |

Specular and subsurface are folded into albedo (energy-preserving approximation). Invalid texels marked `Depth = Max`. Alpha masking is *disabled* during capture so opacity can be evaluated separately at trace time [S22 p69].

### Sampling at a ray hit
MeshIndex → card grid → cell yields 6 cards; surface normal selects **3 cards to project**; per card, Gather4 the depth texture for manual bilinear, weight texels by `|TexelDepth − RayHitT|` (occlusion rejection) and `CardNormal · HitNormal` (anti-stretch), discard invalid texels, blend all card samples [S22 p70].

### Lighting on the cache
- Texels can sit inside geometry ⇒ **rays hitting triangle backfaces return zero radiance** (blackens interior texels, stops bilinear leaks); tracing from texels needs a normal/direction-based surface bias [S22 p73].
- **Update scheduling**: `priority = LastUsed − LastUpdated`, tracked separately for direct and indirect. Radix-sort histogram; consume buckets until budget: **1024×1024 texels/frame direct, 512×512 texels/frame indirect** [S22 p74]. Engine-default framing: direct updates SurfaceCacheTexels/32 per frame (`r.LumenScene.DirectLighting.UpdateFactor=32`), radiosity /64 (`r.LumenScene.Radiosity.UpdateFactor=64`) [CVar544]. Lighting is resampled when pages resize/remap [S22 p74].
- **Direct lighting**: selected pages split into **8×8 texel tiles emitted in Z-order** (trace coherency); **first 8 lights per tile** (`MaxLightsPerTile`); **1-bit shadow mask per light**; pass 1 samples existing shadow maps and emits a compacted ray list for unresolved texels (typically behind camera); pass 2 traces offscreen shadow rays; pass 3 applies lights with the mask [S22 p75].
- **Radiosity (indirect on cache)**: a texture-space final gather. Per updated tile: **one 4×4-texel-spacing hemispherical probe with 4×4 traces** (`Radiosity.ProbeSpacing=4`, `HemisphereProbeResolution=4` ⇒ 16 rays/probe) [S22 p77, CVar544]; probe placement and ray directions **jittered over a 4-frame cycle** (frame index stored per page). Each hit samples *current-frame direct + last-frame indirect* ⇒ two fresh bounces per update, n+2 via feedback [S22 p76]. Gather: bilinear of 4 probes with (a) **probe-plane weighting** and (b) **visibility weighting using the probe's stored hitT depths**; temporally accumulated into the indirect atlas with **max 4 accumulated frames** (counter per texel) to bound ghosting at the low update rate [S22 p78]. Whole radiosity budget: **< 1/16 of total Lumen budget** [S22 p76].

### Voxel lighting (Global-SDF hit shading)
Global SDF hits don't know the mesh ⇒ cards are also merged into **4 clipmaps of 64³ voxels, radiance per 6 axis directions per voxel**; sampling interpolates 3 directions by normal, normalized by alpha-channel weights that account for missing cards [S22 p80]. Built via a cached **visibility buffer**: modified 4³ bricks tracked; per brick, cull objects; **6 rays per voxel**, one thread per mesh-SDF trace, `InterlockedMin` into `24b MeshIndex | 8b HitT` [S22 p81]. Geometry is cached; the whole visibility buffer is **re-shaded from the surface cache every frame** (after lane compaction) since lighting changes can't be tracked cheaply [S22 p82]. Acknowledged as the #1 quality limitation ("too coarse") [S22 p83].

---

## 3. Software ray tracing (SM-relevant path)

Motivation: scale below HWRT-class GPUs, 60 fps consoles, and **heavily overlapping kitbashed instances** where a two-level BVH forces the ray through every overlap [S22 p32]. Note for your SM5.0 target: Epic gates SWRT on DX12/SM6 [Docs-Tech] only because of wave intrinsics (prefix-sum compaction, waveops); the algorithms themselves are plain compute — groupshared scans substitute.

### Mesh distance fields (MDF)
- Built at import: Embree point queries for distance; **64 rays per voxel counting backface hits** for sign; ~**0.6 ms build for 1.5M tris**. Storage: **narrow-band SDF, mip-mapped virtual volume texture, sparse 8³ bricks + 0.5 texel border, [-4,+4] voxel distances quantized to 1 byte**; mip0 resolution from mesh size/import settings, each mip halves res and doubles max encoded distance [S22 p34].
- **Streaming**: per-frame GPU pass computes wanted mip per instance from camera distance; CPU downloads requests, streams bricks in/out of a **fixed 320 MB pool** with a linear allocator (no fragmentation handling needed) [S22 p35].
- **Tracing**: sphere-trace stepping across mips (closer ⇒ finer mip, farther ⇒ coarser; à la Claybook [Aaltonen 2018]); **hard cap 64 iterations — the 64th iteration is forced to report a hit at current t** (over-occlude rather than leak or loop); hit normal via **6-tap central differencing at ±0.5 voxel**; then sample surface cache [S22 p36].
- **Robustness fixes** (critical for a reimplementation):
  - *Open meshes*: negative region wrapped — a **virtual surface inserted after 4 voxels** of negative distance [S22 p47].
  - *Thin walls*: sub-voxel walls never cross zero ⇒ **runtime expand by half the voxel diagonal**; original data preserved. Expand alone kills contact shadows via the needed bias, so: **start rays at t=0 with zero expand and increase expand linearly with t** (GI/shadow rays); fall expand back to 0 at shadow-ray end (don't hit the light's fixture) [S22 p48-50]. For **reflection rays** the linear ramp self-intersects at grazing angles ⇒ per-step `expand = min(MaxExpand, currentDistanceToSurface)` — always escapes the origin surface, trading over-occlusion for slight leak [S22 p51].
  - *Foliage*: per-instance **coverage channel** (flagged by two-sided material), resampled into a dedicated Global-SDF channel; coverage reduces expand, increases min step size, and drives **stochastic transparency** at hits (probabilistically continue); extra surface bias compensates un-animated SDFs [S22 p53]. Docs guidance: walls ≥ 10 cm to avoid leaks [Docs-Tech].

### Scene traversal for MDF rays
BVHs and grids were tried and rejected — long incoherent rays must march every overlapping instance [S22 p38]. Adopted constraint: **detail (MDF) rays limited to 2 m**, then continue in the Global SDF [S22 p44]. Culling for GI rays: frustum-cull instances → mark froxels actually containing screen pixels → cull objects to those froxels (coarse bounds test then **fine cull by sampling the mesh SDF**) → compact per-cell lists. Trace kernel: load one cull-grid cell, march every object in it **up to the current best hitT** [S22 p44]. **Directional-light shadow rays** are full-length (no cone-widening excuse): cull objects into a light-space 2D grid by **rasterizing their OBBs** with SDF fine-culling in the pixel shader; trace any-hit [S22 p45].

### Global distance field (GSDF)
- **4 clipmaps of 256³ voxels** centered on the camera; per-clipmap **sparse virtual volume texture, 8³ bricks + 0.5-texel border, narrow-band [-4,+4] voxel distances in 1 byte** [S22 p40]. Lumen scene range: **200 m default, up to 800 m** [Docs-Tech] (i.e., clipmap extents double per level).
- **Update**: heavily cached. Track modifications → GPU list of modified bricks → cull instances to clipmap → to bricks → **SDF-sample fine culling** → alloc/free persistent bricks → per-voxel min over culled objects (MDFs + heightfields). Distant clipmaps update less often (timeslicing) and have per-clipmap LOD thresholds dropping small objects. **Static and dynamic bricks cached separately** and composited so movers don't force re-merge of static surroundings. Non-uniform scale handled by clamping to analytical OBB distance [S22 p41-42].
- **Coarse mip**: quarter-res, *non-sparse* volume rebuilt fully by sampling the sparse field + **5 Eikonal propagation iterations**; used for empty-space skipping instead of clipmap-hopping (clipmaps have inconsistent LOD content) [S22 p42].
- **Tracing**: iterate clipmaps small→large; per step sample coarse mip, only touch sparse bricks near surface; 6-tap gradient at hit; shade from **voxel lighting** (not surface cache directly — instance unknown) [S22 p43, p80].
- **Landscape**: one heightfield per landscape component; raymarch to zero crossing, LERP last two steps for hitT; evaluate surface-cache opacity at hit and keep marching if transparent [S22 p37].

### Cone tracing history (why it's not the shipping path)
Prefiltered MDF cone tracing existed and worked: surface-cache mip from cone-intersection footprint, partial occlusion from axis-to-surface distance, out-of-order partial hits merged with Weighted-Blended OIT [S22 p140]. Abandoned: leak-vs-over-occlusion is unavoidable, can never resolve a small distant window, incompatible with HWRT [S22 p141]; prefiltering also gathers invisible texels ⇒ leaks [KN-blog]. Replaced by Monte Carlo + the Final Gather. (Earlier abandoned reps: card-heightfield tracing/POM — unreliable coverage leaks; voxel cone tracing — merge leaking in coarse mips; 8³ 1-bit voxel bricks — slow [S22 p5, p39; KN-blog].) Narkowicz's prototype-to-ship optimization: 25.56 ms (Vega 64, 1080p: radiosity 3.86 + direct 2.26 + prefilter/voxel-inject 8.48 + card diffuse 5.50 + card reflections 5.46) → **<8 ms on PS5, later ~4 ms** [KN-blog].

### Screen tracing (runs first in both SW and HW pipelines)
HZB traversal — stackless walk of closest-HZB mips [Uludag 2014]; iteration budget clamped for grazing rays; diffuse rays trace **half-res HZB**; typical 5–50 steps [S22 p26]. Purpose: covers GBuffer↔ray-scene mismatches (self-intersection, Nanite-fallback mismatch), unrepresented geometry (skinned meshes in SWRT), any-scale detail [S22 p23-24]. Handoff correctness: **step back to last unoccluded position** whenever the ray goes behind a surface or offscreen, then hand {t, miss} to the next tracer [S22 p27]. After screen traces, **compact surviving rays**: up to 50% tracing speedup; compaction is **order-preserving** (Z-order within a 128×128 threadgroup, waveops prefix sum for local offset + one atomic for global) — 21% faster reflection tracing than naive atomic compaction which scrambles ray origins [S22 p29-30].

---

## 4. Screen probe gather (Final Gather, opaque)

### Layout & placement
- Probe = **octahedral map, typically 8×8 (64 traces)**, uniformly distributed **world-space** directions (neighbors share direction indexing — the property that makes reprojection/filtering O(1) lookups); atlas stores **radiance + hit distance**, with border texels [S21 s33]. `TracingOctahedronResolution=8` (clamped 4–16) [CVar544].
- **Uniform grid every 16 pixels** (`DownsampleFactor=16` ⇒ probes at 1/16-res in each dimension), then **hierarchical refinement** [Křivánek 2007]: iterate 16→8→4 px; at each level, test plane-distance interpolation from existing probes at each candidate; where it fails, place an adaptive probe; **flood fill** the final level instead of descending to per-pixel [S21 s34-35]. Adaptive probes are appended **at the bottom of the same atlas** (no separate dispatch/barriers); when the adaptive budget is exhausted, remaining failures just use flood-fill neighbors. Adaptive budget: `AdaptiveProbeAllocationFraction=0.5` of uniform count [S21 s36, CVar544].
- **Placement grid + octahedral ray directions temporally jittered** per frame; probes sit **directly on GBuffer pixels** (position+normal from GBuffer, zero gap ⇒ no interpolation leak); intra-cell occlusion differences are the temporal filter's job to hide [S21 s37].

### Ray allocation — product importance sampling
Runs one threadgroup per probe (affordable only because of downsampling) [S22 p158]:
1. Build per-octahedral-texel PDF = **BRDF PDF × incoming-radiance PDF**.
   - *Lighting PDF*: reproject probe position into **last frame's screen radiance cache**, average the 4 neighboring probes (radiance already indexed by position+direction ⇒ no search); on reprojection failure (offscreen/disocclusion) fall back to the **world radiance cache** — importance sampling never goes blind [S21 s44].
   - *BRDF PDF*: accumulated from the actual GBuffer pixels that will interpolate this probe (≈ hemisphere clip for a wall probe) [S21 s45].
2. Start from the uniform 64-direction set (keeps tracing lanes full), **sort rays by PDF; for every 3 rays below the cull threshold, subdivide the highest-PDF ray one octahedral mip level** (3 culled + 1 original = 4 sub-rays) — Structured Importance Sampling [Agarwal 2003] hierarchical thresholding mapped onto the **octahedral mip quadtree**; net effect "quality of 4× more rays for free" [S21 s47-50, S22 p159]. Tracing goes through an indirection (RayCoord, MipLevel per lane); traced radiance is composited back into the uniform octahedral layout before integration [S21 s49].
3. Refinements: **only the BRDF may cull** (lighting PDF is approximate/noisy — last frame might have missed the small bright source; must keep exploring); cull *more* aggressively at BRDF>0 thresholds but **down-weight culled directions in the spatial filter** to avoid corner darkening [S21 s52].
- Firefly control at trace time: `MaxRayIntensity=40` (pre-exposed radiance clamp) [CVar544].

### Radiance-range partition
Screen probe rays are **shortened** (screen-space range covers roughly 16 px → 2 m…16 m); beyond that, misses interpolate the world radiance cache; total trace distance 200 m; skylight past that. Explicit latency-by-distance design: contact AO (16 px) = zero latency; screen RC = temporal accumulation; world RC = probe caching; skylight = slow amortized update [S22 p148-150, p168-169].

### Probe-space spatial filter
- **3×3 in probe space ≈ 48×48 screen-space kernel**; only depth weighting needed (incoming radiance is receiver-normal-independent) [S21 s57]. Default **3 passes** (`SpatialFilterNumPasses=3`) [CVar544].
- Neighbor gather uses the *matching octahedral cell* (shared world-space direction indexing). Leak rejection: **reproject the neighbor's ray hit** toward the current probe, reject if the angle between reprojected direction and the direction being filtered exceeds threshold — filters distant lighting, preserves local shadowing [S21 s58]. Bug fix that matters: angle error asymptotically vanishes for distant hits (no parallax) so distant light always passes ⇒ leaks over contact shadows; **clamp the neighbor's hit distance to your own hit distance before reprojection** — restores contact shadows while keeping distant lighting smooth [S21 s61-63].

### Integration & upsampling to full res
- Per probe, octahedral radiance is converted to **3rd-order spherical harmonics**; full-res pixels load SH coherently and do the analytic Ramamoorthi diffuse convolution against per-pixel normal. Chosen over per-pixel BRDF-sampled octahedral fetches (incoherent, noisy at ~8 spp × 4 probes) and over Filtered Importance Sampling with mips (pulls radiance from behind the hemisphere ⇒ self-lighting near direct light) [S21 s89-90].
- Interpolation weights: **plane-distance weighting** (probe plane vs pixel plane — stops foreground misses leaking onto background). The **interpolation offset is jittered** at full res (`FullResolutionJitterWidth=1` tile ⇒ ±16 px), *but only applied when the jittered target still lies in the pixel's plane*; this spatially distributes probe-to-probe differences and widens TAA's 3×3 neighborhood clamp acceptance ⇒ temporal stabilization [S21 s39, CVar544]. Newer engine versions add stochastic probe-selection interpolation (`StochasticInterpolation`) and optional half-res integrate (`IntegrateDownsampleFactor=2`) [Docs-Perf].
- **Short-range AO / bent normal**: downsampled tracing loses contact shadows ⇒ full-res screen-space bent normal, trace length tied to probe spacing (~16 px); combined with probe GI via **Horizon-Based Indirect Lighting** [Mayaux 2018] heuristics: probe GI = far-field irradiance, bent normal = near-field visibility with the paper's multibounce approximation [S21 s93-95]. Applied **after** the temporal filter ⇒ zero lag [S22 p167].
- Validation: with rays/resolution cranked, the pipeline matches the path tracer [S21 s40].

---

## 5. World-space radiance cache

Purpose: distant lighting is high-noise for screen probes (a small bright window's solid angle shrinks with distance), slow to trace (long incoherent rays), but slowly-varying — a caching opportunity; also the importance-sampling fallback and the source of stable error ("world-space error is stable and easy to hide, like volumetric lightmaps") [S21 s66-67, McLaren *Tomorrow Children* lineage].

- **Placement**: sparse — probes exist only where screen probes will interpolate them. Marking pass writes into **3D clipmap indirection grids** centered on the camera. Defaults [CVar544]: **4 clipmaps** (`RadianceCache.NumClipmaps=4`), **first clipmap world extent 2500 UU = 25 m** (`ClipmapWorldExtent=2500`), **48³ placement grid per clipmap** (`GridResolution=48`) ⇒ probe spacing ~1 m in clipmap 0, doubling per clipmap (~25/50/100/200 m range). Clipmap distribution maintains bounded screen-space probe density [S21 s75].
- **Probe payload**: octahedral **32×32 radiance + trace distance** (`ProbeResolution=32`), i.e. 16× the directional texel count of an 8×8 screen probe [S21 s76, S22 p161]; radiance prefiltered into mips (used by translucency; mips reduce aliasing when 16 world rays back 1 froxel ray) [S22 p173]. Probes live in a fixed atlas with **persistent allocations + free list** [S22 p162].
- **Caching protocol**: (1) carry over probes still marked this frame; (2) trace probes newly revealed by camera/scene motion; (3) **re-trace a fixed-size subset chosen by a GPU priority queue to propagate lighting changes** — fixed cost, variable lighting latency [S22 p163]. Budget: **300 probes traced/frame** default, 1000 at cinematic (`NumProbesToTraceBudget`) [CVar544]. Overflow policy: cache-miss traces above budget run at reduced resolution; lighting-update traces are skipped entirely until under budget — hitch-free by construction [S21 s79].
- **Importance sampling**: no incoming-lighting estimate here; BRDF-only, at **trace-tile granularity** (can't sort 1024 rays/probe): accumulate BRDF from dependent screen probes, dice the probe into tiles, allocate tile resolution ∝ BRDF, supersample near camera — up to **64×64 effective = 4096 traces/probe** ⇒ extremely stable distant lighting [S21 s80]. (Matrix night mode — city lit purely by emissive bulb meshes — is the stress test this passes [S22 p164].)
- **Ray connection math** (the leak-prone part):
  - World probe rays **start beyond the interpolation footprint** (self-lighting exclusion); screen probe rays must cover footprint + skipped distance [S21 s71-72].
  - The origin gap between screen ray and world ray causes parallax leaks; fix = **simple sphere parallax**: intersect the screen-probe ray with the world-probe sphere and use the octahedral texel at that intersection (accept directional error to eliminate the positional gap) [S21 s73-74].
- **Spatial filter between world probes**: neighbors may be across a wall (no mutual visibility assumption); ideal = re-march neighbor ray through own depth map; shipped = **single occlusion test of a point along the neighbor ray using stored probe hit-depths** — nearly free, kills most leaks [S21 s81-82].
- Also consumed by: screen-probe IS guidance, hair, translucency forward shading, multi-bounce quality, glossy reflection fallback [S21 s85]. The **translucency/fog volumetric gather** has its own overlapped instance: froxel probes at 3×3/4×4 with HZB visibility skips, spatial+temporal filter, pre-integrated **SH2 irradiance**, backed by a 16×16-directional world radiance cache; ~1/8 of the opaque budget; its dispatches overlap the opaque WRC ⇒ "almost free" [S22 p171-173].

---

## 6. Temporal stability machinery

- **What is jittered**: screen-probe placement grid (per frame), octahedral ray directions (per frame), full-res interpolation offset (plane-constrained, ±1 tile), radiosity probe placement + directions (4-frame cycle). GSDF/voxel updates are cached, not jittered.
- **Full-res temporal filter**: plain exponential accumulation with **depth-based history rejection — relative depth threshold 0.005** (`Temporal.DistanceThreshold=0.005`; optional normal-based rejection cvar exists), **max 10 accumulated frames** (`Temporal.MaxFramesAccumulated=10` ⇒ steady-state blend ~0.1) [CVar544]. Explicitly **no neighborhood clamp** — clamping fights probe jitter and flickers; depth rejection gives stability at the cost of lighting-change latency [S21 s98, S22 p166].
- **Fast-lighting-update heuristic** (anti-ghosting): during tracing, store **hit velocity next to hit depth**; per probe compute the projected-area fraction belonging to fast-moving objects; when a pixel's interpolated lighting is **>10% from fast movers** (`FractionOfLightingMovingForFastUpdateMode=0.1`), switch that pixel to fast-update mode: drop temporal filter strength, raise spatial filter strength [S21 s99-100, S22 p167, CVar544].
- **Zero-lag near field**: short-range AO applied post-temporal-filter [S22 p167].
- **Stability by construction**: world-space probe error is static under camera rotation (probes and directions are world-anchored; only the *selection* changes); SH integration is smooth; the jittered interpolation feeds TAA/TSR a wider neighborhood so the clamp doesn't eat the signal [S21 s39, s67]. Camera rotation costs nothing to the WRC (probes persist); translation causes incremental probe allocation bounded by the 300-probe budget.
- **History invalidation** in reflections: temporal accumulation + neighborhood clamp per tile (tile borders explicitly cleared by the spatial-reuse pass to avoid reading uninitialized neighbors) and **double-strength bilateral filter forced on disocclusion areas** [S22 p181, p187].
- Surface-cache side: direct lighting reacts in ~`UpdateFactor`-frames; radiosity ghosting bounded by the 4-frame accumulation cap [S22 p78].

---

## 7. Performance budgets (all PS5, 1080p internal, TSR→4K unless noted)

**Quality tiers** [S22 p192, Docs-Perf]: High = 60 fps target, **Global SDF only, 1/16 rpp final gather, 1/4 rpp reflections, ~4 ms total GI+reflections budget**; Epic = 30 fps, **mesh-SDF detail tracing on, 1/4 rpp gather, 1 rpp (full-res) reflections, ~8 ms budget**. Each scalability tier ≈ half the cost of the one above [Docs-Perf].

| Scene (PS5) | Config | Surface cache | Final gather | Reflections | Total |
|---|---|---|---|---|---|
| Land of Nanite | SWRT High / Epic | 0.47 / 0.93 | 2.07 / 3.46 | 0.23 / 0.24 | **2.77 / 4.63 ms** |
| Lyra | SWRT High / Epic | 0.36 / 0.66 | 1.65 / 3.30 | 2.28 / 4.74 | **4.29 / 8.70 ms** |
| Matrix Awakens | HWRT+FarField High / Epic | 1.03 / 1.17 | 3.05 / 5.52 | 2.30 / 4.62 | **6.38 / 11.31 ms** |
| Lake House (2080 Ti) | HWRT Epic / 1 rpp ArchVis | 1.68 / 1.68 | 4.01 / 8.01 | 1.56 / 1.56 | **7.25 / 11.25 ms** |

[S22 p193-196]

**Final gather internal breakdown** (PS5, 1080p, 1/2 rpp, total **3.74 ms**) [S21 s102]: place probes 0.13 / generate rays 0.35 / **trace 1.07** / probe-space filter 0.24 / world radiance cache 0.53 / interpolate+integrate 0.62 / temporal filter 0.32 / bent normal 0.39. Scaled down (caches at 1/32 & 1/128 res, bent normal off) ⇒ **1/8 rpp = 2.15 ms** [S21 s103]. Scaled up: 2 rpp = 4.23 ms on 2080 Ti [S21 s110].

**Tracing method comparison (final-gather tracing / reflection tracing, PS5 1080p)** [S22 p134-136]: Land of Nanite (≈100 overlapping meshes per surface point): GSDF 0.94/0.04 vs HWRT **10.03**/0.46 ms — the kitbash pathology; Lyra: 1.21/1.58 vs 1.41/2.03; Matrix: SWRT 1.83/1.76, HWRT 1.72/1.79, HWRT+FarField 2.13/2.52. Hit-lighting reflections in Matrix: **11.54 ms vs 2.44 ms** surface-cache shading [S22 p108]. Cost/accuracy ordering: linear-screen < HZB-screen < GSDF < MDF < HWRT+cache < HWRT+hit-lighting [S22 p132].

**Other bounded budgets**: card capture 512×512 texels/f; direct lighting 1024² texels/f; radiosity 512² texels/f (<1/16 Lumen budget); WRC 300 probes/f; MDF pool 320 MB; surface cache attribute atlases 88 MB + lighting atlases. Rough-reflection probe reuse: −50–70% reflection cost; glossy WRC shortening: −16% further (Matrix roads) [S22 p183-184]. Async compute available for scene lighting / diffuse / reflections [Docs-Perf]. 1080p+TSR beats native 4K + low settings on final quality [S22 p192].

---

## 8. Known artifacts and mitigations (catalog)

| Artifact | Cause | Mitigation |
|---|---|---|
| Light leak through thin walls | sub-voxel SDF walls never cross zero | half-voxel-diagonal runtime expand + t-linear ramp; ≥10 cm wall guidance; per-mesh SDF resolution scale [S22 p48-50, Docs-Tech] |
| Lost contact shadows | expand bias | t=0 zero-expand ramp; reflection-specific min(expand, distToSurface) [S22 p50-51] |
| Foliage over-occlusion | expand on leaves | coverage channel: reduced expand, larger steps, stochastic transparency, extra bias [S22 p53] |
| Interior texel leaks (cache bilinear) | texels inside geometry | backface hits return black; depth-delta + normal weights + invalid-texel discard at sampling [S22 p70, p73] |
| Missing surface-cache coverage (trees, >N layers) | card parameterization limit | energy loss instead of leak (deliberate design); pink debug view [S22 p7, p83] |
| Probe interpolation leak across depth | probe on other plane | plane-distance weighting; adaptive placement where interpolation fails; plane-constrained jitter [S21 s34-39] |
| Spatial-filter leak over contact shadow | distant hits pass angle test | clamp neighbor hitT to own before reprojection [S21 s61] |
| World-probe leak (parallax gap) | separate ray origins | sphere parallax reprojection; footprint skip/cover [S21 s71-74] |
| World-probe neighbor leak across wall | no mutual visibility | one occlusion test via stored probe depths [S21 s82] |
| Ghosting behind movers | depth-rejection temporal filter | hit-velocity tracking → fast-update mode at >10% moving lighting [S21 s99-100] |
| Boiling/flicker under rotation | jittered probes/rays | world-space octahedral directions, depth-not-clamp history, 10-frame accumulation, plane-constrained interpolation jitter widening TAA clamp; stable-error WRC for distant light [S21 s37-39, s67] |
| Fireflies | small bright emissive, no explicit sampling | MaxRayIntensity=40 clamp; WRC 4096-effective-ray supersampling; reflections tonemapped-weight bilateral; emissives remain noise-limited (future work: explicit emissive sampling) [CVar544, S21 s80, S22 p181, p197] |
| Offscreen/distant energy loss | 200 m SWRT range, screen-trace-only beyond | HWRT far-field (HLOD1, 200 m→1 km, ray mask, translated TLAS trick) [S22 p118-127] |
| Self-intersection (Nanite fallback vs GBuffer) | LOD mismatch | screen traces first + short backface-ignoring epsilon ray then long ray [S22 p112-117] |
| Hitches on fast camera | probe cache misses | fixed WRC trace budget with degraded-resolution overflow; surface cache priority queues [S21 s79, S22 p74] |
| View-dependent GI (screen traces) | screen-space reuse | acknowledged tradeoff; screen traces optional [Docs-Tech] |

---

## 9. Why screen probes instead of world-space-only probes (DDGI-style)

Epic's stated rationale, consistent across both talks [S21 s16-20, S22 p143-146]:

**Irradiance-field failures** (DDGI class): (1) **irradiance is computed at the probe, not the pixel** — the interpolated result is wrong wherever visibility differs between probe and pixel ⇒ leaking *and* over-occlusion, and probe placement becomes a hand-tuning problem; (2) irradiance near occluders is **higher-frequency than any affordable spatial probe density** ⇒ the "distinctive flat look"; (3) volumetric storage caps spatial resolution; (4) implementations hide artifacts behind **slow lighting updates**. Screen-space-denoiser failures (other extreme): fixed 1-rpp cosine sampling can't adapt to where noise actually is (noise grows as bright features shrink with distance — the denoiser runs *after* sampling and can only blur); full-res screen-space filtering is expensive; disocclusion starves history [S21 s18-20].

**Screen probes are the deliberate midpoint** — "the accuracy of tracing from pixels with significantly less cost" [S22 p145]: probes *are* pixels (GBuffer position/normal ⇒ no placement problem, no probe-to-pixel visibility gap); the cache stores **radiance, not irradiance**, so integration happens per-pixel against the true BRDF/normal at full res; downsampling 16× buys a threadgroup per probe ⇒ **product importance sampling** (offline-quality sampling in real time) and probe-space filtering (3² ≈ 48² screen); interpolation failure is detected and answered with **adaptive probe placement** rather than leaking; directional indexing (shared world-space octahedral parameterization) makes temporal reprojection, spatial filtering and history-guided IS all O(1) lookups. Where world probes *are* the right tool — slowly-varying distant radiance — Lumen still uses them (the WRC), because "world space error is stable and easy to hide" while screen-space error moves with the camera [S21 s67]. The screen probe cache is view-adaptive by construction (probe density tracks screen content, cost independent of world size), at the price of needing the temporal filter to cover jitter and the WRC to cover offscreen/disocclusion — which is exactly the architecture: each range of the light path assigned to the cache that can afford its latency [S22 p168-169].

---

**Reimplementation notes for an SM5.0 compute target** (synthesis): the only SM6-bound pieces are waveop prefix sums (ray compaction, feedback merge) — replaceable with groupshared scans at ~the cost Epic quotes for "local atomics" compaction (still within 21% of optimal) [S22 p29-30]; DXR-dependent pieces are entirely optional (SWRT path is complete); the critical correctness inventory is: SDF expand/bias trio, backface-black cache texels, hitT-clamped spatial filter, sphere-parallax WRC connection, plane-constrained jitter, depth-rejection temporal with velocity-driven fast path, and the fixed-budget priority queues on every cached representation.