#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/assets/asset_handle.h>
#include <engine/ecs/prefab.h>
#include <engine/threading/threader.h>
#include <editor/deploy/deploy.h>

#include <filesystem/filesystem.h>

namespace unravel
{

struct editor_actions
{
    static auto new_scene(rtti::context& ctx) -> bool;
    static auto open_scene(rtti::context& ctx) -> bool;
    static auto open_scene_from_asset(rtti::context& ctx, const asset_handle<scene_prefab>& asset) -> bool;
    static auto save_scene(rtti::context& ctx) -> bool;
    static auto save_scene_as(rtti::context& ctx) -> bool;
    static auto prompt_save_scene(rtti::context& ctx) -> bool;

    static auto close_project(rtti::context& ctx) -> bool;
    static auto reload_project(rtti::context& ctx) -> bool;

    static void run_project(const deploy_settings& params);
    static auto deploy_project(rtti::context& ctx, const deploy_settings& params)
        -> std::map<std::string, tpp::shared_future<void>>;


    static void recompile_shaders();
    static void recompile_textures();
    static void recompile_scripts();
    static void recompile_all();
    static void generate_script_workspace();
    static void open_workspace_on_file(const fs::path& file, int line = 0);

};

} // namespace unravel
