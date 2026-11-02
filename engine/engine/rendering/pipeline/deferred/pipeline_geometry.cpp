#include "pipeline_internal.h"

namespace unravel
{
namespace
{
namespace ANONYMOUS
{
/// Where pbr_material::write_instance_constants puts the subsurface color (u_subsurface_color in fs_deferred_geom.eshader).
constexpr size_t subsurface_instance_constant = 1;

/// Turns the subsurface color among a material's instance @p constants black, as a run without subsurface draws it.
/// The opacity only counts under a subsurface color.
void clear_subsurface_color(math::vec4* constants)
{
    auto& subsurface = constants[subsurface_instance_constant];
    subsurface = math::vec4(0.0f, 0.0f, 0.0f, subsurface.w);
}

/// @p Layout (instance_material_vertex_data, skinned_instance_material_vertex_data) with a black subsurface color,
/// for the batches of a run without subsurface.
template<typename Layout>
struct subsurface_free_layout : Layout
{
    subsurface_free_layout() = default;
    subsurface_free_layout(const batch_instance& instance, const material& batch_material)
        : Layout(instance, batch_material)
    {
        static_assert(sizeof(subsurface_free_layout) == sizeof(Layout), "packed into the instance buffer as Layout");
        clear_subsurface_color(this->constants);
    }
};

/// Material textures filter anisotropically, up to the device maximum (BGFX_RESET_MAXANISOTROPY): trilinear alone
/// picks the mip from the longer footprint axis and smears surfaces seen at grazing angles, such as distant floors.
constexpr std::uint32_t material_anisotropic_sampler_flags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;

/// The texture's own sampler state (wrap, border...) with anisotropic filtering, unless it asks for point sampling.
auto get_material_sampler_flags(const gfx::texture::ptr& texture) -> std::uint32_t
{
    if(!texture)
    {
        return material_anisotropic_sampler_flags;
    }
    const auto own_flags = static_cast<std::uint32_t>(texture->flags & BGFX_SAMPLER_BITS_MASK);
    const bool is_point = (own_flags & (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT)) != 0u;
    return is_point ? own_flags : own_flags | material_anisotropic_sampler_flags;
}
/// Material texture mip bias while TAA jitters the view: the jitter integrates the slightly sharper mip's
/// extra detail instead of aliasing it.
constexpr float material_taa_mip_bias = -0.3f;

/// The G-buffer's u_camera_clip_planes: near, far, and the material texture mip bias (TAA jitter active or not).
auto make_geometry_clip_planes(const camera& view_camera) -> math::vec4
{
    const bool is_jittered = view_camera.get_aa_data().y > 1.0f;
    return {view_camera.get_near_clip(), view_camera.get_far_clip(), is_jittered ? material_taa_mip_bias : 0.0f, 0.0f};
}
} // namespace ANONYMOUS
} // namespace

namespace rendering
{

namespace
{
auto create_or_resize_hiz_buffer(gfx::render_view& rview, const usize32_t& render_size) -> const gfx::texture::ptr&
{
    // R32F for precision, a full mip chain, compute-written and point-sampled with clamped UVs.
    return rview.tex_acquire("HIZBUFFER",
                             {.size = render_size,
                              .format = bgfx::TextureFormat::R32F,
                              .flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_MIN_POINT |
                                       BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP |
                                       BGFX_SAMPLER_V_CLAMP,
                              .has_mips = true});
}

// Velocity (motion vector) target, at the render size of the G-buffer it pairs with. RGBA16F: RG = total uv-delta
// (uv_curr - uv_prev), BA = the OBJECT-ONLY component (total minus the camera-induced
// part), both computed inside the velocity pass with one consistent matrix set.
//
// TWO framebuffers over the SAME color texture, and the split is load-bearing:
//  - "VELOCITY_FBO_CAMERA" is color-only, for the fullscreen camera sub-pass, which
//    SAMPLES the G-buffer depth. Rendering that sub-pass with DEPTH attached made the
//    same texture SRV and DSV of one draw - D3D11 silently unbinds the SRV and every
//    depth sample returns 0 (the NEAR PLANE), inflating the written camera velocity by
//    the near-plane parallax while keeping its direction plausible. That corruption
//    masqueraded as a cross-pass "previous view-projection mismatch" for three debugging
//    rounds (see tasks/velocity_buffer_plan.md).
//  - "VELOCITY_FBO" carries the shared DEPTH attachment for the movers sub-pass, which
//    depth-tests EQUAL and never samples depth - no conflict there.
auto create_or_resize_v_buffer(gfx::render_view& rview, const usize32_t& render_size)
    -> const gfx::frame_buffer::ptr&
{
    const auto& depth = rview.tex_find("DEPTH");

    const auto& tex = rview.tex_acquire("VELOCITY",
                                        {.size = render_size,
                                         .format = bgfx::TextureFormat::RGBA16F,
                                         .flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP});

    auto& camera_fbo = rview.fbo_acquire("VELOCITY_FBO_CAMERA");
    if(gfx::needs_recreate(camera_fbo, {tex}))
    {
        camera_fbo.reset();
        camera_fbo = std::make_shared<gfx::frame_buffer>();
        camera_fbo->populate({tex});
    }

    auto& fbo = rview.fbo_acquire("VELOCITY_FBO");
    if(gfx::needs_recreate(fbo, {tex, depth}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({tex, depth});
    }

    return fbo;
}
} // namespace

void deferred::submit_pbr_material(geom_program& program, const pbr_material& mat, bool keeps_subsurface)
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

    // The values an instanced draw reads per instance (pbr_material::write_instance_constants), here as uniforms.
    std::array<math::vec4, material::MAX_INSTANCE_CONSTANTS> constants{};
    mat.write_instance_constants(constants.data());
    if(!keeps_subsurface)
    {
        ANONYMOUS::clear_subsurface_color(constants.data());
    }
    const auto& tiling = mat.get_tiling();
    const auto& dither_threshold = mat.get_dither_threshold();
    const auto surface_data2 = mat.get_surface_data2();

    gfx::set_texture(program.s_tex_color, 0, albedo_tex, ANONYMOUS::get_material_sampler_flags(albedo_tex));
    gfx::set_texture(program.s_tex_normal, 1, normal_tex, ANONYMOUS::get_material_sampler_flags(normal_tex));
    gfx::set_texture(program.s_tex_roughness, 2, roughness_tex, ANONYMOUS::get_material_sampler_flags(roughness_tex));
    gfx::set_texture(program.s_tex_metalness, 3, metalness_tex, ANONYMOUS::get_material_sampler_flags(metalness_tex));
    gfx::set_texture(program.s_tex_ao, 4, ao_tex, ANONYMOUS::get_material_sampler_flags(ao_tex));
    gfx::set_texture(program.s_tex_emissive, 5, emissive_tex, ANONYMOUS::get_material_sampler_flags(emissive_tex));

    gfx::set_uniform(program.u_base_color, constants[0]);
    gfx::set_uniform(program.u_subsurface_color, constants[1]);
    gfx::set_uniform(program.u_emissive_color, constants[2]);
    gfx::set_uniform(program.u_surface_data, constants[3]);
    gfx::set_uniform(program.u_tiling, tiling);
    gfx::set_uniform(program.u_dither_threshold, dither_threshold);
    gfx::set_uniform(program.u_surface_data2, surface_data2);

    auto state = mat.get_render_states(true, true, true);

    bgfx::setState(state);
}

auto deferred::collect_visible_models(scene& scn,
                                      const camera& camera,
                                      const run_params& rparams,
                                      layer_mask render_mask,
                                      delta_t dt) -> visibility_set_models_t
{
    visibility_set_models_t visibility_set;
    if((rparams.pflags & pipeline_steps::geometry_pass) == 0u)
    {
        return visibility_set;
    }
    gather_visible_models(scn,
                          &camera,
                          rparams.vflags,
                          render_mask,
                          dt,
                          [&](entt::handle entity, const lod_data& lod_data)
                          {
                              visibility_set.emplace_back(visibility_data{entity, lod_data});
                          });
    return visibility_set;
}

void deferred::run_g_buffer_pass(const visibility_set_models_t& visibility_set,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 const feature_limits& limits,
                                 delta_t dt)
{
    APP_SCOPE_PERF("Rendering/G-Buffer Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& viewport_size = camera.get_viewport_size();

    const auto& gbuffer = rview.fbo_get(limits.subsurface ? "GBUFFER" : "GBUFFER_NO_SUBSURFACE");

    gfx::render_pass pass("G-Buffer/Pass");
    pass.clear();
    pass.set_view_proj(view, proj);
    pass.bind(gbuffer.get());

    // Clear batch collector for this frame
    batch_collector_.clear();
    skinned_batch_collector_.clear();
    source_runs_.clear();
    // The limits alone decide it: checking every drawn material would cost a scene with many of them on the CPU.
    gbuffer_has_subsurface_ = limits.subsurface;
    skinned_batching_run_ = is_skinned_batching_active();
    auto& skinning_palettes = engine::context().get_cached<renderer>().get_skinning_palettes();

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

        const auto current_time = lod_data.current_time;
        const auto current_lod_index = lod_data.current_lod_index;
        const auto target_lod_index = lod_data.target_lod_index;

        // Optimized single-component LOD transition parameters
        // Positive: current LOD fading out (1.0 -> 0.0)
        // Negative: target LOD fading in (0.0 -> -1.0)
        const float transition_progress = lod_data.transition_time > 0.0f 
            ? current_time / lod_data.transition_time 
            : 1.0f;
        
        const auto params = math::vec3{1.0f - transition_progress, 0.0f, 0.0f};      // Current LOD: positive, fading out
        const auto params_inv = math::vec3{-transition_progress, 0.0f, 0.0f};        // Target LOD: negative, fading in

        const auto& submesh_transforms = model_comp.get_submesh_transforms();
        const auto& bone_transforms = model_comp.get_bone_transforms();
        const auto& skinning_matrices = model_comp.get_skinning_transforms();

        model_comp.set_last_render_frame(gfx::get_render_frame());

        const auto extras = model_comp.get_submit_extras(false);

        // Check if this model can be batched (static mesh, no skinning). Movers stay
        // batched: their instances carry the previous-frame transform alongside the
        // current one, and the velocity pass re-rasterizes them with the IDENTICAL
        // instanced matrix math (vs_velocity_instanced mirrors vs_deferred_geom_instanced
        // bit for bit), so the EQUAL depth test holds without excluding them here.
        const bool is_skinned = !skinning_matrices.empty();
        // Skinned batching takes the skinned submeshes; the model's other submeshes then batch like a static model's.
        const bool batch_skinned = is_skinned && skinned_batching_run_;
        const bool can_batch = batch_collector::is_static_mesh_batching_enabled() && (!is_skinned || batch_skinned);

        if(batch_skinned)
        {
            // Movers carry their previous palettes for the velocity pass, which draws these batches again.
            const auto* prev_skinning = velocity_run_active_ && model_comp.has_motion()
                                            ? &model_comp.get_prev_skinning_transforms()
                                            : nullptr;
            const auto rows = skinning_palettes.acquire(entity, skinning_matrices, prev_skinning);
            bool is_drawn = model.submit_for_skinned_batching(skinned_batch_collector_,
                                                              world_transform,
                                                              submesh_transforms,
                                                              rows.current,
                                                              rows.previous,
                                                              current_lod_index,
                                                              params.x,
                                                              &view_frustum,
                                                              &camera,
                                                              extras);
            if(math::epsilonNotEqual(current_time, 0.0f, math::epsilon<float>()))
            {
                is_drawn = model.submit_for_skinned_batching(skinned_batch_collector_,
                                                             world_transform,
                                                             submesh_transforms,
                                                             rows.current,
                                                             rows.previous,
                                                             target_lod_index,
                                                             params_inv.x,
                                                             &view_frustum,
                                                             &camera,
                                                             extras) ||
                           is_drawn;
            }
            // Counted like the shadow path: a model whose every submesh was culled draws nothing.
            stats_.drawn_skinned_models += is_drawn ? 1 : 0;
        }

        if (can_batch)
        {
            // Movers attach their previous-frame transforms so the velocity pass's
            // instanced submit can pick their instances out of the shared batches.
            auto batch_extras = extras;
            if(velocity_run_active_ && model_comp.has_motion())
            {
                batch_extras.prev_world_transform = &model_comp.get_prev_world_transform();
                batch_extras.prev_submesh_transforms = &model_comp.get_prev_submesh_transforms();
            }
            // Collect this model for batching with appropriate transforms
            bool is_drawn = model.submit_for_batching(batch_collector_, world_transform, submesh_transforms, current_lod_index, params.x, &view_frustum, &camera, batch_extras);
            // Handle LOD transitions for batched models
            if(math::epsilonNotEqual(current_time, 0.0f, math::epsilon<float>()))
            {
                is_drawn = model.submit_for_batching(batch_collector_, world_transform, submesh_transforms, target_lod_index, params_inv.x, &view_frustum, &camera, batch_extras) ||
                           is_drawn;
            }
            // A skinned model counted as a skinned one above; a model whose every submesh was culled draws nothing.
            stats_.drawn_models += !is_skinned && is_drawn ? 1 : 0;
        }
        else
        {
            const auto camera_pos = camera.get_position();
            const auto clip_planes = ANONYMOUS::make_geometry_clip_planes(camera);
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
                        submit_pbr_material(prog, pbr, gbuffer_has_subsurface_);
                    }
                }

                bgfx::submit(pass.id,
                             prog.program->native_handle(),
                             0,
                             submit_params.preserve_state ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
            };
            callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
            {
                geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;

                prog.program->end();
            };

            // Render individually (skinned meshes, complex transforms, etc.). With skinned batching on, only the
            // model's other submeshes are left: no palettes = no skinned submeshes.
            const auto& individual_skinning = batch_skinned ? model::get_empty_skinning() : skinning_matrices;
            model.submit(world_transform,
                         submesh_transforms,
                         bone_transforms,
                         individual_skinning,
                         current_lod_index,
                         callbacks,
                         &view_frustum,
                         &camera,
                         extras);
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
                             individual_skinning,
                             target_lod_index,
                             callbacks,
                             &view_frustum,
                             &camera,
                             extras);
            }
        }
    }

    // Instanced sources (mesh particles, scripted instance draws): drawn with the batches whatever the batching setting.
    // A probe capture only keeps the frame's sources for the shadow maps it shares with the camera.
    has_source_instances_ = false;
    if(is_camera_run_)
    {
        for(const auto* source : instance_sources_)
        {
            source->collect_mesh_instances(batch_collector_, velocity_run_active_);
            source->collect_mesh_runs(source_runs_, instanced_layout::base);
            has_source_instances_ = true;
        }
    }

    // Submit all collected batches
    if (batch_collector::is_static_mesh_batching_enabled() || has_source_instances_)
    {
        submit_batched_geometry(pass, camera);
    }
    if(skinned_batching_run_)
    {
        submit_batched_skinned_geometry(pass, camera);
    }
    submit_source_runs(pass, camera);
    bgfx::discard();
}

namespace
{
/// The instance data of @p instance in a batch drawn with @p batch_material: a layout that carries per-instance
/// material values takes them from the instance's material, else the batch's.
template<typename InstanceData>
auto make_instance_data(const batch_instance& instance, const material& batch_material) -> InstanceData
{
    if constexpr(std::is_constructible_v<InstanceData, const batch_instance&, const material&>)
    {
        return InstanceData(instance, batch_material);
    }
    else
    {
        return InstanceData(instance);
    }
}

/**
 * Shared core of the instanced batch submits (G-buffer geometry and velocity movers).
 * Iterates the prepared batches, selects instances via @p should_include, packs the
 * selected ones DIRECTLY into a transient bgfx instance buffer as @p InstanceData
 * (which supplies packed_size() and a batch_instance conversion - two passes, count
 * then pack, so the all-instances case stays zero-copy and the filtered case allocates
 * exactly what it draws), binds the submesh buffers, and hands the draw to
 * @p submit_batch - program, uniform, state and stats differences live in the callers.
 * The instance count is clamped to what bgfx actually allocated, so a short transient
 * pool can never be overrun.
 */
template<typename InstanceData, typename BatchList, typename Filter, typename SubmitFn>
void submit_prepared_batches_instanced(const BatchList& prepared_batches,
                                       Filter&& should_include,
                                       SubmitFn&& submit_batch)
{
    for(const auto* batch : prepared_batches)
    {
        if(!batch->is_valid() || batch->instances.empty())
        {
            continue;
        }

        const auto mesh_ptr = batch->key.mesh_ptr;
        const auto material_ptr = batch->key.material_ptr;
        if(!mesh_ptr || !material_ptr)
        {
            continue;
        }

        const auto submesh = mesh_ptr->get_submesh(batch->key.submesh_index, batch->key.lod_index);
        if(!submesh)
        {
            continue;
        }

        uint32_t instance_count = 0;
        for(const auto& instance : batch->instances)
        {
            if(should_include(instance))
            {
                ++instance_count;
            }
        }
        if(instance_count == 0)
        {
            continue;
        }

        bgfx::InstanceDataBuffer instance_buffer;
        bgfx::allocInstanceDataBuffer(&instance_buffer,
                                      instance_count,
                                      static_cast<uint16_t>(InstanceData::packed_size()));
        if(!instance_buffer.data)
        {
            continue;
        }
        instance_count = std::min(instance_count, instance_buffer.num);

        auto* buffer_data = reinterpret_cast<InstanceData*>(instance_buffer.data);
        uint32_t packed = 0;
        for(const auto& instance : batch->instances)
        {
            if(packed >= instance_count)
            {
                break;
            }
            if(should_include(instance))
            {
                buffer_data[packed++] = make_instance_data<InstanceData>(instance, *material_ptr);
            }
        }

        mesh_ptr->bind_render_buffers_for_submesh(submesh, batch->key.lod_index);
        bgfx::setInstanceDataBuffer(&instance_buffer);

        submit_batch(*batch, *material_ptr, instance_count);
    }
}
} // namespace

template<typename PlainLayout, typename MaterialLayout, typename OnDraw>
void deferred::submit_prepared_geometry(batch_collector& collector,
                                        gfx::render_pass& pass,
                                        const camera& camera,
                                        geom_program& plain_program,
                                        geom_program& material_program,
                                        OnDraw&& on_draw)
{
    submit_context context;
    context.view_id = pass.id;
    context.camera_position = camera.get_position();
    // Opaque geometry needs no distance sorting.
    context.enable_distance_sorting = false;
    context.enable_profiling = true;
    collector.prepare_batches(context);
    const auto& prepared_batches = collector.get_prepared_batches();
    if(prepared_batches.empty())
    {
        return;
    }
    const auto camera_pos = camera.get_position();
    const auto clip_planes = ANONYMOUS::make_geometry_clip_planes(camera);
    // A batch whose instances carry per-instance material values (material instances sharing the draw) uses the
    // layout and program that read them; the others keep the matrix-only layout.
    plain_batches_.clear();
    material_value_batches_.clear();
    for(const auto* batch : prepared_batches)
    {
        (batch->has_material_overrides ? material_value_batches_ : plain_batches_).push_back(batch);
    }
    const auto draw_batches = [&](geom_program& program, const std::vector<const batch_group*>& batches, auto layout)
    {
        using instance_data = decltype(layout);
        if(batches.empty())
        {
            return;
        }
        program.program->begin();
        gfx::set_uniform(program.u_camera_wpos, camera_pos);
        gfx::set_uniform(program.u_camera_clip_planes, clip_planes);
        submit_prepared_batches_instanced<instance_data>(
            batches,
            [](const batch_instance&) { return true; },
            [&](const auto& /*batch*/, const material& mat, uint32_t instance_count)
            {
                const bool material_submitted = mat.submit(program.program.get());
                if(!material_submitted && mat.is<pbr_material>())
                {
                    submit_pbr_material(program, static_cast<const pbr_material&>(mat), gbuffer_has_subsurface_);
                }
                on_draw(program, instance_count);
                bgfx::submit(pass.id, program.program->native_handle(), 0, BGFX_DISCARD_ALL);
            });
        program.program->end();
    };
    draw_batches(plain_program, plain_batches_, PlainLayout{});
    if(gbuffer_has_subsurface_)
    {
        draw_batches(material_program, material_value_batches_, MaterialLayout{});
    }
    else
    {
        draw_batches(material_program, material_value_batches_, ANONYMOUS::subsurface_free_layout<MaterialLayout>{});
    }
    stats_.add_batch_stats(collector.get_stats());
}

void deferred::submit_batched_geometry(gfx::render_pass& pass, const camera& camera)
{
    APP_SCOPE_PERF("Rendering/Submit Batched Geometry");
    submit_prepared_geometry<instance_vertex_data, instance_material_vertex_data>(
        batch_collector_,
        pass,
        camera,
        geom_program_instanced_,
        geom_program_instanced_material_,
        [&](geom_program& program, uint32_t instance_count)
        {
            stats_.drawn_static_submeshes += instance_count;
            // No LOD transition: batched instances draw their LOD fully.
            const auto lod_params = math::vec3{0.0f, -1.0f, 1.0f};
            gfx::set_uniform(program.u_lod_params, lod_params);
        });

    // NOT cleared here: the velocity pass reuses the prepared batches (mover instances
    // carry their previous transforms) in the same frame. run_pipeline_impl clears the
    // collector at the end of the run, which also invalidates the transform pointers.
}

auto deferred::is_skinned_batching_active() const -> bool
{
    const auto is_usable = [](const auto& program) { return program.program && program.program->is_valid(); };
    return batch_collector::is_skinned_mesh_batching_enabled() && is_usable(geom_program_skinned_instanced_) &&
           is_usable(geom_program_skinned_instanced_material_) && is_usable(velocity_program_skinned_instanced_);
}

void deferred::submit_batched_skinned_geometry(gfx::render_pass& pass, const camera& camera)
{
    APP_SCOPE_PERF("Rendering/Submit Batched Skinned Geometry");
    auto& skinning_palettes = engine::context().get_cached<renderer>().get_skinning_palettes();
    submit_prepared_geometry<skinned_instance_vertex_data, skinned_instance_material_vertex_data>(
        skinned_batch_collector_,
        pass,
        camera,
        geom_program_skinned_instanced_,
        geom_program_skinned_instanced_material_,
        [&](geom_program& /*program*/, uint32_t instance_count)
        {
            stats_.drawn_skinned_submeshes += instance_count;
            skinning_palettes.bind();
        });
    // NOT cleared here: the velocity pass draws these batches again (submit_batched_skinned_velocity).
}

auto deferred::is_baked_animation_active() const -> bool
{
    const auto is_usable = [](const auto& program) { return program.program && program.program->is_valid(); };
    return get_render_feature_support().is_available(render_feature::skinned_mesh_batching) &&
           is_usable(geom_program_skinned_baked_instanced_) && is_usable(velocity_program_skinned_baked_instanced_);
}

namespace
{
/// Binds @p run's submesh and instances. Returns how many instances it draws: none past the end of the frame's
/// instance memory.
auto bind_instanced_run(const instanced_run& run) -> uint32_t
{
    if(run.first >= run.buffer->num)
    {
        return 0;
    }
    const auto& part = *run.part;
    const auto submesh = part.mesh_ptr->get_submesh(part.submesh, 0);
    if(!submesh)
    {
        return 0;
    }
    part.mesh_ptr->bind_render_buffers_for_submesh(submesh, 0);
    bgfx::setInstanceDataBuffer(run.buffer, run.first, run.count);
    return std::min(run.count, run.buffer->num - run.first);
}
} // namespace

void deferred::submit_source_runs(gfx::render_pass& pass, const camera& camera)
{
    if(source_runs_.empty())
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Submit Instanced Runs");
    source_runs_.merge();
    auto& baked_animations = engine::context().get_cached<rendering_system>().get_baked_animations();
    const auto camera_pos = camera.get_position();
    const auto clip_planes = ANONYMOUS::make_geometry_clip_planes(camera);
    // No LOD transition: packed instances draw their LOD fully.
    const auto lod_params = math::vec3{0.0f, -1.0f, 1.0f};
    const auto draw_runs = [&](geom_program& program, bool is_baked)
    {
        program.program->begin();
        gfx::set_uniform(program.u_camera_wpos, camera_pos);
        gfx::set_uniform(program.u_camera_clip_planes, clip_planes);
        for(const auto& run : source_runs_.get_runs())
        {
            if(run.part->is_baked != is_baked)
            {
                continue;
            }
            const uint32_t instance_count = bind_instanced_run(run);
            if(instance_count == 0)
            {
                continue;
            }
            const auto& mat = *run.part->material_ptr;
            const bool material_submitted = mat.submit(program.program.get());
            if(!material_submitted && mat.is<pbr_material>())
            {
                submit_pbr_material(program, static_cast<const pbr_material&>(mat), gbuffer_has_subsurface_);
            }
            if(is_baked)
            {
                stats_.drawn_skinned_submeshes += instance_count;
                baked_animations.bind(run.part->palette_offset, run.part->is_rigid);
            }
            else
            {
                stats_.drawn_static_submeshes += instance_count;
                gfx::set_uniform(program.u_lod_params, lod_params);
            }
            bgfx::submit(pass.id, program.program->native_handle(), 0, BGFX_DISCARD_ALL);
        }
        program.program->end();
    };
    draw_runs(geom_program_instanced_, false);
    if(is_baked_animation_active())
    {
        draw_runs(geom_program_skinned_baked_instanced_, true);
    }
}

auto deferred::is_velocity_wanted(const run_params& params) const -> bool
{
    // The step bit is the opt-out lever: probe captures build pflags from 0 and never set it, and a custom caller can
    // clear it.
    const bool is_camera_run = params.run_type == pipeline_run_type::camera;
    if(!is_camera_run || (params.pflags & pipeline_steps::velocity_pass) == 0u)
    {
        return false;
    }
    const bool has_temporal_effect = params.fill_taa_params || params.fill_gtao_params || params.fill_ssr_params ||
                                     params.fill_ssil_params || params.fill_gi_params;
    return has_temporal_effect || debug_pass_ == debug_pass_velocity || temporal_probe_pass_.is_busy();
}

void deferred::run_velocity_pass(const visibility_set_models_t& visibility_set,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 const usize32_t& render_size)
{
    if(!velocity_run_active_)
    {
        // Sole owner of the buffer's lifetime (the PREV_DEPTH convention): drop it as soon as
        // no consumer wants it instead of waiting for the idle collector.
        rview.fbo_remove("VELOCITY_FBO");
        rview.fbo_remove("VELOCITY_FBO_CAMERA");
        rview.tex_remove("VELOCITY");
        return;
    }
    if(!velocity_camera_program_.program || !velocity_camera_program_.program->is_valid())
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Velocity Pass");

    const auto& fbo = create_or_resize_v_buffer(rview, render_size);
    const auto& gbuffer = rview.fbo_get("GBUFFER");

    const auto& view = camera.get_view();
    // Jittered, deliberately: the movers sub-pass must rasterize exactly like the G-buffer for
    // the EQUAL depth test, and the fullscreen reconstruction mirrors the TAA formulation
    // (jittered current unprojection + unjittered previous reprojection, camera.h:267-275).
    const auto& proj = camera.get_projection();
    const auto prev_vp = camera.get_prev_view_projection_unjittered();

    // 1) Camera-derived velocity for every pixel, reconstructed from depth. Makes the buffer
    //    complete so consumers never branch on "does this pixel have object velocity".
    //    MUST render into the color-only FBO: this pass SAMPLES the depth texture, and with
    //    DEPTH attached the SRV/DSV conflict silently zeroes every depth read on D3D11
    //    (near-plane reconstruction -> inflated camera velocity; see create_or_resize_v_buffer).
    {
        const auto& camera_fbo = rview.fbo_get("VELOCITY_FBO_CAMERA");
        gfx::render_pass pass("Velocity/Camera Pass");
        pass.bind(camera_fbo.get());
        // Every pixel is drawn below: a clear instead of a load spares a tile-based GPU reading the old contents.
        pass.clear(BGFX_CLEAR_COLOR, 0, 1.0f, 0);
        pass.set_view_proj(view, proj);

        if(velocity_camera_program_.program->begin())
        {
            gfx::set_texture(velocity_camera_program_.s_depth,
                             0,
                             gbuffer->get_texture(4),
                             BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                                 BGFX_SAMPLER_MAG_POINT);
            gfx::set_uniform(velocity_camera_program_.u_prev_view_proj, prev_vp.get_matrix());

            const auto topology = gfx::clip_quad(1.0f);
            bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_ALWAYS);
            bgfx::submit(pass.id, velocity_camera_program_.program->native_handle());
            bgfx::setState(BGFX_STATE_DEFAULT);
            velocity_camera_program_.program->end();
        }
    }

    // 2) Movers over the top with true per-object motion. Depth EQUAL against the shared
    //    G-buffer depth also re-creates alpha-cutout coverage for free: texels the geometry
    //    FS discarded hold another surface's depth and fail the test here.
    if(velocity_program_.program && velocity_program_.program->is_valid() && velocity_program_skinned_.program &&
       velocity_program_skinned_.program->is_valid())
    {
        gfx::render_pass pass("Velocity/Geometry Pass");
        pass.bind(fbo.get());
        pass.set_view_proj(view, proj);

        const auto& view_frustum = camera.get_frustum();

        for(const auto& element : visibility_set)
        {
            const auto& entity = element.entity;
            auto& model_comp = entity.get<model_component>();
            if(!model_comp.has_motion())
            {
                continue;
            }
            const auto& model = model_comp.get_model();
            if(!model.is_valid())
            {
                continue;
            }
            // Mover stamp for the GI reflection temporal's gate, BEFORE the batched skip so
            // batched movers (drawn by submit_batched_velocity below) count too: the buffer
            // holds object velocity this frame, so reflected-content stillness readings are
            // trustworthy only under the gate's cap for the next temporal window.
            velocity_movers_frame_ = gfx::get_render_frame();
            // Batchable movers were collected into the shared batches with their previous
            // transforms and are drawn by submit_batched_velocity below - mirroring the
            // G-buffer's can_batch split exactly so nothing draws twice.
            const bool is_skinned = !model_comp.get_skinning_transforms().empty();
            const bool batch_skinned = is_skinned && skinned_batching_run_;
            const bool batched =
                batch_collector::is_static_mesh_batching_enabled() && (!is_skinned || batch_skinned);
            if(batched)
            {
                continue;
            }

            const auto& transform_comp = entity.get<transform_component>();
            const auto& world_transform = transform_comp.get_transform_global();
            const auto& submesh_transforms = model_comp.get_submesh_transforms();
            const auto& bone_transforms = model_comp.get_bone_transforms();
            // Batched skinned submeshes are submit_batched_skinned_velocity's; the model's other submeshes stay here.
            const auto& skinning_matrices =
                batch_skinned ? model::get_empty_skinning() : model_comp.get_skinning_transforms();

            auto extras = model_comp.get_submit_extras(false);
            extras.prev_world_transform = &model_comp.get_prev_world_transform();
            extras.prev_submesh_transforms = &model_comp.get_prev_submesh_transforms();
            extras.prev_skinning_transforms = &model_comp.get_prev_skinning_transforms();

            model::submit_callbacks callbacks;
            callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
            {
                velocity_geom_program& prog =
                    submit_params.skinned ? velocity_program_skinned_ : velocity_program_;
                prog.program->begin();
                gfx::set_uniform(prog.u_prev_view_proj, prev_vp.get_matrix());
            };
            callbacks.setup_params_per_submesh =
                [&](const model::submit_callbacks::params& submit_params, const material& mat)
            {
                velocity_geom_program& prog =
                    submit_params.skinned ? velocity_program_skinned_ : velocity_program_;
                // Match the material's cull so two-sided surfaces keep velocity coverage, but
                // own the depth/write bits: EQUAL test, no depth write, color only.
                const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
                bgfx::submit(pass.id,
                             prog.program->native_handle(),
                             0,
                             submit_params.preserve_state ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
            };
            callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
            {
                velocity_geom_program& prog =
                    submit_params.skinned ? velocity_program_skinned_ : velocity_program_;
                prog.program->end();
            };

            // Only the settled LOD, no crossfade second submit: dithered-out texels of the
            // fading LOD fail the EQUAL test and keep their camera-derived velocity, which is
            // a one-transition-long approximation not worth a second draw.
            model.submit(world_transform,
                         submesh_transforms,
                         bone_transforms,
                         skinning_matrices,
                         element.lod_data.current_lod_index,
                         callbacks,
                         &view_frustum,
                         &camera,
                         extras);
        }

        // Batched movers: instanced over the SAME prepared batches the G-buffer drew,
        // with the doubled per-instance stream (current + previous world matrix). Instanced sources are movers too.
        if(has_source_instances_)
        {
            velocity_movers_frame_ = gfx::get_render_frame();
        }
        if(batch_collector::is_static_mesh_batching_enabled() || has_source_instances_)
        {
            submit_batched_velocity(pass, prev_vp);
        }
        submit_batched_skinned_velocity(pass, prev_vp);
        submit_source_velocity_runs(pass, prev_vp);
        bgfx::discard();
    }
}

void deferred::submit_batched_velocity(gfx::render_pass& pass, const math::transform& prev_vp)
{
    if(!velocity_program_instanced_.program || !velocity_program_instanced_.program->is_valid())
    {
        return;
    }

    // The batches (and their prepare_batches sorting) are the ones submit_batched_geometry
    // built this frame; the collector is deliberately not cleared until the end of the run.
    const auto& prepared_batches = batch_collector_.get_prepared_batches();
    if(prepared_batches.empty())
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Velocity Batched Geometry");

    velocity_program_instanced_.program->begin();
    gfx::set_uniform(velocity_program_instanced_.u_prev_view_proj, prev_vp.get_matrix());

    submit_prepared_batches_instanced<velocity_instance_vertex_data>(
        prepared_batches,
        // Only mover instances carry a previous transform; batches without any skip free.
        [](const batch_instance& instance) { return instance.prev_world_transform_ptr != nullptr; },
        [&](const auto& /*batch*/, const material& mat, uint32_t /*instance_count*/)
        {
            // Match the material's cull (two-sided coverage), own the depth/write bits:
            // EQUAL test against the G-buffer depth, no depth write, color only.
            const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
            bgfx::submit(pass.id, velocity_program_instanced_.program->native_handle(), 0, BGFX_DISCARD_ALL);
        });

    velocity_program_instanced_.program->end();
}

void deferred::submit_batched_skinned_velocity(gfx::render_pass& pass, const math::transform& prev_vp)
{
    auto& program = velocity_program_skinned_instanced_;
    if(!program.program || !program.program->is_valid())
    {
        return;
    }
    // The batches submit_batched_skinned_geometry drew this run; their instances carry the previous palette rows.
    const auto& prepared_batches = skinned_batch_collector_.get_prepared_batches();
    if(prepared_batches.empty())
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Velocity Batched Skinned Geometry");

    auto& skinning_palettes = engine::context().get_cached<renderer>().get_skinning_palettes();
    program.program->begin();
    gfx::set_uniform(program.u_prev_view_proj, prev_vp.get_matrix());
    submit_prepared_batches_instanced<skinned_instance_vertex_data>(
        prepared_batches,
        // Every skinned model is a mover (model_component::has_motion).
        [](const batch_instance&) { return true; },
        [&](const auto& /*batch*/, const material& mat, uint32_t /*instance_count*/)
        {
            // Match the material's cull (two-sided coverage), own the depth/write bits: EQUAL test against the
            // G-buffer depth, no depth write, color only.
            const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
            skinning_palettes.bind();
            bgfx::submit(pass.id, program.program->native_handle(), 0, BGFX_DISCARD_ALL);
        });
    program.program->end();
}

void deferred::submit_source_velocity_runs(gfx::render_pass& pass, const math::transform& prev_vp)
{
    source_runs_.clear();
    for(const auto* source : instance_sources_)
    {
        source->collect_mesh_runs(source_runs_, instanced_layout::velocity);
    }
    if(source_runs_.empty())
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Velocity Instanced Runs");
    const bool is_baked_run = is_baked_animation_active();
    engine::context().get_cached<rendering_system>().get_instanced_draws().prepare(instanced_layout::velocity,
                                                                                    is_baked_run);
    source_runs_.merge();
    auto& baked_animations = engine::context().get_cached<rendering_system>().get_baked_animations();
    const auto draw_runs = [&](velocity_geom_program& program, bool is_baked)
    {
        if(!program.program || !program.program->is_valid())
        {
            return;
        }
        program.program->begin();
        gfx::set_uniform(program.u_prev_view_proj, prev_vp.get_matrix());
        for(const auto& run : source_runs_.get_runs())
        {
            if(run.part->is_baked != is_baked || bind_instanced_run(run) == 0)
            {
                continue;
            }
            // Match the material's cull (two-sided coverage), own the depth/write bits: EQUAL test against the
            // G-buffer depth, no depth write, color only.
            const auto& mat = *run.part->material_ptr;
            const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
            if(is_baked)
            {
                baked_animations.bind(run.part->palette_offset, run.part->is_rigid);
            }
            bgfx::submit(pass.id, program.program->native_handle(), 0, BGFX_DISCARD_ALL);
        }
        program.program->end();
    };
    draw_runs(velocity_program_instanced_, false);
    if(is_baked_run)
    {
        draw_runs(velocity_program_skinned_baked_instanced_, true);
    }
}

void deferred::run_hiz_pass(gfx::render_view& rview, const run_params& params, const usize32_t& viewport_size)
{
    // The GI's screen traces march this same pyramid, so GI with screen traces on is a producer
    // condition of its own, whether or not the screen-space reflection stack runs.
    gi_settings gi;
    const bool gi_wants_hiz = params.run_type == pipeline_run_type::camera && resolve_gi_settings(params, gi) &&
                              (gi.diffuse.screen_traces || (gi.reflections.enabled && gi.reflections.screen_traces));
    const bool want_hiz =
        ((reflection_screen_stack_enabled(params) && (params.fill_ssr_params || params.fill_ssil_params)) ||
         gi_wants_hiz) &&
        get_render_feature_support().is_available(render_feature::hiz);

    if(!want_hiz)
    {
        rview.tex_remove("HIZBUFFER");
        // PREV_DEPTH deliberately survives. It is a SHARED history resource with more than one
        // consumer -- the GI validates reprojected history against it -- and this pass
        // runs before them, so dropping it here would destroy the next consumer's input before
        // it runs. Its lifetime belongs to the one place that decides whether to produce it,
        // at the end of the frame.
        return;
    }

    create_or_resize_hiz_buffer(rview, viewport_size);

    APP_SCOPE_PERF("Rendering/SSR/Hi-Z Pass");

    const auto& gbuffer = rview.fbo_get("GBUFFER");
    if(!gbuffer)
    {
        return;
    }

    hiz_pass::run_params hp;
    hp.depth_buffer = gbuffer->get_texture(4);
    hp.output_hiz = rview.tex_get("HIZBUFFER");

    hiz_pass_.run(rview, hp);
}

} // namespace rendering
} // namespace unravel
