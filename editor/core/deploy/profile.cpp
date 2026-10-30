#include "profile.h"
#include "folders.h"
#include "releases.h"

#include <deploy/meta/deploy_settings.hpp>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/settings/settings.hpp>
#include <logging/logging.h>
#include <serialization/associative_archive.h>
#include <string_utils/utils.h>

// must be below all
#include <engine/assets/impl/asset_writer.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace unravel
{

auto profile_overrides::get_document() const -> const std::string&
{
    return document_;
}

auto profile_overrides::get_document_to_store() const -> const std::string&
{
    return is_edited_ ? document_ : source_document_;
}

auto profile_overrides::get_paths() const -> const serialization::path_set&
{
    return paths_;
}

auto profile_overrides::is_empty() const -> bool
{
    return paths_.empty();
}

auto profile_overrides::is_overridden(hpp::string_view path) const -> bool
{
    return serialization::is_path_covered(path, paths_);
}

auto profile_overrides::has_overrides_within(hpp::string_view path) const -> bool
{
    return std::any_of(paths_.begin(),
                       paths_.end(),
                       [path](const std::string& listed)
                       {
                           return serialization::is_path_within(listed, path);
                       });
}

auto profile_overrides::is_related(hpp::string_view path) const -> bool
{
    return serialization::is_path_related(path, paths_);
}

void profile_overrides::clear()
{
    document_.clear();
    paths_.clear();
}

auto profile_overrides::is_unreadable() const -> bool
{
    return is_unreadable_;
}

void profile_overrides::mark_unreadable()
{
    clear();
    is_edited_ = false;
    is_unreadable_ = true;
}

namespace
{
/// The top node of profile.cfg.
constexpr const char* PROFILE_DOCUMENT_ROOT = "profile";
constexpr const char* PROFILE_FILE_NAME = "profile.cfg";
constexpr const char* PROFILE_SETTINGS_FILE_NAME = "settings.cfg";
constexpr const char* PROFILE_DEPLOY_FILE_NAME = "deploy.cfg";
/// The most a new Android profile lets a texture keep by default.
constexpr auto ANDROID_DEFAULT_MAX_TEXTURE_SIZE = texture_importer_meta::texture_size::size_1024;

/// profile.cfg: what a profile is, apart from its overrides.
struct profile_header
{
    target_platform platform{target_platform::desktop};
    android_deploy_settings android;
    desktop_deploy_settings desktop;
};

/// The target fields deploy.cfg held before profiles, and which of them it had.
struct legacy_target_fields
{
    bool has_target{};
    bool has_android{};
    target_platform platform{target_platform::desktop};
    android_deploy_settings android;
};

SAVE_INLINE(profile_header)
{
    try_save(ar, ser20::make_nvp("platform", obj.platform));
    if(obj.platform == target_platform::android)
    {
        try_save(ar, ser20::make_nvp("android", obj.android));
    }
    if(obj.platform == target_platform::desktop)
    {
        try_save(ar, ser20::make_nvp("desktop", obj.desktop));
    }
}

LOAD_INLINE(profile_header)
{
    try_load(ar, ser20::make_nvp("platform", obj.platform));
    try_load(ar, ser20::make_nvp("android", obj.android));
    try_load(ar, ser20::make_nvp("desktop", obj.desktop));
}

LOAD_INLINE(legacy_target_fields)
{
    obj.has_target = try_load(ar, ser20::make_nvp("deploy_target", obj.platform));
    bool has_android = false;
    has_android |= try_load(ar, ser20::make_nvp("android_application_id", obj.android.application_id));
    has_android |= try_load(ar, ser20::make_nvp("android_version_code", obj.android.version_code));
    has_android |= try_load(ar, ser20::make_nvp("android_screen_orientation", obj.android.screen_orientation));
    has_android |= try_load(ar, ser20::make_nvp("android_icon", obj.android.icon));
    has_android |= try_load(ar, ser20::make_nvp("android_icon_background", obj.android.icon_background));
    has_android |= try_load(ar, ser20::make_nvp("android_release", obj.android.release));
    std::string keystore;
    if(try_load(ar, ser20::make_nvp("android_keystore", keystore)))
    {
        obj.android.keystore = fs::from_utf8_string(keystore);
        has_android = true;
    }
    has_android |= try_load(ar, ser20::make_nvp("android_key_alias", obj.android.key_alias));
    obj.has_android = has_android;
}

} // namespace

} // namespace unravel

namespace unravel::deployment
{
namespace
{
/// Writes `text` to `file` atomically; an empty text removes the file instead.
auto write_text_file(const fs::path& file, const std::string& text) -> result
{
    fs::error_code ec;
    if(text.empty())
    {
        fs::remove(file, ec);
        return ec ? result::failure(fmt::format("Removing {} failed: {}", file.generic_string(), ec.message())) : result{};
    }
    asset_writer::atomic_write_file(
        file,
        [&text](const fs::path& temp)
        {
            std::ofstream stream(temp, std::ios::binary);
            stream << text;
        },
        ec);
    return ec ? result::failure(fmt::format("Writing {} failed: {}", file.generic_string(), ec.message())) : result{};
}

auto join_names(const std::vector<std::string>& names) -> std::string
{
    std::string joined;
    for(const auto& name : names)
    {
        joined += (joined.empty() ? "" : ", ") + name;
    }
    return joined;
}

/**
 * @brief Reads one of a profile's override files into `overrides`.
 * @return A warning, or an empty string. A file that exists but cannot be read, or does not parse, leaves the
 * overrides unreadable and the file untouched.
 */
template<typename T>
auto read_overrides_file(const std::string& profile,
                         const fs::path& file,
                         const T& base,
                         override_path_filter is_overridable,
                         profile_overrides& overrides) -> std::string;

auto describe_dropped(const std::string& profile, const std::string& file, const std::vector<std::string>& dropped) -> std::string
{
    return fmt::format("Profile {}: {} states settings that no longer exist or that every profile shares, and they were "
                       "left out: {}.",
                       profile,
                       file,
                       join_names(dropped));
}

/// The values a new profile of `platform` starts with, set on `resolved`, which holds the base.
void set_platform_starting_values(target_platform platform, settings& resolved)
{
    if(platform != target_platform::android)
    {
        return;
    }
    auto& texture = resolved.assets.texture;
    if(texture.default_max_size == texture_importer_meta::texture_size::project_default ||
       texture.default_max_size > ANDROID_DEFAULT_MAX_TEXTURE_SIZE)
    {
        texture.default_max_size = ANDROID_DEFAULT_MAX_TEXTURE_SIZE;
    }
    // The global illumination's full tiers and its default atlas (about 538 MB) are a desktop's.
    auto& gi = resolved.global_illumination;
    gi.global_illumination_quality = gi_project_settings::quality_level::high;
    gi.reflection_quality = gi_project_settings::quality_level::high;
    gi.surface_cache_atlas_size = gi_project_settings::atlas_size::size_1024;
    auto& particles = resolved.graphics.particles;
    particles.force_half_resolution = true;
    particles.max_lighting = std::min(particles.max_lighting, ps_soa::particle_lighting::volumetric);
    // The effects a phone's GPU cannot afford at frame rate: global illumination and screen space indirect
    // lighting cost hundreds of milliseconds there, the other screen space effects and temporal AA's extra
    // passes several. The render scale stays the project's.
    auto& limits = resolved.graphics.limits;
    limits.global_illumination = false;
    limits.screen_space_reflections = false;
    limits.screen_space_indirect_lighting = false;
    limits.ambient_occlusion = false;
    limits.volumetric_fog = false;
    limits.max_anti_aliasing = std::min(limits.max_anti_aliasing, anti_aliasing_limit::fxaa);
    limits.max_shadow_resolution = std::min(limits.max_shadow_resolution, sm_resolution::high);
    limits.soft_shadows = false;
    limits.contact_shadows = false;
}

auto load_profile(const fs::path& folder,
                  const settings& base_settings,
                  const deploy_settings& base_deploy,
                  std::vector<std::string>& warnings) -> hpp::optional<settings_profile>
{
    settings_profile profile;
    profile.name = fs::to_utf8_string(folder.filename());
    std::string header_document;
    if(!read_whole_file(folder / PROFILE_FILE_NAME, header_document))
    {
        warnings.push_back(fmt::format("Profile {} has no readable {} and was left out.", profile.name, PROFILE_FILE_NAME));
        return hpp::nullopt;
    }
    profile_header header;
    if(!serialization::load_from_document(header_document, PROFILE_DOCUMENT_ROOT, header))
    {
        warnings.push_back(fmt::format("Profile {}: {} could not be read, and the profile was left out.",
                                       profile.name,
                                       PROFILE_FILE_NAME));
        return hpp::nullopt;
    }
    profile.platform = header.platform;
    profile.android = header.android;
    profile.desktop = header.desktop;
    for(auto&& warning : {read_overrides_file(profile.name,
                                              folder / PROFILE_SETTINGS_FILE_NAME,
                                              base_settings,
                                              &is_overridable_settings_path,
                                              profile.settings),
                          read_overrides_file(profile.name,
                                              folder / PROFILE_DEPLOY_FILE_NAME,
                                              base_deploy,
                                              &is_overridable_deploy_path,
                                              profile.deploy)})
    {
        if(!warning.empty())
        {
            warnings.push_back(warning);
        }
    }
    return profile;
}

template<typename T>
auto read_overrides_file(const std::string& profile,
                         const fs::path& file,
                         const T& base,
                         override_path_filter is_overridable,
                         profile_overrides& overrides) -> std::string
{
    const auto file_name = fs::to_utf8_string(file.filename());
    std::string document;
    fs::error_code ec;
    const bool is_present = fs::exists(file, ec);
    if(is_present && !read_whole_file(file, document))
    {
        overrides.mark_unreadable();
    }
    const auto dropped = overrides.is_unreadable() ? std::vector<std::string>{} : overrides.read(document, base, is_overridable);
    if(overrides.is_unreadable())
    {
        return fmt::format("Profile {}: {} could not be read, so the profile overrides none of its settings and the file "
                           "is left as it is until it is mended.",
                           profile,
                           file_name);
    }
    return dropped.empty() ? std::string{} : describe_dropped(profile, file_name, dropped);
}

} // namespace

auto is_valid_profile_name(const std::string& name) -> bool
{
    return is_valid_release_name(name);
}

auto get_profiles_folder(const fs::path& project_root) -> fs::path
{
    return project_root / PROFILES_FOLDER;
}

auto get_profile_folder(const fs::path& project_root, const std::string& name) -> fs::path
{
    return get_profiles_folder(project_root) / fs::from_utf8_string(name);
}

auto is_overridable_settings_path(hpp::string_view path) -> bool
{
    return !serialization::is_path_within(path, "layer") && !serialization::is_path_within(path, "input") &&
           !serialization::is_path_within(path, "resolution") && !serialization::is_path_within(path, "build") &&
           !serialization::is_path_within(path, "physics/backend");
}

auto is_overridable_deploy_path(hpp::string_view path) -> bool
{
    return !serialization::is_path_within(path, "deploy_location");
}

auto resolve_settings(const settings& base, const settings_profile& profile) -> settings
{
    settings resolved = base;
    if(!profile.settings.apply(resolved))
    {
        APPLOG_WARNING("Profile {}: its settings overrides could not be read; the project settings apply.", profile.name);
        resolved = base;
    }
    resolved.build.profile = profile.name;
    resolved.build.platform = profile.platform;
    return resolved;
}

auto resolve_deploy_settings(const deploy_settings& base, const settings_profile& profile) -> deploy_settings
{
    deploy_settings resolved = base;
    if(!profile.deploy.apply(resolved))
    {
        APPLOG_WARNING("Profile {}: its deploy overrides could not be read; the shared deploy settings apply.", profile.name);
        resolved = base;
    }
    // Every profile builds under the one deploy location.
    resolved.deploy_location = base.deploy_location;
    resolved.profile = profile.name;
    resolved.deploy_target = profile.platform;
    resolved.android = profile.android;
    resolved.desktop = profile.desktop;
    resolved.android_device.clear();
    resolved.android_replace_installed = false;
    return resolved;
}

auto make_profile(const std::string& name, target_platform platform, const settings& base_settings) -> settings_profile
{
    settings_profile profile;
    profile.name = name;
    profile.platform = platform;
    settings resolved = base_settings;
    set_platform_starting_values(platform, resolved);
    serialization::path_set changed;
    serialization::get_changed_paths(serialization::save_to_document(base_settings, SETTINGS_DOCUMENT_ROOT),
                                     serialization::save_to_document(resolved, SETTINGS_DOCUMENT_ROOT),
                                     SETTINGS_DOCUMENT_ROOT,
                                     changed);
    std::erase_if(changed,
                  [](const std::string& path)
                  {
                      return !is_overridable_settings_path(path);
                  });
    profile.settings.set_values(resolved, changed);
    return profile;
}

auto load_profiles(const fs::path& project_root, const settings& base_settings, const deploy_settings& base_deploy)
    -> profile_list
{
    profile_list list;
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(get_profiles_folder(project_root), ec))
    {
        if(!entry.is_directory(ec))
        {
            continue;
        }
        const auto name = fs::to_utf8_string(entry.path().filename());
        if(!is_valid_profile_name(name))
        {
            list.warnings.push_back(fmt::format("The folder profiles/{} is no valid profile name and was left out.", name));
            continue;
        }
        if(auto profile = load_profile(entry.path(), base_settings, base_deploy, list.warnings))
        {
            list.profiles.push_back(std::move(*profile));
        }
    }
    std::sort(list.profiles.begin(),
              list.profiles.end(),
              [](const settings_profile& lhs, const settings_profile& rhs)
              {
                  return string_utils::to_lower(lhs.name) < string_utils::to_lower(rhs.name);
              });
    return list;
}

auto save_profile(const fs::path& project_root, const settings_profile& profile) -> result
{
    if(!is_valid_profile_name(profile.name))
    {
        return result::failure(fmt::format("'{}' is no valid profile name.", profile.name));
    }
    const auto folder = get_profile_folder(project_root, profile.name);
    fs::error_code ec;
    fs::create_directories(folder, ec);
    if(ec)
    {
        return result::failure(fmt::format("Creating {} failed: {}", folder.generic_string(), ec.message()));
    }
    profile_header header;
    header.platform = profile.platform;
    header.android = profile.android;
    header.desktop = profile.desktop;
    auto written = write_text_file(folder / PROFILE_FILE_NAME, serialization::save_to_document(header, PROFILE_DOCUMENT_ROOT));
    if(!written.is_ok())
    {
        return written;
    }
    // A file that could not be read keeps what it holds, for whoever mends it.
    if(!profile.settings.is_unreadable())
    {
        written = write_text_file(folder / PROFILE_SETTINGS_FILE_NAME, profile.settings.get_document_to_store());
        if(!written.is_ok())
        {
            return written;
        }
    }
    if(!profile.deploy.is_unreadable())
    {
        written = write_text_file(folder / PROFILE_DEPLOY_FILE_NAME, profile.deploy.get_document_to_store());
    }
    return written;
}

auto remove_profile_files(const fs::path& project_root, const std::string& name) -> result
{
    if(!is_valid_profile_name(name))
    {
        return result::failure(fmt::format("'{}' is no valid profile name.", name));
    }
    fs::error_code ec;
    fs::remove_all(get_profile_folder(project_root, name), ec);
    return ec ? result::failure(fmt::format("Deleting profile {} failed: {}", name, ec.message())) : result{};
}

auto rename_profile_files(const fs::path& project_root, const std::string& name, const std::string& new_name) -> result
{
    if(!is_valid_profile_name(new_name))
    {
        return result::failure(fmt::format("'{}' is no valid profile name. A name cannot be empty, end with a dot or "
                                           "a space, or hold any of < > : \" / \\ | ? *.",
                                           new_name));
    }
    const auto from = get_profile_folder(project_root, name);
    const auto to = get_profile_folder(project_root, new_name);
    fs::error_code ec;
    if(!is_same_folder_name(name, new_name) && fs::exists(to, ec))
    {
        return result::failure(fmt::format("A profile named {} exists already.", new_name));
    }
    rename_folder(from, to, ec);
    return ec ? result::failure(fmt::format("Renaming profile {} to {} failed: {}", name, new_name, ec.message())) : result{};
}

auto read_legacy_deploy_target(const fs::path& deploy_cfg) -> hpp::optional<legacy_deploy_target>
{
    std::string document;
    if(!read_whole_file(deploy_cfg, document))
    {
        return hpp::nullopt;
    }
    legacy_target_fields fields;
    if(!serialization::load_from_document(document, SETTINGS_DOCUMENT_ROOT, fields) || (!fields.has_target && !fields.has_android))
    {
        return hpp::nullopt;
    }
    legacy_deploy_target legacy;
    legacy.platform = fields.platform;
    legacy.android = fields.android;
    legacy.has_android_settings = serialization::save_to_document(fields.android, SETTINGS_DOCUMENT_ROOT) !=
                                  serialization::save_to_document(android_deploy_settings{}, SETTINGS_DOCUMENT_ROOT);
    return legacy;
}

auto make_profiles_from_legacy(const legacy_deploy_target& legacy, const settings& base_settings, std::string& deploy_profile)
    -> std::vector<settings_profile>
{
    std::vector<settings_profile> profiles;
    profiles.push_back(make_profile(DEFAULT_PROFILE_NAME, target_platform::desktop, base_settings));
    const bool has_android = legacy.platform == target_platform::android || legacy.has_android_settings;
    if(has_android)
    {
        auto android = make_profile(DEFAULT_ANDROID_PROFILE_NAME, target_platform::android, base_settings);
        android.android = legacy.android;
        profiles.push_back(std::move(android));
    }
    deploy_profile = legacy.platform == target_platform::android ? profiles.back().name : profiles.front().name;
    return profiles;
}

} // namespace unravel::deployment
