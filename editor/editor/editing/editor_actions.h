#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/assets/asset_handle.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/prefab.h>
#include <engine/threading/threader.h>
#include <editor/deploy/deploy.h>
#include <logging/logging.h>

#include <filesystem/filesystem.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace unravel
{

struct play_state_info
{
    std::string phase{"inactive"};
    bool is_active{false};
    bool is_paused{false};
    bool is_splash{false};
    bool is_simulation_running{false};
    uint64_t frames_running{0};
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

/**
 * @brief Outcome of checking the prefab nesting graph.
 */
struct prefab_graph_report
{
    /// Prefabs and scenes examined.
    size_t asset_count{};
    /// Assets that reference at least one other prefab.
    size_t nesting_count{};
    /// Deepest chain in the dependency order, in assets.
    size_t max_depth{};
    /// Human-readable ids of assets that take part in, or depend on, a nesting cycle.
    std::vector<std::string> cyclic_ids;

    auto is_valid() const -> bool
    {
        return cyclic_ids.empty();
    }
};

struct editor_actions
{
    /**
     * @brief Checks that every prefab and scene can be ordered for a build.
     *
     * A prefab that instances another has to be baked after it, so the set has to be
     * topologically orderable. A cycle - A instancing B instancing A - has no valid order
     * and would recurse forever if expanded, so it is reported here rather than left to
     * fail at load.
     *
     * Read-only: nothing is written, and no asset is modified.
     */
    static auto validate_prefab_graph(rtti::context& ctx) -> prefab_graph_report;

    /**
     * @brief Resolves nested prefab instances in every prefab and scene, and re-saves them.
     *
     * A prefab that instances another stores a snapshot of it, taken when that prefab was
     * last saved - so the snapshot goes stale the moment the inner asset is edited. The
     * editor hides that by refreshing nested instances on every load, which costs an extra
     * asset load per instance, at runtime as well.
     *
     * This bakes the refresh in: each asset is loaded, its instances resolved against their
     * own assets, and written back with a marker that tells the loader the work is done.
     * Assets are processed dependency-first, so an inner prefab is current before anything
     * nesting it is baked.
     *
     * **Writes to the source assets.** A cycle aborts the whole run before anything is
     * written, because a partially baked set is worse than an unbaked one.
     *
     * Refuses to run during play mode.
     *
     * @return the report from the validation pass, plus how many assets were rewritten.
     */
    static auto bake_prefab_nesting(rtti::context& ctx, size_t* baked_count = nullptr)
        -> prefab_graph_report;

    /**
     * @brief Clears the "nesting already resolved" claim from every compiled asset.
     *
     * The inverse of bake_prefab_nesting, and the safe half of the pair: a runtime that
     * finds no claim refreshes nested instances as it loads them, which is slower but can
     * never serve content the bake did not actually resolve.
     *
     * Deliberately **not** a load-and-re-save. It rewrites the one boolean in place, so it
     * cannot lose data the way a full round-trip through the entity serializer can - which
     * matters most precisely when something about the bake is already suspect.
     *
     * @return how many compiled assets were changed.
     */
    static auto clear_prefab_nesting_marker(rtti::context& ctx) -> size_t;

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
    static auto restart_editor(rtti::context& ctx) -> bool;

    static void run_project(const fs::path& executable_path);
    static auto deploy_project(rtti::context& ctx, const deploy_settings& params)
        -> std::map<std::string, tpp::shared_future<void>>;
    static auto can_deploy_project(rtti::context& ctx, const deploy_settings& params) -> bool;


    static void recompile_shaders(const std::string& group = "");
    static void recompile_textures(const std::string& group = "");
    /// One-time migration: tags textures referenced by material slots with their
    /// authored color space (base color/emissive = sRGB, data maps = linear) and
    /// recompiles the changed ones. Metas with an explicit (non-automatic) color
    /// space are left untouched. Returns the number of textures tagged.
    static auto migrate_texture_color_spaces(const std::string& group = "") -> size_t;
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

    /**
     * @brief Focus/raise the OS main window so asset watcher and similar focus-gated
     * work can run (e.g. after MCP creates materials while Cursor has focus).
     */
    static auto request_main_window_focus(rtti::context& ctx, std::string* error = nullptr) -> bool;

    /**
     * @brief Copy external files/folders into target_path (content-browser Import parity).
     *
     * Resolves item destinations immediately. Copies run as one batch with the filesystem
     * watcher paused so multi-file assets (glTF + .bin + textures) are never observed
     * incomplete. When async is true (default), schedules a single thread-pool job and
     * returns immediately — call wait_import_jobs before treating destinations as present.
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
