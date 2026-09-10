#include "visualization_modes.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <engine/rendering/pipeline/deferred/pipeline.h>

#include <algorithm>
#include <array>

namespace unravel
{
namespace
{

// -----------------------------------------------------------------------------
// Legends
//
// Every color below is the literal value the corresponding debug shader writes, so the
// swatch in the menu matches the pixel on screen. Sources:
//   engine_data/data/shaders/gbuffer/fs_gbuffer_visualize.sc   (G-Buffer / lighting / GTAO)
//   engine_data/data/shaders/velocity/fs_velocity_debug.sc     (motion vectors)
//   engine_data/data/shaders/gi/fs_sdf_debug.sc                (traced views)
//   engine_data/data/shaders/gi/gi_light_voxels_kernel.sh      (sun tiers, vis memo writes)
// Continuous readouts (roughness, depth, ...) carry no legend; their range is stated in the
// description instead.
// -----------------------------------------------------------------------------

constexpr std::array<visualization_swatch, 1> k_legend_diffuse_color = {{
    {{0.0f, 0.0f, 0.0f}, "Pure metal - metalness 1 leaves no diffuse lobe"},
}};

/// ComputeF0(0.5, base, metalness) gives every dielectric F0 = 0.04 LINEAR, and this view
/// sRGB-encodes before writing, so the constant reads as ~0.22 grey on screen - not near-black.
constexpr std::array<visualization_swatch, 1> k_legend_specular_color = {{
    {{0.22f, 0.22f, 0.22f}, "Dielectric - the one flat F0 every non-metal shares (0.04 linear)"},
}};

constexpr std::array<visualization_swatch, 4> k_legend_normals = {{
    {{1.0f, 0.0f, 0.0f}, "Facing +X"},
    {{0.0f, 1.0f, 0.0f}, "Facing +Y (up)"},
    {{0.0f, 0.0f, 1.0f}, "Facing +Z"},
    {{0.0f, 0.0f, 0.0f}, "Facing -X / -Y / -Z: negatives are written raw and clamp to black"},
}};

constexpr std::array<visualization_swatch, 2> k_legend_velocity = {{
    {{0.0f, 0.0f, 0.0f}, "Still - no screen motion this frame"},
    {{1.0f, 0.0f, 1.0f}, "NaN or infinite velocity - a broken previous transform"},
}};

constexpr std::array<visualization_swatch, 3> k_legend_sdf_step_count = {{
    {{0.0f, 1.0f, 0.0f}, "Cheap - the ray resolved in few steps"},
    {{1.0f, 0.0f, 0.0f}, "Expensive - close to the step budget"},
    {{0.0f, 0.0f, 1.0f}, "Budget exhausted without resolving (grazing ray, or too far)"},
}};

/// A channel readout, so there is one categorical color: all-zero. Blue IS the voxel-size
/// presence flag, which is why "blue but no voxel size" cannot occur.
constexpr std::array<visualization_swatch, 1> k_legend_sdf_headers = {{
    {{0.0f, 0.0f, 0.0f}, "Every channel zero - no header at all, the buffer never arrived"},
}};

/// Only the red entry is a flag; the rest is SdfProbeLocal's raw texel value, so the ramp is
/// the legend. Bright green just inside the bounds is EXPECTED - the bake keeps the surface
/// encode_range (4) voxels away from the boundary, and an 8-voxel brick straddles both.
constexpr std::array<visualization_swatch, 4> k_legend_sdf_brick_probe = {{
    {{1.0f, 0.0f, 0.0f}, "Empty brick - the indirection entry says it owns no voxels"},
    {{0.0f, 1.0f, 0.0f}, "Surface brick, texel saturated OUTSIDE (+4 voxels) - the normal reading in the bounds padding"},
    {{0.0f, 0.5f, 0.0f}, "Surface brick, texel near distance zero - the probe point is on the surface"},
    {{0.0f, 0.0f, 0.0f}, "Surface brick, texel saturated INSIDE (-4 voxels) - the probe point is in solid geometry"},
}};

constexpr std::array<visualization_swatch, 3> k_legend_sdf_bounds_entry = {{
    {{0.0f, 1.0f, 0.0f}, "Healthy - comfortably above the hit threshold"},
    {{0.0f, 0.0f, 1.0f}, "Positive but under the threshold - reads as an immediate hit"},
    {{1.0f, 0.0f, 0.0f}, "Negative - the bounds start inside the surface"},
}};

constexpr std::array<visualization_swatch, 5> k_legend_sdf_cascade_levels = {{
    {{1.0f, 0.15f, 0.15f}, "Level 0 - finest"},
    {{0.15f, 1.0f, 0.15f}, "Level 1"},
    {{0.2f, 0.4f, 1.0f}, "Level 2"},
    {{1.0f, 0.9f, 0.2f}, "Level 3 - coarsest"},
    {{0.25f, 0.25f, 0.25f}, "Outside every cascade - per-instance fields answered alone"},
}};

constexpr std::array<visualization_swatch, 2> k_legend_gi_voxel_albedo = {{
    {{1.0f, 0.9f, 0.0f}, "Not attributed at any level - attribution missed the hit"},
    {{1.0f, 0.0f, 1.0f}, "Hit outside every cascade level"},
}};

constexpr std::array<visualization_swatch, 4> k_legend_gi_light_voxels = {{
    {{0.0f, 0.05f, 0.35f}, "Attributed but UNMEASURED - every face was gated, or has had no rotation slot"},
    {{0.0f, 0.0f, 0.0f}, "Measured darkness - a shadow ray found no light (not the same as the above)"},
    {{1.0f, 0.9f, 0.0f}, "Not attributed at any level"},
    {{1.0f, 0.0f, 1.0f}, "Hit outside every cascade level"},
}};

constexpr std::array<visualization_swatch, 1> k_legend_gi_world_probes = {{
    {{1.0f, 0.0f, 1.0f}, "No cascade probe window covers the hit, or every cage weight died"},
}};

constexpr std::array<visualization_swatch, 6> k_legend_gi_probe_sky = {{
    {{0.0f, 0.08f, 0.0f}, "Zero sky - what a sealed interior must read"},
    {{0.5f, 0.0f, 0.0f}, "Low sky fraction"},
    {{1.0f, 1.0f, 0.2f}, "High sky fraction (the ramp is 4x scaled, so it saturates early)"},
    {{0.0f, 0.08f, 0.5f}, "Blue channel forced up over any of the above: inside the blend band, where the reader mixes the next level in"},
    {{0.1f, 0.3f, 1.0f}, "Pure blue: the hit landed INSIDE the field - a trace artifact, not a leak"},
    {{1.0f, 0.0f, 1.0f}, "No level cage answered"},
}};

constexpr std::array<visualization_swatch, 7> k_legend_gi_sun_tiers = {{
    {{0.0f, 0.8f, 0.0f}, "Shadow map (CSM cascade 0) answered; brightness = lit fraction"},
    {{1.0f, 0.0f, 0.0f}, "Traced field answered OCCLUDED - the face injects nothing"},
    {{0.85f, 0.85f, 0.85f}, "Traced field answered LIT; brightness = clearance visibility"},
    {{0.0f, 0.2f, 1.0f}, "Never queried - no directional sun, or the face points away from it"},
    {{0.0f, 0.05f, 0.35f}, "Culled by the pass gates (tunnel guard, cavity visibility)"},
    {{1.0f, 0.9f, 0.0f}, "Not attributed at any level (from the shared attribution pre-check)"},
    {{1.0f, 0.0f, 1.0f}, "OVERLOADED: hit outside every cascade level, OR a stale texel the pass has not rewritten"},
}};

/// This view is DISPLAYED through the sun-tier reader, so the last three rows come from that
/// shared path (attribution pre-check, then the alpha classifier) rather than from the memo.
constexpr std::array<visualization_swatch, 8> k_legend_gi_vis_memo = {{
    {{0.0f, 0.8f, 0.0f}, "Memo HIT - the stored verdict was served (healthy steady state)"},
    {{1.0f, 0.0f, 0.0f}, "Memo MISS - marched and restamped (one sweep after a generation bump is the fill)"},
    {{0.0f, 0.7f, 0.7f}, "Far-band hit - a stored far verdict served in the blend band"},
    {{1.0f, 0.5f, 0.0f}, "Far-band miss - the gated far read"},
    {{0.1f, 0.3f, 1.0f}, "Generation 0 - the memo was never seeded, or the uniform never arrived"},
    {{0.15f, 0.15f, 0.15f}, "No covering cage answered"},
    {{0.0f, 0.05f, 0.35f}, "Culled by the pass gates before the memo is ever consulted"},
    {{1.0f, 0.0f, 1.0f}, "OVERLOADED: hit outside every cascade level, OR a stale texel the pass has not rewritten"},
}};

constexpr std::array<visualization_swatch, 3> k_legend_gi_attr_emissive = {{
    {{0.0f, 0.0f, 0.0f}, "Attributed and genuinely not emissive - the common case"},
    {{1.0f, 0.9f, 0.0f}, "Not attributed at any level"},
    {{1.0f, 0.0f, 1.0f}, "Hit outside every cascade level"},
}};

constexpr std::array<visualization_swatch, 4> k_legend_gi_cage_health = {{
    {{0.0f, 1.0f, 0.1f}, "The cage answered at full weight"},
    {{1.0f, 0.5f, 0.0f}, "Partly rejected - the read renormalised onto the survivors"},
    {{1.0f, 0.05f, 0.0f}, "Almost nothing survived - sealed, or every cage probe dead"},
    {{1.0f, 0.0f, 1.0f}, "No level's cage answered at all"},
}};

constexpr std::array<visualization_swatch, 3> k_legend_gi_dirty_regions = {{
    {{1.0f, 0.1f, 0.05f}, "Inside a dirty region - full flush weight"},
    {{1.0f, 0.75f, 0.1f}, "In the soft margin around one (one probe spacing wide), by how much"},
    {{0.3f, 0.3f, 0.3f}, "Outside every region - the temporal keeps its full history here"},
}};

constexpr std::array<visualization_swatch, 3> k_legend_gi_probe_lattice = {{
    {{1.0f, 0.0f, 0.0f}, "DEAD - the lattice point is inside geometry, so the buried-probe gate "
                         "zeroed it; a room whose corners are all red is one the lattice missed"},
    {{0.15f, 0.3f, 1.0f}, "UNOCCUPIED - no geometry within GI_WORLD_PROBE_SLEEP_SPACINGS; it still "
                          "traces (sleeping it measured no saving). The share of blue is the "
                          "occupancy: mostly blue means a sparse lattice could cover the same slots "
                          "at a far finer spacing"},
    {{0.6f, 0.6f, 0.5f}, "Alive - the probe's own irradiance toward the viewer, tonemapped with a "
                         "floor so a dim probe still reads as a sphere"},
}};

constexpr std::array<visualization_swatch, 5> k_legend_gi_screen_probes = {{
    {{0.2f, 1.0f, 0.3f}, "Traced this frame - brightness is the ray budget it was allocated"},
    {{0.1f, 0.35f, 1.0f}, "Interpolated from its even-lattice parents (the adaptive saving)"},
    {{0.6f, 0.0f, 0.0f}, "Placed but no geometry under it"},
    {{1.0f, 1.0f, 1.0f}, "Tile borders, so probe spacing and lattice origin are readable"},
    {{0.0f, 0.0f, 0.0f}, "Outside the lattice, or the gather did not run this frame"},
}};

constexpr std::array<visualization_swatch, 6> k_legend_gi_probe_tiers = {{
    {{1.0f, 0.0f, 0.0f}, "Screen tier answered (Hi-Z hit read from last frame's composite) - these "
                         "lanes idle while the rest of the 8x8 group marches the SDF"},
    {{0.0f, 1.0f, 0.0f}, "SDF hit (mesh tier or clipmap)"},
    {{0.0f, 0.0f, 1.0f}, "Sky: a completion the world probes could not answer"},
    {{0.1f, 0.1f, 0.1f}, "The remainder: world-probe completions"},
    {{0.25f, 0.25f, 0.25f}, "Interpolated probe - no rays of its own"},
    {{0.0f, 0.0f, 0.0f}, "No geometry under the probe, or the gather did not run"},
}};

constexpr std::array<visualization_swatch, 4> k_legend_gi_temporal = {{
    {{0.6f, 0.0f, 0.0f}, "1-2 frames integrated - effectively unfiltered; fireflies live here"},
    {{1.0f, 0.5f, 0.0f}, "Re-converging"},
    {{0.1f, 1.0f, 0.2f}, "At or near the slow cap - a settled pixel"},
    {{0.1f, 0.2f, 1.0f}, "Blue lift: the moving-hit share is shortening this window on purpose"},
}};

constexpr std::array<visualization_swatch, 6> k_legend_gi_temporal_cause = {{
    {{0.05f, 0.35f, 0.1f}, "No cause: the count grew this frame, or sits at the settings window"},
    {{1.0f, 1.0f, 1.0f}, "Fresh: no usable history (first frame, off-screen last frame, disocclusion)"},
    {{1.0f, 0.1f, 0.05f}, "Dirty region: a placement changed nearby and collapsed the slow lane"},
    {{0.1f, 0.3f, 1.0f}, "Camera motion: the screen-share weighted collapse"},
    {{1.0f, 0.1f, 1.0f}, "Moving hits: the probe's rays hit moving geometry"},
    {{1.0f, 0.9f, 0.1f}, "Change detector: the slow lane snapped to the fast one"},
}};

constexpr std::array<visualization_swatch, 5> k_legend_gi_emitter_share = {{
    {{1.0f, 0.0f, 0.0f}, "Share of the probe's gathered energy the AIMED emitter rays delivered"},
    {{0.0f, 1.0f, 0.0f}, "Aimed rays over traced rays"},
    {{0.0f, 0.0f, 1.0f}, "Emitters selected over GI_EMISSIVE_NEE_PER_PROBE"},
    {{0.25f, 0.25f, 0.25f}, "Interpolated probe - no rays of its own"},
    {{0.0f, 0.0f, 0.0f}, "No geometry under the probe, or no emitter in reach"},
}};

// -----------------------------------------------------------------------------
// Groups
// -----------------------------------------------------------------------------

constexpr std::array<visualization_group_entry, 6> k_visualization_groups = {{
    {visualization_group::surface,
     "surface",
     ICON_MDI_LAYERS,
     "G-Buffer",
     "Surface attributes exactly as the geometry pass wrote them, before any lighting."},
    {visualization_group::occlusion,
     "occlusion",
     ICON_MDI_BLUR,
     "Ambient Occlusion",
     "The occlusion chain: the baked material term, the GTAO pass, and the specular term "
     "derived from them. These feed the indirect lighting only, never direct light."},
    {visualization_group::lighting,
     "lighting",
     ICON_MDI_LIGHTBULB,
     "Lighting",
     "The intermediate lighting buffers the deferred passes produce and the indirect pass "
     "consumes."},
    {visualization_group::motion,
     "motion",
     ICON_MDI_RUN_FAST,
     "Motion",
     "Screen-space motion. Selecting a view here forces the velocity buffer to be produced "
     "even when no other consumer (TAA) is active."},
    {visualization_group::distance_field,
     "distance_field",
     ICON_MDI_CUBE_SCAN,
     "Distance Fields",
     "Sphere-traced from the camera through the resident distance fields - NOT the raster "
     "image. Rays that hit nothing leave the shaded scene showing through. Needs the surface "
     "cache enabled with at least one resident field, otherwise nothing is drawn."},
    {visualization_group::global_illumination,
     "global_illumination",
     ICON_MDI_LIGHTBULB_ON_OUTLINE,
     "Global Illumination",
     "The GI caches read at a traced hit, exactly as a gather ray reads them. Same trace and "
     "the same preconditions as the Distance Fields group."},
}};

// -----------------------------------------------------------------------------
// Modes
//
// Menu order: "full" first, then each group's modes contiguously in group order.
// get_visualization_modes(group) relies on that contiguity.
// -----------------------------------------------------------------------------

constexpr std::array<visualization_mode_entry, 42> k_visualization_modes = {{
    {visualization_mode::full,
     visualization_group::none,
     "full",
     "Full",
     "The normal render. Turns every debug visualization off.",
     {}},

    // -- G-Buffer -------------------------------------------------------------
    {visualization_mode::base_color,
     visualization_group::surface,
     "base_color",
     "Base Color",
     "Material albedo, shown sRGB-encoded. No lighting, no ambient occlusion.",
     {}},
    {visualization_mode::diffuse_color,
     visualization_group::surface,
     "diffuse_color",
     "Diffuse Color",
     "Base color with the metal fraction removed: base_color * (1 - metalness). What the "
     "diffuse lobe is tinted by.",
     k_legend_diffuse_color},
    {visualization_mode::specular_color,
     visualization_group::surface,
     "specular_color",
     "Specular Color (F0)",
     "Normal-incidence reflectance F0, sRGB-encoded. Metals show their own base color; every "
     "dielectric shares one flat grey.",
     k_legend_specular_color},
    {visualization_mode::normals,
     visualization_group::surface,
     "normals",
     "World Normal",
     "The G-Buffer world-space normal written RAW (-1..1), not remapped to 0..1.",
     k_legend_normals},
    {visualization_mode::roughness,
     visualization_group::surface,
     "roughness",
     "Roughness",
     "Perceptual roughness as greyscale: black = 0 (mirror), white = 1 (fully rough).",
     {}},
    {visualization_mode::metalness,
     visualization_group::surface,
     "metalness",
     "Metalness",
     "Metalness as greyscale: black = 0 (dielectric), white = 1 (metal).",
     {}},
    {visualization_mode::emissive_color,
     visualization_group::surface,
     "emissive_color",
     "Emissive Color",
     "Emissive radiance written by the geometry pass. Black where a surface emits nothing.",
     {}},
    {visualization_mode::subsurface_color,
     visualization_group::surface,
     "subsurface_color",
     "Subsurface Color",
     "The subsurface tint channel, sRGB-encoded. Black on materials without subsurface.",
     {}},
    {visualization_mode::depth,
     visualization_group::surface,
     "depth",
     "Depth (Raw)",
     "Raw device depth as greyscale. Non-linear: near geometry spans most of the range and "
     "distance compresses hard, so far detail is expected to look flat.",
     {}},

    // -- Ambient Occlusion ----------------------------------------------------
    {visualization_mode::ambient_occlusion,
     visualization_group::occlusion,
     "ambient_occlusion",
     "G-Buffer AO",
     "The AO channel packed into the G-Buffer alpha: material and baked occlusion, multiplied "
     "in place by ASSAO when that pass runs - so this is NOT a pure material readout. "
     "White = unoccluded.",
     {}},
    {visualization_mode::gtao,
     visualization_group::occlusion,
     "gtao",
     "GTAO Visibility",
     "Ground-truth ambient occlusion visibility from the GTAO pass. White = fully visible, "
     "black = fully occluded. Uniform white means the GTAO pass produced nothing.",
     {}},
    {visualization_mode::gtao_bent_normal,
     visualization_group::occlusion,
     "gtao_bent_normal",
     "GTAO Bent Normal",
     "The GTAO pass world-space bent normal, encoded n * 0.5 + 0.5, so an unoccluded surface "
     "reads as its normal shifted into the 0..1 range. Flat WHITE = the pass produced nothing "
     "and the white fallback texture is bound.",
     {}},
    {visualization_mode::specular_occlusion,
     visualization_group::occlusion,
     "specular_occlusion",
     "Specular Occlusion",
     "The derived specular occlusion term (AO, roughness and view angle, times the GTAO cone "
     "when a bent normal is bound). White = reflections arrive unoccluded.",
     {}},

    // -- Lighting -------------------------------------------------------------
    {visualization_mode::irradiance,
     visualization_group::lighting,
     "irradiance",
     "Environment Irradiance (SH)",
     "Sky and reflection-probe irradiance evaluated from the SH probe along each pixel's "
     "world normal - the ambient term before GI replaces it.",
     {}},
    {visualization_mode::indirect_diffuse,
     visualization_group::lighting,
     "indirect_diffuse",
     "Indirect Diffuse (GI / SSIL)",
     "The buffer feeding the indirect diffuse slot - GI_RESOLVE when the surface cache runs, "
     "SSIL otherwise - scaled by PI and its own replacement weight. Black = neither ran.",
     {}},
    {visualization_mode::reflections,
     visualization_group::lighting,
     "reflections",
     "Reflections (RBUFFER)",
     "The specular reflection buffer: screen-space reflections composited over the GI "
     "reflection tier. This is what the indirect pass mixes in as specular.",
     {}},
    {visualization_mode::reflection_coverage,
     visualization_group::lighting,
     "reflection_coverage",
     "Reflection Coverage",
     "The reflection buffer alpha channel: how strongly it replaces the probe specular. "
     "White = fully reflection-driven, black = probe only.",
     {}},

    // -- Motion ---------------------------------------------------------------
    {visualization_mode::velocity,
     visualization_group::motion,
     "velocity",
     "Motion Vectors",
     "The velocity buffer: hue = direction of screen motion, brightness = magnitude, with 8 "
     "pixels of motion mapped to full brightness.",
     k_legend_velocity},

    // -- Distance Fields ------------------------------------------------------
    {visualization_mode::sdf_normals,
     visualization_group::distance_field,
     "sdf_normals",
     "Field Surface",
     "Shades the traced isosurface by its gradient normal with a headlight. Geometry missing "
     "here is missing from every distance-field consumer.",
     {}},
    {visualization_mode::sdf_step_count,
     visualization_group::distance_field,
     "sdf_step_count",
     "Trace Step Count",
     "Heat map of sphere-trace steps per pixel - where the fields refuse to let rays skip. A "
     "ray that hits nothing draws nothing, so the shaded scene shows through.",
     k_legend_sdf_step_count},
    {visualization_mode::sdf_cascade_levels,
     visualization_group::distance_field,
     "sdf_cascade_levels",
     "Cascade Levels",
     "Which global cascade level answers at the traced surface. A smooth gradient between two "
     "colors is the cross-fade band working; a hard edge means the fade is off.",
     k_legend_sdf_cascade_levels},
    {visualization_mode::sdf_clipmap,
     visualization_group::distance_field,
     "sdf_clipmap",
     "Clipmap Only",
     "Traces the global cascade ALONE with the per-instance fields disabled, shaded by normal. "
     "A fault in the cascade is invisible in the combined trace.",
     {}},
    {visualization_mode::sdf_headers,
     visualization_group::distance_field,
     "sdf_headers",
     "Field Headers",
     "Paints each resident field's bounds with its header channels: red = voxel size x20, "
     "green = grid dimension / 256, blue = 1 when the header carries a voxel size. A readout, "
     "not a verdict - it exists so a header that never arrived reads black rather than being "
     "inferred from a wrong-looking trace. First field in buffer order, not depth sorted.",
     k_legend_sdf_headers},
    {visualization_mode::sdf_brick_probe,
     visualization_group::distance_field,
     "sdf_brick_probe",
     "Brick Probe",
     "What the brick indirection resolves to 0.05 units inside each field's bounds. Red is a "
     "flag; green is the raw encoded distance texel there, so a healthy field reads mostly "
     "green. A fault looks like per-pixel noise, not like the presence of green. Answers the "
     "first field in buffer order the ray enters, so it is not depth sorted.",
     k_legend_sdf_brick_probe},
    {visualization_mode::sdf_bounds_entry,
     visualization_group::distance_field,
     "sdf_bounds_entry",
     "Bounds Entry Sample",
     "Classifies the FIRST field sample of the march, at the bounds entry point - the only "
     "place instance scale and the hit threshold are applied. Answers the first field in "
     "buffer order the ray enters, so it is not depth sorted.",
     k_legend_sdf_bounds_entry},

    // -- Global Illumination --------------------------------------------------
    {visualization_mode::gi_direct_lighting,
     visualization_group::global_illumination,
     "gi_direct_lighting",
     "Direct Lighting (Traced)",
     "Direct lighting evaluated at the traced hit from the resident light buffer, shadowed by "
     "tracing the fields toward each light. Neutral albedo, so this is the lighting alone.",
     {}},
    {visualization_mode::gi_voxel_albedo,
     visualization_group::global_illumination,
     "gi_voxel_albedo",
     "Voxel Albedo",
     "The attribute-voxel albedo at the traced hit - the surface color the GI cache carries.",
     k_legend_gi_voxel_albedo},
    {visualization_mode::gi_light_voxels,
     visualization_group::global_illumination,
     "gi_light_voxels",
     "Light Voxels",
     "The lit voxel cache at the traced hit, through the same reader a gather ray uses. This is "
     "the radiance GI redistributes; judge interior darkness here, not in the tonemapped image.",
     k_legend_gi_light_voxels},
    {visualization_mode::gi_world_probes,
     visualization_group::global_illumination,
     "gi_world_probes",
     "World Probe Irradiance",
     "World-probe irradiance interpolated at the traced hit through the full DDGI weight chain "
     "- what the bounce term and shortened gather rays read.",
     k_legend_gi_world_probes},
    {visualization_mode::gi_probe_sky,
     visualization_group::global_illumination,
     "gi_probe_sky",
     "Probe Sky Fraction",
     "How much of the probe answer at the hit is SKY, from the finest covering level alone "
     "(no far blend). A sealed interior must read dark green; warm means sky enters the cage.",
     k_legend_gi_probe_sky},
    {visualization_mode::gi_sun_tiers,
     visualization_group::global_illumination,
     "gi_sun_tiers",
     "Sun Visibility Tiers",
     "Which tier answers SUN visibility per voxel face. While active this REPLACES the light "
     "volume radiance with tier colors, so GI ingests them - diagnostic only.",
     k_legend_gi_sun_tiers},
    {visualization_mode::gi_vis_memo,
     visualization_group::global_illumination,
     "gi_vis_memo",
     "Bounce Visibility Memo",
     "The live bounce visibility-memo transaction per face - the instrument for whether the "
     "memo is actually hitting. Runs the real load / miss-march / restamp path.",
     k_legend_gi_vis_memo},
    {visualization_mode::gi_attr_emissive,
     visualization_group::global_illumination,
     "gi_attr_emissive",
     "Voxel Emissive",
     "Emitted radiance in the attribute volume at the traced hit - what a gather ray reads as "
     "emission, and the only view of the emissive texture-mean scaling.",
     k_legend_gi_attr_emissive},
    {visualization_mode::gi_cage_health,
     visualization_group::global_illumination,
     "gi_cage_health",
     "Probe Cage Health",
     "How much of the world-probe cage survived at the traced hit, after the dead-probe gate, "
     "Chebyshev and the field march. Red is where the lattice left nothing usable.",
     k_legend_gi_cage_health},
    {visualization_mode::gi_dirty_regions,
     visualization_group::global_illumination,
     "gi_dirty_regions",
     "Dirty Regions",
     "Where the temporal is flushing accumulated light because a placement moved, appeared, "
     "vanished or changed material. Only the regions that fit the shader budget are shown.",
     k_legend_gi_dirty_regions},
    {visualization_mode::gi_probe_lattice,
     visualization_group::global_illumination,
     "gi_probe_lattice",
     "Probe Lattice",
     "Only the level-0 probes, drawn as spheres where they actually sit and composited over the "
     "normal image. Blue is unoccupied (no geometry within reach); the share of blue is the "
     "lattice's occupancy.",
     k_legend_gi_probe_lattice},
    {visualization_mode::gi_screen_probes,
     visualization_group::global_illumination,
     "gi_screen_probes",
     "Screen Probes",
     "Where the adaptive gather placed a probe, whether it traced or interpolated it, and the "
     "ray budget it spent - a cost map as much as a correctness one.",
     k_legend_gi_screen_probes},
    {visualization_mode::gi_temporal,
     visualization_group::global_illumination,
     "gi_temporal",
     "Temporal Health",
     "How many frames each pixel has actually integrated. Answers 'why is this noisy' and "
     "'why is this lagging': pinned-low pixels are being reset every frame.",
     k_legend_gi_temporal},
    {visualization_mode::gi_probe_tiers,
     visualization_group::global_illumination,
     "gi_probe_tiers",
     "Probe Ray Tiers",
     "Which tier answered each traced screen probe's rays, as a share per tile: the red share "
     "is the fraction of the trace group's lanes that idle while their neighbours march the SDF.",
     k_legend_gi_probe_tiers},
    {visualization_mode::gi_temporal_cause,
     visualization_group::global_illumination,
     "gi_temporal_cause",
     "Temporal Reset Cause",
     "Which mechanism limited each pixel's accumulation count this frame: fresh history, a "
     "dirty region, the camera-motion collapse, moving hits, or the change detector.",
     k_legend_gi_temporal_cause},
    {visualization_mode::gi_emitter_share,
     visualization_group::global_illumination,
     "gi_emitter_share",
     "Emitter Sampling Share",
     "Explicit emitter sampling per traced screen probe: the aimed rays' share of the probe's "
     "energy (red), of its rays (green), and the emitters it selected (blue).",
     k_legend_gi_emitter_share},
}};

// Drift guards: the enum is the editor-side mirror of the engine's debug pass ids.
static_assert(static_cast<int>(visualization_mode::sdf_normals) == rendering::deferred::debug_pass_sdf_normals,
              "visualization_mode drifted from deferred::debug_pass_sdf_normals");
static_assert(static_cast<int>(visualization_mode::sdf_brick_probe) == rendering::deferred::debug_pass_sdf_probe,
              "visualization_mode drifted from deferred::debug_pass_sdf_probe");
static_assert(static_cast<int>(visualization_mode::sdf_bounds_entry) == rendering::deferred::debug_pass_sdf_entry,
              "visualization_mode drifted from deferred::debug_pass_sdf_entry");
static_assert(static_cast<int>(visualization_mode::gi_direct_lighting) == rendering::deferred::debug_pass_sdf_direct,
              "visualization_mode drifted from deferred::debug_pass_sdf_direct");
static_assert(static_cast<int>(visualization_mode::gi_voxel_albedo) == rendering::deferred::debug_pass_sdf_attr_albedo,
              "visualization_mode drifted from deferred::debug_pass_sdf_attr_albedo");
static_assert(static_cast<int>(visualization_mode::gi_light_voxels) == rendering::deferred::debug_pass_sdf_light_voxels,
              "visualization_mode drifted from deferred::debug_pass_sdf_light_voxels");
static_assert(static_cast<int>(visualization_mode::gi_world_probes) == rendering::deferred::debug_pass_sdf_world_probes,
              "visualization_mode drifted from deferred::debug_pass_sdf_world_probes");
static_assert(static_cast<int>(visualization_mode::gi_sun_tiers) == rendering::deferred::debug_pass_sdf_sun_tiers,
              "visualization_mode drifted from deferred::debug_pass_sdf_sun_tiers");
static_assert(static_cast<int>(visualization_mode::gi_probe_sky) == rendering::deferred::debug_pass_sdf_probe_sky,
              "visualization_mode drifted from deferred::debug_pass_sdf_probe_sky");
static_assert(static_cast<int>(visualization_mode::gi_vis_memo) == rendering::deferred::debug_pass_sdf_vis_memo,
              "visualization_mode drifted from deferred::debug_pass_sdf_vis_memo");
static_assert(static_cast<int>(visualization_mode::velocity) == rendering::deferred::debug_pass_velocity,
              "visualization_mode drifted from deferred::debug_pass_velocity");
static_assert(static_cast<int>(visualization_mode::gtao) == rendering::deferred::debug_pass_gtao,
              "visualization_mode drifted from deferred::debug_pass_gtao");
static_assert(static_cast<int>(visualization_mode::gtao_bent_normal) == rendering::deferred::debug_pass_gtao_bent_normal,
              "visualization_mode drifted from deferred::debug_pass_gtao_bent_normal");
static_assert(static_cast<int>(visualization_mode::gi_attr_emissive) ==
                  rendering::deferred::debug_pass_gi_attr_emissive,
              "visualization_mode drifted from deferred::debug_pass_gi_attr_emissive");
static_assert(static_cast<int>(visualization_mode::gi_cage_health) == rendering::deferred::debug_pass_gi_cage_health,
              "visualization_mode drifted from deferred::debug_pass_gi_cage_health");
static_assert(static_cast<int>(visualization_mode::gi_dirty_regions) ==
                  rendering::deferred::debug_pass_gi_dirty_regions,
              "visualization_mode drifted from deferred::debug_pass_gi_dirty_regions");
static_assert(static_cast<int>(visualization_mode::gi_probe_lattice) ==
                  rendering::deferred::debug_pass_gi_probe_lattice,
              "visualization_mode drifted from deferred::debug_pass_gi_probe_lattice");
static_assert(static_cast<int>(visualization_mode::gi_screen_probes) ==
                  rendering::deferred::debug_pass_gi_screen_probes,
              "visualization_mode drifted from deferred::debug_pass_gi_screen_probes");
static_assert(static_cast<int>(visualization_mode::gi_temporal) == rendering::deferred::debug_pass_gi_temporal,
              "visualization_mode drifted from deferred::debug_pass_gi_temporal");
static_assert(static_cast<int>(visualization_mode::gi_probe_tiers) == rendering::deferred::debug_pass_gi_probe_tiers,
              "visualization_mode drifted from deferred::debug_pass_gi_probe_tiers");
static_assert(static_cast<int>(visualization_mode::gi_temporal_cause) ==
                  rendering::deferred::debug_pass_gi_temporal_cause,
              "visualization_mode drifted from deferred::debug_pass_gi_temporal_cause");
static_assert(static_cast<int>(visualization_mode::gi_emitter_share) ==
                  rendering::deferred::debug_pass_gi_emitter_share,
              "visualization_mode drifted from deferred::debug_pass_gi_emitter_share");

} // namespace

auto get_visualization_modes() -> hpp::span<const visualization_mode_entry>
{
    return {k_visualization_modes.data(), k_visualization_modes.size()};
}

auto get_visualization_groups() -> hpp::span<const visualization_group_entry>
{
    return {k_visualization_groups.data(), k_visualization_groups.size()};
}

auto get_visualization_modes(visualization_group group) -> hpp::span<const visualization_mode_entry>
{
    // The table stores each group's modes contiguously, so the run is [first, last).
    size_t first = k_visualization_modes.size();
    size_t last = 0;
    for(size_t i = 0; i < k_visualization_modes.size(); ++i)
    {
        if(k_visualization_modes[i].group != group)
        {
            continue;
        }
        first = std::min(first, i);
        last = i + 1;
    }
    if(first >= last)
    {
        return {};
    }
    return {k_visualization_modes.data() + first, last - first};
}

auto find_visualization_group(visualization_group group) -> const visualization_group_entry*
{
    for(const auto& entry : k_visualization_groups)
    {
        if(entry.group == group)
        {
            return &entry;
        }
    }
    return nullptr;
}

auto find_visualization_group(std::string_view name) -> const visualization_group_entry*
{
    for(const auto& entry : k_visualization_groups)
    {
        if(name == entry.name)
        {
            return &entry;
        }
    }
    return nullptr;
}

auto find_visualization_mode(int value) -> const visualization_mode_entry*
{
    for(const auto& entry : k_visualization_modes)
    {
        if(static_cast<int>(entry.mode) == value)
        {
            return &entry;
        }
    }
    return nullptr;
}

auto find_visualization_mode(std::string_view name) -> const visualization_mode_entry*
{
    for(const auto& entry : k_visualization_modes)
    {
        if(name == entry.name)
        {
            return &entry;
        }
    }
    return nullptr;
}

auto to_string(visualization_mode mode) -> const char*
{
    const auto* entry = find_visualization_mode(static_cast<int>(mode));
    return entry != nullptr ? entry->name : "unknown";
}

} // namespace unravel
