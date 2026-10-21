#include "texture_variants.h"
#include "files.h"
#include "keys.h"
#include "profile_cache.h"

#include <concurrency/parallel.h>
#include <engine/assets/impl/asset_extensions.h>
#include <graphics/texture.h>
#include <logging/logging.h>

#include <cstdint>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
/// Failed textures named in the step's error before "and N more".
constexpr std::size_t LISTED_FAILURES_MAX = 10;
constexpr const char* MANIFEST_SUFFIX = ".manifest";

/// What cooking one texture did.
struct cook_outcome
{
    /// The variant's path key under the variants folder; empty when the texture ships the editor's file.
    std::string variant;
    bool is_compiled{};
    bool has_failed{};
};

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

auto get_variant_key(const fs::path& relative) -> std::string
{
    return fs::make_path_key(relative.generic_string());
}

void keep_variant(std::unordered_set<std::string>& kept, const std::string& variant)
{
    kept.insert(variant);
    kept.insert(variant + MANIFEST_SUFFIX);
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
    outcome.variant = get_variant_key(relative);
    const auto variant = plan.variants / relative;
    if(asset_compiler::is_texture_compiled_with(key, variant, parameters.profile))
    {
        return outcome;
    }
    outcome.is_compiled = true;
    outcome.has_failed = !asset_compiler::compile_texture_for_target(key, variant, plan.target);
    return outcome;
}

auto get_source_size(const std::string& key) -> std::uintmax_t
{
    fs::error_code ec;
    const auto size = fs::file_size(fs::resolve_protocol(key), ec);
    return ec ? 0 : size;
}

auto describe_failures(const std::vector<std::string>& failed, const std::string& profile) -> std::string
{
    std::string listed;
    for(std::size_t i = 0; i < failed.size() && i < LISTED_FAILURES_MAX; ++i)
    {
        listed += (i == 0 ? "" : ", ") + failed[i];
    }
    if(failed.size() > LISTED_FAILURES_MAX)
    {
        listed += fmt::format(" and {} more", failed.size() - LISTED_FAILURES_MAX);
    }
    return fmt::format("{} texture(s) could not be compiled for profile {} (the log says why): {}", failed.size(), profile, listed);
}
} // namespace

auto get_profile_texture_target(const deploy_settings& params, const settings& runtime_settings) -> asset_compiler::texture_target
{
    asset_compiler::texture_target target;
    target.platform = params.deploy_target;
    target.defaults = runtime_settings.assets.texture;
    if(params.deploy_target == deploy_platform::android)
    {
        target.family = params.android.texture_compression == android_texture_compression::etc2
                            ? asset_compiler::texture_compression_family::etc2
                            : asset_compiler::texture_compression_family::astc;
    }
    return target;
}

auto make_texture_cook_plan(const fs::path& project_root,
                            const deploy_settings& params,
                            const settings& runtime_settings,
                            const asset_database::database_t& app_database,
                            const std::unordered_set<std::string>& left_out,
                            const asset_compiler::texture_target& editor_target) -> texture_cook_plan
{
    texture_cook_plan plan;
    plan.profile = params.profile;
    plan.target = get_profile_texture_target(params, runtime_settings);
    plan.editor_target = editor_target;
    plan.variants = get_profile_variants_folder(project_root, params.profile, CONTENT_PROTOCOL);
    plan.compiled = fs::resolve_protocol(ex::get_compiled_directory(CONTENT_PROTOCOL));
    for(const auto& [uid, meta] : app_database)
    {
        if(!is_texture_key(meta.location))
        {
            continue;
        }
        (left_out.contains(fs::make_path_key(meta.location)) ? plan.left_out : plan.shipped).push_back(meta.location);
    }
    return plan;
}

auto cook_texture_variants(const texture_cook_plan& plan) -> texture_cook_report
{
    std::vector<cook_outcome> outcomes(plan.shipped.size());
    std::vector<std::uintmax_t> costs;
    costs.reserve(plan.shipped.size());
    for(const auto& key : plan.shipped)
    {
        costs.push_back(get_source_size(key));
    }
    {
        // A compile can hold every thread it gets for seconds: never the pool the frame's loops wait for.
        const poolstl::background_scope background;
        poolstl::for_each_costliest_first_par_if(true,
                                                 costs,
                                                 [&](std::size_t index)
                                                 {
                                                     outcomes[index] = cook_texture(plan, plan.shipped[index]);
                                                 });
    }
    texture_cook_report report;
    std::unordered_set<std::string> kept;
    for(std::size_t i = 0; i < outcomes.size(); ++i)
    {
        const auto& outcome = outcomes[i];
        if(!outcome.variant.empty())
        {
            keep_variant(kept, outcome.variant);
            ++report.variants;
        }
        report.compiled += outcome.is_compiled && !outcome.has_failed ? 1 : 0;
        if(outcome.has_failed)
        {
            report.failed.push_back(plan.shipped[i]);
        }
    }
    // A left-out texture's variant stays for a deploy that ships it, which checks it first.
    for(const auto& key : plan.left_out)
    {
        const auto relative = get_compiled_relative_path(key);
        if(!relative.empty() && get_parameters(plan, key).needs_variant())
        {
            keep_variant(kept, get_variant_key(relative));
        }
    }
    auto swept = sweep_folder(plan.variants, kept);
    report.removed = swept.removed;
    report.undeletable = std::move(swept.failed);
    return report;
}

void cook_textures(const texture_cook_plan& plan)
{
    const auto report = cook_texture_variants(plan);
    APPLOG_INFO("Deploy: profile {} ships {} texture variant(s), {} compiled now; {} file(s) of its cache that nothing needs "
                "were deleted.",
                plan.profile,
                report.variants,
                report.compiled,
                report.removed);
    if(!report.failed.empty())
    {
        throw std::runtime_error(describe_failures(report.failed, plan.profile));
    }
    if(!report.undeletable.empty())
    {
        throw std::runtime_error(fmt::format("{} file(s) of profile {}'s texture cache that no texture needs could not be "
                                             "deleted, and the deploy would ship them, for example {}. Close the program "
                                             "that holds them, then deploy again.",
                                             report.undeletable.size(),
                                             plan.profile,
                                             report.undeletable.front().generic_string()));
    }
}

} // namespace unravel::deployment
