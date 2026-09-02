#include "scene_history_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/camera.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{

auto scene_history_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_snapshot = am.get_asset<gfx::shader>("engine:/data/shaders/gi/fs_gi_scene_snapshot.sc");
    program_.cache_uniforms();
    program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_snapshot);
    return is_valid();
}

auto scene_history_pass::run(gfx::render_view& rview,
                             const gfx::frame_buffer::ptr& source,
                             const gfx::texture::ptr& depth,
                             const camera& cam) -> gfx::texture::ptr
{
    if(!source || !depth || !is_valid())
    {
        return nullptr;
    }
    auto src_tex = source->get_texture();
    if(!src_tex)
    {
        return nullptr;
    }
    // Match the source format: RGBA16F (HDR pipeline, alpha carries the depth) or RGBA8 (LDR
    // fallback, alpha saturates and the readers treat the history as depth-less).
    const auto format = static_cast<gfx::texture_format>(src_tex->info.format);
    const auto size = src_tex->get_size();
    auto& prev = rview.tex_get_or_emplace("PREV_SCENE_HDR");
    if(gfx::needs_recreate(prev, size, format))
    {
        prev.reset();
        prev = std::make_shared<gfx::texture>(size.width,
                                              size.height,
                                              false,
                                              1,
                                              format,
                                              BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }
    // The target wraps the texture; re-populated whenever the texture itself was replaced,
    // not only on a size change, or it would keep drawing into the previous allocation.
    auto& fbo = rview.fbo_get_or_emplace("PREV_SCENE_HDR");
    if(gfx::needs_recreate(fbo, size) || fbo->get_texture() != prev)
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({prev});
    }
    gfx::render_pass pass("History/Prev Scene Color Pass");
    pass.bind(fbo.get());
    // The reconstruction reads u_invViewProj / u_viewProj: the TAA-unjittered pair, which is
    // the record the readers reproject with next frame (gi_resolve_pass binds the same).
    pass.set_view_proj(cam.get_view(), cam.get_projection_unjittered());
    if(program_.program->begin())
    {
        gfx::set_texture(program_.s_scene, 0, src_tex);
        gfx::set_texture(program_.s_depth, 1, depth);
        irect32_t rect(0, 0, int(size.width), int(size.height));
        gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
        auto topology = gfx::clip_quad(1.0f);
        gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::submit(pass.id, program_.program->native_handle());
        gfx::set_state(BGFX_STATE_DEFAULT);
        program_.program->end();
    }
    gfx::discard();
    return prev;
}

} // namespace unravel
