# HDR / Tonemapping / Color Pipeline — Audit and AAA Roadmap

Audit date: 2026-08-10 (branch feature/gi).

STATUS 2026-08-10: Phase 0 (A1-A8) and Phase 1 are IMPLEMENTED (see git log).
A9 documented in-shader (1.8 pre-scale stays off until Phase 3 mid-gray anchoring);
A10 (BRDF LUT) deferred — default path is the analytic Lazarov fit, LUT unused.
Phase 1 notes: compiled textures now emit KTX2 (sRGB/linear tagged); loader maps
the container tag to BGFX_TEXTURE_SRGB; `automatic` = raw (legacy); mesh importer
tags material textures; "Recompile > Textures > Migrate Color Spaces (Project)"
back-fills existing projects (menu action, not auto-run — the initial asset scan
races the registry and an unprompted bulk recompile was judged worse UX).
Remaining phases: 2 (physical units/exposure), 3 (grading+LUT+dither), 4 (TAA
motion vectors/Karis resolve, bloom scatter), 5 (HDR display out).

STATUS 2026-08-10 (later): Phase 2 defaults landed. Auto exposure now floats
freely: min_ev -6, max_ev 16, compensation +1 (anchors metered average at ~18%
mid-gray given the K=12.5 / exp2(-EV)/1.2 formulation: unclamped metering pins
the average at 1/9.6 ~= 10.4%, +1 EV doubles it). Default tonemapper switched
neutral -> AgX (hue-robust hero transform; punchy/golden/aces remain as looks).
STATUS 2026-08-10 (later still): metering retuned after visual review -- the
[0.50, 0.95] band let shadows drag exposure up and wash out lit areas in
contrasty scenes. Now [0.80, 0.98]: highlight-protecting bright-band metering
(UE-style); sky participates so it exposes saturated. Compensation stays +1 =
"lit content near mid-gray, shadows fall dark". Phase 3 step 1 landed: color
grading in the tonemap pass (white balance via CAT02 LMS von Kries, log
contrast around 0.18, Rec.709 saturation -- linear space, post-exposure,
pre-curve) + TPDF dither before UNORM8 quantization (default on).
BUG FOUND+FIXED via sealed-box test: SSR and gi_resolve consumed the previous
frame's FINAL OBUFFER (post-tonemap sRGB RGBA8, UI included) as scene radiance.
Display-referred feedback into linear lighting + free-floating exposure =
runaway brightening in dark scenes. Now a PREV_SCENE_HDR snapshot (post-TAA,
pre-bloom/tonemap/UI, format-matched blit) feeds both consumers. Residual
brightening of sealed interiors under auto exposure is eye adaptation
revealing REAL ambient leaks (unoccluded skylight SH / env probe without GI)
-- per-scene remedies: enable GI, local probe, or raise Min EV.
Phase 3 step 2 landed: per-channel lift/gamma/gain (display-referred, neutral
mid-gray 0.5 authoring, color-cast capable), linear-space vignette
(intensity/smoothness), animated luma-weighted film grain -- all neutral by
default, zero visual change until dialed in. Remaining Phase 3: bake
grade+curve+encode into a 32^3 LUT (perf + external .cube LUT import) now that
the op stack is settled; optional shadows/mids/highlights color wheels.
STATUS 2026-08-11: Phase 4a TAA resolve quality landed WITHOUT motion vectors
(they do not exist in the engine; parked as separate research -- depth
reprojection covers camera motion on static scenes, so these fixes are fully
effective today): Catmull-Rom 5-fetch history reconstruction (kills the
accumulation blur), YCoCg variance box + ray-clip-toward-mean (replaces RGB
clamp; less hue-skewed ghosting), Karis 1/(1+luma)-weighted resolve (kills HDR
firefly flicker). fs_taa.sc only; sharpen path still uses RGB moments.
Auto exposure min_ev default -6 -> -3 (dark reads dark; deep adaptation
surfaces GI residuals -- see memory gi-sealed-box-leak). Sealed-box GI leak
hunt PARKED: dominant source unidentified (floor-bleed model falsified);
next step is a per-tier sun-visibility debug view (spawn task created).
Phase 4b bloom retune landed: SCATTER mode is the new default (threshold 0) --
thresholdless: recursive lerp pyramid (per-hop ONE/INV_SRC_ALPHA premultiplied
blend realizes mix(dst, up, s) exactly; MIP0 keeps its own downsample as
recursion base; pyramid carries scene-level energy). COMBINE REVISED after
visual test: pure mix(scene, pyramid, i) was rejected -- it couples halo
strength to full-screen blur (no halo at 0.05, frosted at 1.0). Now UNIFIED
additive combine `scene + pyramid * intensity` for both modes (they differ
only in pyramid content); base stays sharp at any intensity, the small mean
add is absorbed by scale-invariant auto exposure (UE's model). Defaults:
intensity 0.15, scatter 0.7. LEGACY thresholded mode preserved behind
threshold > 0 (old scenes serialize threshold 5 + intensity 1 -> identical
look). Lens dirt wired: dirt_texture asset slot + dirt_intensity; black
fallback = exact no-op. Karis prefilter + soft clamp retained in both modes.
Tuning notes: Scatter shapes the halo (low = tight/bright near sources,
high = wide veil); Intensity scales the whole glow linearly.
Phase 2 close-out (sky-scale consolidation) landed: ONE named conversion
`perez_luminance_to_engine = 0.1` + `perez_horizon_dim = 0.6` in
perez_luminance.h, produced by compute_perez_exposition(sun_altitude) and
consumed by the sky dome, the irradiance bake and the flat ambient (ratios
hold by construction). The flat Perez ambient's hand-calibrated collapse
(mix(sky, sun, sun_weight*0.25) at exposition 0.1*altitude) was DELETED and
replaced by irradiance shader mode 5 = the SAME hemisphere integration as
mode 1 truncated to L0 (exact analog of cubemap modes 2/3). The 2.0 ambient
boost became the documented `sky_ambient_cubemap_parity` constant (UI parity
between analytic sky and display-referred cubemaps; not derivable).
Fixes 2026-08-11 (late): film grain rewritten -- ign16x16 is a STATIC tiled
16x16 lookup (right for dither, wrong for grain: sliding a fixed tile is not
noise); grain now uses a per-pixel per-frame white-noise hash, amplitude scale
0.12 -> 0.25, highlights keep 20% response. Auto exposure gained
`dark_adaptation` (default 0.5): the single-slope version of UE's Exposure
Compensation Curve / Unity HDRP's Curve Remapping -- only that fraction of a
dark scene's EV deficit below the neutral point is adapted away, so interiors
keep (1 - slope) of their true relative darkness; Min EV remains the hard
stop; bright scenes unaffected (deficit clamped at 0).
LOOK CALIBRATION 2026-08-11, FINAL (user-chosen after A/B vs Unreal incl.
ACES/ACES-Lum/AgX+contrast comparisons): tonemapping method = AGX (hue
robustness won over ACES's built-in punch), auto exposure compensation = +3
(bright band anchored ~83% pre-tonemap -- the brighter anchor is what drives
AgX's flat mid to white whites, substituting for ACES's S-curve contrast),
dark_adaptation = 0.1 (darkness reads as darkness; a scene 5 stops under
neutral lifts only half a stop), min_ev -3. Default light color changed
(255,244,214) warm -> pure white (light.h + defaults.cpp): warmth is per-light
artistic choice, a tinted default skews perceived albedo everywhere.
Insight worth keeping: AgX-with-bright-anchor and ACES-with-mid-anchor reach
similar "lit looks lit" endpoints by different routes; anchor and curve trade
off against each other.
Remaining: 32^3 LUT bake + .cube import (Phase 3 close-out; deferred until
.cube import is wanted -- see decision note), motion vectors (research:
G-buffer velocity target, skinned/instanced prev-transform plumbing).
KEY INSIGHT recorded: with the meter unclamped, exposure is scale-invariant --
multiplying the whole scene by any constant is fully compensated. So the rest
of Phase 2's "physical units" reduces to RELATIVE balance: fold the sky
`exposition = 0.1` / `ambient_intensity_boost = 2.0` / `sun_weight * 0.25`
magics into one documented sky scale and verify sun-direct : sky-ambient :
probe-specular ratios. Absolute lux/candela become an authoring nicety, not a
correctness prerequisite. Pre-exposure (fp16 overflow guard) still Phase 2's
open engineering item.

## Verdict

The post stack has good bones (correct pass order, histogram auto-exposure,
Jimenez bloom, UE-lineage GGX BRDF library, big tonemapper menu). What blocks
"AAA" is not the tonemappers — it is that the engine **lights in gamma space**
(no sRGB decode anywhere on texture inputs), uses **non-physical, display-range
light units** (sky `exposition = 0.1`, `max_ev = 0`, `ambient_intensity_boost = 2`),
and has **no color grading stage at all**. Plus a handful of outright math bugs
listed below.

---

## A. Confirmed bugs / wrong math (small, fix first)

1. **Hable white-point constant mismatch** — `engine_data/data/shaders/tonemapping/tonemapping.sh:132`
   `hable_map` uses the GDC-presentation constants (A=0.22, B=0.30, E=0.01) but
   `whiteScale = 0.72513` was computed from the filmicworlds *website* constants
   (A=0.15, B=0.50, E=0.02). With the constants actually in the file,
   `hable_map(11.2) = 0.86730`. Result: everything is over-brightened ×1.196 and
   whites clip several stops before the intended W=11.2.
   Fix: `const float whiteScale = 0.86730;` (or compute from W in-shader).

2. **FILMIC (Hejl) mode outputs linear into RGBA8** — `tonemapping.sh:140-145` + `:384-388`
   `tonemap_filmic` ends with `pow(result, 2.2)` which *removes* the curve's baked
   1/2.2 gamma (output = linear), but the `TONEMAP_FILMIC` branch only `saturate`s,
   claiming "output is already display-encoded". It is not — the mode renders
   roughly a full gamma too dark. Fix: delete the `pow(2.2)` (keep Hejl's baked
   encode, matching the branch comment) or keep it and run `linear_to_srgb`.

3. **`tonemap_reinhard_luminance` NaN on black pixels** — `tonemapping.sh:103-108`
   `color * (nLum / lum)` is 0/0 for lum==0. Fix: `nLum / max(lum, 1e-5)`.

4. **Reflection probes capture LDR into an RGBA16F cubemap** —
   `engine/engine/rendering/pipeline/deferred/pipeline.cpp:61-71` +
   `create_or_resize_g/l_buffer`. `strip_post_effects_for_reflection_probe_capture`
   clears `fill_hdr_params`, and buffer creation uses `fill_hdr_params` as the
   "HDR formats" switch — so probe G/L buffers drop to RGBA8, clamping the capture
   to [0,1] with 8-bit banding before it is blitted into the RGBA16F cubemap. The
   comment above the function says the opposite ("keep HDR buffer setup").
   Fix: separate "HDR buffer formats" from "tonemapping config" (explicit
   `bool hdr` in run_params); probe captures must render HDR.

5. **AgX decode/encode round-trip mismatch** — `tonemapping.sh:314` + `:419`
   `agx_core` linearizes with `pow(2.2)` and the caller re-encodes with the exact
   piecewise sRGB curve — not inverses; darks shift slightly. Fix: either use
   `srgb_to_linear`-style exact decode in `agx_core`, or output display-encoded
   directly and skip `linear_to_srgb` for the AgX modes.

6. **`TONEMAP_NONE` writes linear to RGBA8** — `tonemapping.sh:364-367`. Passthrough
   without display encode renders dark. If "none" means "no curve", it should
   still `linear_to_srgb`; keep a separate debug-raw mode if raw output is wanted.

7. **Inconsistent luminance weights** — `lighting.sh:1366-1369` uses NTSC
   (0.3, 0.59, 0.11); tonemapping/bloom/histogram use Rec.709
   (0.2126, 0.7152, 0.0722). Unify on Rec.709.

8. **Bloom intensity compounds per cascade hop** — `bloom_pass.cpp:233-236`:
   `effective_weight = intensity * mip_tint.a` is applied at *every* upsample
   step, so mip k's contribution scales by intensity^(k-ish). At the default
   intensity=1 this hides; any other value bends the mip distribution
   exponentially. Fix: per-mip weights in the cascade, global intensity applied
   once in the combine pass.

9. **ACES (Hill fit) calibration** — `tonemapping.sh:170`: the reference
   implementation pre-scales input by 1.8 (BakingLab); here it is commented out,
   so ACES modes sit visibly darker than reference ACES. Decide and document
   (this interacts with mid-gray anchoring, Phase 3).

10. **8-bit PNG BRDF LUT** — `deferred/pipeline.cpp:2480`
    (`ibl_brdf_lut.png`) — quantized split-sum LUT, and no sRGB/linear
    control on its import. Note the runtime default path is the analytic
    `EnvBRDFApprox` (Lazarov), so first verify the LUT path is even reachable;
    then either generate an RG16F LUT at boot or delete the texture path.

## B. The two systemic problems

### B1. No linear-color pipeline (highest impact)

Evidence:
- Not one `BGFX_TEXTURE_SRGB` in engine/editor code; textures load with default
  flags (`asset_reader.cpp:169-177` → `texture.cpp:124`).
- texturec is invoked without color-space flags (`asset_compiler.cpp:536-601`);
  importer meta has no sRGB/linear concept (`texture_importer_meta`).
- `fs_deferred_geom.sc:75` samples albedo raw; `EncodeGBuffer`/`Decode*` never
  convert; lighting multiplies sRGB-encoded albedo by light values; the final
  `linear_to_srgb` at tonemap re-encodes.

Consequence: all shading/GI/bloom/exposure arithmetic runs on sRGB-encoded
texture colors — falloffs, additive lights, bounce colors and material response
are all skewed; procedural inputs (Perez sky, light colors) are mixed in as if
the same space. This is the single biggest visual-fidelity blocker and the first
thing both UE and Unity get right by construction (sRGB sampled textures +
linear working space, ~2002-era "importance of being linear").

Fix outline (Phase 1 below): import-time color-space classification
(albedo/emissive = sRGB; normal/rough/metal/AO/masks = linear), hardware sRGB
sampling, sRGB-encoded G-buffer base color (RGBA8_SRGB RT or manual
encode/decode to avoid 8-bit linear banding), linearize material color pickers,
and sweep every albedo consumer (particles, RmlUI-world, GI surface-cache
`attr_albedo`, SDF debug views).

### B2. Non-physical light units / display-referred lighting

Evidence: `auto_exposure_pass.h:35-42` (`max_ev = 0` "lighting is authored at a
fixed reference exposure rather than calibrated to physical cd/m²"),
`deferred/pipeline.cpp:1399-1414` (`exposition = 0.1`, `ambient_intensity_boost
= 2.0`, empirical `sun_weight * 0.25`), sky shader `u_exposition` scaling.

Consequence: auto-exposure can only brighten (EV range [-4, 0]); HDR headroom
above 1.0 is small, so bloom threshold=5 rarely fires naturally; tonemappers all
operate near their toe; switching tonemap methods changes scene brightness
because there is no common mid-gray anchor.

---

## C. How UE / Unity do it (reference summary)

**Assets**: import classifies color-space per texture type; BaseColor/Emissive
sampled as sRGB formats (hardware decode), data maps linear; color pickers
produce linear floats. Mips generated in linear space.

**Lighting units**: Unity HDRP is fully physical (lux / lumen / candela / EV100
camera with ISO-aperture-shutter); UE is "physically plausible" (lux for
directional, cd/lm for punctual, EV100 exposure). Both meter exposure with a
luminance histogram with percentile trim (as this engine already does) but over
a wide EV range, with compensation and metering masks.

**Range management**: Unity applies *pre-exposure* (previous frame's exposure
multiplied into lighting at render time) so fp16 scene color never overflows;
UE similarly pre-exposes scene color.

**Post order** (both): scene color fp16 → TAA (with motion vectors,
Karis-weighted resolve) → bloom (energy-conserving mip chain; UE default has no
threshold, "scatter" model) → exposure → **combined color-grading + tonemap +
output-encode baked into one 3D LUT** (UE: 32³ LUT evaluated in ACEScg working
space with the ACES-derived filmic S-curve: Slope/Toe/Shoulder/BlackClip/WhiteClip,
blue-correction, expand-gamut; Unity: LogC-space LUT, tonemap modes
Neutral/ACES/Custom/External-LUT) → film grain + triangular dither → 8-bit sRGB
or HDR display encode (PQ / scRGB with paper-white nits, UI composited
separately). UE additionally offers *Local Exposure* (bilateral-grid local
tonemapping) for high-contrast scenes.

**Mid-gray anchoring**: both pin 18% scene-referred gray to ~10% display so
exposure/tonemapper choices don't shift overall image brightness.

---

## D. Roadmap

### Phase 0 — Bug fixes (1-2 days, no content rebake)
Items A1-A10. Verify each against reference curves (desmos / RenderDoc pixel
math). A4 needs a small run_params refactor. Ship independently.

### Phase 1 — Linear color pipeline (the prerequisite for everything else)
1. `texture_importer_meta`: add `color_space { srgb, linear }`, defaulted by
   type (albedo/emissive/UI = srgb; normal/rough/metal/AO/height/mask = linear;
   .hdr/.exr = linear).
2. Compiler: pass `--linear` to texturec for linear types (mip filtering
   correctness); emit sRGB-view info to the runtime.
3. Runtime: create color textures with `BGFX_TEXTURE_SRGB`; audit samplers.
4. G-buffer: keep base color 8-bit but sRGB-encoded (SRGB render target so
   writes encode / reads decode in hardware; fallback: manual encode in
   `EncodeGBuffer`, decode in `Decode*` — one place each).
5. Material system: linearize `u_base_color`, emissive, subsurface, light
   colors at upload (color pickers are sRGB).
6. Sweep other albedo consumers: particles, rmlui_world quad, GI surface cache
   bake (`attr_albedo`), voxel lighting, SDF debug, skybox cubemaps
   (authored sRGB vs .hdr linear).
7. Content migration: expect scenes to brighten; provide a one-shot intensity
   remap for existing scenes + updated defaults.
   Acceptance: gray-card scene — a 0.5 sRGB albedo plane under intensity-π white
   light reads 0.5 sRGB back from the final image; Bistro/Sponza side-by-side
   approved.

### Phase 2 — Exposure & light units
1. Define canonical units (recommend UE-style "plausible": directional in lux,
   punctual in candela-ish scalars, sky/emissive on the same scale) and remove
   `exposition=0.1` / `ambient_intensity_boost=2.0` / `sun_weight*0.25` magics by
   folding them into documented unit conversions.
2. Auto-exposure: widen `min_ev/max_ev` (e.g. -6..16) once units are real;
   keep histogram+percentile design (it is already Frostbite-grade); expose
   compensation curve per-volume.
3. Optional but recommended: pre-exposure multiplier into scene color (Unity
   style) to protect fp16 from bright suns; the `safeHDR` clamps then become
   safety nets instead of load-bearing.
   Acceptance: noon exterior and dim interior both auto-expose correctly with
   the SAME light rig, no per-scene exposure fudging.

### Phase 3 — Tonemap & grading to AAA parity
1. Pick hero transform: AgX (already in-tree, most robust hue behavior) or
   ACES-Hill (with the 1.8 calibration decided). Keep the menu for compat, but
   define ONE default and anchor mid-gray (0.18 → ~10% display) across modes.
2. Add grading stack (new uniforms on tonemapping pass, evaluated before the
   tone curve, in linear or log space): exposure bias, white balance
   (temp/tint via Bradford/LMS), lift-gamma-gain or log contrast, HSV
   saturation, shadow/midtone/highlight color wheels.
3. Bake grade+curve+encode into a 32³ 3D LUT computed on change (one compute
   dispatch), sampled once in `fs_tonemapping` — cost drops below the current
   branch cascade and enables external/imported LUTs.
4. Output dithering: triangular-PDF noise pre-quantization (kills sky banding
   on RGBA8); optional film grain, vignette, chromatic aberration.
   Acceptance: LUT path bit-matches the analytic path within 1/255 on a sweep;
   gradient sky shows no banding at 8-bit.

### Phase 4 — Temporal & specular quality that feeds the tonemapper
1. TAA (`taa/fs_taa.sc`): add G-buffer motion vectors (currently
   camera-reprojection only → dynamic objects ghost); Karis-weighted
   (1/(1+luma)) resolve or tonemap-resolve-untonemap to stop HDR firefly
   flicker; YCoCg variance clip; Catmull-Rom history sampling (bilinear history
   blurs).
2. Bloom: per-mip weights + single global intensity at combine (A8);
   threshold→0 "scatter" mode as default once Phase 2 lands; wire the existing
   `dirt_intensity` stub.
3. Replace/verify BRDF LUT path (A10).

### Phase 5 — HDR display output (optional endgame)
bgfx supports RGB10A2/scRGB swap chains; add PQ (ST.2084) output transform with
paper-white/max-nits settings, Rec.2020 gamut mapping, UI composited in SDR
sub-range. Gate behind display caps query.

## Notes on what is already good (do not regress)
- Pass order TAA → exposure meter → bloom → tonemap → FXAA-after-tonemap is
  correct and matches UE/Unity.
- Histogram auto-exposure (percentile trim, EV100 with K=12.5, log-space
  adaptation, up/down speeds) is the right architecture.
- Bloom: 13-tap Karis-weighted first downsample + tent upsample is the Jimenez
  CoD standard; keep.
- BRDF library is UE-lineage GGX + SmithJointApprox + Schlick with
  multi-scatter energy terms; specular AA (Tokuyoshi-Kaplanyan) present;
  texelFetch G-buffer decode avoids silhouette sparkles.
- ACES Hill matrices and AgX matrices/constants verified correct as transcribed.
