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
    const auto importer = asset_compiler::read_audio_importer(key);
    const auto compressions = get_compressions(plan, key, importer);
    return cook_variant(key,
                        plan.compiled,
                        plan.variants,
                        {compressions.needs_variant(),
                         [&](const fs::path& file)
                         {
                             return asset_compiler::is_audio_compiled_with(key, file, compressions.editor);
                         },
                         [&](const fs::path& file)
                         {
                             return asset_compiler::is_audio_compiled_with(key, file, compressions.profile);
                         },
                         [&](const fs::path& file)
                         {
                             return asset_compiler::compile_audio_clip(key, file, importer, compressions.profile);
                         }});
}
} // namespace

auto make_audio_cook_plan(const fs::path& project_root,
                          const deploy_settings& params,
                          const settings& runtime_settings,
                          const asset_database::database_t& database,
                          const left_out_assets& left_out,
                          const settings::asset_settings::audio_importer_settings& editor_defaults) -> audio_cook_plan
{
    audio_cook_plan plan;
    plan.clips = split_assets(database, is_asset_key<audio_clip>, left_out);
    plan.profile = params.profile;
    plan.platform = params.deploy_target;
    plan.defaults = runtime_settings.assets.audio;
    plan.editor_defaults = editor_defaults;
    plan.variants = get_profile_audio_folder(project_root, params.profile, CONTENT_PROTOCOL);
    plan.compiled = fs::resolve_protocol(ex::get_compiled_directory(CONTENT_PROTOCOL));
    return plan;
}

auto cook_audio_variants(const audio_cook_plan& plan) -> cook_report
{
    return cook_variants(
        plan.clips,
        plan.variants,
        [&plan](const std::string& key)
        {
            return cook_clip(plan, key);
        },
        [&plan](const std::string& key)
        {
            return get_compressions(plan, key, asset_compiler::read_audio_importer(key)).needs_variant();
        });
}

void cook_audio(const audio_cook_plan& plan)
{
    report_cook_or_throw(plan.profile, cook_audio_variants(plan), AUDIO_WORDING);
}

} // namespace unravel::deployment
