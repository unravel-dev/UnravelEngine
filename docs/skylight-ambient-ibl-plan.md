# Skylight-Driven Ambient & IBL Plan

This document outlines the plan to move ambient/indirect lighting from the directional light to the skylight component and to derive irradiance from the atmospheric pass, aligning with industry-standard IBL practices.

---

## Current State

- **Ambient source**: Directional light (`ambient_intensity`, `light.color` for tint)
- **Atmospheric pass**: Renders visible sky to LBUFFER; computes Perez `sky_luminance_rgb` and `sun_luminance_rgb` (time-of-day)
- **Indirect pass**: Uses `u_light_data.xyz` (ambient color) and `u_light_data.w` (intensity) from directional light
- **Skylight component**: Provides parameters to atmospheric pass; expects directional light on same entity

---

## Phase 1: Move Ambient to Skylight Component

### 1.1 Add to `skylight_component`

- `ambient_intensity` (float) — strength of indirect diffuse
- `ambient_color` (math::color) — optional tint override (default from sky)

### 1.2 Remove from `light`

- Remove `ambient_intensity` from `light` struct
- Keep `color` and `intensity` for direct lighting only

### 1.3 Update Pipeline

- In `run_light_buffer_pass`, read ambient from the skylight entity (same entity used for atmospheric pass)
- If no skylight exists, use neutral fallback (e.g. intensity 0, or default gray)

### 1.4 Migration

- Defaults: set skylight ambient in `defaults.cpp` where skylight entities are created

### Files to Modify

| File | Changes |
|------|---------|
| `engine/rendering/ecs/components/light_component.h` | Add `ambient_intensity`, `ambient_color` to skylight_component |
| `engine/rendering/light.h` | Remove `ambient_intensity` from light struct |
| `engine/rendering/pipeline/deferred/pipeline.cpp` | Read ambient from skylight in indirect pass |
| `engine/meta/rendering/light.cpp` | Update reflection for light (remove) and skylight (add) |
| `engine/meta/ecs/components/light_component.cpp` | Add skylight ambient to reflection |
| `engine/defaults/defaults.cpp` | Set skylight ambient defaults |

---

## Phase 2: Irradiance from Atmospheric Pass

The Perez atmospheric pass already computes `sky_luminance_rgb` and `sun_luminance_rgb` from time-of-day. Use these as the basis for ambient instead of the directional light color.

### 2.1 Option A — Simple (Recommended First Step)

Use sky/sun luminance as the ambient basis:

- In `run_atmospherics_pass`, after computing `sky_luminance_rgb` and `sun_luminance_rgb`, derive an ambient term:
  - e.g. `ambient = mix(sky_luminance, sun_luminance, sun_weight) * skylight.ambient_intensity`
- Store in a small struct or pass as uniforms to the indirect lighting pass
- Indirect pass uses this instead of directional light color

**Pros**: Minimal changes, reuses existing Perez data  
**Cons**: Uniform ambient (no normal-dependent variation)

### 2.2 Option B — Spherical Harmonics (SH)

Industry standard (Unreal, Frostbite):

- Add an irradiance pass that samples the Perez sky in many directions
- Project to SH (L0–L2, 9 coefficients per channel)
- Store SH in a buffer or texture
- Indirect pass evaluates SH from surface normal

**Pros**: Normal-dependent diffuse, compact, standard  
**Cons**: More implementation work

### 2.3 Option C — Irradiance Cubemap

Full IBL-style:

- Render Perez sky into a low-res cubemap (e.g. 32×32)
- Convolve with cosine-weighted hemisphere (Lambert)
- Sample irradiance cubemap by normal in indirect pass

**Pros**: High quality, normal-dependent  
**Cons**: More memory and sampling cost

---

## Phase 3: Data Flow (Industry-Standard Layout)

```
Skylight Component
├── ambient_intensity      (user control)
├── ambient_color          (optional tint override)
├── irradiance_quality     "uniform" | "normal_dependent"
├── mode, turbidity, etc.  (existing)

Atmospheric Pass
├── Renders visible sky → LBUFFER (unchanged)
├── Computes sky_luminance, sun_luminance
└── Outputs irradiance for indirect:
    - Option A: (sky_luminance, intensity) → uniforms
    - Option B: SH coefficients → buffer
    - Option C: irradiance cubemap → texture

Indirect Lighting Pass
├── Samples irradiance (from atmosphere or fallback)
├── Applies diffuse_color, AO, energy preservation
└── No longer uses directional light for ambient
```

---

## Implementation Order

| Step | Task | Effort |
|------|------|--------|
| 1 | Add `ambient_intensity` and `ambient_color` to skylight_component | Low |
| 2 | Remove `ambient_intensity` from light struct | Low |
| 3 | Update indirect pass to read ambient from skylight | Low |
| 4 | Wire atmospheric pass to pass sky/sun luminance into indirect (Option A) | Medium |
| 5 | (Later) Add SH or irradiance cubemap for full IBL (Option B/C) | High |

---

## References

- [Karis 2013] "Real Shading in Unreal Engine 4" — split-sum IBL
- [Perez 1993] "An All-Weather Model for Sky Luminance Distribution"
- [Preetham 1999] "A Practical Analytic Model for Daylight"
- Unreal Engine: Sky Light component, reflection captures
- Unity: Skybox, Light Probes, Environment
