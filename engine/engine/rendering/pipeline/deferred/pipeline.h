#pragma once
#include "../pipeline.h"
#include "../passes/hiz_pass.h"
#include "../passes/ssil_pass.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/light.h>
#include <engine/rendering/batch_collector.h>

#include <graphics/utils/font/font_manager.h>
#include <graphics/utils/font/text_buffer_manager.h>
#include <graphics/utils/font/text_metrics.h>
#include <graphics/utils/bgfx_utils.h>
#include <graphics/utils/entry/entry.h>

namespace unravel
{
namespace rendering
{

class deferred : public pipeline
{
public:
    deferred();
    ~deferred();

    auto init(rtti::context& ctx) -> bool override;
    auto deinit(rtti::context& ctx) -> bool;

    auto run_pipeline(scene& scn, const camera& camera, gfx::render_view& rview, delta_t dt, const run_params& params, layer_mask render_mask = layer_mask{layer_reserved::everything_layer})
        -> gfx::frame_buffer::ptr override;

    void run_pipeline(const gfx::frame_buffer::ptr& output,
                      scene& scn,
                      const camera& camera,
                      gfx::render_view& rview,
                      delta_t dt,
                      const run_params& params,
                      layer_mask render_mask = layer_mask{layer_reserved::everything_layer}) override;
    void set_debug_pass(int pass) override;

    enum pipeline_steps : uint32_t
    {
        geometry_pass = 1 << 1,
        shadow_pass = 1 << 2,
        reflection_probe = 1 << 3,
        lighting = 1 << 4,
        atmospheric = 1 << 5,
        particles_pass = 1 << 6,

        full = geometry_pass | shadow_pass | reflection_probe | lighting | atmospheric | particles_pass,
        probe = lighting | atmospheric,
    };

    using pipeline_flags = uint32_t;

    void run_pipeline_impl(const gfx::frame_buffer::ptr& output,
                           scene& scn,
                           const camera& camera,
                           gfx::render_view& rview,
                           delta_t dt,
                           const run_params& params,
                           pipeline_flags pflags,
                           layer_mask render_mask = layer_mask{layer_reserved::everything_layer});

    void run_g_buffer_pass(const visibility_set_models_t& visibility_set,
                           const camera& camera,
                           gfx::render_view& rview,
                           delta_t dt);

    void run_assao_pass(const visibility_set_models_t& visibility_set,
                        const camera& camera,
                        gfx::render_view& rview,
                        delta_t dt,
                        const run_params& rparams);

    auto run_direct_lighting_pass(scene& scn, const camera& camera, gfx::render_view& rview, bool apply_shadows, delta_t dt)
        -> gfx::frame_buffer::ptr;

    auto run_indirect_lighting_pass(scene& scn, const camera& camera, gfx::render_view& rview)
        -> gfx::frame_buffer::ptr;

    void run_reflection_probe_pass(scene& scn, const camera& camera, gfx::render_view& rview, delta_t dt);

    auto run_atmospherics_pass(gfx::frame_buffer::ptr input,
                               scene& scn,
                               const camera& camera,
                               gfx::render_view& rview,
                               delta_t dt) -> gfx::frame_buffer::ptr;

    auto run_ssr_pass(const camera& camera, gfx::render_view& rview, const gfx::frame_buffer::ptr& output,
                       const run_params& rparams)
        -> gfx::frame_buffer::ptr;

    auto run_ssil_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams)
        -> gfx::texture::ptr;

    auto run_fxaa_pass(gfx::render_view& rview, const gfx::frame_buffer::ptr& input, const gfx::frame_buffer::ptr& output,
                       const run_params& rparams)
        -> gfx::frame_buffer::ptr;

    auto run_bloom_pass(gfx::render_view& rview,
                        const gfx::frame_buffer::ptr& input,
                        const run_params& rparams) -> gfx::frame_buffer::ptr;

    auto run_tonemapping_pass(gfx::render_view& rview, const gfx::frame_buffer::ptr& input, const gfx::frame_buffer::ptr& output,
                              const run_params& rparams)
        -> gfx::frame_buffer::ptr;
    void run_debug_visualization_pass(const camera& camera,
                                      gfx::render_view& rview,
                                      const gfx::frame_buffer::ptr& output);

    void build_reflections(scene& scn, const camera& camera, delta_t dt);

    void build_shadows(scene& scn, const camera& camera, delta_t dt, visibility_flags query = visibility_query::not_specified, layer_mask render_mask = layer_mask{layer_reserved::everything_layer});

    auto run_hiz_pass(const camera& camera, gfx::render_view& rview, delta_t dt) -> gfx::texture::ptr;

private:
    struct ref_probe_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_data0, "u_data0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_data1, "u_data1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_tex[0], "s_tex0", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[1], "s_tex1", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[2], "s_tex2", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[3], "s_tex3", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[4], "s_tex4", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex_cube, "s_tex_cube", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr u_data0;
        gfx::program::uniform_ptr u_data1;

        std::array<gfx::program::uniform_ptr, 5> s_tex;
        gfx::program::uniform_ptr s_tex_cube;

        std::unique_ptr<gpu_program> program;
    };

    struct box_ref_probe_program : ref_probe_program
    {
        void cache_uniforms()
        {
            ref_probe_program::cache_uniforms();

            cache_uniform(program.get(), u_data2, "u_data2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_inv_world, "u_inv_world", gfx::uniform_type::Mat4);
        }
        gfx::program::uniform_ptr u_inv_world;
        gfx::program::uniform_ptr u_data2;

    } box_ref_probe_program_;

    struct sphere_ref_probe_program : ref_probe_program
    {
    } sphere_ref_probe_program_;

    struct geom_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_tex_color, "s_tex_color", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex_normal, "s_tex_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex_roughness, "s_tex_roughness", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex_metalness, "s_tex_metalness", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex_ao, "s_tex_ao", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex_emissive, "s_tex_emissive", gfx::uniform_type::Sampler);

            cache_uniform(program.get(), u_base_color, "u_base_color", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_subsurface_color, "u_subsurface_color", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_emissive_color, "u_emissive_color", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_surface_data, "u_surface_data", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_tiling, "u_tiling", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_dither_threshold, "u_dither_threshold", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_surface_data2, "u_surface_data2", gfx::uniform_type::Vec4);

            cache_uniform(program.get(), u_camera_wpos, "u_camera_wpos", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_camera_clip_planes, "u_camera_clip_planes", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_lod_params, "u_lod_params", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr s_tex_color;
        gfx::program::uniform_ptr s_tex_normal;
        gfx::program::uniform_ptr s_tex_roughness;
        gfx::program::uniform_ptr s_tex_metalness;
        gfx::program::uniform_ptr s_tex_ao;
        gfx::program::uniform_ptr s_tex_emissive;

        gfx::program::uniform_ptr u_base_color;
        gfx::program::uniform_ptr u_subsurface_color;
        gfx::program::uniform_ptr u_emissive_color;
        gfx::program::uniform_ptr u_surface_data;
        gfx::program::uniform_ptr u_tiling;
        gfx::program::uniform_ptr u_dither_threshold;
        gfx::program::uniform_ptr u_surface_data2;

        gfx::program::uniform_ptr u_camera_wpos;
        gfx::program::uniform_ptr u_camera_clip_planes;
        gfx::program::uniform_ptr u_lod_params;

        std::unique_ptr<gpu_program> program;
    };

    geom_program geom_program_;
    geom_program geom_program_skinned_;
    geom_program geom_program_instanced_;

    struct color_lighting : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_light_position, "u_light_position", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_light_direction, "u_light_direction", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_light_data, "u_light_data", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_light_color_intensity, "u_light_color_intensity", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_camera_position, "u_camera_position", gfx::uniform_type::Vec4);

            cache_uniform(program.get(), s_tex[0], "s_tex0", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[1], "s_tex1", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[2], "s_tex2", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[3], "s_tex3", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[4], "s_tex4", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[5], "s_tex5", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[6], "s_tex6", gfx::uniform_type::Sampler);
        }
        gfx::program::uniform_ptr u_light_position;
        gfx::program::uniform_ptr u_light_direction;
        gfx::program::uniform_ptr u_light_data;
        gfx::program::uniform_ptr u_light_color_intensity;
        gfx::program::uniform_ptr u_camera_position;
        std::array<gfx::program::uniform_ptr, 7> s_tex;

        std::shared_ptr<gpu_program> program;
    };

    struct irradiance_compute_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_mode, "u_mode", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_irradiance_tint_intensity, "u_irradiance_tint_intensity", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sun_direction, "u_sun_direction", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sky_luminance, "u_sky_luminance", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sun_luminance, "u_sun_luminance", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sky_luminance_xyz, "u_sky_luminance_xyz", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_exposition, "u_exposition", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_perez_coeff, "u_perez_coeff", gfx::uniform_type::Vec4, 5);
            cache_uniform(program.get(), s_env, "s_env", gfx::uniform_type::Sampler);
        }
        gfx::program::uniform_ptr u_mode;
        gfx::program::uniform_ptr u_irradiance_tint_intensity;
        gfx::program::uniform_ptr u_sun_direction;
        gfx::program::uniform_ptr u_sky_luminance;
        gfx::program::uniform_ptr u_sun_luminance;
        gfx::program::uniform_ptr u_sky_luminance_xyz;
        gfx::program::uniform_ptr u_exposition;
        gfx::program::uniform_ptr u_perez_coeff;
        gfx::program::uniform_ptr s_env;

        std::unique_ptr<gpu_program> program;
    } irradiance_compute_program_;

    struct indirect_lighting_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_light_data, "u_light_data", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_camera_position, "u_camera_position", gfx::uniform_type::Vec4);

            cache_uniform(program.get(), s_tex[0], "s_tex0", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[1], "s_tex1", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[2], "s_tex2", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[3], "s_tex3", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[4], "s_tex4", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[5], "s_tex5", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[6], "s_tex6", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_irradiance, "s_irradiance", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_ssil, "s_ssil", gfx::uniform_type::Sampler);
        }
        gfx::program::uniform_ptr u_light_data;
        gfx::program::uniform_ptr u_camera_position;
        std::array<gfx::program::uniform_ptr, 7> s_tex;
        gfx::program::uniform_ptr s_irradiance;
        gfx::program::uniform_ptr s_ssil;

        std::unique_ptr<gpu_program> program;

    } indirect_lighting_program_;

    struct debug_visualization_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_params, "u_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_tex[0], "s_tex0", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[1], "s_tex1", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[2], "s_tex2", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[3], "s_tex3", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[4], "s_tex4", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[5], "s_tex5", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[6], "s_tex6", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_tex[7], "s_tex7", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr u_params;
        std::array<gfx::program::uniform_ptr, 8> s_tex;

        std::unique_ptr<gpu_program> program;

    } debug_visualization_program_;

    struct irradiance_pass_result
    {
        gfx::texture::ptr irradiance_tex;
        math::vec3 global_color = {1.0f, 1.0f, 1.0f};
        float global_intensity = 0.0f;
    };
    auto run_irradiance_pass(scene& scn, gfx::render_view& rview) -> irradiance_pass_result;

    auto get_light_program(const light& l) const -> const color_lighting&;
    auto get_light_program_no_shadows(const light& l) const -> const color_lighting&;
    void submit_pbr_material(geom_program& program, const pbr_material& mat);
    void submit_batched_geometry(gfx::render_pass& pass, const camera& camera);

    color_lighting color_lighting_[uint8_t(light_type::count)][uint8_t(sm_depth::count)][uint8_t(sm_impl::count)];
    color_lighting color_lighting_no_shadow_[uint8_t(light_type::count)];

    asset_handle<gfx::texture> ibl_brdf_lut_;

    // Static mesh batching system
    batch_collector batch_collector_;

public:

private:

    ssil_pass ssil_pass_;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    int debug_pass_{-1};

};

} // namespace rendering
} // namespace unravel
