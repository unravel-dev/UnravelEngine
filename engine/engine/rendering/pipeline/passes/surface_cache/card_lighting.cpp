#include "card_lighting.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/shadow.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace unravel
{

namespace
{
constexpr int k_batch_capacity = 64;

void pack_card_batch(const std::vector<uint32_t>& rows, uint32_t start, uint32_t count, float out_batch[8 * 4])
{
    std::memset(out_batch, 0, sizeof(float) * 8 * 4);
    for(uint32_t i = 0; i < count; ++i)
    {
        out_batch[i] = float(rows[start + i]);
    }
}
} // namespace

auto card_lighting::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs_lit = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_card_lighting.sc");
    auto cs_bounce = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_card_bounce.sc");
    lighting_program_.program = std::make_unique<gpu_program>(cs_lit);
    lighting_program_.cache_uniforms();
    bounce_program_.program = std::make_unique<gpu_program>(cs_bounce);
    bounce_program_.cache_uniforms();
    return lighting_program_.program && lighting_program_.program->is_valid() && bounce_program_.program &&
           bounce_program_.program->is_valid();
}

void card_lighting::release()
{
    lighting_program_.program.reset();
    bounce_program_.program.reset();
}

auto card_lighting::update_dirty_pages(const run_params& params) -> std::vector<uint32_t>
{
    APP_SCOPE_PERF("Rendering/Surface Cache GI/Card Lighting");
    std::vector<uint32_t> lit_rows;
    if(!params.atlas || !params.material || !params.cards || !params.dirty_upload_indices ||
       params.dirty_upload_indices->empty() || !lighting_program_.program ||
       !lighting_program_.program->is_valid())
    {
        return lit_rows;
    }
    const int budget = std::clamp(params.pages_per_frame, 1, k_batch_capacity);
    const auto& dirty = *params.dirty_upload_indices;
    const uint32_t dirty_count = static_cast<uint32_t>(dirty.size());
    if(dirty_count == 0)
    {
        return lit_rows;
    }
    lit_rows.reserve(size_t(budget));
    // Importance path: caller may pass priority-sorted rows (sun-facing first).
    // Fallback: round-robin cursor so every card still refreshes.
    if(params.use_priority_order)
    {
        for(uint32_t i = 0; i < dirty_count && int(lit_rows.size()) < budget; ++i)
        {
            const uint32_t row = dirty[i];
            if(row < params.uploaded_card_count)
            {
                lit_rows.push_back(row);
            }
        }
    }
    else
    {
        for(int n = 0; n < budget; ++n)
        {
            const uint32_t row = dirty[(light_cursor_ + uint32_t(n)) % dirty_count];
            if(row < params.uploaded_card_count)
            {
                lit_rows.push_back(row);
            }
        }
        light_cursor_ = (light_cursor_ + uint32_t(budget)) % dirty_count;
    }
    if(lit_rows.empty())
    {
        return lit_rows;
    }
    float sun_dir[4] = {params.lights.sun_direction.x,
                        params.lights.sun_direction.y,
                        params.lights.sun_direction.z,
                        params.lights.sun_intensity};
    float sun_col[4] = {params.lights.sun_color.x, params.lights.sun_color.y, params.lights.sun_color.z, 1.0f};
    float point_pos[MAX_POINT_LIGHTS * 4]{};
    float point_col[MAX_POINT_LIGHTS * 4]{};
    const int point_count = std::clamp(params.lights.point_count, 0, MAX_POINT_LIGHTS);
    for(int i = 0; i < point_count; ++i)
    {
        const auto& p = params.lights.points[size_t(i)];
        point_pos[i * 4 + 0] = p.position.x;
        point_pos[i * 4 + 1] = p.position.y;
        point_pos[i * 4 + 2] = p.position.z;
        point_pos[i * 4 + 3] = p.range;
        point_col[i * 4 + 0] = p.color.x;
        point_col[i * 4 + 1] = p.color.y;
        point_col[i * 4 + 2] = p.color.z;
        point_col[i * 4 + 3] = p.intensity;
    }
    auto emissive_tex = params.emissive ? params.emissive : default_textures::get().black_texture();
    const uint32_t batch_count = uint32_t(lit_rows.size());
    float card_batch[16 * 4]{};
    // Pack up to 64 rows across 16 vec4s (shader still reads u_card_batch[zi>>2] for zi<32).
    // For batches >32, dispatch in chunks of 32.
    const shadow::shadowmap_generator* sun_shadows = params.lights.sun_shadows;
    bool has_sun_shadow = false;
    if(sun_shadows && params.lights.has_sun)
    {
        const bgfx::TextureHandle cascade0 = sun_shadows->get_rt_texture(0);
        has_sun_shadow = bgfx::isValid(cascade0);
    }
    const bool apply_sun = params.lights.has_sun && has_sun_shadow;
    uint32_t dispatched = 0;
    while(dispatched < batch_count)
    {
        const uint32_t chunk = std::min(32u, batch_count - dispatched);
        pack_card_batch(lit_rows, dispatched, chunk, card_batch);
        float p0[4] = {0.0f, params.page_uv_size, params.atlas_size, float(point_count)};
        float p1[4] = {params.seed_with_skylight ? 1.0f : 0.0f,
                       apply_sun ? 1.0f : 0.0f,
                       1.0f,
                       std::clamp(params.history, 0.0f, 0.95f)};
        float p2[4] = {1.0f, float(chunk), has_sun_shadow ? 1.0f : 0.0f, 0.0f};
        {
            gfx::render_pass pass("SurfaceCacheGI/CardLighting");
            pass.bind();
            lighting_program_.program->begin();
            gfx::set_uniform(lighting_program_.u_card_lit_params0, p0);
            gfx::set_uniform(lighting_program_.u_card_lit_params1, p1);
            gfx::set_uniform(lighting_program_.u_card_lit_params2, p2);
            gfx::set_uniform(lighting_program_.u_sun_dir_intensity, sun_dir);
            gfx::set_uniform(lighting_program_.u_sun_color, sun_col);
            gfx::set_uniform(lighting_program_.u_card_batch, card_batch, 8);
            if(point_count > 0)
            {
                gfx::set_uniform(lighting_program_.u_point_pos_range, point_pos, uint16_t(point_count));
                gfx::set_uniform(lighting_program_.u_point_color_intensity, point_col, uint16_t(point_count));
            }
            gfx::set_texture(lighting_program_.s_cards, 0, params.cards);
            gfx::set_texture(lighting_program_.s_irradiance,
                             1,
                             params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture());
            if(has_sun_shadow)
            {
                sun_shadows->submit_uniforms(5);
            }
            else
            {
                auto black = default_textures::get().black_texture();
                gfx::set_texture(lighting_program_.s_shadowMap0, 5, black);
                gfx::set_texture(lighting_program_.s_shadowMap1, 6, black);
                gfx::set_texture(lighting_program_.s_shadowMap2, 7, black);
                gfx::set_texture(lighting_program_.s_shadowMap3, 8, black);
            }
            gfx::set_image(2, params.material->native_handle(), 0, bgfx::Access::Read);
            gfx::set_image(3, emissive_tex->native_handle(), 0, bgfx::Access::Read);
            gfx::set_image(4, params.atlas->native_handle(), 0, bgfx::Access::ReadWrite);
            bgfx::dispatch(pass.id, lighting_program_.program->native_handle(), 8, 8, uint16_t(chunk));
            lighting_program_.program->end();
            gfx::discard();
        }
        if(bounce_program_.program && bounce_program_.program->is_valid() && params.bounce_strength > 1e-4f)
        {
            float b0[4] = {float(chunk),
                           float(params.uploaded_card_count),
                           params.page_uv_size,
                           params.atlas_size};
            float b1[4] = {params.max_gather_distance, 1.0f, params.bounce_strength, params.card_thickness};
            gfx::render_pass pass("SurfaceCacheGI/CardBounce");
            pass.bind();
            bounce_program_.program->begin();
            gfx::set_uniform(bounce_program_.u_bounce_params0, b0);
            gfx::set_uniform(bounce_program_.u_bounce_params1, b1);
            gfx::set_uniform(bounce_program_.u_card_batch, card_batch, 8);
            gfx::set_texture(bounce_program_.s_cards, 0, params.cards);
            if(bounce_program_.u_opacity_params0)
            {
                float op0[4] = {params.opacity_origin.x,
                                params.opacity_origin.y,
                                params.opacity_origin.z,
                                params.opacity_voxel_size};
                float op1[4] = {params.opacity_dims.x,
                                params.opacity_dims.y,
                                params.opacity_dims.z,
                                params.opacity_enabled ? 1.0f : 0.0f};
                gfx::set_uniform(bounce_program_.u_opacity_params0, op0);
                gfx::set_uniform(bounce_program_.u_opacity_params1, op1);
            }
            gfx::set_image(1, params.material->native_handle(), 0, bgfx::Access::Read);
            gfx::set_image(2, params.atlas->native_handle(), 0, bgfx::Access::ReadWrite);
            if(params.opacity_volume)
            {
                gfx::set_image(3, params.opacity_volume->native_handle(), 0, bgfx::Access::Read);
            }
            bgfx::dispatch(pass.id, bounce_program_.program->native_handle(), 8, 8, uint16_t(chunk));
            bounce_program_.program->end();
            gfx::discard();
        }
        dispatched += chunk;
    }
    return lit_rows;
}

} // namespace unravel
