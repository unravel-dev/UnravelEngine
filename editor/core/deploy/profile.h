#pragma once

#include "deploy_settings.h"
#include "result.h"

// The overrides' templates save and load these types wherever they are used.
#include <deploy/meta/deploy_settings.hpp>
#include <engine/meta/settings/settings.hpp>
#include <engine/settings/settings.h>
#include <serialization/sparse_document.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <functional>
#include <string>
#include <vector>

namespace unravel
{

/// The root node of the settings documents a profile overrides (settings.cfg, deploy.cfg).
constexpr const char* SETTINGS_DOCUMENT_ROOT = "settings";

/// Whether a settings property may differ per profile, by its path in the settings document.
using override_path_filter = bool (*)(hpp::string_view path);

/**
 * @brief What a profile overrides in one settings document: the overridden properties and their values,
 * kept as a sparse document that states only those and is loaded over a copy of the base.
 *
 * The overridden paths are the document's leaf paths (serialization::get_leaf_paths): objects are walked
 * into, while a value, an array or an asset handle is overridden as a whole. Properties the base does
 * not override stay the base's, so a later edit of the base reaches every profile that leaves it alone.
 */
class profile_overrides
{
public:
    /// The sparse document; empty when nothing is overridden.
    auto get_document() const -> const std::string&;
    /**
     * @brief The text to store: the document as it was read until the overrides are edited, so saving a
     * profile for another change keeps the file byte for byte, entries this version does not know included.
     */
    auto get_document_to_store() const -> const std::string&;
    auto get_paths() const -> const serialization::path_set&;
    auto is_empty() const -> bool;
    /// Whether `path` is overridden, or lies inside an overridden value.
    auto is_overridden(hpp::string_view path) const -> bool;
    /// Whether `path` is overridden or holds an override: a group with overridden properties in it.
    auto has_overrides_within(hpp::string_view path) const -> bool;
    /// Whether `path` is overridden, lies in an overridden value, or holds an override: what an editor marks.
    auto is_related(hpp::string_view path) const -> bool;
    void clear();

    /**
     * @brief Whether the file these overrides were read from could not be read: they override nothing then,
     * and their file is never written, so mending it by hand loses nothing.
     */
    auto is_unreadable() const -> bool;
    /// Marks the overrides as read from a file that could not be read.
    void mark_unreadable();

    /// Loads the overridden properties over `resolved`, which holds the base, and nothing else.
    template<typename T>
    auto apply(T& resolved) const -> bool;

    /**
     * @brief Overrides the properties `changed` with their values in `resolved`.
     * @param resolved The base with these overrides applied, then edited: the earlier overrides keep the
     * values it holds for them.
     */
    template<typename T>
    void set_values(const T& resolved, const serialization::path_set& changed);

    /**
     * @brief Stops overriding `path`, and every override inside it or holding it (a value is overridden
     * whole, so reverting a part of it reverts all of it).
     * @param resolved The base with these overrides applied.
     */
    template<typename T>
    void revert(const T& resolved, hpp::string_view path);

    /**
     * @brief Takes the overrides a sparse document states, keeping the ones `is_overridable` admits and
     * the base has. A document that does not parse overrides nothing and marks the overrides unreadable.
     * @return The paths that were left out, for a warning.
     */
    template<typename T>
    auto read(const std::string& document, const T& base, override_path_filter is_overridable) -> std::vector<std::string>;

private:
    /// The sparse document of the paths, from `resolved`, which holds their values.
    template<typename T>
    void write(const T& resolved);

    std::string document_;
    serialization::path_set paths_;
    /// The text read, stored again while the overrides are not edited.
    std::string source_document_;
    bool is_edited_{false};
    bool is_unreadable_{false};
};

/// A named target of the project: a platform, what it changes in the project settings and in the shared
/// deploy settings, and its platform's own settings. Stored in profiles/<name>/.
struct settings_profile
{
    /// Its folder under profiles/, and the folder it builds into under the deploy location.
    std::string name;
    target_platform platform{target_platform::desktop};
    /// The Android build's identity, signing and texture formats; used by Android profiles only.
    android_deploy_settings android;
    /// The desktop build's icon; used by desktop profiles only.
    desktop_deploy_settings desktop;
    /// Overrides of the project settings (settings/settings.cfg), in profiles/<name>/settings.cfg.
    profile_overrides settings;
    /// Overrides of the shared deploy settings (deploy/deploy.cfg), in profiles/<name>/deploy.cfg.
    profile_overrides deploy;
};

template<typename T>
auto profile_overrides::apply(T& resolved) const -> bool
{
    if(paths_.empty())
    {
        return true;
    }
    const serialization::scoped_path_filter filter(SETTINGS_DOCUMENT_ROOT, paths_);
    return serialization::load_from_document(document_, SETTINGS_DOCUMENT_ROOT, resolved);
}

template<typename T>
void profile_overrides::set_values(const T& resolved, const serialization::path_set& changed)
{
    for(const auto& path : changed)
    {
        serialization::add_path(paths_, path);
    }
    write(resolved);
    is_edited_ = true;
}

template<typename T>
void profile_overrides::revert(const T& resolved, hpp::string_view path)
{
    serialization::remove_path(paths_, path);
    write(resolved);
    is_edited_ = true;
}

template<typename T>
auto profile_overrides::read(const std::string& document, const T& base, override_path_filter is_overridable)
    -> std::vector<std::string>
{
    clear();
    is_unreadable_ = false;
    is_edited_ = false;
    source_document_ = document;
    if(document.empty())
    {
        return {};
    }
    serialization::path_set stated;
    if(!serialization::get_leaf_paths(document, SETTINGS_DOCUMENT_ROOT, stated))
    {
        mark_unreadable();
        return {};
    }
    serialization::path_set known;
    serialization::get_leaf_paths(serialization::save_to_document(base, SETTINGS_DOCUMENT_ROOT), SETTINGS_DOCUMENT_ROOT, known);
    std::vector<std::string> dropped;
    for(const auto& path : stated)
    {
        if(is_overridable(path) && known.contains(path))
        {
            paths_.insert(path);
        }
        else
        {
            dropped.push_back(path);
        }
    }
    if(paths_.empty())
    {
        return dropped;
    }
    // Loaded through the kept paths only, so nothing a dropped path names reaches the values.
    T resolved = base;
    {
        const serialization::scoped_path_filter filter(SETTINGS_DOCUMENT_ROOT, paths_);
        if(!serialization::load_from_document(document, SETTINGS_DOCUMENT_ROOT, resolved))
        {
            mark_unreadable();
            return {};
        }
    }
    write(resolved);
    return dropped;
}

template<typename T>
void profile_overrides::write(const T& resolved)
{
    document_ = serialization::save_to_sparse_document(resolved, SETTINGS_DOCUMENT_ROOT, paths_);
}

} // namespace unravel

/// The project's settings profiles on disk: profiles/<name>/profile.cfg, settings.cfg and deploy.cfg.
namespace unravel::deployment
{

/// The project's folder of profiles, one subfolder per profile.
constexpr const char* PROFILES_FOLDER = "profiles";
/// The profile a project gets when it has none.
constexpr const char* DEFAULT_PROFILE_NAME = "Desktop";
/// The Android profile a project from before profiles gets when it deployed to Android.
constexpr const char* DEFAULT_ANDROID_PROFILE_NAME = "Android";

/// A profile name is a folder name, as a release name is (is_valid_release_name).
auto is_valid_profile_name(const std::string& name) -> bool;

auto get_profiles_folder(const fs::path& project_root) -> fs::path;
auto get_profile_folder(const fs::path& project_root, const std::string& name) -> fs::path;

/// Whether a project settings property may differ per profile. Layers and input actions may not, since
/// scenes and scripts name them; nor the physics backend, which gameplay is tuned to; nor the Game view's
/// list of resolutions, an editor setting the Game view keeps a choice from; nor the build section, which
/// the deploy writes.
auto is_overridable_settings_path(hpp::string_view path) -> bool;
/// Whether a deploy settings property may differ per profile: every one but the deploy location, which
/// all profiles share (each builds into a folder of its own in it).
auto is_overridable_deploy_path(hpp::string_view path) -> bool;

/// The project settings a profile runs with: the base with the profile's overrides, and its build section.
auto resolve_settings(const settings& base, const settings_profile& profile) -> settings;
/// The deploy settings a profile deploys with: the shared ones with the profile's overrides, and the
/// profile's own fields. The device fields are left empty.
auto resolve_deploy_settings(const deploy_settings& base, const settings_profile& profile) -> deploy_settings;

/**
 * @brief A new profile of `platform`, with the platform's starting values as visible overrides: for
 * Android, smaller textures and the budgets a phone can carry. Nothing is stored.
 */
auto make_profile(const std::string& name, target_platform platform, const settings& base_settings) -> settings_profile;

/// The profiles of a project, by name, and what was wrong with their files.
struct profile_list
{
    std::vector<settings_profile> profiles;
    /// One sentence per problem: a profile folder that could not be read, overrides that were left out.
    std::vector<std::string> warnings;
};

/// Reads every profile in profiles/. A profile's overrides are checked against the base settings: the
/// ones that name no property, or one that may not differ per profile, are left out with a warning.
auto load_profiles(const fs::path& project_root, const settings& base_settings, const deploy_settings& base_deploy)
    -> profile_list;

/// Writes a profile's files; a document with no overrides removes its file.
auto save_profile(const fs::path& project_root, const settings_profile& profile) -> result;

/// Deletes a profile's folder.
auto remove_profile_files(const fs::path& project_root, const std::string& name) -> result;

/// Renames a profile's folder. Refuses a name in use or not valid.
auto rename_profile_files(const fs::path& project_root, const std::string& name, const std::string& new_name) -> result;

/// What a project's deploy settings held before profiles: the platform it deployed to and the Android
/// fields, read from deploy/deploy.cfg.
struct legacy_deploy_target
{
    target_platform platform{target_platform::desktop};
    android_deploy_settings android;
    /// Whether any Android field differs from its default.
    bool has_android_settings{};
};

/// The old target fields of a deploy.cfg; empty when the file has none (a deploy.cfg written with
/// profiles, or no file).
auto read_legacy_deploy_target(const fs::path& deploy_cfg) -> hpp::optional<legacy_deploy_target>;

/**
 * @brief The profiles a project from before profiles starts with: Desktop, and Android when it deployed
 * to Android or set an Android field.
 * @param deploy_profile Receives the name of the profile of the old target.
 */
auto make_profiles_from_legacy(const legacy_deploy_target& legacy, const settings& base_settings, std::string& deploy_profile)
    -> std::vector<settings_profile>;

} // namespace unravel::deployment
