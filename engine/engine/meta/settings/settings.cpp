#include "settings.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/array.hpp>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/input/input.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/physics/physics_types.h>
#include <engine/settings/boot_config.h>

namespace unravel
{

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
            entt::attribute{"tooltip", "Missing..."},
        })
        .data<&settings::app_settings::product>("product"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "product"},
            entt::attribute{"pretty_name", "Product"},
            entt::attribute{"tooltip", "Missing..."},
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
            entt::attribute{"tooltip", "Evict down to this fraction of the maximum (hysteresis; keep < budget)."},
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
        });
}

SAVE_INLINE(platform_renderer_settings)
{
    try_save(ar, ser20::make_nvp("windows", obj.windows));
    try_save(ar, ser20::make_nvp("linux", obj.linux));
    try_save(ar, ser20::make_nvp("macos", obj.macos));
}

LOAD_INLINE(platform_renderer_settings)
{
    try_load(ar, ser20::make_nvp("windows", obj.windows));
    try_load(ar, ser20::make_nvp("linux", obj.linux));
    try_load(ar, ser20::make_nvp("macos", obj.macos));
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
                            "Per-platform preferred graphics backend. Applied at process start; requires restart."},
        })
        .data<&settings::graphics_settings::eviction>("eviction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "eviction"},
            entt::attribute{"pretty_name", "GPU Eviction / Paging"},
            entt::attribute{"tooltip", "Controls automatic eviction/paging of GPU resources to stay under budget."},
        });
}

SAVE_INLINE(settings::graphics_settings)
{
    try_save(ar, ser20::make_nvp("renderer", obj.renderer));
    try_save(ar, ser20::make_nvp("eviction", obj.eviction));
}

LOAD_INLINE(settings::graphics_settings)
{
    try_load(ar, ser20::make_nvp("renderer", obj.renderer));
    try_load(ar, ser20::make_nvp("eviction", obj.eviction));
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
        });
}

SAVE_INLINE(settings::standalone_settings)
{
    try_save(ar, ser20::make_nvp("startup_scene", obj.startup_scene));
}

LOAD_INLINE(settings::standalone_settings)
{
    try_load(ar, ser20::make_nvp("startup_scene", obj.startup_scene));
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
            entt::attribute{"tooltip", "Physics engine adapter. Applied at process start; requires editor restart."},
        })
        .data<&settings::physics_settings::fixed_timestep>("fixed_timestep"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fixed_timestep"},
            entt::attribute{"pretty_name", "Fixed Timestep"},
            entt::attribute{"step", 0.001f},
            entt::attribute{"tooltip",
                            "A framerate-independent interval which dictates when physics calculations and "
                            "FixedUpdate events are performed."},
        })
        .data<&settings::physics_settings::max_fixed_steps>("max_fixed_steps"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_fixed_steps"},
            entt::attribute{"pretty_name", "Max Fixed Steps"},
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
}

LOAD_INLINE(settings::resolution_settings::resolution)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("width", obj.width));
    try_load(ar, ser20::make_nvp("height", obj.height));
    try_load(ar, ser20::make_nvp("aspect", obj.aspect));
}

SAVE_INLINE(settings::resolution_settings)
{
    try_save(ar, ser20::make_nvp("resolutions", obj.resolutions));
    try_save(ar, ser20::make_nvp("current_resolution", obj.current_resolution));
}

LOAD_INLINE(settings::resolution_settings)
{
    try_load(ar, ser20::make_nvp("resolutions", obj.resolutions));
    try_load(ar, ser20::make_nvp("current_resolution", obj.current_resolution));
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
        });
}

SAVE(settings)
{
    try_save(ar, ser20::make_nvp("app", obj.app));
    try_save(ar, ser20::make_nvp("assets", obj.assets));
    try_save(ar, ser20::make_nvp("graphics", obj.graphics));
    try_save(ar, ser20::make_nvp("splash", obj.splash));
    try_save(ar, ser20::make_nvp("standalone", obj.standalone));
    try_save(ar, ser20::make_nvp("layer", obj.layer));
    try_save(ar, ser20::make_nvp("input", obj.input));
    try_save(ar, ser20::make_nvp("physics", obj.physics));
    try_save(ar, ser20::make_nvp("resolutions", obj.resolution));
}
SAVE_INSTANTIATE(settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(settings, ser20::oarchive_binary_t);

LOAD(settings)
{
    try_load(ar, ser20::make_nvp("app", obj.app));
    try_load(ar, ser20::make_nvp("assets", obj.assets));
    try_load(ar, ser20::make_nvp("graphics", obj.graphics));
    try_load(ar, ser20::make_nvp("splash", obj.splash));
    try_load(ar, ser20::make_nvp("standalone", obj.standalone));
    try_load(ar, ser20::make_nvp("layer", obj.layer));
    try_load(ar, ser20::make_nvp("input", obj.input));
    // Prefer "physics"; fall back to legacy "time" (timestep fields only).
    if(!try_load(ar, ser20::make_nvp("physics", obj.physics)))
    {
        try_load(ar, ser20::make_nvp("time", obj.physics));
    }
    try_load(ar, ser20::make_nvp("resolutions", obj.resolution));
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

} // namespace unravel
