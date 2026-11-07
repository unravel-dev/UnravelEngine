#include "texture_variants.h"
#include "files.h"
#include "keys.h"
#include "profile_cache.h"

#include <engine/assets/impl/asset_extensions.h>
#include <graphics/texture.h>

namespace unravel::deployment
{
namespace
{
constexpr cook_wording TEXTURE_WORDING{"texture(s)", "texture variant(s)", "texture cache"};

auto is_texture_key(const std::string& key) -> bool
{
    return ex::is_format<gfx::texture>(fs::path(key).extension().generic_string());
}

/// What a texture compiles with for the profile and for the editor, from one read of its importer settings.
struct texture_parameters_pair
{
    asset_compiler::texture_compile_parameters profile;
    asset_compiler::texture_compile_parameters editor;

    /// Whether the profile compiles the texture differently from the editor.
    auto needs_variant() const -> bool
    {
        return profile != editor;
    }
};

auto get_parameters(const texture_cook_plan& plan, const std::string& key) -> texture_parameters_pair
{
    const auto importer = asset_compiler::read_texture_importer(key);
    return {asset_compiler::resolve_texture_parameters(importer, plan.target),
            asset_compiler::resolve_texture_parameters(importer, plan.editor_target)};
}

auto cook_texture(const texture_cook_plan& plan, const std::string& key) -> cook_outcome
{
    cook_outcome outcome;
    const auto relative = get_compiled_relative_path(key);
    if(relative.empty())
    {
        return outcome;
    }
    // The editor's file ships where the profile compiles the texture as the editor does, once that file is
    // current: settings changed while the editor was closed leave it compiled with the old ones until the
    // editor compiles it again, and a deploy never ships those.
    const auto parameters = get_parameters(plan, key);
    if(!parameters.needs_variant() && asset_compiler::is_texture_compiled_with(key, plan.compiled / relative, parameters.editor))
    {
        return outcome;
    }
    outcome.files.push_back(get_cache_file_key(relative));
    const auto variant = plan.variants / relative;
    if(asset_compiler::is_texture_compiled_with(key, variant, parameters.profile))
    {
        return outcome;
    }
    if(asset_compiler::compile_texture_for_target(key, variant, plan.target))
    {
        ++outcome.compiled;
    }
    else
    {
        outcome.has_failed = true;
    }
    return outcome;
}

/// The formats an Android or web profile's textures compile to.
auto get_compression_family(deploy_texture_compression compression) -> asset_compiler::texture_compression_family
{
    switch(compression)
    {
        case deploy_texture_compression::etc2:
            return asset_compiler::texture_compression_family::etc2;
        case deploy_texture_compression::bc:
            return asset_compiler::texture_compression_family::bc;
        case deploy_texture_compression::astc:
        default:
            return asset_compiler::texture_compression_family::astc;
    }
}
} // namespace

auto get_profile_texture_target(const deploy_settings& params, const settings& runtime_settings) -> asset_compiler::texture_target
{
    asset_compiler::texture_target target;
    target.platform = params.deploy_target;
    target.defaults = runtime_settings.assets.texture;
    if(params.deploy_target == deploy_platform::android)
    {
        target.family = get_compression_family(params.android.texture_compression);
    }
    if(params.deploy_target == deploy_platform::web)
    {
        target.family = get_compression_family(params.web.texture_compression);
    }
    return target;
}

auto make_texture_cook_plan(const fs::path& project_root,
                            const deploy_settings& params,
                            const settings& runtime_settings,
                            const std::string& protocol,
                            const asset_database::database_t& database,
                            const std::unordered_set<std::string>& left_out,
                            const asset_compiler::texture_target& editor_target) -> texture_cook_plan
{
    texture_cook_plan plan;
    plan.profile = params.profile;
    plan.target = get_profile_texture_target(params, runtime_settings);
    if(protocol != CONTENT_PROTOCOL)
    {
        // The engine's textures take the defaults every project shares, as the editor compiles them; only their
        // formats follow the profile.
        plan.target.defaults = editor_target.defaults;
    }
    plan.editor_target = editor_target;
    plan.variants = get_profile_variants_folder(project_root, params.profile, protocol);
    plan.compiled = fs::resolve_protocol(ex::get_compiled_directory(protocol));
    for(const auto& [uid, meta] : database)
    {
        if(!is_texture_key(meta.location))
        {
            continue;
        }
        (left_out.contains(fs::make_path_key(meta.location)) ? plan.left_out : plan.shipped).push_back(meta.location);
    }
    return plan;
}

auto cook_texture_variants(const texture_cook_plan& plan) -> cook_report
{
    const auto outcomes = cook_all(plan.shipped,
                                   [&plan](std::size_t index)
                                   {
                                       return cook_texture(plan, plan.shipped[index]);
                                   });
    cook_report report;
    std::unordered_set<std::string> kept;
    for(std::size_t i = 0; i < outcomes.size(); ++i)
    {
        add_outcome(plan.shipped[i], outcomes[i], kept, report);
    }
    // A left-out texture's variant stays for a deploy that ships it, which checks it first.
    for(const auto& key : plan.left_out)
    {
        const auto relative = get_compiled_relative_path(key);
        if(!relative.empty() && get_parameters(plan, key).needs_variant())
        {
            keep_cache_file(kept, get_cache_file_key(relative));
        }
    }
    sweep_cache_folder(plan.variants, kept, report);
    return report;
}

void cook_textures(const texture_cook_plan& plan)
{
    report_cook_or_throw(plan.profile, cook_texture_variants(plan), TEXTURE_WORDING);
}

} // namespace unravel::deployment
