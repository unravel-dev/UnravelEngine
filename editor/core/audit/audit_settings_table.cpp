#include "audit_settings_table.h"

#include <logging/logging.h>

#include <algorithm>

namespace unravel::audit
{
namespace
{

/// The engine starts phone profiles without the effects a phone's graphics processor cannot afford at frame rate.
constexpr const char* PHONE_EFFECT_REASON =
    "The engine starts phone profiles without the effects a phone's graphics processor cannot afford at frame rate.";

auto describe_switch(bool value) -> std::string
{
    return value ? "on" : "off";
}

auto get_texture_side(texture_importer_meta::texture_size size) -> std::int64_t
{
    switch(size)
    {
        case texture_importer_meta::texture_size::size_32:
            return 32;
        case texture_importer_meta::texture_size::size_64:
            return 64;
        case texture_importer_meta::texture_size::size_128:
            return 128;
        case texture_importer_meta::texture_size::size_256:
            return 256;
        case texture_importer_meta::texture_size::size_512:
            return 512;
        case texture_importer_meta::texture_size::size_1024:
            return 1024;
        case texture_importer_meta::texture_size::size_4096:
            return 4096;
        case texture_importer_meta::texture_size::size_8192:
            return 8192;
        case texture_importer_meta::texture_size::size_16384:
            return 16384;
        // Project Default compiles at 2048 (asset_compiler::resolve_texture_parameters).
        case texture_importer_meta::texture_size::size_2048:
        case texture_importer_meta::texture_size::project_default:
        default:
            return 2048;
    }
}

auto describe_anti_aliasing(anti_aliasing_limit value) -> std::string
{
    switch(value)
    {
        case anti_aliasing_limit::none:
            return "None";
        case anti_aliasing_limit::fxaa:
            return "FXAA";
        case anti_aliasing_limit::temporal:
        default:
            return "Temporal";
    }
}

auto describe_shadow_resolution(sm_resolution value) -> std::string
{
    switch(value)
    {
        case sm_resolution::low:
            return "Low";
        case sm_resolution::medium:
            return "Medium";
        case sm_resolution::high:
            return "High";
        case sm_resolution::very_high:
        default:
            return "Very High";
    }
}

auto describe_gi_quality(gi_project_settings::quality_level value) -> std::string
{
    return value == gi_project_settings::quality_level::epic ? "Epic" : "High";
}

#define AUDIT_LIMIT_SWITCH(field, label_text, reason_text)                                                             \
    performance_setting                                                                                                \
    {                                                                                                                  \
        "graphics/limits/" #field, "Graphics > Allowed Features > " label_text, reason_text,                           \
            [](const settings& value) -> std::int64_t { return value.graphics.limits.field ? 1 : 0; },                 \
            [](const settings& value) { return describe_switch(value.graphics.limits.field); },                        \
            [](settings& to, const settings& from) { to.graphics.limits.field = from.graphics.limits.field; }          \
    }

auto make_settings() -> std::vector<performance_setting>
{
    std::vector<performance_setting> list{
        {"assets/texture/default_max_size",
         "Assets > Textures > Default Max Size",
         "Each doubling of a texture's side takes four times the memory.",
         [](const settings& value)
         {
             return get_texture_side(value.assets.texture.default_max_size);
         },
         [](const settings& value)
         {
             return std::to_string(get_texture_side(value.assets.texture.default_max_size));
         },
         [](settings& to, const settings& from)
         {
             to.assets.texture.default_max_size = from.assets.texture.default_max_size;
         }},
        {"global_illumination/global_illumination_quality",
         "Global Illumination > Global Illumination Quality",
         "The global illumination's full tiers are a desktop's.",
         [](const settings& value)
         {
             return static_cast<std::int64_t>(value.global_illumination.global_illumination_quality);
         },
         [](const settings& value)
         {
             return describe_gi_quality(value.global_illumination.global_illumination_quality);
         },
         [](settings& to, const settings& from)
         {
             to.global_illumination.global_illumination_quality = from.global_illumination.global_illumination_quality;
         }},
        {"global_illumination/reflection_quality",
         "Global Illumination > Reflection Quality",
         "The global illumination's full tiers are a desktop's.",
         [](const settings& value)
         {
             return static_cast<std::int64_t>(value.global_illumination.reflection_quality);
         },
         [](const settings& value)
         {
             return describe_gi_quality(value.global_illumination.reflection_quality);
         },
         [](settings& to, const settings& from)
         {
             to.global_illumination.reflection_quality = from.global_illumination.reflection_quality;
         }},
        {"global_illumination/surface_cache_atlas_size",
         "Global Illumination > Surface Cache Atlas",
         "The atlas takes about 34 MB at 1024, 134 MB at 2048 and 538 MB at 4096.",
         [](const settings& value)
         {
             return static_cast<std::int64_t>(value.global_illumination.surface_cache_atlas_size);
         },
         [](const settings& value)
         {
             return std::to_string(static_cast<std::uint32_t>(value.global_illumination.surface_cache_atlas_size));
         },
         [](settings& to, const settings& from)
         {
             to.global_illumination.surface_cache_atlas_size = from.global_illumination.surface_cache_atlas_size;
         }},
        {"graphics/particles/force_half_resolution",
         "Graphics > Particles > Force Half Resolution",
         "Particles drawn at half resolution fill a quarter of the pixels.",
         [](const settings& value) -> std::int64_t
         {
             // Off is the costlier value.
             return value.graphics.particles.force_half_resolution ? 0 : 1;
         },
         [](const settings& value)
         {
             return describe_switch(value.graphics.particles.force_half_resolution);
         },
         [](settings& to, const settings& from)
         {
             to.graphics.particles.force_half_resolution = from.graphics.particles.force_half_resolution;
         }},
        {"graphics/material_quality",
         "Graphics > Material Quality",
         "The Quality Switch nodes of graph materials take their cheaper input at Low, and browsers on phones run web "
         "builds too.",
         [](const settings& value) -> std::int64_t
         {
             return value.graphics.material_quality == material_graph_quality::high ? 1 : 0;
         },
         [](const settings& value) -> std::string
         {
             return value.graphics.material_quality == material_graph_quality::high ? "High" : "Low";
         },
         [](settings& to, const settings& from)
         {
             to.graphics.material_quality = from.graphics.material_quality;
         }},
        AUDIT_LIMIT_SWITCH(global_illumination,
                           "Global Illumination",
                           "Global illumination costs hundreds of milliseconds a frame on a phone's graphics processor."),
        AUDIT_LIMIT_SWITCH(screen_space_reflections,
                           "Screen Space Reflections",
                           "Screen space effects cost several milliseconds a frame on a phone."),
        AUDIT_LIMIT_SWITCH(screen_space_indirect_lighting,
                           "Screen Space Indirect Lighting",
                           "Screen space indirect lighting costs hundreds of milliseconds a frame on a phone's graphics "
                           "processor."),
        AUDIT_LIMIT_SWITCH(ambient_occlusion,
                           "Ambient Occlusion",
                           "Screen space effects cost several milliseconds a frame on a phone."),
        AUDIT_LIMIT_SWITCH(volumetric_fog, "Volumetric Fog", PHONE_EFFECT_REASON),
        AUDIT_LIMIT_SWITCH(soft_shadows, "Soft Shadows", PHONE_EFFECT_REASON),
        AUDIT_LIMIT_SWITCH(contact_shadows,
                           "Contact Shadows",
                           "Contact shadows keep code that holds registers in every light program."),
        AUDIT_LIMIT_SWITCH(subsurface,
                           "Subsurface",
                           "Subsurface scattering keeps code that holds registers in every light program."),
        AUDIT_LIMIT_SWITCH(volumetric_clouds,
                           "Volumetric Clouds",
                           "Volumetric clouds raymarch every sky pixel, where flat clouds cost next to nothing."),
        AUDIT_LIMIT_SWITCH(detailed_indirect_lighting,
                           "Detailed Indirect Lighting",
                           "The indirect lighting's finest detail runs on every lit pixel."),
        AUDIT_LIMIT_SWITCH(full_precision_light_targets,
                           "Full Precision Light Targets",
                           "RGBA16F targets are stored raw on tile-based graphics processors, where RG11B10F compresses."),
        AUDIT_LIMIT_SWITCH(per_frame_exposure_metering,
                           "Per Frame Exposure Metering",
                           "Exposure need not meter every frame: it keeps adapting from the last measurement."),
        AUDIT_LIMIT_SWITCH(decal_surface_channels,
                           "Decal Normals And Roughness",
                           "Decal normals and roughness add three targets and two passes."),
        AUDIT_LIMIT_SWITCH(translucent_scene_color,
                           "Translucency Scene Color",
                           "Refracting see-through surfaces copy the whole lit view once a frame they are drawn."),
        {"graphics/limits/translucent_lights",
         "Graphics > Allowed Features > Translucency Lights",
         "Each nearby light lights every pixel of every see-through surface of the translucency pass again.",
         [](const settings& value)
         {
             return static_cast<std::int64_t>(value.graphics.limits.translucent_lights);
         },
         [](const settings& value)
         {
             return std::to_string(value.graphics.limits.translucent_lights);
         },
         [](settings& to, const settings& from)
         {
             to.graphics.limits.translucent_lights = from.graphics.limits.translucent_lights;
         }},
        {"graphics/limits/max_anti_aliasing",
         "Graphics > Allowed Features > Max Anti-Aliasing",
         "Temporal anti-aliasing's extra passes cost several milliseconds a frame on a phone.",
         [](const settings& value)
         {
             return static_cast<std::int64_t>(value.graphics.limits.max_anti_aliasing);
         },
         [](const settings& value)
         {
             return describe_anti_aliasing(value.graphics.limits.max_anti_aliasing);
         },
         [](settings& to, const settings& from)
         {
             to.graphics.limits.max_anti_aliasing = from.graphics.limits.max_anti_aliasing;
         }},
        {"graphics/limits/max_shadow_resolution",
         "Graphics > Allowed Features > Max Shadow Resolution",
         PHONE_EFFECT_REASON,
         [](const settings& value)
         {
             return static_cast<std::int64_t>(value.graphics.limits.max_shadow_resolution);
         },
         [](const settings& value)
         {
             return describe_shadow_resolution(value.graphics.limits.max_shadow_resolution);
         },
         [](settings& to, const settings& from)
         {
             to.graphics.limits.max_shadow_resolution = from.graphics.limits.max_shadow_resolution;
         }},
        {"graphics/eviction/keep_mip_tails",
         "Graphics > GPU Eviction / Paging > Keep Mip Tails",
         "Without mip tails an evicted texture leaves the GPU whole and its first draw waits for all of it.",
         [](const settings& value) -> std::int64_t
         {
             // Off is the costlier value.
             return value.graphics.eviction.keep_mip_tails ? 0 : 1;
         },
         [](const settings& value)
         {
             return describe_switch(value.graphics.eviction.keep_mip_tails);
         },
         [](settings& to, const settings& from)
         {
             to.graphics.eviction.keep_mip_tails = from.graphics.eviction.keep_mip_tails;
         }},
        {"graphics/eviction/reduce_textures_in_use",
         "Graphics > GPU Eviction / Paging > Reduce Textures In Use",
         "Without it the textures on screen may hold more GPU memory than the budget, which a phone shares with "
         "the system.",
         [](const settings& value) -> std::int64_t
         {
             // Off is the costlier value.
             return value.graphics.eviction.reduce_textures_in_use ? 0 : 1;
         },
         [](const settings& value)
         {
             return describe_switch(value.graphics.eviction.reduce_textures_in_use);
         },
         [](settings& to, const settings& from)
         {
             to.graphics.eviction.reduce_textures_in_use = from.graphics.eviction.reduce_textures_in_use;
         }},
        {"graphics/eviction/max_upload_mb",
         "Graphics > GPU Eviction / Paging > Max Upload (MiB / frame)",
         "Each megabyte brought back to the GPU in one frame adds to that frame's time.",
         [](const settings& value) -> std::int64_t
         {
             return value.graphics.eviction.max_upload_mb;
         },
         [](const settings& value)
         {
             return std::to_string(value.graphics.eviction.max_upload_mb);
         },
         [](settings& to, const settings& from)
         {
             to.graphics.eviction.max_upload_mb = from.graphics.eviction.max_upload_mb;
         }},
        {"graphics/reflection_probe_faces_per_frame",
         "Graphics > Reflection Probe Faces Per Frame",
         "Each reflection probe face renders the scene again, so the faces a frame renders while probes bake add to "
         "that frame's cost on a phone's graphics processor.",
         [](const settings& value)
         {
             return static_cast<std::int64_t>(value.graphics.reflection_probe_faces_per_frame);
         },
         [](const settings& value)
         {
             return std::to_string(value.graphics.reflection_probe_faces_per_frame);
         },
         [](settings& to, const settings& from)
         {
             to.graphics.reflection_probe_faces_per_frame = from.graphics.reflection_probe_faces_per_frame;
         }},
        {"graphics/limits/max_bloom_mips",
         "Graphics > Allowed Features > Max Bloom Mips",
         "Each bloom mip is two render passes.",
         [](const settings& value)
         {
             return static_cast<std::int64_t>(value.graphics.limits.max_bloom_mips);
         },
         [](const settings& value)
         {
             return std::to_string(value.graphics.limits.max_bloom_mips);
         },
         [](settings& to, const settings& from)
         {
             to.graphics.limits.max_bloom_mips = from.graphics.limits.max_bloom_mips;
         }},
    };
    return list;
}

#undef AUDIT_LIMIT_SWITCH

} // namespace

auto get_performance_settings() -> const std::vector<performance_setting>&
{
    static const std::vector<performance_setting> all = make_settings();
    return all;
}

auto find_performance_setting(const std::string& path) -> const performance_setting*
{
    const auto& all = get_performance_settings();
    const auto found = std::find_if(all.begin(),
                                    all.end(),
                                    [&path](const performance_setting& setting)
                                    {
                                        return path == setting.path;
                                    });
    return found != all.end() ? &*found : nullptr;
}

} // namespace unravel::audit
