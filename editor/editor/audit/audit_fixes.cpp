#include "audit_fixes.h"

#include <audit/audit_catalog.h>
#include <audit/audit_settings_table.h>
#include <deploy/content.h>
#include <deploy/profile.h>
#include <editor/editing/editor_actions.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>

#include <array>
#include <vector>

namespace unravel
{
namespace
{

/// The platforms of the profiles an issue names.
auto get_issue_platforms(project_manager& pm, const audit::issue& item) -> std::vector<target_platform>
{
    std::vector<target_platform> platforms;
    for(const auto& name : item.profiles)
    {
        const auto* profile = pm.get_profiles().find(name);
        if(profile != nullptr && std::find(platforms.begin(), platforms.end(), profile->platform) == platforms.end())
        {
            platforms.push_back(profile->platform);
        }
    }
    return platforms;
}

/**
 * @brief The quality a texture compiles with on `platform`, to edit: Android's own, made from the texture's when it
 * had none, so the fix leaves the other platforms alone; for desktop and web (which reads desktop's) the desktop
 * override while there is one, else the texture's own.
 */
auto get_editable_quality(texture_importer_meta& importer, target_platform platform) -> texture_importer_meta::quality_meta&
{
    if(platform == target_platform::android)
    {
        auto& android = importer.platforms.android;
        if(!android.is_overridden)
        {
            android.is_overridden = true;
            android.quality = importer.quality;
        }
        return android.quality;
    }
    return importer.platforms.desktop.is_overridden ? importer.platforms.desktop.quality : importer.quality;
}

/// The largest texture size that is no larger than `pixels`.
auto get_size_at_most(std::int64_t pixels) -> texture_importer_meta::texture_size
{
    using size = texture_importer_meta::texture_size;
    constexpr std::array<std::pair<std::int64_t, size>, 10> SIZES{{{16384, size::size_16384},
                                                                   {8192, size::size_8192},
                                                                   {4096, size::size_4096},
                                                                   {2048, size::size_2048},
                                                                   {1024, size::size_1024},
                                                                   {512, size::size_512},
                                                                   {256, size::size_256},
                                                                   {128, size::size_128},
                                                                   {64, size::size_64},
                                                                   {32, size::size_32}}};
    for(const auto& [side, value] : SIZES)
    {
        if(side <= pixels)
        {
            return value;
        }
    }
    return size::size_32;
}

/// The clip's compression on `platform`, to edit: the platform's own, made from the clip's when it had none, so the
/// fix leaves the other platforms alone.
auto get_editable_compression(audio_importer_meta& importer, target_platform platform) -> audio_importer_meta::compression_quality&
{
    auto& own = platform == target_platform::android ? importer.platforms.android
                : platform == target_platform::web   ? importer.platforms.web
                                                     : importer.platforms.desktop;
    if(!own.is_overridden)
    {
        own.is_overridden = true;
        own.compression = importer.compression;
    }
    return own.compression;
}

/// Edits a copy of the asset's import settings of type T and writes them as the inspector's Apply does.
template<typename T, typename Edit>
auto edit_import_settings(rtti::context& ctx, const std::string& key, std::string& error, Edit&& edit) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto meta = am.get_metadata_for_key(key);
    const auto* current = meta.meta.importer ? meta.meta.importer->safe_cast<T>() : nullptr;
    if(current == nullptr)
    {
        error = "The asset is no longer in the project, or its import settings changed type.";
        return false;
    }
    auto edited = std::make_shared<T>(*current);
    edit(*edited);
    meta.meta.importer = edited;
    if(!editor_actions::save_import_settings(key, meta.meta))
    {
        error = "The asset's .meta file could not be written.";
        return false;
    }
    return true;
}

auto fix_texture(rtti::context& ctx, const audit::issue& item, const audit::config& settings, std::string& error) -> bool
{
    auto& pm = ctx.get_cached<project_manager>();
    const auto platforms = get_issue_platforms(pm, item);
    if(item.fix == audit::fixes::GENERATE_MIPS)
    {
        return edit_import_settings<texture_importer_meta>(ctx,
                                                           item.where.key,
                                                           error,
                                                           [](texture_importer_meta& importer)
                                                           {
                                                               importer.generate_mipmaps = true;
                                                           });
    }
    if(platforms.empty())
    {
        error = "The issue names no profile that still exists.";
        return false;
    }
    const auto* owner = audit::find_rule(audit::rules::TEXTURE_OVERSIZED);
    return edit_import_settings<texture_importer_meta>(ctx,
                                                       item.where.key,
                                                       error,
                                                       [&](texture_importer_meta& importer)
                                                       {
                                                           for(const auto platform : platforms)
                                                           {
                                                               auto& quality = get_editable_quality(importer, platform);
                                                               if(item.fix == audit::fixes::COMPRESS_TEXTURE)
                                                               {
                                                                   quality.compression =
                                                                       texture_importer_meta::compression_quality::normal_quality;
                                                               }
                                                               else if(owner != nullptr)
                                                               {
                                                                   quality.max_size = get_size_at_most(
                                                                       settings.get_threshold(*owner, audit::thresholds::MAX_SIDE, platform));
                                                               }
                                                           }
                                                       });
}

auto fix_audio(rtti::context& ctx, const audit::issue& item, std::string& error) -> bool
{
    auto& pm = ctx.get_cached<project_manager>();
    const auto platforms = get_issue_platforms(pm, item);
    if(item.fix == audit::fixes::COMPRESS_AUDIO && platforms.empty())
    {
        error = "The issue names no profile that still exists.";
        return false;
    }
    return edit_import_settings<audio_importer_meta>(ctx,
                                                     item.where.key,
                                                     error,
                                                     [&](audio_importer_meta& importer)
                                                     {
                                                         if(item.fix == audit::fixes::FORCE_MONO)
                                                         {
                                                             importer.force_to_mono = true;
                                                             return;
                                                         }
                                                         for(const auto platform : platforms)
                                                         {
                                                             get_editable_compression(importer, platform) =
                                                                 audio_importer_meta::compression_quality::normal_quality;
                                                         }
                                                     });
}

auto fix_mesh(rtti::context& ctx, const audit::issue& item, std::string& error) -> bool
{
    return edit_import_settings<mesh_importer_meta>(ctx,
                                                    item.where.key,
                                                    error,
                                                    [](mesh_importer_meta& importer)
                                                    {
                                                        importer.model.generate_lods = true;
                                                    });
}

auto fix_setting(rtti::context& ctx, const audit::issue& item, std::string& error) -> bool
{
    const auto* setting = audit::find_performance_setting(item.where.key);
    if(setting == nullptr)
    {
        error = "The setting is not one the audit knows how to change.";
        return false;
    }
    auto& pm = ctx.get_cached<project_manager>();
    const auto& base = pm.get_settings();
    for(const auto& name : item.profiles)
    {
        const auto* profile = pm.get_profiles().find(name);
        if(profile == nullptr)
        {
            continue;
        }
        auto edited = pm.get_profile_settings(name);
        const auto start = deployment::resolve_settings(base, deployment::make_profile(name, profile->platform, base));
        setting->copy(edited, start);
        const auto result = pm.override_profile_settings(ctx, name, edited);
        if(!result.is_ok())
        {
            error = result.error;
            return false;
        }
    }
    return true;
}

auto fix_content_rule(rtti::context& ctx, const audit::issue& item, std::string& error) -> bool
{
    auto& pm = ctx.get_cached<project_manager>();
    for(const auto& name : item.profiles)
    {
        auto edited = pm.get_profile_deploy_settings(name);
        if(!edited)
        {
            continue;
        }
        deployment::reset_rules(*edited, item.where.detail);
        const auto result = pm.override_profile_deploy(ctx, name, *edited);
        if(!result.is_ok())
        {
            error = result.error;
            return false;
        }
    }
    return true;
}

} // namespace

auto apply_audit_fix(rtti::context& ctx, const audit::issue& item, const audit::config& settings, std::string& error) -> bool
{
    const auto& fix = item.fix;
    if(fix == audit::fixes::COMPRESS_TEXTURE || fix == audit::fixes::LIMIT_TEXTURE_SIZE || fix == audit::fixes::GENERATE_MIPS)
    {
        return fix_texture(ctx, item, settings, error);
    }
    if(fix == audit::fixes::COMPRESS_AUDIO || fix == audit::fixes::FORCE_MONO)
    {
        return fix_audio(ctx, item, error);
    }
    if(fix == audit::fixes::GENERATE_LODS)
    {
        return fix_mesh(ctx, item, error);
    }
    if(fix == audit::fixes::USE_PLATFORM_START)
    {
        return fix_setting(ctx, item, error);
    }
    if(fix == audit::fixes::REMOVE_CONTENT_RULE)
    {
        return fix_content_rule(ctx, item, error);
    }
    error = "This issue has no fix the editor can apply.";
    return false;
}

} // namespace unravel
