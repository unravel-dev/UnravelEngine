#include "shader_cook.h"
#include "files.h"
#include "keys.h"
#include "profile.h"
#include "profile_cache.h"

#include <asset_compiler/asset_compiler.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/meta/rendering/graph_material.hpp>
#include <graphics/graphics.h>

#include <algorithm>

namespace unravel::deployment
{
namespace
{
constexpr cook_wording SHADER_WORDING{"shader(s)", "cooked shader file(s)", "shader cache"};

/// One shader of one protocol to cook: a shader asset, or the shader of a graph material.
struct shader_job
{
    const shader_cook_protocol* protocol{};
    std::string key;
    bool is_graph_material{};
};

/// The cooked file of the shader compiled to `relative` for the renderer of `renderer_extension`, relative to
/// the protocol's folder, as the compiled folder names it ("shaders/a.eshader.asset.wgsl").
auto get_cooked_relative_path(const fs::path& relative, const std::string& renderer_extension) -> fs::path
{
    auto cooked = relative;
    cooked.concat(renderer_extension);
    return cooked;
}

/// Whether the editor's own compile of graph material @p key's shader for @p renderer, beside the compiled material, is
/// current: the deploy ships that one and cooks none.
auto is_compiled_beside_material(const std::string& key, const std::string& renderer) -> bool
{
    if(renderer != gfx::get_current_renderer_filename_extension())
    {
        return false;
    }
    const auto compiled = asset_reader::resolve_compiled_path(key).string();
    return asset_compiler::is_graph_material_shader_compiled(key, get_graph_material_shader_file(compiled, renderer));
}

/// Cooks one shader for each renderer: its files ship from the cache, one per renderer, named as the compiled shader's
/// or, for a graph material, as the compiled material's with the renderer's extension added. A graph material's shader
/// for the editor's renderer ships from beside the compiled material while that one is current.
auto cook_shader(const shader_cook_plan& plan, const shader_job& job) -> cook_outcome
{
    cook_outcome outcome;
    const auto relative = get_compiled_relative_path(job.key);
    if(relative.empty())
    {
        return outcome;
    }
    const auto& renderers = job.is_graph_material ? plan.graph_material_renderers : plan.renderers;
    const auto is_compiled = job.is_graph_material ? asset_compiler::is_graph_material_shader_compiled : asset_compiler::is_shader_compiled;
    const auto compile = job.is_graph_material ? asset_compiler::compile_graph_material_shader : asset_compiler::compile_shader;
    for(const auto& renderer : renderers)
    {
        if(job.is_graph_material && is_compiled_beside_material(job.key, renderer))
        {
            continue;
        }
        const auto cooked = get_cooked_relative_path(relative, renderer);
        outcome.files.push_back(get_cache_file_key(cooked));
        const auto output = job.protocol->variants / cooked;
        if(is_compiled(job.key, output))
        {
            continue;
        }
        // The compile writes its temporary files beside the output.
        fs::error_code ec;
        fs::create_directories(output.parent_path(), ec);
        if(compile(job.key, output))
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

/// Keeps the cooked files of @p keys for @p renderers.
void keep_cooked_files(const std::vector<std::string>& keys, const std::set<std::string>& renderers, std::unordered_set<std::string>& kept)
{
    for(const auto& key : keys)
    {
        const auto relative = get_compiled_relative_path(key);
        if(relative.empty())
        {
            continue;
        }
        for(const auto& renderer : renderers)
        {
            keep_cache_file(kept, get_cache_file_key(get_cooked_relative_path(relative, renderer)));
        }
    }
}

/// A left-out shader's cooked files stay for a deploy that ships it, which checks them first.
void keep_left_out(const shader_cook_plan& plan, const shader_cook_protocol& protocol, std::unordered_set<std::string>& kept)
{
    keep_cooked_files(protocol.shaders.left_out, plan.renderers, kept);
    keep_cooked_files(protocol.graph_materials.left_out, plan.graph_material_renderers, kept);
}

auto make_jobs(const shader_cook_plan& plan) -> std::vector<shader_job>
{
    std::vector<shader_job> jobs;
    for(const auto& protocol : plan.protocols)
    {
        for(const auto& key : protocol.shaders.shipped)
        {
            jobs.push_back({&protocol, key, false});
        }
        for(const auto& key : protocol.graph_materials.shipped)
        {
            jobs.push_back({&protocol, key, true});
        }
    }
    return jobs;
}

auto cook_jobs(const shader_cook_plan& plan, const std::vector<shader_job>& jobs) -> std::vector<cook_outcome>
{
    std::vector<std::string> keys;
    keys.reserve(jobs.size());
    for(const auto& job : jobs)
    {
        keys.push_back(job.key);
    }
    return cook_all(keys,
                    [&](std::size_t index)
                    {
                        return cook_shader(plan, jobs[index]);
                    });
}

/// Deletes what no shader of the protocol needs from its folder.
void sweep_protocol(const shader_cook_plan& plan,
                    const shader_cook_protocol& protocol,
                    std::unordered_set<std::string> kept,
                    cook_report& report)
{
    keep_left_out(plan, protocol, kept);
    sweep_cache_folder(protocol.variants, kept, report);
}

auto make_shader_cook_protocol(const fs::path& variants,
                               const asset_database::database_t& database,
                               const left_out_assets& left_out) -> shader_cook_protocol
{
    shader_cook_protocol protocol;
    protocol.variants = variants;
    protocol.shaders = split_assets(database, is_asset_key<gfx::shader>, left_out);
    protocol.graph_materials = split_assets(database, is_asset_key<material>, left_out);
    return protocol;
}

/// The renderer extensions a deploy with `params` ships shaders for (get_shader_variants), all of them named.
auto get_shipped_renderers(const deploy_settings& params) -> std::set<std::string>
{
    auto renderers = get_shader_variants(params);
    if(renderers.empty())
    {
        const auto& platform = gfx::get_renderer_platform_supported_filename_extensions();
        renderers.insert(platform.begin(), platform.end());
    }
    return renderers;
}

/// The renderer extensions of a web build: WGSL for WebGPU, GLSL ES for WebGL 2 (OpenGL ES).
auto get_web_shader_variants(web_graphics graphics) -> std::set<std::string>
{
    const std::string webgpu = gfx::get_renderer_filename_extension(bgfx::RendererType::WebGPU);
    const std::string webgl2 = gfx::get_renderer_filename_extension(bgfx::RendererType::OpenGLES);
    switch(graphics)
    {
        case web_graphics::webgpu_then_webgl2:
            return {webgpu, webgl2};
        case web_graphics::webgl2:
            return {webgl2};
        case web_graphics::webgpu:
        default:
            return {webgpu};
    }
}

} // namespace

auto get_shader_variants(const deploy_settings& params) -> std::set<std::string>
{
    switch(params.deploy_target)
    {
        case deploy_platform::android:
            return {gfx::get_renderer_filename_extension(bgfx::RendererType::Vulkan)};
        case deploy_platform::web:
            return get_web_shader_variants(params.web.graphics);
        case deploy_platform::desktop:
        default:
            break;
    }
    // A desktop build ships every renderer its platform compiles shaders for, unless its profile leaves some out.
    const auto platform = get_current_renderer_platform();
    std::set<std::string> variants;
    if(!is_desktop_graphics_restricted(params.desktop.graphics, platform))
    {
        return variants;
    }
    for(const auto renderer : get_desktop_renderers(params.desktop.graphics, platform))
    {
        variants.insert(gfx::get_renderer_filename_extension(preferred_renderer_to_gfx_type(renderer)));
    }
    return variants;
}

auto get_web_shader_sets(const deploy_settings& params) -> std::vector<shader_set_job>
{
    if(params.deploy_target != deploy_platform::web || !params.deploy_packed_assets ||
       params.web.graphics != web_graphics::webgpu_then_webgl2)
    {
        return {};
    }
    return {{ex::WEBGPU_GRAPHICS_NAME, gfx::get_renderer_filename_extension(bgfx::RendererType::WebGPU)},
            {ex::WEBGL2_GRAPHICS_NAME, gfx::get_renderer_filename_extension(bgfx::RendererType::OpenGLES)}};
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
                           const left_out_assets& left_out) -> shader_cook_plan
{
    shader_cook_plan plan;
    plan.profile = params.profile;
    plan.renderers = get_cooked_shader_variants(get_shader_variants(params));
    plan.protocols.push_back(
        make_shader_cook_protocol(get_profile_shaders_folder(project_root, params.profile, ENGINE_PROTOCOL), engine_database, {}));
    plan.protocols.push_back(
        make_shader_cook_protocol(get_profile_shaders_folder(project_root, params.profile, CONTENT_PROTOCOL), app_database, left_out));
    // Left-out graph materials count too: the sweep keeps their cooked files for the deploy that ships them.
    const bool has_graph_materials = std::any_of(plan.protocols.begin(),
                                                 plan.protocols.end(),
                                                 [](const shader_cook_protocol& protocol)
                                                 {
                                                     return !protocol.graph_materials.shipped.empty() ||
                                                            !protocol.graph_materials.left_out.empty();
                                                 });
    if(has_graph_materials)
    {
        plan.graph_material_renderers = get_shipped_renderers(params);
    }
    if(!plan.has_work())
    {
        plan.protocols.clear();
    }
    return plan;
}

auto cook_shader_variants(const shader_cook_plan& plan) -> cook_report
{
    cook_report report;
    if(!plan.has_work())
    {
        return report;
    }
    const auto jobs = make_jobs(plan);
    const auto outcomes = cook_jobs(plan, jobs);
    std::vector<std::unordered_set<std::string>> kept(plan.protocols.size());
    for(std::size_t i = 0; i < outcomes.size(); ++i)
    {
        add_outcome(jobs[i].key, outcomes[i], kept[std::size_t(jobs[i].protocol - plan.protocols.data())], report);
    }
    for(std::size_t i = 0; i < plan.protocols.size(); ++i)
    {
        sweep_protocol(plan, plan.protocols[i], std::move(kept[i]), report);
    }
    return report;
}

void cook_shaders(const shader_cook_plan& plan)
{
    report_cook_or_throw(plan.profile, cook_shader_variants(plan), SHADER_WORDING);
}

} // namespace unravel::deployment
