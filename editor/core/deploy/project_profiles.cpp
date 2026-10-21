#include "project_profiles.h"
#include "folders.h"
#include "profile_cache.h"
#include "releases.h"

#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>

namespace unravel::deployment
{
namespace
{
/// Where a migration keeps the deploy settings it read the old target from.
constexpr const char* LEGACY_DEPLOY_BACKUP = "deploy.cfg.before-profiles";
constexpr const char* DEPLOY_SETTINGS_FILE = "deploy/deploy.cfg";

void sort_by_name(std::vector<settings_profile>& profiles)
{
    std::sort(profiles.begin(),
              profiles.end(),
              [](const settings_profile& lhs, const settings_profile& rhs)
              {
                  return string_utils::to_lower(lhs.name) < string_utils::to_lower(rhs.name);
              });
}

/**
 * @brief Why an edit of a profile is refused: the paths of `changed` that `is_overridable` refuses, dotted as
 * the editor's tools name them ("input.actions"), and where they are changed instead.
 * @return Empty when every changed path may be overridden.
 */
auto describe_refused(const serialization::path_set& changed, bool (*is_overridable)(hpp::string_view), const char* where)
    -> std::string
{
    std::string refused;
    std::size_t count = 0;
    for(const auto& path : changed)
    {
        if(is_overridable(path))
        {
            continue;
        }
        std::string dotted = path;
        std::replace(dotted.begin(), dotted.end(), '/', '.');
        refused += (refused.empty() ? "" : ", ") + dotted;
        ++count;
    }
    if(refused.empty())
    {
        return refused;
    }
    return fmt::format("A profile cannot override {}, which every profile shares. Change {} in {}.",
                       refused,
                       count == 1 ? "it" : "them",
                       where);
}

/**
 * @brief Copies deploy.cfg beside itself before its fields from before profiles are dropped: to
 * deploy.cfg.before-profiles, or a numbered name when an earlier backup with other contents holds that one.
 * @param backup Set to the backup's path.
 */
auto back_up_legacy_deploy_settings(const fs::path& deploy_cfg, fs::path& backup) -> result
{
    fs::error_code ec;
    std::string contents;
    read_whole_file(deploy_cfg, contents);
    backup = deploy_cfg.parent_path() / LEGACY_DEPLOY_BACKUP;
    for(int number = 2; fs::exists(backup, ec); ++number)
    {
        std::string kept;
        if(read_whole_file(backup, kept) && kept == contents)
        {
            return {};
        }
        backup = deploy_cfg.parent_path() / fmt::format("{}-{}", LEGACY_DEPLOY_BACKUP, number);
    }
    fs::copy_file(deploy_cfg, backup, ec);
    return ec ? result::failure(fmt::format("Keeping {} as {} failed, and the deploy settings from before profiles it "
                                            "holds may be lost when it is saved: {}",
                                            deploy_cfg.generic_string(),
                                            backup.filename().generic_string(),
                                            ec.message()))
              : result{};
}

/// Whether the project has a folder in profiles/, readable or not.
auto has_profile_folders(const fs::path& project_root) -> bool
{
    fs::error_code ec;
    for(fs::directory_iterator it(get_profiles_folder(project_root), ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_directory(ec))
        {
            return true;
        }
    }
    return false;
}

auto join_profile_names(const std::vector<settings_profile>& profiles) -> std::string
{
    std::string names;
    for(const auto& profile : profiles)
    {
        names += (names.empty() ? "" : ", ") + profile.name;
    }
    return names;
}

/// Moves a profile's folder of kept releases to its new name; nothing to move is no failure.
auto rename_releases_folder(const fs::path& project_root, const std::string& name, const std::string& new_name) -> result
{
    const auto from = get_profile_releases_folder(project_root, name);
    const auto to = get_profile_releases_folder(project_root, new_name);
    fs::error_code ec;
    if(!fs::exists(from, ec))
    {
        return {};
    }
    rename_folder(from, to, ec);
    return ec ? result::failure(fmt::format("Moving the releases of profile {} failed: {}", name, ec.message())) : result{};
}
} // namespace

auto project_profiles::load(const fs::path& project_root,
                            const settings& base_settings,
                            deploy_settings& base_deploy,
                            std::string& deploy_profile) -> std::vector<std::string>
{
    clear();
    project_root_ = project_root;
    std::vector<std::string> warnings;
    // deploy.cfg is saved without its fields from before profiles from now on: it is kept as it was first.
    const auto deploy_cfg = project_root / DEPLOY_SETTINGS_FILE;
    const auto legacy = read_legacy_deploy_target(deploy_cfg);
    fs::path backup;
    if(legacy)
    {
        if(auto kept = back_up_legacy_deploy_settings(deploy_cfg, backup); !kept.is_ok())
        {
            has_unkept_legacy_settings_ = true;
            warnings.push_back(kept.error);
        }
    }
    read_profile_folders(base_settings, base_deploy, warnings);
    const bool is_without_profiles = profiles_.empty() && !has_profile_folders(project_root);
    if(is_without_profiles)
    {
        make_first_profiles(legacy, base_settings, deploy_profile, warnings);
        if(legacy)
        {
            APPLOG_INFO("Made the profiles {} from the project's deploy settings, which are kept in {}.",
                        join_profile_names(profiles_),
                        backup.filename().generic_string());
        }
    }
    else if(legacy)
    {
        warnings.push_back(fmt::format("{} holds a deploy target and Android settings from before profiles, which "
                                       "the profiles hold now. They are not used, and the file is kept as {}.",
                                       DEPLOY_SETTINGS_FILE,
                                       backup.filename().generic_string()));
    }
    if(profiles_.empty())
    {
        make_replacement_profile(base_settings, warnings);
    }
    adopt_legacy_releases(base_deploy, warnings);
    sort_by_name(profiles_);
    remove_unknown_profile_caches(project_root, get_names());
    return warnings;
}

auto project_profiles::has_unkept_legacy_settings() const -> bool
{
    return has_unkept_legacy_settings_;
}

void project_profiles::read_profile_folders(const settings& base_settings,
                                            const deploy_settings& base_deploy,
                                            std::vector<std::string>& warnings)
{
    fs::error_code ec;
    if(!fs::exists(get_profiles_folder(project_root_), ec))
    {
        return;
    }
    auto list = load_profiles(project_root_, base_settings, base_deploy);
    profiles_ = std::move(list.profiles);
    warnings.insert(warnings.end(), list.warnings.begin(), list.warnings.end());
}

void project_profiles::make_first_profiles(const hpp::optional<legacy_deploy_target>& legacy,
                                           const settings& base_settings,
                                           std::string& deploy_profile,
                                           std::vector<std::string>& warnings)
{
    auto first = legacy ? make_profiles_from_legacy(*legacy, base_settings, deploy_profile)
                        : std::vector<settings_profile>{make_profile(DEFAULT_PROFILE_NAME, target_platform::desktop, base_settings)};
    for(auto& profile : first)
    {
        add_profile(std::move(profile), warnings);
    }
}

void project_profiles::make_replacement_profile(const settings& base_settings, std::vector<std::string>& warnings)
{
    // Every profile folder failed to read: a new one, under a name that leaves those folders alone.
    fs::error_code ec;
    std::string name = DEFAULT_PROFILE_NAME;
    for(int number = 2; fs::exists(get_profile_folder(project_root_, name), ec); ++number)
    {
        name = fmt::format("{} {}", DEFAULT_PROFILE_NAME, number);
    }
    warnings.push_back(fmt::format("No profile could be read, so the project got a new one named {}.", name));
    add_profile(make_profile(name, target_platform::desktop, base_settings), warnings);
}

void project_profiles::adopt_legacy_releases(deploy_settings& base_deploy, std::vector<std::string>& warnings)
{
    // Releases were always desktop builds: the first desktop profile takes them, with the release patched.
    const auto desktop = std::find_if(profiles_.begin(),
                                      profiles_.end(),
                                      [](const settings_profile& profile)
                                      {
                                          return profile.platform == target_platform::desktop;
                                      });
    if(desktop == profiles_.end())
    {
        return;
    }
    if(auto moved = move_releases_into_profile(project_root_, desktop->name); !moved.is_ok())
    {
        warnings.push_back(moved.error);
    }
    // A patch release of its own wins over the shared one from before profiles.
    constexpr const char* patch_release_path = "deploy_patch_release";
    if(base_deploy.deploy_patch_release.empty() || desktop->deploy.is_overridden(patch_release_path))
    {
        base_deploy.deploy_patch_release.clear();
        return;
    }
    auto adopted = *desktop;
    adopted.deploy.set_values(resolve_deploy_settings(base_deploy, adopted), {patch_release_path});
    base_deploy.deploy_patch_release.clear();
    if(auto stored = store(adopted); !stored.is_ok())
    {
        warnings.push_back(stored.error);
        return;
    }
    *desktop = std::move(adopted);
}

void project_profiles::add_profile(settings_profile profile, std::vector<std::string>& warnings)
{
    if(auto saved = save_profile(project_root_, profile); !saved.is_ok())
    {
        warnings.push_back(saved.error);
    }
    profiles_.push_back(std::move(profile));
}

void project_profiles::clear()
{
    has_unkept_legacy_settings_ = false;
    project_root_.clear();
    profiles_.clear();
    ++revision_;
}

auto project_profiles::get_all() const -> const std::vector<settings_profile>&
{
    return profiles_;
}

auto project_profiles::get_names() const -> std::vector<std::string>
{
    std::vector<std::string> names;
    names.reserve(profiles_.size());
    for(const auto& profile : profiles_)
    {
        names.push_back(profile.name);
    }
    return names;
}

auto project_profiles::find(const std::string& name) const -> const settings_profile*
{
    const auto found = std::find_if(profiles_.begin(),
                                    profiles_.end(),
                                    [&name](const settings_profile& profile)
                                    {
                                        return profile.name == name;
                                    });
    return found == profiles_.end() ? nullptr : &*found;
}

auto project_profiles::find_mutable(const std::string& name) -> settings_profile*
{
    return const_cast<settings_profile*>(find(name));
}

auto project_profiles::get_revision() const -> std::uint64_t
{
    return revision_;
}

auto project_profiles::check_new_name(const std::string& name, const std::string& renamed) const -> result
{
    if(!is_valid_profile_name(name))
    {
        return result::failure(fmt::format("'{}' is no valid profile name. A name cannot be empty, end with a dot or a "
                                           "space, or hold any of < > : \" / \\ | ? *.",
                                           name));
    }
    const bool is_taken = std::any_of(profiles_.begin(),
                                      profiles_.end(),
                                      [&](const settings_profile& profile)
                                      {
                                          return profile.name != renamed && is_same_folder_name(profile.name, name);
                                      });
    fs::error_code ec;
    const bool is_on_disk = (renamed.empty() || !is_same_folder_name(renamed, name)) && fs::exists(get_profile_folder(project_root_, name), ec);
    if(is_taken || is_on_disk)
    {
        return result::failure(fmt::format("A profile named {} exists already.", name));
    }
    return {};
}

auto project_profiles::store(const settings_profile& profile) -> result
{
    auto saved = save_profile(project_root_, profile);
    if(saved.is_ok())
    {
        ++revision_;
    }
    return saved;
}

auto project_profiles::create(const std::string& name, target_platform platform, const settings& base_settings, const std::string& copy_from)
    -> result
{
    if(auto checked = check_new_name(name); !checked.is_ok())
    {
        return checked;
    }
    settings_profile profile;
    if(!copy_from.empty())
    {
        const auto* source = find(copy_from);
        if(source == nullptr)
        {
            return result::failure(fmt::format("No profile named {} to copy.", copy_from));
        }
        profile = *source;
        profile.name = name;
    }
    else
    {
        profile = make_profile(name, platform, base_settings);
    }
    if(auto stored = store(profile); !stored.is_ok())
    {
        return stored;
    }
    profiles_.push_back(std::move(profile));
    sort_by_name(profiles_);
    APPLOG_INFO("Created profile {}", name);
    return {};
}

auto project_profiles::rename(const std::string& name, const std::string& new_name) -> result
{
    auto* profile = find_mutable(name);
    if(profile == nullptr)
    {
        return result::failure(fmt::format("No profile named {}.", name));
    }
    if(name == new_name)
    {
        return {};
    }
    if(auto checked = check_new_name(new_name, name); !checked.is_ok())
    {
        return checked;
    }
    if(auto renamed = rename_profile_files(project_root_, name, new_name); !renamed.is_ok())
    {
        return renamed;
    }
    // The releases belong to the profile: a profile that keeps its name keeps them too.
    if(auto releases = rename_releases_folder(project_root_, name, new_name); !releases.is_ok())
    {
        if(auto restored = rename_profile_files(project_root_, new_name, name); !restored.is_ok())
        {
            return result::failure(fmt::format("{} Renaming the profile back failed too: {}", releases.error, restored.error));
        }
        return releases;
    }
    profile->name = new_name;
    ++revision_;
    rename_profile_cache(project_root_, name, new_name);
    sort_by_name(profiles_);
    APPLOG_INFO("Renamed profile {} to {}", name, new_name);
    return {};
}

auto project_profiles::remove(const std::string& name) -> result
{
    const auto found = std::find_if(profiles_.begin(),
                                    profiles_.end(),
                                    [&name](const settings_profile& profile)
                                    {
                                        return profile.name == name;
                                    });
    if(found == profiles_.end())
    {
        return result::failure(fmt::format("No profile named {}.", name));
    }
    if(profiles_.size() <= 1)
    {
        return result::failure("A project keeps at least one profile. Create another one first.");
    }
    if(auto removed = remove_profile_files(project_root_, name); !removed.is_ok())
    {
        return removed;
    }
    profiles_.erase(found);
    ++revision_;
    fs::error_code ec;
    fs::remove_all(get_profile_releases_folder(project_root_, name), ec);
    remove_profile_cache(project_root_, name);
    APPLOG_INFO("Deleted profile {}", name);
    return {};
}

auto project_profiles::set_android(const std::string& name, const android_deploy_settings& android) -> result
{
    const auto* profile = find(name);
    if(profile == nullptr)
    {
        return result::failure(fmt::format("No profile named {}.", name));
    }
    auto changed = *profile;
    changed.android = android;
    if(auto stored = store(changed); !stored.is_ok())
    {
        return stored;
    }
    *find_mutable(name) = std::move(changed);
    return {};
}

auto project_profiles::check_editable(const settings_profile& profile, const profile_overrides& overrides, const char* file) const
    -> result
{
    if(!overrides.is_unreadable())
    {
        return {};
    }
    return result::failure(fmt::format("Profile {}'s {} could not be read. Mend or delete the file, then open the project "
                                       "again to change what it overrides.",
                                       profile.name,
                                       file));
}

auto project_profiles::override_settings(const std::string& name, const settings& base, const settings& before, const settings& edited)
    -> result
{
    const auto* profile = find(name);
    if(profile == nullptr)
    {
        return result::failure(fmt::format("No profile named {}.", name));
    }
    if(auto editable = check_editable(*profile, profile->settings, "settings.cfg"); !editable.is_ok())
    {
        return editable;
    }
    auto values = deployment::resolve_settings(base, *profile);
    const auto changed = serialization::apply_edit(before, edited, SETTINGS_DOCUMENT_ROOT, values);
    if(auto refused = describe_refused(changed, &is_overridable_settings_path, "the project settings"); !refused.empty())
    {
        return result::failure(std::move(refused));
    }
    if(changed.empty())
    {
        return {};
    }
    auto updated = *profile;
    updated.settings.set_values(values, changed);
    if(auto stored = store(updated); !stored.is_ok())
    {
        return stored;
    }
    *find_mutable(name) = std::move(updated);
    return {};
}

auto project_profiles::override_settings(const std::string& name, const settings& base, const settings& edited) -> result
{
    const auto before = resolve_settings(name, base);
    if(!before)
    {
        return result::failure(fmt::format("No profile named {}.", name));
    }
    return override_settings(name, base, *before, edited);
}

auto project_profiles::override_deploy(const std::string& name,
                                       const deploy_settings& base,
                                       const deploy_settings& before,
                                       const deploy_settings& edited) -> result
{
    const auto* profile = find(name);
    if(profile == nullptr)
    {
        return result::failure(fmt::format("No profile named {}.", name));
    }
    if(auto editable = check_editable(*profile, profile->deploy, "deploy.cfg"); !editable.is_ok())
    {
        return editable;
    }
    auto values = deployment::resolve_deploy_settings(base, *profile);
    const auto changed = serialization::apply_edit(before, edited, SETTINGS_DOCUMENT_ROOT, values);
    if(auto refused = describe_refused(changed, &is_overridable_deploy_path, "the shared deploy settings"); !refused.empty())
    {
        return result::failure(std::move(refused));
    }
    if(changed.empty())
    {
        return {};
    }
    auto updated = *profile;
    updated.deploy.set_values(values, changed);
    if(auto stored = store(updated); !stored.is_ok())
    {
        return stored;
    }
    *find_mutable(name) = std::move(updated);
    return {};
}

auto project_profiles::override_deploy(const std::string& name, const deploy_settings& base, const deploy_settings& edited) -> result
{
    const auto before = resolve_deploy(name, base);
    if(!before)
    {
        return result::failure(fmt::format("No profile named {}.", name));
    }
    return override_deploy(name, base, *before, edited);
}

auto project_profiles::revert_settings(const std::string& name, const settings& base, hpp::string_view path) -> result
{
    const auto* profile = find(name);
    if(profile == nullptr)
    {
        return result::failure(fmt::format("No profile named {}.", name));
    }
    if(auto editable = check_editable(*profile, profile->settings, "settings.cfg"); !editable.is_ok())
    {
        return editable;
    }
    if(!profile->settings.is_related(path))
    {
        return {};
    }
    auto updated = *profile;
    updated.settings.revert(deployment::resolve_settings(base, *profile), path);
    if(auto stored = store(updated); !stored.is_ok())
    {
        return stored;
    }
    *find_mutable(name) = std::move(updated);
    return {};
}

auto project_profiles::revert_deploy(const std::string& name, const deploy_settings& base, hpp::string_view path) -> result
{
    const auto* profile = find(name);
    if(profile == nullptr)
    {
        return result::failure(fmt::format("No profile named {}.", name));
    }
    if(auto editable = check_editable(*profile, profile->deploy, "deploy.cfg"); !editable.is_ok())
    {
        return editable;
    }
    if(!profile->deploy.is_related(path))
    {
        return {};
    }
    auto updated = *profile;
    updated.deploy.revert(deployment::resolve_deploy_settings(base, *profile), path);
    if(auto stored = store(updated); !stored.is_ok())
    {
        return stored;
    }
    *find_mutable(name) = std::move(updated);
    return {};
}

auto project_profiles::resolve_settings(const std::string& name, const settings& base) const -> hpp::optional<settings>
{
    const auto* profile = find(name);
    if(profile == nullptr)
    {
        return hpp::nullopt;
    }
    return deployment::resolve_settings(base, *profile);
}

auto project_profiles::resolve_deploy(const std::string& name, const deploy_settings& base) const -> hpp::optional<deploy_settings>
{
    const auto* profile = find(name);
    if(profile == nullptr)
    {
        return hpp::nullopt;
    }
    return deployment::resolve_deploy_settings(base, *profile);
}

} // namespace unravel::deployment
