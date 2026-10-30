#pragma once
#include <context/context.hpp>
#include <engine/settings/settings.h>
#include <editor/settings/settings.h>
#include <deploy/deploy_settings.h>
#include <deploy/project_profiles.h>
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

    /**
     * @brief Creates a project in `project_path` (absolute; missing, or an empty directory) and
     *        opens it.
     * @param error When set, receives one sentence saying why the project was not created.
     * @return True when the project was created and opened.
     */
    auto create_project(rtti::context& ctx, const fs::path& project_path, std::string* error = nullptr) -> bool;

    /**
     * @brief Whether create_project would accept `project_path`, without creating anything.
     * @param error When set, receives one sentence saying why not.
     */
    auto can_create_project(const fs::path& project_path, std::string* error = nullptr) const -> bool;

    /// Copies editor agent instruction templates into the project root as
    /// `UNRAVEL-AGENTS.md` (overwrites those files). Returns
    /// true when `UNRAVEL-AGENTS.md` was written successfully.
    auto regenerate_agent_files() -> bool;

    void save_editor_settings();

    void load_editor_settings();

    /**
     * @brief Prepares restart CLI args: persists recent projects and sets -p recent.
     *
     * Also strips -r/--renderer so the next process applies project boot settings.
     */
    void prepare_restart(std::vector<std::string>& arguments);

    /**
     * @brief Resolves a -p/--project CLI value to an absolute project directory.
     *
     * Supports the special value "recent" (first recent project). Returns empty
     * when the value cannot be resolved.
     */
    [[nodiscard]] auto resolve_project_cli_path(const std::string& project_arg) const -> fs::path;

    /**
     * @brief Peeks project boot settings and publishes a resolved @ref boot_config.
     *
     * Call before @ref engine::init_core when a project CLI arg may be present.
     */
    void prepare_boot_config(rtti::context& ctx, const cmd_line::parser& parser);

    auto get_name() const -> const std::string&;

    void set_name(const std::string& name);

    /// The project settings (settings/settings.cfg): the base every profile overrides.
    auto get_settings() -> settings&;
    /// The deploy settings every profile shares (deploy/deploy.cfg), and this session's Android device.
    auto get_deploy_settings() -> deploy_settings&;
    auto get_editor_settings() -> editor_settings&;
    auto get_project_editor_settings() -> project_editor_settings&;
    auto get_project_info() -> project_info&;
    auto get_project_info() const -> const project_info&;


    auto has_open_project() const -> bool;

    /// The project's settings profiles. Change them through the functions below, which keep the editor's
    /// choices and the settings it runs with in step.
    auto get_profiles() const -> const deployment::project_profiles&;

    /// The profile the Deploy window builds; always one of the project's.
    auto get_deploy_profile() const -> const std::string&;
    auto set_deploy_profile(const std::string& name) -> deployment::result;

    /// The profile whose settings the editor runs with; empty for the project settings.
    auto get_preview_profile() const -> const std::string&;
    /// Runs the editor with a profile's settings, or the project settings for an empty name. Refused in play
    /// mode: the running game keeps the settings it started with.
    auto set_preview_profile(rtti::context& ctx, const std::string& name) -> deployment::result;

    /// The project settings a profile runs with; the project settings for an unknown profile.
    auto get_profile_settings(const std::string& profile) const -> settings;
    /// The deploy settings a profile deploys with, with this session's Android device; empty for an unknown
    /// profile.
    auto get_profile_deploy_settings(const std::string& profile) const -> hpp::optional<deploy_settings>;
    /// The deploy settings of the deploy profile, which the Deploy window and project_deploy deploy with.
    auto get_deploy_request() const -> deploy_settings;

    auto create_profile(rtti::context& ctx, const std::string& name, target_platform platform, const std::string& copy_from = {})
        -> deployment::result;
    /// Renames a profile; refused while a deploy runs or the game plays (check_profiles_changeable).
    auto rename_profile(rtti::context& ctx, const std::string& name, const std::string& new_name) -> deployment::result;
    /// Deletes a profile; refused while a deploy runs or the game plays (check_profiles_changeable).
    auto remove_profile(rtti::context& ctx, const std::string& name) -> deployment::result;
    /// Why profiles cannot be renamed or deleted now: a deploy reads them and their caches as it runs, and a
    /// game in play mode keeps the settings it started with. Ok when they can.
    auto check_profiles_changeable(rtti::context& ctx) const -> deployment::result;
    auto set_profile_android(rtti::context& ctx, const std::string& name, const android_deploy_settings& android) -> deployment::result;
    auto set_profile_desktop(rtti::context& ctx, const std::string& name, const desktop_deploy_settings& desktop) -> deployment::result;
    /// Overrides in a profile what `edited` (the profile's settings after an edit) changes.
    auto override_profile_settings(rtti::context& ctx, const std::string& name, const settings& edited) -> deployment::result;
    /// Overrides in a profile what an edit changed against `before`, the settings it started from: an override
    /// changed meanwhile keeps its new value (deployment::project_profiles::override_settings).
    auto override_profile_settings(rtti::context& ctx, const std::string& name, const settings& before, const settings& edited)
        -> deployment::result;
    auto override_profile_deploy(rtti::context& ctx, const std::string& name, const deploy_settings& edited) -> deployment::result;
    auto override_profile_deploy(rtti::context& ctx,
                                 const std::string& name,
                                 const deploy_settings& before,
                                 const deploy_settings& edited) -> deployment::result;
    auto revert_profile_settings(rtti::context& ctx, const std::string& name, hpp::string_view path) -> deployment::result;
    auto revert_profile_deploy(rtti::context& ctx, const std::string& name, hpp::string_view path) -> deployment::result;

    /// True once opening a project asked the editor to restart, because the project uses another
    /// renderer or physics backend than the running one; the project opens after the restart.
    auto is_restart_pending() const -> bool
    {
        return restart_opens_recent_;
    }

    void load_project_settings();
    /// Saves the project settings and gives the editor what follows from them: the settings it runs with and
    /// the texture defaults it compiles with.
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
    /// Puts the settings the editor runs with into the context: the preview profile's, or the project's.
    void publish_settings(rtti::context& ctx);
    /// Points the asset compiler at the project's texture defaults; recompiles the project's textures when
    /// they changed while the project is open.
    void apply_texture_defaults(const settings::asset_settings::texture_importer_settings& defaults, bool is_open);
    /// Reads the profiles and makes the editor's choices name existing ones.
    void load_profiles();
    /// Keeps the deploy and preview profiles naming existing profiles after a change of the profiles.
    void fix_profile_choices(rtti::context& ctx);

    void fixup_editor_settings_on_save();
    void fixup_editor_settings_on_load();
    void push_recent_project(const fs::path& project_path);
    [[nodiscard]] auto try_restart_for_boot_mismatch(rtti::context& ctx, const fs::path& project_path) -> bool;

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
    deployment::project_profiles profiles_;
    /// When true, @ref prepare_restart adds -p recent even if no project is open yet.
    bool restart_opens_recent_{false};

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
