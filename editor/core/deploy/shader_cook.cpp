#include "shader_cook.h"
#include "files.h"
#include "keys.h"
#include "profile_cache.h"

#include <asset_compiler/asset_compiler.h>
#include <concurrency/parallel.h>
#include <engine/assets/impl/asset_extensions.h>
#include <graphics/graphics.h>
#include <logging/logging.h>

#include <cstdint>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
/// Failed shaders named in the step's error before "and N more".
constexpr std::size_t LISTED_FAILURES_MAX = 10;
constexpr const char* MANIFEST_SUFFIX = ".manifest";

/// One shader of one protocol to cook.
struct shader_job
{
    const shader_cook_protocol* protocol{};
    std::string key;
};

/// What cooking one shader did.
struct shader_outcome
{
    /// The path keys of its cooked files under the protocol's folder, one per renderer.
    std::vector<std::string> variants;
    std::size_t compiled{};
    bool has_failed{};
};

auto is_shader_key(const std::string& key) -> bool
{
    return ex::is_format<gfx::shader>(fs::path(key).extension().generic_string());
}

/// The cooked file of the shader compiled to `relative` for the renderer of `renderer_extension`, relative to
/// the protocol's folder, as the compiled folder names it ("shaders/a.eshader.asset.wgsl").
auto get_cooked_relative_path(const fs::path& relative, const std::string& renderer_extension) -> fs::path
{
    auto cooked = relative;
    cooked.concat(renderer_extension);
    return cooked;
}

auto get_variant_key(const fs::path& relative) -> std::string
{
    return fs::make_path_key(relative.generic_string());
}

auto cook_shader(const shader_cook_plan& plan, const shader_job& job) -> shader_outcome
{
    shader_outcome outcome;
    const auto relative = get_compiled_relative_path(job.key);
    if(relative.empty())
    {
        return outcome;
    }
    for(const auto& renderer : plan.renderers)
    {
        const auto cooked = get_cooked_relative_path(relative, renderer);
        outcome.variants.push_back(get_variant_key(cooked));
        const auto output = job.protocol->variants / cooked;
        if(asset_compiler::is_shader_compiled(job.key, output))
        {
            continue;
        }
        // The compile writes its temporary files beside the output.
        fs::error_code ec;
        fs::create_directories(output.parent_path(), ec);
        if(asset_compiler::compile_shader(job.key, output))
        {
            ++outcome.compiled;
        }
        else
        {
            outcome.has_failed = true;
        }
    }
    return outcome;
}

void keep_variant(std::unordered_set<std::string>& kept, const std::string& variant)
{
    kept.insert(variant);
    kept.insert(variant + MANIFEST_SUFFIX);
}

/// A left-out shader's cooked files stay for a deploy that ships it, which checks them first.
void keep_left_out(const shader_cook_plan& plan, const shader_cook_protocol& protocol, std::unordered_set<std::string>& kept)
{
    for(const auto& key : protocol.left_out)
    {
        const auto relative = get_compiled_relative_path(key);
        if(relative.empty())
        {
            continue;
        }
        for(const auto& renderer : plan.renderers)
        {
            keep_variant(kept, get_variant_key(get_cooked_relative_path(relative, renderer)));
        }
    }
}

auto make_jobs(const shader_cook_plan& plan) -> std::vector<shader_job>
{
    std::vector<shader_job> jobs;
    for(const auto& protocol : plan.protocols)
    {
        for(const auto& key : protocol.shipped)
        {
            jobs.push_back({&protocol, key});
        }
    }
    return jobs;
}

auto get_source_size(const std::string& key) -> std::uintmax_t
{
    fs::error_code ec;
    const auto size = fs::file_size(fs::resolve_protocol(key), ec);
    return ec ? 0 : size;
}

auto cook_jobs(const shader_cook_plan& plan, const std::vector<shader_job>& jobs) -> std::vector<shader_outcome>
{
    std::vector<shader_outcome> outcomes(jobs.size());
    std::vector<std::uintmax_t> costs;
    costs.reserve(jobs.size());
    for(const auto& job : jobs)
    {
        costs.push_back(get_source_size(job.key));
    }
    // A compile can hold every thread it gets for seconds: never the pool the frame's loops wait for.
    const poolstl::background_scope background;
    poolstl::for_each_costliest_first_par_if(true,
                                             costs,
                                             [&](std::size_t index)
                                             {
                                                 outcomes[index] = cook_shader(plan, jobs[index]);
                                             });
    return outcomes;
}

/// Deletes what no shader of the protocol needs from its folder.
void sweep_protocol(const shader_cook_plan& plan,
                    const shader_cook_protocol& protocol,
                    std::unordered_set<std::string> kept,
                    shader_cook_report& report)
{
    keep_left_out(plan, protocol, kept);
    auto swept = sweep_folder(protocol.variants, kept);
    report.removed += swept.removed;
    report.undeletable.insert(report.undeletable.end(), swept.failed.begin(), swept.failed.end());
}

auto make_shader_cook_protocol(const fs::path& variants,
                               const asset_database::database_t& database,
                               const std::unordered_set<std::string>& left_out) -> shader_cook_protocol
{
    shader_cook_protocol protocol;
    protocol.variants = variants;
    for(const auto& [uid, meta] : database)
    {
        if(!is_shader_key(meta.location))
        {
            continue;
        }
        (left_out.contains(fs::make_path_key(meta.location)) ? protocol.left_out : protocol.shipped).push_back(meta.location);
    }
    return protocol;
}

auto describe_failures(const std::vector<std::string>& failed, const std::string& profile) -> std::string
{
    return fmt::format("{} shader(s) could not be compiled for profile {} (the log says why): {}",
                       failed.size(),
                       profile,
                       list_keys(failed, LISTED_FAILURES_MAX));
}
} // namespace

auto get_shader_variants(const deploy_settings& params) -> std::set<std::string>
{
    switch(params.deploy_target)
    {
        case deploy_platform::android:
            return {gfx::get_renderer_filename_extension(bgfx::RendererType::Vulkan)};
        case deploy_platform::web:
            return {gfx::get_renderer_filename_extension(bgfx::RendererType::WebGPU)};
        case deploy_platform::desktop:
        default:
            return {};
    }
}

auto get_cooked_shader_variants(const std::set<std::string>& shipped) -> std::set<std::string>
{
    const auto& compiled = gfx::get_renderer_platform_supported_filename_extensions();
    std::set<std::string> cooked;
    for(const auto& extension : shipped)
    {
        if(!compiled.contains(extension))
        {
            cooked.insert(extension);
        }
    }
    return cooked;
}

auto make_shader_cook_plan(const fs::path& project_root,
                           const deploy_settings& params,
                           const asset_database::database_t& engine_database,
                           const asset_database::database_t& app_database,
                           const std::unordered_set<std::string>& left_out) -> shader_cook_plan
{
    shader_cook_plan plan;
    plan.profile = params.profile;
    plan.renderers = get_cooked_shader_variants(get_shader_variants(params));
    if(!plan.has_work())
    {
        return plan;
    }
    plan.protocols.push_back(
        make_shader_cook_protocol(get_profile_shaders_folder(project_root, params.profile, ENGINE_PROTOCOL), engine_database, {}));
    plan.protocols.push_back(
        make_shader_cook_protocol(get_profile_shaders_folder(project_root, params.profile, CONTENT_PROTOCOL), app_database, left_out));
    return plan;
}

auto cook_shader_variants(const shader_cook_plan& plan) -> shader_cook_report
{
    shader_cook_report report;
    if(!plan.has_work())
    {
        return report;
    }
    const auto jobs = make_jobs(plan);
    const auto outcomes = cook_jobs(plan, jobs);
    std::vector<std::unordered_set<std::string>> kept(plan.protocols.size());
    for(std::size_t i = 0; i < outcomes.size(); ++i)
    {
        const auto& outcome = outcomes[i];
        auto& protocol_kept = kept[std::size_t(jobs[i].protocol - plan.protocols.data())];
        for(const auto& variant : outcome.variants)
        {
            keep_variant(protocol_kept, variant);
        }
        report.variants += outcome.variants.size();
        report.compiled += outcome.compiled;
        if(outcome.has_failed)
        {
            report.failed.push_back(jobs[i].key);
        }
    }
    for(std::size_t i = 0; i < plan.protocols.size(); ++i)
    {
        sweep_protocol(plan, plan.protocols[i], std::move(kept[i]), report);
    }
    return report;
}

void cook_shaders(const shader_cook_plan& plan)
{
    const auto report = cook_shader_variants(plan);
    APPLOG_INFO("Deploy: profile {} ships {} cooked shader file(s), {} compiled now; {} file(s) of its shader cache that "
                "nothing needs were deleted.",
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
        throw std::runtime_error(fmt::format("{} file(s) of profile {}'s shader cache that no shader needs could not be "
                                             "deleted, and the deploy would ship them, for example {}. Close the program "
                                             "that holds them, then deploy again.",
                                             report.undeletable.size(),
                                             plan.profile,
                                             report.undeletable.front().generic_string()));
    }
}

} // namespace unravel::deployment
