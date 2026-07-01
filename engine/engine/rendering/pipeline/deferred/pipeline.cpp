#include "pipeline.h"
#include "glm/ext/scalar_integer.hpp"
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/perez_luminance.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <engine/rendering/default_textures.h>
#include <engine/engine.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/renderer.h>

#include <graphics/index_buffer.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <algorithm>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <graphics/vertex_buffer.h>

namespace unravel
{
namespace rendering
{

namespace
{


auto get_default_format() -> gfx::texture_format
{
    return gfx::texture_format::RGBA8;
}

auto get_default_hdr_format() -> gfx::texture_format
{
    return gfx::texture_format::RGBA16F;
}

auto get_default_depth_format() -> gfx::texture_format
{
    return gfx::texture_format::D32F;
}

// Cubemap face captures keep HDR buffer setup from create_run_params, but write the
// linear lighting result directly to the cubemap face before post-processing.
void strip_post_effects_for_reflection_probe_capture(pipeline::run_params& params)
{
    params.fill_assao_params = {};
    params.fill_auto_exposure_params = {};
    params.fill_bloom_params = {};
    params.fill_taa_params = {};
    params.apply_taa_params = {};
    params.fill_ssr_params = {};
    params.fill_ssil_params = {};
    params.fill_hdr_params = {};
}

void clear_reflection_probe_face(const gfx::frame_buffer::ptr& fbo)
{
    if(!fbo)
    {
        return;
    }

    gfx::render_pass pass("Reflection Probe/Clear Face");
    pass.bind(fbo.get());
    pass.set_view_proj(nullptr, nullptr);
    pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);
}

// run_pipeline_impl takes const camera& for reads; jitter only touches projection jitter state.
void apply_pipeline_taa_jitter_to_camera(const camera& view_camera,
                                         const usize32_t& viewport_size,
                                         const pipeline::run_params& params)
{
    camera& cam = const_cast<camera&>(view_camera);
    if(params.apply_taa_params)
    {
        params.apply_taa_params(cam, viewport_size);
    }
    else
    {
        cam.set_aa_data(viewport_size, 0u, 1u);
    }
}

auto create_or_resize_d_buffer(gfx::render_view& rview,
                               const usize32_t& viewport_size,
                               const pipeline::run_params& params) -> const gfx::texture::ptr&
{
    auto& depth = rview.tex_get_or_emplace("DEPTH");
    if(gfx::needs_recreate(depth, viewport_size))
    {
        depth.reset();
        depth = std::make_shared<gfx::texture>(viewport_size.width,
                                               viewport_size.height,
                                               false,
                                               1,
                                               gfx::texture_format::D32F,
                                               BGFX_TEXTURE_RT);
    }

    return depth;
}

auto create_or_resize_hiz_buffer(gfx::render_view& rview, const usize32_t& viewport_size) -> const gfx::texture::ptr&
{
    auto& hiz = rview.tex_get_or_emplace("HIZBUFFER");
    if(gfx::needs_recreate(hiz, viewport_size))
    {
        // Create Hi-Z texture with compute shader support
        hiz.reset();
        hiz = std::make_shared<gfx::texture>(viewport_size.width,
                                             viewport_size.height,
                                             true,                            // generate mips
                                             1,                               // one layer
                                             gfx::texture_format::R32F,       // R32F for better precision
                                             BGFX_TEXTURE_RT |                // Render target
                                                 BGFX_TEXTURE_COMPUTE_WRITE | // Allow compute writes
                                                 BGFX_SAMPLER_MIN_POINT |     // Point sampling for min filter
                                                 BGFX_SAMPLER_MAG_POINT |     // Point sampling for mag filter
                                                 BGFX_SAMPLER_MIP_POINT |     // Point sampling for mips
                                                 BGFX_SAMPLER_U_CLAMP |       // Clamp UVs
                                                 BGFX_SAMPLER_V_CLAMP         // Clamp UVs
        );
    }

    return hiz;
}

auto create_or_resize_g_buffer(gfx::render_view& rview,
                               const usize32_t& viewport_size,
                               const pipeline::run_params& params) -> const gfx::frame_buffer::ptr&
{
    auto& depth = create_or_resize_d_buffer(rview, viewport_size, params);

    auto& fbo = rview.fbo_get_or_emplace("GBUFFER");
    if(gfx::needs_recreate(fbo, viewport_size))
    {
        auto format = params.fill_hdr_params ? get_default_hdr_format() : get_default_format();

        auto tex0 = std::make_shared<gfx::texture>(viewport_size.width,
                                                   viewport_size.height,
                                                   false,
                                                   1,
                                                   get_default_format(),
                                                   BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT);

        auto tex1 = std::make_shared<gfx::texture>(viewport_size.width,
                                                   viewport_size.height,
                                                   false,
                                                   1,
                                                   format,
                                                   BGFX_TEXTURE_RT);

        auto tex2 = std::make_shared<gfx::texture>(viewport_size.width,
                                                   viewport_size.height,
                                                   false,
                                                   1,
                                                   format,
                                                   BGFX_TEXTURE_RT);

        auto tex3 = std::make_shared<gfx::texture>(viewport_size.width,
                                                   viewport_size.height,
                                                   false,
                                                   1,
                                                   get_default_format(),
                                                   BGFX_TEXTURE_RT);

        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex0, tex1, tex2, tex3, depth});
    }

    return fbo;
}

auto create_or_resize_l_buffer(gfx::render_view& rview,
                               const usize32_t& viewport_size,
                               const pipeline::run_params& params) -> const gfx::frame_buffer::ptr&
{
    auto& depth = create_or_resize_d_buffer(rview, viewport_size, params);

    auto& fbo = rview.fbo_get_or_emplace("LBUFFER");
    if(gfx::needs_recreate(fbo, viewport_size))
    {
        auto format = params.fill_hdr_params ? get_default_hdr_format() : get_default_format();

        auto tex = std::make_shared<gfx::texture>(viewport_size.width,
                                                  viewport_size.height,
                                                  false,
                                                  1,
                                                  format,
                                                  BGFX_TEXTURE_RT);
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
        
        auto tex_unshadowed = std::make_shared<gfx::texture>(viewport_size.width,
                                                              viewport_size.height,
                                                              false,
                                                              1,
                                                              format,
                                                              BGFX_TEXTURE_RT);
      

        auto& fbo_depth = rview.fbo_get_or_emplace("LBUFFER_DEPTH");
        fbo_depth.reset();
        fbo_depth = std::make_shared<gfx::frame_buffer>();
        fbo_depth->populate({tex, depth});
    }

    return fbo;
}

auto create_or_resize_r_buffer(gfx::render_view& rview,
                               const usize32_t& viewport_size,
                               const pipeline::run_params& params) -> const gfx::frame_buffer::ptr&
{
    auto& fbo = rview.fbo_get_or_emplace("RBUFFER");
    if(gfx::needs_recreate(fbo, viewport_size))
    {
        auto format = params.fill_hdr_params ? get_default_hdr_format() : get_default_format();

        auto tex = std::make_shared<gfx::texture>(viewport_size.width,
                                                  viewport_size.height,
                                                  false,
                                                  1,
                                                  format,
                                                  BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE);

        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}
auto create_or_resize_o_buffer(gfx::render_view& rview,
                               const usize32_t& viewport_size,
                               const pipeline::run_params& params) -> const gfx::frame_buffer::ptr&
{
    auto& depth = create_or_resize_d_buffer(rview, viewport_size, params);

    auto& tex = rview.tex_get_or_emplace("OBUFFER");
    if(gfx::needs_recreate(tex, viewport_size))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(viewport_size.width,
                                            viewport_size.height,
                                            false,
                                            1,
                                            get_default_format(),
                                            BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT);

    }
    {
        auto& fbo = rview.fbo_get_or_emplace("OBUFFER_DEPTH");
        if(gfx::needs_recreate(fbo, viewport_size))
        {
            fbo.reset();
            fbo = std::make_shared<gfx::frame_buffer>();
            fbo->populate({tex, depth});
        }
    }

    auto& fbo = rview.fbo_get_or_emplace("OBUFFER");
    if(gfx::needs_recreate(fbo, viewport_size))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex});
    }

    return fbo;
}

auto create_or_get_irradiance_texture(gfx::render_view& rview) -> const gfx::texture::ptr&
{
    auto& tex = rview.tex_get_or_emplace("IRRADIANCE_SH");
    if(gfx::needs_recreate(tex, {9, 1}))
    {
        // Match auto-exposure: RGBA32F + COMPUTE_WRITE uses glTexStorage2D on GL (immutable
        // storage). Initial data must go through update_texture_2d (glTexSubImage2D), not
        // the texture ctor _mem path. BGFX_TEXTURE_RT keeps the GL texture sampleable after
        // compute image writes (same pattern as Hi-Z and other compute targets).
        // Layout: 9x1, one texel per SH coefficient, rgb = channels R,G,B (a unused).
        tex.reset();
        tex = std::make_shared<gfx::texture>(9,
                                             1,
                                             false,
                                             1,
                                             gfx::texture_format::RGBA32F,
                                             BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE |
                                                 BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                                 BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

        float initial_coeffs[9 * 4] = {};
        const gfx::memory_view* initial_pixels = gfx::copy(initial_coeffs, sizeof(initial_coeffs));
        gfx::update_texture_2d(tex->native_handle(), 0, 0, 0, 0, 9, 1, initial_pixels);
    }
    return tex;
}

auto should_rebuild_shadows(const shadow::shadow_map_models_t& visibility_set,
                            const light& light,
                            const math::bbox& light_bounds,
                            const math::transform& light_transform) -> bool
{
    APP_SCOPE_PERF("Rendering/Shadow Rebuild Check Per Light");

    auto light_world_bounds = math::bbox::mul(light_bounds, light_transform);
    for(const auto& element : visibility_set)
    {
        const auto& entity = element.entity;
        const auto& lod_data = element.lod_data;
        const auto& transform_comp_ref = entity.get<transform_component>();
        const auto& model_comp_ref = entity.get<model_component>();
        const auto& model_world_bounds = model_comp_ref.get_world_bounds();

        bool result = light_world_bounds.intersect(model_world_bounds);

        if(result)
            return true;
    }

    return false;
}

auto reflection_screen_stack_enabled(const pipeline::run_params& params) -> bool
{
    return params.run_type == pipeline::pipeline_run_type::camera &&
           (params.pflags & deferred::pipeline_steps::reflection_probe) != 0u;
}
} // namespace

auto deferred::get_light_program(const light& l) const -> const color_lighting&
{
    return color_lighting_[uint8_t(l.type)][uint8_t(l.shadow_params.depth)][uint8_t(l.shadow_params.type)];
}

auto deferred::get_light_program_no_shadows(const light& l) const -> const color_lighting&
{
    return color_lighting_no_shadow_[uint8_t(l.type)];
}

void deferred::submit_pbr_material(geom_program& program, const pbr_material& mat)
{
    const auto& color_map = mat.get_color_map();
    const auto& normal_map = mat.get_normal_map();
    const auto& roughness_map = mat.get_roughness_map();
    const auto& metalness_map = mat.get_metalness_map();
    const auto& ao_map = mat.get_ao_map();
    const auto& emissive_map = mat.get_emissive_map();

    const auto& albedo = color_map ? color_map : mat.default_color_map();
    const auto& normal = normal_map ? normal_map : mat.default_normal_map();
    const auto& roughness = roughness_map ? roughness_map : mat.default_color_map();
    const auto& metalness = metalness_map ? metalness_map : mat.default_color_map();
    const auto& ao = ao_map ? ao_map : mat.default_color_map();
    const auto& emissive = emissive_map ? emissive_map : mat.default_color_map();

    // Resolve and pin every texture up front. asset_handle::get() returns a
    // shared_ptr<texture>; holding our own copies for the duration of the
    // submit keeps the texture objects alive even if the asset watcher
    // thread invalidates/reloads these handles mid-frame (which happens
    // intermittently right after a scene is opened). Without this, a texture
    // resolved here could be released between the individual set_texture
    // calls, leaving a dangling pointer for native_handle().
    const auto albedo_tex = albedo.get();
    const auto normal_tex = normal.get();
    const auto roughness_tex = roughness.get();
    const auto metalness_tex = metalness.get();
    const auto ao_tex = ao.get();
    const auto emissive_tex = emissive.get();

    const auto& base_color = mat.get_base_color();
    const auto& subsurface_color = mat.get_subsurface_color();
    const auto& emissive_color = mat.get_emissive_color();
    const float emissive_intensity = mat.get_emissive_intensity();
    const auto& surface_data = mat.get_surface_data();
    const auto& tiling = mat.get_tiling();
    const auto& dither_threshold = mat.get_dither_threshold();
    const auto surface_data2 = mat.get_surface_data2();

    gfx::set_texture(program.s_tex_color, 0, albedo_tex);
    gfx::set_texture(program.s_tex_normal, 1, normal_tex);
    gfx::set_texture(program.s_tex_roughness, 2, roughness_tex);
    gfx::set_texture(program.s_tex_metalness, 3, metalness_tex);
    gfx::set_texture(program.s_tex_ao, 4, ao_tex);
    gfx::set_texture(program.s_tex_emissive, 5, emissive_tex);

    math::color premultiplied_emissive{
        emissive_color.value.r * emissive_intensity,
        emissive_color.value.g * emissive_intensity,
        emissive_color.value.b * emissive_intensity,
        emissive_color.value.a};

    gfx::set_uniform(program.u_base_color, base_color);
    gfx::set_uniform(program.u_subsurface_color, subsurface_color);
    gfx::set_uniform(program.u_emissive_color, premultiplied_emissive);
    gfx::set_uniform(program.u_surface_data, surface_data);
    gfx::set_uniform(program.u_tiling, tiling);
    gfx::set_uniform(program.u_dither_threshold, dither_threshold);
    gfx::set_uniform(program.u_surface_data2, surface_data2);

    auto state = mat.get_render_states(true, true, true);

    gfx::set_state(state);
}

void deferred::build_reflections(scene& scn, const camera& camera, delta_t dt)
{
    APP_SCOPE_PERF("Rendering/Reflection Generation Pass");

    scn.registry->view<transform_component, reflection_probe_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& reflection_probe_comp, auto&& active)
        {
            if(reflection_probe_comp.already_generated())
            {
                return;
            }

            // reflection_probe_comp.set_generation_frame(gfx::get_render_frame());

            const auto& world_transform = transform_comp.get_transform_global();

            const auto& bounds = reflection_probe_comp.get_bounds();
            if(!camera.test_obb(bounds, world_transform))
            {
                return;
            }

            const auto& probe = reflection_probe_comp.get_probe();

            auto handle = scn.create_handle(e);
            {
                gfx::render_pass::push_scope("Build Reflections");

                if(reflection_probe_comp.is_bake_cycle_unstarted())
                {
                    for(std::uint32_t face = 0; face < 6; ++face)
                    {
                        clear_reflection_probe_face(reflection_probe_comp.get_cubemap_fbo(face));
                    }
                }

                bool any_face_dirty = false;
                // iterate trough each cube face
                for(std::uint32_t face = 0; face < 6; ++face)
                {
                    if(reflection_probe_comp.already_generated(face))
                    {
                        continue;
                    }

                    reflection_probe_comp.set_generation_frame(face, gfx::get_render_frame());

                    auto camera = camera::get_face_camera(face, world_transform);
                    camera.set_far_clip(probe.get_face_extents(face, world_transform));
                    auto& rview = reflection_probe_comp.get_render_view(face);
                    const auto& cubemap_fbo = reflection_probe_comp.get_cubemap_fbo(face);

                    camera.set_viewport_size(usize32_t(cubemap_fbo->get_size()));

                    bool not_environment = probe.method != reflect_method::environment;

                    pipeline_flags pflags = 0;
                    visibility_flags vflags = visibility_query::is_static;

                    if(not_environment)
                    {
                        pflags |= pipeline_steps::geometry_pass;
                    }

                    if(reflection_probe_comp.get_capture_sky())
                    {
                        pflags |= pipeline_steps::atmospheric;
                    }

                    if(reflection_probe_comp.get_capture_shadows())
                    {
                        pflags |= pipeline_steps::shadow_pass;
                        vflags |= visibility_query::is_shadow_caster;
                    }

                    auto params = create_run_params(handle, &scn, &camera);
                    params.run_type = pipeline_run_type::reflection_probe_capture;
                    params.vflags = vflags;
                    params.pflags = pflags;
                    strip_post_effects_for_reflection_probe_capture(params);

                    //if(!reflection_probe_comp.get_capture_sky())
                    {
                        clear_reflection_probe_face(cubemap_fbo);
                    }

                    run_pipeline_impl(cubemap_fbo, scn, camera, rview, dt, params);
                    any_face_dirty = true;
                }

                if(any_face_dirty && reflection_probe_comp.is_bake_complete())
                {
                    auto env_cube = reflection_probe_comp.get_cubemap();
                    auto env_cube_prefiltered = reflection_probe_comp.get_cubemap_prefiltered();
                    prefilter_pass::run_params prefilter_params;

                    prefilter_params.apply_prefilter = reflection_probe_comp.get_apply_prefilter();

                    for(std::uint32_t face = 0; face < 6; ++face)
                    {
                        const auto& cubemap_fbo = reflection_probe_comp.get_cubemap_fbo(face);
                        prefilter_params.input_faces[face] = cubemap_fbo->get_texture();
                    }

                    prefilter_params.output_cube = env_cube;
                    prefilter_params.output_cube_prefiltered = env_cube_prefiltered;

                    prefilter_pass_.run(reflection_probe_comp.get_render_view(0), prefilter_params);
                }

                gfx::render_pass::pop_scope();
            }
        });
}

void deferred::build_shadows(scene& scn, const camera& camera, delta_t dt, visibility_flags query, layer_mask render_mask)
{
    APP_SCOPE_PERF("Rendering/Shadow Generation Pass");

    query |= visibility_query::is_dirty | visibility_query::is_shadow_caster;

    bool queried = false;
    shadow::shadow_map_models_t dirty_models;

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();

    scn.registry->view<transform_component, light_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp)
        {
            const auto& light = light_comp.get_light();

            bool is_directional = light.type == light_type::directional;
            bool has_render_mask = render_mask.mask != layer_reserved::everything_layer;
            bool camera_dependant = is_directional || has_render_mask;
            bool is_active = scn.registry->all_of<active_component>(e);

            auto& generator = light_comp.get_shadowmap_generator();
            generator.enable_adaptive_shadows(true);
            generator.set_altitude_scale_factor(0.4f);
            if(!camera_dependant && generator.already_updated())
            {
                return;
            }

            APP_SCOPE_PERF("Rendering/Shadow Generation Pass Per Light");

            auto world_transform = transform_comp.get_transform_global();
            world_transform.reset_scale();
            const auto& light_direction = world_transform.z_unit_axis();

            generator.update(camera, light, world_transform, is_active);

            if(!is_active)
            {
                return;
            }

            const auto& bounds = light_comp.get_bounds_precise(light_direction);
            if(!camera.test_obb(bounds, world_transform))
            {
                return;
            }

            if(!light.casts_shadows)
            {
                return;
            }

            if(!queried)
            {
                gather_visible_models(scn, nullptr, query, render_mask, dt, [&](entt::handle entity, const lod_data& lod_data)
                {
                    dirty_models.emplace_back(shadow::shadow_visibility_data{entity, lod_data});
                });
                queried = true;
            }

            bool should_rebuild = should_rebuild_shadows(dirty_models, light, bounds, world_transform);

            // If shadows shouldn't be rebuilt - continue.
            if(!should_rebuild)
                return;

            APP_SCOPE_PERF("Rendering/Shadow Generation Pass Per Light After Cull");

            generator.generate_shadowmaps(dirty_models, camera, &stats_);
        });
}

auto deferred::run_pipeline(scene& scn,
                            const camera& camera,
                            gfx::render_view& rview,
                            delta_t dt,
                            const run_params& params,
                            layer_mask render_mask) -> gfx::frame_buffer::ptr
{
    const auto& viewport_size = camera.get_viewport_size();
    const auto& obuffer = create_or_resize_o_buffer(rview, viewport_size, params);

    run_pipeline_impl(obuffer, scn, camera, rview, dt, params, render_mask);

    return obuffer;
}

void deferred::run_pipeline(const gfx::frame_buffer::ptr& output,
                            scene& scn,
                            const camera& camera,
                            gfx::render_view& rview,
                            delta_t dt,
                            const run_params& params,
                            layer_mask render_mask)
{
    auto obuffer = run_pipeline(scn, camera, rview, dt, params, render_mask);

    blit_pass::run_params pass_params;
    pass_params.input = obuffer;
    pass_params.output = output;
    blit_pass_.run(rview, pass_params);
}

void deferred::set_debug_pass(int pass)
{
    debug_pass_ = pass;
}

void deferred::run_pipeline_impl(const gfx::frame_buffer::ptr& output,
                                 scene& scn,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 delta_t dt,
                                 const run_params& params,
                                 layer_mask render_mask)
{
    APP_SCOPE_PERF("Rendering/Run Pipeline");

    const pipeline_flags stages = params.pflags;
    const bool is_camera_run = params.run_type == pipeline_run_type::camera;
    const bool is_probe_capture = params.run_type == pipeline_run_type::reflection_probe_capture;

    if(is_camera_run)
    {
        stats_ = {};
    }

    visibility_set_models_t visibility_set;
    gfx::frame_buffer::ptr target = nullptr;

    const bool build_shadowmaps = (stages & pipeline_steps::shadow_pass) != 0u;
    const bool build_reflection_probes = (stages & pipeline_steps::reflection_probe) != 0u;

    if(build_reflection_probes)
    {
        build_reflections(scn, camera, dt);
    }

    if(build_shadowmaps)
    {
        build_shadows(scn, camera, dt, visibility_query::not_specified, render_mask);
    }

    const auto& viewport_size = camera.get_viewport_size();
    create_or_resize_d_buffer(rview, viewport_size, params);
    create_or_resize_g_buffer(rview, viewport_size, params);
    create_or_resize_l_buffer(rview, viewport_size, params);
    create_or_resize_r_buffer(rview, viewport_size, params);

    apply_pipeline_taa_jitter_to_camera(camera, viewport_size, params);

    if(stages & pipeline_steps::geometry_pass)
    {
        gather_visible_models(scn, &camera, params.vflags, render_mask, dt, [&](entt::handle entity, const lod_data& lod_data)
        {
            visibility_set.emplace_back(visibility_data{entity, lod_data});
        });
    }

    run_g_buffer_pass(visibility_set, camera, rview, dt);

    run_assao_pass(camera, rview, dt, params);

    run_reflection_probe_pass(scn, camera, rview, build_reflection_probes, dt);

    const bool hiz_active = run_hiz_pass(camera, rview, params, viewport_size, dt);

    // SSR samples the previous visible output before this frame overwrites it, so traced
    // reflections use the same resolved scene color that was presented last frame.
    run_ssr_pass(camera, rview, output, params);

    // Direct lighting starts the current frame LBUFFER after SSR has consumed its history source.
    target = run_direct_lighting_pass(scn, camera, rview, build_shadowmaps, dt);

    // SSIL pass
    run_ssil_pass(camera, rview, params);

    // Indirect lighting after SSIL so it can use the result.
    target = run_indirect_lighting_pass(scn, camera, rview, build_reflection_probes, dt);

    if(stages & pipeline_steps::atmospheric)
    {
        target = run_atmospherics_pass(target, scn, camera, rview, dt);
    }

    if(stages & pipeline_steps::particles_pass)
    {
        run_particle_pass(scn, camera, rview, target);
    }

    if(is_probe_capture)
    {
        blit_pass::run_params pass_params;
        pass_params.input = target;
        pass_params.output = output;
        blit_pass_.run(rview, pass_params);
        batch_collector_.clear();
        return;
    }

    target = run_taa_pass(camera, rview, target, output, params);

    run_auto_exposure_pass(rview, target, params, dt);

    target = run_bloom_pass(rview, target, params);

    target = run_tonemapping_pass(rview, target, output, params);

    run_fxaa_pass(rview, target, output, params);

    if(is_camera_run)
    {
        run_ui_pass(scn, camera, rview, output);

        if(debug_pass_ >= 0)
        {
            run_debug_visualization_pass(camera, rview, output);
        }
    }

    // After all passes that sample PREV_DEPTH (must follow Hi-Z / SSIL path).
    if(hiz_active)
    {
        snapshot_prev_depth(rview, viewport_size);
    }

    // Clear batch collector for this frame
    batch_collector_.clear();

}

void deferred::snapshot_prev_depth(gfx::render_view& rview, const usize32_t& viewport_size)
{
    auto depth_src = rview.fbo_get("GBUFFER")->get_texture(4);
    auto& prev_depth = rview.tex_get_or_emplace("PREV_DEPTH");
    if(gfx::needs_recreate(prev_depth, viewport_size))
    {
        prev_depth.reset();
        prev_depth = std::make_shared<gfx::texture>(viewport_size.width,
                                                    viewport_size.height,
                                                    false,
                                                    1,
                                                    gfx::texture_format::D32F,
                                                    BGFX_TEXTURE_BLIT_DST);
    }
    gfx::render_pass blit_pass("History/Prev Depth Blit Pass");
    gfx::blit(blit_pass.id,
              prev_depth->native_handle(), 0, 0,
              depth_src->native_handle(), 0, 0);
}

void deferred::run_g_buffer_pass(const visibility_set_models_t& visibility_set,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 delta_t dt)
{
    APP_SCOPE_PERF("Rendering/G-Buffer Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& viewport_size = camera.get_viewport_size();

    const auto& gbuffer = rview.fbo_get("GBUFFER");

    gfx::render_pass pass("G-Buffer/Pass");
    pass.clear();
    pass.set_view_proj(view, proj);
    pass.bind(gbuffer.get());

    // Clear batch collector for this frame
    batch_collector_.clear();

    const auto& view_frustum = camera.get_frustum();

    for(const auto& element : visibility_set)
    {
        const auto& entity = element.entity;
        const auto& lod_data = element.lod_data;
        const auto& transform_comp = entity.get<transform_component>();
        auto& model_comp = entity.get<model_component>();

        const auto& model = model_comp.get_model();
        if(!model.is_valid())
        {
            continue;
        }

        const auto& world_transform = transform_comp.get_transform_global();
        const auto clip_planes = math::vec2(camera.get_near_clip(), camera.get_far_clip());

        const auto current_time = lod_data.current_time;
        const auto current_lod_index = lod_data.current_lod_index;
        const auto target_lod_index = lod_data.target_lod_index;

        // Optimized single-component LOD transition parameters
        // Positive: current LOD fading out (1.0 → 0.0)
        // Negative: target LOD fading in (0.0 → -1.0)
        const float transition_progress = lod_data.transition_time > 0.0f 
            ? current_time / lod_data.transition_time 
            : 1.0f;
        
        const auto params = math::vec3{1.0f - transition_progress, 0.0f, 0.0f};      // Current LOD: positive, fading out
        const auto params_inv = math::vec3{-transition_progress, 0.0f, 0.0f};        // Target LOD: negative, fading in

        const auto& submesh_transforms = model_comp.get_submesh_transforms();
        const auto& bone_transforms = model_comp.get_bone_transforms();
        const auto& skinning_matrices = model_comp.get_skinning_transforms();

        auto camera_pos = camera.get_position();


        model::submit_callbacks callbacks;
        callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
        {
            if(submit_params.skinned)
            {
                stats_.drawn_skinned_models++;
            }
            else
            {
                stats_.drawn_models++;
            }
            geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;
            prog.program->begin();
            gfx::set_uniform(prog.u_camera_wpos, camera_pos);
            gfx::set_uniform(prog.u_camera_clip_planes, clip_planes);
        };
        callbacks.setup_params_per_instance = [&](const model::submit_callbacks::params& submit_params)
        {
            geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;

            gfx::set_uniform(prog.u_lod_params, params);
        };
        callbacks.setup_params_per_submesh =
            [&](const model::submit_callbacks::params& submit_params, const material& mat)
        {
            if(submit_params.skinned)
            {
                stats_.drawn_skinned_submeshes++;
            }
            else
            {
                stats_.drawn_static_submeshes++;
            }
            geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;

            bool submitted = mat.submit(prog.program.get());
            if(!submitted)
            {
                if(mat.is<pbr_material>())
                {
                    const auto& pbr = static_cast<const pbr_material&>(mat);
                    submit_pbr_material(prog, pbr);
                }
            }

            gfx::submit(pass.id, prog.program->native_handle(), 0, submit_params.preserve_state);
        };
        callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
        {
            geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;

            prog.program->end();
        };

        model_comp.set_last_render_frame(gfx::get_render_frame());

        // Check if this model can be batched (static mesh, no skinning)
        const bool is_skinned = !skinning_matrices.empty();
        const bool can_batch = batch_collector::is_static_mesh_batching_enabled() && !is_skinned;

        if (can_batch)
        {
            // Collect this model for batching with appropriate transforms            
            model.submit_for_batching(batch_collector_, world_transform, submesh_transforms, current_lod_index, params.x, &view_frustum);
            stats_.drawn_models++;
            // Handle LOD transitions for batched models
            if(math::epsilonNotEqual(current_time, 0.0f, math::epsilon<float>()))
            {
                model.submit_for_batching(batch_collector_, world_transform, submesh_transforms, target_lod_index, params_inv.x, &view_frustum);
                stats_.drawn_models++;
            }
        }
        else
        {
            // Render individually (skinned meshes, complex transforms, etc.)
            model.submit(world_transform,
                         submesh_transforms,
                         bone_transforms,
                         skinning_matrices,
                         current_lod_index,
                         callbacks,
                         &view_frustum);
            if(math::epsilonNotEqual(current_time, 0.0f, math::epsilon<float>()))
            {
                callbacks.setup_params_per_instance = [&](const model::submit_callbacks::params& submit_params)
                {
                    geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;

                    gfx::set_uniform(prog.u_lod_params, params_inv);
                };

                model.submit(world_transform,
                             submesh_transforms,
                             bone_transforms,
                             skinning_matrices,
                             target_lod_index,
                             callbacks,
                             &view_frustum);
            }
        }
    }

    // Submit all collected batches
    if (batch_collector::is_static_mesh_batching_enabled())
    {
        submit_batched_geometry(pass, camera);
    }
    gfx::discard();
}

void deferred::submit_batched_geometry(gfx::render_pass& pass, const camera& camera)
{
    APP_SCOPE_PERF("Rendering/Submit Batched Geometry");

    // Prepare batches for rendering
    submit_context context;
    context.view_id = pass.id;
    context.camera_position = camera.get_position();
    context.enable_distance_sorting = false; // Opaque objects don't need distance sorting
    context.max_instances_per_batch = 1024;  // BGFX instance limit
    context.enable_profiling = true;

    batch_collector_.prepare_batches(context);

    const auto& prepared_batches = batch_collector_.get_prepared_batches();
    if (prepared_batches.empty())
    {
        return;
    }

    // Set up common uniforms
    const auto camera_pos = camera.get_position();
    const auto clip_planes = math::vec2(camera.get_near_clip(), camera.get_far_clip());

    geom_program_instanced_.program->begin();
    gfx::set_uniform(geom_program_instanced_.u_camera_wpos, camera_pos);
    gfx::set_uniform(geom_program_instanced_.u_camera_clip_planes, clip_planes);

    // Submit each batch
    for (const auto* batch : prepared_batches)
    {
        if (!batch->is_valid() || batch->instances.empty())
        {
            continue;
        }

        const auto instance_count = static_cast<uint32_t>(batch->instances.size());
        stats_.drawn_static_submeshes += instance_count;

        const auto mesh_ptr = batch->key.mesh_ptr;
        const auto material_ptr = batch->key.material_ptr;
        const auto lod_index = batch->key.lod_index;
        const auto submesh_index = batch->key.submesh_index;

        if (!mesh_ptr || !material_ptr)
        {
            continue;
        }

        const auto submesh = mesh_ptr->get_submesh(submesh_index, lod_index);
        if(!submesh)
        {
            continue;
        }

        // Create instance buffer from batch instances
        const auto instance_data_size = static_cast<uint16_t>(instance_vertex_data::packed_size());
        
        // Allocate instance buffer
        bgfx::InstanceDataBuffer instance_buffer;
        bgfx::allocInstanceDataBuffer(&instance_buffer, instance_count, instance_data_size);
        if (!instance_buffer.data)
        {
            continue; // Skip this batch if allocation failed
        }

        
        // Submit the mesh with instancing
        // Bind vertex and index buffers for the specific submesh
        mesh_ptr->bind_render_buffers_for_submesh(submesh, lod_index);
        
        // Pack instance data into buffer
        auto* buffer_data = reinterpret_cast<instance_vertex_data*>(instance_buffer.data);
        for (size_t i = 0; i < batch->instances.size(); ++i)
        {
            buffer_data[i] = instance_vertex_data(batch->instances[i]);
        }

        // Set instance data buffer
        bgfx::setInstanceDataBuffer(&instance_buffer);

        // Submit material properties
        bool material_submitted = material_ptr->submit(geom_program_instanced_.program.get());
        if (!material_submitted)
        {
            if (material_ptr->is<pbr_material>())
            {
                const auto& pbr = static_cast<const pbr_material&>(*material_ptr);
                submit_pbr_material(geom_program_instanced_, pbr);
            }
        }

        // Set LOD parameters (using global LOD settings for now)
        const auto lod_params = math::vec3{0.0f, -1.0f, 1.0f}; // Default LOD params
        gfx::set_uniform(geom_program_instanced_.u_lod_params, lod_params);

        // Submit the instanced draw call
        gfx::submit(pass.id, geom_program_instanced_.program->native_handle(), 0, false);
    }

    geom_program_instanced_.program->end();

    // Update statistics
    const auto& batch_stats = batch_collector_.get_stats();
    stats_.add_batch_stats(batch_stats);
    
    // Clear batches to invalidate all transform pointers and free memory
    batch_collector_.clear();
}

void deferred::run_assao_pass(const camera& camera,
                              gfx::render_view& rview,
                              delta_t dt,
                              const run_params& rparams)
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_assao_params)
    {
        assao_pass_.release_resources(rview);
        return;
    }
    APP_SCOPE_PERF("Rendering/ASSAO Pass");

    const auto& gbuffer = rview.fbo_get("GBUFFER");

    auto color_ao = gbuffer->get_texture(0);
    auto normal = gbuffer->get_texture(1);
    auto depth = gbuffer->get_texture(4);

    assao_pass::run_params params;
    params.depth = depth.get();
    params.normal = normal.get();
    params.color_ao = color_ao.get();

    rparams.fill_assao_params(params);

    assao_pass_.run(camera, rview, params);
}

auto deferred::run_irradiance_pass(scene& scn, gfx::render_view& rview) -> deferred::irradiance_pass_result
{
    APP_SCOPE_PERF("Rendering/Irradiance Pass");

    irradiance_pass_result result;

    if(irradiance_compute_program_.program && irradiance_compute_program_.program->is_valid())
    {
        const auto& irradiance_tex = create_or_get_irradiance_texture(rview);

        struct skylight_params
        {
            float intensity = 0.0f;
            float sun_weight = 1.0f;
            float exposition = 0.1f;
            float sky_brightness = 1.0f;
            math::vec3 color = {1.0f, 1.0f, 1.0f};
            math::vec3 tint = {1.0f, 1.0f, 1.0f};
            math::vec3 light_dir;
            irradiance_perez_params perez;
            bool use_perez = false;
            bool is_skybox = false;
            bool use_sky = true;
            bool directional = true;
            asset_handle<gfx::texture> cubemap;
        };
        skylight_params dominant;

        scn.registry->view<transform_component, skylight_component, active_component>().each(
            [&](auto e, auto&& transform_comp_ref, auto&& skylight_comp_ref, auto&& active)
            {
                const auto& skylight = skylight_comp_ref;
                float irradiance_intensity = skylight.get_irradiance_intensity();
                if(irradiance_intensity <= 0.0f)
                    return;

                const auto& world_transform = transform_comp_ref.get_transform_global();
                math::vec3 light_dir = world_transform.z_unit_axis();
                math::vec3 irradiance_color = {1.0f, 1.0f, 1.0f};
                bool use_perez = false;
                bool is_skybox = (skylight.get_mode() == skylight_component::sky_mode::skybox);
                // Two independent axes:
                //  - wants_sky: does the sky/environment color contribute, or is the ambient a flat tint?
                //  - directional: does the ambient vary with the surface normal (full SH), or is it flat (L0)?
                const bool wants_sky = skylight.get_irradiance_use_sky();
                const bool directional =
                    (skylight.get_irradiance_quality() == skylight_component::irradiance_quality::directional);
                float sun_weight = 1.0f;

                if(!is_skybox)
                {
                    float sun_elevation = -light_dir.y;
                    // sun_weight: 0 at horizon, 1 at zenith. Smooth ramp over ~20° to avoid near-1 at low angles.
                    float x = math::clamp(sun_elevation / 0.35f, 0.0f, 1.0f);
                    sun_weight = x * x * (3.0f - 2.0f * x);
                }
                float exposition = 0.1f;

                if(!wants_sky)
                {
                    // Sky ignored: flat artist ambient straight from the tint color. Kept independent
                    // of sun elevation (sun_weight=1) and at unit exposition so the tint reads literally.
                    sun_weight = 1.0f;
                    exposition = 1.0f;
                }
                else if(!is_skybox && directional)
                {
                    use_perez = true;
                    compute_irradiance_perez_params(light_dir, skylight.get_turbidity(), dominant.perez);
                    compute_perez_luminance(light_dir, dominant.perez.sky_luminance_rgb, dominant.perez.sun_luminance_rgb);
                    irradiance_color = glm::mix(dominant.perez.sky_luminance_rgb, dominant.perez.sun_luminance_rgb, sun_weight);
                    exposition = dominant.perez.exposition;
                }
                else if(!is_skybox)
                {
                    // Flat ambient but the sky still contributes: collapse the Perez sky to one color.
                    // Perez integral (sky + circumsolar + sun disc) yields ~4-5x zenith luminance.
                    // mix(sky, sun, sun_weight * 0.25) empirically matches the directional result at same intensity.
                    math::vec3 sky_luminance_rgb;
                    math::vec3 sun_luminance_rgb;
                    compute_perez_luminance(light_dir, sky_luminance_rgb, sun_luminance_rgb);
                    irradiance_color = glm::mix(sky_luminance_rgb, sun_luminance_rgb, sun_weight * 0.25f);
                    float sun_altitude = -light_dir.y;
                    float altitude_factor = bx::lerp(0.6f, 1.0f, bx::clamp(bx::abs(sun_altitude), 0.0f, 1.0f));
                    exposition = 0.1f * altitude_factor;
                }
                // skybox + wants_sky: irradiance_color stays white; the cubemap supplies the color in-shader.

                float sky_brightness = skylight.get_sky_brightness();
                exposition *= sky_brightness;

                const auto& tint = skylight.get_irradiance_tint();
                math::vec3 tint_vec = {tint.value.r, tint.value.g, tint.value.b};
                irradiance_color.x *= tint_vec.x;
                irradiance_color.y *= tint_vec.y;
                irradiance_color.z *= tint_vec.z;

                if(irradiance_intensity > dominant.intensity)
                {
                    dominant.intensity = irradiance_intensity;
                    dominant.color = irradiance_color;
                    dominant.tint = tint_vec;
                    dominant.light_dir = light_dir;
                    dominant.use_perez = use_perez;
                    dominant.is_skybox = is_skybox;
                    dominant.use_sky = wants_sky;
                    dominant.directional = directional;
                    dominant.sun_weight = sun_weight;
                    dominant.exposition = exposition;
                    dominant.sky_brightness = sky_brightness;
                    dominant.cubemap = (is_skybox && wants_sky) ? skylight.get_cubemap() : asset_handle<gfx::texture>{};
                }
            });

        gfx::render_pass irr_pass("Irradiance/Compute Pass");
        irradiance_compute_program_.program->begin();
        gfx::set_image(0, irradiance_tex->native_handle(), 0, bgfx::Access::Write);

        int mode = 0;
        float ambient_vec[4];
        if(dominant.use_perez)
        {
            ambient_vec[0] = dominant.tint.x;
            ambient_vec[1] = dominant.tint.y;
            ambient_vec[2] = dominant.tint.z;
            ambient_vec[3] = dominant.intensity;
        }
        else
        {
            ambient_vec[0] = dominant.color.x;
            ambient_vec[1] = dominant.color.y;
            ambient_vec[2] = dominant.color.z;
            ambient_vec[3] = dominant.intensity;
        }
        auto cubemap_tex = dominant.cubemap.get();
        const bool use_cubemap = dominant.is_skybox && dominant.use_sky && cubemap_tex && cubemap_tex->info.cubeMap;

        // Perez sky modes use physical luminance (exposition-scaled); cubemaps are typically
        // pre-baked in display range. Boost intensity for sky-derived non-cubemap modes so shadow
        // fill matches cubemap at the same user-facing intensity. The flat tint-only ambient is
        // already in display range, so it gets no boost.
        constexpr float ambient_intensity_boost = 2.0f;
        if(use_cubemap)
            ambient_vec[3] *= dominant.sky_brightness;
        else if(dominant.use_sky)
            ambient_vec[3] *= ambient_intensity_boost;

        gfx::set_uniform(irradiance_compute_program_.u_irradiance_tint_intensity, ambient_vec);

        // exposition: scale ambient to display range (matches atmospheric sky, ~0.1 at noon).
        float exp_val = dominant.exposition;
        float exp_vec[4] = {exp_val, 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(irradiance_compute_program_.u_exposition, exp_vec);

        if(dominant.intensity > 0.0f && dominant.use_perez)
        {
            mode = 1;
            gfx::set_uniform(irradiance_compute_program_.u_sun_direction, dominant.perez.sun_direction);
            gfx::set_uniform(irradiance_compute_program_.u_sun_luminance, dominant.perez.sun_luminance_rgb);
            gfx::set_uniform(irradiance_compute_program_.u_sky_luminance_xyz, dominant.perez.sky_luminance_xyz);
            gfx::set_uniform(irradiance_compute_program_.u_perez_coeff, &dominant.perez.perez_coeff[0][0], 5);
        }
        else if(use_cubemap)
        {
            // mode 2 = full directional SH, mode 3 = flat (cubemap averaged into L0 only).
            mode = dominant.directional ? 2 : 3;
            gfx::set_texture(irradiance_compute_program_.s_env, 1, cubemap_tex);
        }
        else if(!dominant.use_sky && dominant.directional)
        {
            // No sky contribution but directional requested: hemisphere gradient from the tint
            // (full tint up -> darkened tint down). Flat tint-only stays at mode 0.
            mode = 4;
        }

        // x=mode, y=sun_weight (applied in shader for all modes)
        float mode_vec[4] = {float(mode), dominant.sun_weight, 0.0f, 0.0f};
        gfx::set_uniform(irradiance_compute_program_.u_mode, mode_vec);

        bgfx::dispatch(irr_pass.id, irradiance_compute_program_.program->native_handle(), 1, 1, 1);
        irradiance_compute_program_.program->end();

        result.irradiance_tex = irradiance_tex;
        result.global_color = dominant.color;
        result.global_intensity = dominant.intensity;
    }
    else
    {
        // Fallback when irradiance compute is unavailable: still create/bind texture (zeros)
        const auto& irradiance_tex = create_or_get_irradiance_texture(rview);
        result.irradiance_tex = irradiance_tex;
        scn.registry->view<transform_component, skylight_component, active_component>().each(
            [&](auto e, auto&& transform_comp_ref, auto&& skylight_comp_ref, auto&& active)
            {
                const auto& skylight = skylight_comp_ref;
                if(skylight.get_irradiance_quality() != skylight_component::irradiance_quality::flat)
                    return;
                float irradiance_intensity = skylight.get_irradiance_intensity();
                if(irradiance_intensity <= 0.0f)
                    return;
                const auto& world_transform = transform_comp_ref.get_transform_global();
                math::vec3 light_dir = world_transform.z_unit_axis();
                math::vec3 irradiance_color = {1.0f, 1.0f, 1.0f};
                // Only fold in the sky color when sky contribution is enabled; otherwise the
                // flat tint (applied below) is the whole ambient.
                if(skylight.get_irradiance_use_sky() && skylight.get_mode() != skylight_component::sky_mode::skybox)
                {
                    math::vec3 sky_luminance_rgb;
                    math::vec3 sun_luminance_rgb;
                    compute_perez_luminance(light_dir, sky_luminance_rgb, sun_luminance_rgb);
                    float sun_elevation = -light_dir.y;
                    float x = math::clamp(sun_elevation / 0.35f, 0.0f, 1.0f);
                    float sun_weight = x * x * (3.0f - 2.0f * x);
                    irradiance_color = glm::mix(sky_luminance_rgb, sun_luminance_rgb, sun_weight);
                    irradiance_intensity *= sun_weight;
                }
                const auto& tint = skylight.get_irradiance_tint();
                irradiance_color.x *= tint.value.r;
                irradiance_color.y *= tint.value.g;
                irradiance_color.z *= tint.value.b;
                if(irradiance_intensity > result.global_intensity)
                {
                    result.global_intensity = irradiance_intensity;
                    result.global_color = irradiance_color;
                }
            });
    }

    return result;
}

auto deferred::run_direct_lighting_pass(scene& scn,
                                        const camera& camera,
                                        gfx::render_view& rview,
                                        bool apply_shadows,
                                        delta_t dt) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Direct Lighting Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& lbuffer = rview.fbo_get("LBUFFER");

    const auto buffer_size = lbuffer->get_size();

    gfx::render_pass pass("Direct Lighting/Pass");
    pass.bind(lbuffer.get());
    pass.set_view_proj(view, proj);
    pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);

    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp_ref, auto&& light_comp_ref, auto&& active)
        {
            const auto& light = light_comp_ref.get_light();
            const auto& generator = light_comp_ref.get_shadowmap_generator();
            auto world_transform = transform_comp_ref.get_transform_global();
            world_transform.reset_scale();
            const auto& light_position = world_transform.get_position();
            const auto& light_direction = world_transform.z_unit_axis();

            const auto& bounds = light_comp_ref.get_bounds_precise(light_direction);
            if(!camera.test_obb(bounds, world_transform))
            {
                return;
            }

            irect32_t rect(0, 0, irect32_t::value_type(buffer_size.width), irect32_t::value_type(buffer_size.height));
            if(light_comp_ref
                   .compute_projected_sphere_rect(rect, light_position, light_direction, camera_pos, view, proj) == 0)
                return;

            
            APP_SCOPE_PERF("Rendering/Direct Lighting Pass/Per Light");

            bool has_shadows = light.casts_shadows && apply_shadows;

            stats_.drawn_lights++;
            stats_.drawn_lights_casting_shadows += uint32_t(has_shadows);

            const auto& lprogram = has_shadows ? get_light_program(light) : get_light_program_no_shadows(light);

            lprogram.program->begin();

            float contact_shadow_distance = light.contact_shadow.enabled
                                             ? light.contact_shadow.ray_length
                                             : 0.0f;

            float n_dot_l_low = light.contact_shadow.n_dot_l_fade_start;
            float n_dot_l_high = light.contact_shadow.n_dot_l_fade_end;
            if(n_dot_l_high < n_dot_l_low)
            {
                const float t = n_dot_l_low;
                n_dot_l_low = n_dot_l_high;
                n_dot_l_high = t;
            }
            const float contact_shadow_uniform[4] = {light.contact_shadow.thickness,
                                                     n_dot_l_low,
                                                     n_dot_l_high,
                                                     light.contact_shadow.normal_facing_reject};

            if(light.type == light_type::directional)
            {
                float light_data[4] = {0.0f, 0.0f, 0.0f, contact_shadow_distance};

                gfx::set_uniform(lprogram.u_light_direction, light_direction);
                gfx::set_uniform(lprogram.u_light_data, light_data);
            }
            if(light.type == light_type::point)
            {
                float light_data[4] = {light.point_data.range,
                                       light.point_data.exponent_falloff,
                                       0.0f,
                                       contact_shadow_distance};

                gfx::set_uniform(lprogram.u_light_position, light_position);
                gfx::set_uniform(lprogram.u_light_data, light_data);
            }

            if(light.type == light_type::spot)
            {
                float light_data[4] = {light.spot_data.get_range(),
                                       math::cos(math::radians(light.spot_data.get_inner_angle() * 0.5f)),
                                       math::cos(math::radians(light.spot_data.get_outer_angle() * 0.5f)),
                                       contact_shadow_distance};

                gfx::set_uniform(lprogram.u_light_direction, light_direction);
                gfx::set_uniform(lprogram.u_light_position, light_position);
                gfx::set_uniform(lprogram.u_light_data, light_data);
            }

            gfx::set_uniform(lprogram.u_contact_shadow, contact_shadow_uniform);

            float light_color_intensity[4] = {light.color.value.r,
                                              light.color.value.g,
                                              light.color.value.b,
                                              light.intensity};

            gfx::set_uniform(lprogram.u_light_color_intensity, light_color_intensity);

            gfx::set_uniform(lprogram.u_camera_position, camera_pos);

            size_t i = 0;
            for(; i < gbuffer->get_attachment_count(); ++i)
            {
                gfx::set_texture(lprogram.s_tex[i], i, gbuffer->get_texture(i));
            }
            // Skip s_tex5 (RBUFFER) and s_tex6 (BRDF LUT) — not used by per-light direct shaders.
            // Shadow maps start at slot 7.
            i = 7;

            if(has_shadows)
            {
                generator.submit_uniforms(i);
            }
            gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
            auto topology = gfx::clip_quad(1.0f);
            gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ADD);
            gfx::submit(pass.id, lprogram.program->native_handle());
            gfx::set_state(BGFX_STATE_DEFAULT);

            lprogram.program->end();
        });

    gfx::discard();

    return lbuffer;
}

auto deferred::run_indirect_lighting_pass(scene& scn,
                                          const camera& camera,
                                          gfx::render_view& rview,
                                          bool apply_reflection,
                                          delta_t dt) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Indirect Lighting Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& rbuffer = rview.fbo_safe_get("RBUFFER");
    const auto& lbuffer = rview.fbo_get("LBUFFER");

    const auto irradiance_result = run_irradiance_pass(scn, rview);

    gfx::render_pass pass("Indirect Lighting/Pass");
    pass.bind(lbuffer.get());
    pass.set_view_proj(view, proj);

    const auto& iprogram = indirect_lighting_program_;
    iprogram.program->begin();

    float light_data[4] = {irradiance_result.global_color.x, irradiance_result.global_color.y, irradiance_result.global_color.z, irradiance_result.global_intensity};
    gfx::set_uniform(iprogram.u_light_data, light_data);
    gfx::set_uniform(iprogram.u_camera_position, camera_pos);

    size_t i = 0;
    for(; i < gbuffer->get_attachment_count(); ++i)
    {
        gfx::set_texture(iprogram.s_tex[i], i, gbuffer->get_texture(i));
    }
    gfx::set_texture(iprogram.s_tex[i], i, apply_reflection ? rbuffer->get_texture(0) : default_textures::get().black_texture());
    i++;
    gfx::set_texture(iprogram.s_tex[i], i, ibl_brdf_lut_.get());
    i++;
    gfx::set_texture(iprogram.s_irradiance, 7, irradiance_result.irradiance_tex ? irradiance_result.irradiance_tex : default_textures::get().black_texture());
    
    const auto& ssil_tex = rview.tex_safe_get("SSIL");
    // Transparent (alpha 0) fallback when SSIL is disabled/absent so the shader's
    // mix(irradiance, ssil.rgb, ssil.a) collapses to the pure SH probe. The opaque-black
    // default (alpha 1) would instead force mix() to 0 and wipe out the ambient.
    gfx::set_texture(iprogram.s_ssil, 8, ssil_tex ? ssil_tex : default_textures::get().transparent_texture());
    

    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ADD);
    gfx::submit(pass.id, iprogram.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);

    iprogram.program->end();

    gfx::discard();

    return lbuffer;
}

void deferred::run_reflection_probe_pass(scene& scn, const camera& camera, gfx::render_view& rview, bool apply_probes, delta_t dt)
{
    if(!apply_probes)
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Reflection Probe Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();

    const auto& viewport_size = camera.get_viewport_size();
    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& rbuffer = rview.fbo_get("RBUFFER");

    const auto buffer_size = rbuffer->get_size();

    gfx::render_pass pass("Reflections/Buffer Pass");
    pass.bind(rbuffer.get());
    pass.set_view_proj(view, proj);
    pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);


    std::vector<entt::entity> sorted_probes;

    // Collect all entities with the relevant components
    scn.registry->view<transform_component, reflection_probe_component, active_component>().each(
        [&](auto e, auto&& transform_comp_ref, auto&& probe_comp_ref, auto&& active)
        {
            sorted_probes.emplace_back(e);
        });

    // Sort the probes based on the method and max range
    std::sort(std::begin(sorted_probes),
              std::end(sorted_probes),
              [&](const auto& lhs, const auto& rhs)
              {
                  const auto& lhs_comp = scn.registry->get<reflection_probe_component>(lhs);
                  const auto& lhs_probe = lhs_comp.get_probe();

                  const auto& rhs_comp = scn.registry->get<reflection_probe_component>(rhs);
                  const auto& rhs_probe = rhs_comp.get_probe();

                  // Environment probes should be last
                  if(lhs_probe.method != rhs_probe.method)
                  {
                      return lhs_probe.method < rhs_probe.method; // Environment method is "greater"
                  }

                  // If the reflection methods are the same, compare based on the maximum range
                  return lhs_probe.get_max_range() > rhs_probe.get_max_range(); // Smaller ranges first
              });

    // Render or process the sorted probes
    for(const auto& e : sorted_probes)
    {
        auto& transform_comp_ref = scn.registry->get<transform_component>(e);
        auto& probe_comp_ref = scn.registry->get<reflection_probe_component>(e);

        const auto& probe = probe_comp_ref.get_probe();
        const auto& world_transform = transform_comp_ref.get_transform_global();
        const auto& probe_position = world_transform.get_position();
        const auto& probe_scale = world_transform.get_scale();

        irect32_t rect(0, 0, irect32_t::value_type(buffer_size.width), irect32_t::value_type(buffer_size.height));
        if(probe_comp_ref.compute_projected_sphere_rect(rect, probe_position, probe_scale, camera_pos, view, proj) == 0)
        {
            continue;
        }

        const auto& cubemap = probe_comp_ref.get_cubemap_prefiltered();

        ref_probe_program* ref_probe_program = nullptr;
        float influence_radius = 0.0f;
        if(probe.type == probe_type::sphere && sphere_ref_probe_program_.program)
        {
            ref_probe_program = &sphere_ref_probe_program_;
            influence_radius =
                math::max(probe_scale.x, math::max(probe_scale.y, probe_scale.z)) * probe.sphere_data.range;
        }

        if(probe.type == probe_type::box && box_ref_probe_program_.program)
        {
            math::transform t = world_transform;
            t.scale(probe.box_data.extents);
            auto u_inv_world = math::inverse(t).get_matrix();
            float data2[4] = {probe.box_data.extents.x,
                              probe.box_data.extents.y,
                              probe.box_data.extents.z,
                              probe.box_data.transition_distance};

            ref_probe_program = &box_ref_probe_program_;

            gfx::set_uniform(box_ref_probe_program_.u_inv_world, u_inv_world);
            gfx::set_uniform(box_ref_probe_program_.u_data2, data2);

            influence_radius = math::length(t.get_scale() + probe.box_data.transition_distance);
        }

        if(ref_probe_program)
        {
            float mips = cubemap ? float(cubemap->info.numMips) : 1.0f;
            float data0[4] = {
                probe_position.x,
                probe_position.y,
                probe_position.z,
                influence_radius,
            };

            const bool is_global_fallback = probe.method == reflect_method::environment;
            const float source_validity = 1.0f;
            float data1[4] = {mips, probe.intensity, is_global_fallback ? 1.0f : 0.0f, source_validity};
            float capture[4] = {probe_comp_ref.get_apply_prefilter() ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};

            gfx::set_uniform(ref_probe_program->u_data0, data0);
            gfx::set_uniform(ref_probe_program->u_data1, data1);
            gfx::set_uniform(ref_probe_program->u_capture, capture);

            for(size_t i = 0; i < gbuffer->get_attachment_count(); ++i)
            {
                gfx::set_texture(ref_probe_program->s_tex[i], i, gbuffer->get_texture(i));
            }

            gfx::set_texture(ref_probe_program->s_tex_cube, 5, cubemap);

            gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
            auto topology = gfx::clip_quad(1.0f);
            gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);

            ref_probe_program->program->begin();
            gfx::submit(pass.id, ref_probe_program->program->native_handle());
            gfx::set_state(BGFX_STATE_DEFAULT);
            ref_probe_program->program->end();
        }
    }

    gfx::discard();
}

auto deferred::run_atmospherics_pass(gfx::frame_buffer::ptr input,
                                     scene& scn,
                                     const camera& camera,
                                     gfx::render_view& rview,
                                     delta_t dt) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Atmospheric Pass");

    atmospheric_pass_perez::run_params params_perez;
    atmospheric_pass_skybox::run_params params_skybox;

    bool found_sun = false;

    skylight_component::sky_mode mode{};
    scn.registry->view<transform_component, skylight_component, active_component>().each(
        [&](auto e, auto&& transform_comp_ref, auto&& light_comp_ref, auto&& active)
        {
            auto entity = scn.create_handle(e);

            if(found_sun)
            {
                APPLOG_WARNING("[{}] More than one entity with this component. Others are ignored.", "Skylight");
                return;
            }
            const auto& cubemap = light_comp_ref.get_cubemap();
            auto cubemap_texture = cubemap.get();
            if(cubemap_texture)
            {
                if(cubemap_texture->info.cubeMap)
                {
                    params_skybox.cubemap = cubemap;
                }
            }

            mode = light_comp_ref.get_mode();
            found_sun = true;
            if(auto light_comp = entity.template try_get<light_component>())
            {
                const auto& light = light_comp->get_light();

                if(light.type == light_type::directional)
                {
                    const auto& world_transform = transform_comp_ref.get_transform_global();
                    
                    params_perez.light_direction = world_transform.z_unit_axis();
                    params_perez.turbidity = light_comp_ref.get_turbidity();
                    params_perez.cloud_mode = static_cast<int>(light_comp_ref.get_cloud_mode());
                    params_perez.cloud_coverage = light_comp_ref.get_cloud_coverage();
                    params_perez.cloud_base_altitude = light_comp_ref.get_cloud_base_altitude();
                    params_perez.cloud_top_altitude = light_comp_ref.get_cloud_top_altitude();
                    params_perez.cloud_density = light_comp_ref.get_cloud_density();
                    params_perez.cloud_absorption = light_comp_ref.get_cloud_absorption();
                    params_perez.cloud_light_absorption = light_comp_ref.get_cloud_light_absorption();
                    params_perez.cloud_time = light_comp_ref.get_cloud_time();
                    params_perez.sky_brightness = light_comp_ref.get_sky_brightness();
                    params_perez.cloud_vol_uv_scale = light_comp_ref.get_cloud_vol_uv_scale();
                    params_perez.cloud_vol_edge_width = light_comp_ref.get_cloud_vol_edge_width();
                    params_perez.cloud_vol_shape_power = light_comp_ref.get_cloud_vol_shape_power();
                    params_perez.cloud_vol_detail_erode = light_comp_ref.get_cloud_vol_detail_erode();
                    params_perez.cloud_vol_macro_strength = light_comp_ref.get_cloud_vol_macro_strength();
                    params_perez.cloud_vol_coarse_scale = light_comp_ref.get_cloud_vol_coarse_scale();
                    params_perez.cloud_vol_base_mix = light_comp_ref.get_cloud_vol_base_mix();
                    params_perez.cloud_vol_sun_intensity = light_comp_ref.get_cloud_vol_sun_intensity();
                }
                params_perez.irradiance_intensity = light_comp_ref.get_irradiance_intensity();
            }
            params_skybox.sky_brightness = light_comp_ref.get_sky_brightness();
        });

    if(!found_sun)
    {
        return input;
    }
    const auto& viewport_size = camera.get_viewport_size();

    auto c = camera;
    c.set_projection_mode(projection_mode::perspective);

    auto lbuffer_depth = rview.fbo_get("LBUFFER_DEPTH");

    switch(mode)
    {
        case unravel::skylight_component::sky_mode::skybox:
            atmospheric_pass_skybox_.run(lbuffer_depth, c, rview, dt, params_skybox);
            break;
        default:
            atmospheric_pass_perez_.run(lbuffer_depth, c, rview, dt, params_perez);
            break;
    }

    return input;
}

void deferred::run_ssr_pass(const camera& camera,
                            gfx::render_view& rview,
                            const gfx::frame_buffer::ptr& previous_frame_source,
                            const run_params& rparams)
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_ssr_params)
    {
        ssr_pass_.release_resources(rview);
        return;
    }

    ssr_pass::run_params ssr_params;

    ssr_params.output = rview.fbo_get("RBUFFER");
    ssr_params.g_buffer = rview.fbo_get("GBUFFER");

    ssr_params.previous_frame =
        previous_frame_source ? previous_frame_source->get_texture() : rview.fbo_get("LBUFFER")->get_texture();

    ssr_params.cam = &camera;

    if(rparams.fill_ssr_params)
    {
        rparams.fill_ssr_params(ssr_params);
    }

    ssr_params.hiz_buffer = rview.tex_get("HIZBUFFER");

    // BUG Cone tracing is not working properly, so we disable it for now.
    ssr_params.settings.fidelityfx.enable_cone_tracing = false;

    ssr_pass_.run(rview, ssr_params);
}

void deferred::run_ssil_pass(const camera& camera,
                             gfx::render_view& rview,
                             const run_params& rparams)
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_ssil_params)
    {
        ssil_pass_.release_resources(rview);
        rview.tex_remove("SSIL");
        rview.tex_remove("PREV_SSIL");
        return;
    }

    ssil_pass::run_params ssil_params;
    ssil_params.g_buffer = rview.fbo_get("GBUFFER");
    ssil_params.direct_lighting = rview.fbo_get("LBUFFER")->get_texture(0);
    ssil_params.prev_depth = rview.tex_safe_get("PREV_DEPTH");
    ssil_params.prev_ssil = rview.tex_safe_get("PREV_SSIL");
    // Last frame's environment SH (the pass that computes it runs later, in the indirect
    // lighting pass); used as the per-ray miss fallback so escaped rays integrate the
    // environment. Persists across frames in the render_view, so it is null only on frame 0.
    ssil_params.irradiance_sh = rview.tex_safe_get("IRRADIANCE_SH");
    ssil_params.cam = &camera;

    rparams.fill_ssil_params(ssil_params);

    ssil_params.hiz_buffer = rview.tex_get("HIZBUFFER");

    auto result = ssil_pass_.run(rview, ssil_params);
    rview.tex_get_or_emplace("SSIL") = result;

    if(ssil_params.settings.enable_multi_bounce && result)
    {
        // 1:1 blit of the SSIL output into PREV_SSIL. The output is full-res when the
        // trace runs reduced-res (the joint-bilateral upsample pass already reconstructed
        // it edge-aware), so feeding it back is safe -- the old failure mode was a NAIVE
        // full-viewport upscale that bled bright indirect across depth boundaries. Sizing
        // PREV_SSIL to the result keeps the blit a 1:1 copy regardless of trace resolution.
        const auto prev_sz = result->get_size();
        auto& prev_ssil = rview.tex_get_or_emplace("PREV_SSIL");

        if(gfx::needs_recreate(prev_ssil, prev_sz))
        {
            prev_ssil.reset();
            prev_ssil = std::make_shared<gfx::texture>(static_cast<std::uint16_t>(prev_sz.width),
                                                       static_cast<std::uint16_t>(prev_sz.height),
                                                       false,
                                                       1,
                                                       gfx::texture_format::RGBA16F,
                                                       BGFX_TEXTURE_BLIT_DST |
                                                           BGFX_SAMPLER_U_CLAMP |
                                                           BGFX_SAMPLER_V_CLAMP);
        }
        gfx::render_pass blit_pass("SSIL/Prev SSIL Blit Pass");
        gfx::blit(blit_pass.id,
                  prev_ssil->native_handle(), 0, 0,
                  result->native_handle(), 0, 0);
    }
    else
    {
        rview.tex_remove("PREV_SSIL");
    }

}

auto deferred::run_taa_pass(const camera& camera,
                            gfx::render_view& rview,
                            const gfx::frame_buffer::ptr& input,
                            const gfx::frame_buffer::ptr& output,
                            const run_params& rparams) -> gfx::frame_buffer::ptr
{
    if(!rparams.fill_taa_params)
    {
        taa_pass_.release_resources(rview);
        return input;
    }
    const auto& gbuffer = rview.fbo_safe_get("GBUFFER");
    if(!input || !gbuffer)
    {
        return input;
    }
    taa_pass::run_params p;
    p.input = input;
    p.output = nullptr;
    p.cam = &camera;
    p.g_buffer = gbuffer;
    rparams.fill_taa_params(p);
    return taa_pass_.run(rview, p);
}

auto deferred::run_fxaa_pass(gfx::render_view& rview,
                             const gfx::frame_buffer::ptr& input,
                             const gfx::frame_buffer::ptr& output,
                             const run_params& rparams) -> gfx::frame_buffer::ptr
{
    if(!rparams.fill_fxaa_params || rparams.fill_taa_params)
    {
        fxaa_pass_.release_resources(rview);
        return input;
    }

    APP_SCOPE_PERF("Rendering/FXAA Pass");

    fxaa_pass::run_params params;
    params.input = input;
    params.output = output;

    rparams.fill_fxaa_params(params);

    return fxaa_pass_.run(rview, params);
}

void deferred::run_auto_exposure_pass(gfx::render_view& rview,
                                      const gfx::frame_buffer::ptr& input,
                                      const run_params& rparams,
                                      delta_t dt)
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_auto_exposure_params)
    {
        auto_exposure_pass_.release_resources(rview);
        return;
    }
    auto_exposure_pass::run_params params;
    params.input = input;
    params.delta_time = dt.count();
    rparams.fill_auto_exposure_params(params);
    auto_exposure_pass_.run(rview, params);
}

auto deferred::run_bloom_pass(gfx::render_view& rview,
                              const gfx::frame_buffer::ptr& input,
                              const run_params& rparams) -> gfx::frame_buffer::ptr
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_bloom_params || !rparams.fill_hdr_params)
    {
        bloom_pass_.release_resources(rview);
        return input;
    }
    bloom_pass::run_params params;
    params.input = input;
    rparams.fill_bloom_params(params);

    if(rparams.fill_auto_exposure_params)
    {
        params.exposure_texture = auto_exposure_pass_.get_exposure_texture(rview);
    }

    return bloom_pass_.run(rview, params);
}

auto deferred::run_tonemapping_pass(gfx::render_view& rview,
                                    const gfx::frame_buffer::ptr& input,
                                    const gfx::frame_buffer::ptr& output,
                                    const run_params& rparams) -> gfx::frame_buffer::ptr
{
    if(!rparams.fill_hdr_params)
    {
        tonemapping_pass_.release_resources(rview);
        return input;
    }
    APP_SCOPE_PERF("Rendering/Tonemapping Pass");

    tonemapping_pass::run_params params;
    params.input = input;

    if(!rparams.fill_fxaa_params || rparams.fill_taa_params)
    {
        params.output = output;
    }

    rparams.fill_hdr_params(params);

    if(rparams.fill_auto_exposure_params)
    {
        params.exposure_texture = auto_exposure_pass_.get_exposure_texture(rview);
    }

    return tonemapping_pass_.run(rview, params);
}

void deferred::run_debug_visualization_pass(const camera& camera,
                                            gfx::render_view& rview,
                                            const gfx::frame_buffer::ptr& output)
{
    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& rbuffer = rview.fbo_safe_get("RBUFFER");
    const auto& irradiance_tex = create_or_get_irradiance_texture(rview);

    gfx::render_pass pass("Debug/Visualization Pass");
    pass.bind(output.get());
    pass.set_view_proj(view, proj);
    // pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);

    const auto output_size = output->get_size();

    debug_visualization_program_.program->begin();

    float u_params[4] = {float(debug_pass_), 0.0f, 0.0f, 0.0f};

    gfx::set_uniform(debug_visualization_program_.u_params, u_params);

    size_t i = 0;
    for(; i < gbuffer->get_attachment_count(); ++i)
    {
        gfx::set_texture(debug_visualization_program_.s_tex[i], i, gbuffer->get_texture(i));
    }
    gfx::set_texture(debug_visualization_program_.s_tex[i], i, rbuffer);
    ++i;
    gfx::set_texture(debug_visualization_program_.s_tex[i], i, irradiance_tex);
    ++i;
    const auto& ssil_tex = rview.tex_safe_get("SSIL");
    if(ssil_tex)
    {
        gfx::set_texture(debug_visualization_program_.s_tex[i], i, ssil_tex);
    }

    irect32_t rect(0, 0, irect32_t::value_type(output_size.width), irect32_t::value_type(output_size.height));
    gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::submit(pass.id, debug_visualization_program_.program->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);
    debug_visualization_program_.program->end();

    gfx::discard();
}

auto deferred::run_hiz_pass(const camera& camera,
                              gfx::render_view& rview,
                              const run_params& params,
                              const usize32_t& viewport_size,
                              delta_t dt) -> bool
{
    (void)dt;
    const bool want_hiz =
        reflection_screen_stack_enabled(params) && (params.fill_ssr_params || params.fill_ssil_params);

    if(!want_hiz)
    {
        rview.tex_remove("HIZBUFFER");
        rview.tex_remove("PREV_DEPTH");
        return false;
    }

    create_or_resize_hiz_buffer(rview, viewport_size);

    APP_SCOPE_PERF("Rendering/SSR/Hi-Z Pass");

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    if(!gbuffer)
    {
        return false;
    }

    hiz_pass::run_params hp;
    hp.depth_buffer = gbuffer->get_texture(4);
    hp.output_hiz = rview.tex_get("HIZBUFFER");
    hp.cam = &camera;

    hiz_pass_.run(rview, hp);
    return true;
}

deferred::deferred()
{
    init(engine::context());
}

deferred::~deferred()
{
    deinit(engine::context());
}

auto deferred::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto load_program = [&](const std::string& vs, const std::string& fs)
    {
        auto vs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/" + vs + ".sc");
        auto fs_shadfer = am.get_asset<gfx::shader>("engine:/data/shaders/" + fs + ".sc");

        return std::make_unique<gpu_program>(vs_shader, fs_shadfer);
    };

    geom_program_.program = load_program("deferred_geom/vs_deferred_geom", "deferred_geom/fs_deferred_geom");
    geom_program_.cache_uniforms();

    geom_program_skinned_.program = load_program("deferred_geom/vs_deferred_geom_skinned", "deferred_geom/fs_deferred_geom");
    geom_program_skinned_.cache_uniforms();

    geom_program_instanced_.program = load_program("deferred_geom/vs_deferred_geom_instanced", "deferred_geom/fs_deferred_geom");
    geom_program_instanced_.cache_uniforms();

    sphere_ref_probe_program_.program = load_program("vs_clip_quad_ex", "reflection_probe/fs_sphere_reflection_probe");
    sphere_ref_probe_program_.cache_uniforms();

    box_ref_probe_program_.program = load_program("vs_clip_quad_ex", "reflection_probe/fs_box_reflection_probe");
    box_ref_probe_program_.cache_uniforms();

    indirect_lighting_program_.program = load_program("vs_clip_quad", "fs_deferred_indirect_light");
    indirect_lighting_program_.cache_uniforms();

    auto cs_irradiance = am.get_asset<gfx::shader>("engine:/data/shaders/irradiance/cs_irradiance_sh.sc");
    if(cs_irradiance)
    {
        irradiance_compute_program_.program = std::make_unique<gpu_program>(cs_irradiance);
        irradiance_compute_program_.cache_uniforms();
    }

    debug_visualization_program_.program = load_program("vs_clip_quad", "gbuffer/fs_gbuffer_visualize");
    debug_visualization_program_.cache_uniforms();

    // Color lighting.

    // clang-format off
    color_lighting_no_shadow_[uint8_t(light_type::spot)].program = load_program("vs_clip_quad", "fs_deferred_spot_light");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_spot_light_hard");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcf");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcss");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_vsm");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_esm");

    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_spot_light_hard_linear");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcf_linear");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_pcss_linear");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_vsm_linear");
    color_lighting_[uint8_t(light_type::spot)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_spot_light_esm_linear");

    color_lighting_no_shadow_[uint8_t(light_type::point)].program = load_program("vs_clip_quad", "fs_deferred_point_light");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_point_light_hard");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_pcf");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_pcss");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_vsm");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_esm");

    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_point_light_hard_linear");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_pcf_linear");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_pcss_linear");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_vsm_linear");
    color_lighting_[uint8_t(light_type::point)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_point_light_esm_linear");

    color_lighting_no_shadow_[uint8_t(light_type::directional)].program = load_program("vs_clip_quad", "fs_deferred_directional_light");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_directional_light_hard");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcf");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcss");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_vsm");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::invz)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_esm");

    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::hard)].program = load_program("vs_clip_quad", "fs_deferred_directional_light_hard_linear");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcf) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcf_linear");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::pcss) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_pcss_linear");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::vsm) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_vsm_linear");
    color_lighting_[uint8_t(light_type::directional)][uint8_t(sm_depth::linear)][uint8_t(sm_impl::esm) ].program = load_program("vs_clip_quad", "fs_deferred_directional_light_esm_linear");
    // clang-format on

    for(auto& byLightType : color_lighting_no_shadow_)
    {
        if(byLightType.program)
        {
            byLightType.cache_uniforms();
        }
    }
    for(auto& byLightType : color_lighting_)
    {
        for(auto& byDepthType : byLightType)
        {
            for(auto& bySmImpl : byDepthType)
            {
                if(bySmImpl.program)
                {
                    bySmImpl.cache_uniforms();
                }
            }
        }
    }

    ibl_brdf_lut_ = am.get_asset<gfx::texture>("engine:/data/textures/ibl_brdf_lut.png");

    return pipeline::init(ctx);
}

auto deferred::deinit(rtti::context& ctx) -> bool
{
    return true;
}


} // namespace rendering
} // namespace unravel
