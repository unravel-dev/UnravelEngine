#include "gi_component.hpp"

#include <engine/rendering/gi/global_sdf_clipmap.h>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(gi_settings::diffuse_settings)
{
    using settings = gi_settings::diffuse_settings;

    entt::meta_factory<settings>{}
        .type("gi_settings::diffuse_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_settings::diffuse_settings"},
            entt::attribute{"pretty_name", "Diffuse"},
        })
        .data<&settings::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip",
                            "Multiplier on the indirect lighting gathered by the screen probes, including the "
                            "rough reflections they provide. 1 is physically based."},
        })
        .data<&settings::quality>("quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "quality"},
            entt::attribute{"pretty_name", "Quality"},
            entt::attribute{"min", 0.25f},
            entt::attribute{"max", 8.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip",
                            "The final gather's quality: how many rays each screen probe traces - 4 x 4 below 0.39, "
                            "8 x 8 up to 1.27 and 16 x 16 above. From 4 the interpolation between probes jitters "
                            "less, and from 6 the probes are twice as dense. Higher values reduce noise in the "
                            "indirect lighting at a much higher GPU cost and memory."},
        })
        .data<&settings::screen_traces>("screen_traces"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "screen_traces"},
            entt::attribute{"pretty_name", "Screen Traces"},
            entt::attribute{"tooltip",
                            "Traces rays through the depth buffer before the distance field, so visible geometry "
                            "contributes its exact shape and its lighting from the previous frame. This adds contact "
                            "detail but makes the lighting depend on what is on screen. When off, rays trace the "
                            "distance field only."},
        })
        .data<&settings::update_speed>("update_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "update_speed"},
            entt::attribute{"pretty_name", "Update Speed"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip",
                            "How quickly lighting changes reach the indirect lighting. Higher values accumulate fewer "
                            "frames over time and refresh more radiance cache probes per frame: changes appear "
                            "sooner, at the cost of more noise and GPU time."},
        })
        .data<&settings::max_trace_distance>("max_trace_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_trace_distance"},
            entt::attribute{"pretty_name", "Max Trace Distance"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 20000.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip",
                            "The farthest distance, in metres, that indirect lighting and reflection rays travel. "
                            "Distances that are too short let light leak into large enclosed spaces; longer "
                            "distances cost more GPU time."},
        });
}

REFLECT_INLINE(gi_settings::reflection_settings)
{
    using settings = gi_settings::reflection_settings;

    auto enabled_predicate = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            if(auto data = obj.try_cast<settings>())
            {
                return data->enabled;
            }
            return false;
        });

    entt::meta_factory<settings>{}
        .type("gi_settings::reflection_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_settings::reflection_settings"},
            entt::attribute{"pretty_name", "Reflections"},
        })
        .data<&settings::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip",
                            "Traces a reflection ray for every smooth surface and fills rough surfaces from the "
                            "screen probes, in place of the screen-space reflections and the reflection probes. "
                            "When off, those provide the reflections."},
        })
        .data<&settings::quality>("quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "quality"},
            entt::attribute{"pretty_name", "Quality"},
            entt::attribute{"predicate", enabled_predicate},
            entt::attribute{"min", 0.25f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip",
                            "The traced reflections' quality: above 1 each pixel reuses more of its neighbours' "
                            "rays (5 at 1, 10 at 2), and at 0.25 only one pixel of each 2 x 2 block traces a ray, "
                            "the others are reconstructed from it. Higher values reduce noise in glossy reflections "
                            "at a higher GPU cost."},
        })
        .data<&settings::screen_traces>("screen_traces"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "screen_traces"},
            entt::attribute{"pretty_name", "Screen Traces"},
            entt::attribute{"predicate", enabled_predicate},
            entt::attribute{"tooltip",
                            "Traces reflection rays through the depth buffer before the distance field, so visible "
                            "geometry reflects with its exact shape and lighting. When off, reflection rays trace "
                            "the distance field only."},
        })
        .data<&settings::max_roughness_to_trace>("max_roughness_to_trace"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_roughness_to_trace"},
            entt::attribute{"pretty_name", "Max Roughness To Trace"},
            entt::attribute{"predicate", enabled_predicate},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip",
                            "Surfaces smoother than this roughness trace their own reflection ray; rougher surfaces "
                            "take their reflections from the screen probes. Higher values sharpen glossy reflections "
                            "at a higher GPU cost."},
        });
}

REFLECT_INLINE(gi_settings::ambient_occlusion_settings)
{
    using settings = gi_settings::ambient_occlusion_settings;

    auto enabled_predicate = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            if(auto data = obj.try_cast<settings>())
            {
                return data->enabled;
            }
            return false;
        });

    entt::meta_factory<settings>{}
        .type("gi_settings::ambient_occlusion_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_settings::ambient_occlusion_settings"},
            entt::attribute{"pretty_name", "Ambient Occlusion"},
        })
        .data<&settings::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip",
                            "Applies short-range ambient occlusion: the contact shadowing finer than the screen probe "
                            "spacing. While on, it replaces the screen-space ambient occlusion (GTAO or ASSAO)."},
        })
        .data<&settings::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"predicate", enabled_predicate},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip",
                            "Strength of the short-range ambient occlusion: 0 applies none, 1 applies it in full."},
        });
}

REFLECT_INLINE(gi_settings::scene_settings)
{
    using settings = gi_settings::scene_settings;

    entt::meta_factory<settings>{}
        .type("gi_settings::scene_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_settings::scene_settings"},
            entt::attribute{"pretty_name", "Surface Cache"},
        })
        .data<&settings::lighting_quality>("lighting_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lighting_quality"},
            entt::attribute{"pretty_name", "Lighting Quality"},
            entt::attribute{"min", 0.25f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip",
                            "The quality of the cached surface lighting: how many rays each radiosity probe traces - "
                            "from 2 x 2 at 0.5 or below to 8 x 8 at 4 (4 x 4 at 1) - and, from 6, probes twice as "
                            "dense. Higher values reduce noise in the multi-bounce lighting, most visible in "
                            "reflections, at a higher GPU cost and memory."},
        })
        .data<&settings::detail>("detail"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "detail"},
            entt::attribute{"pretty_name", "Detail"},
            entt::attribute{"min", 0.25f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip",
                            "The size of the objects rays can see: higher values give smaller objects cards in the "
                            "surface cache and keep smaller objects in the distance field, so they show in the "
                            "indirect lighting and reflections, at a higher GPU cost. Lower values leave more small "
                            "objects out."},
        })
        .data<&settings::view_distance>("view_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "view_distance"},
            entt::attribute{"pretty_name", "View Distance"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 20000.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip",
                            "Distance from the camera, in metres, within which surfaces keep their cached lighting "
                            "for rays to read. Larger values extend indirect lighting and reflections to distant "
                            "geometry at a higher GPU and memory cost. The reach of the global distance field "
                            "(200 m) limits it."},
        })
        .data<&settings::lighting_update_speed>("lighting_update_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lighting_update_speed"},
            entt::attribute{"pretty_name", "Lighting Update Speed"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip",
                            "How quickly lighting changes reach the cached surface lighting. Higher values relight "
                            "a larger part of the surface cache each frame, so moving lights and changing materials "
                            "show sooner, at a higher GPU cost."},
        })
        .data<&settings::surface_cache_resolution>("surface_cache_resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface_cache_resolution"},
            entt::attribute{"pretty_name", "Surface Cache Resolution"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip",
                            "Scale of the texel density and resolution of the cached surfaces. Lower values save GPU "
                            "memory and capture time at the cost of coarser cached lighting."},
        });
}

REFLECT_INLINE(gi_settings::distance_field_settings)
{
    using settings = gi_settings::distance_field_settings;

    entt::meta_factory<settings>{}
        .type("gi_settings::distance_field_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_settings::distance_field_settings"},
            entt::attribute{"pretty_name", "Distance Field"},
        })
        .data<&settings::levels_per_update>("levels_per_update"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "levels_per_update"},
            entt::attribute{"pretty_name", "Levels Per Update"},
            entt::attribute{"min", 1},
            entt::attribute{"max", global_sdf_clipmap::level_count},
            entt::attribute{"tooltip",
                            "Maximum number of distance field levels rebuilt in one frame. Lower values spread the "
                            "rebuild cost over several frames, so moved geometry can take a few frames to appear in "
                            "the field; higher values react sooner with larger frame-time spikes."},
        })
        .data<&settings::level_blend_band>("level_blend_band"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "level_blend_band"},
            entt::attribute{"pretty_name", "Level Blend Band"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 16.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip",
                            "Width of the cross-fade between neighbouring distance field levels, in voxels of the "
                            "finer level. Levels are built independently, so their surfaces can differ by about one "
                            "coarse voxel; a band wider than that hides the seam. 0 switches levels abruptly, which "
                            "can pop as the camera moves."},
        });
}

REFLECT_INLINE(gi_settings)
{
    entt::meta_factory<gi_settings>{}
        .type("gi_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_settings"},
            entt::attribute{"pretty_name", "Settings"},
        })
        .data<&gi_settings::diffuse>("diffuse"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "diffuse"},
            entt::attribute{"pretty_name", "Diffuse"},
            entt::attribute{"tooltip", "Indirect diffuse lighting gathered by the screen probes."},
        })
        .data<&gi_settings::reflections>("reflections"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reflections"},
            entt::attribute{"pretty_name", "Reflections"},
            entt::attribute{"tooltip", "Traced reflections of smooth surfaces and probe reflections of rough ones."},
        })
        .data<&gi_settings::ambient_occlusion>("ambient_occlusion"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ambient_occlusion"},
            entt::attribute{"pretty_name", "Ambient Occlusion"},
            entt::attribute{"tooltip", "Short-range ambient occlusion below the screen probe spacing."},
        })
        .data<&gi_settings::scene>("scene"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scene"},
            entt::attribute{"pretty_name", "Surface Cache"},
            entt::attribute{"tooltip",
                            "The surface cache: the cached lighting of the scene's surfaces that rays read where "
                            "they hit the distance field."},
        })
        .data<&gi_settings::distance_field>("distance_field"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "distance_field"},
            entt::attribute{"pretty_name", "Distance Field"},
            entt::attribute{"tooltip", "The global distance field that rays trace beyond the screen."},
        });
}

SAVE_INLINE(gi_settings::diffuse_settings)
{
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    try_save(ar, ser20::make_nvp("quality", obj.quality));
    try_save(ar, ser20::make_nvp("screen_traces", obj.screen_traces));
    try_save(ar, ser20::make_nvp("update_speed", obj.update_speed));
    try_save(ar, ser20::make_nvp("max_trace_distance", obj.max_trace_distance));
}
SAVE_INSTANTIATE(gi_settings::diffuse_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_settings::diffuse_settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_settings::diffuse_settings)
{
    try_load(ar, ser20::make_nvp("intensity", obj.intensity));
    try_load(ar, ser20::make_nvp("quality", obj.quality));
    try_load(ar, ser20::make_nvp("screen_traces", obj.screen_traces));
    try_load(ar, ser20::make_nvp("update_speed", obj.update_speed));
    try_load(ar, ser20::make_nvp("max_trace_distance", obj.max_trace_distance));
}
LOAD_INSTANTIATE(gi_settings::diffuse_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_settings::diffuse_settings, ser20::iarchive_binary_t);

SAVE_INLINE(gi_settings::reflection_settings)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("quality", obj.quality));
    try_save(ar, ser20::make_nvp("screen_traces", obj.screen_traces));
    try_save(ar, ser20::make_nvp("max_roughness_to_trace", obj.max_roughness_to_trace));
}
SAVE_INSTANTIATE(gi_settings::reflection_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_settings::reflection_settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_settings::reflection_settings)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("quality", obj.quality));
    try_load(ar, ser20::make_nvp("screen_traces", obj.screen_traces));
    try_load(ar, ser20::make_nvp("max_roughness_to_trace", obj.max_roughness_to_trace));
}
LOAD_INSTANTIATE(gi_settings::reflection_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_settings::reflection_settings, ser20::iarchive_binary_t);

SAVE_INLINE(gi_settings::ambient_occlusion_settings)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
}
SAVE_INSTANTIATE(gi_settings::ambient_occlusion_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_settings::ambient_occlusion_settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_settings::ambient_occlusion_settings)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("intensity", obj.intensity));
}
LOAD_INSTANTIATE(gi_settings::ambient_occlusion_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_settings::ambient_occlusion_settings, ser20::iarchive_binary_t);

SAVE_INLINE(gi_settings::scene_settings)
{
    try_save(ar, ser20::make_nvp("lighting_quality", obj.lighting_quality));
    try_save(ar, ser20::make_nvp("detail", obj.detail));
    try_save(ar, ser20::make_nvp("view_distance", obj.view_distance));
    try_save(ar, ser20::make_nvp("lighting_update_speed", obj.lighting_update_speed));
    try_save(ar, ser20::make_nvp("surface_cache_resolution", obj.surface_cache_resolution));
}
SAVE_INSTANTIATE(gi_settings::scene_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_settings::scene_settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_settings::scene_settings)
{
    try_load(ar, ser20::make_nvp("lighting_quality", obj.lighting_quality));
    try_load(ar, ser20::make_nvp("detail", obj.detail));
    try_load(ar, ser20::make_nvp("view_distance", obj.view_distance));
    try_load(ar, ser20::make_nvp("lighting_update_speed", obj.lighting_update_speed));
    try_load(ar, ser20::make_nvp("surface_cache_resolution", obj.surface_cache_resolution));
}
LOAD_INSTANTIATE(gi_settings::scene_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_settings::scene_settings, ser20::iarchive_binary_t);

SAVE_INLINE(gi_settings::distance_field_settings)
{
    try_save(ar, ser20::make_nvp("levels_per_update", obj.levels_per_update));
    try_save(ar, ser20::make_nvp("level_blend_band", obj.level_blend_band));
}
SAVE_INSTANTIATE(gi_settings::distance_field_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_settings::distance_field_settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_settings::distance_field_settings)
{
    try_load(ar, ser20::make_nvp("levels_per_update", obj.levels_per_update));
    try_load(ar, ser20::make_nvp("level_blend_band", obj.level_blend_band));
}
LOAD_INSTANTIATE(gi_settings::distance_field_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_settings::distance_field_settings, ser20::iarchive_binary_t);

SAVE_INLINE(gi_settings)
{
    try_save(ar, ser20::make_nvp("diffuse", obj.diffuse));
    try_save(ar, ser20::make_nvp("reflections", obj.reflections));
    try_save(ar, ser20::make_nvp("ambient_occlusion", obj.ambient_occlusion));
    try_save(ar, ser20::make_nvp("scene", obj.scene));
    try_save(ar, ser20::make_nvp("distance_field", obj.distance_field));
}
SAVE_INSTANTIATE(gi_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_settings)
{
    try_load(ar, ser20::make_nvp("diffuse", obj.diffuse));
    try_load(ar, ser20::make_nvp("reflections", obj.reflections));
    try_load(ar, ser20::make_nvp("ambient_occlusion", obj.ambient_occlusion));
    try_load(ar, ser20::make_nvp("scene", obj.scene));
    try_load(ar, ser20::make_nvp("distance_field", obj.distance_field));
}
LOAD_INSTANTIATE(gi_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_settings, ser20::iarchive_binary_t);

// --- Reflection + Serialization: gi_component ---

REFLECT(gi_component)
{
    entt::meta_factory<gi_component>{}
        .type("gi_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_component"},
            entt::attribute{"category", "Rendering/Post Processing"},
            entt::attribute{"pretty_name", "Global Illumination"},
        })
        .func<&component_meta<gi_component>::exists>("component_exists"_hs)
        .func<&component_meta<gi_component>::add>("component_add"_hs)
        .func<&component_meta<gi_component>::remove>("component_remove"_hs)
        .func<&component_meta<gi_component>::save>("component_save"_hs)
        .func<&component_meta<gi_component>::load>("component_load"_hs)
        .data<&gi_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip",
                            "Enables global illumination: multi-bounce indirect lighting, reflections and short-range "
                            "ambient occlusion, traced through the depth buffer and the scene's distance field.\n"
                            "When off, indirect diffuse falls back to SSIL when present, otherwise to the "
                            "environment probe."},
        })
        .data<&gi_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(gi_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(gi_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_component, ser20::oarchive_binary_t);

LOAD(gi_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(gi_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_component, ser20::iarchive_binary_t);

} // namespace unravel
