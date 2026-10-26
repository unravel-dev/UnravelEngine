#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <engine/assets/asset_handle.h>
#include <engine/ecs/scene.h>
#include <engine/layers/layer_mask.h>
#include <engine/input/input.h>
#include <engine/assets/asset_manager.h>
#include <engine/physics/physics_types.h>
#include <engine/rendering/eviction_settings.h>
#include <engine/rendering/feature_limits.h>
#include <engine/rendering/render_scale.h>
#include <engine/rendering/gi/gi_project_settings.h>
#include <engine/rendering/particles/ps/particle_types.h>
#include <engine/settings/boot_config.h>
#include <engine/settings/target_platform.h>
#include <graphics/texture.h>

#include <string>
#include <vector>


namespace unravel
{

struct settings
{
    struct app_settings
    {
        std::string company;
        std::string product;
        std::string version;
        friend auto operator==(const app_settings& lhs, const app_settings& rhs) -> bool = default;

        auto operator=(const app_settings& rhs) -> app_settings&
        {
            if(this == &rhs)
            {
                return *this;
            }
            company = rhs.company;
            product = rhs.product;
            version = rhs.version;
            return *this;
        }
    } app;

    struct splash_logo_entry
    {
        asset_handle<gfx::texture> logo;
        float duration_sec = 2.0f;

        friend auto operator==(const splash_logo_entry& lhs, const splash_logo_entry& rhs) -> bool = default;
    };

    struct splash_settings
    {
        bool enabled = true;
        bool show_made_with = true;
        float fade_in_sec = 0.5f;
        float fade_out_sec = 0.5f;
        std::vector<splash_logo_entry> logos;

        friend auto operator==(const splash_settings& lhs, const splash_settings& rhs) -> bool = default;
    } splash;

    struct asset_settings
    {
        struct texture_importer_settings
        {
            texture_importer_meta::texture_size default_max_size{texture_importer_meta::texture_size::size_2048};
            texture_importer_meta::compression_quality default_compression{texture_importer_meta::compression_quality::normal_quality};
            friend auto operator==(const texture_importer_settings& lhs, const texture_importer_settings& rhs) -> bool = default;
        } texture;

        struct mesh_settings
        {
            friend auto operator==(const mesh_settings& lhs, const mesh_settings& rhs) -> bool = default;
        } mesh;

        friend auto operator==(const asset_settings& lhs, const asset_settings& rhs) -> bool = default;
    } assets;

    struct graphics_settings
    {
        /// Per-platform preferred renderer. Applied at process start (cold); changing it requires restart.
        platform_renderer_settings renderer;
        /// Draws the meshes that share a mesh and a material with one instanced draw call. The renderer
        /// applies it every frame.
        bool static_mesh_batching{true};
        /// Draws the skinned meshes that share a mesh and a material with one instanced draw call, their
        /// bone palettes uploaded once per frame for every view. The renderer applies it every frame.
        bool skinned_mesh_batching{true};
        /// Keeps a light's shadow map, or a cascade of the sun, across frames while nothing it shows changed,
        /// instead of rendering it again every frame. The renderer applies it every frame.
        bool shadow_map_caching{true};
        /// GPU resource eviction/paging policy. Persisted with the project so paging behaves the same
        /// in standalone builds; the renderer drives it every frame from this configuration.
        eviction_settings eviction;

        /// Project-wide particle quality and budgets. The renderer applies them every frame.
        struct particle_settings
        {
            /// Multiplier of every emitter's rates and bursts (0..1): fewer particles on smaller targets.
            float spawn_scale{1.0f};
            /// Sort particles where their emitters ask for it; off draws them in spawn order (cheaper; alpha-
            /// blended layers may then show order errors).
            bool sorting{true};
            /// Draw every emitter that can (all but multiply blend) at half resolution.
            bool force_half_resolution{false};
            /// Emitters smaller on screen than this fraction of the view's height are not drawn, and freeze with
            /// renderer-based culling. 0 = off.
            float min_screen_size{0.0f};
            /// Most particles one view draws: past it the emitters smallest on screen are skipped. 0 = no limit.
            uint32_t max_drawn_particles{0};
            /// Multiplier of every emitter's Max Particles (0..1): spawns past the scaled capacity are dropped.
            float max_particles_scale{1.0f};
            /// The most expensive lighting mode particles use: lower modes stand in for higher ones.
            ps_soa::particle_lighting max_lighting{ps_soa::particle_lighting::surface};

            friend auto operator==(const particle_settings& lhs, const particle_settings& rhs) -> bool = default;
        } particles;

        /// The fraction of a game view's size its scene renders at (MIN_RENDER_SCALE..1). The finished scene
        /// is upscaled to the view's size before the screen-space UI. The editor's views can use it, a scale
        /// of their own or none (view_render_params). The renderer applies it every frame.
        float render_scale{1.0f};
        /// The most expensive rendering features scenes may use. The renderer applies them every frame.
        feature_limits limits;

        friend auto operator==(const graphics_settings& lhs, const graphics_settings& rhs) -> bool = default;
    } graphics;

    /// Project-wide global illumination: the surface cache's budgets and card resolution rule. The renderer
    /// applies them every frame; the per-volume settings live on the Global Illumination component.
    gi_project_settings global_illumination;

    struct resolution_settings
    {
        friend auto operator==(const resolution_settings& lhs, const resolution_settings& rhs) -> bool = default;

        /// What a preset stands for. The pickers of the views group the presets by it, and the views show a phone
        /// or a tablet inside a frame of that device.
        enum class device_type : uint8_t
        {
            desktop,
            phone,
            tablet
        };

        /// How a phone or a tablet is held. Desktop presets keep the shape they have.
        enum class screen_orientation : uint8_t
        {
            portrait,
            landscape
        };

        auto get_current_resolution_index() const -> int
        {
            for(size_t i = 0; i < resolutions.size(); ++i)
            {
                if(resolutions[i].name == current_resolution.name)
                {
                    return static_cast<int>(i);
                }
            }
            return 0;
        }
        auto set_current_resolution_index(size_t index) -> void
        {
            if(index < resolutions.size())
            {
                current_resolution = resolutions[index];
            }
        }

        struct resolution
        {
            std::string name;
            int width;
            int height;
            float aspect; // 0 means free aspect
            /// Phones and tablets are listed held upright; the view's orientation turns them.
            device_type device{device_type::desktop};

            friend auto operator==(const resolution& lhs, const resolution& rhs) -> bool = default;
        };

        // A few common shapes per device rather than every model: the shapes are what a layout has to survive.
        std::vector<resolution> resolutions = {
            {"Free Aspect", 0, 0, 0.0f},
            {"16:9 Aspect", 0, 0, 16.0f / 9.0f},
            {"16:10 Aspect", 0, 0, 16.0f / 10.0f},
            {"WXGA", 1366, 768, 1366.0f / 768.0f},
            {"Full HD", 1920, 1080, 1920.0f / 1080.0f},
            {"QHD", 2560, 1440, 2560.0f / 1440.0f},
            {"4K UHD", 3840, 2160, 3840.0f / 2160.0f},
            {"Phone 16:9", 1080, 1920, 1080.0f / 1920.0f, device_type::phone},
            {"Phone 19.5:9", 1080, 2340, 1080.0f / 2340.0f, device_type::phone},
            {"Phone 20:9", 1080, 2400, 1080.0f / 2400.0f, device_type::phone},
            {"Phone 20:9 HD+", 720, 1600, 720.0f / 1600.0f, device_type::phone},
            {"Tablet 4:3", 1536, 2048, 1536.0f / 2048.0f, device_type::tablet},
            {"Tablet 16:10", 1200, 1920, 1200.0f / 1920.0f, device_type::tablet}
        };

        resolution current_resolution = resolutions[0];
        /// How the Game view holds a phone or a tablet preset.
        screen_orientation orientation{screen_orientation::portrait};
    } resolution;
   

    struct standalone_settings
    {
        friend auto operator==(const standalone_settings& lhs, const standalone_settings& rhs) -> bool = default;

        asset_handle<scene_prefab> startup_scene;
    } standalone;

    struct input_settings
    {
        friend auto operator==(const input_settings& lhs, const input_settings& rhs) -> bool = default;

        input::action_map actions = input_system::get_default_mapping();
    } input;

    struct layer_settings
    {
        friend auto operator==(const layer_settings& lhs, const layer_settings& rhs) -> bool = default;

        std::array<std::string, 32> layers = get_reserved_layers_as_array();
    } layer;

    struct physics_settings
    {
        /// Cold: physics engine adapter. Applied at process start; changing it requires restart.
        physics_backend_type backend{physics_backend_type::auto_detect};
        float fixed_timestep{0.02f};
        int max_fixed_steps{3};

        /// Constraint solver iterations per step. Bullet's default is 10; a pile of
        /// primitives is usually indistinguishable at 4-6, and solver time scales
        /// close to linearly with this.
        int solver_iterations{10};

        friend auto operator==(const physics_settings& lhs, const physics_settings& rhs) -> bool = default;
    } physics;

    /// What a deployed game was built as. The deploy writes it into the settings it ships; in the
    /// editor it names the profile being previewed. Empty in the project's own settings.
    struct build_settings
    {
        /// The settings profile the build was made from.
        std::string profile;
        target_platform platform{target_platform::desktop};

        friend auto operator==(const build_settings& lhs, const build_settings& rhs) -> bool = default;
    } build;

    friend auto operator==(const settings& lhs, const settings& rhs) -> bool = default;

};
} // namespace unravel
