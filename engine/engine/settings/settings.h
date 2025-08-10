#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <engine/assets/asset_handle.h>
#include <engine/ecs/scene.h>
#include <engine/layers/layer_mask.h>
#include <engine/input/input.h>
#include <engine/assets/asset_manager.h>

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
    } app;

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
        friend auto operator==(const graphics_settings& lhs, const graphics_settings& rhs) -> bool = default;
    } graphics;

    struct resolution_settings
    {
        friend auto operator==(const resolution_settings& lhs, const resolution_settings& rhs) -> bool = default;

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

    struct time_settings
    {
        friend auto operator==(const time_settings& lhs, const time_settings& rhs) -> bool = default;

        float fixed_timestep{0.02f};
        int max_fixed_steps{3};
    } time;

    friend auto operator==(const settings& lhs, const settings& rhs) -> bool = default;

};
} // namespace unravel
