#include "depth_copy_pass.h"

#include <engine/assets/asset_manager.h>
#include <graphics/device_caps.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{

auto depth_copy_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.eshader");
    // At the source's size, each texel takes the depth of the source texel under it: a copy.
    auto fs_upscale_depth = am.get_asset<gfx::shader>("engine:/data/shaders/fs_upscale_depth.eshader");
    depth_program_.cache_uniforms();
    depth_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_upscale_depth);
    return depth_program_.program->is_valid();
}

auto depth_copy_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    if(!params.source)
    {
        return nullptr;
    }
    const bool has_depth_blit = gfx::get_device_caps().has_depth_blit;
    const std::uint64_t copy_flags = has_depth_blit ? BGFX_TEXTURE_BLIT_DST : BGFX_TEXTURE_RT;
    const auto& copy = rview.tex_acquire(params.name,
                                         {.size = params.source->get_size(),
                                          .format = bgfx::TextureFormat::Enum(params.source->info.format),
                                          .flags = copy_flags | params.sampler_flags});
    if(!has_depth_blit)
    {
        return draw_copy(rview, params, copy) ? copy : nullptr;
    }
    gfx::render_pass pass(params.pass_name);
    bgfx::blit(pass.id,
               bgfx::TextureRegion{.handle = copy->native_handle()},
               bgfx::TextureRegion{.handle = params.source->native_handle()});
    return copy;
}

auto depth_copy_pass::draw_copy(gfx::render_view& rview, const run_params& params, const gfx::texture::ptr& copy)
    -> bool
{
    if(!depth_program_.program->begin())
    {
        return false;
    }
    auto& fbo = rview.fbo_acquire(params.name);
    if(gfx::needs_recreate(fbo, {copy}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({copy});
    }
    gfx::render_pass pass(params.pass_name);
    pass.bind(fbo.get());
    gfx::set_texture(depth_program_.s_depth, 0, params.source, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
    bgfx::submit(pass.id, depth_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    depth_program_.program->end();
    return true;
}

} // namespace unravel
