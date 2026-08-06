All research is complete. I have full primary-source captures: both DDGI JCGT papers (full text), RTXGI SDK shaders and headers (verbatim), Godot SDFGI source + integrate shader (verbatim), the AMD GI-1.0 paper (full text), the SEED Surfel GI speaker notes (full deck), the Radiance Cascades paper (LaTeX source), Lumen SIGGRAPH 2022 notes, and canonical LPV/VXGI sources. Here is the report.

---

# World-Space Probe GI for Real-Time Rendering: A Technical Literature Survey

Scope: algorithmic detail for a from-scratch **SM 5.0, no-HWRT** implementation whose top priority is **temporal stability under camera motion/rotation**. All constants below come from the cited primary sources (papers, SDK source, engine source).

---

## 1. DDGI — Majercik et al. 2019 (JCGT 8/2) + "Scaling DDGI for Production" 2021 (JCGT 10/2)

Sources: [2019 paper](https://jcgt.org/published/0008/02/01/), [2021 scaling paper](https://jcgt.org/published/0010/02/01/) ([arXiv mirror](https://arxiv.org/abs/2009.10796)).

### 1.1 Data layout
- Probes at vertices of an **axis-uniform 3D grid**; power-of-two per-axis counts (bitwise indexing); per-axis spacing may differ. Recommended: at least one full 8-probe cage per room; **1–2 m spacing** at human scale.
- Per probe, two octahedral maps (Cigolle et al. 2014 mapping), packed in a 2D atlas:
  - **Irradiance: 8×8 texels, `R11G11B10F`** (paper found `RGB10A2` a good quality/size balance; `RGB16F` is reference; `R11G11B10F` saves 45% vs 16F; `RGB5A1` breaks — needs hysteresis 0.8 and flickers).
  - **Mean distance + mean squared distance: 16×16 texels, `RG16F`.** (Explicit note: previous light-field probes needed 128×128×6 high-precision cube maps; the visibility-weighted interpolant lets 16×16 medium precision suffice.)
- **1-texel gutter border** around each probe, filled by copying the diagonally/mirror-corresponding interior texels so HW bilinear never crosses probes; probes aligned to 4×4 write boundaries. The 2021 paper's Figure 9 gives the exact border-copy index map; the RTXGI `UpdateBorderTexel` code (below) is the reference implementation.

### 1.2 Ray allocation and probe update (the exact math)
Each frame, for each active probe: `n` rays in a **spherical-Fibonacci pattern, stochastically rotated each frame**, all `m·n` rays batched in one dispatch; hits shaded with the same deferred-shading routine as screen pixels (probes sample last frame's probes → free infinite bounce with one-frame-per-bounce lag).

**Irradiance update, Eq. (1) of both papers:**

```
E'[n̂] = α·E[n̂] + (1−α) · Σ_{rays ω} max(0, n̂·ω) · L(ω)
```

- `α` = **hysteresis**, set **0.85–0.98** in the 2019 paper (0.95 typical; RTXGI default **0.97**).
- Distance and distance² blend identically but weight with a **power-cosine** (`pow(max(0,n̂·ω), k)` — "depth sharpening"; RTXGI `probeDistanceExponent = 50`). Texels with lobe weight < **0.001** are not updated.
- RTXGI normalizes by `1/(2·Σweights)` instead of `1/N` (variance reduction; the factor ½ compensates the cosine-sum normalization, and the sampler multiplies by `2π` at the end to complete the Monte Carlo estimator).
- Rays: 2019 benchmarks 32–256/probe (used **64 rays/probe with 32×8×32 probes** for its largest scenes at ~1.5–1.65 GRays/s on RTX 2080 Ti); RTXGI default **256**.

**Perceptual ("gamma 5") encoding** (2021, §4.2): store `pow(sum, 1/5)`; on sample, decode with `pow(texel, 5·0.5)`, do the weighted sum, then **square** the result ("leave a gamma = 2 curve to approximate sRGB blending for the trilinear"). Exponent 5.0 determined experimentally — lower converges slower, higher no faster. Benefits: perceptually-linear light→dark transitions and low-frequency firefly suppression.

**Fast-convergence heuristics** (2021, §4.3) — irradiance only, never visibility:
- Per-texel: change magnitude (max component of `newSum − old`) > **0.25** of max → `hysteresis −= 0.15`; > **0.80** → `hysteresis = 0` (distribution assumed completely changed).
- Per-event, applied to *all* probes: small lighting change (flashlight) → irradiance hysteresis −15% for 4 frames; large lighting change (time-of-day snap) → −50% for 10 frames; large geometry change → irradiance −50% for 10 frames *and* visibility −50% for 7 frames.
- If TAA runs on top, its hysteresis must be reduced in the same events or it re-adds the lag.

**Backface rule (core leak fix, 2021 §4.1):** rays hitting backfaces record **irradiance 0** and **shorten the recorded depth by 80%** (store `0.2·hitT`), so the probe sees interior surfaces as shadowed but doesn't nuke the Chebyshev weight to 0 (which normalization could re-inflate) nor skew average depth.

### 1.3 Shading query (verbatim weight chain, RTXGI [`Irradiance.hlsl`](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/rtxgi-sdk/shaders/ddgi/Irradiance.hlsl))
For the 8-probe cage around the (biased) shading point, per probe:

1. **Trilinear**: `trilinear = max(0.001, lerp(1−a, a, offset))`, `w_tri = tx·ty·tz` where `a = clamp((P_biased − P_baseProbe)/spacing, 0, 1)`.
2. **Wrap-shading backface term** (soft; deliberately not a hard cull so detail geometry doesn't reject all probes): `wrap = (dot(dirToProbe, n̂)+1)/2; w *= wrap² + 0.2`.
3. **Chebyshev visibility** (variance-shadow-map statistics on the *biased* point): sample filtered `(μ, μ2)` in direction probe→point (distance texture values ×2 due to blending normalization), `σ² = |μ² − μ2|`; if `r > μ`: `w_cheb = σ²/(σ² + (r−μ)²)`, then **contrast boost `w_cheb = w_cheb³`**, then `w *= max(0.05, w_cheb)` (never fully zero — fallback value needed when *no* probe has visibility).
4. Floor: `w = max(1e−6, w)`; **perception crush**: if `w < 0.2`: `w *= w²/0.2²` (kills near-zero contributions smoothly, motivated by logarithmic perception of dim light-leaks — the 2019 paper phrases this as reducing contributions below ~5% of representable intensity).
5. `w *= w_tri`; sample irradiance in direction `n̂`, decode gamma, accumulate; finally divide by `Σw`, square (linearize), `×2π`. (RTXGI adds ×1.0989 energy compensation when irradiance is stored `R10G10B10A2`.)

**Self-shadow bias** (2021, Eq. 2 — replaces the 2019 trio of scene-tuned mean/variance/Chebyshev biases):

```
bias = (0.2·n̂ + 0.8·ω̂_o) · (0.75 · minAxialProbeSpacing) · k,   k default 0.3
```

`ω̂_o` = direction to camera. RTXGI exposes it as `surfaceBias = n̂·normalBias + (−viewDir)·viewBias` with defaults **0.1/0.1** and an explicit warning that defaults are scene-scale-dependent. Raise it when ray counts drop (higher depth variance).

**Light-leak causes → fixes taxonomy:** probe inside wall (→ classification Off + relocation), query near visibility discontinuity (→ self-shadow bias moves query into lower-variance region), undersampled visibility (→ 16×16 depth texels + cos^50 sharpening), bilinear across wall (→ Chebyshev × wrap × crush), thin/zero-thickness walls (unsolvable by the statistics — content rule: walls thicker than a probe-spacing-dependent bound, single-sided geometry so backface test works).

### 1.4 Probe relocation (2021 §5 + RTXGI `ProbeRelocationCS.hlsl`)
Iterative optimizer (5 iterations at init; capped to avoid oscillation through tangent backfaces):
- If `backfaceCount/rays > 0.25` → assume inside geometry → move **through the closest backface** (offset += dir·(backDist + 0.5·minFrontfaceDistance) in RTXGI).
- Else if closest frontface < `probeMinFrontfaceDistance` (RTXGI default **1.0**) and closest/farthest frontface directions oppose (`dot ≤ 0`) → nudge toward farthest frontface, step `min(0.2, farthestDist)·dir` (paper) / `min(farthestDist,1)·dir` (SDK).
- Else drift offset back toward 0.
- Offset clamp: paper — never more than half min spacing per axis; RTXGI — ellipsoid `‖offset/spacing‖² < 0.45²` (0.2025). Backface hit distances are stored negated and ×0.2, so relocation rescales with ×(−5).

### 1.5 Probe states (2021 §6 — the six-state machine)
States: **Off, Sleeping, Newly Awake, Awake, Newly Vigilant, Vigilant**. Initialization (Listing 5): trace distance-only rays 5 frames while relocating; still-in-wall → **Off** (never trace/update); frontface within `probeSpacing` → **Newly Vigilant**; else **Sleeping**. Every frame, dynamic-object AABBs are **extended by one grid cell + the self-shadow bias**; Sleeping probes inside → **Newly Awake**. Newly-* probes optionally converge in one frame with hysteresis 0 (big ray batch), then become Awake/Vigilant, which trace every frame. Rules of thumb: a probe must be awake iff it shades (or is about to shade) a surface — camera visibility is irrelevant because probes propagate multi-bounce irradiance; participating media additionally require awake probes in empty space. Measured gain: **30–50%** of trace+update time.

### 1.6 Infinite scrolling volumes and cascades (2021 §7.3–7.4)
- **Tracking window**: probe grid conceptually camera-locked, implemented as a **3D fixed-length circular buffer with phase offset**; when the camera crosses one cell along an axis, the trailing probe plane "leapfrogs" to the front. Only that plane is cleared/reconverged; interior probes keep valid data (in RTXGI, `DDGIClearScrolledPlane` zeroes scrolled probes and the blend pass sets hysteresis to 0 for black probes; offsets reset when `scrollOffset % probeCount == 0`).
- **Multi-volume cascades**: weight per volume falls off linearly from 1→0 over the **last grid cell** on each axis (start at the second-to-last plane of probes); sample densest volume first, accumulate weights, stop at Σw = 1. RTXGI's `DDGIGetVolumeBlendWeight` is the same math.
- **Critical stability detail for camera-locked volumes**: naive blending pops when a plane leapfrogs. Fix: **tighten the transition region by one grid cell per axis and center it on the camera**, so newly-covered points fade in as the camera approaches rather than snapping the frame the plane arrives.
- Multiple volumes can be traced in one dispatch by packing all volumes' rays into one texture.

### 1.7 Timings (2019, RTX 2080 Ti, 32×8×32 probes × 64 rays, 8×8 `RGB10A2` + 16×16 `RG16F`)
Ray-gen 0.1 ms, cast 0.8 ms, shade 0.4 ms, probe update 0.7 ms (0.3 color + 0.4 depth), irradiance sampling in deferred shade 0.5 ms → **~2.6 ms** for the diffuse GI contribution (glossy mirror trace +2.4 ms); full-frame GI ~6 ms vs. ~1 min/frame path traced.

---

## 2. RTXGI SDK deltas vs. the papers

Source: [RTXGI-DDGI GitHub + DDGIVolume docs](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md), shaders under `rtxgi-sdk/shaders/ddgi/`.

- **Two states only** (ACTIVE/INACTIVE) instead of six. Classification uses **32 "fixed rays"** (`RTXGI_DDGI_NUM_FIXED_RAYS`, deterministic directions, never rotated, **excluded from radiance blending** to avoid bias; blending starts at ray index 32 when relocation/classification is on): Phase 1 — backface ratio over fixed rays > `probeFixedRayBackfaceThreshold` (**0.25**) → INACTIVE. Phase 2 — probe is ACTIVE only if some fixed-ray frontface hit lies **within the probe's voxel** (compared against ray/axis-plane intersection distances at ±spacing); otherwise INACTIVE. INACTIVE probes don't blend and contribute 0 to gathers.
- **Blending extras** (from `ProbeBlendingCS.hlsl`, verbatim behavior):
  - Random-ray backface culling: skipped per ray; if backfaces ≥ `probeRandomRayBackfaceThreshold` (**0.1**) × rays → abort the probe's whole blend this frame.
  - Distance clamp: `probeMaxRayDistance = 1.5 × length(probeSpacing)` during distance blending.
  - Hysteresis mods: previous texel exactly black → hysteresis 0 (instant seed, used by scroll-clear); `maxComponent(old−new) > probeIrradianceThreshold` (**0.25**) → `hysteresis −= 0.75`; `luminance(delta) > probeBrightnessThreshold` (**0.10**) → `delta *= 0.25` (clamps per-frame brightening).
  - **Minimum darkening step** `c_threshold = 1/1024`: when darkening, step at least the smallest 10-bit-representable increment so lerp quantization can't stall light→dark convergence in low-bit formats.
- **Probe variability** (convergence detection): per texel, coefficient of variation `cv = sqrt(luma(σ²))/luma(μ)` of the pre-lerp sample vs. means; reduced to a volume-wide average texture. It **never reaches zero** — the app watches it settle into a band and can pause tracing/blending for converged static volumes; any change (light/geometry) bumps it and resumes updates.
- Layout/limits: ray data as texture array (row = probe, column = ray, radiance + hitT); ≤ **16,384 probes per plane** (D3D12), ≤ 4 GB volume resource; per-frame ray rotation via James Arvo's random rotation (Graphics Gems 3); border texels updated in the same compute dispatch after an `AllMemoryBarrierWithGroupSync()`.
- Recommended content: probe spacing "every 2–3 meters", no zero-thickness walls, single-sided geometry.

---

## 3. Godot 4 SDFGI — fully software, SM5-class (the most relevant prior art for your constraints)

Sources: [announcement blog (Linietsky, June 2020)](https://godotengine.org/article/godot-40-gets-sdf-based-real-time-global-illumination/), [official SDFGI docs](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_sdfgi.html), [PR #39827](https://github.com/godotengine/godot/pull/39827), and engine source: [`servers/rendering/renderer_rd/environment/gi.h`](https://github.com/godotengine/godot/blob/master/servers/rendering/renderer_rd/environment/gi.h), [`.../shaders/environment/sdfgi_integrate.glsl`](https://github.com/godotengine/godot/blob/master/servers/rendering/renderer_rd/shaders/environment/sdfgi_integrate.glsl), `sdfgi_preprocess.glsl`, `sdfgi_direct_light.glsl`, `gi.glsl`.

### 3.1 Representation (hard numbers from `gi.h`)
- `MAX_CASCADES = 8` (default **6**, configurable 4–8 in docs; 4–6 typical), `CASCADE_SIZE = 128` → each cascade is a **128³ 8-bit SDF volume**; each cascade covers **2× the extent of the previous** (so 6 cascades = 64× the base cell size at the far end). `y_scale` option compresses Y (default **75%**) since scenes are usually flatter than wide.
- `PROBE_DIVISOR = 16` → light probes every 16 voxels → **9×9×9 probes per cascade** (odd, "encloses endpoints").
- Per cascade: `sdf_tex` (128³ R8), `light_tex` + `light_aniso_0_tex` + `light_aniso_1_tex` (**6-axis anisotropic radiance voxels**; albedo/emission stored at half size, 64³), a `SolidCell` GPU list `{position, albedo, static_light, static_light_aniso}` (u32×4) driving indirect dispatches, `MAX_DYNAMIC_LIGHTS = 128` per cascade, `MAX_STATIC_LIGHTS = 1024`.
- Probes: `LIGHTPROBE_OCT_SIZE = 6` → **6×6 octahedral irradiance per probe** stored with a 1-texel border in 8×8 blocks, format **RGBE9995 in an `r32ui` image**; a second layer set stores unconvolved **radiance** (same encoding) for rough reflections. Plus an `occlusion_texture` (4-bit per probe/octant region, `use_occlusion`) and an `ambient_texture` (per-probe L0 for volumetric fog).
- Tunables in source: `bounce_feedback = 0.5` (multi-bounce feedback loop; >0 enables multibounce, ~0.3–1.0 sensible, high values can cause feedback "infection" in bright closed rooms), `normal_bias = 1.1`, `probe_bias = 1.1`, `reads_sky = true`, `energy = 1.0`.

### 3.2 SDF construction (no RT anywhere)
Static-only meshes are **rasterized/voxelized** into albedo/emission/aniso voxels + solid bits (`render_albedo/emission/emission_aniso/geom_facing`), then the **SDF is built by jump flooding** (`jump_flood`/`jump_flood_half` ping-pong sets, then upscale) into the 128³ 8-bit distance texture. On cascade scroll, only **dirty regions** (the newly exposed slab, `dirty_regions`) are re-voxelized and re-flooded, amortized; this is why fast camera motion is the expensive case (docs explicitly warn cost "especially increases when the camera moves fast"). Dynamic objects **receive** GI but do not contribute occlusion/bounce (docs: "supports dynamic lights, but *not* dynamic occluders or dynamic emissive surfaces").

### 3.3 Direct light injection
Per solid cell (indirect dispatch over the `SolidCell` list): static lights are baked into `static_light` at build; **dynamic lights re-evaluated amortized over `frames_to_update_light`** (enum 1/2/4/8/16, default **4 frames** — "in 4 frames" project setting). Result written into the anisotropic light voxels. Light changes are therefore near-real-time even though geometry is latent.

### 3.4 Probe update — exact algorithm (`sdfgi_integrate.glsl`, MODE_PROCESS)
Per probe, per frame, `ray_count` rays (project setting 8–128, default **16**):
- Directions: **Vogel spiral** (`golden-angle`) over a hemisphere mirrored to a sphere, with a **per-probe hash offset** (stable per world position) and — key trick — **stratified across the history window**: ray `i` of frame with history index `h` uses direction index `h + i·history_size` out of `history_size × ray_count` total directions. So a 30-frame history at 16 rays/frame covers **480 unique directions** before repeating.
- Ray origin biased by `probe_bias` (1.1) in cell units; then **sphere-march the SDF**: `d = tex(sdf).r·255 − 1`; hit when `d < 0.05`; else `advance += d`. On leaving cascade bounds, the ray **continues into the next-coarser cascade** (up to `max_cascades`).
- On hit: normal from **SDF central differences** (ε = 0.001 in UVW); radiance = light voxel with anisotropy: `L = hit_light · (Σ max(0, n·aniso0) + Σ max(0, −n·aniso1))`; `alpha = 1`. On miss: sky irradiance octahedral map sampled at **mip 2** ("we don't usually throw a lot of rays, so this compensates") × sky energy, or flat sky color; `alpha = 0`.
- Accumulation: rays are projected into **16-coefficient (4-band) SH per probe** (RGB → 48 floats in shared memory), scaled `4/ray_count`.

**Temporal filter — a sliding-window box average, *not* an EMA.** Each frame's SH sample is quantized to fixed point (`value × 2^10`, clamped to int16) and written into a **ring-buffer history texture** (`rgba16i`, `history_size` layers; history size = the "frames to converge" setting, enum 5/10/15/20/25/30, default **30**), while a running-sum texture (`rgba32i`) is updated incrementally: `average += new − oldest`. Reconstruction divides by `history_size·2^10`. Consequences worth copying: **exact convergence in `history_size` frames, zero residual flicker for a static scene (the window contents become literally constant), no hysteresis tuning, and fixed response latency = window length.** Fireflies are bounded by the int16 clamp.

**Store pass (MODE_STORE):** SH → octahedral, "because octahedral is much faster to read from the screen than spherical harmonics, despite the very slight quality loss." For each 6×6 texel direction: evaluate SH basis, apply Lambertian convolution `l_mult = {1, 2/3·(3), 1/4·(5), 0·(7)}` (band 3 contributes nothing to irradiance) → **irradiance** map; also store unconvolved **radiance** map; both RGBE9995; write the 1-texel octahedral border with the standard edge/corner mirror copies (explicit index table in the shader). L0×0.88622 goes to the ambient (fog) texture.

### 3.5 Cascade scrolling (MODE_SCROLL / MODE_SCROLL_STORE)
When the camera crosses a probe-cell boundary, the cascade scrolls by integer probe steps: history and average textures are **copied shifted**; probes that scroll in from the edge are **seeded from the parent (coarser) cascade** by trilinearly interpolating its SH averages — and the seed is **replicated into every history slot** (so the box filter starts "pre-converged" at the parent's value and refines over the next `history_size` frames). At the outermost cascade the edge just clones existing data. This — plus per-cascade world-space anchoring of probes (probes never move continuously with the camera, only in discrete probe-size steps) — is SDFGI's core camera-motion-stability mechanism.

### 3.6 Gather & artifacts
Per pixel: blend the two cascades containing the point; 8-probe trilinear with octahedral lookup, weighted by the **occlusion texture** (per-octant visibility, `use_occlusion`) to stop inter-wall interpolation; `normal_bias` (1.1, in cells) offsets the sample along the normal against striping. Reflections: sphere-march the SDF from screen, shade with the radiance probes ("dynamic rough-reflection lightmap"; sharp reflections only on opaque).
Known artifacts (docs + blog): leaks when **walls are thinner than one voxel of the containing cascade**; visible cascade transitions as the camera moves; "splotches" at low ray count/short converge window; GI latency for distant modified geometry ("will be correct once the camera gets closer" — SDF only rebuilds on cascade scroll); Y-billboarding of probes at 75%/50% y_scale; Forward+ renderer only. Announced perf target: full 60 fps on a **GTX 1060** at introduction.

---

## 4. AMD GI-1.0 (Boissé et al., AMD TR 22-10-9831 / [arXiv 2310.19855](https://arxiv.org/abs/2310.19855), [PDF](https://gpuopen.com/download/GPUOpen2022_GI1_0.pdf), shipped in [Capsaicin](https://gpuopen.com/learn/gi-1-1-glossy-reflection-rendering/))

Two-level radiance caching at **¼ sample/pixel**; explicitly positioned against Lumen's SSRC: "temporally stable lighting **without needing an additional world-space [probe] structure**".

### 4.1 Screen cache (level 1)
- One **8×8-octahedral hemispherical probe per 8×8 screen tile**, placed on a **random (Halton-jittered) pixel of the tile**; probe grid stored in a render-target-sized 2D texture; hit distance in alpha. **Temporal upscale**: only ¼ of tiles spawn per frame; full population after 4 frames.
- **Reprojection** (Alg. 1): whole 8×8 lane group finds the best prior-frame probe: accept if `plane_dist = |dot(world_probe − world_p, n̂_p)| < cell_size` **and** `dot(n̂_probe, n̂_p) > 0.95`; tie-break by packed distance via atomic-min.
- **Adaptive `cell_size` heuristic** (Alg. 6) used everywhere (reprojection, guiding, filtering, interpolation): `cell_size = tan(fov_y · proj_size · max(1/h, h/w²)) · distance_to_camera`, `proj_size = 8` px — i.e. spatial-error tolerance grows linearly with view depth to trade detail for stability at distance.
- **Hole filling under motion** (Alg. 2): constant ray budget, but rays are *stolen* from tiles that succeeded reprojection (`override_tiles`) and given to disoccluded tiles (`empty_tiles`) by random atomic exchange.
- **Ray guiding**: reconstruct the hemisphere from reprojected probes in a **3×3 tile neighborhood with parallax correction** (using stored hit distance), build a CDF over the 8×8 cells in LDS, importance-sample cells; jitter within cell.
- **Radiance blending** (Alg. 3) — a plain EMA "led to a significant loss in visual fidelity"; instead a **biased shadow-preserving hysteresis** on luminances `l1 = mean(curr)`, `l2 = mean(prev)`:
  ```
  a = max(l1 − l2 − min(l1,l2), 0) / max(max(l1,l2), 1e−4)
  a = clamp(a, 0, 0.95)²            // clamp and remap
  out = lerp(curr, prev, a)
  ```
  i.e. history is only retained when the *new* estimate is a large brightening (suppresses fireflies/emissive flicker; darkening propagates instantly → deliberate darkening bias, shadows stay crisp). Cells that got **no rays** (guiding skew) are filled with the average of populated cells rather than left black (energy backfill).
- **Probe masking + mip search** (Algs. 4–5): 32-bit per-tile probe-position mask with a "first valid probe" mip chain enables sparse directional neighbor search; used for a **7×7 separable sparse probe-space blur** with `cell_size` plane rejection and **angle-error rejection** (`reject if dot(dir, normalize(hit_q − world_p)) < cos(π/50)`) to keep small-scale occlusion (same idea as Lumen).
- **Persistent LRU side cache**: probes evicted by thin-geometry flip-flop (fences, foliage) are pushed to a persistent queue (position+normal packed in 128 bits, radiance in a second grid-sized texture) and re-injected into reconstruction via a per-tile scatter list; MRU reordering each frame. Kills the "thin feature wobble" failure mode.

### 4.2 World cache (level 2) — hash grid, not probes
- Caches **outgoing radiance at secondary hit points** in a GPU hash map (Binder et al. 2019 lineage): descriptor = **quantized hit position (with distance-adaptive quantization level = radiance LODs) + quantized ray direction**; hash 1 → bucket, hash 2 → fingerprint, **linear probing** within bucket (hash functions from Jarzynski & Olano 2020). Per-cell **decay counter** reset on access; zero → deallocate.
- **Leak fix**: leaks appear when position *and* direction quantize equal across a thin wall, mainly when `ray_length < cell_size`; fix = hash the **boolean `(ray_length < cell_size)`** into the descriptor, splitting near-field and far-field events into different cells.
- **Two-level tiling for filtering**: cells grouped into **8×8 tiles with a mip chain**, tiles are **2D** (project along the dominant axis of the outgoing direction — local surfaces are planar at cell scale), linear memory layout; temporal EMA at mip 0, box-filtered mips; queries read the finest mip with enough samples. Cross-tile filtering is skipped — the screen cache hides the seams.
- **Multi-bounce**: reproject last frame's (direct+indirect) lighting at secondary vertices → "temporal radiance feedback" ≈ infinite bounces free.
- **Light sampling**: **world-space ReSTIR** — RIS+WRS reservoirs stored in a hash grid keyed by quantized position (normal in a side stream for bilateral thresholding); deviations for speed: **no initial visibility ray, exactly 1 shadow ray per reservoir after resampling, temporal-only reuse (no spatial passes)** — accepted extra darkening bias for latency; plus a **uniform light grid** (per-cell top-K light list, importance = trilinearly-interpolated corner estimate of ∭L dV with area-of-effect culling) to seed reservoir candidates.

### 4.3 Irradiance resolve
Probes projected to **3-band SH** (SH reprojection amortizes: only new probes are projected); per-pixel interpolation of the 4 neighbor probes, edge-aware in depth/normal, probes farther than `cell_size` off-plane get weight 0; if all weights die → **"relaxed interpolation"** (equal weights) flagged in alpha as a **denoiser hint**; pixel-position jitter (cancelled if it leaves the pixel plane) breaks probe-grid structure; denoiser = temporal accumulation + spatial filter with radius adaptive to history length. Short-range detail comes from a **bent-cone horizon-based SSGI** (1 slice/frame, product of bent cone and cosine lobe, small windowing against ringing). Total: **1.932–3.124 ms on RX 6900 XT**.

**vs. Lumen** ([SIGGRAPH 2022 course](https://advances.realtimerendering.com/s2022/index.html)): Lumen also gathers with 8×8 screen probes at 1/16 res (adaptive subdivision on detailed geometry), but its second level is a **world-space radiance cache of 32×32-octahedral probes in camera-centered clipmaps** (probes persist/re-used across frames; screen-probe rays are shortened and complete from the world probe on miss), plus a mesh-SDF/surface-cache tracing backend. GI-1.0 replaces the world *probe volume* with the sparse *hash cache of outgoing radiance* and relies on the screen probes for all angular detail — cheaper and unbounded in extent, but nothing exists where the camera hasn't looked, and offscreen lighting persistence is weaker than Lumen's world probes.

---

## 5. EA SEED — Global Illumination Based on Surfels (GIBS), SIGGRAPH 2021

Sources: [slides PDF (Advances course)](https://advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf), [SEED page](https://www.ea.com/seed/news/siggraph21-global-illumination-surfels), [talk video](https://www.youtube.com/watch?v=h1ocYFrtsM4). Lineage: PICA PICA (Stachowiak 2018); shipped later in Frostbite titles (e.g. College Football 25).

- **Surfel = {position, normal, radius}**, spawned **from the G-buffer**: screen split into **16×16-texel tiles**; each tile finds its least-covered texel; spawn if coverage passes a randomized threshold; stop at target coverage. Radius scaled for **~constant screen-space footprint** (shrink + spawn on approach, grow + recycle on retreat) → resolution-independent, constant quality and cost per screen area.
- **Attachment**: surfels store a transform ID (written to the G-buffer; skinned meshes write the highest-weight bone) + local position → follow rigid *and* skinned geometry (1-bone approximation).
- **Budget/recycling**: fixed pre-allocated pool; free-list stack maintained with GPU atomics; recycling probability from {live count, frames since last shading contribution, distance}, compared to a uniform random number.
- **Acceleration structure**: uniform grid near the camera + **trapezoidal grids along each principal axis** (slice thickness grows with distance — "projection-like"), so cells match the distance-scaled surfel radii; surfel radius ≤ cell side guaranteed; surfels inserted into all overlapped neighbor cells. Apply pass: per pixel, fetch the cell's first N surfels, weight by orientation+distance; if total weight < 1, top up with the **cell's average irradiance**.
- **Occlusion / leak fix (directly transplantable)**: per surfel a **4×4-texel hemispherical "radial depth function"** storing moving averages of **depth and depth²** (initialized to the surfel diameter, updated only by hits within the diameter), tested with **Chebyshev's inequality** — explicitly "inspired by Variance Shadow Maps and DDGI". Kills wall-bleed from large distant surfels.
- **Integration — Multiscale Mean Estimator**: long-term EMA whose **blend factor is driven by short-term mean + short-term variance estimators** (fast reaction when the short-term mean departs from history, near-frozen when converged; per Stachowiak's earlier writeup). Variance also drives **per-surfel ray allocation**: high variance/new surfels request many rays; converged surfels go "dormant" (only enough rays to detect change); global ray budget enforced by a count pass + proportional allocation pass. Rays are **binned/sorted** by (cell, direction) à la Battlefield V before tracing.
- **Ray guiding**: per-surfel **6×6-texel hemisphere radiance map (8-bit + one 16-bit scale, renormalized each iteration)**; sample by CDF walk (hierarchical 3×3 walk possible). **Irradiance sharing** between neighbor surfels (via the accel structure) when variance is high — the main blotch-noise killer (64-sample comparisons shown).
- **Many lights**: stochastic lightcuts (view-space Morton-ordered light BVH, cut of 2–8 nodes) vs. reservoir sampling (**4–8 candidates, 1 winner, 1 shadow ray**); reservoirs cheaper on console, lightcuts converge faster.
- **Transparents / non-G-buffer geometry fallback**: an **SH probe volume in 3–4 nested clipmap levels** following the camera (scroll + copy valid probes; new probes initialized by interpolating the parent level; per-level updates round-robin), same MSME integrator, **Sloan de-ringing windowing**; level transitions via border blending or **blue-noise dithered level selection** (1 volume sample instead of 2). Probe apply ≈ 0.2 ms at 900p.
- **Performance** (PS5, 4K output, screen passes 1080p, worst-case cold start): ~**7 ms** converging in a sun-lit open map; **~11 ms** in a 1400-light stress scene (which "converges in… about never" without importance sampling — many-light sampling was the open problem); filtering ~0.2 ms.
- **Pros vs probe grids** (their argument): compute/cache exactly on surfaces (no wasted volume probes, no probes in walls), resolution-independent, persistent across visibility (unlike screen-space), no placement/relocation problem. **Cons**: needs spawn-from-screen (offscreen/never-seen geometry has no surfels until viewed — hence the probe-clipmap fallback), unbounded-scene management complexity (recycling, non-linear grid), irradiance sharing needed to fight per-surfel variance, transparency needs a second system, and it leans on HWRT for the surfel rays (a software port would have to trace SDF/voxels instead).

---

## 6. Radiance Cascades (Sannikov, Grinding Gear Games / Path of Exile 2)

Sources: [paper (LaTeX/GitHub, Raikiri/Sannikov)](https://github.com/Raikiri/RadianceCascadesPaper), [radiance.wiki](https://radiance.wiki/), [tmpvar interactive PoC](https://tmpvar.com/poc/radiance-cascades/), [GM Shaders explainer with Sannikov](https://mini.gmshaders.com/p/radiance-cascades).

- **Penumbra condition**: to resolve a penumbra from the smallest light (size `w`, at distance `d` from an occluder), you need linear step `Δp <~ D` and angular step `Δω <~ 1/D` at distance `D`. The **penumbra hypothesis**: a discretization satisfying this is representative of the whole field (superposition-of-penumbras argument; spans the AO limit `Δp→0, Δω→∞` and the envmap limit `Δp→∞, Δω→0`).
- **Radiance interval** `L_{a,b}(p, ω)`: radiance reaching `p` from `ω` originating only within `t ∈ [a,b]` along the ray, plus a transparency interval `β_{a,b} ∈ {0,1}` (or continuous). **Merge rule** (the whole method in two lines):
  ```
  L_{a,c} = L_{a,b} + β_{a,b} · L_{b,c}
  β_{a,c} = β_{a,b} · β_{b,c}
  ```
  Full radiance = recursive merge of intervals with `t₀ = 0, t_N → ∞`. Also **interval extension**: `L_{a,2x+a}` can be built from `L_{a,a+x}` merged with a **shifted copy of itself** (`L_{a,a+x}(p + xω, ω)`) — so long ranges can be raymarched as a short range plus O(log) doublings (only for regular grids where the shifted probe exists).
- **Cascade scaling** (per cascade i): `Δp ∼ 2^i`, `Δω ∼ 2^(−i)`, interval `t_i ∼ 2^i`. Because data per cascade obeys Nyquist in both space and angle, **plain bilinear/trilinear interpolation is valid by construction — no disocclusion/leak heuristics needed** ("classic radiance probes require special handling of disocclusion exactly *because* they attempt to encode full radiance instead of radiance intervals").
- **Memory scaling**: flatland (2D pos + 1D dir): `M_i = M₀/2^i` → all cascades ≤ **2·M₀**. Full 3D (3D pos + 2D dir): probes ∼ 8^(−i), texels ∼ 4^i → again `M_i = M₀/2^i`, total 2·M₀. Surface fields (2D pos + 2D dir): `M_i = M₀` per cascade → `N·M₀`.
- **Diffuse resolve**: merge cascades N−1→0 down onto cascade 0 (each texel of cascade i merges 2–4 averaged texels of cascade i+1), then integrate cascade 0's few directions against the cosine lobe. Specular = cone queries: merging fewer upper cascades ⇔ narrower cone.
- **PoE2 production implementation**: **screen-space flatland-style cascades** populated by screen-space raymarching; marches **4 rays simultaneously** and stores occlusion as a **128-bit bitmask** (≈128 binary occlusion rays per cascade texel); runs in **~3 ms on a GTX 1050**. Example layout from the paper: cascade 0 = 32×32 probes × 8×8 angular texels, cascade 1 = 16×16 probes × 8×16, atlas width = 2× cascade 0. (Radiant 2d variant used 4× angular growth per cascade — constant memory per cascade — and empirically looked better; open question.)
- **Is it practical for 3D world-space GI?** The paper is candid: full-3D cascade 0 storage "is practically equivalent to voxelizing the entire scene — often a 'dealbreaker' for large-scale scenes"; but the hierarchy is "basically a drop-in improvement for any implementation that uses a uniform grid of radiance probes" (interval extension replaces long ray marches), and a hybrid — **screen-placed probes storing world-space intervals** (probe i: 2^i×2^i octahedral texels, 2^i px apart on the depth buffer, bilateral spatial + bilinear angular interpolation, SDF-marched intervals, per-cascade temporal reprojection) — captures offscreen light but ran 30–50 ms on an RTX 3060 in the author's demo (1–2 orders slower than the screen-space marcher). Degradation mode is graceful and unusual: undersampling turns sharp shadows into area-light-like penumbras rather than noise or leaks.

---

## 7. Classic fallbacks: VXGI and LPV

### Voxel cone tracing / VXGI
Sources: [Crassin et al. 2011, "Interactive Indirect Illumination Using Voxel Cone Tracing"](https://onlinelibrary.wiley.com/doi/abs/10.1111/j.1467-8659.2011.02063.x); Panteleev, [GTC 2014 "Practical Real-Time Voxel-Based GI"](https://cgvr.cs.uni-bremen.de/theses/finishedtheses/VoxelConeTracing/S4552-rt-voxel-based-global-illumination-gpus.pdf), [GDC 2015 VXGI deck](https://developer.download.nvidia.com/assets/events/GDC15/GEFORCE/VXGI_Dynamic_Global_Illumination_GDC15.pdf), [NVIDIA VXGI docs](https://docs.nvidia.com/gameworks/content/gameworkslibrary/visualfx/vxgi.htm).
- Crassin: sparse voxel **octree** with anisotropic (6-directional) prefiltered opacity+radiance mips; direct light injected from an RSM into leaves, mipmapped up; per-pixel gather = a handful of wide diffuse cones (+1 tight specular cone); a cone accumulates quadrilinearly-interpolated pre-integrated voxels front-to-back with `c += (1−c.a)·sample` while the sample LOD grows with cone radius.
- **VXGI (production)** replaced the octree with a **camera-centered 3D clipmap** (typically ~5 levels, 32³–128³ per level, finer LODs near the camera) of **opacity + emittance** voxels, revoxelized per frame (optionally only moving parts), because octree maintenance was the bottleneck.
- **Why it fell out of favor**: (1) leaks through any wall thinner than a voxel at that clip level — worse because cone accumulation *averages* opacity, so partial occlusion leaks; (2) flicker/aliasing when voxelization shifts (clipmap snap on camera motion — the exact instability you're trying to avoid; mitigations were temporal filters and snapping to voxel-sized steps); (3) large memory + full revoxelization cost every frame; (4) banding/over-occlusion from coarse cone quadrature; (5) view-dependent quality cliff at clipmap boundaries. Shipped rarely (The Tomorrow Children being the famous cascaded-VCT title) and was superseded by probe/RT hybrids.

### Light Propagation Volumes (Crytek)
Sources: [Kaplanyan, SIGGRAPH 2009 Advances course, "Light Propagation Volumes in CryEngine 3"](https://advances.realtimerendering.com/s2009/Light_Propagation_Volumes.pdf); [Kaplanyan & Dachsbacher, I3D 2010, "Cascaded Light Propagation Volumes for Real-Time Indirect Illumination"](https://doi.org/10.1145/1730804.1730821).
- Pipeline: render **RSMs** (reflective shadow maps) per light → convert each RSM texel to a VPL → **inject** as 2-band (4-coefficient) SH per color channel into a ~**32³** radiance grid (plus a **geometry volume** of SH occlusion built from RSM depth + camera depth) → **iteratively propagate** to the 6 face neighbors (≈8 iterations for 32³; each iteration re-projects flux through faces with occlusion attenuation from the GV) → sample the grid per pixel like an irradiance volume. Cascaded version nests ~3 camera-centered grids.
- **Failure modes** (why it's a fallback only): only 2-band SH → strong directional smearing and **self-illumination/bleeding** (light propagates through the wall the GV undersampled); range limited by iteration count (light travels 1 cell per iteration); single bounce; flicker when RSM sampling changes under light/camera motion; grid snapping needs the same "move in whole-cell steps" trick as every other camera-centered volume. Essentially zero cost by modern standards (fully SM5-friendly), decent for large-scale sun bounce, unusable for contact-quality occlusion.

---

## 8. Cross-cutting comparison and production combinations

| | **World probes** (DDGI/RTXGI, SDFGI, Lumen world cache) | **Screen probes** (Lumen SSRC, GI-1.0 screen cache) | **Surfels** (SEED GIBS) |
|---|---|---|---|
| **Leaking** | The failure mode; needs the full stack: Chebyshev visibility (16×16 depth, cos^50), self-shadow bias, backface rules, relocation/classification, occlusion bits (SDFGI). Residual leaks for sub-voxel/sub-spacing walls | Essentially leak-free at primary surfaces (probes lie *on* geometry — GI-1.0's stated reason for the design); leaks reappear at the secondary cache (GI-1.0's `ray_length < cell_size` hash-bit fix) | Leaks from large distant surfels through walls; fixed cheaply by 4×4 per-surfel depth+depth² Chebyshev masks |
| **Offscreen / never-seen geometry** | Fully handled — probes exist everywhere in the volume (the reason every production stack keeps a world structure) | Not represented; disocclusions must be re-solved (hole-filling ray reallocation, LRU side caches); offscreen emitters invisible unless a world level catches them | Only surfaces *ever seen* get surfels; offscreen persistence is good once spawned; brand-new views pay a spawn+converge cost; transparents/volumetrics need a probe fallback |
| **Reaction latency to light change** | 1 frame per bounce + hysteresis window; DDGI: α=0.97 EMA plus threshold-triggered hysteresis drops (−0.15 / →0 per texel; global event heuristics); SDFGI: direct light in ≤4 frames, indirect over the 30-frame box window | Fastest (per-frame re-trace of visible set + darkening-biased blend); GI-1.0 world cache decays/updates via temporal feedback | Variance-triggered: MSME detects mean shift, surfels up their ray budget within a few frames, then go dormant |
| **Camera motion/rotation stability** | **Best-in-class if done right**: probes anchored in world space; rotation costs *nothing*; translation costs one probe plane (scroll+clear+fast converge). Pitfalls: cascade-boundary popping (fix: tightened camera-centered blend region — DDGI 2021 §7.4), SDF/voxel rebuild spikes on fast motion (SDFGI) | Weakest: everything reprojects; disocclusion noise, thin-feature wobble (GI-1.0's LRU cache exists precisely for this), and the final image leans on TAA-style accumulation | Good: surfels are world-anchored once spawned; motion cost = spawn rate; screen-space spawn pattern itself is stable under rotation |
| **Memory** | Dense: probeCount × (8×8 + 2×16×16) texels — e.g. 32×16×32 probes ≈ 16K probes ≈ tens of MB; SDFGI ≈ 6 × (128³ SDF + 64³ light×3 + probes) | Small: a few render-target-sized textures + hash grid (sparse, capped, decays) | Medium, capped: fixed surfel pool + grid + 6×6 guiding + 4×4 depth per surfel |
| **Scalability to large worlds** | Scrolling volumes / clipmapped cascades (DDGI ISV, SDFGI cascades, Lumen clipmaps) — memory constant, latency at cascade edges | Free (screen-sized) — the world level is the scaling question | Constant screen-space density by construction; trapezoidal grid keeps lookups O(1) |
| **HW floor** | **SM5-capable**: rays can come from SDF (SDFGI), voxels, or any software tracer; DDGI's math is tracer-agnostic | SM5-capable if rays are software (Lumen SWRT mode does exactly this); GI-1.0 as published uses DXR 1.1 | Published implementation depends on HWRT for incoherent surfel rays; software port = SDF tracing per surfel |

**What production engines actually combine** (screen gather + world fallback is the consensus):
- **UE5 Lumen**: screen probes (8×8, 1/16 res, adaptive) → mesh-SDF/global-SDF software or HWRT rays → **world radiance cache** (32×32 octahedral probes, clipmaps, cross-frame reuse) for distant light + surface cache for hit lighting ([SIGGRAPH 2022](https://advances.realtimerendering.com/s2022/index.html)).
- **AMD GI-1.0**: screen probes + **hash-grid world cache** + world-space ReSTIR.
- **RTXGI/DDGI**: world probes *are* the gather (optionally + RT reflections); Unity/UE4 integrations per the 2021 paper.
- **Godot SDFGI**: world probe cascades + SDF tracing, screen-space is only an optional separate SSAO/SSIL.
- **SEED GIBS**: surfels for opaque + **SH probe clipmap** for everything else; slides list "combine with SSGI" as future work.
- **PoE2**: screen-space radiance cascades (with world-interval variants demonstrated but ~10× slower).

### Takeaways for a from-scratch SM 5.0 (no HWRT), camera-motion-stable implementation
1. **The Godot SDFGI template is the proven software path**: cascaded 128³ 8-bit JFA SDF as the ray backend; sphere-march cost is predictable and SM5-trivial. Its **sliding-window (ring-buffer sum) probe integrator** is the single most stability-relevant idea in this survey — deterministic convergence, literally zero steady-state flicker, no hysteresis tuning — at the price of a fixed `history_size` latency and `history × SH` memory. Combine it with DDGI's threshold heuristics only if you need faster light response (drop the window / reseed on large per-texel change, like RTXGI's black-probe → hysteresis 0 path).
2. **Anchor probes in world space and move them only in whole-cell steps** (DDGI ISV circular buffer / SDFGI scroll). Camera *rotation* must be a no-op by construction. On scroll, seed new probes from the parent cascade (SDFGI) or clear+converge with hysteresis 0 (RTXGI) — and **tighten and camera-center the cascade blend region by one cell** (DDGI 2021) or you will pop on every leapfrog.
3. **Copy the DDGI visibility stack wholesale** — it is the difference between leaking and not: 16×16 `RG16F` mean/mean² with cos^50 blending and 1.5×spacing clamp, Chebyshev with `w³` contrast + 0.05 floor + 0.2 crush, wrap-shading `(0.5(d+1))²+0.2`, self-shadow bias `(0.2n̂+0.8ω̂_o)·0.75·spacing·0.3`, backface radiance-0/depth×0.2, relocation with the 0.45-ellipsoid clamp, and gamma-5 perceptual blending.
4. For an SDF backend, backface classification is unavailable (no triangle winding) — use SDFGI's substitutes: **interior detection via SDF sign/occlusion bits**, per-octant occlusion probes for interpolation weighting, and content rules on wall thickness per cascade.
5. **Radiance cascades** are worth mining even if not adopted wholesale: storing *intervals* instead of full radiance makes interpolation leak-free by construction, and the interval-extension trick can replace long SDF marches in a uniform probe grid; full-3D cascades cost ≈ 2× cascade 0 memory — viable exactly when you already pay for a voxel/SDF volume.
6. The surfel ideas that transplant cheaply into a probe system: **variance-driven ray budgets** (dormant converged probes ≙ RTXGI variability pause), **MSME-style short-term/long-term estimators** as a principled alternative to threshold-triggered hysteresis, and 6×6 CDF **ray guiding** per probe.