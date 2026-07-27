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
 * @brief Camera-centered opacity clipmap for software short traces into the surface cache.
 *
 * Cards are stamped as thin shells into a 3D volume. Bounce/probe gathers march
 * this volume for visibility (Lumen-lite: visibility first, then sample atlas).
 */
class opacity_clipmap
{
public:
    static constexpr uint16_t DIM = 64;
    static constexpr float DEFAULT_EXTENT = 48.0f;

    auto init(rtti::context& ctx) -> bool;
    void release();

    /**
     * @brief Clear + stamp uploaded GPU cards into the opacity volume around the camera.
     */
    void update_from_cards(const math::vec3& camera_position,
                           float extent,
                           const gfx::texture::ptr& cards,
                           uint32_t card_count,
                           float card_thickness);

    auto volume() const -> const gfx::texture::ptr& { return volume_; }
    auto origin() const -> math::vec3 { return origin_; }
    auto voxel_size() const -> float { return voxel_size_; }
    auto dims() const -> math::vec3 { return math::vec3(float(DIM), float(DIM), float(DIM)); }
    auto is_valid() const -> bool { return volume_ != nullptr; }

private:
    void ensure_volume();
    void inject_from_card_texture(const gfx::texture::ptr& cards, uint32_t card_count, float card_thickness);

    struct inject_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_cards, "s_cards", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_opacity_params0, "u_opacity_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_opacity_params1, "u_opacity_params1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_opacity_params2, "u_opacity_params2", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr s_cards;
        gfx::program::uniform_ptr u_opacity_params0;
        gfx::program::uniform_ptr u_opacity_params1;
        gfx::program::uniform_ptr u_opacity_params2;
        std::unique_ptr<gpu_program> program;
    } inject_program_{};

    struct clear_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_opacity_params0, "u_opacity_params0", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr u_opacity_params0;
        std::unique_ptr<gpu_program> program;
    } clear_program_{};

    gfx::texture::ptr volume_{};
    math::vec3 origin_{0.0f, 0.0f, 0.0f};
    float voxel_size_ = 1.0f;
    float extent_ = DEFAULT_EXTENT;
};

} // namespace surface_cache
} // namespace unravel
