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
    const auto parameters = get_parameters(plan, key);
    return cook_variant(key,
                        plan.compiled,
                        plan.variants,
                        {parameters.needs_variant(),
                         [&](const fs::path& file)
                         {
                             return asset_compiler::is_texture_compiled_with(key, file, parameters.editor);
                         },
                         [&](const fs::path& file)
                         {
                             return asset_compiler::is_texture_compiled_with(key, file, parameters.profile);
                         },
                         [&](const fs::path& file)
                         {
                             return asset_compiler::compile_texture_for_target(key, file, plan.target);
                         }});
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

auto get_texture_set_name(deploy_texture_compression compression) -> std::string
{
    return asset_compiler::get_family_name(get_compression_family(compression));
}

auto get_web_texture_sets(const web_deploy_settings& web) -> std::vector<deploy_texture_compression>
{
    if(web.phone_texture_compression == web.texture_compression)
    {
        return {web.texture_compression};
    }
    return {web.texture_compression, web.phone_texture_compression};
}

auto make_texture_set_cook_plan(const texture_cook_plan& plan,
                                const fs::path& project_root,
                                const std::string& protocol,
                                deploy_texture_compression compression) -> texture_cook_plan
{
    texture_cook_plan set = plan;
    set.target.family = get_compression_family(compression);
    set.variants = get_profile_texture_set_folder(project_root, plan.profile, get_texture_set_name(compression), protocol);
    return set;
}

auto make_texture_cook_plan(const fs::path& project_root,
                            const deploy_settings& params,
                            const settings& runtime_settings,
                            const std::string& protocol,
                            const asset_database::database_t& database,
                            const left_out_assets& left_out,
                            const asset_compiler::texture_target& editor_target) -> texture_cook_plan
{
    texture_cook_plan plan;
    plan.textures = split_assets(database, is_asset_key<gfx::texture>, left_out);
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
    return plan;
}

auto cook_texture_variants(const texture_cook_plan& plan) -> cook_report
{
    return cook_variants(
        plan.textures,
        plan.variants,
        [&plan](const std::string& key)
        {
            return cook_texture(plan, key);
        },
        [&plan](const std::string& key)
        {
            return get_parameters(plan, key).needs_variant();
        });
}

void cook_textures(const texture_cook_plan& plan)
{
    report_cook_or_throw(plan.profile, cook_texture_variants(plan), TEXTURE_WORDING);
}

} // namespace unravel::deployment
