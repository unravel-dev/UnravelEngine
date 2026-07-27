#include "opacity_clipmap.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace surface_cache
{

auto opacity_clipmap::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs_clear = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_opacity_clear.sc");
    auto cs_inject = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_opacity_inject.sc");
    clear_program_.program = std::make_unique<gpu_program>(cs_clear);
    clear_program_.cache_uniforms();
    inject_program_.program = std::make_unique<gpu_program>(cs_inject);
    inject_program_.cache_uniforms();
    ensure_volume();
    return clear_program_.program && clear_program_.program->is_valid() && inject_program_.program &&
           inject_program_.program->is_valid() && volume_ != nullptr;
}

void opacity_clipmap::release()
{
    clear_program_.program.reset();
    inject_program_.program.reset();
    volume_.reset();
}

void opacity_clipmap::ensure_volume()
{
    if(volume_)
    {
        return;
    }
    volume_ = std::make_shared<gfx::texture>(DIM,
                                             DIM,
                                             DIM,
                                             false,
                                             gfx::texture_format::RGBA16F,
                                             BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP |
                                                 BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP);
}

void opacity_clipmap::update_from_cards(const math::vec3& camera_position,
                                        float extent,
                                        const gfx::texture::ptr& cards,
                                        uint32_t card_count,
                                        float card_thickness)
{
    APP_SCOPE_PERF("Rendering/Surface Cache GI/Opacity Clipmap");
    ensure_volume();
    if(!volume_ || !clear_program_.program || !clear_program_.program->is_valid())
    {
        return;
    }
    extent_ = std::clamp(extent, 16.0f, 96.0f);
    voxel_size_ = extent_ * 2.0f / float(DIM);
    const float snap = voxel_size_;
    origin_ = math::vec3(std::floor((camera_position.x - extent_) / snap) * snap,
                         std::floor((camera_position.y - extent_ * 0.55f) / snap) * snap,
                         std::floor((camera_position.z - extent_) / snap) * snap);
    float clear_p0[4] = {0.0f, 0.0f, 0.0f, float(DIM)};
    {
        gfx::render_pass pass("SurfaceCacheGI/OpacityClear");
        pass.bind();
        clear_program_.program->begin();
        gfx::set_uniform(clear_program_.u_opacity_params0, clear_p0);
        gfx::set_image(0, volume_->native_handle(), 0, bgfx::Access::Write);
        const uint16_t g = uint16_t((DIM + 7) / 8);
        bgfx::dispatch(pass.id, clear_program_.program->native_handle(), g, g, g);
        clear_program_.program->end();
        gfx::discard();
    }
    inject_from_card_texture(cards, card_count, card_thickness);
}

void opacity_clipmap::inject_from_card_texture(const gfx::texture::ptr& cards,
                                               uint32_t card_count,
                                               float card_thickness)
{
    if(!volume_ || !cards || card_count == 0 || !inject_program_.program ||
       !inject_program_.program->is_valid())
    {
        return;
    }
    float p0[4] = {origin_.x, origin_.y, origin_.z, voxel_size_};
    float p1[4] = {float(DIM), float(DIM), float(DIM), float(card_count)};
    float p2[4] = {card_thickness, 0.0f, 0.0f, 0.0f};
    gfx::render_pass pass("SurfaceCacheGI/OpacityInject");
    pass.bind();
    inject_program_.program->begin();
    gfx::set_uniform(inject_program_.u_opacity_params0, p0);
    gfx::set_uniform(inject_program_.u_opacity_params1, p1);
    gfx::set_uniform(inject_program_.u_opacity_params2, p2);
    gfx::set_texture(inject_program_.s_cards, 0, cards);
    gfx::set_image(1, volume_->native_handle(), 0, bgfx::Access::ReadWrite);
    const uint16_t groups = uint16_t((card_count + 63) / 64);
    bgfx::dispatch(pass.id, inject_program_.program->native_handle(), groups, 1, 1);
    inject_program_.program->end();
    gfx::discard();
}

} // namespace surface_cache
} // namespace unravel
