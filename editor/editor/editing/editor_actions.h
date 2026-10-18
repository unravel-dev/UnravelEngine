#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/assets/asset_handle.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/prefab.h>
#include <engine/threading/threader.h>
#include <logging/logging.h>

#include <filesystem/filesystem.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace unravel
{

struct editing_action_t;
struct asset_meta;
struct animation_controller;

struct play_state_info
{
    std::string phase{"inactive"};
    bool is_active{false};
    bool is_paused{false};
    bool is_splash{false};
    bool is_simulation_running{false};
    uint64_t frames_running{0};
    uint64_t frames_simulated{0};
};

struct selection_info
{
    std::string active_entity_id;
    std::vector<std::string> entity_ids;
};

struct log_query_entry
{
    uint64_t id{0};
    level::level_enum level{level::off};
    std::string text;
    std::string filename;
    std::string funcname;
    int line{0};
};

/**
 * @brief One content-browser-style import entry (external path -> project folder).
 */
struct import_files_item
{
    std::string source_path;
    std::string dest_path;
    std::string dest_key;
    bool is_directory{false};
};

/**
 * @brief Batch import result: resolved items plus one future for the whole copy job.
 *
 * When async, `future` is a single thread-pool job that copies every item (watcher paused
 * for the batch). When sync, `future` is already ready with the overall success flag.
 */
struct import_files_result
{
    std::vector<import_files_item> items;
    tpp::shared_future<bool> future;
};

struct editor_actions
{
    static auto new_scene(rtti::context& ctx) -> bool;
    static auto open_scene(rtti::context& ctx) -> bool;
    static auto open_scene_from_asset(rtti::context& ctx, const asset_handle<scene_prefab>& asset) -> bool;

    /**
     * @brief Opens the Animation Controller editor on a controller asset.
     */
    static void open_animation_controller(rtti::context& ctx, const asset_handle<animation_controller>& asset);
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
    static auto restart_editor(rtti::context& ctx) -> bool;

    static void recompile_shaders(const std::string& group = "");
    static void recompile_textures(const std::string& group = "");
    /// Writes an asset's import settings (@p meta, the asset database's entry) to the .meta file beside
    /// the asset @p key names; the asset watcher then imports the asset again with them.
    static auto save_import_settings(const std::string& key, const asset_meta& meta) -> bool;

    /**
     * @brief Writes `entity` and its children to a prefab file and, with `attach`, makes the entity
     *        an instance of it - what dropping an entity on the Content Browser does.
     *
     * The entity becomes the source of the new prefab, so nothing of it is overridden: its old prefab
     * bookkeeping (overrides, instance id and document) is reset, and a nested-instance slot it
     * filled is stated removed on its container.
     *
     * @param prefab_path Absolute path of the .pfb to write; its folder must exist.
     * @return The prefab, or an empty handle when the file could not be written.
     */
    static auto save_entity_as_prefab(rtti::context& ctx, entt::handle entity, const fs::path& prefab_path, bool attach)
        -> asset_handle<prefab>;
    /// One-time migration: tags textures referenced by material slots with their
    /// authored color space (base color/emissive = sRGB, data maps = linear) and
    /// recompiles the changed ones. Metas with an explicit (non-automatic) color
    /// space are left untouched. Returns the number of textures tagged.
    static auto migrate_texture_color_spaces(const std::string& group = "") -> size_t;
    /// Re-saves every prefab and scene in the project in the current format: nested prefabs
    /// before the prefabs that contain them (a container must never name a nested asset's
    /// slots), scenes last. Returns the number of files rewritten; `total_count` receives how
    /// many were attempted.
    static auto migrate_prefabs(rtti::context& ctx, size_t* total_count = nullptr) -> size_t;
    static void recompile_ui(const std::string& group = "");
    static void recompile_scripts(const std::string& group = "");
    static void recompile_meshes(const std::string& group = "");
    static void recompile_all(const std::string& group = "");
    static void generate_script_workspace();
    static void open_workspace_on_file(const fs::path& file, int line = 0);

    /**
     * @brief Flags every reflection probe across all loaded scenes for rebuild.
     *
     * Backs the editor's "Build Reflection Captures" menu item. When force_full_first_frame
     * is true, each probe bakes all six faces in a single frame for instant visible results;
     * when false, bakes are time-sliced to avoid editor hitches.
     *
     * @return Number of probes that were flagged.
     */
    static auto rebuild_reflection_probes(rtti::context& ctx, bool force_full_first_frame = true) -> size_t;

    // Play mode (shared by header UI + MCP)
    static auto can_enter_play(rtti::context& ctx, std::string* error = nullptr) -> bool;
    static auto get_play_state(rtti::context& ctx) -> play_state_info;
    static auto set_play_active(rtti::context& ctx,
                                bool active,
                                bool allow_splash = true,
                                std::string* error = nullptr) -> bool;
    static auto toggle_play(rtti::context& ctx, bool allow_splash = true, std::string* error = nullptr) -> bool;
    static auto set_play_paused(rtti::context& ctx, bool paused, std::string* error = nullptr) -> bool;
    static auto skip_play_frame(rtti::context& ctx, std::string* error = nullptr) -> bool;

    // Selection (entity handles only for Phase A)
    static auto get_selection(rtti::context& ctx) -> selection_info;
    static auto set_selection(rtti::context& ctx,
                              const std::vector<std::string>& entity_ids,
                              bool add = false,
                              std::string* error = nullptr) -> bool;
    static void clear_selection(rtti::context& ctx);
    /// Selects the asset at @p key as a content browser click does, so the inspector shows it. Textures,
    /// materials, meshes, prefabs and scenes.
    static auto select_asset(rtti::context& ctx, const std::string& key, std::string* error = nullptr) -> bool;
    /// Shows the file or folder at @p key in the Content Browser, as an asset field's locate button does: the
    /// browser opens the folder that holds it, scrolls to it and rings it. The Content panel comes to the front.
    static auto reveal_in_content_browser(rtti::context& ctx, const std::string& key, std::string* error = nullptr)
        -> bool;
    /// Opens the Project Settings window at the category named @p category (its sidebar label); another name keeps the
    /// current page.
    static auto open_project_settings(rtti::context& ctx,
                                      const std::string& category,
                                      std::string* error = nullptr) -> bool;

    // Console log history
    static auto get_recent_logs(rtti::context& ctx,
                                level::level_enum min_level,
                                size_t max_count,
                                uint64_t after_id = 0) -> std::vector<log_query_entry>;

    // Entity inspect
    static auto inspect_entity(rtti::context& ctx,
                               const std::string& entity_id,
                               bool include_components,
                               std::string* error = nullptr) -> std::string;

    // Panel focus (ImGui dock tabs)
    static auto focus_scene_panel(rtti::context& ctx, std::string* error = nullptr) -> bool;
    static auto focus_game_panel(rtti::context& ctx, std::string* error = nullptr) -> bool;
    /// Opens the MCP Server window, as the header's MCP indicator does.
    static auto show_mcp_window(rtti::context& ctx, std::string* error = nullptr) -> bool;

    /**
     * @brief Focus/raise the OS main window so asset watcher and similar focus-gated
     * work can run (e.g. after MCP creates materials while Cursor has focus).
     */
    static auto request_main_window_focus(rtti::context& ctx, std::string* error = nullptr) -> bool;

    /**
     * @brief Rebuilds the armatures that mesh changes left pending, as undo steps of the change.
     *
     * A model's new mesh binds its own node entities: the nodes it needs that no existing child
     * provides are created, and the old mesh's nodes it does not bind again are deleted. Both
     * steps record their entities, so undoing them puts back the old nodes with their ids.
     * @return The executed steps, empty when no rebuild was pending.
     */
    static auto rebuild_pending_armatures(rtti::context& ctx) -> std::vector<std::shared_ptr<editing_action_t>>;

    /**
     * @brief Where import_files copies each of @p paths: target_path / the source's file name,
     * a directory with its whole tree. Nothing is copied.
     */
    static auto resolve_import_items(const std::vector<std::string>& paths, const fs::path& target_path)
        -> std::vector<import_files_item>;

    /**
     * @brief Copy external files/folders into target_path (content-browser Import parity).
     *
     * Resolves item destinations immediately. Copies run as one batch with the filesystem
     * watcher paused so multi-file assets (glTF + .bin + textures) are never observed
     * incomplete. When async is true (default), schedules a single thread-pool job and
     * returns immediately - call wait_import_jobs before treating destinations as present.
     * When async is false, copies on the calling thread and returns a ready future.
     * Asset compilation remains asynchronous via the asset watcher after resume.
     */
    static auto import_files(rtti::context& ctx,
                             const std::vector<std::string>& paths,
                             const fs::path& target_path,
                             bool async = true) -> import_files_result;

    /**
     * @brief Block until the batch import copy job completes or timeout elapses.
     * @return true when the job finished successfully within timeout.
     */
    static auto wait_import_jobs(import_files_result& result,
                                 std::chrono::milliseconds timeout) -> bool;

};

} // namespace unravel
