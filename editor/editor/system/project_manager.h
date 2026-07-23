#pragma once
#include <context/context.hpp>
#include <engine/settings/settings.h>
#include <editor/settings/settings.h>
#include <editor/deploy/deploy.h>
#include <editor/project/project_editor_settings.h>
#include <editor/project/project_info.h>
#include <filesystem/syncer.h>
#include <cmd_line/parser.h>

#include <cstdint>
#include <string>
#include <vector>

namespace unravel
{
class project_manager
{
public:
    project_manager(rtti::context& ctx, cmd_line::parser& parser);
    ~project_manager();

    auto init(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
    auto deinit(rtti::context& ctx) -> bool;


    /// Result of `inspect_project` describing a project's on-disk metadata.
    /// Used so UI callers can show a confirmation prompt before committing to
    /// `open_project`, without having to implement any of the file I/O
    /// themselves.
    enum class project_compat : std::uint8_t
    {
        /// No `project.cfg` found - this is a legacy project. `open_project`
        /// will create one on-the-fly, preserving all existing content.
        no_info_file,
        /// Project was last opened by the same or a newer engine build than
        /// the running one. Safe to open (or at worst: the rare case of a
        /// downgrade, which we don't guard against).
        ok,
        /// Project was last opened by a strictly older engine build than the
        /// running one, compared across all version components. On-disk
        /// formats may have evolved since, so the UI layer should prompt the
        /// user before committing to an open that re-saves everything.
        engine_older,
    };

    struct project_compat_report
    {
        project_compat status = project_compat::ok;
        /// The `project_info` loaded from disk, if any. When `status ==
        /// no_info_file`, this is default-constructed.
        project_info on_disk;
    };

    /// Inspects a project directory *without* opening it. Cheap enough to be
    /// called from the hub list / recent-projects view for every entry.
    auto inspect_project(const fs::path& project_path) const -> project_compat_report;

    auto open_project(rtti::context& ctx, const fs::path& project_path) -> bool;

    void close_project(rtti::context& ctx);

    void create_project(rtti::context& ctx, const fs::path& project_path);

    /// Copies editor agent instruction templates into the project root as
    /// `UNRAVEL-AGENTS.md` (overwrites those files). Returns
    /// true when `UNRAVEL-AGENTS.md` was written successfully.
    auto regenerate_agent_files() -> bool;

    void save_editor_settings();

    void load_editor_settings();

    /**
     * @brief Prepares restart CLI args: persists recent projects and sets -p recent.
     */
    void prepare_restart(std::vector<std::string>& arguments);

    auto get_name() const -> const std::string&;

    void set_name(const std::string& name);

    auto get_settings() -> settings&;
    auto get_deploy_settings() -> deploy_settings&;
    auto get_editor_settings() -> editor_settings&;
    auto get_project_editor_settings() -> project_editor_settings&;
    auto get_project_info() -> project_info&;
    auto get_project_info() const -> const project_info&;


    auto has_open_project() const -> bool;

    void load_project_settings();
    void save_project_settings(rtti::context& ctx);
    void load_deploy_settings();
    void save_deploy_settings();
    void load_project_editor_settings();
    void save_project_editor_settings();
    /// Loads `app:/project.cfg` into `project_info_`. If the file is missing,
    /// returns false and leaves `project_info_` default-constructed.
    auto load_project_info() -> bool;
    void save_project_info();

private:
    void fixup_editor_settings_on_save();
    void fixup_editor_settings_on_load();

    void setup_directory(rtti::context& ctx, fs::syncer& syncer);
    void setup_meta_syncer(rtti::context& ctx, fs::syncer& syncer, const fs::path& data_dir, const fs::path& meta_dir);
    void setup_cache_syncer(rtti::context& ctx,
                            std::vector<uint64_t>& watchers,
                            fs::syncer& syncer,
                            const fs::path& meta_dir,
                            const fs::path& cache_dir);
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);


    /// Current project name
    std::string project_name_;
    settings project_settings_;
    deploy_settings deploy_settings_;
    editor_settings editor_settings_;
    project_editor_settings project_editor_settings_;
    project_info project_info_;

    fs::syncer app_meta_syncer_;
    fs::syncer app_cache_syncer_;
    std::vector<std::uint64_t> app_watchers_;

    fs::syncer editor_meta_syncer_;
    fs::syncer editor_cache_syncer_;
    std::vector<std::uint64_t> editor_watchers_;

    fs::syncer engine_meta_syncer_;
    fs::syncer engine_cache_syncer_;
    std::vector<std::uint64_t> engine_watchers_;
};
} // namespace unravel
