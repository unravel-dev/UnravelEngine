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
//   engine_data/data/shaders/lumen/fs_lumen_scene_debug.sc     (Lumen views)
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

constexpr std::array<visualization_swatch, 6> k_legend_exposure = {{
    {{0.20f, 0.95f, 0.35f}, "Trace: log2 adapted exposure over the last 256 frames (oldest left)"},
    {{0.95f, 0.85f, 0.15f}, "Trace: log2 target exposure - a gap to green is adaptation in flight"},
    {{0.35f, 0.45f, 0.60f}, "Histogram: the bin's share of the metered weight (full bar = 5%)"},
    {{1.0f, 1.0f, 1.0f}, "The metered log2 luminance the percentile trim produced"},
    {{0.15f, 0.80f, 0.90f}, "The neutral point: EV100 == compensation, where exposure is 1"},
    {{0.85f, 0.15f, 0.10f}, "The min / max EV100 clamps; the metered marker resting on one = held"},
}};

// Lumen views: engine_data/data/shaders/lumen/fs_lumen_scene_debug.sc (the scene views) and
// engine_data/data/shaders/lumen/fs_lumen_visualize_screen.sc (the dedicated reflection rays, the screen probe
// frames). The debug colours of the scene views go through the lit image's tone map as UE's do, so on screen they
// read slightly darker than these swatches.

constexpr std::array<visualization_swatch, 1> k_legend_lumen_scene = {{
    {{0.0f, 0.0f, 0.0f}, "No card covers the hit: a ray that ends here finds nothing in the surface cache."},
}};

constexpr std::array<visualization_swatch, 2> k_legend_lumen_surface_cache = {{
    {{1.0f, 0.0f, 1.0f}, "The objects at the hit have cards, but none of the cards covers this point."},
    {{1.0f, 1.0f, 0.0f},
     "None of the objects at the hit has cards: they were left out of the surface cache, for example "
     "because they are too small for the card resolution at their distance."},
}};

constexpr std::array<visualization_swatch, 6> k_legend_lumen_object_grid = {{
    {{0.0f, 1.0f, 0.0f}, "A card of a listed object covers the hit."},
    {{1.0f, 0.0f, 1.0f}, "A card's page is captured, but the card's depth disagrees with the hit."},
    {{0.0f, 1.0f, 1.0f}, "The hit lies inside a card's bounds, but the card's page is not captured yet."},
    {{1.0f, 1.0f, 0.0f}, "A card faces the hit's normal, but the hit lies outside the card's bounds."},
    {{1.0f, 0.0f, 0.0f}, "No card of the listed objects faces the hit's normal."},
    {{0.3f, 0.3f, 0.3f}, "The object grid lists no object at this point."},
}};

constexpr std::array<visualization_swatch, 3> k_legend_lumen_reflection_rays = {{
    {{1.0f, 0.0f, 0.0f}, "A perfectly smooth surface. It traces its own reflection rays."},
    {{0.68f, 0.0f, 0.0f}, "A surface just below the traced-roughness limit. It still traces reflection rays."},
    {{0.5f, 0.5f, 0.5f},
     "A rougher surface, shaded in grey. It takes its specular reflection from the screen probes instead."},
}};

constexpr std::array<visualization_swatch, 3> k_legend_lumen_direct_lighting_updates = {{
    {{1.0f, 1.0f, 1.0f}, "The page's direct lighting was updated this frame."},
    {{1.0f, 0.0f, 0.0f}, "Updated last frame; the colour turns blue as the frames since pile up."},
    {{0.0f, 0.0f, 1.0f}, "Not updated for 8 frames or more."},
}};

constexpr std::array<visualization_swatch, 3> k_legend_lumen_indirect_lighting_updates = {{
    {{1.0f, 1.0f, 1.0f}, "The page's indirect lighting was updated this frame."},
    {{1.0f, 0.0f, 0.0f}, "Updated last frame; the colour turns blue as the frames since pile up."},
    {{0.0f, 0.0f, 1.0f}, "Not updated for 16 frames or more."},
}};

constexpr std::array<visualization_swatch, 4> k_legend_lumen_radiosity_frames = {{
    {{1.0f, 1.0f, 1.0f}, "The tile's radiosity has accumulated 4 frames, the most it keeps."},
    {{0.0f, 0.0f, 0.0f}, "No frames yet, and the outline of every 8x8 tile."},
    {{1.0f, 0.0f, 1.0f}, "The objects at the hit have cards, but none of the cards covers this point."},
    {{1.0f, 1.0f, 0.0f}, "None of the objects at the hit has cards."},
}};

constexpr std::array<visualization_swatch, 2> k_legend_lumen_screen_probe_frames = {{
    {{1.0f, 0.0f, 0.0f}, "The gather's temporal filter accumulates its maximum number of frames here."},
    {{0.5f, 0.5f, 0.5f}, "Grey shows through where it accumulates fewer, down to none: a new or disoccluded pixel."},
}};

constexpr std::array<visualization_swatch, 7> k_legend_lumen_card_coverage = {{
    {{0.0f, 1.0f, 0.0f}, "A captured card covers the hit and agrees with its depth."},
    {{1.0f, 0.0f, 1.0f}, "A card's page is captured, but the card's depth disagrees with the hit."},
    {{0.0f, 1.0f, 1.0f}, "The hit lies inside a card's bounds, but the card's page is not captured yet."},
    {{1.0f, 1.0f, 0.0f}, "A card faces the hit's normal, but the hit lies outside the card's bounds."},
    {{1.0f, 0.0f, 0.0f}, "None of the object's resident cards faces the hit's normal."},
    {{0.0f, 0.0f, 1.0f}, "The object has no cards: none were built, or none survived the build."},
    {{0.3f, 0.3f, 0.3f}, "The hit belongs to no object the surface cache tracks."},
}};

// -----------------------------------------------------------------------------
// Groups
// -----------------------------------------------------------------------------

constexpr std::array<visualization_group_entry, 5> k_visualization_groups = {{
    {visualization_group::surface,
     "surface",
     ICON_MDI_LAYERS,
     "G-Buffer",
     "Surface attributes exactly as the geometry pass wrote them, before any lighting."},
    {visualization_group::occlusion,
     "occlusion",
     ICON_MDI_BLUR,
     "Ambient Occlusion",
     "The occlusion chain: the material AO times the screen-space AO (GTAO, or ASSAO when GTAO "
     "is off), and the specular term derived from it. These feed the indirect lighting only, "
     "never direct light."},
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
    {visualization_group::lumen,
     "lumen",
     ICON_MDI_CARDS_OUTLINE,
     "Global Illumination",
     "The scene as the global illumination traces it: the global distance field its rays march, and the surface "
     "cache of mesh cards that shades their hits. These views need global illumination enabled on the camera or a "
     "volume, because the surface cache updates only while it runs."},
}};

// -----------------------------------------------------------------------------
// Modes
//
// Menu order: "full" first, then each group's modes contiguously in group order.
// get_visualization_modes(group) relies on that contiguity.
// -----------------------------------------------------------------------------

constexpr auto k_visualization_modes = std::to_array<visualization_mode_entry>({
    {visualization_mode::full,
     visualization_group::none,
     "full",
     "Lit",
     "The lit image. Turns every debug view off.",
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
     "Ambient Occlusion",
     "The occlusion of the untraced indirect lighting (environment SH, reflection probes): the "
     "material AO from the G-Buffer times the screen-space AO (the short-range AO of the global "
     "illumination while it runs, otherwise GTAO, or ASSAO when GTAO is off), before the diffuse multi-bounce. The "
     "GI takes the same; SSIL and traced reflections take only the material AO. White = unoccluded.",
     {}},
    {visualization_mode::ao_bent_normals,
     visualization_group::occlusion,
     "ao_bent_normals",
     "AO Bent Normals",
     "The world-space bent normal of the screen-space AO, encoded n * 0.5 + 0.5, so an "
     "unoccluded surface reads as its normal shifted into the 0..1 range. Flat WHITE = no bent "
     "normal: neither GTAO nor the global illumination's short-range AO runs (ASSAO has none).",
     {}},
    {visualization_mode::specular_occlusion,
     visualization_group::occlusion,
     "specular_occlusion",
     "Specular Occlusion",
     "The specular occlusion of the untraced reflections (probes, sky, the rough specular of the global "
     "illumination): the "
     "share of the GGX lobe inside the visibility cone of the ambient occlusion, around its bent "
     "normal, with the multi-bounce of F0 (tinted on metals). SSR and the traced reflections "
     "take only the material AO's. White = reflections arrive unoccluded.",
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
     "What the indirect diffuse adds to an 18% grey surface: the GI resolve (SSIL when no GI runs) times its "
     "diffuse occlusion, at the frame's exposure through the tone mapper. Black = neither ran.",
     {}},
    {visualization_mode::reflections,
     visualization_group::lighting,
     "reflections",
     "Reflections",
     "The indirect specular radiance, ahead of the environment BRDF: the traced layers (SSR, or the "
     "traced reflections of the global illumination) plus the share they leave of the probe layer (the "
     "reflection probes, or its rough specular) - completed with the environment SH where nothing covers it - each "
     "under its specular occlusion. This is what the indirect pass mixes in as specular.",
     {}},
    {visualization_mode::reflection_coverage,
     visualization_group::lighting,
     "reflection_coverage",
     "Reflection Coverage",
     "The share of the specular the traced reflections (SSR, or the global illumination's) cover; "
     "the rest comes from the probe layer. White = fully traced, black = probe layer only.",
     {}},
    {visualization_mode::exposure,
     visualization_group::lighting,
     "exposure",
     "Exposure (Visualize HDR)",
     "Auto exposure's own state, drawn as a panel over the lit image: the 256-frame trace of "
     "adapted against target exposure, and this frame's metering histogram on a log2 luminance "
     "axis with the metered value, the neutral point and the EV100 clamps marked.",
     k_legend_exposure},

    // -- Motion ---------------------------------------------------------------
    {visualization_mode::velocity,
     visualization_group::motion,
     "velocity",
     "Motion Vectors",
     "The velocity buffer: hue = direction of screen motion, brightness = magnitude, with 8 "
     "pixels of motion mapped to full brightness.",
     k_legend_velocity},

    // -- Lumen ----------------------------------------------------------------
    // UE's Show > Visualize > Lumen menu first, then its console-only r.Lumen.Visualize modes, then ours.
    {visualization_mode::lumen_overview,
     visualization_group::lumen,
     "lumen_overview",
     "Overview",
     "The lit image with three global illumination views tiled along its top and named in each tile: Geometry "
     "Normals, "
     "Reflection View and Surface Cache.",
     {}},
    {visualization_mode::lumen_performance_overview,
     visualization_group::lumen,
     "lumen_performance_overview",
     "Performance Overview",
     "The lit image with the Dedicated Reflection Rays view tiled in its top-left corner.",
     {}},
    {visualization_mode::lumen_scene,
     visualization_group::lumen,
     "lumen_scene",
     "GI Scene",
     "The scene as the global illumination's rays see it. The global distance field is traced from the camera, and "
     "each hit is "
     "shaded from the cards of the objects that the object grid lists there, exactly as the gather and "
     "reflection rays shade their own hits. A ray that hits nothing shows the sky.",
     k_legend_lumen_scene},
    {visualization_mode::lumen_reflection_view,
     visualization_group::lumen,
     "lumen_reflection_view",
     "Reflection View",
     "The GI Scene as the reflections see it: traced only as far as the reflection rays trace (the Global "
     "Illumination component's Max Trace Distance, 200 m by default).",
     k_legend_lumen_scene},
    {visualization_mode::lumen_surface_cache,
     visualization_group::lumen,
     "lumen_surface_cache",
     "Surface Cache",
     "The GI Scene lighting, with every hit the surface cache cannot shade marked in a solid color, so "
     "that gaps in the card coverage stand out.",
     k_legend_lumen_surface_cache},
    {visualization_mode::lumen_geometry_normals,
     visualization_group::lumen,
     "lumen_geometry_normals",
     "Geometry Normals",
     "The normal of the global distance field at each GI Scene hit: the geometry as the rays see it, "
     "with x, y and z mapped from -1..1 to 0..1.",
     {}},
    {visualization_mode::lumen_reflection_rays,
     visualization_group::lumen,
     "lumen_reflection_rays",
     "Dedicated Reflection Rays",
     "Which surfaces trace their own reflection rays. Every surface smoother than the traced-roughness limit "
     "(the Global Illumination component's Max Roughness To Trace, 0.4 by default) is drawn in red, brighter "
     "the smoother it is; the sky shows behind the scene.",
     k_legend_lumen_reflection_rays},
    {visualization_mode::lumen_scene_albedo,
     visualization_group::lumen,
     "lumen_scene_albedo",
     "Albedo",
     "The albedo the surface cache stores at each GI Scene hit, in place of its lighting. Use it to check "
     "that the cards captured each material's color.",
     k_legend_lumen_scene},
    {visualization_mode::lumen_scene_normals,
     visualization_group::lumen,
     "lumen_scene_normals",
     "Normals",
     "The normals the surface cache cards captured at each GI Scene hit, with x, y and z mapped from -1..1 "
     "to 0..1. Compare with Geometry Normals to see where a card's surface differs from the distance field's.",
     k_legend_lumen_scene},
    {visualization_mode::lumen_scene_emissive,
     visualization_group::lumen,
     "lumen_scene_emissive",
     "Emissive",
     "The emissive light the surface cache cards captured at each GI Scene hit.",
     k_legend_lumen_scene},
    {visualization_mode::lumen_card_weights,
     visualization_group::lumen,
     "lumen_card_weights",
     "Card Weights",
     "Which cards shade each GI Scene hit: one color per card, blended by each card's weight where several "
     "cover the hit, with dark lines through the centers of the card's 8x8 atlas tiles.",
     k_legend_lumen_scene},
    {visualization_mode::lumen_scene_direct,
     visualization_group::lumen,
     "lumen_scene_direct",
     "Direct Lighting",
     "The direct lighting the surface cache cards hold at each GI Scene hit: the light that reaches the "
     "surface straight from the sun and the local lights, with their shadows.",
     k_legend_lumen_scene},
    {visualization_mode::lumen_scene_indirect,
     visualization_group::lumen,
     "lumen_scene_indirect",
     "Indirect Lighting",
     "The indirect lighting the surface cache cards hold at each GI Scene hit: the light that has bounced "
     "between surfaces at least once.",
     k_legend_lumen_scene},
    {visualization_mode::lumen_direct_lighting_updates,
     visualization_group::lumen,
     "lumen_direct_lighting_updates",
     "Direct Lighting Updates",
     "How recently each card page's direct lighting was updated. Pages near the camera and in view are updated "
     "most often; the frame's budget decides the rest.",
     k_legend_lumen_direct_lighting_updates},
    {visualization_mode::lumen_indirect_lighting_updates,
     visualization_group::lumen,
     "lumen_indirect_lighting_updates",
     "Indirect Lighting Updates",
     "How recently each card page's indirect lighting (radiosity) was updated, on twice the time scale of the "
     "direct lighting updates.",
     k_legend_lumen_indirect_lighting_updates},
    {visualization_mode::lumen_screen_probe_frames,
     visualization_group::lumen,
     "lumen_screen_probe_frames",
     "Screen Probe Frames Accumulated",
     "How many frames the screen probe gather's temporal filter has accumulated at each pixel, in red over a "
     "grey copy of the image: full red at its maximum.",
     k_legend_lumen_screen_probe_frames},
    {visualization_mode::lumen_radiosity_frames,
     visualization_group::lumen,
     "lumen_radiosity_frames",
     "Radiosity Frames Accumulated",
     "How many frames the radiosity of each 8x8 tile of the cards has accumulated, from black (none) to white "
     "(4, the most), with every tile outlined.",
     k_legend_lumen_radiosity_frames},
    {visualization_mode::lumen_card_atlas,
     visualization_group::lumen,
     "lumen_card_atlas",
     "Card Atlas",
     "The surface cache's physical atlas, fitted to the viewport: the albedo of every resident card page as "
     "it was captured.",
     {}},
    {visualization_mode::lumen_card_coverage,
     visualization_group::lumen,
     "lumen_card_coverage",
     "Card Coverage",
     "Each object's own mesh distance field, traced from the camera. Every hit is colored by how close that "
     "object's cards come to covering it, which shows where and why the cards miss a surface.",
     k_legend_lumen_card_coverage},
    {visualization_mode::lumen_object_grid,
     visualization_group::lumen,
     "lumen_object_grid",
     "Object Grid",
     "Each GI Scene hit, colored by how close the objects that the object grid lists there come to "
     "covering it with their cards. A hit that Card Coverage shows as covered but this view does not points "
     "to an object missing from the grid.",
     k_legend_lumen_object_grid},
});

// Drift guards: the enum is the editor-side mirror of the engine's debug pass ids.
static_assert(static_cast<int>(visualization_mode::velocity) == rendering::deferred::debug_pass_velocity,
              "visualization_mode drifted from deferred::debug_pass_velocity");
static_assert(static_cast<int>(visualization_mode::ao_bent_normals) == rendering::deferred::debug_pass_ao_bent_normals,
              "visualization_mode drifted from deferred::debug_pass_ao_bent_normals");
static_assert(static_cast<int>(visualization_mode::exposure) == rendering::deferred::debug_pass_exposure,
              "visualization_mode drifted from deferred::debug_pass_exposure");
static_assert(static_cast<int>(visualization_mode::lumen_scene) == rendering::deferred::debug_pass_lumen_scene,
              "visualization_mode drifted from deferred::debug_pass_lumen_scene");
static_assert(static_cast<int>(visualization_mode::lumen_card_atlas) ==
                  rendering::deferred::debug_pass_lumen_card_atlas,
              "visualization_mode drifted from deferred::debug_pass_lumen_card_atlas");
static_assert(static_cast<int>(visualization_mode::lumen_card_coverage) ==
                  rendering::deferred::debug_pass_lumen_card_coverage,
              "visualization_mode drifted from deferred::debug_pass_lumen_card_coverage");
static_assert(static_cast<int>(visualization_mode::lumen_scene_albedo) ==
                  rendering::deferred::debug_pass_lumen_scene_albedo,
              "visualization_mode drifted from deferred::debug_pass_lumen_scene_albedo");
static_assert(static_cast<int>(visualization_mode::lumen_object_grid) == rendering::deferred::debug_pass_lumen_object_grid,
              "visualization_mode drifted from deferred::debug_pass_lumen_object_grid");
static_assert(static_cast<int>(visualization_mode::lumen_surface_cache) ==
                  rendering::deferred::debug_pass_lumen_surface_cache,
              "visualization_mode drifted from deferred::debug_pass_lumen_surface_cache");
static_assert(static_cast<int>(visualization_mode::lumen_scene_direct) ==
                  rendering::deferred::debug_pass_lumen_scene_direct,
              "visualization_mode drifted from deferred::debug_pass_lumen_scene_direct");
static_assert(static_cast<int>(visualization_mode::lumen_scene_indirect) ==
                  rendering::deferred::debug_pass_lumen_scene_indirect,
              "visualization_mode drifted from deferred::debug_pass_lumen_scene_indirect");
static_assert(static_cast<int>(visualization_mode::lumen_reflection_rays) ==
                  rendering::deferred::debug_pass_lumen_reflection_rays,
              "visualization_mode drifted from deferred::debug_pass_lumen_reflection_rays");
static_assert(static_cast<int>(visualization_mode::lumen_reflection_view) ==
                  rendering::deferred::debug_pass_lumen_reflection_view,
              "visualization_mode drifted from deferred::debug_pass_lumen_reflection_view");
static_assert(static_cast<int>(visualization_mode::lumen_geometry_normals) ==
                  rendering::deferred::debug_pass_lumen_geometry_normals,
              "visualization_mode drifted from deferred::debug_pass_lumen_geometry_normals");
static_assert(static_cast<int>(visualization_mode::lumen_scene_normals) ==
                  rendering::deferred::debug_pass_lumen_scene_normals,
              "visualization_mode drifted from deferred::debug_pass_lumen_scene_normals");
static_assert(static_cast<int>(visualization_mode::lumen_scene_emissive) ==
                  rendering::deferred::debug_pass_lumen_scene_emissive,
              "visualization_mode drifted from deferred::debug_pass_lumen_scene_emissive");
static_assert(static_cast<int>(visualization_mode::lumen_card_weights) ==
                  rendering::deferred::debug_pass_lumen_card_weights,
              "visualization_mode drifted from deferred::debug_pass_lumen_card_weights");
static_assert(static_cast<int>(visualization_mode::lumen_direct_lighting_updates) ==
                  rendering::deferred::debug_pass_lumen_direct_lighting_updates,
              "visualization_mode drifted from deferred::debug_pass_lumen_direct_lighting_updates");
static_assert(static_cast<int>(visualization_mode::lumen_indirect_lighting_updates) ==
                  rendering::deferred::debug_pass_lumen_indirect_lighting_updates,
              "visualization_mode drifted from deferred::debug_pass_lumen_indirect_lighting_updates");
static_assert(static_cast<int>(visualization_mode::lumen_radiosity_frames) ==
                  rendering::deferred::debug_pass_lumen_radiosity_frames,
              "visualization_mode drifted from deferred::debug_pass_lumen_radiosity_frames");
static_assert(static_cast<int>(visualization_mode::lumen_screen_probe_frames) ==
                  rendering::deferred::debug_pass_lumen_screen_probe_frames,
              "visualization_mode drifted from deferred::debug_pass_lumen_screen_probe_frames");
static_assert(static_cast<int>(visualization_mode::lumen_overview) == rendering::deferred::debug_pass_lumen_overview,
              "visualization_mode drifted from deferred::debug_pass_lumen_overview");
static_assert(static_cast<int>(visualization_mode::lumen_performance_overview) ==
                  rendering::deferred::debug_pass_lumen_performance_overview,
              "visualization_mode drifted from deferred::debug_pass_lumen_performance_overview");

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
