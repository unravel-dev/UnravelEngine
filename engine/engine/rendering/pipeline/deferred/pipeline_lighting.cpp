#include "pipeline_internal.h"

namespace unravel
{
namespace
{
namespace ANONYMOUS
{
using namespace rendering::deferred_detail;

/// First sampler stage of the shadow maps' depth attachments in the light programs (s_shadowDepth0).
constexpr uint8_t light_shadow_compare_stage = 12;
/// First sampler stage of the shadow maps in the light programs (s_shadowMap0).
constexpr uint8_t light_shadow_map_stage = 7;
/// Sampler stage of the cloud shadow map in the directional light programs, after the 4 cascades.
constexpr uint8_t light_cloud_shadow_stage = 11;
/// The stages fs_pbr_lighting.sh declares the GPU pre-exposure at (PRE_EXPOSURE_STAGE): the direct light
/// programs leave s_tex5 / s_tex6 unbound, the indirect ones keep 13 free.
constexpr uint8_t light_pre_exposure_stage = 6;
constexpr uint8_t indirect_pre_exposure_stage = 13;

/// The color_lighting_pcf_tent_ slot of a PCF light (get_pcf_kernel), or no slot for the 5x5 tent.
auto get_pcf_tent_slot(const light& l) -> hpp::optional<size_t>
{
    switch(get_pcf_kernel(l))
    {
        case pcf_kernel::tent3:
            return pcf_tent3_slot;
        case pcf_kernel::tent7:
            return pcf_tent7_slot;
        case pcf_kernel::tent5:
        default:
            return hpp::nullopt;
    }
}

/// Whether a light runs the contact shadow ray march: the length submit_light uploads is above 0.
auto has_contact_shadow(const light& l) -> bool
{
    return l.contact_shadow.enabled && l.contact_shadow.ray_length > 0.0f;
}

template<typename Program>
auto is_program_loaded(const Program& program) -> bool
{
    return program.program && program.program->is_valid();
}

/// The leanest of a light's @p variants (deferred::light_program_variants) that draws its image: with the contact
/// shadow code for a light with contact shadows, with the subsurface code when the G-buffer holds subsurface colors,
/// and without both otherwise. The run's limits decide which variants exist (deferred::update_light_programs); one
/// that did not load falls back to another that did.
template<typename Variants>
auto pick_light_variant(const light& l, bool gbuffer_has_subsurface, const Variants& variants) -> const auto&
{
    if(has_contact_shadow(l) && is_program_loaded(variants.full))
    {
        return variants.full;
    }
    if(gbuffer_has_subsurface && is_program_loaded(variants.no_contact))
    {
        return variants.no_contact;
    }
    if(is_program_loaded(variants.no_contact_no_subsurface))
    {
        return variants.no_contact_no_subsurface;
    }
    return is_program_loaded(variants.no_contact) ? variants.no_contact : variants.full;
}

/// The leanest of the indirect pass's @p variants (deferred::indirect_lighting_no_screen_inputs_variants) that draws this
/// run's image: without the subsurface read when the G-buffer holds no subsurface color, and with less detail when the
/// limits leave it out. A variant that did not load falls back to the fuller one.
template<typename Variants>
auto pick_indirect_lighting_variant(bool gbuffer_has_subsurface, bool is_detailed, const Variants& variants) -> const auto&
{
    const auto& with_subsurface = !is_detailed && is_program_loaded(variants.low_detail) ? variants.low_detail : variants.base;
    const auto& without_subsurface = !is_detailed && is_program_loaded(variants.no_subsurface_low_detail)
                                         ? variants.no_subsurface_low_detail
                                         : variants.no_subsurface;
    return !gbuffer_has_subsurface && is_program_loaded(without_subsurface) ? without_subsurface : with_subsurface;
}

/// Period of the contact-shadow dither's temporal offset (frames); TAA integrates it.
constexpr int contact_shadow_dither_frames = 16;
/// Most particle lights one view draws (the brightest of every emitter's, emitter_render_desc::light_count).
constexpr size_t max_particle_lights_per_view = 64;
/// RBUFFER's clear, packed RGBA8 (bgfx converts it for float targets): black with alpha 1 - no
/// traced reflection yet, and the whole pixel left to the probe layer.
constexpr uint32_t reflection_traced_clear_rgba = 0x000000ff;
} // namespace ANONYMOUS
} // namespace

namespace rendering
{

namespace
{
// Cubemap face captures strip post-processing and write the linear HDR lighting
// result directly to the cubemap face. Clearing fill_hdr_params also used to drop
// the G/L buffers to RGBA8 (LDR-clamped IBL); force_hdr_buffers keeps them float.
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
    params.fill_fog_params = {};
    params.force_hdr_buffers = true;
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
} // namespace

auto deferred::get_light_program(const light& l) const -> const color_lighting&
{
    const auto type = uint8_t(l.type);
    const auto depth = uint8_t(l.shadow_params.depth);
    const auto impl = uint8_t(l.shadow_params.type);
    const hpp::optional<size_t> tent_slot =
        l.shadow_params.type == sm_impl::pcf ? ANONYMOUS::get_pcf_tent_slot(l) : hpp::optional<size_t>{};
    // The tent is chosen on the variant every allowed slot has, created or still loading, so a variant that is
    // missing never changes the filter.
    if(tent_slot && color_lighting_pcf_tent_[type][depth][*tent_slot].no_contact_no_subsurface.program)
    {
        return ANONYMOUS::pick_light_variant(l, gbuffer_has_subsurface_, color_lighting_pcf_tent_[type][depth][*tent_slot]);
    }
    return ANONYMOUS::pick_light_variant(l, gbuffer_has_subsurface_, color_lighting_[type][depth][impl]);
}

auto deferred::get_light_program_no_shadows(const light& l) const -> const color_lighting&
{
    return ANONYMOUS::pick_light_variant(l, gbuffer_has_subsurface_, color_lighting_no_shadow_[uint8_t(l.type)]);
}

void deferred::build_reflections(scene& scn, const camera& camera, delta_t dt, const feature_limits& limits)
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

                    if(not_environment && reflection_probe_comp.get_capture_shadows())
                    {
                        pflags |= pipeline_steps::shadow_pass;
                        vflags |= visibility_query::is_shadow_caster;
                    }

                    auto params = create_run_params(handle, &scn, &camera);
                    params.run_type = pipeline_run_type::reflection_probe_capture;
                    params.vflags = vflags;
                    params.pflags = pflags;
                    // The camera's limits: its shadow maps are shared with the captures, so both render
                    // every light the same way.
                    apply_feature_limits(params, limits);
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

                    prefilter_pass_.run(prefilter_params);
                    reflection_probe_comp.mark_capture_ready();
                }

                gfx::render_pass::pop_scope();
            }
        });
}

void deferred::run_temporal_probe_pass(const camera& camera,
                                       gfx::render_view& rview,
                                       const gfx::frame_buffer::ptr& output)
{
    temporal_probe_pass::run_params probe_params;
    probe_params.color = output->get_texture(0);
    probe_params.velocity = rview.tex_find("VELOCITY");
    const auto& probe_gbuffer = rview.fbo_get("GBUFFER");
    probe_params.depth = probe_gbuffer ? probe_gbuffer->get_texture(4) : nullptr;
    probe_params.prev_depth = rview.tex_find("PREV_DEPTH");
    probe_params.cam = &camera;
    temporal_probe_pass_.run(probe_params);
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

    auto normal = gbuffer->get_texture(1);
    auto depth = gbuffer->get_texture(4);

    assao_pass::run_params params;
    params.depth = depth.get();
    params.normal = normal.get();

    rparams.fill_assao_params(params);

    assao_pass_.run(camera, rview, params);
}

namespace
{
/// The luminance of a linear color (Rec. 709).
auto get_luminance(const math::vec3& color) -> float
{
    return math::dot(color, math::vec3(0.2126f, 0.7152f, 0.0722f));
}

/// The directional light's luminous intensity in engine units (intensity x the luminance of
/// its linear colour): the one knob the sun-relative Perez sky follows (perez_luminance.h).
/// The strongest active, enabled directional light wins; 0 when the scene has none, which keeps
/// the fixed conversion.
auto find_sun_luminous_intensity(scene& scn) -> float
{
    float strongest = 0.0f;
    scn.registry->view<light_component, active_component>().each(
        [&](auto e, auto&& light_comp_ref, auto&& active)
        {
            const auto& light = light_comp_ref.get_light();
            if(light.type != light_type::directional || !light_comp_ref.is_enabled())
            {
                return;
            }
            const auto color = light.color.to_linear();
            const math::vec3 linear_color(color.value.r, color.value.g, color.value.b);
            strongest = math::max(strongest, light.intensity * get_luminance(linear_color));
        });
    return strongest;
}

/// The value of the constant SH basis function, the factor between a flat ambient and its L0 coefficient.
constexpr float sh_l0_basis = 0.282095f;
/// The share of a sky's zenith irradiance its L0 band keeps: the sky fills one hemisphere of the sphere L0 averages.
constexpr float sky_l0_irradiance_share = 0.5f;

/**
 * The flat ambient of @p skylight without the irradiance compute pass: the level a white wall displays, as
 * cs_irradiance_sh.eshader's uniform mode reads it. A sky's level comes from its irradiance at the zenith and its
 * zenith color, a cubemap counts as white: reading its texels back has no path here.
 * @param light_direction The skylight's z axis: where its light travels.
 */
auto compute_flat_ambient(const skylight_component& skylight, const math::vec3& light_direction, float sun_intensity) -> math::vec3
{
    const auto tint_color = skylight.get_irradiance_tint().to_linear();
    const math::vec3 tint(tint_color.value.r, tint_color.value.g, tint_color.value.b);
    const float intensity = skylight.get_irradiance_intensity() * skylight.get_sky_brightness();
    if(!skylight.get_irradiance_use_sky() || skylight.get_mode() == skylight_component::sky_mode::skybox)
    {
        return tint * intensity;
    }
    irradiance_perez_params perez;
    compute_irradiance_perez_params(light_direction, skylight.get_turbidity(), sun_intensity, perez);
    const float parity = perez.sun_relative ? 1.0f : sky_ambient_cubemap_parity;
    const float irradiance = perez.horizontal_irradiance_unit * perez.exposition * compute_perez_sun_weight(-light_direction.y);
    const float level = irradiance * sky_l0_irradiance_share * parity * intensity / math::pi<float>();
    const math::vec3 sky_color = perez.sky_luminance_rgb / math::max(get_luminance(perez.sky_luminance_rgb), math::epsilon<float>());
    return sky_color * tint * level;
}

/// Writes @p ambient (compute_flat_ambient) as the L0 band of the view's environment SH, the other bands black. The
/// texel after them holds the GPU's share of the pre-exposure and stays as it is.
void write_flat_irradiance(const gfx::texture::ptr& irradiance_tex, const math::vec3& ambient)
{
    std::array<float, pre_exposure_state::gpu_texel * 4> texels{};
    const math::vec3 l0 = ambient / sh_l0_basis;
    texels[0] = l0.x;
    texels[1] = l0.y;
    texels[2] = l0.z;
    bgfx::updateTexture2D(irradiance_tex->native_handle(),
                          0,
                          0,
                          0,
                          0,
                          pre_exposure_state::gpu_texel,
                          1,
                          bgfx::copy(texels.data(), uint32_t(sizeof(texels))));
}
} // namespace

auto deferred::run_irradiance_pass(scene& scn, gfx::render_view& rview, const feature_limits& limits) -> deferred::irradiance_pass_result
{
    APP_SCOPE_PERF("Rendering/Irradiance Pass");

    irradiance_pass_result result;

    if(get_render_feature_support().is_available(render_feature::irradiance_sh) &&
       begin_program(irradiance_compute_program_.program))
    {
        const auto& irradiance_tex = create_or_get_irradiance_texture(rview);

        struct skylight_params
        {
            float intensity = 0.0f;
            float sun_weight = 1.0f;
            float exposition = perez_luminance_to_engine;
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
        const float sun_intensity = get_skylight_params(scn, limits).sun_intensity;
        const auto sky = find_skylight_in_effect(*scn.registry);

        scn.registry->view<transform_component, skylight_component, active_component>().each(
            [&](auto e, auto&& transform_comp_ref, auto&& skylight_comp_ref, auto&& active)
            {
                if(e != sky.entity())
                    return;
                const auto& skylight = skylight_comp_ref;
                float irradiance_intensity = skylight.get_irradiance_intensity();
                if(irradiance_intensity <= 0.0f)
                    return;

                const auto& world_transform = transform_comp_ref.get_transform_global();
                math::vec3 light_dir = world_transform.z_unit_axis();
                math::vec3 irradiance_color = {1.0f, 1.0f, 1.0f};
                bool use_perez = false;
                irradiance_perez_params candidate_perez;
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
                    // The shared day/night ramp (perez_luminance.h): 0 at the horizon, 1 from
                    // ~20 degrees up - the ramp the sun-relative exposition fades with too.
                    sun_weight = compute_perez_sun_weight(-light_dir.y);
                }
                float exposition = perez_luminance_to_engine;

                if(!wants_sky)
                {
                    // Sky ignored: flat artist ambient straight from the tint color. Kept independent
                    // of sun elevation (sun_weight=1) and at unit exposition so the tint reads literally.
                    sun_weight = 1.0f;
                    exposition = 1.0f;
                }
                else if(!is_skybox)
                {
                    // Sky contributes: one full Perez projection either way. `directional` only
                    // decides whether the SH bake keeps all bands (mode 1) or truncates to the
                    // L0 average (mode 5) -- the flat ambient is the SAME integral by
                    // construction. This replaced an empirical CPU-side collapse
                    // (mix(sky, sun, sun_weight * 0.25), a hand-calibrated match) with math.
                    // Computed into a candidate-local struct: writing into dominant.perez here
                    // let any later-iterated skylight stomp the true dominant's Perez params.
                    use_perez = true;
                    compute_irradiance_perez_params(light_dir, skylight.get_turbidity(), sun_intensity, candidate_perez);
                    irradiance_color = glm::mix(candidate_perez.sky_luminance_rgb, candidate_perez.sun_luminance_rgb, sun_weight);
                    // The shared Perez -> engine conversion (perez_luminance.h); the sky dome
                    // pass uses this same value, so ambient and dome cannot drift apart.
                    exposition = candidate_perez.exposition;
                }
                // skybox + wants_sky: irradiance_color stays white; the cubemap supplies the color in-shader.

                float sky_brightness = skylight.get_sky_brightness();
                exposition *= sky_brightness;

                // Tint is a picker (sRGB) color; the Perez luminances it scales are linear.
                const auto tint = skylight.get_irradiance_tint().to_linear();
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
                    dominant.perez = candidate_perez;
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
        bgfx::setImage(0, irradiance_tex->native_handle(), 0, bgfx::Access::Write);

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
        // pre-baked in display range. The parity constant (perez_luminance.h) keeps the two
        // source types comparable at the same user-facing intensity slider - on the FIXED
        // conversion only: the sun-relative exposition already lands the slider's 1.0 on the
        // calibrated sky (the atmosphere's sky-to-sun ratio x the sun), a parity factor would double it.
        // The flat tint-only ambient is already in display range, so it gets no boost -- and
        // that includes the skybox-without-cubemap fallback (use_sky set but the texture
        // missing or still loading), which also renders the flat mode: gating on use_sky
        // boosted that fallback 2x and made the ambient pop when the cubemap finished loading.
        if(use_cubemap)
            ambient_vec[3] *= dominant.sky_brightness;
        else if(dominant.use_perez && !dominant.perez.sun_relative)
            ambient_vec[3] *= sky_ambient_cubemap_parity;

        gfx::set_uniform(irradiance_compute_program_.u_irradiance_tint_intensity, ambient_vec);

        // exposition: scale ambient to display range (matches atmospheric sky, ~0.1 at noon).
        float exp_val = dominant.exposition;
        float exp_vec[4] = {exp_val, 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(irradiance_compute_program_.u_exposition, exp_vec);

        // Only the cubemap modes read s_env, but the program declares it and D3D11 flags an empty
        // sampler slot on every dispatch, so the other modes bind a black cube.
        gfx::texture::ptr env_texture = default_textures::get().black_cube_texture();
        if(dominant.intensity > 0.0f && dominant.use_perez)
        {
            // mode 1 = full directional SH, mode 5 = flat (the SAME Perez integration
            // truncated to L0), mirroring the cubemap pair below.
            mode = dominant.directional ? 1 : 5;
            gfx::set_uniform(irradiance_compute_program_.u_sun_direction, dominant.perez.sun_direction);
            gfx::set_uniform(irradiance_compute_program_.u_sky_luminance_xyz, dominant.perez.sky_luminance_xyz);
            gfx::set_uniform(irradiance_compute_program_.u_perez_coeff, &dominant.perez.perez_coeff[0][0], 5);
        }
        else if(use_cubemap)
        {
            // mode 2 = full directional SH, mode 3 = flat (cubemap averaged into L0 only).
            mode = dominant.directional ? 2 : 3;
            env_texture = cubemap_tex;
        }
        else if(!dominant.use_sky && dominant.directional)
        {
            // No sky contribution but directional requested: hemisphere gradient from the tint
            // (full tint up -> darkened tint down). Flat tint-only stays at mode 0.
            mode = 4;
        }
        gfx::set_texture(irradiance_compute_program_.s_env, 1, env_texture);

        // Cloud coverage coupling: the Perez sky is blended toward an overcast grey by the mean
        // cloud transmittance (lowest mip of the cloud shadow map).
        const bool couple_clouds = dominant.use_perez && cloud_shadow_.valid && cloud_shadow_.map;
        gfx::set_texture(irradiance_compute_program_.s_cloudShadow,
                         2,
                         couple_clouds ? cloud_shadow_.map : default_textures::get().white_texture());

        // x=mode, y=sun_weight (applied in shader for all modes), z=cloud coverage coupling
        float mode_vec[4] = {float(mode), dominant.sun_weight, couple_clouds ? 1.0f : 0.0f, 0.0f};
        gfx::set_uniform(irradiance_compute_program_.u_mode, mode_vec);

        gfx::dispatch(irr_pass.id, irradiance_compute_program_.program->native_handle(), 1, 1, 1);
        irradiance_compute_program_.program->end();

        result.irradiance_tex = irradiance_tex;
    }
    else
    {
        // Without the compute pass the SH holds a flat ambient written from the CPU, whatever quality the sky asks for.
        const auto& irradiance_tex = create_or_get_irradiance_texture(rview);
        result.irradiance_tex = irradiance_tex;
        math::vec3 ambient(0.0f);
        const float sun_intensity = get_skylight_params(scn, limits).sun_intensity;
        const auto sky = find_skylight_in_effect(*scn.registry);
        scn.registry->view<transform_component, skylight_component, active_component>().each(
            [&](auto e, auto&& transform_comp_ref, auto&& skylight_comp_ref, auto&& active)
            {
                if(e == sky.entity() && skylight_comp_ref.get_irradiance_intensity() > 0.0f)
                {
                    ambient = compute_flat_ambient(skylight_comp_ref, transform_comp_ref.get_transform_global().z_unit_axis(), sun_intensity);
                }
            });
        write_flat_irradiance(irradiance_tex, ambient);
    }

    return result;
}

namespace
{
/**
 * The inverse of @p camera's camera-relative view-projection: the light passes rebuild each pixel's offset from the
 * camera through it (fs_pbr_lighting.sh u_invViewProjRelative), which keeps millimetres anywhere in the world. Inverted
 * in double, so the inversion adds no rounding of its own.
 */
auto get_inverse_view_projection_relative(const camera& camera) -> math::mat4
{
    return math::mat4(glm::inverse(glm::dmat4(camera.get_view_projection_relative().get_matrix())));
}
} // namespace

void deferred::run_direct_lighting_pass(scene& scn,
                                        const camera& camera,
                                        gfx::render_view& rview,
                                        bool apply_shadows,
                                        const feature_limits& limits,
                                        delta_t dt)
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

    light_submit_context context;
    context.view = pass.id;
    context.cam = &camera;
    context.gbuffer = gbuffer;
    context.buffer_size = buffer_size;
    context.pre_exposure = get_pre_exposure(rview);
    context.inv_view_proj_relative = get_inverse_view_projection_relative(camera);
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp_ref, auto&& light_comp_ref, auto&& active)
        {
            if(!light_comp_ref.is_enabled())
            {
                return;
            }
            const auto light = limit_light(light_comp_ref.get_light(), limits);
            auto world_transform = transform_comp_ref.get_transform_global();
            world_transform.reset_scale();
            const auto& generator = light_comp_ref.get_shadowmap_generator();
            const bool has_shadows = light.casts_shadows && apply_shadows && generator.has_shadow_map();
            submit_light(light,
                         world_transform,
                         light_comp_ref.get_linear_color(),
                         has_shadows ? &generator : nullptr,
                         context);
        });
    submit_particle_lights(scn, context);

    bgfx::discard();
}

void deferred::submit_light(const light& light,
                            const math::transform& world_transform,
                            const math::vec3& linear_color,
                            const shadow::shadowmap_generator* shadows,
                            const light_submit_context& context)
{
    const camera& camera = *context.cam;
    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();
    if(!ANONYMOUS::is_light_needed(light, linear_color))
    {
        return;
    }
    const auto& light_position = world_transform.get_position();
    const auto& light_direction = world_transform.z_unit_axis();
    const math::bsphere reach = light.compute_world_bounds_sphere(light_position, light_direction);
    if(!camera.get_frustum().test_sphere(reach))
    {
        return;
    }
    irect32_t rect(0,
                   0,
                   irect32_t::value_type(context.buffer_size.width),
                   irect32_t::value_type(context.buffer_size.height));
    if(light.type != light_type::directional &&
       math::compute_projected_sphere_rect(rect.left,
                                           rect.right,
                                           rect.top,
                                           rect.bottom,
                                           reach.position,
                                           reach.radius,
                                           camera_pos,
                                           view,
                                           proj) == 0)
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Direct Lighting Pass/Per Light");

    const bool has_shadows = shadows != nullptr;

    stats_.drawn_lights++;
    stats_.drawn_lights_casting_shadows += uint32_t(has_shadows);

    const auto& lprogram = has_shadows ? get_light_program(light) : get_light_program_no_shadows(light);

    lprogram.program->begin();

    const float contact_shadow_distance = light.contact_shadow.enabled
                                           ? light.contact_shadow.ray_length
                                           : 0.0f;
    // The dither advances only while TAA integrates it (aa_data.x is the temporal frame
    // index, 0 without TAA); wrapped so the noise offsets stay in float range.
    const float contact_frame = std::fmod(camera.get_aa_data().x, float(ANONYMOUS::contact_shadow_dither_frames));
    const float contact_shadow_uniform[4] = {light.contact_shadow.thickness,
                                             light.contact_shadow.max_distance,
                                             light.contact_shadow.opacity,
                                             contact_frame};

    if(light.type == light_type::directional)
    {
        float light_data[4] = {0.0f, 0.0f, 0.0f, contact_shadow_distance};

        gfx::set_uniform(lprogram.u_light_direction, light_direction);
        gfx::set_uniform(lprogram.u_light_data, light_data);
    }
    if(light.type == light_type::point)
    {
        float light_data[4] = {light.point_data.range, 0.0f, 0.0f, contact_shadow_distance};

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

    if(light.type != light_type::directional)
    {
        const auto source_axis = light.compute_source_axis(world_transform.y_unit_axis());
        const float light_source[4] = {source_axis.x, source_axis.y, source_axis.z, light.source_radius};
        gfx::set_uniform(lprogram.u_light_source, light_source);
    }

    gfx::set_uniform(lprogram.u_contact_shadow, contact_shadow_uniform);

    // Written with the view's pre-exposure, like every pass that lights the HDR buffers.
    float light_color_intensity[4] = {linear_color.x,
                                      linear_color.y,
                                      linear_color.z,
                                      light.intensity * context.pre_exposure.value};

    gfx::set_uniform(lprogram.u_light_color_intensity, light_color_intensity);

    gfx::set_uniform(lprogram.u_camera_position, camera_pos);
    gfx::set_uniform(lprogram.u_inv_view_proj_relative, context.inv_view_proj_relative);

    size_t i = 0;
    for(; i < context.gbuffer->get_attachment_count(); ++i)
    {
        gfx::set_texture(lprogram.s_tex[i], i, context.gbuffer->get_texture(i));
    }
    // Skip s_tex5 (RBUFFER) and s_tex6 (BRDF LUT) - not used by per-light direct shaders.
    if(has_shadows)
    {
        shadows->submit_uniforms(ANONYMOUS::light_shadow_map_stage, ANONYMOUS::light_shadow_compare_stage);
    }

    if(light.type == light_type::directional)
    {
        // A white fallback keeps the sampler bound when there is no layer; the enable flag skips the read.
        const auto cloud_shadow = ANONYMOUS::make_cloud_shadow_uniforms(cloud_shadow_);
        gfx::set_uniform(lprogram.u_cloudShadow, cloud_shadow.placement);
        gfx::set_uniform(lprogram.u_cloudShadow2, cloud_shadow.layer);
        gfx::set_texture(lprogram.s_cloudShadow,
                         ANONYMOUS::light_cloud_shadow_stage,
                         cloud_shadow.is_applied ? cloud_shadow_.map : default_textures::get().white_texture());
    }
    bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ADD);
    context.pre_exposure.bind(ANONYMOUS::light_pre_exposure_stage);
    bgfx::submit(context.view, lprogram.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);

    lprogram.program->end();
}

void deferred::submit_particle_lights(scene& scn, const light_submit_context& context)
{
    particle_lights_.clear();
    // Probes and captures leave particle lights out, as they do the particles: a passing flash is not baked.
    if(!current_run_->is_camera_run() || context.cam == nullptr)
    {
        return;
    }
    // Only emitters the views draw (billboards are marked by last frame's particle pass) light the scene.
    scn.registry->view<particle_emitter_component, active_component>().each(
        [&](auto e, auto&& emitter, auto&& active)
        {
            if(emitter.is_enabled() && emitter.get_light_count() > 0 && ps_soa::is_valid(emitter.get_emitter_handle()) &&
               emitter.was_used_last_frame() && is_on_rendered_layer(*scn.registry, e, current_run_->render_mask))
            {
                ps_soa::collect_particle_lights(emitter.get_emitter_handle(), particle_lights_);
            }
        });
    // Lights that add nothing or reach nothing in view do not take a place in the budget.
    const camera& view_camera = *context.cam;
    particle_lights_.erase(std::remove_if(particle_lights_.begin(),
                                          particle_lights_.end(),
                                          [&](const ps_soa::particle_light& particle)
                                          {
                                              if(!ANONYMOUS::is_adding_light(particle.intensity, particle.color))
                                              {
                                                  return true;
                                              }
                                              const math::vec3 reach(particle.range);
                                              return !view_camera.test_aabb(
                                                  math::bbox(particle.position - reach, particle.position + reach));
                                          }),
                           particle_lights_.end());
    // The brightest within the view's budget.
    const size_t count = math::min(particle_lights_.size(), ANONYMOUS::max_particle_lights_per_view);
    std::partial_sort(particle_lights_.begin(),
                      particle_lights_.begin() + std::ptrdiff_t(count),
                      particle_lights_.end(),
                      [](const ps_soa::particle_light& a, const ps_soa::particle_light& b)
                      {
                          return a.intensity > b.intensity;
                      });
    light point;
    point.type = light_type::point;
    point.casts_shadows = false;
    point.source_radius = 0.0f;
    point.source_length = 0.0f;
    for(size_t index = 0; index < count; ++index)
    {
        const auto& particle = particle_lights_[index];
        point.intensity = particle.intensity;
        point.point_data.range = particle.range;
        math::transform place;
        place.set_position(particle.position);
        submit_light(point, place, particle.color, nullptr, context);
    }
}

auto deferred::run_indirect_lighting_pass(const camera& camera,
                                          gfx::render_view& rview,
                                          const irradiance_pass_result& irradiance_result,
                                          bool apply_reflection,
                                          const feature_limits& limits,
                                          delta_t dt) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Indirect Lighting Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& lbuffer = rview.fbo_get("LBUFFER");

    gfx::render_pass pass("Indirect Lighting/Pass");
    pass.bind(lbuffer.get());
    pass.set_view_proj(view, proj);

    const bool samples_probe_inline = apply_reflection && inline_probe_.cubemap;
    const auto& defaults = default_textures::get();
    const gfx::texture::ptr traced_layer = apply_reflection ? get_traced_reflection_layer(rview) : defaults.black_texture();
    // Surface cache GI and SSIL produce the SAME quantity in the same units -- a hemispherical
    // indirect diffuse estimate plus the weight with which it replaces the environment probe --
    // so they feed one consumer slot and only one of them is used. The cache wins when present:
    // it sees geometry off screen and behind the camera, which SSIL cannot at any sample count.
    auto indirect_diffuse_tex = rview.tex_find("GI_RESOLVE");
    if(!indirect_diffuse_tex)
    {
        indirect_diffuse_tex = rview.tex_find("SSIL");
    }
    // The occlusion of both indirect terms: the screen-space AO, and the GTSO table for the
    // specular. The untraced reflection layer (PBUFFER) takes the full occlusion, the traced
    // layers in RBUFFER only the material AO's.
    const auto screen_ao = get_screen_ao_inputs(rview);
    // With all three screen inputs at their defaults the variant takes their values instead of fetching them.
    const bool has_screen_inputs = indirect_diffuse_tex || traced_layer != defaults.black_texture() ||
                                   screen_ao.texture != defaults.white_texture();
    const auto& full_program = samples_probe_inline ? indirect_lighting_probe_program_ : indirect_lighting_program_;
    const auto& lean_variants =
        samples_probe_inline ? indirect_lighting_probe_no_screen_inputs_ : indirect_lighting_no_screen_inputs_;
    const auto& iprogram =
        !has_screen_inputs && ANONYMOUS::is_program_loaded(lean_variants.base)
            ? ANONYMOUS::pick_indirect_lighting_variant(gbuffer_has_subsurface_, limits.detailed_indirect_lighting, lean_variants)
            : full_program;
    iprogram.program->begin();

    gfx::set_uniform(iprogram.u_camera_position, camera_pos);
    gfx::set_uniform(iprogram.u_inv_view_proj_relative, get_inverse_view_projection_relative(camera));

    size_t i = 0;
    for(; i < gbuffer->get_attachment_count(); ++i)
    {
        gfx::set_texture(iprogram.s_tex[i], i, gbuffer->get_texture(i));
    }
    gfx::set_texture(iprogram.s_tex[i], i, traced_layer);
    i++;
    gfx::set_texture(iprogram.s_tex[i], i, ibl_brdf_lut_.get());
    i++;
    gfx::set_texture(iprogram.s_irradiance, 7, irradiance_result.irradiance_tex ? irradiance_result.irradiance_tex : defaults.black_texture());

    // Transparent (alpha 0) fallback when both are disabled/absent so the shader's
    // mix(irradiance, ssil.rgb, ssil.a) collapses to the pure SH probe. The opaque-black
    // default (alpha 1) would instead force mix() to 0 and wipe out the ambient.
    gfx::set_texture(iprogram.s_ssil,
                     8,
                     indirect_diffuse_tex ? indirect_diffuse_tex : default_textures::get().transparent_texture());
    // Whether that slot carries a real estimate: with it the shader takes the resolve outright;
    // without it the environment SH answers (fs_pbr_lighting.sh, pbr_indirect). And whether the
    // estimate is SSIL's, whose rays resolved the screen-space visibility per pixel: it takes
    // no screen-space AO, where the GI resolve takes it below its probe lattice.
    const bool indirect_diffuse_is_ssil = indirect_diffuse_tex && !rview.tex_find("GI_RESOLVE");
    const float indirect_params[4] = {indirect_diffuse_tex ? 1.0f : 0.0f,
                                      indirect_diffuse_is_ssil ? 1.0f : 0.0f,
                                      screen_ao.multi_bounce_albedo_cap,
                                      get_gi_resolve_scale(rview)};
    gfx::set_uniform(iprogram.u_indirect_params, indirect_params);
    gfx::set_texture(iprogram.s_screen_ao, 9, screen_ao.texture);
    gfx::set_uniform(iprogram.u_screen_ao, screen_ao.params.data());
    if(samples_probe_inline)
    {
        gfx::set_texture(iprogram.s_probe_cube, 10, inline_probe_.cubemap);
        gfx::set_uniform(iprogram.u_probe_sphere, inline_probe_.sphere);
        gfx::set_uniform(iprogram.u_probe_sample, inline_probe_.sample);
        gfx::set_uniform(iprogram.u_probe_layer_params, probe_layer_inputs{}.params.data());
    }
    else
    {
        const auto probe_layer = get_probe_layer_inputs(rview, apply_reflection ? get_probe_reflection_layer(rview) : nullptr);
        gfx::set_texture(iprogram.s_probe_layer,
                         10,
                         probe_layer.texture ? probe_layer.texture : default_textures::get().black_texture());
        gfx::set_uniform(iprogram.u_probe_layer_params, probe_layer.params.data());
    }
    gfx::set_texture(iprogram.s_specular_occlusion, 12, default_textures::get().specular_occlusion().texture.get());
    const auto pre_exposure = get_pre_exposure(rview);
    gfx::set_uniform(iprogram.u_pre_exposure, pre_exposure.to_uniform().data());
    pre_exposure.bind(ANONYMOUS::indirect_pre_exposure_stage);
    

    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ADD);
    bgfx::submit(pass.id, iprogram.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);

    iprogram.program->end();

    bgfx::discard();

    return lbuffer;
}

void deferred::clear_traced_reflections(gfx::render_view& rview)
{
    const auto& rbuffer = rview.fbo_find("RBUFFER");
    if(!rbuffer)
    {
        return;
    }
    // The traced layers composite into RBUFFER from (0, 0, 0, 1): no traced radiance yet, and
    // the whole pixel left to the probe layer.
    gfx::render_pass traced_clear_pass("Reflections/Traced Clear");
    traced_clear_pass.bind(rbuffer.get());
    traced_clear_pass.clear(BGFX_CLEAR_COLOR, ANONYMOUS::reflection_traced_clear_rgba, 0.0f, 0);
    traced_reflections_cleared_ = true;
}

auto deferred::get_traced_reflection_layer(gfx::render_view& rview) const -> gfx::texture::ptr
{
    const auto& rbuffer = rview.fbo_find("RBUFFER");
    if(rbuffer && (traced_reflections_cleared_ || gi_reflections_written_))
    {
        return rbuffer->get_texture(0);
    }
    // Opaque black is the traced clear: no traced radiance, the whole pixel left to the probe layer.
    return default_textures::get().black_texture();
}

auto deferred::get_probe_reflection_layer(gfx::render_view& rview) const -> gfx::texture::ptr
{
    const auto& pbuffer = rview.fbo_find("PBUFFER");
    if(pbuffer && (probe_layer_written_ || gi_reflections_written_))
    {
        return pbuffer->get_texture(0);
    }
    // Transparent black is the probe layer's clear: no probe covers the pixel.
    return default_textures::get().transparent_texture();
}

void deferred::run_reflection_probe_pass(scene& scn,
                                         const camera& camera,
                                         gfx::render_view& rview,
                                         bool apply_probes,
                                         bool is_layer_read_by_gi,
                                         delta_t dt)
{
    probe_layer_written_ = false;
    inline_probe_ = {};
    if(!apply_probes)
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Reflection Probe Pass");

    std::vector<entt::entity> sorted_probes;

    // Collect the active probes that hold a bake: an unbaked probe's cubemap is uninitialized
    // memory, and a single NaN texel from it spreads across the frame through bloom.
    scn.registry->view<transform_component, reflection_probe_component, active_component>().each(
        [&](auto e, auto&& transform_comp_ref, auto&& probe_comp_ref, auto&& active)
        {
            if(probe_comp_ref.has_capture())
            {
                sorted_probes.emplace_back(e);
            }
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

                  // Environment probes first: the other probes blend over them.
                  if(lhs_probe.method != rhs_probe.method)
                  {
                      return lhs_probe.method < rhs_probe.method;
                  }

                  // The same method: larger ranges first, so the more local probes blend over them.
                  return lhs_probe.get_max_range() > rhs_probe.get_max_range();
              });

    // Without probes the layer stays empty; the indirect pass reads the same empty layer from a
    // constant texture, so the full-screen clear is skipped unless the GI reflections read it.
    if(sorted_probes.empty() && !is_layer_read_by_gi)
    {
        return;
    }

    // A single probe the layer would only pass on to the indirect pass is sampled there instead.
    if(sorted_probes.size() == 1 && !is_layer_read_by_gi && !is_probe_layer_debug_view())
    {
        inline_probe_ = make_inline_probe(scn, sorted_probes.front(), rview);
        if(inline_probe_.cubemap)
        {
            return;
        }
    }

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& camera_pos = camera.get_position();

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& pbuffer = rview.fbo_get("PBUFFER");

    const auto buffer_size = pbuffer->get_size();
    const auto pre_exposure = get_pre_exposure(rview);

    gfx::render_pass pass("Reflections/Buffer Pass");
    pass.bind(pbuffer.get());
    pass.set_view_proj(view, proj);
    pass.clear(BGFX_CLEAR_COLOR, 0, 0.0f, 0);
    probe_layer_written_ = true;

    // Render or process the sorted probes, unoccluded: the GI reflection trace reads this layer
    // as the open sky, and the indirect pass occludes it (ComposeIndirectSpecular).
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
            // PBUFFER holds pre-exposed reflections; the captured cubemaps are absolute radiance.
            float data1[4] = {mips, probe.intensity * pre_exposure.value, is_global_fallback ? 1.0f : 0.0f, source_validity};
            float capture[4] = {probe_comp_ref.get_apply_prefilter() ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};

            gfx::set_uniform(ref_probe_program->u_data0, data0);
            gfx::set_uniform(ref_probe_program->u_data1, data1);
            pre_exposure.bind();
            gfx::set_uniform(ref_probe_program->u_capture, capture);

            for(size_t i = 0; i < gbuffer->get_attachment_count(); ++i)
            {
                gfx::set_texture(ref_probe_program->s_tex[i], i, gbuffer->get_texture(i));
            }

            gfx::set_texture(ref_probe_program->s_tex_cube, 5, cubemap);

            bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
            auto topology = gfx::clip_quad(1.0f);
            // Over-blend, rgb premultiplied by the probe's weight and alpha the union of the
            // coverages (1 - (1 - a)(1 - b)): the indirect pass and the GI reflection sky
            // fallback read that alpha as how much of the pixel the probes answer, and fill the
            // rest with the environment. BGFX_STATE_BLEND_ALPHA on alpha squared it instead.
            bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                           BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
                                                          BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                          BGFX_STATE_BLEND_ONE,
                                                          BGFX_STATE_BLEND_INV_SRC_ALPHA));

            ref_probe_program->program->begin();
            bgfx::submit(pass.id, ref_probe_program->program->native_handle());
            bgfx::setState(BGFX_STATE_DEFAULT);
            ref_probe_program->program->end();
        }
    }

    bgfx::discard();
}

auto deferred::make_inline_probe(scene& scn, entt::entity probe, gfx::render_view& rview) const -> inline_probe
{
    auto& probe_comp = scn.registry->get<reflection_probe_component>(probe);
    const auto& settings = probe_comp.get_probe();
    const auto& cubemap = probe_comp.get_cubemap_prefiltered();
    if(settings.type != probe_type::sphere || !cubemap || !indirect_lighting_probe_program_.program ||
       !indirect_lighting_probe_program_.program->is_valid())
    {
        return {};
    }
    const auto& world_transform = scn.registry->get<transform_component>(probe).get_transform_global();
    const auto& scale = world_transform.get_scale();
    // The same probe the layer pass would draw (fs_sphere_reflection_probe: u_data0, u_data1, u_capture).
    inline_probe result;
    result.cubemap = cubemap;
    result.sphere = math::vec4(world_transform.get_position(),
                               math::max(scale.x, math::max(scale.y, scale.z)) * settings.sphere_data.range);
    result.sample = math::vec4(float(cubemap->info.numMips),
                               settings.intensity * get_pre_exposure(rview).value,
                               probe_comp.get_apply_prefilter() ? 1.0f : 0.0f,
                               1.0f);
    return result;
}

auto deferred::is_probe_layer_debug_view() const -> bool
{
    return debug_pass_ == debug_pass_ao_bent_normals || (debug_pass_ >= 0 && debug_pass_ < debug_pass_gbuffer_modes);
}

namespace
{
/// The cloud mode @p limits let a sky render: without volumetric clouds, volumetric ones render flat.
auto limit_cloud_mode(skylight_component::cloud_mode mode, const feature_limits& limits) -> skylight_component::cloud_mode
{
    if(!limits.volumetric_clouds && mode == skylight_component::cloud_mode::volumetric)
    {
        return skylight_component::cloud_mode::flat;
    }
    return mode;
}

/// Copies the skylight settings into the atmospheric pass parameters, the cloud mode as @p limits allow it (the
/// cloud shadow map and the sky must both render the limited mode). Returns false when the scene has no active
/// skylight. Only the sun direction depends on the directional light.
auto gather_skylight_params(scene& scn,
                            const feature_limits& limits,
                            atmospheric_pass_perez::run_params& params_perez,
                            atmospheric_pass_skybox::run_params& params_skybox,
                            skylight_component::sky_mode& mode) -> bool
{
    bool found_sun = false;
    const auto sky = find_skylight_in_effect(*scn.registry);
    scn.registry->view<transform_component, skylight_component, active_component>().each(
        [&](auto e, auto&& transform_comp_ref, auto&& light_comp_ref, auto&& active)
        {
            if(e != sky.entity())
            {
                return;
            }
            auto entity = scn.create_handle(e);

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

            params_perez.turbidity = light_comp_ref.get_turbidity();
            params_perez.sky_brightness = light_comp_ref.get_sky_brightness();
            params_perez.cloud_mode = static_cast<int>(limit_cloud_mode(light_comp_ref.get_cloud_mode(), limits));
            params_perez.cloud_coverage = light_comp_ref.get_cloud_coverage();
            params_perez.cloud_macro_variation = light_comp_ref.get_cloud_macro_variation();
            params_perez.cloud_base_altitude = light_comp_ref.get_cloud_base_altitude();
            params_perez.cloud_thickness = light_comp_ref.get_cloud_thickness();
            params_perez.cloud_size = light_comp_ref.get_cloud_size();
            params_perez.cloud_softness = light_comp_ref.get_cloud_softness();
            params_perez.cloud_detail_erode = light_comp_ref.get_cloud_detail_erode();
            params_perez.cloud_density = light_comp_ref.get_cloud_density();
            params_perez.cloud_shadow_strength = light_comp_ref.get_cloud_shadow_strength();
            params_perez.cloud_brightness = light_comp_ref.get_cloud_brightness();
            params_perez.cloud_world_space_altitude = light_comp_ref.get_cloud_world_space_altitude();
            params_perez.cloud_shadows = light_comp_ref.get_cloud_shadows();
            params_perez.cloud_shadow_opacity = light_comp_ref.get_cloud_shadow_opacity();
            params_perez.cloud_wind_offset = light_comp_ref.get_cloud_wind_offset();
            params_perez.cloud_time = light_comp_ref.get_cloud_time();

            if(auto light_comp = entity.template try_get<light_component>())
            {
                const auto& light = light_comp->get_light();

                if(light.type == light_type::directional)
                {
                    const auto& world_transform = transform_comp_ref.get_transform_global();
                    params_perez.light_direction = world_transform.z_unit_axis();
                }
            }
            params_skybox.sky_brightness = light_comp_ref.get_sky_brightness();
        });
    return found_sun;
}
} // namespace

auto deferred::get_skylight_params(scene& scn, const feature_limits& limits) -> const skylight_frame_params&
{
    const uint64_t frame = gfx::get_render_frame();
    auto& cached = skylight_params_;
    if(cached.frame == frame && cached.registry == scn.registry.get() && cached.limits == limits)
    {
        return cached;
    }
    cached = {};
    cached.frame = frame;
    cached.registry = scn.registry.get();
    cached.limits = limits;
    cached.sun_intensity = find_sun_luminous_intensity(scn);
    skylight_component::sky_mode mode{};
    cached.has_skylight = gather_skylight_params(scn, limits, cached.perez, cached.skybox, mode);
    cached.is_skybox = mode == skylight_component::sky_mode::skybox;
    cached.perez.sun_intensity = cached.sun_intensity;
    return cached;
}

void deferred::run_cloud_shadow_pass(scene& scn,
                                     const camera& camera,
                                     gfx::render_view& rview,
                                     const feature_limits& limits)
{
    cloud_shadow_ = {};
    // The map is an R8 target with mips the backend builds; without them the lights take no cloud shadow.
    if(!get_render_feature_support().is_available(render_feature::cloud_shadow))
    {
        return;
    }
    const auto& sky = get_skylight_params(scn, limits);
    if(!sky.has_skylight || sky.is_skybox)
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Cloud Shadow Pass");
    cloud_shadow_ = atmospheric_pass_perez_.run_cloud_shadow_pass(camera, rview, sky.perez);
}

void deferred::run_atmospherics_pass(const gfx::frame_buffer::ptr& target,
                                     scene& scn,
                                     const camera& camera,
                                     gfx::render_view& rview,
                                     const feature_limits& limits)
{
    APP_SCOPE_PERF("Rendering/Atmospheric Pass");

    const auto& sky = get_skylight_params(scn, limits);

    if(!sky.has_skylight)
    {
        return;
    }
    atmospheric_pass_perez::run_params params_perez = sky.perez;
    atmospheric_pass_skybox::run_params params_skybox = sky.skybox;
    const auto pre_exposure = get_pre_exposure(rview);
    params_perez.pre_exposure = pre_exposure;
    params_skybox.sky_brightness *= pre_exposure.value;
    params_skybox.pre_exposure = pre_exposure;
    const auto& viewport_size = camera.get_viewport_size();

    auto c = camera;
    c.set_projection_mode(projection_mode::perspective);

    auto lbuffer_depth = rview.fbo_get("LBUFFER_DEPTH");

    if(sky.is_skybox)
    {
        atmospheric_pass_skybox_.run(lbuffer_depth, c, rview, params_skybox);
    }
    else
    {
        // target is the color-only LBUFFER over the same texture: the cloud composite
        // renders into it because it samples the depth that LBUFFER_DEPTH attaches.
        atmospheric_pass_perez_.run(lbuffer_depth, target, c, rview, params_perez);
    }
}

namespace
{
/**
 * The sun the fog scatters: the strongest enabled directional light, as the lit particles pick it, with its shadow
 * cascades when @p use_shadow_maps (the run built them) and the light renders them as `limits` limit it. ESM maps
 * store exponential depth, which the fog's depth test cannot read: such a sun leaves the fog unshadowed.
 */
auto find_fog_sun(scene& scn, bool use_shadow_maps, const feature_limits& limits) -> fog_pass::sun_light
{
    fog_pass::sun_light sun;
    float strongest = -1.0f;
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp, auto&& active)
        {
            const auto light = limit_light(light_comp.get_light(), limits);
            if(!light_comp.is_enabled() || light.type != light_type::directional || light.intensity <= strongest)
            {
                return;
            }
            strongest = light.intensity;
            // The light travels along its z axis; the fog wants the direction toward it.
            sun.direction = -transform_comp.get_transform_global().z_unit_axis();
            sun.illuminance = light_comp.get_linear_color() * (light.intensity * light.volumetric_intensity);
            const auto& generator = light_comp.get_shadowmap_generator();
            const bool has_shadow = use_shadow_maps && light.casts_shadows && light.shadow_params.type != sm_impl::esm &&
                                    bgfx::isValid(generator.get_rt_texture(0));
            sun.shadow = has_shadow ? &generator : nullptr;
            sun.shadow_cascades = has_shadow ? std::uint32_t(light.directional_shadow_params.num_splits) : 0u;
            sun.shadow_depth_offset = light.shadow_params.depth == sm_depth::linear ? 0.5f : 0.0f;
        });
    return sun;
}

/// The enabled fog volumes @p camera sees.
auto gather_fog_volumes(scene& scn, const camera& camera) -> std::vector<fog_pass::fog_volume>
{
    std::vector<fog_pass::fog_volume> volumes;
    scn.registry->view<transform_component, fog_volume_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& volume_comp, auto&& active)
        {
            if(!volume_comp.enabled)
            {
                return;
            }
            const auto& world = transform_comp.get_transform_global();
            const math::vec3& extents = volume_comp.settings.extents;
            if(!camera.test_obb(math::bbox(-extents, extents), world))
            {
                return;
            }
            volumes.push_back({world, volume_comp.settings});
        });
    return volumes;
}

/// The map the fog may read for @p generator's light: its atlas tile, once this frame's casters are in it (a tile the
/// view did not resolve may hold another light's map since the atlas last moved).
auto find_fog_light_shadow(const shadow::shadowmap_generator& generator) -> const shadow::shadowmap_generator*
{
    return generator.get_atlas_tile() && generator.already_resolved() ? &generator : nullptr;
}

/// The enabled point and spot lights that scatter in the fog (volumetric intensity above 0).
auto gather_fog_local_lights(scene& scn, bool use_shadow_maps) -> std::vector<fog_pass::local_light>
{
    std::vector<fog_pass::local_light> lights;
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp, auto&& active)
        {
            const auto& light = light_comp.get_light();
            if(!light_comp.is_enabled() || light.type == light_type::directional || light.intensity <= 0.0f ||
               light.volumetric_intensity <= 0.0f)
            {
                return;
            }
            // Scale must not skew the light's axis.
            auto world = transform_comp.get_transform_global();
            world.reset_scale();
            fog_pass::local_light local;
            local.id = static_cast<std::uint32_t>(entt::to_integral(e));
            local.position = world.get_position();
            local.direction = world.z_unit_axis();
            local.is_spot = light.type == light_type::spot;
            local.range = local.is_spot ? light.spot_data.get_range() : light.point_data.range;
            // Half angles, as the direct lighting gets them.
            local.cos_inner = math::cos(math::radians(light.spot_data.get_inner_angle() * 0.5f));
            local.cos_outer = math::cos(math::radians(light.spot_data.get_outer_angle() * 0.5f));
            local.intensity = light_comp.get_linear_color() * (light.intensity * light.volumetric_intensity);
            if(use_shadow_maps)
            {
                local.shadow = find_fog_light_shadow(light_comp.get_shadowmap_generator());
            }
            lights.push_back(local);
        });
    return lights;
}
} // namespace

auto deferred::run_fog_pass(scene& scn,
                            const camera& camera,
                            gfx::render_view& rview,
                            const gfx::frame_buffer::ptr& target,
                            const run_params& rparams,
                            bool use_shadow_maps) -> fog_pass::shader_params
{
    // The fog integrates along rays from the camera's position, which an orthographic view does not have.
    if(!rparams.fill_fog_params || camera.get_projection_mode() != projection_mode::perspective)
    {
        fog_pass_.release_volumetric(rview);
        return {};
    }
    APP_SCOPE_PERF("Rendering/Fog Pass");
    fog_pass::run_params params;
    rparams.fill_fog_params(params);
    params.target = target;
    params.depth = rview.tex_find("DEPTH");
    params.environment = rview.tex_find("IRRADIANCE_SH");
    params.pre_exposure = get_pre_exposure(rview);
    params.sun = find_fog_sun(scn, use_shadow_maps, rparams.limits);
    // The same cloud shadow the directional light applies.
    const auto cloud_shadow = ANONYMOUS::make_cloud_shadow_uniforms(cloud_shadow_);
    params.clouds.map = cloud_shadow.is_applied ? cloud_shadow_.map : gfx::texture::ptr{};
    params.clouds.placement = cloud_shadow.placement;
    params.clouds.layer = cloud_shadow.layer;
    params.allows_volumetric = rparams.limits.volumetric_fog;
    params.volumes = gather_fog_volumes(scn, camera);
    if(params.config.volumetric && params.allows_volumetric)
    {
        params.local_lights = gather_fog_local_lights(scn, use_shadow_maps);
    }
    params.is_camera_cut = has_camera_cut(camera);
    return fog_pass_.run(camera, rview, params);
}

void deferred::run_ssr_pass(const camera& camera,
                            gfx::render_view& rview,
                            const run_params& rparams)
{
    if(!is_ssr_wanted(rparams))
    {
        ssr_pass_.release_resources(rview);
        return;
    }

    ssr_pass::run_params ssr_params;

    ssr_params.output = rview.fbo_get("RBUFFER");
    ssr_params.g_buffer = rview.fbo_get("GBUFFER");

    // Last frame's post-TAA linear scene color. Before the first snapshot exists the
    // fallback is BLACK, not LBUFFER: on frame 0 the LBUFFER was just created and has
    // not been written yet (its first clear happens in the direct lighting pass, which
    // runs AFTER SSR), so it would trace one frame of undefined GPU memory as radiance.
    auto prev_scene = rview.tex_find("PREV_SCENE_HDR");
    ssr_params.previous_frame = prev_scene ? prev_scene : default_textures::get().black_texture();
    // This frame's velocity buffer, handed to the pass explicitly (a valid texture IS the enable).
    ssr_params.velocity = rview.tex_find("VELOCITY");

    ssr_params.cam = &camera;

    if(rparams.fill_ssr_params)
    {
        rparams.fill_ssr_params(ssr_params);
    }

    // Content-lag signal for the temporal's release ceiling (see run_params) - held one
    // accumulation window past the last mover draw. Placed after fill_ssr_params so the
    // settings window is final.
    const uint64_t ssr_frame_now = gfx::get_render_frame();
    ssr_params.velocity_movers_recent =
        velocity_movers_frame_ != ~0ull && ssr_frame_now >= velocity_movers_frame_ &&
        ssr_frame_now - velocity_movers_frame_ <=
            uint64_t(math::max(ssr_params.settings.fidelityfx.temporal.max_accum_frames, 1));

    ssr_params.hiz_buffer = rview.tex_get("HIZBUFFER");
    ssr_params.pre_exposure = get_pre_exposure(rview);

    ssr_pass_.run(rview, ssr_params);
}

void deferred::run_screen_ao_pass(const camera& camera, gfx::render_view& rview, delta_t dt, const run_params& rparams)
{
    if(gi_short_range_ao_owns_view(rparams))
    {
        gtao_pass_.release_resources(rview);
        rview.tex_remove("GTAO");
        assao_pass_.release_resources(rview);
        return;
    }
    // GTAO takes precedence: with both volumes enabled only GTAO runs.
    if(run_gtao_pass(camera, rview, rparams))
    {
        assao_pass_.release_resources(rview);
        return;
    }
    run_assao_pass(camera, rview, dt, rparams);
}

auto deferred::run_gtao_pass(const camera& camera, gfx::render_view& rview, const run_params& rparams) -> bool
{
    if(!reflection_screen_stack_enabled(rparams) || !rparams.fill_gtao_params)
    {
        gtao_pass_.release_resources(rview);
        rview.tex_remove("GTAO");
        return false;
    }
    gtao_pass::run_params gtao_params;
    gtao_params.g_buffer = rview.fbo_get("GBUFFER");
    // This frame's velocity buffer and last frame's depth; a null texture disables the
    // respective temporal path inside the pass.
    gtao_params.velocity = rview.tex_find("VELOCITY");
    gtao_params.prev_depth = rview.tex_find("PREV_DEPTH");
    gtao_params.cam = &camera;
    rparams.fill_gtao_params(gtao_params);
    auto result = gtao_pass_.run(rview, gtao_params);
    if(!result)
    {
        rview.tex_remove("GTAO");
        return false;
    }
    rview.tex_acquire("GTAO") = result;
    rview.data().get_or_emplace<gtao_pass::settings>("GTAO_SETTINGS") = gtao_params.config;
    return true;
}

auto deferred::get_probe_layer_inputs(gfx::render_view& rview, const gfx::texture::ptr& pbuffer) const
    -> probe_layer_inputs
{
    probe_layer_inputs inputs;
    inputs.texture = pbuffer;
    if(!gi_reflections_written_ || !pbuffer)
    {
        return inputs;
    }
    // The GI reflections own the view: the gather's rough specular is the whole untraced layer (no reflection
    // probes or sky specular are composited under it), read straight from its history.
    const auto rough_specular = rview.tex_find("GI_ROUGH_SPECULAR");
    inputs.texture = rough_specular ? rough_specular : default_textures::get().black_texture();
    inputs.params = {1.0f, get_gi_resolve_scale(rview), rough_specular ? 1.0f : 0.0f, 0.0f};
    return inputs;
}

auto deferred::get_screen_ao_inputs(gfx::render_view& rview) const -> screen_ao_inputs
{
    screen_ao_inputs inputs;
    inputs.texture = default_textures::get().white_texture();
    inputs.params = {1.0f, 0.0f, 1.0f, 0.0f};
    // The GI's short-range AO when the gather produced it this frame: the diffuse takes the
    // visibility through the multi-bounce fit at the post-process intensity the gather
    // published with it (no bent normal: the ambient axis stays the normal), the untraced
    // specular the bent cone.
    const auto& gi_ao = rview.tex_find(gi_gather_pass::screen_ao_texture);
    using screen_ao_state = gi_gather_pass::screen_ao_state;
    const auto* gi_ao_state = rview.data().try_get<screen_ao_state>(screen_ao_state::view_key);
    if(gi_ao && gi_ao_state && gi_ao_state->frame == uint32_t(gfx::get_render_frame()))
    {
        inputs.texture = gi_ao;
        inputs.params = {gi_ao_state->intensity, 0.0f, 1.0f, 1.0f};
        inputs.multi_bounce_albedo_cap = gi::GI_SHORT_RANGE_AO_MAX_MULTIBOUNCE_ALBEDO;
        return inputs;
    }
    const auto& gtao_tex = rview.tex_find("GTAO");
    const auto* gtao_settings = rview.data().try_get<gtao_pass::settings>("GTAO_SETTINGS");
    if(gtao_tex && gtao_settings)
    {
        inputs.texture = gtao_tex;
        inputs.params = {gtao_settings->intensity,
                         gtao_settings->bent_normal_strength,
                         gtao_settings->multi_bounce ? 1.0f : 0.0f,
                         1.0f};
    }
    else if(auto assao_tex = assao_pass_.get_ao_texture(rview))
    {
        inputs.texture = assao_tex;
    }
    return inputs;
}

void deferred::run_ssil_pass(const camera& camera,
                             gfx::render_view& rview,
                             const run_params& rparams,
                             bool gi_active)
{
    // Under GI the indirect pass reads GI_RESOLVE, never SSIL: nothing would consume the result.
    if(gi_active || !reflection_screen_stack_enabled(rparams) || !rparams.fill_ssil_params)
    {
        ssil_pass_.release_resources(rview);
        rview.tex_remove("SSIL");
        rview.tex_remove("PREV_SSIL");
        return;
    }

    ssil_pass::run_params ssil_params;
    ssil_params.g_buffer = rview.fbo_get("GBUFFER");
    ssil_params.direct_lighting = rview.fbo_get("LBUFFER")->get_texture(0);
    ssil_params.prev_depth = rview.tex_find("PREV_DEPTH");
    // This frame's velocity buffer, handed to the pass explicitly (a valid texture IS the enable).
    ssil_params.velocity = rview.tex_find("VELOCITY");
    ssil_params.prev_ssil = rview.tex_find("PREV_SSIL");
    // This frame's environment SH (the irradiance pass runs before SSIL); used as the per-ray miss
    // fallback so escaped rays integrate the environment.
    ssil_params.irradiance_sh = rview.tex_find("IRRADIANCE_SH");
    ssil_params.cam = &camera;

    rparams.fill_ssil_params(ssil_params);

    ssil_params.hiz_buffer = rview.tex_get("HIZBUFFER");
    ssil_params.pre_exposure = get_pre_exposure(rview);

    auto result = ssil_pass_.run(rview, ssil_params);
    rview.tex_acquire("SSIL") = result;

    if(ssil_params.settings.enable_multi_bounce && result)
    {
        // 1:1 blit of the SSIL output into PREV_SSIL. The output is full-res when the
        // trace runs reduced-res (the joint-bilateral upsample pass already reconstructed
        // it edge-aware), so feeding it back is safe -- the old failure mode was a NAIVE
        // full-viewport upscale that bled bright indirect across depth boundaries. Sizing
        // PREV_SSIL to the result keeps the blit a 1:1 copy regardless of trace resolution.
        const auto& prev_ssil =
            rview.tex_acquire("PREV_SSIL",
                              {.size = result->get_size(),
                               .format = bgfx::TextureFormat::RGBA16F,
                               .flags = BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP});
        gfx::render_pass blit_pass("SSIL/Prev SSIL Blit Pass");
        bgfx::blit(blit_pass.id,
                   bgfx::TextureRegion{.handle = prev_ssil->native_handle()},
                   bgfx::TextureRegion{.handle = result->native_handle()});
    }
    else
    {
        rview.tex_remove("PREV_SSIL");
    }

}

} // namespace rendering
} // namespace unravel
