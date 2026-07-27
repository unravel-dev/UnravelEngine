#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <math/math.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace unravel
{

/**
 * @brief Camera-centered irradiance probe cascades for surface-cache final gather.
 *
 * Matches Lumen's split: surface-cache cards are world/volume persistent; the
 * final-gather probe grid follows the camera (world-snapped) so walking a large
 * scene never falls off a volume-center cascade.
 *
 * Sample pass is hybrid: direct card gather at the G-buffer hit (directional
 * bounce color) + probe cascades (stable low-frequency fill).
 */
class gi_probe_volume
{
public:
    static constexpr uint32_t NEAR_SIZE_X = 10;
    static constexpr uint32_t NEAR_SIZE_Y = 6;
    static constexpr uint32_t NEAR_SIZE_Z = 10;
    static constexpr uint32_t FAR_SIZE_X = 8;
    static constexpr uint32_t FAR_SIZE_Y = 4;
    static constexpr uint32_t FAR_SIZE_Z = 8;
    static constexpr uint32_t NEAR_COUNT = NEAR_SIZE_X * NEAR_SIZE_Y * NEAR_SIZE_Z;
    static constexpr uint32_t FAR_COUNT = FAR_SIZE_X * FAR_SIZE_Y * FAR_SIZE_Z;
    static constexpr uint32_t TOTAL_PROBES = NEAR_COUNT + FAR_COUNT;
    static constexpr uint32_t PROBE_GATHER_CARDS = 512;
    static constexpr int DEFAULT_PROBES_PER_FRAME = 160;

    struct settings
    {
        float near_extent = 40.0f;
        float far_extent = 120.0f;
        int probes_per_frame = DEFAULT_PROBES_PER_FRAME;
        float probe_history = 0.40f;
        float gather_distance = 160.0f;
        float gather_intensity = 2.5f;
        float cache_blend = 1.0f;
        bool seed_with_skylight = true;
        /// Soft-reset temporal blend for N frames after a large sun rotation.
        int sun_soft_reset_frames = 0;
    };

    struct update_params
    {
        const camera* cam{};
        gfx::texture::ptr atlas{};
        gfx::texture::ptr cards{};
        gfx::texture::ptr irradiance_sh{};
        gfx::texture::ptr opacity_volume{};
        uint32_t card_count = 0;
        float page_uv_size = 0.0f;
        float card_thickness = 0.35f;
        math::vec3 opacity_origin{0.0f, 0.0f, 0.0f};
        math::vec3 opacity_dims{64.0f, 64.0f, 64.0f};
        float opacity_voxel_size = 1.0f;
        bool opacity_enabled = false;
        settings cfg{};
    };

    /// Request lower probe history for a few frames (sun / lighting change).
    void request_soft_reset(int frames = 4);

    struct sample_params
    {
        const camera* cam{};
        gfx::frame_buffer::ptr g_buffer{};
        gfx::texture::ptr irradiance_sh{};
        /// Sampleable atlas (blit-resolved from UAV).
        gfx::texture::ptr atlas_srv{};
        gfx::texture::ptr cards{};
        uint32_t card_count = 0;
        float page_uv_size = 0.0f;
        float card_thickness = 0.35f;
        settings cfg{};
    };

    auto init(rtti::context& ctx) -> bool;
    void release();

    void update_world(gfx::render_view& rview, const update_params& params);

    auto sample_frame(gfx::render_view& rview, const sample_params& params) -> gfx::texture::ptr;

    auto get_probe_texture() const -> const gfx::texture::ptr& { return probe_tex_; }

private:
    void ensure_probe_texture();
    auto create_or_update_output(gfx::render_view& rview, const usize32_t& size) -> gfx::texture::ptr;

    struct update_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_cards, "s_cards", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_irradiance, "s_irradiance", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_probe_params0, "u_probe_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_params1, "u_probe_params1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_params2, "u_probe_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_origin_near, "u_probe_origin_near", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_spacing_near, "u_probe_spacing_near", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_origin_far, "u_probe_origin_far", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_spacing_far, "u_probe_spacing_far", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_opacity_params0, "u_opacity_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_opacity_params1, "u_opacity_params1", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr s_cards;
        gfx::program::uniform_ptr s_irradiance;
        gfx::program::uniform_ptr u_probe_params0;
        gfx::program::uniform_ptr u_probe_params1;
        gfx::program::uniform_ptr u_probe_params2;
        gfx::program::uniform_ptr u_probe_origin_near;
        gfx::program::uniform_ptr u_probe_spacing_near;
        gfx::program::uniform_ptr u_probe_origin_far;
        gfx::program::uniform_ptr u_probe_spacing_far;
        gfx::program::uniform_ptr u_opacity_params0;
        gfx::program::uniform_ptr u_opacity_params1;
        std::unique_ptr<gpu_program> program;
    } update_program_{};

    struct sample_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_gbuffer0, "s_gbuffer0", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gbuffer1, "s_gbuffer1", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gbuffer4, "s_gbuffer4", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_probes, "s_probes", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_irradiance, "s_irradiance", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_atlas, "s_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_cards, "s_cards", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_probe_params0, "u_probe_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_params1, "u_probe_params1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_params2, "u_probe_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_origin_near, "u_probe_origin_near", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_spacing_near, "u_probe_spacing_near", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_origin_far, "u_probe_origin_far", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_probe_spacing_far, "u_probe_spacing_far", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_camera_position, "u_camera_position", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr s_gbuffer0;
        gfx::program::uniform_ptr s_gbuffer1;
        gfx::program::uniform_ptr s_gbuffer4;
        gfx::program::uniform_ptr s_probes;
        gfx::program::uniform_ptr s_irradiance;
        gfx::program::uniform_ptr s_atlas;
        gfx::program::uniform_ptr s_cards;
        gfx::program::uniform_ptr u_probe_params0;
        gfx::program::uniform_ptr u_probe_params1;
        gfx::program::uniform_ptr u_probe_params2;
        gfx::program::uniform_ptr u_probe_origin_near;
        gfx::program::uniform_ptr u_probe_spacing_near;
        gfx::program::uniform_ptr u_probe_origin_far;
        gfx::program::uniform_ptr u_probe_spacing_far;
        gfx::program::uniform_ptr u_camera_position;
        std::unique_ptr<gpu_program> program;
    } sample_program_{};

    /// Snap cascades to camera; returns true if the grid rebased.
    auto upload_grid_uniforms(const math::vec3& camera_position, const settings& cfg) -> bool;

    gfx::texture::ptr probe_tex_{};
    math::vec3 near_origin_{0.0f, 0.0f, 0.0f};
    math::vec3 near_spacing_{1.0f, 1.0f, 1.0f};
    math::vec3 far_origin_{0.0f, 0.0f, 0.0f};
    math::vec3 far_spacing_{1.0f, 1.0f, 1.0f};
    math::vec3 camera_anchor_{0.0f, 0.0f, 0.0f};
    math::vec3 last_near_origin_{0.0f, 0.0f, 0.0f};
    math::vec3 last_far_origin_{0.0f, 0.0f, 0.0f};
    bool has_grid_anchor_ = false;
    uint32_t update_cursor_ = 0;
    uint32_t frame_index_ = 0;
    int rebase_boost_frames_ = 0;
    int sun_soft_reset_frames_ = 0;
};

} // namespace unravel
