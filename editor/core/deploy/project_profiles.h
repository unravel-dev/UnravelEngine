#pragma once

#include "profile.h"
#include "result.h"

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace unravel::deployment
{

/**
 * @brief A project's settings profiles, kept in step with their files: every change is written at once.
 *
 * Changing a profile's name moves everything named after it: its folder, its kept releases
 * (releases/<profile>) and its texture cache (cache/profiles/<profile>). Its build folder under the deploy
 * location keeps its name, since it holds the user's builds.
 */
class project_profiles
{
public:
    /**
     * @brief Reads the profiles of the project at `project_root`.
     *
     * A project from before profiles gets its first ones from deploy/deploy.cfg (make_profiles_from_legacy),
     * and deploy.cfg is kept as deploy.cfg.before-profiles. A project with no profile gets Desktop. Releases
     * kept before profiles, and the release their deploys patched, become the first desktop profile's
     * whenever they are found (a teammate's project from before profiles merged in, say). The texture caches
     * of profiles that no longer exist are deleted.
     * @param base_deploy The shared deploy settings as loaded; their patch release from before profiles
     * moves into a profile. The caller saves them, unless has_unkept_legacy_settings.
     * @param deploy_profile Set to the profile of the old target by a migration; otherwise left as it is.
     * @return What was wrong with the profiles' files, one sentence each.
     */
    auto load(const fs::path& project_root,
              const settings& base_settings,
              deploy_settings& base_deploy,
              std::string& deploy_profile) -> std::vector<std::string>;

    void clear();

    /**
     * @brief Whether deploy/deploy.cfg holds settings from before profiles that could not be backed up: saving
     * the deploy settings would drop them, so they must not be saved until the file is copied aside.
     */
    auto has_unkept_legacy_settings() const -> bool;

    auto get_all() const -> const std::vector<settings_profile>&;
    auto get_names() const -> std::vector<std::string>;
    /// The profile named `name`, or null.
    auto find(const std::string& name) const -> const settings_profile*;
    /// Counts the changes, so a view can notice one.
    auto get_revision() const -> std::uint64_t;
    /// Whether create, or rename of `renamed`, takes `name`: the error says why not.
    auto check_new_name(const std::string& name, const std::string& renamed = {}) const -> result;

    /**
     * @brief Creates a profile. With `copy_from` it starts as a copy of that profile; otherwise with the
     * platform's starting values (make_profile).
     */
    auto create(const std::string& name, target_platform platform, const settings& base_settings, const std::string& copy_from = {})
        -> result;
    auto rename(const std::string& name, const std::string& new_name) -> result;
    /// Deletes a profile, its kept releases and its texture cache. The last profile cannot be deleted.
    auto remove(const std::string& name) -> result;

    /// Replaces an Android profile's identity, signing and texture formats.
    auto set_android(const std::string& name, const android_deploy_settings& android) -> result;

    /**
     * @brief Overrides, in a profile, the project settings an edit changed; an edit of the settings every
     * profile shares is refused with an error, and nothing changes.
     * @param before The profile's resolved settings (resolve_settings) the edit started from. What the edit
     * changed against them is stored over the profile's current overrides, so an override changed since
     * (by a tool, during a drag) keeps its new value.
     * @param edited The same settings after the edit.
     */
    auto override_settings(const std::string& name, const settings& base, const settings& before, const settings& edited)
        -> result;
    /// override_settings for an edit of the profile's current resolved settings.
    auto override_settings(const std::string& name, const settings& base, const settings& edited) -> result;
    /// override_settings for the deploy settings; the deploy location is shared and refused with an error.
    auto override_deploy(const std::string& name,
                         const deploy_settings& base,
                         const deploy_settings& before,
                         const deploy_settings& edited) -> result;
    auto override_deploy(const std::string& name, const deploy_settings& base, const deploy_settings& edited) -> result;

    /// Stops overriding a project settings property (a path such as "graphics/particles/spawn_scale"); a path
    /// the profile does not override changes nothing.
    auto revert_settings(const std::string& name, const settings& base, hpp::string_view path) -> result;
    auto revert_deploy(const std::string& name, const deploy_settings& base, hpp::string_view path) -> result;

    auto resolve_settings(const std::string& name, const settings& base) const -> hpp::optional<settings>;
    auto resolve_deploy(const std::string& name, const deploy_settings& base) const -> hpp::optional<deploy_settings>;

private:
    /// Reads every profile folder; a folder that cannot be read is left out with a warning.
    void read_profile_folders(const settings& base_settings, const deploy_settings& base_deploy, std::vector<std::string>& warnings);
    /// The first profiles of a project without profile folders: from its settings from before profiles, or Desktop.
    void make_first_profiles(const hpp::optional<legacy_deploy_target>& legacy,
                             const settings& base_settings,
                             std::string& deploy_profile,
                             std::vector<std::string>& warnings);
    /// A new Desktop profile, under a name no folder uses, when every profile folder failed to read.
    void make_replacement_profile(const settings& base_settings, std::vector<std::string>& warnings);
    /// Moves releases kept before profiles, and the shared patch release, into the first desktop profile.
    void adopt_legacy_releases(deploy_settings& base_deploy, std::vector<std::string>& warnings);
    /// Saves a new profile and keeps it; a profile that cannot be saved is kept with a warning.
    void add_profile(settings_profile profile, std::vector<std::string>& warnings);

    auto find_mutable(const std::string& name) -> settings_profile*;
    /// Writes a profile and counts the change.
    auto store(const settings_profile& profile) -> result;
    /// The error for an edit of overrides read from a file that could not be read; empty when they can change.
    auto check_editable(const settings_profile& profile, const profile_overrides& overrides, const char* file) const -> result;

    fs::path project_root_;
    std::vector<settings_profile> profiles_;
    std::uint64_t revision_{};
    bool has_unkept_legacy_settings_{false};
};

} // namespace unravel::deployment
