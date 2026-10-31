#include "settings.hpp"

#include <algorithm>
#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/array.hpp>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/input/input.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/physics/physics_types.h>
#include <engine/rendering/render_feature_support.h>
#include <engine/settings/boot_config.h>

namespace unravel
{
namespace
{
/// The hint under an Allowed Features row whose feature the running device does not run.
auto make_device_hint(render_feature feature) -> entt::property_predicate_t<std::string>
{
    return entt::property_predicate<std::string>(
        [feature](const entt::meta_any&) -> std::string
        {
            return get_render_feature_support().get_hint(feature);
        });
}
} // namespace

REFLECT_INLINE(settings::app_settings)
{
    entt::meta_factory<settings::app_settings>{}
        .type("app_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "app_settings"},
            entt::attribute{"pretty_name", "Application Settings"},
        })
        .data<&settings::app_settings::company>("company"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "company"},
            entt::attribute{"pretty_name", "Company"},
            entt::attribute{"tooltip",
                            "The company that makes the game. With Product it names the folder the game saves in "
                            "(Application.persistentDataPath) and the generated Android application id."},
        })
        .data<&settings::app_settings::product>("product"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "product"},
            entt::attribute{"pretty_name", "Product"},
            entt::attribute{"tooltip",
                            "The game's name. With Company it names the folder the game saves in "
                            "(Application.persistentDataPath) and the generated Android application id. The "
                            "project name stands in when it is empty."},
        })
        .data<&settings::app_settings::version>("version"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "version"},
            entt::attribute{"pretty_name", "Version"},
            entt::attribute{"tooltip", "Missing..."},
        });
}

SAVE_INLINE(settings::app_settings)
{
    try_save(ar, ser20::make_nvp("company", obj.company));
    try_save(ar, ser20::make_nvp("product", obj.product));
    try_save(ar, ser20::make_nvp("version", obj.version));
}

LOAD_INLINE(settings::app_settings)
{
    try_load(ar, ser20::make_nvp("company", obj.company));
    try_load(ar, ser20::make_nvp("product", obj.product));
    try_load(ar, ser20::make_nvp("version", obj.version));
}


REFLECT_INLINE(settings::asset_settings::texture_importer_settings)
{
    entt::meta_factory<settings::asset_settings::texture_importer_settings>{}
        .type("texture_importer_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_importer_settings"},
            entt::attribute{"pretty_name", "Texture Importer Settings"},
        })
        .data<&settings::asset_settings::texture_importer_settings::default_max_size>("default_max_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "default_max_size"},
            entt::attribute{"pretty_name", "Default Max Size"},
            entt::attribute{"tooltip", "The default maximum size for textures."},
        })
        .data<&settings::asset_settings::texture_importer_settings::default_compression>("default_compression"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "default_compression"},
            entt::attribute{"pretty_name", "Default Compression"},
            entt::attribute{"tooltip", "The default compression for textures."},
        });
}

REFLECT_INLINE(settings::asset_settings)
{
    entt::meta_factory<settings::asset_settings>{}
        .type("asset_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "asset_settings"},
            entt::attribute{"pretty_name", "Assets"},
        })
        .data<&settings::asset_settings::texture>("texture"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture"},
            entt::attribute{"pretty_name", "Textures"},
            entt::attribute{"tooltip", "Import settings a texture uses while its own are 'project_default'."},
        });
}

SAVE_INLINE(settings::asset_settings::texture_importer_settings)
{
    try_save(ar, ser20::make_nvp("default_max_size", obj.default_max_size));
    try_save(ar, ser20::make_nvp("default_compression", obj.default_compression));
}

LOAD_INLINE(settings::asset_settings::texture_importer_settings)
{
    try_load(ar, ser20::make_nvp("default_max_size", obj.default_max_size));
    try_load(ar, ser20::make_nvp("default_compression", obj.default_compression));
}

SAVE_INLINE(settings::asset_settings)
{
    try_save(ar, ser20::make_nvp("texture", obj.texture));
}

LOAD_INLINE(settings::asset_settings)
{
    try_load(ar, ser20::make_nvp("texture", obj.texture));
}


REFLECT_INLINE(eviction_settings)
{
    entt::meta_factory<eviction_settings>{}
        .type("eviction_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "eviction_settings"},
            entt::attribute{"pretty_name", "GPU Eviction / Paging"},
        })
        .data<&eviction_settings::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Evict GPU resources each frame to keep memory under the budget. "
                                       "Evicted resources are restored automatically on next use."},
        })
        .data<&eviction_settings::auto_budget>("auto_budget"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "auto_budget"},
            entt::attribute{"pretty_name", "Auto Budget (GPU Memory)"},
            entt::attribute{"tooltip", "Derive the budget from the backend's reported GPU memory. "
                                       "Falls back to the manual budget when no limit is reported."},
        })
        .data<&eviction_settings::budget_fraction>("budget_fraction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "budget_fraction"},
            entt::attribute{"pretty_name", "Budget %"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Start evicting once GPU usage exceeds this fraction of the maximum."},
        })
        .data<&eviction_settings::target_fraction>("target_fraction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "target_fraction"},
            entt::attribute{"pretty_name", "Target %"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Evict down to this fraction of the maximum (hysteresis: keep it below the budget)."},
        })
        .data<&eviction_settings::manual_budget_mb>("manual_budget_mb"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "manual_budget_mb"},
            entt::attribute{"pretty_name", "Manual Budget (MiB)"},
            entt::attribute{"min", 16},
            entt::attribute{"max", 1024*32},
            entt::attribute{"tooltip", "Budget compared against evictable resident bytes when not using auto budget."},
        })
        .data<&eviction_settings::strategy>("strategy"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "strategy"},
            entt::attribute{"pretty_name", "Strategy"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 3},
            entt::attribute{"tooltip", "Victim selection policy: 0=LRU, 1=LFU, 2=Largest first, 3=Age TTL."},
        })
        .data<&eviction_settings::min_age_frames>("min_age_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_age_frames"},
            entt::attribute{"pretty_name", "Min Age (frames)"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 600},
            entt::attribute{"tooltip", "Resources used within this many frames are never evicted (anti-thrash)."},
        })
        .data<&eviction_settings::max_evictions>("max_evictions"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_evictions"},
            entt::attribute{"pretty_name", "Max Evictions / Frame"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 1024},
            entt::attribute{"tooltip", "Caps how many resources may be evicted per frame (0 = unlimited)."},
        });
}

SAVE_INLINE(eviction_settings)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("auto_budget", obj.auto_budget));
    try_save(ar, ser20::make_nvp("budget_fraction", obj.budget_fraction));
    try_save(ar, ser20::make_nvp("target_fraction", obj.target_fraction));
    try_save(ar, ser20::make_nvp("manual_budget_mb", obj.manual_budget_mb));
    try_save(ar, ser20::make_nvp("strategy", obj.strategy));
    try_save(ar, ser20::make_nvp("min_age_frames", obj.min_age_frames));
    try_save(ar, ser20::make_nvp("max_evictions", obj.max_evictions));
}

LOAD_INLINE(eviction_settings)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("auto_budget", obj.auto_budget));
    try_load(ar, ser20::make_nvp("budget_fraction", obj.budget_fraction));
    try_load(ar, ser20::make_nvp("target_fraction", obj.target_fraction));
    try_load(ar, ser20::make_nvp("manual_budget_mb", obj.manual_budget_mb));
    try_load(ar, ser20::make_nvp("strategy", obj.strategy));
    try_load(ar, ser20::make_nvp("min_age_frames", obj.min_age_frames));
    try_load(ar, ser20::make_nvp("max_evictions", obj.max_evictions));
}

REFLECT_INLINE(preferred_renderer)
{
    entt::meta_factory<preferred_renderer>{}
        .type("preferred_renderer"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "preferred_renderer"},
            entt::attribute{"pretty_name", "Preferred Renderer"},
        })
        .data<preferred_renderer::auto_detect>("auto_detect"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "auto_detect"},
            entt::attribute{"pretty_name", "Auto"},
        })
        .data<preferred_renderer::opengl>("opengl"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "opengl"},
            entt::attribute{"pretty_name", "OpenGL"},
        })
        .data<preferred_renderer::vulkan>("vulkan"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vulkan"},
            entt::attribute{"pretty_name", "Vulkan"},
        })
        .data<preferred_renderer::direct3d11>("direct3d11"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "direct3d11"},
            entt::attribute{"pretty_name", "Direct3D 11"},
        })
        .data<preferred_renderer::direct3d12>("direct3d12"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "direct3d12"},
            entt::attribute{"pretty_name", "Direct3D 12"},
        })
        .data<preferred_renderer::metal>("metal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "metal"},
            entt::attribute{"pretty_name", "Metal"},
        });
}

REFLECT_INLINE(platform_renderer_settings)
{
    entt::meta_factory<platform_renderer_settings>{}
        .type("platform_renderer_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "platform_renderer_settings"},
            entt::attribute{"pretty_name", "Renderer Backend"},
        })
        .data<&platform_renderer_settings::windows>("windows"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "windows"},
            entt::attribute{"pretty_name", "Windows"},
            entt::attribute{"tooltip", "Preferred renderer when running on Windows. Requires editor restart."},
        })
        .data<&platform_renderer_settings::linux>("linux"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "linux"},
            entt::attribute{"pretty_name", "Linux"},
            entt::attribute{"tooltip", "Preferred renderer when running on Linux. Requires editor restart."},
        })
        .data<&platform_renderer_settings::macos>("macos"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "macos"},
            entt::attribute{"pretty_name", "macOS"},
            entt::attribute{"tooltip", "Preferred renderer when running on macOS. Requires editor restart."},
        })
        .data<&platform_renderer_settings::android>("android"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android"},
            entt::attribute{"pretty_name", "Android"},
            entt::attribute{"tooltip", "Preferred renderer when running on Android. Vulkan is the only one there."},
        });
}

SAVE_INLINE(platform_renderer_settings)
{
    try_save(ar, ser20::make_nvp("windows", obj.windows));
    try_save(ar, ser20::make_nvp("linux", obj.linux));
    try_save(ar, ser20::make_nvp("macos", obj.macos));
    try_save(ar, ser20::make_nvp("android", obj.android));
}

LOAD_INLINE(platform_renderer_settings)
{
    try_load(ar, ser20::make_nvp("windows", obj.windows));
    try_load(ar, ser20::make_nvp("linux", obj.linux));
    try_load(ar, ser20::make_nvp("macos", obj.macos));
    try_load(ar, ser20::make_nvp("android", obj.android));
}

REFLECT_INLINE(settings::graphics_settings::particle_settings)
{
    entt::meta_factory<settings::graphics_settings::particle_settings>{}
        .type("particle_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_settings"},
            entt::attribute{"pretty_name", "Particles"},
        })
        .data<&settings::graphics_settings::particle_settings::spawn_scale>("spawn_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spawn_scale"},
            entt::attribute{"pretty_name", "Spawn Scale"},
            entt::attribute{"tooltip", "Multiplier of every emitter's rates and bursts: fewer particles on smaller targets without touching the effects."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&settings::graphics_settings::particle_settings::sorting>("sorting"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sorting"},
            entt::attribute{"pretty_name", "Sorting"},
            entt::attribute{"tooltip", "Sort particles where their emitters ask for it. Off draws them in spawn order: cheaper, but alpha-blended layers may show order errors."},
        })
        .data<&settings::graphics_settings::particle_settings::force_half_resolution>("force_half_resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "force_half_resolution"},
            entt::attribute{"pretty_name", "Force Half Resolution"},
            entt::attribute{"tooltip", "Draw every emitter that can (all but multiply blend) at half resolution: a quarter of the particles' fill cost."},
        })
        .data<&settings::graphics_settings::particle_settings::min_screen_size>("min_screen_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_screen_size"},
            entt::attribute{"pretty_name", "Min Screen Size"},
            entt::attribute{"tooltip", "Emitters smaller on screen than this fraction of the view's height are not drawn, and freeze with renderer-based culling. 0 = off."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&settings::graphics_settings::particle_settings::max_drawn_particles>("max_drawn_particles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_drawn_particles"},
            entt::attribute{"pretty_name", "Max Drawn Particles"},
            entt::attribute{"tooltip", "Most particles one view draws: past it the emitters smallest on screen are skipped (and freeze with renderer-based culling). 0 = no limit."},
        })
        .data<&settings::graphics_settings::particle_settings::max_particles_scale>("max_particles_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_particles_scale"},
            entt::attribute{"pretty_name", "Max Particles Scale"},
            entt::attribute{"tooltip", "Multiplier of every emitter's Max Particles: spawns past the scaled capacity are dropped, so dense effects thin out on smaller targets."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&settings::graphics_settings::particle_settings::max_lighting>("max_lighting"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_lighting"},
            entt::attribute{"pretty_name", "Max Lighting"},
            entt::attribute{"tooltip", "The most expensive lighting mode particles use. Volumetric: surface-lit particles light as volumetric (no per-pixel sun). Unlit: lit particles draw unlit."},
        });
}

SAVE_INLINE(settings::graphics_settings::particle_settings)
{
    try_save(ar, ser20::make_nvp("spawn_scale", obj.spawn_scale));
    try_save(ar, ser20::make_nvp("sorting", obj.sorting));
    try_save(ar, ser20::make_nvp("force_half_resolution", obj.force_half_resolution));
    try_save(ar, ser20::make_nvp("min_screen_size", obj.min_screen_size));
    try_save(ar, ser20::make_nvp("max_drawn_particles", obj.max_drawn_particles));
    try_save(ar, ser20::make_nvp("max_particles_scale", obj.max_particles_scale));
    try_save(ar, ser20::make_nvp("max_lighting", obj.max_lighting));
}

LOAD_INLINE(settings::graphics_settings::particle_settings)
{
    try_load(ar, ser20::make_nvp("spawn_scale", obj.spawn_scale));
    try_load(ar, ser20::make_nvp("sorting", obj.sorting));
    try_load(ar, ser20::make_nvp("force_half_resolution", obj.force_half_resolution));
    try_load(ar, ser20::make_nvp("min_screen_size", obj.min_screen_size));
    try_load(ar, ser20::make_nvp("max_drawn_particles", obj.max_drawn_particles));
    try_load(ar, ser20::make_nvp("max_particles_scale", obj.max_particles_scale));
    try_load(ar, ser20::make_nvp("max_lighting", obj.max_lighting));
}

REFLECT_INLINE(anti_aliasing_limit)
{
    entt::meta_factory<anti_aliasing_limit>{}
        .type("anti_aliasing_limit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "anti_aliasing_limit"},
            entt::attribute{"pretty_name", "Anti-Aliasing Limit"},
        })
        .data<anti_aliasing_limit::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
        })
        .data<anti_aliasing_limit::fxaa>("fxaa"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fxaa"},
            entt::attribute{"pretty_name", "FXAA"},
        })
        .data<anti_aliasing_limit::temporal>("temporal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "temporal"},
            entt::attribute{"pretty_name", "Temporal"},
        });
}

REFLECT_INLINE(feature_limits)
{
    entt::meta_factory<feature_limits>{}
        .type("feature_limits"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "feature_limits"},
            entt::attribute{"pretty_name", "Allowed Features"},
        })
        .data<&feature_limits::global_illumination>("global_illumination"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "global_illumination"},
            entt::attribute{"pretty_name", "Global Illumination"},
            entt::attribute{"tooltip", "Allow global illumination. When off, scenes are lit by their probes and the sky only."},
            entt::attribute{"hint", make_device_hint(render_feature::global_illumination)},
        })
        .data<&feature_limits::screen_space_reflections>("screen_space_reflections"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "screen_space_reflections"},
            entt::attribute{"pretty_name", "Screen Space Reflections"},
            entt::attribute{"tooltip", "Allow screen space reflections."},
            entt::attribute{"hint", make_device_hint(render_feature::screen_space_reflections)},
        })
        .data<&feature_limits::screen_space_indirect_lighting>("screen_space_indirect_lighting"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "screen_space_indirect_lighting"},
            entt::attribute{"pretty_name", "Screen Space Indirect Lighting"},
            entt::attribute{"tooltip", "Allow screen space indirect lighting."},
            entt::attribute{"hint", make_device_hint(render_feature::screen_space_indirect_lighting)},
        })
        .data<&feature_limits::ambient_occlusion>("ambient_occlusion"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ambient_occlusion"},
            entt::attribute{"pretty_name", "Ambient Occlusion"},
            entt::attribute{"tooltip", "Allow screen space ambient occlusion (GTAO and ASSAO)."},
            entt::attribute{"hint", make_device_hint(render_feature::gtao)},
        })
        .data<&feature_limits::bloom>("bloom"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bloom"},
            entt::attribute{"pretty_name", "Bloom"},
            entt::attribute{"tooltip", "Allow bloom."},
        })
        .data<&feature_limits::volumetric_fog>("volumetric_fog"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric_fog"},
            entt::attribute{"pretty_name", "Volumetric Fog"},
            entt::attribute{"tooltip", "Allow volumetric fog. When off, fog that asks for it renders without its "
                                       "shadows and light shafts."},
            entt::attribute{"hint", make_device_hint(render_feature::volumetric_fog)},
        })
        .data<&feature_limits::max_anti_aliasing>("max_anti_aliasing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_anti_aliasing"},
            entt::attribute{"pretty_name", "Max Anti-Aliasing"},
            entt::attribute{"tooltip", "The most expensive anti-aliasing a view may use. With FXAA, views asking for temporal AA get FXAA."},
        })
        .data<&feature_limits::max_shadow_resolution>("max_shadow_resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_shadow_resolution"},
            entt::attribute{"pretty_name", "Max Shadow Resolution"},
            entt::attribute{"tooltip", "The largest shadow map a light may use. Lights asking for larger ones render at this resolution."},
        })
        .data<&feature_limits::soft_shadows>("soft_shadows"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "soft_shadows"},
            entt::attribute{"pretty_name", "Soft Shadows"},
            entt::attribute{"tooltip", "Allow soft shadows (PCSS, VSM and ESM). When off, those lights filter their shadows with PCF."},
            entt::attribute{"hint", make_device_hint(render_feature::soft_shadows)},
        })
        .data<&feature_limits::contact_shadows>("contact_shadows"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "contact_shadows"},
            entt::attribute{"pretty_name", "Contact Shadows"},
            entt::attribute{"tooltip", "Allow contact shadows."},
        })
        .data<&feature_limits::subsurface>("subsurface"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "subsurface"},
            entt::attribute{"pretty_name", "Subsurface"},
            entt::attribute{"tooltip", "Allow subsurface scattering. When off, materials render without their subsurface "
                                       "color, and the lights skip its cost."},
        })
        .data<&feature_limits::volumetric_clouds>("volumetric_clouds"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric_clouds"},
            entt::attribute{"pretty_name", "Volumetric Clouds"},
            entt::attribute{"tooltip", "Allow volumetric clouds. When off, skies that ask for them render flat clouds, "
                                       "without the raymarch."},
        })
        .data<&feature_limits::detailed_indirect_lighting>("detailed_indirect_lighting"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "detailed_indirect_lighting"},
            entt::attribute{"pretty_name", "Detailed Indirect Lighting"},
            entt::attribute{"tooltip", "Allow the finest detail of the indirect lighting. When off, views without screen "
                                       "space effects read less of the environment's lighting, at a lower cost per pixel."},
        });
}

SAVE_INLINE(feature_limits)
{
    try_save(ar, ser20::make_nvp("global_illumination", obj.global_illumination));
    try_save(ar, ser20::make_nvp("screen_space_reflections", obj.screen_space_reflections));
    try_save(ar, ser20::make_nvp("screen_space_indirect_lighting", obj.screen_space_indirect_lighting));
    try_save(ar, ser20::make_nvp("ambient_occlusion", obj.ambient_occlusion));
    try_save(ar, ser20::make_nvp("bloom", obj.bloom));
    try_save(ar, ser20::make_nvp("volumetric_fog", obj.volumetric_fog));
    try_save(ar, ser20::make_nvp("max_anti_aliasing", obj.max_anti_aliasing));
    try_save(ar, ser20::make_nvp("max_shadow_resolution", obj.max_shadow_resolution));
    try_save(ar, ser20::make_nvp("soft_shadows", obj.soft_shadows));
    try_save(ar, ser20::make_nvp("contact_shadows", obj.contact_shadows));
    try_save(ar, ser20::make_nvp("subsurface", obj.subsurface));
    try_save(ar, ser20::make_nvp("volumetric_clouds", obj.volumetric_clouds));
    try_save(ar, ser20::make_nvp("detailed_indirect_lighting", obj.detailed_indirect_lighting));
}

LOAD_INLINE(feature_limits)
{
    try_load(ar, ser20::make_nvp("global_illumination", obj.global_illumination));
    try_load(ar, ser20::make_nvp("screen_space_reflections", obj.screen_space_reflections));
    try_load(ar, ser20::make_nvp("screen_space_indirect_lighting", obj.screen_space_indirect_lighting));
    try_load(ar, ser20::make_nvp("ambient_occlusion", obj.ambient_occlusion));
    try_load(ar, ser20::make_nvp("bloom", obj.bloom));
    try_load(ar, ser20::make_nvp("volumetric_fog", obj.volumetric_fog));
    try_load(ar, ser20::make_nvp("max_anti_aliasing", obj.max_anti_aliasing));
    try_load(ar, ser20::make_nvp("max_shadow_resolution", obj.max_shadow_resolution));
    try_load(ar, ser20::make_nvp("soft_shadows", obj.soft_shadows));
    try_load(ar, ser20::make_nvp("contact_shadows", obj.contact_shadows));
    try_load(ar, ser20::make_nvp("subsurface", obj.subsurface));
    try_load(ar, ser20::make_nvp("volumetric_clouds", obj.volumetric_clouds));
    try_load(ar, ser20::make_nvp("detailed_indirect_lighting", obj.detailed_indirect_lighting));
}

REFLECT_INLINE(settings::graphics_settings)
{
    entt::meta_factory<settings::graphics_settings>{}
        .type("graphics_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "graphics_settings"},
            entt::attribute{"pretty_name", "Graphics Settings"},
        })
        .data<&settings::graphics_settings::renderer>("renderer"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "renderer"},
            entt::attribute{"pretty_name", "Renderer Backend"},
            entt::attribute{"tooltip",
                            "Per-platform preferred graphics backend. Applied at process start. Requires a restart."},
        })
        .data<&settings::graphics_settings::static_mesh_batching>("static_mesh_batching"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "static_mesh_batching"},
            entt::attribute{"pretty_name", "Static Mesh Batching"},
            entt::attribute{"tooltip", "Draw the meshes that share a mesh and a material with one instanced draw call."},
        })
        .data<&settings::graphics_settings::skinned_mesh_batching>("skinned_mesh_batching"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "skinned_mesh_batching"},
            entt::attribute{"pretty_name", "Skinned Mesh Batching"},
            entt::attribute{"tooltip",
                            "Draw the skinned meshes that share a mesh and a material with one instanced draw call. "
                            "Their bone palettes reach the GPU once per frame for every view, instead of once per "
                            "draw."},
        })
        .data<&settings::graphics_settings::shadow_map_caching>("shadow_map_caching"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shadow_map_caching"},
            entt::attribute{"pretty_name", "Shadow Map Caching"},
            entt::attribute{"tooltip",
                            "Keep a light's shadow map, or a cascade of the sun, from one frame to the next while "
                            "nothing it shows changed. A map is rendered again when its light, a caster in its reach "
                            "or the camera's cascades change. A spot or point light with moving casters in reach "
                            "keeps a copy of the casters that hold still and draws only the moving ones over it."},
        })
        .data<&settings::graphics_settings::render_scale>("render_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "render_scale"},
            entt::attribute{"pretty_name", "Render Scale"},
            entt::attribute{"tooltip",
                            "The fraction of a game view's size its scene renders at. The finished scene is "
                            "upscaled to the view's size before the screen-space UI, and every scene pass "
                            "shades fewer pixels. The editor's Game and Scene views can use it, a scale of "
                            "their own or none, from their toolbars."},
            entt::attribute{"min", MIN_RENDER_SCALE},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", RENDER_SCALE_STEP},
        })
        .data<&settings::graphics_settings::limits>("limits"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "limits"},
            entt::attribute{"pretty_name", "Allowed Features"},
            entt::attribute{"tooltip",
                            "The rendering features scenes may use, and the most expensive versions of them. Where "
                            "these do not allow what a camera and its volumes ask for, the effect is turned off or "
                            "lowered. Scenes themselves are not changed."},
        })
        .data<&settings::graphics_settings::eviction>("eviction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "eviction"},
            entt::attribute{"pretty_name", "GPU Eviction / Paging"},
            entt::attribute{"tooltip", "Controls automatic eviction/paging of GPU resources to stay under budget."},
        })
        .data<&settings::graphics_settings::particles>("particles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particles"},
            entt::attribute{"pretty_name", "Particles"},
            entt::attribute{"tooltip", "Project-wide particle quality and budgets."},
        });
}

SAVE_INLINE(settings::graphics_settings)
{
    try_save(ar, ser20::make_nvp("renderer", obj.renderer));
    try_save(ar, ser20::make_nvp("static_mesh_batching", obj.static_mesh_batching));
    try_save(ar, ser20::make_nvp("eviction", obj.eviction));
    try_save(ar, ser20::make_nvp("particles", obj.particles));
    try_save(ar, ser20::make_nvp("render_scale", obj.render_scale));
    try_save(ar, ser20::make_nvp("limits", obj.limits));
    try_save(ar, ser20::make_nvp("skinned_mesh_batching", obj.skinned_mesh_batching));
    try_save(ar, ser20::make_nvp("shadow_map_caching", obj.shadow_map_caching));
}

LOAD_INLINE(settings::graphics_settings)
{
    try_load(ar, ser20::make_nvp("renderer", obj.renderer));
    try_load(ar, ser20::make_nvp("static_mesh_batching", obj.static_mesh_batching));
    try_load(ar, ser20::make_nvp("eviction", obj.eviction));
    try_load(ar, ser20::make_nvp("particles", obj.particles));
    try_load(ar, ser20::make_nvp("render_scale", obj.render_scale));
    try_load(ar, ser20::make_nvp("limits", obj.limits));
    try_load(ar, ser20::make_nvp("skinned_mesh_batching", obj.skinned_mesh_batching));
    try_load(ar, ser20::make_nvp("shadow_map_caching", obj.shadow_map_caching));
}

REFLECT_INLINE(gi_project_settings::atlas_size)
{
    entt::meta_factory<gi_project_settings::atlas_size>{}
        .type("gi_atlas_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_atlas_size"},
            entt::attribute{"pretty_name", "Atlas Size"},
        })
        .data<gi_project_settings::atlas_size::size_1024>("size_1024"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_1024"},
            entt::attribute{"pretty_name", "1024 (about 34 MB)"},
        })
        .data<gi_project_settings::atlas_size::size_2048>("size_2048"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_2048"},
            entt::attribute{"pretty_name", "2048 (about 134 MB)"},
        })
        .data<gi_project_settings::atlas_size::size_4096>("size_4096"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_4096"},
            entt::attribute{"pretty_name", "4096 (about 538 MB)"},
        });
}

REFLECT_INLINE(gi_project_settings::quality_level)
{
    entt::meta_factory<gi_project_settings::quality_level>{}
        .type("gi_quality_level"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_quality_level"},
            entt::attribute{"pretty_name", "Quality Level"},
        })
        .data<gi_project_settings::quality_level::high>("high"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high"},
            entt::attribute{"pretty_name", "High"},
        })
        .data<gi_project_settings::quality_level::epic>("epic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "epic"},
            entt::attribute{"pretty_name", "Epic"},
        });
}

REFLECT_INLINE(gi_project_settings::capture_atlas_size)
{
    entt::meta_factory<gi_project_settings::capture_atlas_size>{}
        .type("gi_capture_atlas_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_capture_atlas_size"},
            entt::attribute{"pretty_name", "Capture Atlas Size"},
        })
        .data<gi_project_settings::capture_atlas_size::size_512>("size_512"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_512"},
            entt::attribute{"pretty_name", "512"},
        })
        .data<gi_project_settings::capture_atlas_size::size_1024>("size_1024"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_1024"},
            entt::attribute{"pretty_name", "1024"},
        })
        .data<gi_project_settings::capture_atlas_size::size_2048>("size_2048"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_2048"},
            entt::attribute{"pretty_name", "2048"},
        });
}

REFLECT_INLINE(gi_project_settings)
{
    entt::meta_factory<gi_project_settings>{}
        .type("gi_project_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_project_settings"},
            entt::attribute{"pretty_name", "Global Illumination"},
        })
        .data<&gi_project_settings::global_illumination_quality>("global_illumination_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "global_illumination_quality"},
            entt::attribute{"pretty_name", "Global Illumination Quality"},
            entt::attribute{"group", "Scalability"},
            entt::attribute{"tooltip",
                            "High: screen probes every 32 pixels instead of 16, each pixel lit from one of its four\n"
                            "probes in turn, the short-range AO searched at half resolution, coarser radiance cache\n"
                            "probes and fewer of them traced again per frame, the cards relit half as often, their\n"
                            "bounce light from sparser, cheaper probes and none smaller than 4 texels. Faster,\n"
                            "softer, noisier and slower to react. Epic: the full quality."},
        })
        .data<&gi_project_settings::reflection_quality>("reflection_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reflection_quality"},
            entt::attribute{"pretty_name", "Reflection Quality"},
            entt::attribute{"group", "Scalability"},
            entt::attribute{"tooltip",
                            "High: one pixel of every 2 x 2 traces a reflection ray, and each pixel is resolved\n"
                            "from 3 neighbouring rays. Faster, blurrier on rough surfaces. Epic: every pixel\n"
                            "traces, 5 neighbouring rays."},
        })
        .data<&gi_project_settings::surface_cache_atlas_size>("surface_cache_atlas_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface_cache_atlas_size"},
            entt::attribute{"pretty_name", "Surface Cache Atlas"},
            entt::attribute{"group", "Surface Cache"},
            entt::attribute{"tooltip",
                            "Edge of the surface cache atlas, in texels: how much card detail stays resident.\n"
                            "Every texel holds the captured material and the card lighting, about 34 bytes.\n"
                            "A scene that wants more texels than this keeps its farthest cards at lower\n"
                            "resolution or none. Changing it recaptures the whole surface cache."},
        })
        .data<&gi_project_settings::hi_res_reflection_pages>("hi_res_reflection_pages"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hi_res_reflection_pages"},
            entt::attribute{"pretty_name", "Hi-Res Reflection Pages"},
            entt::attribute{"group", "Surface Cache"},
            entt::attribute{"tooltip",
                            "Smooth reflections of surfaces off screen ask for finer card pages where they hit,\n"
                            "kept while the reflections still read them: sharper mirror-like reflections when the\n"
                            "atlas has room. The request reaches the CPU through a readback that waits for the GPU\n"
                            "once every 16 frames, a hitch on GPU-heavy frames."},
        })
        .data<&gi_project_settings::card_captures_per_frame>("card_captures_per_frame"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_captures_per_frame"},
            entt::attribute{"pretty_name", "Card Captures Per Frame"},
            entt::attribute{"group", "Card Capture"},
            entt::attribute{"tooltip",
                            "Card pages captured per frame at most. Higher fills the surface cache faster when the\n"
                            "view moves, at more CPU and GPU cost per frame."},
            entt::attribute{"min", 1},
            entt::attribute{"max", 4096},
        })
        .data<&gi_project_settings::card_capture_atlas_size>("card_capture_atlas_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_capture_atlas_size"},
            entt::attribute{"pretty_name", "Capture Atlas"},
            entt::attribute{"group", "Card Capture"},
            entt::attribute{"tooltip",
                            "Edge of the atlas the cards are captured into each frame, in texels: the most\n"
                            "texels captured per frame."},
        })
        .data<&gi_project_settings::card_capture_refresh_fraction>("card_capture_refresh_fraction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_capture_refresh_fraction"},
            entt::attribute{"pretty_name", "Refresh Fraction"},
            entt::attribute{"group", "Card Capture"},
            entt::attribute{"tooltip",
                            "The share of the capture budget spent capturing resident card pages again, oldest\n"
                            "first, so material changes (color, emissive, textures) reach the GI. 0 disables:\n"
                            "a card then keeps what it captured until it is reallocated."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.005f},
        })
        .data<&gi_project_settings::card_texel_density_scale>("card_texel_density_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_texel_density_scale"},
            entt::attribute{"pretty_name", "Texel Density Scale"},
            entt::attribute{"group", "Card Resolution"},
            entt::attribute{"tooltip",
                            "A card's resolution: this many texels per unit of half extent over its distance to\n"
                            "the viewer, rounded up to a power of two."},
            entt::attribute{"min", 1.0f},
            entt::attribute{"step", 1.0f},
        })
        .data<&gi_project_settings::card_max_texel_density>("card_max_texel_density"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_max_texel_density"},
            entt::attribute{"pretty_name", "Max Texel Density"},
            entt::attribute{"group", "Card Resolution"},
            entt::attribute{"tooltip", "The most texels per metre a card gets, however close the viewer is."},
            entt::attribute{"min", 1.0f},
            entt::attribute{"step", 1.0f},
        })
        .data<&gi_project_settings::card_max_resolution>("card_max_resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_max_resolution"},
            entt::attribute{"pretty_name", "Max Card Resolution"},
            entt::attribute{"group", "Card Resolution"},
            entt::attribute{"tooltip", "The most texels a card gets along its longer side."},
            entt::attribute{"min", 8},
            entt::attribute{"max", 2048},
        })
        .data<&gi_project_settings::card_min_resolution>("card_min_resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_min_resolution"},
            entt::attribute{"pretty_name", "Min Card Resolution"},
            entt::attribute{"group", "Card Resolution"},
            entt::attribute{"tooltip",
                            "A card that would get fewer texels than this is not resident: far and small cards\n"
                            "leave the surface cache. Emissive light sources stay down to one texel."},
            entt::attribute{"min", 1},
            entt::attribute{"max", 1024},
        })
        .data<&gi_project_settings::mesh_cards_min_size>("mesh_cards_min_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh_cards_min_size"},
            entt::attribute{"pretty_name", "Min Card Size"},
            entt::attribute{"group", "Card Resolution"},
            entt::attribute{"tooltip",
                            "A card face smaller than this squared, in metres, is not resident (a fifth of it for\n"
                            "emissive light sources)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.01f},
        });
}

SAVE_INLINE(gi_project_settings)
{
    try_save(ar, ser20::make_nvp("global_illumination_quality", obj.global_illumination_quality));
    try_save(ar, ser20::make_nvp("reflection_quality", obj.reflection_quality));
    try_save(ar, ser20::make_nvp("surface_cache_atlas_size", obj.surface_cache_atlas_size));
    try_save(ar, ser20::make_nvp("hi_res_reflection_pages", obj.hi_res_reflection_pages));
    try_save(ar, ser20::make_nvp("card_captures_per_frame", obj.card_captures_per_frame));
    try_save(ar, ser20::make_nvp("card_capture_atlas_size", obj.card_capture_atlas_size));
    try_save(ar, ser20::make_nvp("card_capture_refresh_fraction", obj.card_capture_refresh_fraction));
    try_save(ar, ser20::make_nvp("card_texel_density_scale", obj.card_texel_density_scale));
    try_save(ar, ser20::make_nvp("card_max_texel_density", obj.card_max_texel_density));
    try_save(ar, ser20::make_nvp("card_max_resolution", obj.card_max_resolution));
    try_save(ar, ser20::make_nvp("card_min_resolution", obj.card_min_resolution));
    try_save(ar, ser20::make_nvp("mesh_cards_min_size", obj.mesh_cards_min_size));
}

LOAD_INLINE(gi_project_settings)
{
    try_load(ar, ser20::make_nvp("global_illumination_quality", obj.global_illumination_quality));
    try_load(ar, ser20::make_nvp("reflection_quality", obj.reflection_quality));
    try_load(ar, ser20::make_nvp("surface_cache_atlas_size", obj.surface_cache_atlas_size));
    try_load(ar, ser20::make_nvp("hi_res_reflection_pages", obj.hi_res_reflection_pages));
    try_load(ar, ser20::make_nvp("card_captures_per_frame", obj.card_captures_per_frame));
    try_load(ar, ser20::make_nvp("card_capture_atlas_size", obj.card_capture_atlas_size));
    try_load(ar, ser20::make_nvp("card_capture_refresh_fraction", obj.card_capture_refresh_fraction));
    try_load(ar, ser20::make_nvp("card_texel_density_scale", obj.card_texel_density_scale));
    try_load(ar, ser20::make_nvp("card_max_texel_density", obj.card_max_texel_density));
    try_load(ar, ser20::make_nvp("card_max_resolution", obj.card_max_resolution));
    try_load(ar, ser20::make_nvp("card_min_resolution", obj.card_min_resolution));
    try_load(ar, ser20::make_nvp("mesh_cards_min_size", obj.mesh_cards_min_size));
}

REFLECT_INLINE(settings::splash_logo_entry)
{
    entt::meta_factory<settings::splash_logo_entry>{}
        .type("splash_logo_entry"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "splash_logo_entry"},
            entt::attribute{"pretty_name", "Logo"},
        })
        .data<&settings::splash_logo_entry::logo>("logo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "logo"},
            entt::attribute{"pretty_name", "Logo"},
            entt::attribute{"tooltip", "Texture asset displayed during the splash sequence."},
        })
        .data<&settings::splash_logo_entry::duration_sec>("duration_sec"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "duration_sec"},
            entt::attribute{"pretty_name", "Duration (s)"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 30.0f},
            entt::attribute{"step", 0.1f},
        });
}

SAVE_INLINE(settings::splash_logo_entry)
{
    try_save(ar, ser20::make_nvp("logo", obj.logo));
    try_save(ar, ser20::make_nvp("duration_sec", obj.duration_sec));
}

LOAD_INLINE(settings::splash_logo_entry)
{
    try_load(ar, ser20::make_nvp("logo", obj.logo));
    try_load(ar, ser20::make_nvp("duration_sec", obj.duration_sec));
}

REFLECT_INLINE(settings::splash_settings)
{
    entt::meta_factory<settings::splash_settings>{}
        .type("splash_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "splash_settings"},
            entt::attribute{"pretty_name", "Splash Screen"},
        })
        .data<&settings::splash_settings::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Show Splash Screen"},
            entt::attribute{"tooltip", "Display a splash screen when entering play mode before the game starts."},
        })
        .data<&settings::splash_settings::show_made_with>("show_made_with"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "show_made_with"},
            entt::attribute{"pretty_name", "Show Made With Unravel"},
            entt::attribute{"tooltip", "Append the engine branding logo to the splash sequence."},
        })
        .data<&settings::splash_settings::fade_in_sec>("fade_in_sec"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fade_in_sec"},
            entt::attribute{"pretty_name", "Fade In (s)"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"step", 0.05f},
        })
        .data<&settings::splash_settings::fade_out_sec>("fade_out_sec"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fade_out_sec"},
            entt::attribute{"pretty_name", "Fade Out (s)"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"step", 0.05f},
        })
        .data<&settings::splash_settings::logos>("logos"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "logos"},
            entt::attribute{"pretty_name", "Logos"},
            entt::attribute{"tooltip", "Project logos shown sequentially before play begins."},
        });
}

SAVE_INLINE(settings::splash_settings)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("show_made_with", obj.show_made_with));
    try_save(ar, ser20::make_nvp("fade_in_sec", obj.fade_in_sec));
    try_save(ar, ser20::make_nvp("fade_out_sec", obj.fade_out_sec));
    try_save(ar, ser20::make_nvp("logos", obj.logos));
}

LOAD_INLINE(settings::splash_settings)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("show_made_with", obj.show_made_with));
    try_load(ar, ser20::make_nvp("fade_in_sec", obj.fade_in_sec));
    try_load(ar, ser20::make_nvp("fade_out_sec", obj.fade_out_sec));
    try_load(ar, ser20::make_nvp("logos", obj.logos));
}

REFLECT_INLINE(settings::standalone_settings)
{

    entt::meta_factory<settings::standalone_settings>{}
        .type("standalone_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "standalone_settings"},
            entt::attribute{"pretty_name", "Standalone Settings"},
        })
        .data<&settings::standalone_settings::startup_scene>("startup_scene"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "startup_scene"},
            entt::attribute{"pretty_name", "Startup Scene"},
            entt::attribute{"tooltip", "The scene to load first."},
        })
        .data<&settings::standalone_settings::run_in_background>("run_in_background"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "run_in_background"},
            entt::attribute{"pretty_name", "Run In Background"},
            entt::attribute{"tooltip",
                            "Whether the game keeps its frame rate while its window is minimized. When it is off, a "
                            "minimized game runs at most 20 frames per second. Scripts change it with "
                            "Application.runInBackground."},
        });
}

SAVE_INLINE(settings::standalone_settings)
{
    try_save(ar, ser20::make_nvp("startup_scene", obj.startup_scene));
    try_save(ar, ser20::make_nvp("run_in_background", obj.run_in_background));
}

LOAD_INLINE(settings::standalone_settings)
{
    try_load(ar, ser20::make_nvp("startup_scene", obj.startup_scene));
    try_load(ar, ser20::make_nvp("run_in_background", obj.run_in_background));
}

REFLECT_INLINE(physics_backend_type)
{
    entt::meta_factory<physics_backend_type>{}
        .type("physics_backend_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_backend_type"},
            entt::attribute{"pretty_name", "Physics Backend"},
        })
        .data<physics_backend_type::auto_detect>("auto"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "auto"},
            entt::attribute{"pretty_name", "Auto (Box3D)"},
        })
        .data<physics_backend_type::box3d>("box3d"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "box3d"},
            entt::attribute{"pretty_name", "Box3D"},
        })
        .data<physics_backend_type::bullet>("bullet"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bullet"},
            entt::attribute{"pretty_name", "Bullet"},
        });
}

REFLECT_INLINE(settings::physics_settings)
{
    entt::meta_factory<settings::physics_settings>{}
        .type("physics_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_settings"},
            entt::attribute{"pretty_name", "Physics Settings"},
        })
        .data<&settings::physics_settings::backend>("backend"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "backend"},
            entt::attribute{"pretty_name", "Physics Backend"},
            entt::attribute{"tooltip", "Physics engine adapter. Applied at process start. Requires an editor restart."},
        })
        .data<&settings::physics_settings::fixed_timestep>("fixed_timestep"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fixed_timestep"},
            entt::attribute{"pretty_name", "Fixed Timestep"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"step", 0.001f},
            entt::attribute{"tooltip",
                            "A framerate-independent interval which dictates when physics calculations and "
                            "FixedUpdate events are performed."},
        })
        .data<&settings::physics_settings::max_fixed_steps>("max_fixed_steps"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_fixed_steps"},
            entt::attribute{"pretty_name", "Max Fixed Steps"},
            entt::attribute{"min", 1},
            entt::attribute{"tooltip",
                            "A cap for framerate-independent worst case scenario. No more than this many fixed "
                            "updates per frame."},
        })
        .data<&settings::physics_settings::solver_iterations>("solver_iterations"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "solver_iterations"},
            entt::attribute{"pretty_name", "Solver Iterations"},
            entt::attribute{"min", 1},
            entt::attribute{"tooltip",
                            "Constraint solver iterations per step. Solver time scales close to linearly with "
                            "this. Lower values settle stacks less crisply; 4-6 is usually indistinguishable "
                            "for piles of primitives."},
        });
}

SAVE_INLINE(settings::physics_settings)
{
    try_save(ar, ser20::make_nvp("backend", obj.backend));
    try_save(ar, ser20::make_nvp("fixed_timestep", obj.fixed_timestep));
    try_save(ar, ser20::make_nvp("max_fixed_steps", obj.max_fixed_steps));
    try_save(ar, ser20::make_nvp("solver_iterations", obj.solver_iterations));
}

LOAD_INLINE(settings::physics_settings)
{
    try_load(ar, ser20::make_nvp("backend", obj.backend));
    try_load(ar, ser20::make_nvp("fixed_timestep", obj.fixed_timestep));
    try_load(ar, ser20::make_nvp("max_fixed_steps", obj.max_fixed_steps));
    try_load(ar, ser20::make_nvp("solver_iterations", obj.solver_iterations));
}

REFLECT_INLINE(settings::layer_settings)
{
    entt::meta_factory<settings::layer_settings>{}
        .type("layer_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "layer_settings"},
            entt::attribute{"pretty_name", "Layer Settings"},
        })
        .data<&settings::layer_settings::layers>("layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "layers"},
            entt::attribute{"pretty_name", "Layers"},
            entt::attribute{"readonly_count", get_reserved_layers().size()},
            entt::attribute{"element_label", "Layer"},
            entt::attribute{"tooltip", ""},
        });
}

SAVE_INLINE(settings::layer_settings)
{
    try_save(ar, ser20::make_nvp("layers", obj.layers));
}

LOAD_INLINE(settings::layer_settings)
{
    try_load(ar, ser20::make_nvp("layers", obj.layers));
}

SAVE_INLINE(settings::input_settings)
{
    try_save(ar, ser20::make_nvp("actions", obj.actions));
}

LOAD_INLINE(settings::input_settings)
{
    try_load(ar, ser20::make_nvp("actions", obj.actions));
}


SAVE_INLINE(settings::resolution_settings::resolution)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("width", obj.width));
    try_save(ar, ser20::make_nvp("height", obj.height));
    try_save(ar, ser20::make_nvp("aspect", obj.aspect));
    try_save(ar, ser20::make_nvp("device", obj.device));
    try_save(ar, ser20::make_nvp("dpi", obj.dpi));
    try_save(ar, ser20::make_nvp("safe_left", obj.safe_left));
    try_save(ar, ser20::make_nvp("safe_top", obj.safe_top));
    try_save(ar, ser20::make_nvp("safe_right", obj.safe_right));
    try_save(ar, ser20::make_nvp("safe_bottom", obj.safe_bottom));
}

LOAD_INLINE(settings::resolution_settings::resolution)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("width", obj.width));
    try_load(ar, ser20::make_nvp("height", obj.height));
    try_load(ar, ser20::make_nvp("aspect", obj.aspect));
    try_load(ar, ser20::make_nvp("device", obj.device));
    try_load(ar, ser20::make_nvp("dpi", obj.dpi));
    try_load(ar, ser20::make_nvp("safe_left", obj.safe_left));
    try_load(ar, ser20::make_nvp("safe_top", obj.safe_top));
    try_load(ar, ser20::make_nvp("safe_right", obj.safe_right));
    try_load(ar, ser20::make_nvp("safe_bottom", obj.safe_bottom));
}

namespace
{
using resolution_preset = settings::resolution_settings::resolution;

/// The presets every project was created with before presets had a device.
auto get_presets_before_devices() -> std::vector<resolution_preset>
{
    return {{"Free Aspect", 0, 0, 0.0f},
            {"16:9 Aspect", 0, 0, 16.0f / 9.0f},
            {"16:10 Aspect", 0, 0, 16.0f / 10.0f},
            {"Full HD (1920x1080)", 1920, 1080, 16.0f / 9.0f},
            {"WXGA (1366x768)", 1366, 768, 16.0f / 9.0f},
            {"QHD (2560x1440)", 2560, 1440, 16.0f / 9.0f},
            {"4K UHD (3840x2160)", 3840, 2160, 16.0f / 9.0f}};
}

auto has_preset_named(const std::vector<resolution_preset>& presets, const std::string& name) -> bool
{
    return std::any_of(presets.begin(),
                       presets.end(),
                       [&](const resolution_preset& preset)
                       {
                           return preset.name == name;
                       });
}

/// Presets with pixels match by their size, the others by their aspect.
auto has_same_shape(const resolution_preset& lhs, const resolution_preset& rhs) -> bool
{
    if(lhs.width != rhs.width || lhs.height != rhs.height)
    {
        return false;
    }
    return lhs.width > 0 || lhs.aspect == rhs.aspect;
}

/// Brings presets saved before presets had a device up to date. A project that kept the old defaults gets today's,
/// with its pick kept by shape; a list of the project's own keeps its presets and gains the default phones and tablets.
void add_device_presets(settings::resolution_settings& obj)
{
    const settings::resolution_settings defaults{};
    if(obj.resolutions == get_presets_before_devices())
    {
        const auto same_shape = std::find_if(defaults.resolutions.begin(),
                                             defaults.resolutions.end(),
                                             [&](const resolution_preset& preset)
                                             {
                                                 return has_same_shape(preset, obj.current_resolution);
                                             });
        obj.resolutions = defaults.resolutions;
        obj.current_resolution = same_shape != defaults.resolutions.end() ? *same_shape : defaults.current_resolution;
        return;
    }
    for(const auto& preset : defaults.resolutions)
    {
        if(preset.device != settings::resolution_settings::device_type::desktop &&
           !has_preset_named(obj.resolutions, preset.name))
        {
            obj.resolutions.push_back(preset);
        }
    }
}
} // namespace

SAVE_INLINE(settings::resolution_settings)
{
    try_save(ar, ser20::make_nvp("resolutions", obj.resolutions));
    try_save(ar, ser20::make_nvp("current_resolution", obj.current_resolution));
    try_save(ar, ser20::make_nvp("orientation", obj.orientation));
}

LOAD_INLINE(settings::resolution_settings)
{
    try_load(ar, ser20::make_nvp("resolutions", obj.resolutions));
    try_load(ar, ser20::make_nvp("current_resolution", obj.current_resolution));
    // The orientation came with the devices: settings without it were saved before phones and tablets were presets.
    if(!try_load(ar, ser20::make_nvp("orientation", obj.orientation)))
    {
        add_device_presets(obj);
    }
}

REFLECT_INLINE(target_platform)
{
    entt::meta_factory<target_platform>{}
        .type("target_platform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "target_platform"},
            entt::attribute{"pretty_name", "Platform"},
        })
        .data<target_platform::desktop>("desktop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "desktop"},
            entt::attribute{"pretty_name", "Desktop"},
        })
        .data<target_platform::android>("android"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android"},
            entt::attribute{"pretty_name", "Android"},
        });
}

REFLECT_INLINE(settings::build_settings)
{
    entt::meta_factory<settings::build_settings>{}
        .type("build_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "build_settings"},
            entt::attribute{"pretty_name", "Build"},
        })
        .data<&settings::build_settings::profile>("profile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "profile"},
            entt::attribute{"pretty_name", "Profile"},
            entt::attribute{"tooltip", "The settings profile the build was made from."},
        })
        .data<&settings::build_settings::platform>("platform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "platform"},
            entt::attribute{"pretty_name", "Platform"},
            entt::attribute{"tooltip", "The platform the build runs on."},
        });
}

SAVE_INLINE(settings::build_settings)
{
    try_save(ar, ser20::make_nvp("profile", obj.profile));
    try_save(ar, ser20::make_nvp("platform", obj.platform));
}

LOAD_INLINE(settings::build_settings)
{
    try_load(ar, ser20::make_nvp("profile", obj.profile));
    try_load(ar, ser20::make_nvp("platform", obj.platform));
}

REFLECT_INLINE(settings::resolution_settings::resolution)
{
    entt::meta_factory<settings::resolution_settings::resolution>{}
        .type("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
        })
        .data<&settings::resolution_settings::resolution::name>("name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "name"},
            entt::attribute{"pretty_name", "Name"},
            entt::attribute{"tooltip", "Display name for this resolution"},
        })
        .data<&settings::resolution_settings::resolution::width>("width"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "width"},
            entt::attribute{"pretty_name", "Width"},
            entt::attribute{"min", 0},
            entt::attribute{"tooltip", "Width in pixels (0 for free aspect)"},
        })
        .data<&settings::resolution_settings::resolution::height>("height"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "height"},
            entt::attribute{"pretty_name", "Height"},
            entt::attribute{"min", 0},
            entt::attribute{"tooltip", "Height in pixels (0 for free aspect)"},
        })
        .data<&settings::resolution_settings::resolution::aspect>("aspect"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "aspect"},
            entt::attribute{"pretty_name", "Aspect Ratio"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"tooltip", "Aspect ratio (0 for free aspect)"},
        })
        .data<&settings::resolution_settings::resolution::device>("device"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "device"},
            entt::attribute{"pretty_name", "Device"},
            entt::attribute{"tooltip", "The device the preset stands for. The views show a phone or a tablet in its frame."},
        });
}

REFLECT_INLINE(settings::resolution_settings::device_type)
{
    entt::meta_factory<settings::resolution_settings::device_type>{}
        .type("resolution_device_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution_device_type"},
            entt::attribute{"pretty_name", "Device"},
        })
        .data<settings::resolution_settings::device_type::desktop>("desktop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "desktop"},
            entt::attribute{"pretty_name", "Desktop"},
        })
        .data<settings::resolution_settings::device_type::phone>("phone"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "phone"},
            entt::attribute{"pretty_name", "Phone"},
        })
        .data<settings::resolution_settings::device_type::tablet>("tablet"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tablet"},
            entt::attribute{"pretty_name", "Tablet"},
        });
}

REFLECT_INLINE(settings::resolution_settings::screen_orientation)
{
    entt::meta_factory<settings::resolution_settings::screen_orientation>{}
        .type("resolution_screen_orientation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution_screen_orientation"},
            entt::attribute{"pretty_name", "Orientation"},
        })
        .data<settings::resolution_settings::screen_orientation::portrait>("portrait"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "portrait"},
            entt::attribute{"pretty_name", "Portrait"},
        })
        .data<settings::resolution_settings::screen_orientation::landscape>("landscape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "landscape"},
            entt::attribute{"pretty_name", "Landscape"},
        });
}

REFLECT_INLINE(settings::resolution_settings)
{
    entt::meta_factory<settings::resolution_settings>{}
        .type("resolution_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution_settings"},
            entt::attribute{"pretty_name", "Resolution Settings"},
        })
        .data<&settings::resolution_settings::resolutions>("resolutions"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolutions"},
            entt::attribute{"pretty_name", "Resolutions"},
            entt::attribute{"tooltip", "List of available resolutions"},
        })
        .data<&settings::resolution_settings::current_resolution>("current_resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "current_resolution"},
            entt::attribute{"pretty_name", "Current Resolution"},
            entt::attribute{"tooltip", "The current resolution to use"},
        })
        .data<&settings::resolution_settings::orientation>("orientation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "orientation"},
            entt::attribute{"pretty_name", "Orientation"},
            entt::attribute{"tooltip", "How the Game view holds a phone or a tablet preset."},
        });
}

REFLECT(settings)
{

    entt::meta_factory<settings>{}
        .type("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
        })
        .data<&settings::app>("app"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "app"},
            entt::attribute{"pretty_name", "Application"},
            entt::attribute{"tooltip", "Missing..."},
        })
        .data<&settings::graphics>("graphics"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "graphics"},
            entt::attribute{"pretty_name", "Graphics"},
            entt::attribute{"tooltip", "Missing..."},
        })
        .data<&settings::global_illumination>("global_illumination"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "global_illumination"},
            entt::attribute{"pretty_name", "Global Illumination"},
            entt::attribute{"tooltip", "Project-wide budgets and card resolution of the surface cache."},
        })
        .data<&settings::splash>("splash"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "splash"},
            entt::attribute{"pretty_name", "Splash Screen"},
            entt::attribute{"tooltip", "Play mode splash screen shown before the game starts."},
        })
        .data<&settings::standalone>("standalone"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "standalone"},
            entt::attribute{"pretty_name", "Standalone"},
            entt::attribute{"tooltip", "Missing..."},
        })
        .data<&settings::physics>("physics"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics"},
            entt::attribute{"pretty_name", "Physics"},
            entt::attribute{"tooltip", "Physics backend and fixed-step simulation settings."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"tooltip", "Resolution settings for the project"},
        })
        .data<&settings::assets>("assets"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "assets"},
            entt::attribute{"pretty_name", "Assets"},
            entt::attribute{"tooltip", "Import defaults for the project's assets"},
        });
}

SAVE(settings)
{
    try_save(ar, ser20::make_nvp("app", obj.app));
    try_save(ar, ser20::make_nvp("assets", obj.assets));
    try_save(ar, ser20::make_nvp("graphics", obj.graphics));
    try_save(ar, ser20::make_nvp("global_illumination", obj.global_illumination));
    try_save(ar, ser20::make_nvp("splash", obj.splash));
    try_save(ar, ser20::make_nvp("standalone", obj.standalone));
    try_save(ar, ser20::make_nvp("layer", obj.layer));
    try_save(ar, ser20::make_nvp("input", obj.input));
    try_save(ar, ser20::make_nvp("physics", obj.physics));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    // Only a deploy names a build; the project's own settings carry no build section.
    if(!obj.build.profile.empty())
    {
        try_save(ar, ser20::make_nvp("build", obj.build));
    }
}
SAVE_INSTANTIATE(settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(settings, ser20::oarchive_binary_t);

LOAD(settings)
{
    try_load(ar, ser20::make_nvp("app", obj.app));
    try_load(ar, ser20::make_nvp("assets", obj.assets));
    try_load(ar, ser20::make_nvp("graphics", obj.graphics));
    try_load(ar, ser20::make_nvp("global_illumination", obj.global_illumination));
    try_load(ar, ser20::make_nvp("splash", obj.splash));
    try_load(ar, ser20::make_nvp("standalone", obj.standalone));
    try_load(ar, ser20::make_nvp("layer", obj.layer));
    try_load(ar, ser20::make_nvp("input", obj.input));
    // Prefer "physics"; fall back to legacy "time" (timestep fields only).
    if(!try_load(ar, ser20::make_nvp("physics", obj.physics)))
    {
        try_load(ar, ser20::make_nvp("time", obj.physics));
    }
    // Prefer "resolution", the property's name; fall back to legacy "resolutions".
    if(!try_load(ar, ser20::make_nvp("resolution", obj.resolution)))
    {
        try_load(ar, ser20::make_nvp("resolutions", obj.resolution));
    }
    try_load(ar, ser20::make_nvp("build", obj.build));
}
LOAD_INSTANTIATE(settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(settings, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const settings& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("settings", obj));
    }
}

void save_to_file_bin(const std::string& absolute_path, const settings& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("settings", obj));
    }
}

auto load_from_file(const std::string& absolute_path, settings& obj) -> bool
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_iarchive_associative(stream);
        return try_load(ar, ser20::make_nvp("settings", obj));
    }

    return false;
}

auto load_from_file_bin(const std::string& absolute_path, settings& obj) -> bool
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::iarchive_binary_t ar(stream);
        return try_load(ar, ser20::make_nvp("settings", obj));
    }

    return false;
}

/**
 * @brief Load-only view of the cold-boot sections of a settings document.
 *
 * Deliberately a separate type from @ref settings: loading it visits the "graphics" and
 * "physics" nodes and nothing else, so it can never reach an asset handle.
 */
struct settings_boot_sections
{
    settings::graphics_settings graphics{};
    settings::physics_settings physics{};
};

LOAD_INLINE(settings_boot_sections)
{
    try_load(ar, ser20::make_nvp("graphics", obj.graphics));
    try_load(ar, ser20::make_nvp("physics", obj.physics));
}

auto load_boot_sections_from_file(const std::string& absolute_path, settings& obj) -> bool
{
    std::ifstream stream(absolute_path);
    if(!stream.good())
    {
        return false;
    }
    settings_boot_sections sections{};
    auto ar = ser20::create_iarchive_associative(stream);
    if(!try_load(ar, ser20::make_nvp("settings", sections)))
    {
        return false;
    }
    obj.graphics = sections.graphics;
    obj.physics = sections.physics;
    return true;
}

/**
 * @brief Load-only view of the import defaults of a settings document; it never reaches an asset
 * handle, for the same reason as settings_boot_sections.
 */
struct settings_asset_sections
{
    settings::asset_settings assets{};
};

LOAD_INLINE(settings_asset_sections)
{
    try_load(ar, ser20::make_nvp("assets", obj.assets));
}

auto load_asset_sections_from_file(const std::string& absolute_path, settings::asset_settings& obj) -> bool
{
    std::ifstream stream(absolute_path);
    if(!stream.good())
    {
        return false;
    }
    settings_asset_sections sections{};
    sections.assets = obj;
    auto ar = ser20::create_iarchive_associative(stream);
    if(!try_load(ar, ser20::make_nvp("settings", sections)))
    {
        return false;
    }
    obj = sections.assets;
    return true;
}

/**
 * @brief Load-only view of the application section of a settings document; it never reaches an
 * asset handle, for the same reason as settings_boot_sections.
 */
struct settings_app_section
{
    settings::app_settings app{};
};

LOAD_INLINE(settings_app_section)
{
    try_load(ar, ser20::make_nvp("app", obj.app));
}

auto load_app_section_from_file(const std::string& absolute_path, settings::app_settings& obj) -> bool
{
    std::ifstream stream(absolute_path);
    if(!stream.good())
    {
        return false;
    }
    settings_app_section section{};
    section.app = obj;
    auto ar = ser20::create_iarchive_associative(stream);
    if(!try_load(ar, ser20::make_nvp("settings", section)))
    {
        return false;
    }
    obj = section.app;
    return true;
}

} // namespace unravel
