#include "audio_variants.h"
#include "files.h"
#include "keys.h"
#include "profile_cache.h"

#include <asset_compiler/audio_compression.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/audio/audio_clip.h>

namespace unravel::deployment
{
namespace
{
constexpr cook_wording AUDIO_WORDING{"audio clip(s)", "audio variant(s)", "audio cache"};

auto is_audio_key(const std::string& key) -> bool
{
    return ex::is_format<audio_clip>(fs::path(key).extension().generic_string());
}

/// What a clip is stored as for the profile and for the editor (get_stored_compression), from one read of its import
/// settings: a clip that is no lossless source ships its own bytes for every profile.
struct compression_pair
{
    audio_importer_meta::compression_quality profile{};
    audio_importer_meta::compression_quality editor{};

    /// Whether the profile compiles the clip differently from the editor.
    auto needs_variant() const -> bool
    {
        return profile != editor;
    }
};

auto get_compressions(const audio_cook_plan& plan, const std::string& key, const audio_importer_meta& importer)
    -> compression_pair
{
    const auto codec = asset_compiler::get_audio_codec(key);
    return {asset_compiler::get_stored_compression(asset_compiler::resolve_audio_compression(importer, plan.platform, plan.defaults),
                                                   codec),
            asset_compiler::get_stored_compression(
                asset_compiler::resolve_audio_compression(importer, target_platform::desktop, plan.editor_defaults),
                codec)};
}

auto cook_clip(const audio_cook_plan& plan, const std::string& key) -> cook_outcome
{
    cook_outcome outcome;
    const auto relative = get_compiled_relative_path(key);
    if(relative.empty())
    {
        return outcome;
    }
    const auto importer = asset_compiler::read_audio_importer(key);
    const auto compressions = get_compressions(plan, key, importer);
    // The editor's file ships where the profile compiles the clip as the editor does, once that file is current:
    // settings changed while the editor was closed leave it compiled with the old ones until the editor compiles it
    // again, and a deploy never ships those.
    if(!compressions.needs_variant() &&
       asset_compiler::is_audio_compiled_with(key, plan.compiled / relative, compressions.editor))
    {
        return outcome;
    }
    outcome.files.push_back(get_cache_file_key(relative));
    const auto variant = plan.variants / relative;
    if(asset_compiler::is_audio_compiled_with(key, variant, compressions.profile))
    {
        return outcome;
    }
    if(asset_compiler::compile_audio_clip(key, variant, importer, compressions.profile))
    {
        ++outcome.compiled;
    }
    else
    {
        outcome.has_failed = true;
    }
    return outcome;
}
} // namespace

auto make_audio_cook_plan(const fs::path& project_root,
                          const deploy_settings& params,
                          const settings& runtime_settings,
                          const asset_database::database_t& database,
                          const std::unordered_set<std::string>& left_out,
                          const settings::asset_settings::audio_importer_settings& editor_defaults) -> audio_cook_plan
{
    audio_cook_plan plan;
    plan.profile = params.profile;
    plan.platform = params.deploy_target;
    plan.defaults = runtime_settings.assets.audio;
    plan.editor_defaults = editor_defaults;
    plan.variants = get_profile_audio_folder(project_root, params.profile, CONTENT_PROTOCOL);
    plan.compiled = fs::resolve_protocol(ex::get_compiled_directory(CONTENT_PROTOCOL));
    for(const auto& [uid, meta] : database)
    {
        if(!is_audio_key(meta.location))
        {
            continue;
        }
        (left_out.contains(fs::make_path_key(meta.location)) ? plan.left_out : plan.shipped).push_back(meta.location);
    }
    return plan;
}

auto cook_audio_variants(const audio_cook_plan& plan) -> cook_report
{
    const auto outcomes = cook_all(plan.shipped,
                                   [&plan](std::size_t index)
                                   {
                                       return cook_clip(plan, plan.shipped[index]);
                                   });
    cook_report report;
    std::unordered_set<std::string> kept;
    for(std::size_t i = 0; i < outcomes.size(); ++i)
    {
        add_outcome(plan.shipped[i], outcomes[i], kept, report);
    }
    // A left-out clip's variant stays for a deploy that ships it, which checks it first.
    for(const auto& key : plan.left_out)
    {
        const auto relative = get_compiled_relative_path(key);
        if(!relative.empty() && get_compressions(plan, key, asset_compiler::read_audio_importer(key)).needs_variant())
        {
            keep_cache_file(kept, get_cache_file_key(relative));
        }
    }
    sweep_cache_folder(plan.variants, kept, report);
    return report;
}

void cook_audio(const audio_cook_plan& plan)
{
    report_cook_or_throw(plan.profile, cook_audio_variants(plan), AUDIO_WORDING);
}

} // namespace unravel::deployment
