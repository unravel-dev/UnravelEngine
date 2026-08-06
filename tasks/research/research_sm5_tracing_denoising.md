All research is gathered. Here is the complete report.

---

# Global Illumination on SM 5.0 (D3D11-class): A Literature Deep-Dive

Scope: compute shaders allowed; **no** hardware ray tracing, bindless, or guaranteed wave intrinsics. Sources are primary (papers, vendor slide decks, engine docs) with exact formulas quoted where published.

---

## 1. SDF Scene Representations for Software Ray Tracing

### 1.1 Per-mesh distance fields (MDF)

**Generation (UE5 Lumen, from the [SIGGRAPH 2022 Lumen slides](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf), pp. 34–35):**
- Generated **at mesh import** using an Embree *point query* for nearest-triangle distance. Sign is determined by casting **64 rays per voxel and counting back-face hits** (majority inside/outside vote). ~0.6 ms to build a 1.5 M-triangle mesh.
- **Narrow-band SDF**: only distances in **[−4, +4] voxels**, quantized to **1 byte**. This is the single most important storage decision — it makes 8-bit storage viable and bounds gradient error.
- **Resolution heuristic**: mip0 resolution is proportional to mesh bounds ("resolution based on mesh size and import settings"); each successive mip **halves resolution and doubles the max encoded object-space distance**. In UE this is exposed as a per-asset *Distance Field Resolution Scale* plus a project-wide voxel-density scalar ([UE5 Mesh Distance Fields docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-distance-fields-in-unreal-engine), [properties reference](https://dev.epicgames.com/documentation/unreal-engine/mesh-distance-fields-properties-in-unreal-engine)).
- **Two-sided/foliage meshes** get a *Two-Sided Distance Field Generation* build flag ([UE 4.27 DFAO docs](https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/LightingAndShadows/DistanceFieldAmbientOcclusion)).

**Storage:** a *virtual volume texture per mip*: page table + **sparse 8³ bricks with 0.5-texel border** in a page atlas. A GPU pass computes required mip per instance from camera distance; CPU streams pages in/out of a **fixed 320 MB brick pool with a linear allocator** (no 3D fragmentation). This is the SM5-friendly alternative to bindless: one page-table texture + one brick atlas texture.

**Mesh SDF tracing** (same deck, p. 36): ray-march stepping across mips (coarse mip for empty space, fine mip near surface — "sphere tracing with mip acceleration," after Aaltonen's Claybook). **Iteration cap 64; the 64th iteration is forced to be a hit** (accept over-occlusion rather than leak). Hit normal = SDF gradient from **6 taps at ±0.5 voxel**.

**Known SDF failure modes and the shipped fixes (Lumen deck pp. 47–54):**
- *Open meshes* → infinite negative region: during generation, **wrap negative distance after 4 voxels** (insert a virtual back surface).
- *Thin walls* thinner than a voxel never cross zero → leaks. Fix: **runtime "surface expand" by half the voxel diagonal**. Expand is applied at runtime so original data is preserved.
- *Contact shadows vs bias*: start rays at `t=0` with **zero expand, growing linearly with distance**; for shadow rays, fall the expand back to 0 near the light end. For **reflection rays** the linear ramp self-intersects at grazing angles, so instead per step: `expand = min(MaxExpand, distanceToSurface)` — always escapes the start surface, trading over-occlusion for slight leaking.
- *Foliage*: a per-instance **coverage channel** (from two-sided materials) resampled into the global SDF; coverage reduces expand, increases min step, and drives **stochastic transparency** at hits; animated foliage gets extra surface bias.

### 1.2 Global distance field (GDF) clipmaps

**Layout (Lumen, pp. 40–43):** **4 clipmaps of 256³**, each a sparse virtual volume texture of **8³ bricks (0.5-texel border, [−4,+4]-voxel narrow band, 1 byte)**, centered on the camera. Clipmap hierarchy (not mips) so distant scene is LOD-simplified — each clipmap has its own per-clipmap object-size cull threshold. UE4's earlier GDF similarly used 4 camera-centered clipmaps with partial updates ([UE docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-distance-fields-in-unreal-engine)); Godot SDFGI likewise uses camera-following cascades where "each further cascade doubles cell size," typically 4–8 cascades ([Godot article](https://godotengine.org/article/godot-40-gets-sdf-based-real-time-global-illumination/), [Godot docs](https://github.com/godotengine/godot-docs/blob/master/tutorials/3d/global_illumination/using_sdfgi.rst)).

**Compositing** = per-voxel `min()` over instance SDFs: loop culled objects per brick, take min distance from each mesh SDF/heightfield. **Non-uniform scale** breaks SDF metricity; the shipped fix is to clamp the sampled distance by the analytic distance to the instance's OBB.

**Incremental update strategy (the important part):**
- Track scene modifications → build **list of dirty bricks on GPU** → cull objects to clipmap → cull to dirty bricks (coarse bounds test, then *fine cull by actually sampling the mesh SDF*) → allocate/free bricks → update only those bricks.
- **Time-splice**: farther clipmaps update less often (they also contain the most instances, so this saves the most).
- **Static/dynamic separation**: static geometry cached in its own bricks; dynamic bricks composite static bricks + dynamic objects, so a moving car never forces re-merging the buildings around it.
- **Coarse occupancy mip**: a quarter-res *non-sparse* distance volume for empty-space skipping, rebuilt fully each update by sampling the sparse GDF then running **5 iterations of Eikonal propagation** (Aaltonen). Ray marching samples the continuous coarse mip every step and only touches sparse bricks near surfaces.
- Camera-motion handling in cascade systems is **scrolling**: Godot only regenerates a cascade when the camera crosses its region boundary; RTXGI's "infinite scrolling volume" scrolls the probe/brick grid like a tank tread — only the newly exposed edge planes are recomputed ([RTXGI DDGIVolume docs](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md)).

**Trace pipeline & ranges (Lumen pp. 44–46):** screen-space traces → **mesh-SDF detail traces limited to first 2 m** (objects culled to a frustum froxel grid, fine-culled by SDF sample, compacted lists per cell) → global-SDF traces to 200 m → **skylight on miss**. Shadow rays (parallel, full length) instead cull objects to a light-space 2D grid by rasterizing bounds.

### 1.3 Sphere tracing robustness

- **Baseline** ([Hart 1996] sphere tracing): step `t += SDF(p)`. Budget in shipped code: Lumen caps mesh-SDF marches at **64 iterations, forcing a hit at the cap**; typical hit epsilon scales with voxel size and distance (pixel-projected epsilon for primary rays).
- **Over-relaxation** ([Keinert et al., *Enhanced Sphere Tracing*, 2014](https://diglib.eg.org/items/8ea5fa60-fe2f-4fef-8fd0-3783cb3200f0); [summary](https://www.lgdv.tf.fau.de/publications/enhanced-sphere-tracing/)): step `t += ω·SDF(p)` with **ω ∈ [1, 2)**; the step is *safe* iff consecutive unbounding spheres overlap, i.e. `ω·r_prev ≤ r_prev + r_cur`; on failure, fall back to a plain step from the previous position. Later refinements fit a linear SDF model along the ray to pick the optimal step ([Bálint & Valasek 2018](https://people.inf.elte.hu/csabix/publications/articles/eurographics-2018-shortpaper.pdf)).
- **Cone vs ray**: for soft shadows / anti-aliased visibility, track the minimum angular occlusion along the march ([Quilez, *soft shadows in raymarched SDFs*](https://iquilezles.org/articles/rmshadows/)):
  - Basic: `res = min(res, k*h/t)` (`h` = SDF sample, `t` = distance along ray, `k` = hardness ≈ 8–128).
  - Improved (uses previous sample `ph` to estimate the true closest approach — removes banding; credited to Aaltonen):
    ```glsl
    y   = h*h / (2.0*ph);
    d   = sqrt(h*h - y*y);
    res = min(res, d / (w*max(0.0, t - y)));   // w = light angular size
    ```
  This same "min of h/t" trick is what Lumen/DFAO-style *cone occlusion* uses: a diffuse ray with an implicit cone footprint hits "softly," which both antialiases the SDF and lets short detail traces hand off to the coarse GDF once the cone footprint exceeds mesh-SDF resolution.
- **Self-intersection at start**: start bias along normal + ray direction of ~1 voxel; Lumen's linear-expand scheme (§1.1) is the refined version. For probe-style queries, DDGI's *self-shadow bias* (exact formula in §2.4) is the analogue.
- **Thin geometry**: narrow-band expand by half voxel diagonal (§1.1), or Godot's rule that walls must be **thicker than one voxel of the cascade** to avoid leaks ([Godot article](https://godotengine.org/article/godot-40-gets-sdf-based-real-time-global-illumination/)).
- **Empty-space skipping**: coarse non-sparse min-distance mip (Lumen), bit-occupancy bricks were tried and rejected as *slower* than distance bricks (Lumen p. 39 — they tested 1 bit/voxel in 8³ bricks and dropped it), mip-stepping (Claybook/Aaltonen).

### 1.4 Surface attributes at an SDF hit (no per-triangle data)

Three proven families:

| Approach | What it stores | Quality | Complexity | Sources |
|---|---|---|---|---|
| **Surface cache / cards (Lumen)** | Ortho-projected "cards" per mesh: albedo/normal/emissive/depth captured at runtime into a **4096² atlas of 128² pages**, BC-compressed on GPU; direct+indirect lighting cached per texel | High spatial detail; supports **multi-bounce via feedback**; mirror-capable near field | High: card placement (surfel clustering at import), GPU feedback, page management, capture budget | [Lumen 2022 deck](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf) pp. 57–84 |
| **Voxel radiance volume (Lumen's global fallback)** | **4 clipmaps of 64³ voxels, radiance per 6 axis directions + weight in alpha**; built by merging cards; geometry cached in a per-voxel *visibility buffer* (`24-bit mesh index | 8-bit hitT`, 6 rays/voxel, InterlockedMin), shaded every frame from the surface cache | Coarse (acknowledged "too coarse" limitation) but leak-resistant, cheap to sample at GDF hits | Medium | Lumen deck pp. 80–83 |
| **Voxelized albedo/emissive (SDFGI, SVOGI)** | Godot SDFGI: albedo/emissive voxelized per cascade alongside the SDF; lighting gathered into probes. CRYENGINE SVOGI: opacity/albedo voxels, cone-traced | Flat-ish, cascade-resolution bound; no per-material fidelity in bounce | Lowest; fully automatic, no authoring | [Godot article](https://godotengine.org/article/godot-40-gets-sdf-based-real-time-global-illumination/), [CRYENGINE SVOGI docs](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/25535599) |

Lumen's **card sampling** logic (deck p. 70) is worth copying exactly: mesh index → card grid → 6 axis-aligned cards per cell → pick 3 by hit normal → per card, `Gather4` the card depth, **reject texels where |storedDepth − hitDepth| is large** (occluded), weight by `cardNormal·hitNormal`, discard invalid (depth=MAX) texels, blend. Missing coverage **loses energy instead of leaking** — the key asymmetry that makes cards usable where voxel cone tracing was not (Lumen deck p. 27; also [Narkowicz, *Journey to Lumen*](https://knarkowicz.wordpress.com/2022/08/18/journey-to-lumen/) on why heightfield-traced cards and voxel cone tracing were discarded).

Surface-cache lighting update (pp. 73–78): fixed budgets — **1024² texels/frame direct, 512² indirect**; page priority = `LastUsed − LastUpdated` selected via GPU radix-sort histogram; direct lighting: 8×8 tiles, up to 8 lights/tile, 1-bit shadow mask from shadow maps + traced fallback rays; indirect: **one 4×4 hemispherical probe per 4×4 texel tile**, 4-frame jittered, interpolated with plane + hitT-visibility weights, temporally accumulated **max 4 frames**. Bounces n+2 come free via feedback (this frame's gather reads last frame's indirect).

---

## 2. Radiance Encodings

### 2.1 Octahedral maps — exact math

From [Cigolle, Donow, Evangelakos, Mara, McGuire, Meyer, *A Survey of Efficient Representations for Independent Unit Vectors*, JCGT 2014](https://jcgt.org/published/0003/02/01/) (Listings 1–2, verbatim):

```glsl
vec2 signNotZero(vec2 v) {
    return vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
}
// sphere -> octahedron -> [-1,1]^2
vec2 float32x3_to_oct(in vec3 v) {
    vec2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + abs(v.z)));
    return (v.z <= 0.0) ? ((1.0 - abs(p.yx)) * signNotZero(p)) : p;
}
vec3 oct_to_float32x3(vec2 e) {
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
    return normalize(v);
}
```

Hemi-octahedral (for hemispherical probes, e.g. surface-cache probes) — center diamond rotated to fill the square:

```glsl
vec2 float32x3_to_hemioct(vec3 v) {
    vec2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + v.z));
    return vec2(p.x + p.y, p.x - p.y);
}
vec3 hemioct_to_float32x3(vec2 e) {
    vec2 temp = vec2(e.x + e.y, e.x - e.y) * 0.5;
    vec3 v = vec3(temp, 1.0 - abs(temp.x) - abs(temp.y));
    return normalize(v);
}
```

For *storage-precise* snorm quantization use `float32x3_to_octn_precise` (floor + test all floor/ceil combos, keep highest cosine) — same paper, Listing 3.

**Border handling in atlases** ([DDGI paper, JCGT 2019](https://www.jcgt.org/published/0008/02/01/paper-lowres.pdf) Fig. 3; [RTXGI docs](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md)): each N×N probe gets a **1-texel gutter** so hardware bilinear never reads a neighboring probe. The octahedral wrap is a rotation, not a modulo: **edge border texels copy the interior texel mirrored across the edge (reversed order); the 4 corner texels copy the diagonally opposite interior corner.** DDGI additionally pads probes to 4×4 write alignment. Update border texels in the same compute pass that blends the probe interior.

**Resolutions in shipped/published systems**: DDGI: **8×8 irradiance (R11G11B10F), 16×16 depth+depth² (RG16F)**; Lumen: 8×8 screen probes, **32×32 world probes** (16:1 directional ratio); AMD GI-1.0: 8×8 screen probes ([GI-1.0 paper](https://gpuopen.com/download/publications/GPUOpen2022_GI1_0.pdf)).

### 2.2 Spherical harmonics: L1, L2, ringing, ZH3

- Irradiance from radiance SH is convolution with the clamped cosine; per-band scale factors `Â₀ = π, Â₁ = 2π/3, Â₂ = π/4` (Ramamoorthi & Hanrahan). L2 (9 coeff/channel, 27 floats) represents irradiance to ~1% error on smooth environments; **L1 (4 coeff/channel, 12 floats) can go negative and visibly flattens/desaturates**, especially opposite a dominant light.
- **Ringing**: strong point-like lights produce negative lobes; standard mitigation is windowing (Hann/Lanczos on band coefficients) — see [Sloan, *Stupid Spherical Harmonics Tricks*](https://www.ppsloan.org/publications/StupidSH36.pdf).
- **ZH3 hallucination** ([Roughton, Silvennoinen, Sloan et al., *ZH3: Quadratic Zonal Harmonics*, i3D 2024](https://www.activision.com/cdn/research/ZH3-Publication.pdf); [ACM](https://dl.acm.org/doi/10.1145/3651294); [slides](https://www.activision.com/cdn/research/ZH3-I3D-Presentation-Slides.pdf); [Unity-shader port](https://gist.github.com/pema99/f735ca33d1299abe0e143ee94fc61e73)): store only linear SH (12 floats) but reconstruct as if a quadratic *zonal* lobe existed:
  - Zonal axis per channel: `zonalAxis = normalize(vec3(sh[3], sh[1], sh[2]))` (i.e. the L1 vector), or one shared axis from **luminance-weighted L1** (`0.2126, 0.7152, 0.0722`) to avoid color fringes.
  - `ratio = |L1| / L0` (L1 magnitude projected on the axis over DC).
  - **Hallucinated coefficient (curve fit, §3.4.3): `zonalL2 = L0 · (0.08·ratio + 0.6·ratio²)`**.
  - Evaluation adds `0.25 · zonalL2 · sqrt(5/(16π)) · (3z² − 1)` with `z = dot(zonalAxis, dir)` on top of the standard L1 irradiance eval (the 0.25 is the cosine-kernel L2 zonal scale).
  - Result: ~L2-like irradiance quality at L1 cost (15 coefficients if the ZH3 term is stored explicitly, 12 if hallucinated). Best-paper runner-up i3D 2024.
- **HL2 ambient cube** (6 axis values) is the cheapest usable basis — shipped for probe irradiance in The Division ([Stefanov GDC 2016](http://mrakobes.com/Nikolay.Stefanov.GDC.2016.pdf)) and in Lumen's voxel radiance clipmaps (6 directions/voxel).

### 2.3 Ambient dice & spherical Gaussians

- **Ambient dice** ([Iwanicki & Sloan, EGSR 2017](https://www.ppsloan.org/publications/); used for specular in CoD:Infinite Warfare): values at the **12 vertices of an icosahedron**, reconstructed with per-vertex quadratic+quartic radial lobes (`w(d) = c₁(d·vᵢ)² + c₂(d·vᵢ)⁴`); evaluation touches the few vertices around the query direction. Less ringing than SH at similar cost; better suited to slightly-directional specular than SH.
- **Spherical Gaussians** ([Pettineo's SG series, part 2](https://therealmjp.github.io/posts/sg-series-part-2-spherical-gaussians-101/)):
  - Definition: **`G(v; μ, λ, a) = a · e^{λ(μ·v − 1)}`**.
  - Sphere integral: **`∫ G dv = 2π (a/λ)(1 − e^{−2λ})`** (drop `e^{−2λ}` for sharp lobes).
  - Product of two SGs is an SG: `λₘ = λ₁+λ₂`, `μₘ = (λ₁μ₁+λ₂μ₂)/λₘ` (plus amplitude decay).
  - Inner product (closed form): `∫G₁G₂ = 2π a₁a₂ (e^{dₘ−λₘ} − e^{−dₘ−λₘ})/dₘ`, `dₘ = ‖λ₁μ₁+λ₂μ₂‖`.
  - The Order: 1886 baked 5–9 SG lobes per lightmap texel and evaluated an anisotropic SG warp for specular (parts 3–6 of the series; Neubelt & Pettineo, SIGGRAPH 2015).

### 2.4 Which encoding where (recommendation grounded in the above)

- **(a) Per-probe distant radiance**: octahedral atlas texels (8×8 near / 32×32 far) — *raw radiance, not a basis* — because you need to importance-sample it and re-project it into any downstream basis; this is what Lumen and GI-1.0 do. Pair irradiance-style probes with **16×16 octahedral depth + depth² for Chebyshev visibility** (DDGI).
- **(b) Per-pixel final irradiance**: L1 SH + **ZH3 hallucination** (12 floats, near-L2 quality), or full L2 if bandwidth allows; ambient cube only for the cheapest tier. Lumen integrates screen probes to third-order SH per pixel before shading.
- **(c) Rough specular**: prefiltered octahedral radiance mips or direct GGX-importance-resampling of probe radiance (Lumen resamples the 8×8 screen probes for roughness 0.4–1.0, 32×32 world probes for 0.3–0.4 — deck pp. 183–184); SG/ambient-dice if you need a compact baked representation.

**Probe interpolation weights — exact DDGI math** ([JCGT 2019 paper](https://www.jcgt.org/published/0008/02/01/paper-lowres.pdf) §5.2; [production follow-up](https://arxiv.org/pdf/2009.10796)):
For each of the 8 cage probes: `w = w_trilinear · w_backface · w_visibility`, plus a perceptual dark-region suppression (fade contributions below ~5% of representable intensity), and epsilon-guarded normalization.
- Backface (smooth): `w_backface = (dot(dirToProbe, N)+1)²/4 + 0.2` style smooth wrap (soft, not binary).
- **Chebyshev visibility** (variance shadow map inequality, [Donnelly & Lauritzen 2006]): with `μ = E[r]`, `σ² = E[r²] − μ²` read from the 16×16 depth probe, and query distance `d`:
  `P(visible) = 1` if `d ≤ μ`, else **`σ² / (σ² + (d − μ)²)`**; RTXGI floors/cubes it (`max(w³, ~0.05)`) to sharpen contrast and avoid zero-weight singularities.
- **Self-shadow bias** (replaces three hand-tuned biases; [Majercik et al. 2021, Eq. 2](https://arxiv.org/pdf/2009.10796)):
  **`BiasVector = (0.2·n + 0.8·ω_o) · (0.75·D) · B`**, `D` = min axial probe spacing, `B` user scalar, default **0.3**. Add to the shading point before the visibility test.
- Probe update rays hitting **backfaces record 0 radiance and shorten stored depth by 80%** so probes inside walls go dark and read as occluded.
- **Probe relocation**: iteratively offset probes out of geometry, clamped to **45% of cell spacing**; **classification** into Off/Sleeping/Awake/Vigilant states cuts update cost ([RTXGI docs](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md), [arXiv 2009.10796](https://arxiv.org/pdf/2009.10796)).

**Probe temporal blend (hysteresis) — exact** ([DDGI Eq. 1](https://www.jcgt.org/published/0008/02/01/paper-lowres.pdf)):
`newTexel = lerp( Σ_rays max(0, texelDir·rayDir)·rayRadiance , oldTexel, α )` with **α ∈ [0.85, 0.98]**; depth texels use a cosine-power lobe (texels with lobe weight < 0.001 skipped). Production additions ([arXiv](https://arxiv.org/pdf/2009.10796) §4.2–4.3): store irradiance with **perceptual gamma 5.0** (`pow(E, 1/5)` on write, blend in encoded space — makes light→dark convergence perceptually linear and damps fireflies); per-texel hysteresis adaptation: **change > 25% of max → α −= 0.15; change > 80% → α = 0** (assume the lighting distribution changed completely).

---

## 3. Temporal Accumulation & Denoising at Low Sample Counts

### 3.1 Reprojection math

Standard geometry-based reprojection ([SVGF paper §4.1](https://research.nvidia.com/publication/2017-07_spatiotemporal-variance-guided-filtering-real-time-reconstruction-path-traced); preprint text extracted):
1. Motion vector per pixel from raster (`uv_prev = uv − motion`), or manual: `p_clip_prev = ViewProj_prev · float4(worldPos, 1)`.
2. **2×2 bilinear tap resampling of history, each tap individually validated** on backprojected depth, (object-space) normal, and mesh ID; invalid taps' weights are redistributed over valid taps; if all four fail, **retry with a 3×3 footprint** (rescues thin geometry/foliage); if that fails → disocclusion → drop history (`C′ = C`).
3. Depth validity should be *plane-based*, not absolute: compare against expected depth extrapolated with screen-space depth gradients (see `w_z` below), so slanted surfaces don't self-reject.
4. EMA accumulation: **`C′ᵢ = α·Cᵢ + (1−α)·C′ᵢ₋₁`, α = 0.2** in SVGF. For probe-space accumulation use the DDGI hysteresis form above (α = 1−0.02…0.15 equivalent).
5. Variance-guided blending: track per-pixel accumulated frame count `N`; effective `α = max(1/(N+1), α_min)` gives fast convergence after disocclusion and stability at steady state (standard practice; cf. [temporal AA survey notes](https://interplayoflight.wordpress.com/2020/05/30/a-survey-of-temporal-antialiasing-techniques-presentation-notes/)).

### 3.2 SVGF — exact equations ([Schied et al., HPG 2017](https://research.nvidia.com/publication/2017-07_spatiotemporal-variance-guided-filtering-real-time-reconstruction-path-traced))

- **Moments/variance**: accumulate first and second raw moments of luminance `μ₁, μ₂` with the same reprojection tests; **`σ′² = μ₂′ − μ₁′²`**. For pixels with **< 4 frames of history**, estimate variance *spatially* with a 7×7 depth/normal-driven bilateral filter instead.
- **À-trous wavelet**: 5-level edge-aware à-trous, **5×5 cross-bilateral kernel per level**, kernel taps `h = (1/16, 1/4, 3/8, 1/4, 1/16)` (outer product), holes of `2ⁱ−1` zeros per level → effective **65×65** footprint:
  `ĉᵢ₊₁(p) = Σ_q h(q)·w(p,q)·ĉᵢ(q) / Σ_q h(q)·w(p,q)`
  Variance propagates as `Var(ĉᵢ₊₁(p)) = Σ h²w²Var / (Σ hw)²` and steers the next level.
- **Edge-stopping weights**: `w = w_z · w_n · w_l` with **σ_z = 1, σ_n = 128, σ_l = 4** (fixed for all scenes):
  - Depth (local plane model via clip-depth screen gradients): **`w_z = exp( −|z(p) − z(q)| / (σ_z·|∇z(p)·(p−q)| + ε) )`**
  - Normal: **`w_n = max(0, n(p)·n(q))^σ_n`**
  - Luminance (variance-normalized; variance pre-blurred with a 3×3 Gaussian): **`w_l = exp( −|l(p) − l(q)| / (σ_l·sqrt(Gauss₃ₓ₃(Var(l(p)))) + ε) )`**
- The **output of the *first* wavelet iteration becomes the color history** for next frame's temporal pass (best stability/bias tradeoff).
- Filter *demodulated* irradiance (divide out albedo, re-modulate after) so texture detail isn't blurred.

**Lumen's alternative** (deck pp. 143–167): don't denoise full-res at all — gather in probe space where a **3×3 probe-space filter ≈ 48×48 screen-space filter**, then a full-res temporal filter with **depth+normal rejection only (no neighborhood clamp)**, accepting lighting lag; mitigations: speed up accumulation for pixels whose rays hit fast-moving objects, and apply the shortest-range occlusion (contact AO, ~16 pixels) *after* the temporal filter so it has zero latency. Latency budget is tiered by distance: contact AO (none) → screen probes (temporal accumulation) → world probes (whole-probe reuse + priority-queue retrace) → skylight (many frames).

### 3.3 ReSTIR GI on SM 5.0

[Ouyang et al., *ReSTIR GI: Path Resampling for Real-Time Path Tracing*, CGF 2021](https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.14378); background: [Wyman et al. SIGGRAPH 2023 course](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf).

- A reservoir per pixel stores one candidate **sample = {visible point x_v, sample point x_s, sample normal n_s, outgoing radiance L_o}** plus `{w_sum, M, W}`. Streaming update: `w_sum += w; M += 1; if (rand() < w/w_sum) keep sample;` with `w = p̂(x)/p(x)`, target `p̂` = luminance of the sample's unshadowed contribution. Final unbiased weight **`W = w_sum / (M · p̂(y))`**.
- **Nothing in this requires HWRT** — candidate generation is "one ray + radiance estimate," which an SDF trace + surface-cache/voxel shading provides. All the resampling is pure compute + textures: fully SM 5.0-expressible (reservoirs in structured buffers).
- **Feasible subset**: *temporal-only reuse* (reproject pixel → merge previous reservoir, clamping history `M ≤ ~20×` current to bound staleness) is cheap (one buffer read/write, a few ALU) and dramatically stabilizes small bright emitters — the exact weakness of uniform probe sampling. Spatial reuse adds the visibility-bias problem (neighbors' samples may be occluded from your point) — Ouyang handles it with either a visibility *re-trace* (expensive: extra SDF ray per reuse) or accepts darkening bias; the reconnection **Jacobian** for reusing neighbor q's sample at pixel r: `|J| = (cosφ_r/cosφ_q) · (‖x_q − x_s‖²/‖x_r − x_s‖²)`.
- Practical verdict for SM5: temporal reservoir reuse on half-res GI rays = good value; full spatial ReSTIR = usually not worth the extra visibility rays when an SDF trace costs as much as it does. Lumen chose product-importance-sampling from reprojected probe radiance instead (deck pp. 157–159), which solves the same problem without reservoir bias machinery — strongly recommended as the SM5 default.

### 3.4 Bilateral upsampling from half/quarter res — exact weights

[Kopf et al., *Joint Bilateral Upsampling*, SIGGRAPH 2007]: upsampled solution at full-res pixel p from low-res solution S at coarse pixels q↓:
`S̃_p = (1/k_p) Σ_{q↓} S_{q↓} · f(‖p↓ − q↓‖) · g(‖Ĩ_p − Ĩ_{q↓}‖)` — spatial Gaussian `f` times range weight `g` on the *full-res guide image*.
The GI-practical variant (guide = depth+normal G-buffer; used by DDGI sample code, SSAO/SSGI upsamplers): for the 4 bilinear-parent coarse texels,
`w_q = w_bilinear(q) · exp(−|z_hi − z_q| / (σ_z·|∇z|·Δ + ε)) · max(0, n_hi·n_q)^{σ_n}` , normalize; fall back to nearest-depth tap ("nearest-depth upsampling") when Σw ≈ 0. SVGF's `w_z`/`w_n` forms (§3.2) drop in directly as these weights. Poisson-disk / golden-spiral kernels are the standard *pre-upsample* spatial reuse patterns for half-res reflection/GI buffers ([Stachowiak, *Stochastic Screen-Space Reflections*, SIGGRAPH 2015](https://www.ea.com/frostbite/news/stochastic-screen-space-reflections) uses 4-sample spiral reuse weighted by `brdf/pdf` ratio).

### 3.5 Anti-flicker toolkit

- **YCoCg transform** (Karis, *High-Quality Temporal Supersampling*, SIGGRAPH 2014): `Y = x/4 + y/2 + z/4; Co = x/2 − z/2; Cg = −x/4 + y/2 − z/4`. Clamp/clip in YCoCg — the AABB is tighter and chroma ghosting dies first.
- **Variance clipping** (Salvi, *An Excursion in Temporal Supersampling*, GDC 2016; implementation documented in [Intel's TAA sample](https://github.com/GameTechDev/TAA) and the [TAA survey notes](https://interplayoflight.wordpress.com/2020/05/30/a-survey-of-temporal-antialiasing-techniques-presentation-notes/)): over the 3×3 neighborhood compute raw moments `m₁ = Σc/9`, `m₂ = Σc²/9`; `μ = m₁`, `σ = sqrt(max(0, m₂ − m₁²))`; **AABB = [μ − γσ, μ + γσ], γ ≈ 0.75–1.25** (smaller while moving, larger when still). *Clip* (not clamp) history toward the AABB center: `t = intersect(aabb, segment(center, history)); history = lerp(center, history, t)`.
- **Firefly suppression / luminance clamping**: tonemapped ("Karis") weighting during any resolve/average: `w = 1/(1 + luma(c))`, un-weight after (`Σw·c / Σw`). Lumen applies tonemapped weighting **only in the last-ditch bilateral filter**, not in the physically-based spatial reuse, to avoid crushing highlights (deck p. 181). Also clamp per-ray radiance before accumulation (fixed HDR cap) — every probe system does this.
- **Dual history (fast/slow)**: keep a short-α "fast" history (e.g. ~4-frame EMA) and a long-α "slow" history; detect lighting change by |fast − slow| (relative to σ) and either clamp slow to a fast-derived AABB or drop α of the slow buffer. This is the responsiveness mechanism of NVIDIA's ReLAX/NRD denoiser ([NRD GitHub](https://github.com/NVIDIAGameWorks/RayTracingDenoiser)) and maps 1:1 to SM5. DDGI's threshold-based hysteresis drop (§2.4) is the probe-space equivalent.

---

## 4. Specular / Rough Reflections from the Same GI Data

**Pipeline archetype** (Lumen deck pp. 175–187, built on [Stachowiak 2015](https://www.ea.com/frostbite/news/stochastic-screen-space-reflections)): GGX visible-lobe importance sample → ray trace (screen-space HZB march first, SDF fallback) → **spatial reuse** (neighbors' hits reweighted by *your* BRDF: `w = brdf_p(dir_q)/pdf_q`) → temporal accumulation (with neighborhood clamp) → **variance/disocclusion-gated bilateral filter** (double strength where history is absent, tonemapped weights) → TAA. Tile-classify and compact so sky/diffuse-reused pixels are skipped; HZB traversal is a stackless closest-HZB mip walk ([Uludag, GPU Pro 5]) with iteration caps for grazing rays.

**Roughness tiering (reuse the diffuse GI data!)** — the big SM5 win:
- roughness > ~0.4: **skip reflection rays entirely**; importance-sample GGX and interpolate radiance from the 8×8 screen probes (50–70% of reflection cost removed).
- ~0.3–0.4: trace **shortened** rays; on miss interpolate the 32×32 world-probe radiance (kills ray binning need; −16% more).
- < 0.3: full traces; SDFs are weak at true mirrors — accept surface-cache-resolution hits or gate mirror-quality behind SSR-only.

**SSR + probe/SDF fallback blending**: accumulate SSR hit confidence (ray march result × screen-edge fade × backface rejection), then `final = lerp(probe_or_SDF_fallback, ssr, confidence)` — the standard shipped pattern (Frostbite, CryEngine; see Stachowiak deck). Keep the fallback *the same GI probes* so the seam is only in sharpness, not brightness.

**Filtered importance sampling** ([Colbert & Křivánek, GPU Gems 3 ch. 20 / EGSR 2008](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling)): with N samples against a prefiltered environment/probe map of `w×h` texels, sample mip
**`lod = max(0, ½·log₂(Ω_s/Ω_p))`**, `Ω_s = 1/(N·p(ω))` (solid angle per sample), `Ω_p = 4π/(#texels)` (solid angle per texel). 8–32 samples at the right mip ≈ hundreds at mip 0. Works identically on octahedral probe atlases.

**Prefiltered radiance mips**: split-sum ([Karis, *Real Shading in UE4*, SIGGRAPH 2013](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf)): `∫L·f ≈ (prefiltered radiance at mip(roughness)) × envBRDF(NoV, roughness)` with the 2D BRDF LUT. Prefilter probe octahedral maps with GGX-distributed taps per mip.

**Contact hardening**: use ray **hit distance** to select filter footprint/mip: `coneWidth ≈ t·tan(θ_lobe)` → sharper near contact, blurrier far; SVGF-style specular denoisers and NRD use hit-T-driven kernels; with prefiltered probes, choose `lod ∝ log2(t · tanθ / texelFootprint)`. The SDF cone-occlusion term (§1.3) gives the same hardening for shadows.

---

## 5. Sky, Emissive, and Infinite Lighting

- **Sky at miss**: every surveyed system terminates its trace chain into the sky: Lumen's pipeline is literally `screen traces → mesh SDF → global SDF → skylight` (deck p. 46), with skylight updated over many frames (allowed latency tier, p. 169). DDGI probe rays that miss return environment radiance, so sky enters the irradiance field automatically. Practical detail: use a *prefiltered* sky (cubemap/octahedral mips or SH) at miss, not the analytic sky, so 1-ray estimates aren't fireflies; and gate sky contribution by trace transparency/coverage for foliage.
- **Sky visibility as a separate baked/cached term**: The Division stores per-probe **spherical sky-visibility** (per HL2 basis direction) and reconstructs `irradiance[dir] = sky_coeffs[dir]·sky_visibility[dir] + Σ brick_radiance·brick_weight[dir]` — sky changes (time of day, weather) are then free at runtime ([Stefanov GDC 2016 slides](http://mrakobes.com/Nikolay.Stefanov.GDC.2016.pdf), pp. 18, 38).
- **Emissive meshes**: Lumen captures emissive into the surface cache during card capture, so *any ray that hits the cache picks it up* — no explicit light sampling. This is noisy for small bright emitters; the Matrix Awakens night mode works because the **32×32 world probes** have enough directional resolution and are temporally stable (deck p. 164); "explicit sampling of emissive meshes" is listed as future work (p. ~200). Godot SDFGI merges emissive into the voxelized albedo cascade at bake, but **doesn't support dynamic emissive changes** ([Godot docs](https://github.com/godotengine/godot-docs/blob/master/tutorials/3d/global_illumination/using_sdfgi.rst)). CRYENGINE SVOGI injects emissive at voxelization ([docs](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/25535599)). Multi-bounce sky/emissive propagation comes from feedback: Lumen's n+2-bounce surface-cache loop; The Division's "use previous frame's irradiance of the closest probe per surfel"; Godot's `bounce_feedback` scalar (0.3–0.5 recommended, >0.5 risks feedback blowup).

---

## 6. Exposure / Pre-Exposure and Precision in GI Buffers

- **Pre-exposure** ([Lagarde & de Rousiers, *Moving Frostbite to PBR*, SIGGRAPH 2014 course](https://seblagarde.wordpress.com/2015/07/14/siggraph-2014-moving-frostbite-to-physically-based-rendering/)): multiply lighting by the (previous frame's) exposure **at the end of the lighting shader, before writing** to FP16 targets, instead of at tonemap time. FP16 max is 65504 with ~10-bit mantissa; physical sun-lit luminances overflow/quantize without this. For GI accumulation buffers the same applies: store `radiance × preExposure`, and when mixing history from a frame with different exposure, rescale by `exposure_now/exposure_prev` before blending — otherwise EMA states drift when exposure animates. (Probe caches that persist many frames should either store un-preexposed values in FP16 with a per-volume scale, or rescale on read.)
- **R11G11B10F tradeoffs** ([Wronski, *Small float formats – R11G11B10F precision*](https://bartwronski.com/2017/04/02/small-float-formats-r11g11b10f-precision/)): 5-bit exponents with 6/6/5-bit mantissas, no sign; half the memory of RGBA16F; error is ~2× (R/G) to ~4× (B) worse than FP16 → banding in dark blues and slow-accumulation shimmer. Verdict for GI: **R11G11B10 is fine for per-frame radiance/probe atlases (DDGI stores irradiance as R11G11B10F), but use RG(BA)16F for long-lived accumulation/history buffers and for moments/variance** (μ₂ underflows badly in small formats). DDGI's gamma-5 perceptual encoding (§2.4) doubles as a precision equalizer in small formats.
- **Chroma tricks**: [Mavridis & Papaioannou, *The Compact YCoCg Frame Buffer*, JCGT 2012](https://jcgt.org/published/0001/01/02/) — store Y every pixel, Co/Cg checkerboarded (2 channels total), reconstruct chroma with an edge-aware filter; applicable to GI history buffers where chroma noise is perceptually cheap. LogLuv-style encodings (log-luminance + chroma) are the older shipped variant. Keep variance/moments in luminance only (SVGF already does).

---

## 7. SM 5.0 / D3D11-Era Constraints and Shipped Precedents

### 7.1 API constraints

- **Typed UAV loads** ([MS docs](https://learn.microsoft.com/en-us/windows/win32/direct3d11/typed-unordered-access-view-loads), [DirectX-Specs](https://microsoft.github.io/DirectX-Specs/d3d/UAVTypedLoad.html)): base D3D11 guarantees typed UAV **loads** only for `R32_FLOAT/R32_UINT/R32_SINT`. On D3D11.3, a set of formats (incl. **RGBA16F**, RGBA8, R16F, R8, RGBA32F…) becomes available **as a set** if the cap is present; **R11G11B10_FLOAT is individually optional** — query per format via `CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2)` for `D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD`. Typed UAV *writes* to these formats are fine on base D3D11. Fallbacks when loads are unavailable: (a) alias the data as `R32_UINT` and pack/unpack manually (`f32tof16` pairs, or hand-rolled R11G11B10 pack); (b) `RW(ByteAddress|Structured)Buffer`; (c) ping-pong SRV-read/UAV-write. Design consequence: prefer **read-as-SRV, write-as-UAV** pass graphs (which every system above already uses) over read-modify-write on packed HDR formats.
- **Group shared memory**: 32 KB per thread group, ≤1024 threads/group in cs_5_0. GI-1.0 shows the pattern: keep an 8×8 octahedral probe **in LDS** for reprojection/filter passes ([GI-1.0 paper](https://gpuopen.com/download/publications/GPUOpen2022_GI1_0.pdf) §2.1).
- **No wave intrinsics** (SM 6.0+): replace ballots/prefix sums with LDS reductions + `GroupMemoryBarrierWithGroupSync`, LDS `InterlockedAdd` for compaction, and global-memory atomics for cross-group queues (Lumen's dirty-brick lists, GPU feedback compaction, priority histograms are all buildable this way — they're atomics + radix-histogram patterns, not wave ops).
- **No bindless**: everything above is already atlas-shaped — SDF brick pool + page-table textures, 4096² surface-cache atlas, octahedral probe atlases, clipmap volumes. This is not a coincidence: Lumen's SWRT path targets exactly this hardware class (deck p. 5: "plenty of video cards that can't do HWRT").
- **3D texture updates**: `UpdateSubresource` to volumes is slow and serializing; update via CS UAV writes to `RWTexture3D` (row-major brick dispatches), and keep bricks small (8³) so dirty-region updates are tight. Eikonal/JFA propagation passes are plain CS.

### 7.2 Shipped D3D11-class GI systems (evidence the above works)

| System | Scene rep | Lighting rep | Update cost | Key numbers |
|---|---|---|---|---|
| **The Division** (Ubisoft Massive, GDC 2016) | Baked **surfels** per probe (G-buffer cubemap captures, CPU-clustered into "bricks") | PRT probes: sky-visibility + surfel-brick weights over **HL2 ambient cube / 2nd-order SH**; runtime relight of surfels→bricks→probes every frame on GPU | **0.95 ms XB1 (non-async), 0.47 ms GTX 760**, relighting 2 sectors (600–800 probes)/frame | 4 m probe spacing + wall probes; 64 m 2D sector cells, ≤1000 (typ. 200–300) probes/sector; Manhattan: 1.156M probes, 56.4M surfels, 1.07 GB; irradiance volume **32×16×32 voxels per basis direction covering 100×50×100 m**; separate interior volume selected by stencil to stop bleeding; distant 2D "sector probe" texture. [Slides](http://mrakobes.com/Nikolay.Stefanov.GDC.2016.pdf), [GDC Vault](https://gdcvault.com/play/1023273/Global-Illumination-in-Tom-Clancy) |
| **Enlisted / Dagor** (Gaijin, GDC 2020) | Scene **voxelized temporally from the G-buffer** into nested camera-centered 3D cascades (no offline voxelization) | Nested 3D grids of light-probe/irradiance data, multi-bounce via feedback | **< 1 ms PS4-class; 0.57 ms GTX 1070** | Semi-dynamic scenes, numerous lights; pure D3D11-era compute. [GDC Vault](https://www.gdcvault.com/play/1026469/Scalable-Real-Time-Global-Illumination), [Dagor engine](https://gaijinentertainment.github.io/DagorEngine/dagor-home/dagor_engine.html) |
| **CRYENGINE SVOGI** (DX11, since 3.8) | Sparse voxel octree of opacity/albedo, incremental GPU voxelization; optional analytical proxies | Cone-traced AO + sun bounce (diffuse-only default; specular still from env probes) | **3–4 ms XB1; 2–3 ms mid PC (low-spec mode); AO-only < 2 ms XB1** | No precompute, works with time-of-day; leaks controlled by voxel size. [Docs](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/25535599) |
| **Metro Exodus (2019, non-RT path)** | Coarse **voxel grid near the camera fed from a reflective shadow map** | Voxel GI term (coarse, camera-local) + baked/probe ambient | shipped on base consoles | Replaced by DDGI-style probes in Enhanced Edition. [4A tech dive](https://www.4a-games.com.mt/4a-dna/in-depth-technical-dive-into-metro-exodus-pc-enhanced-edition), [NVIDIA blog](https://developer.nvidia.com/blog/global-illumination-in-metro-exodus) |
| **Godot 4 SDFGI** (compute-only, no HWRT — Vulkan but SM5-class algorithms) | Per-cascade voxelization → **SDF cascades** (JFA-built), 4–8 cascades, cascade cell size doubling | Probe grid per cascade, octahedral irradiance + occlusion term; specular by SDF march; `bounce_feedback` multi-bounce | 60 fps on GTX 1060 | SDF updates **only when camera crosses cascade boundaries**; dynamic objects receive but don't contribute; walls must exceed 1 voxel thickness; Y-scale 50–75% trick for probe density. [Article](https://godotengine.org/article/godot-40-gets-sdf-based-real-time-global-illumination/), [docs](https://github.com/godotengine/godot-docs/blob/master/tutorials/3d/global_illumination/using_sdfgi.rst) |
| **AMD GI-1.0** (2022, compute-first) | Any tracer (SDF works) | **Two-level cache**: 8×8 octahedral *screen probes* (¼ spawned/frame, reprojected with plane/normal checks in LDS) + hash-grid *world cache* of outgoing radiance | ~ms-scale | The closest published "Lumen-lite" recipe; reprojection kernel pseudocode in §2.1. [Paper](https://gpuopen.com/download/publications/GPUOpen2022_GI1_0.pdf) |

### 7.3 Synthesis for a from-scratch SM 5.0 implementation

The literature converges on one architecture for this hardware class: **narrow-band brick-atlas mesh SDFs + a 4-cascade sparse global SDF with dirty-brick updates (§1)**, surface attributes from a **card/surface cache near-field + 6-direction voxel radiance far-field (§1.4)**, gathered by **jittered octahedral screen probes (8×8) importance-sampled from last frame's reprojected radiance, backed by 32×32 world probes on a clipmap with Chebyshev-guarded interpolation (§2)**, filtered **in probe space (3×3) plus a depth/normal-rejecting temporal pass and SVGF-style edge-stopped upsampling (§3)**, with **roughness-tiered reflections reusing the same probes (§4)**, sky injected at trace miss with cached sky-visibility (§5), pre-exposed FP16/R11G11B10 buffers with gamma-encoded probe storage (§6), and everything expressed as SRV-read/UAV-write atlas passes with LDS reductions instead of wave ops (§7.1). Every component above has shipped on D3D11-class hardware.