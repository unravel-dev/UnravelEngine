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
#include <engine/settings/boot_config.h>
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
        /// GPU resource eviction/paging policy. Persisted with the project so paging behaves the same
        /// in standalone builds; the renderer drives it every frame from this configuration.
        eviction_settings eviction;

        friend auto operator==(const graphics_settings& lhs, const graphics_settings& rhs) -> bool = default;
    } graphics;

    struct resolution_settings
    {
        friend auto operator==(const resolution_settings& lhs, const resolution_settings& rhs) -> bool = default;

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

            friend auto operator==(const resolution& lhs, const resolution& rhs) -> bool = default;
        };
    
        std::vector<resolution> resolutions = {
            {"Free Aspect", 0, 0, 0.0f},
            {"16:9 Aspect", 0, 0, 16.0f / 9.0f},
            {"16:10 Aspect", 0, 0, 16.0f / 10.0f},
            {"Full HD (1920x1080)", 1920, 1080, 16.0f / 9.0f},
            {"WXGA (1366x768)", 1366, 768, 16.0f / 9.0f},
            {"QHD (2560x1440)", 2560, 1440, 16.0f / 9.0f},
            {"4K UHD (3840x2160)", 3840, 2160, 16.0f / 9.0f}
        };

        resolution current_resolution = resolutions[0];
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
        physics_backend_type backend{physics_backend_type::bullet};
        float fixed_timestep{0.02f};
        int max_fixed_steps{3};

        /// Constraint solver iterations per step. Bullet's default is 10; a pile of
        /// primitives is usually indistinguishable at 4-6, and solver time scales
        /// close to linearly with this.
        int solver_iterations{10};

        friend auto operator==(const physics_settings& lhs, const physics_settings& rhs) -> bool = default;
    } physics;

    friend auto operator==(const settings& lhs, const settings& rhs) -> bool = default;

};
} // namespace unravel
