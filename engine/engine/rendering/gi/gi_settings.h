#pragma once

#include <engine/rendering/gi/lumen_constants.h>

#include <cstdint>

namespace unravel
{

/**
 * @brief Global illumination settings: the GI's post-process settings, one group per stage. Distances are in metres.
 *
 * Authored on gi_component and blended across post-process volumes by gi_component::merge_into.
 */
struct gi_settings
{
    /// The screen probe gather: indirect diffuse lighting, and the rough reflections read from its probes.
    struct diffuse_settings
    {
        /// Scale of the gathered indirect lighting. 1 is physically based.
        float intensity = 1.0f;
        /// Scale of the final gather's quality: the rays each screen probe traces
        /// (lumen_pass::get_probe_trace_resolution), the full-resolution jitter and the probe spacing.
        float quality = 1.0f;
        /// Rays march the depth buffer before the distance field.
        bool screen_traces = true;
        /// How fast lighting changes reach the gather: the temporal accumulates fewer frames and the radiance cache
        /// re-traces more probes per frame as it rises.
        float update_speed = 1.0f;
        /// The farthest any GI ray travels, diffuse and reflection.
        float max_trace_distance = gi::lumen::LUMEN_MAX_TRACE_DISTANCE;

        auto operator==(const diffuse_settings&) const -> bool = default;
    };

    /// GI reflections: one traced ray per smooth pixel, the rough ones read from the gather's probes.
    struct reflection_settings
    {
        /// The GI owns the view's reflections; off leaves them to the screen-space reflections and the probes.
        bool enabled = true;
        /// Scale of the reflections' quality: the rays the resolve reuses per pixel, and one traced pixel in four at
        /// the lowest quality (lumen_pass::get_reflection_downsample_factor).
        float quality = 1.0f;
        /// Rays march the depth buffer before the distance field.
        bool screen_traces = true;
        /// Pixels below this roughness trace a ray.
        float max_roughness_to_trace = gi::lumen::LUMEN_MAX_ROUGHNESS_TO_TRACE;

        auto operator==(const reflection_settings&) const -> bool = default;
    };

    /// The GI's short-range ambient occlusion: the contact detail below the probe spacing.
    struct ambient_occlusion_settings
    {
        /// Views with GI apply the short-range AO in place of the screen-space AO.
        bool enabled = true;
        /// Strength of the occlusion: 0 none, 1 full.
        float intensity = 1.0f;

        auto operator==(const ambient_occlusion_settings&) const -> bool = default;
    };

    /// The GI scene: the surface cache of cards every distance-field hit reads its lighting from.
    struct scene_settings
    {
        /// Scale of the surface cache lighting's quality: the rays and the spacing of the radiosity probes
        /// (lumen_pass::get_radiosity_layout).
        float lighting_quality = 1.0f;
        /// Scale of the size of the objects the scene keeps: the cards' minimum resolution and the smallest object
        /// the global distance field composes.
        float detail = 1.0f;
        /// How far the GI scene reaches. The cards are kept out to the global distance field's fixed 200 m reach,
        /// so they always cover it and smaller values change nothing.
        float view_distance = 200.0f;
        /// How fast lighting changes reach the cards: the share of the atlas the direct lighting and the radiosity
        /// relight per frame.
        float lighting_update_speed = 1.0f;
        /// Scale of the cards' texel density and resolution limits.
        float surface_cache_resolution = 1.0f;

        auto operator==(const scene_settings&) const -> bool = default;
    };

    /// The global distance field the GI traces beyond the screen.
    struct distance_field_settings
    {
        /// Clipmap levels rebuilt per frame at most, finest first.
        uint32_t levels_per_update = 1;
        /// Width of the cross-fade from one level into the next, in voxels of the finer level.
        float level_blend_band = 4.0f;

        auto operator==(const distance_field_settings&) const -> bool = default;
    };

    diffuse_settings diffuse{};
    reflection_settings reflections{};
    ambient_occlusion_settings ambient_occlusion{};
    scene_settings scene{};
    distance_field_settings distance_field{};

    auto operator==(const gi_settings&) const -> bool = default;
};

} // namespace unravel
