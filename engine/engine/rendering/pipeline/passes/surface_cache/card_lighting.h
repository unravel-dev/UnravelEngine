#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <math/math.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace unravel
{
namespace shadow
{
class shadowmap_generator;
}

/**
 * @brief World-space card lighting + one card-space bounce into the radiance atlas.
 */
class card_lighting
{
public:
    static constexpr int MAX_POINT_LIGHTS = 8;

    struct point_light
    {
        math::vec3 position{0.0f, 0.0f, 0.0f};
        float range = 1.0f;
        math::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 0.0f;
    };

    struct light_env
    {
        math::vec3 sun_direction{0.0f, -1.0f, 0.0f};
        math::vec3 sun_color{1.0f, 1.0f, 1.0f};
        float sun_intensity = 0.0f;
        bool has_sun = false;
        const shadow::shadowmap_generator* sun_shadows = nullptr;
        std::array<point_light, MAX_POINT_LIGHTS> points{};
        int point_count = 0;
    };

    struct run_params
    {
        gfx::texture::ptr atlas{};
        gfx::texture::ptr material{};
        gfx::texture::ptr emissive{};
        gfx::texture::ptr cards{};
        gfx::texture::ptr irradiance_sh{};
        gfx::texture::ptr opacity_volume{};
        const std::vector<uint32_t>* dirty_upload_indices{};
        uint32_t uploaded_card_count = 0;
        float page_uv_size = 0.0f;
        float atlas_size = 2048.0f;
        float history = 0.5f;
        float max_gather_distance = 90.0f;
        float gather_intensity = 1.0f;
        float bounce_strength = 0.35f;
        float card_thickness = 0.35f;
        bool seed_with_skylight = true;
        bool use_priority_order = false;
        int pages_per_frame = 48;
        light_env lights{};
        math::vec3 opacity_origin{0.0f, 0.0f, 0.0f};
        math::vec3 opacity_dims{64.0f, 64.0f, 64.0f};
        float opacity_voxel_size = 1.0f;
        bool opacity_enabled = false;
    };

    auto init(rtti::context& ctx) -> bool;
    void release();

    /// Lights up to pages_per_frame cards from dirty_upload_indices. Returns GPU rows lit.
    auto update_dirty_pages(const run_params& params) -> std::vector<uint32_t>;

private:
    struct lighting_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_cards, "s_cards", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_irradiance, "s_irradiance", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_shadowMap0, "s_shadowMap0", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_shadowMap1, "s_shadowMap1", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_shadowMap2, "s_shadowMap2", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_shadowMap3, "s_shadowMap3", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_card_lit_params0, "u_card_lit_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_card_lit_params1, "u_card_lit_params1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_card_lit_params2, "u_card_lit_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sun_dir_intensity, "u_sun_dir_intensity", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sun_color, "u_sun_color", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_point_pos_range, "u_point_pos_range", gfx::uniform_type::Vec4, MAX_POINT_LIGHTS);
            cache_uniform(program.get(),
                          u_point_color_intensity,
                          "u_point_color_intensity",
                          gfx::uniform_type::Vec4,
                          MAX_POINT_LIGHTS);
            cache_uniform(program.get(), u_card_batch, "u_card_batch", gfx::uniform_type::Vec4, 8);
        }

        gfx::program::uniform_ptr s_cards;
        gfx::program::uniform_ptr s_irradiance;
        gfx::program::uniform_ptr s_shadowMap0;
        gfx::program::uniform_ptr s_shadowMap1;
        gfx::program::uniform_ptr s_shadowMap2;
        gfx::program::uniform_ptr s_shadowMap3;
        gfx::program::uniform_ptr u_card_lit_params0;
        gfx::program::uniform_ptr u_card_lit_params1;
        gfx::program::uniform_ptr u_card_lit_params2;
        gfx::program::uniform_ptr u_sun_dir_intensity;
        gfx::program::uniform_ptr u_sun_color;
        gfx::program::uniform_ptr u_point_pos_range;
        gfx::program::uniform_ptr u_point_color_intensity;
        gfx::program::uniform_ptr u_card_batch;
        std::unique_ptr<gpu_program> program;
    } lighting_program_{};

    struct bounce_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_cards, "s_cards", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_bounce_params0, "u_bounce_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_bounce_params1, "u_bounce_params1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_opacity_params0, "u_opacity_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_opacity_params1, "u_opacity_params1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_card_batch, "u_card_batch", gfx::uniform_type::Vec4, 8);
        }

        gfx::program::uniform_ptr s_cards;
        gfx::program::uniform_ptr u_bounce_params0;
        gfx::program::uniform_ptr u_bounce_params1;
        gfx::program::uniform_ptr u_opacity_params0;
        gfx::program::uniform_ptr u_opacity_params1;
        gfx::program::uniform_ptr u_card_batch;
        std::unique_ptr<gpu_program> program;
    } bounce_program_{};

    uint32_t light_cursor_ = 0;
};

} // namespace unravel
