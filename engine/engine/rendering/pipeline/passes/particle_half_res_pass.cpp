#include "particle_half_res_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{
namespace
{
constexpr std::uint32_t k_point_clamp = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT |
                                        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr std::uint32_t k_linear_clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
/// The half-size target's clear: no light, full transmittance (the reactive target takes 0).
constexpr std::uint32_t k_clear_transmittance = 0x000000ff;
/// Composite: scene x transmittance + particle light; the light buffer's alpha is kept.
constexpr std::uint64_t k_composite_blend = BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,
                                                                           BGFX_STATE_BLEND_SRC_ALPHA,
                                                                           BGFX_STATE_BLEND_ZERO,
                                                                           BGFX_STATE_BLEND_ONE);

auto get_half_size(const usize32_t& full_size) -> usize32_t
{
    return {(full_size.width + 1) / 2, (full_size.height + 1) / 2};
}
} // namespace

auto particle_half_res_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_depth = am.get_asset<gfx::shader>("engine:/data/shaders/particles/fs_particle_half_depth.sc");
    auto fs_composite = am.get_asset<gfx::shader>("engine:/data/shaders/particles/fs_particle_half_composite.sc");
    depth_program_.cache_uniforms();
    composite_program_.cache_uniforms();
    depth_program_.program = std::make_unique<gpu_program>(vs, fs_depth);
    composite_program_.program = std::make_unique<gpu_program>(vs, fs_composite);
    return depth_program_.program->is_valid() && composite_program_.program->is_valid();
}

auto particle_half_res_pass::acquire_targets(gfx::render_view& rview, const usize32_t& full_size, bool has_reactive)
    -> gfx::frame_buffer::ptr
{
    const auto size = get_half_size(full_size);
    const auto& color =
        rview.tex_acquire("PARTICLE_HALF", {.size = size, .format = bgfx::TextureFormat::RGBA16F, .flags = BGFX_TEXTURE_RT});
    const auto& depth = rview.tex_acquire("PARTICLE_HALF_DEPTH",
                                          {.size = size, .format = bgfx::TextureFormat::D32F, .flags = BGFX_TEXTURE_RT});
    if(!has_reactive)
    {
        auto& fbo = rview.fbo_acquire("PARTICLE_HALF_LIGHT");
        if(gfx::needs_recreate(fbo, {color, depth}))
        {
            fbo.reset();
            fbo = std::make_shared<gfx::frame_buffer>();
            fbo->populate({color, depth});
        }
        return fbo;
    }
    const auto& reactive = rview.tex_acquire("PARTICLE_HALF_REACTIVE",
                                             {.size = size, .format = bgfx::TextureFormat::R8, .flags = BGFX_TEXTURE_RT});
    auto& fbo = rview.fbo_acquire("PARTICLE_HALF");
    if(gfx::needs_recreate(fbo, {color, reactive, depth}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({color, reactive, depth});
    }
    return fbo;
}

auto particle_half_res_pass::acquire_composite_target(gfx::render_view& rview, const run_params& params)
    -> gfx::frame_buffer::ptr
{
    const auto& light = params.lbuffer_depth->get_texture(0);
    if(!params.reactive)
    {
        auto& fbo = rview.fbo_acquire("PARTICLE_HALF_COMPOSITE_LIGHT");
        if(gfx::needs_recreate(fbo, {light}))
        {
            fbo.reset();
            fbo = std::make_shared<gfx::frame_buffer>();
            fbo->populate({light});
        }
        return fbo;
    }
    auto& fbo = rview.fbo_acquire("PARTICLE_HALF_COMPOSITE");
    if(gfx::needs_recreate(fbo, {light, params.reactive}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({light, params.reactive});
    }
    return fbo;
}

void particle_half_res_pass::downsample_depth(const gfx::frame_buffer::ptr& targets, const gfx::texture::ptr& scene_depth)
{
    gfx::render_pass pass("Particles/Half Depth");
    pass.bind(targets.get());
    pass.clear(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, k_clear_transmittance, 1.0f, 0);
    if(!depth_program_.program->begin())
    {
        return;
    }
    gfx::set_texture(depth_program_.s_depth, 0, scene_depth, k_point_clamp);
    bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
    bgfx::submit(pass.id, depth_program_.program->native_handle());
    depth_program_.program->end();
}

void particle_half_res_pass::composite(const run_params& params,
                                       const gfx::frame_buffer::ptr& targets,
                                       const gfx::frame_buffer::ptr& composite_target)
{
    gfx::render_pass pass("Particles/Half Composite");
    pass.bind(composite_target.get());
    // The composite linearizes depths through the view's projection (u_proj).
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    if(!composite_program_.program->begin())
    {
        return;
    }
    gfx::set_texture(composite_program_.s_half_color, 0, targets->get_texture(0), k_linear_clamp);
    // Without a reactive target the composite's coverage output goes nowhere; the light stands in as its input.
    const bool has_reactive = static_cast<bool>(params.reactive);
    const auto& depth = targets->get_texture(has_reactive ? 2 : 1);
    gfx::set_texture(composite_program_.s_half_reactive, 1, targets->get_texture(has_reactive ? 1 : 0), k_linear_clamp);
    gfx::set_texture(composite_program_.s_half_depth, 2, depth, k_point_clamp);
    gfx::set_texture(composite_program_.s_depth, 3, params.lbuffer_depth->get_texture(1), k_point_clamp);
    const bool is_independent = has_reactive && params.has_independent_blend;
    bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | k_composite_blend |
                       (is_independent ? BGFX_STATE_BLEND_INDEPENDENT : 0),
                   is_independent ? ps_soa::reactive_blend_rgba : 0u);
    bgfx::submit(pass.id, composite_program_.program->native_handle());
    composite_program_.program->end();
}

auto particle_half_res_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    const bool is_ready = depth_program_.program && depth_program_.program->is_valid() && composite_program_.program &&
                          composite_program_.program->is_valid();
    if(!is_ready || !params.cam || !params.lbuffer_depth || !params.draw_particles)
    {
        return false;
    }
    APP_SCOPE_PERF("Rendering/Particle Pass/Half Resolution");
    const auto targets = acquire_targets(rview, params.lbuffer_depth->get_size(), static_cast<bool>(params.reactive));
    downsample_depth(targets, params.lbuffer_depth->get_texture(1));
    gfx::render_pass pass("Particles/Half Pass");
    pass.bind(targets.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    if(!params.draw_particles(pass.id))
    {
        return false;
    }
    composite(params, targets, acquire_composite_target(rview, params));
    return true;
}

} // namespace unravel
