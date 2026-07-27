#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/texture.h>
#include <math/math.h>

#include <cstdint>
#include <memory>

namespace rtti
{
class context;
}

namespace unravel
{
namespace surface_cache
{

/**
 * @brief Material + emissive atlases for surface-cache cards.
 *
 * Primary seed: mesh PBR constants written per page (world-persistent).
 * Optional G-buffer project refines albedo/emissive when a surface is visible.
 */
class card_capture
{
public:
    static constexpr uint16_t ATLAS_SIZE = 2048;
    static constexpr uint16_t PAGE_SIZE = 64;

    auto init(rtti::context& ctx) -> bool;
    void ensure_atlases();
    auto material_atlas() const -> const gfx::texture::ptr& { return material_; }
    auto emissive_atlas() const -> const gfx::texture::ptr& { return emissive_; }

    /**
     * @brief Fill one atlas page with constant albedo / emissive from mesh materials.
     */
    void fill_page(uint16_t page_x,
                   uint16_t page_y,
                   const math::vec3& albedo,
                   float albedo_coverage,
                   const math::vec3& emissive);

    /**
     * @brief Fill page with base_color * color_map (tiled). Needed for red maps with white tint.
     */
    void fill_page_textured(uint16_t page_x,
                            uint16_t page_y,
                            const math::vec3& albedo_tint,
                            float albedo_coverage,
                            const math::vec3& emissive,
                            const gfx::texture::ptr& color_map);

    void release();

private:
    struct fill_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_fill_params0, "u_fill_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_fill_albedo, "u_fill_albedo", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_fill_emissive, "u_fill_emissive", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr u_fill_params0;
        gfx::program::uniform_ptr u_fill_albedo;
        gfx::program::uniform_ptr u_fill_emissive;
        std::unique_ptr<gpu_program> program;
    } fill_program_{};

    struct fill_textured_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_color_map, "s_color_map", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_fill_params0, "u_fill_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_fill_albedo, "u_fill_albedo", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_fill_emissive, "u_fill_emissive", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr s_color_map;
        gfx::program::uniform_ptr u_fill_params0;
        gfx::program::uniform_ptr u_fill_albedo;
        gfx::program::uniform_ptr u_fill_emissive;
        std::unique_ptr<gpu_program> program;
    } fill_textured_program_{};

    gfx::texture::ptr material_{};
    gfx::texture::ptr emissive_{};
};

} // namespace surface_cache
} // namespace unravel
