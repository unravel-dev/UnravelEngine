#pragma once
#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_pass.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include "trace_resolution.h"

namespace unravel
{

/**
 * @brief Ground Truth Ambient Occlusion (Jimenez et al. 2016), a horizon-based screen-space
 * visibility integral with bent normals, in the shape of Intel's XeGTAO.
 *
 * Standalone replacement for the ASSAO pass, designed to sit under the GI: the GI resolves
 * occlusion at its probe lattice and coarser, so the default radius is short (contact
 * scale) and the output multiplies indirect diffuse and specular occlusion only - never
 * direct light. The bent normal steers the environment (SH) diffuse lookup toward the
 * unoccluded directions.
 *
 * Chain: depth prefilter (view-depth mips) -> main (visibility + bent normal) -> spatial
 * denoise -> temporal accumulation -> upsample to the full-resolution "GTAO" texture
 * (RGBA8: rgb = world bent normal * 0.5 + 0.5, a = visibility).
 */
class gtao_pass
{
public:
    struct settings
    {
        /// World-space radius of the occlusion search. 4 m adds the room-scale term the
        /// GI's probe lattice under-delivers (evaluated against 0.5 and 16 in the Bistro and
        /// the GI test suite); the search is capped on screen by max_screen_radius, so the
        /// value mostly matters for far geometry. Contact-only: 0.5-1 m.
        float radius = 4.0f;
        /// Portion of the radius over which an occluder's influence fades to zero. 0.3 keeps
        /// most of the radius at full weight without the pop a hard cutoff shows when an
        /// occluder crosses the boundary (XeGTAO's 0.615 is softer, 0.2 pops).
        float falloff_range = 0.3f;
        /// Power applied to the visibility (XeGTAO's final value power). 1 = ground truth
        /// for the depth buffer; 1.6 pairs with the wide radius (2.2 goes past dark into dirty).
        float final_power = 1.6f;
        /// Longest horizon search as a fraction of the AO target height. Bounds the cost and
        /// the sample spacing of a wide radius; the world radius shrinks with it so the
        /// falloff stays consistent. 0.25 = XeGTAO-like contact scale, 0.4 = wide.
        float max_screen_radius = 0.4f;
        /// Blend between no occlusion (0) and the full visibility (1) at the consumer.
        float intensity = 1.0f;
        /// Lets horizons relax behind thin occluders (railings, foliage) instead of treating
        /// them as walls. 0 = off, 0.7 = strong.
        float thin_occluder_compensation = 0.0f;
        /// 0 = low (1 slice x 2 steps), 1 = medium (2 x 2), 2 = high (3 x 3), 3 = ultra (9 x 3).
        int32_t quality_level = 3;
        /// Resolution the visibility is computed at; the result is upsampled edge-aware.
        /// Half is the default for the wide-radius look (a quarter of the cost, and the
        /// signal is low-frequency at that radius); Full for contact-scale reference.
        trace_resolution resolution = trace_resolution::half;
        /// Joint-bilateral denoise passes over the raw visibility (0 disables).
        int32_t denoise_passes = 2;
        bool enable_temporal = true;
        /// History weight of the temporal accumulation (0 = off, 0.95 = very smooth).
        float temporal_history = 0.9f;
        /// Relative view-depth tolerance for the temporal disocclusion test.
        float temporal_depth_threshold = 0.1f;
        /// How far the diffuse lookups (the GI probes and the environment SH) follow the bent
        /// normal instead of the geometric normal (0 = geometric normal).
        float bent_normal_strength = 1.0f;
        /// Multi-bounce approximation (Jimenez 2016): brightens the diffuse occlusion on light
        /// albedos by the interreflection a crevice gets back from its own walls.
        bool multi_bounce = false;
        /// Generate the receiver normal from the depth buffer (the geometric normal, edge-aware)
        /// instead of reading the G-buffer.
        bool generate_normals = false;
        /// Strength of the full-resolution normal-map detail term: the pixel's shading normal
        /// against the AO texel's bent cone, and the map's perturbation carried into the bent
        /// normal. Applied where the main pass did not see the shading normal at every pixel
        /// (generated normals, or reduced resolution).
        float normal_map_detail = 1.0f;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr g_buffer;
        /// This frame's velocity buffer (null = camera reprojection only).
        gfx::texture::ptr velocity;
        /// Previous frame's device depth (null = no temporal history this frame).
        gfx::texture::ptr prev_depth;
        const camera* cam{};
        settings config;
    };

    /// Slices per pixel for a quality level (see settings::quality_level).
    static auto get_slice_count(int32_t quality_level) -> uint32_t;
    /// Horizon samples per slice side for a quality level.
    static auto get_steps_per_slice(int32_t quality_level) -> uint32_t;

    auto init(rtti::context& ctx) -> bool;
    /// Runs the chain; returns the full-resolution "GTAO" texture (null when unavailable).
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr;
    /// Releases every GPU resource this pass owns in the render view.
    void release_resources(gfx::render_view& rview);

private:
    struct frame_context
    {
        usize32_t full_size{};
        usize32_t ao_size{};
        const camera* cam{};
        settings config{};
    };

    auto create_or_update_texture(gfx::render_view& rview,
                                  const std::string& name,
                                  const usize32_t& size,
                                  gfx::texture_format format,
                                  bool has_mips,
                                  uint64_t flags) -> gfx::texture::ptr;
    void set_common_uniforms(const frame_context& ctx) const;
    void run_prefilter(gfx::render_view& rview, const frame_context& ctx, const run_params& params,
                       const gfx::texture::ptr& depth_mips);
    auto run_main(gfx::render_view& rview, const frame_context& ctx, const run_params& params,
                  const gfx::texture::ptr& depth_mips) -> gfx::texture::ptr;
    auto run_denoise(gfx::render_view& rview, const frame_context& ctx, const run_params& params,
                     const gfx::texture::ptr& depth_mips, gfx::texture::ptr input) -> gfx::texture::ptr;
    auto run_temporal(gfx::render_view& rview, const frame_context& ctx, const run_params& params,
                      const gfx::texture::ptr& depth_mips, const gfx::texture::ptr& input) -> gfx::texture::ptr;
    auto run_upsample(gfx::render_view& rview, const frame_context& ctx, const run_params& params,
                      const gfx::texture::ptr& depth_mips, const gfx::texture::ptr& input) -> gfx::texture::ptr;
    void release_history(gfx::render_view& rview);

    /// Uniforms every program of the chain declares (gtao_common.sh).
    struct common_uniforms
    {
        gfx::program::uniform_ptr u_gtao_size;
        gfx::program::uniform_ptr u_gtao_full_size;
        gfx::program::uniform_ptr u_gtao_params0;
        gfx::program::uniform_ptr u_gtao_params1;
        gfx::program::uniform_ptr u_gtao_params2;
        gfx::program::uniform_ptr u_gtao_params3;
    };

    struct prefilter_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_gtao_depth;
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_gtao_depth, "s_gtao_depth", gfx::uniform_type::Sampler);
        }
        auto is_valid() const -> bool { return program && program->is_valid(); }
    } prefilter_program_;

    struct main_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_gtao_depth_mips;
        gfx::program::uniform_ptr s_gtao_normal;
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_gtao_depth_mips, "s_gtao_depth_mips", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_normal, "s_gtao_normal", gfx::uniform_type::Sampler);
        }
        auto is_valid() const -> bool { return program && program->is_valid(); }
    } main_program_;

    struct denoise_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_gtao_input;
        gfx::program::uniform_ptr s_gtao_depth_mips;
        gfx::program::uniform_ptr s_gtao_normal;
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_gtao_input, "s_gtao_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_depth_mips, "s_gtao_depth_mips", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_normal, "s_gtao_normal", gfx::uniform_type::Sampler);
        }
        auto is_valid() const -> bool { return program && program->is_valid(); }
    } denoise_program_;

    struct temporal_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_gtao_current;
        gfx::program::uniform_ptr s_gtao_history;
        gfx::program::uniform_ptr s_gtao_velocity;
        gfx::program::uniform_ptr s_gtao_depth_mips;
        gfx::program::uniform_ptr s_gtao_prev_depth;
        gfx::program::uniform_ptr u_gtao_prev_view_proj;
        gfx::program::uniform_ptr u_gtao_temporal;
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_gtao_current, "s_gtao_current", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_history, "s_gtao_history", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_velocity, "s_gtao_velocity", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_depth_mips, "s_gtao_depth_mips", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_prev_depth, "s_gtao_prev_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_gtao_prev_view_proj, "u_gtao_prev_view_proj", gfx::uniform_type::Mat4);
            cache_uniform(program.get(), u_gtao_temporal, "u_gtao_temporal", gfx::uniform_type::Vec4);
        }
        auto is_valid() const -> bool { return program && program->is_valid(); }
    } temporal_program_;

    struct upsample_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr s_gtao_input;
        gfx::program::uniform_ptr s_gtao_depth_mips;
        gfx::program::uniform_ptr s_gtao_depth;
        gfx::program::uniform_ptr s_gtao_normal;
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_gtao_input, "s_gtao_input", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_depth_mips, "s_gtao_depth_mips", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_depth, "s_gtao_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gtao_normal, "s_gtao_normal", gfx::uniform_type::Sampler);
        }
        auto is_valid() const -> bool { return program && program->is_valid(); }
    } upsample_program_;

    common_uniforms common_{};
    uniforms_cache common_cache_{};
};

} // namespace unravel
