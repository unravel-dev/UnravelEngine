#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/assets/asset_handle.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/prefab.h>
#include <engine/threading/threader.h>
#include <editor/deploy/deploy.h>

#include <filesystem/filesystem.h>

#include <functional>
#include <string>

namespace unravel
{

struct editor_actions
{
    static auto new_scene(rtti::context& ctx) -> bool;
    static auto open_scene(rtti::context& ctx) -> bool;
    static auto open_scene_from_asset(rtti::context& ctx, const asset_handle<scene_prefab>& asset) -> bool;
    static auto save_scene(rtti::context& ctx) -> bool;
    static auto save_scene_as(rtti::context& ctx) -> bool;
    static auto prompt_save_scene(rtti::context& ctx, const std::function<void()>& on_continue) -> bool;

    /**
     * @brief Non-modal scene load (shared by File menu + MCP). Clears edit state, loads asset,
     * syncs prefabs, clears unsaved flag, and updates project opened_scene. Returns false on failure.
     */
    static auto load_scene_from_asset(rtti::context& ctx,
                                      const asset_handle<scene_prefab>& asset,
                                      std::string* error = nullptr) -> bool;

    /**
     * @brief Non-modal new scene from preset (shared by create-scene modal + MCP). Cancels any
     * pending create-scene modal, builds defaults::create_scene_from_preset, clears opened_scene.
     */
    static auto new_scene_from_preset(rtti::context& ctx, defaults::scene_preset preset) -> bool;

    /**
     * @brief Atomic-save active scene to path/key. When update_source is true, sets scene.source
     * and project opened_scene. Returns false on play mode or write failure.
     */
    static auto save_scene_to_path(rtti::context& ctx,
                                   const fs::path& path,
                                   bool update_source = true,
                                   bool show_notification = true) -> bool;

    static auto close_project(rtti::context& ctx) -> bool;
    static auto reload_project(rtti::context& ctx) -> bool;

    static void run_project(const fs::path& executable_path);
    static auto deploy_project(rtti::context& ctx, const deploy_settings& params)
        -> std::map<std::string, tpp::shared_future<void>>;
    static auto can_deploy_project(rtti::context& ctx, const deploy_settings& params) -> bool;


    static void recompile_shaders(const std::string& group = "");
    static void recompile_textures(const std::string& group = "");
    static void recompile_ui(const std::string& group = "");
    static void recompile_scripts(const std::string& group = "");
    static void recompile_meshes(const std::string& group = "");
    static void recompile_all(const std::string& group = "");
    static void generate_script_workspace();
    static void open_workspace_on_file(const fs::path& file, int line = 0);

    /**
     * @brief Flags every reflection probe across all loaded scenes for rebuild.
     *
     * Mirrors Unreal's "Build > Build Reflection Captures" menu. When force_full_first_frame
     * is true, each probe bakes all six faces in a single frame for instant visible results;
     * when false, bakes are time-sliced to avoid editor hitches.
     *
     * @return Number of probes that were flagged.
     */
    static auto rebuild_reflection_probes(rtti::context& ctx, bool force_full_first_frame = true) -> size_t;

};

} // namespace unravel
