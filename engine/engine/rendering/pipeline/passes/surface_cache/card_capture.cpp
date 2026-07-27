#include "card_capture.h"

#include <engine/assets/asset_manager.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{
namespace surface_cache
{

auto card_capture::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_card_page_fill.sc");
    auto cs_tex = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_card_page_fill_textured.sc");
    fill_program_.program = std::make_unique<gpu_program>(cs);
    fill_program_.cache_uniforms();
    fill_textured_program_.program = std::make_unique<gpu_program>(cs_tex);
    fill_textured_program_.cache_uniforms();
    return fill_program_.program && fill_program_.program->is_valid();
}

void card_capture::ensure_atlases()
{
    if(!material_)
    {
        material_ = std::make_shared<gfx::texture>(ATLAS_SIZE,
                                                   ATLAS_SIZE,
                                                   false,
                                                   1,
                                                   gfx::texture_format::RGBA16F,
                                                   BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT |
                                                       BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }
    if(!emissive_)
    {
        emissive_ = std::make_shared<gfx::texture>(ATLAS_SIZE,
                                                   ATLAS_SIZE,
                                                   false,
                                                   1,
                                                   gfx::texture_format::RGBA16F,
                                                   BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT |
                                                       BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }
}

void card_capture::fill_page(uint16_t page_x,
                             uint16_t page_y,
                             const math::vec3& albedo,
                             float albedo_coverage,
                             const math::vec3& emissive)
{
    ensure_atlases();
    if(!fill_program_.program || !fill_program_.program->is_valid() || !material_ || !emissive_)
    {
        return;
    }
    float p0[4] = {float(page_x * PAGE_SIZE), float(page_y * PAGE_SIZE), float(PAGE_SIZE), 0.0f};
    float alb[4] = {albedo.x, albedo.y, albedo.z, albedo_coverage};
    float emi[4] = {emissive.x, emissive.y, emissive.z, 1.0f};
    gfx::render_pass pass("SurfaceCacheGI/PageFill");
    pass.bind();
    fill_program_.program->begin();
    gfx::set_uniform(fill_program_.u_fill_params0, p0);
    gfx::set_uniform(fill_program_.u_fill_albedo, alb);
    gfx::set_uniform(fill_program_.u_fill_emissive, emi);
    gfx::set_image(0, material_->native_handle(), 0, bgfx::Access::Write);
    gfx::set_image(1, emissive_->native_handle(), 0, bgfx::Access::Write);
    const uint16_t groups = uint16_t((PAGE_SIZE + 7) / 8);
    bgfx::dispatch(pass.id, fill_program_.program->native_handle(), groups, groups, 1);
    fill_program_.program->end();
    gfx::discard();
}

void card_capture::fill_page_textured(uint16_t page_x,
                                      uint16_t page_y,
                                      const math::vec3& albedo_tint,
                                      float albedo_coverage,
                                      const math::vec3& emissive,
                                      const gfx::texture::ptr& color_map)
{
    ensure_atlases();
    if(!color_map || !fill_textured_program_.program || !fill_textured_program_.program->is_valid() ||
       !material_ || !emissive_)
    {
        fill_page(page_x, page_y, albedo_tint, albedo_coverage, emissive);
        return;
    }
    float p0[4] = {float(page_x * PAGE_SIZE), float(page_y * PAGE_SIZE), float(PAGE_SIZE), 0.0f};
    float alb[4] = {albedo_tint.x, albedo_tint.y, albedo_tint.z, albedo_coverage};
    float emi[4] = {emissive.x, emissive.y, emissive.z, 1.0f};
    gfx::render_pass pass("SurfaceCacheGI/PageFillTextured");
    pass.bind();
    fill_textured_program_.program->begin();
    gfx::set_uniform(fill_textured_program_.u_fill_params0, p0);
    gfx::set_uniform(fill_textured_program_.u_fill_albedo, alb);
    gfx::set_uniform(fill_textured_program_.u_fill_emissive, emi);
    gfx::set_texture(fill_textured_program_.s_color_map, 0, color_map);
    gfx::set_image(1, material_->native_handle(), 0, bgfx::Access::Write);
    gfx::set_image(2, emissive_->native_handle(), 0, bgfx::Access::Write);
    const uint16_t groups = uint16_t((PAGE_SIZE + 7) / 8);
    bgfx::dispatch(pass.id, fill_textured_program_.program->native_handle(), groups, groups, 1);
    fill_textured_program_.program->end();
    gfx::discard();
}

void card_capture::release()
{
    material_.reset();
    emissive_.reset();
    fill_program_.program.reset();
    fill_textured_program_.program.reset();
}

} // namespace surface_cache
} // namespace unravel
