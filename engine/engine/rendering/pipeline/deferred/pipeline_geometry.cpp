#include "pipeline_internal.h"

#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/ecs/systems/reflection_probe_system.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>
#include <engine/rendering/pipeline/passes/gi_radiance_cache.h>

#include <limits>

namespace unravel
{
namespace
{
namespace ANONYMOUS
{
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
//    the near-plane parallax while keeping its direction plausible, which reads like a
//    cross-pass "previous view-projection mismatch".
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

auto deferred::get_g_buffer_graph(const material& mat) -> const graph_material*
{
    const auto* graph = mat.safe_cast<graph_material>();
    return graph != nullptr && !graph->is_forward() ? graph : nullptr;
}

auto deferred::submit_geometry_material(geom_program& program, const material& mat) -> gfx::program::handle_type_t
{
    if(const auto* graph = get_g_buffer_graph(mat))
    {
        if(auto* graph_program = graph_material_programs_.begin(*graph, program.vertex_variant, program.graph_fragment_variant))
        {
            draws_animated_offsets_ = draws_animated_offsets_ || graph->is_world_position_offset_animated();
            submit_graph_material(program, *graph);
            return graph_program->native_handle();
        }
    }
    // The stand-in (fs_deferred_geom.eshader) draws the default surface with the material's sidedness.
    bgfx::setState(mat.get_render_states(true, true, true));
    return program.program->native_handle();
}

auto deferred::draws_instance_values(geom_program& program, const material& mat) -> bool
{
    const auto* graph = get_g_buffer_graph(mat);
    return graph == nullptr || graph_material_programs_.begin(*graph, program.vertex_variant, program.graph_fragment_variant) != nullptr;
}

void deferred::submit_graph_material(geom_program& program, const graph_material& mat)
{
    graph_material_programs_.submit_values(mat);
    bgfx::setState(mat.get_render_states(true, true, true));
}

auto deferred::begin_offset_velocity_program(const material& mat, const std::string& vertex_variant, const math::vec3& camera_position)
    -> gpu_program*
{
    // Most draws are of other materials: they name no velocity variant.
    const auto* graph = mat.safe_cast<graph_material>();
    if(graph == nullptr || !graph->uses_world_position_offset())
    {
        return nullptr;
    }
    return graph_material_programs_.begin_moved_vertices(mat,
                                                         graph_material_programs::get_velocity_variant(vertex_variant),
                                                         velocity_fragment_shader_,
                                                         camera_position);
}

namespace
{
/// Whether a material @p mdl draws with moves its vertices by a World Position Offset that animates: the model moves
/// though it stands still (graph_material::is_world_position_offset_animated).
auto has_animated_offset(const model& mdl) -> bool
{
    bool is_animated = false;
    mdl.for_each_loaded_render_material(
        [&is_animated](const material& mat)
        {
            const auto* graph = mat.safe_cast<graph_material>();
            is_animated = is_animated || (graph != nullptr && graph->is_world_position_offset_animated());
        });
    return is_animated;
}

/// Whether the batches of @p mat draw every instance in the velocity pass: its World Position Offset animates.
auto is_offset_animated(const material* mat) -> bool
{
    const auto* graph = mat != nullptr ? mat->safe_cast<graph_material>() : nullptr;
    return graph != nullptr && graph->is_world_position_offset_animated();
}
} // namespace

auto deferred::collect_visible_models(scene& scn,
                                      const camera& camera,
                                      const run_params& rparams,
                                      layer_mask render_mask,
                                      delta_t dt) -> visibility_set_models_t
{
    visibility_set_models_t visibility_set;
    if(!runs_step(rparams, geometry_pass))
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

void deferred::run_g_buffer_pass(hpp::span<const visibility_data> visibility_set,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 const feature_limits& limits,
                                 delta_t dt,
                                 g_buffer_phase phase)
{
    APP_SCOPE_PERF("Rendering/G-Buffer Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& viewport_size = camera.get_viewport_size();

    const auto& gbuffer = rview.fbo_get(limits.subsurface ? "GBUFFER" : "GBUFFER_NO_SUBSURFACE");

    const bool is_first_phase = phase == g_buffer_phase::first;
    gfx::render_pass pass(is_first_phase ? "G-Buffer/Pass" : "G-Buffer/After Decals");
    if(is_first_phase)
    {
        pass.clear();
    }
    pass.set_view_proj(view, proj);
    pass.bind(gbuffer.get());

    if(is_first_phase)
    {
        // Clear batch collector for this frame
        batch_collector_.clear();
        skinned_batch_collector_.clear();
        source_runs_.clear();
        decal_receiver_batches_.clear();
        decal_receiver_skinned_batches_.clear();
        translucent_models_.clear();
        translucent_reads_.clear();
        // The limits alone decide it: checking every drawn material would cost a scene with many of them on the CPU.
        gbuffer_has_subsurface_ = limits.subsurface;
        skinned_batching_run_ = is_skinned_batching_active();
        draws_animated_offsets_ = false;
        graph_material_programs_.release_unused();
    }
    else
    {
        // The first phase's batches are drawn: they wait for the velocity pass while this phase collects its own. A
        // swap with the empty stash, which the first phase cleared, leaves both collectors whole.
        std::swap(decal_receiver_batches_, batch_collector_);
        std::swap(decal_receiver_skinned_batches_, skinned_batch_collector_);
    }
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

        // Surfaces of the translucency pass leave the G-buffer; a model that has some is drawn again there.
        bool has_forward = false;
        auto extras = model_comp.get_submit_extras(false);
        extras.material_pass = model_material_pass::deferred;
        extras.found_forward = &has_forward;

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
                const auto program_handle = submit_geometry_material(prog, mat);
                bgfx::submit(pass.id,
                             program_handle,
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
        if(has_forward)
        {
            translucent_models_.push_back(element);
            translucent_reads_.push_back(get_translucent_reads(element));
        }
    }

    // Instanced sources (mesh particles, scripted instance draws): drawn with the batches whatever the batching setting.
    // A probe capture only keeps the frame's sources for the shadow maps it shares with the camera.
    if(is_first_phase)
    {
        has_source_instances_ = false;
    }
    if(is_first_phase && current_run_->is_camera_run())
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
        const bool draws_values =
            batch->has_material_overrides && batch->key.material_ptr && draws_instance_values(material_program, *batch->key.material_ptr);
        (draws_values ? material_value_batches_ : plain_batches_).push_back(batch);
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
                const auto program_handle = submit_geometry_material(program, mat);
                on_draw(program, instance_count);
                bgfx::submit(pass.id, program_handle, 0, BGFX_DISCARD_ALL);
            });
        program.program->end();
    };
    draw_batches(plain_program, plain_batches_, PlainLayout{});
    draw_batches(material_program, material_value_batches_, MaterialLayout{});
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
    return is_usable(geom_program_skinned_baked_instanced_) && is_usable(velocity_program_skinned_baked_instanced_);
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
            const auto program_handle = submit_geometry_material(program, *run.part->material_ptr);
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
            bgfx::submit(pass.id, program_handle, 0, BGFX_DISCARD_ALL);
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
    if(!is_camera_run(params) || !runs_step(params, velocity_pass))
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
        const auto camera_position = camera.get_position();

        for(const auto& element : visibility_set)
        {
            const auto& entity = element.entity;
            auto& model_comp = entity.get<model_component>();
            const auto& model = model_comp.get_model();
            if(!model.is_valid())
            {
                continue;
            }
            // A still model moves too while a World Position Offset of its materials animates.
            if(!model_comp.has_motion() && !(draws_animated_offsets_ && has_animated_offset(model)))
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
            extras.material_pass = model_material_pass::deferred;
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
                // A material whose World Position Offset moves the vertices draws with its own vertex shader.
                auto* offset_program = begin_offset_velocity_program(mat,
                                                                     submit_params.skinned ? "skinned" : gfx::shader::DEFAULT_VARIANT,
                                                                     camera_position);
                // Match the material's cull so two-sided surfaces keep velocity coverage, but
                // own the depth/write bits: EQUAL test, no depth write, color only.
                const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
                bgfx::submit(pass.id,
                             offset_program != nullptr ? offset_program->native_handle() : prog.program->native_handle(),
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
            submit_batched_velocity(pass, prev_vp, camera_position);
        }
        submit_batched_skinned_velocity(pass, prev_vp, camera_position);
        submit_source_velocity_runs(pass, prev_vp, camera_position);
        bgfx::discard();
    }
}

void deferred::submit_batched_velocity(gfx::render_pass& pass, const math::transform& prev_vp, const math::vec3& camera_position)
{
    if(!velocity_program_instanced_.program || !velocity_program_instanced_.program->is_valid())
    {
        return;
    }

    // The batches (and their prepare_batches sorting) are the ones submit_batched_geometry
    // built this frame, in both G-buffer phases; the collectors are not cleared until the end of the run.
    // The batches of a material whose World Position Offset animates draw every instance: a still one moves too.
    mover_batches_.clear();
    offset_batches_.clear();
    for(const auto* collector : {&decal_receiver_batches_, &batch_collector_})
    {
        for(const auto* batch : collector->get_prepared_batches())
        {
            (draws_animated_offsets_ && is_offset_animated(batch->key.material_ptr.get()) ? offset_batches_ : mover_batches_).push_back(batch);
        }
    }
    if(mover_batches_.empty() && offset_batches_.empty())
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Velocity Batched Geometry");

    velocity_program_instanced_.program->begin();
    gfx::set_uniform(velocity_program_instanced_.u_prev_view_proj, prev_vp.get_matrix());
    const auto submit_batch = [&](const auto& /*batch*/, const material& mat, uint32_t /*instance_count*/)
    {
        auto* offset_program = begin_offset_velocity_program(mat, "instanced", camera_position);
        // Match the material's cull (two-sided coverage), own the depth/write bits:
        // EQUAL test against the G-buffer depth, no depth write, color only.
        const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
        const auto program = offset_program != nullptr ? offset_program->native_handle() : velocity_program_instanced_.program->native_handle();
        bgfx::submit(pass.id, program, 0, BGFX_DISCARD_ALL);
    };
    submit_prepared_batches_instanced<velocity_instance_vertex_data>(
        mover_batches_,
        // Only mover instances carry a previous transform; batches without any skip free.
        [](const batch_instance& instance) { return instance.prev_world_transform_ptr != nullptr; },
        submit_batch);
    // Still instances draw at their transform of both frames (velocity_instance_vertex_data).
    submit_prepared_batches_instanced<velocity_instance_vertex_data>(
        offset_batches_,
        [](const batch_instance&) { return true; },
        submit_batch);

    velocity_program_instanced_.program->end();
}

void deferred::submit_batched_skinned_velocity(gfx::render_pass& pass, const math::transform& prev_vp, const math::vec3& camera_position)
{
    auto& program = velocity_program_skinned_instanced_;
    if(!program.program || !program.program->is_valid())
    {
        return;
    }
    // The batches submit_batched_skinned_geometry drew this run, in both G-buffer phases; their instances carry the
    // previous palette rows.
    std::vector<const batch_group*> prepared_batches(decal_receiver_skinned_batches_.get_prepared_batches().begin(),
                                                     decal_receiver_skinned_batches_.get_prepared_batches().end());
    prepared_batches.insert(prepared_batches.end(),
                            skinned_batch_collector_.get_prepared_batches().begin(),
                            skinned_batch_collector_.get_prepared_batches().end());
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
            auto* offset_program = begin_offset_velocity_program(mat, "skinned_instanced", camera_position);
            const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
            skinning_palettes.bind();
            bgfx::submit(pass.id,
                         offset_program != nullptr ? offset_program->native_handle() : program.program->native_handle(),
                         0,
                         BGFX_DISCARD_ALL);
        });
    program.program->end();
}

void deferred::submit_source_velocity_runs(gfx::render_pass& pass, const math::transform& prev_vp, const math::vec3& camera_position)
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
            auto* offset_program = begin_offset_velocity_program(mat, is_baked ? "skinned_baked_instanced" : "instanced", camera_position);
            const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL | cull_state);
            if(is_baked)
            {
                baked_animations.bind(run.part->palette_offset, run.part->is_rigid);
            }
            bgfx::submit(pass.id,
                         offset_program != nullptr ? offset_program->native_handle() : program.program->native_handle(),
                         0,
                         BGFX_DISCARD_ALL);
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
    const bool gi_wants_hiz = is_camera_run(params) && resolve_gi_settings(params, gi) &&
                              (gi.diffuse.screen_traces || (gi.reflections.enabled && gi.reflections.screen_traces));
    const bool want_hiz =
        ((reflection_screen_stack_enabled(params) && (params.fill_ssr_params || params.fill_ssil_params)) ||
         gi_wants_hiz) &&
        get_render_feature_support().is_available(render_feature::hiz);
    const auto& gbuffer = rview.fbo_find("GBUFFER");

    // The texture existing is what the run's readers take as built, so no pyramid means no texture.
    if(!want_hiz || !gbuffer)
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

    hiz_pass::run_params hp;
    hp.depth_buffer = gbuffer->get_texture(4);
    hp.output_hiz = rview.tex_get("HIZBUFFER");

    hiz_pass_.run(rview, hp);
}

auto deferred::collect_translucent_draws(const run_context& run) -> translucent_mesh_list
{
    translucent_mesh_list list;
    translucent_order_.clear();
    translucent_order_independent_.clear();
    translucent_velocity_.clear();
    translucent_probes_.clear();
    if(translucent_models_.empty())
    {
        return list;
    }
    APP_SCOPE_PERF("Rendering/Translucency/Collect");
    stats_.drawn_translucent_models += uint32_t(translucent_models_.size());
    const math::vec3 eye = run.cam.get_position();
    std::vector<translucent_mesh_list::key> keys;
    keys.reserve(translucent_models_.size());
    for(size_t i = 0; i < translucent_models_.size(); ++i)
    {
        const auto& model_comp = translucent_models_[i].entity.get<model_component>();
        keys.push_back({model_comp.get_sort_priority(), math::distance2(eye, model_comp.get_world_bounds().get_center())});
        const auto& reads = translucent_reads_[i];
        if(reads.has_sorted)
        {
            translucent_order_.push_back(i);
        }
        if(reads.has_order_independent)
        {
            translucent_order_independent_.push_back(i);
        }
        if(reads.writes_velocity)
        {
            translucent_velocity_.push_back(i);
        }
        list.has_depth_prepass = list.has_depth_prepass || reads.has_depth_prepass;
        list.scene_textures |= reads.scene_textures;
    }
    std::stable_sort(translucent_order_.begin(),
                     translucent_order_.end(),
                     [&keys](size_t a, size_t b)
                     {
                         return is_drawn_before(keys[a], keys[b]);
                     });
    list.keys.reserve(translucent_order_.size());
    for(const size_t index : translucent_order_)
    {
        list.keys.push_back(keys[index]);
    }
    // A probe capture's surfaces reflect no probe: one would read the cube being written.
    if(run.is_camera_run())
    {
        const bool gi_owns_reflections = gi_reflections_own_view(run.params);
        run.scn.registry->view<transform_component, reflection_probe_component, active_component>().each(
            [&](auto e, auto&& transform_comp, auto&& probe_comp, auto&& active)
            {
                if(probe_comp.has_capture() && probe_comp.get_cubemap_prefiltered() &&
                   reflection_probe_system::is_used_in_view(probe_comp.get_probe(), gi_owns_reflections))
                {
                    translucent_probes_.push_back(e);
                }
            });
    }
    translucent_model_probes_.clear();
    translucent_model_probes_.reserve(translucent_models_.size());
    for(const auto& element : translucent_models_)
    {
        translucent_model_probes_.push_back(find_forward_probe(run, element.entity.get<model_component>().get_world_bounds().get_center()));
    }
    list.draw = [this, &run](bgfx::ViewId view, size_t index)
    {
        draw_translucent_model(view, translucent_order_[index], run, false);
    };
    list.has_order_independent = !translucent_order_independent_.empty();
    if(list.has_order_independent)
    {
        list.order_independent_key = get_order_independent_key(eye);
    }
    list.draw_order_independent = [this, &run](bgfx::ViewId view)
    {
        for(const size_t index : translucent_order_independent_)
        {
            draw_translucent_model(view, index, run, true);
        }
    };
    if(!translucent_velocity_.empty())
    {
        list.draw_velocity = [this, &run](bgfx::ViewId view)
        {
            for(const size_t index : translucent_velocity_)
            {
                draw_translucent_velocity(view, translucent_models_[index], run);
            }
        };
    }
    return list;
}

void deferred::draw_translucent_velocity(bgfx::ViewId view, const visibility_data& element, const run_context& run)
{
    const auto& entity = element.entity;
    if(!entity.valid() || !entity.all_of<model_component, transform_component>() || !velocity_program_.program ||
       !velocity_program_skinned_.program)
    {
        return;
    }
    auto& model_comp = entity.get<model_component>();
    const auto& model = model_comp.get_model();
    if(!model.is_valid())
    {
        return;
    }
    const auto& camera = run.cam;
    const auto camera_position = camera.get_position();
    const auto prev_vp = camera.get_prev_view_projection_unjittered();
    auto extras = model_comp.get_submit_extras(false);
    extras.material_pass = model_material_pass::forward;
    extras.prev_world_transform = &model_comp.get_prev_world_transform();
    extras.prev_submesh_transforms = &model_comp.get_prev_submesh_transforms();
    extras.prev_skinning_transforms = &model_comp.get_prev_skinning_transforms();

    model::submit_callbacks callbacks;
    callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
    {
        velocity_geom_program& prog = submit_params.skinned ? velocity_program_skinned_ : velocity_program_;
        prog.program->begin();
        gfx::set_uniform(prog.u_prev_view_proj, prev_vp.get_matrix());
    };
    callbacks.setup_params_per_submesh = [&](const model::submit_callbacks::params& submit_params, const material& mat)
    {
        const uint8_t discard = submit_params.preserve_state ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL;
        const auto* graph = mat.safe_cast<graph_material>();
        if(graph == nullptr || !writes_translucent_velocity(*graph))
        {
            bgfx::discard(discard);
            return;
        }
        velocity_geom_program& prog = submit_params.skinned ? velocity_program_skinned_ : velocity_program_;
        // A World Position Offset moves the vertices in the material's own vertex shader, as in the velocity pass.
        auto* offset_program =
            begin_offset_velocity_program(mat, submit_params.skinned ? "skinned" : gfx::shader::DEFAULT_VARIANT, camera_position);
        // The scene's depth decides what the surface covers; the surface writes none.
        const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | cull_state);
        bgfx::submit(view, offset_program != nullptr ? offset_program->native_handle() : prog.program->native_handle(), 0, discard);
    };
    callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
    {
        velocity_geom_program& prog = submit_params.skinned ? velocity_program_skinned_ : velocity_program_;
        prog.program->end();
    };
    const auto& view_frustum = camera.get_frustum();
    model.submit(entity.get<transform_component>().get_transform_global(),
                 model_comp.get_submesh_transforms(),
                 model_comp.get_bone_transforms(),
                 model_comp.get_skinning_transforms(),
                 element.lod_data.current_lod_index,
                 callbacks,
                 &view_frustum,
                 &camera,
                 extras);
}

auto deferred::collect_translucent_gi_points(const math::vec3& eye) -> const std::vector<math::vec4>*
{
    translucent_gi_points_.clear();
    translucent_gi_marked_.assign(translucent_models_.size(), 0);
    // The nearest models first: past the budget, the farthest go without and take the sky's light, whole.
    auto& order = translucent_gi_order_;
    order.clear();
    for(size_t i = 0; i < translucent_models_.size(); ++i)
    {
        if(translucent_reads_[i].has_gi_variant)
        {
            const auto center = translucent_models_[i].entity.get<model_component>().get_world_bounds().get_center();
            order.emplace_back(math::distance2(eye, center), i);
        }
    }
    std::sort(order.begin(), order.end());
    for(const auto& [distance, index] : order)
    {
        const auto& bounds = translucent_models_[index].entity.get<model_component>().get_world_bounds();
        if(add_translucent_gi_points(bounds, eye))
        {
            translucent_gi_marked_[index] = 1;
        }
    }
    return translucent_gi_points_.empty() ? nullptr : &translucent_gi_points_;
}

auto deferred::add_translucent_gi_points(const math::bbox& bounds, const math::vec3& eye) -> bool
{
    // A cell apart, else two cells apart for a model too large for its share; past that it is left out.
    const size_t budget = std::min(gi_radiance_cache::max_surface_points - translucent_gi_points_.size(), max_gi_points_per_model);
    for(const float spacing : {1.0f, 2.0f})
    {
        translucent_gi_model_points_.clear();
        if(gi_radiance_cache::make_surface_points(bounds, eye, budget, spacing, translucent_gi_model_points_))
        {
            translucent_gi_points_.insert(translucent_gi_points_.end(), translucent_gi_model_points_.begin(), translucent_gi_model_points_.end());
            return true;
        }
    }
    return false;
}

auto deferred::get_order_independent_key(const math::vec3& eye) const -> translucent_mesh_list::key
{
    // The composite sorts as one object: the box around every model with an Order Independent surface.
    math::bbox bounds;
    int32_t sort_priority = std::numeric_limits<int32_t>::max();
    for(size_t i = 0; i < translucent_order_independent_.size(); ++i)
    {
        const auto& model_comp = translucent_models_[translucent_order_independent_[i]].entity.get<model_component>();
        const math::bbox& model_bounds = model_comp.get_world_bounds();
        if(i == 0)
        {
            bounds = model_bounds;
        }
        else
        {
            bounds.add_point(model_bounds.min);
            bounds.add_point(model_bounds.max);
        }
        sort_priority = std::min(sort_priority, model_comp.get_sort_priority());
    }
    return {sort_priority, math::distance2(eye, bounds.get_center())};
}

auto deferred::draws_with_gi(const graph_material& graph, size_t index) const -> bool
{
    const auto* cache = get_forward_gi_cache();
    const bool is_marked = index < translucent_gi_marked_.size() && translucent_gi_marked_[index] != 0;
    return graph.has_gi_variant() && is_marked && cache != nullptr && cache->has_surface_sh();
}

auto deferred::writes_translucent_velocity(const graph_material& graph) -> bool
{
    // A prepass puts the surface's depth in the scene's: its motion goes with it, so TAA reprojects the two alike.
    return graph.writes_velocity() || graph.has_depth_prepass();
}

auto deferred::get_translucent_reads(const visibility_data& element) -> translucent_reads
{
    translucent_reads reads;
    const auto& model = element.entity.get<model_component>().get_model();
    for(uint32_t slot = 0; slot < uint32_t(model.get_shared_materials().size()); ++slot)
    {
        const auto mat = model.get_render_material(slot);
        const auto* graph = mat ? mat->safe_cast<graph_material>() : nullptr;
        if(graph != nullptr && graph->is_forward())
        {
            reads.has_order_independent = reads.has_order_independent || graph->is_order_independent();
            reads.has_sorted = reads.has_sorted || !graph->is_order_independent();
            reads.has_depth_prepass = reads.has_depth_prepass || graph->has_depth_prepass();
            reads.has_gi_variant = reads.has_gi_variant || graph->has_gi_variant();
            reads.writes_velocity = reads.writes_velocity || writes_translucent_velocity(*graph);
            reads.scene_textures |= graph->get_scene_textures();
        }
    }
    return reads;
}

auto deferred::find_forward_probe(const run_context& run, const math::vec3& position) const -> forward_probe
{
    forward_probe result;
    float best_radius = std::numeric_limits<float>::max();
    entt::entity environment = entt::null;
    entt::entity best = entt::null;
    for(const auto e : translucent_probes_)
    {
        const auto& probe_comp = run.scn.registry->get<reflection_probe_component>(e);
        const auto& probe = probe_comp.get_probe();
        if(probe.method == reflect_method::environment)
        {
            environment = environment == entt::null ? e : environment;
            continue;
        }
        const auto& world = run.scn.registry->get<transform_component>(e).get_transform_global();
        float radius = 0.0f;
        bool is_inside = false;
        if(probe.type == probe_type::sphere)
        {
            const auto& scale = world.get_scale();
            radius = math::max(scale.x, math::max(scale.y, scale.z)) * probe.sphere_data.range;
            is_inside = math::distance(position, world.get_position()) < radius;
        }
        else
        {
            math::transform box = world;
            box.scale(probe.box_data.extents);
            const math::vec3 local = math::inverse(box).transform_coord(position);
            radius = math::length(box.get_scale() + probe.box_data.transition_distance);
            is_inside = math::abs(local.x) <= 1.0f && math::abs(local.y) <= 1.0f && math::abs(local.z) <= 1.0f;
        }
        if(is_inside && radius < best_radius)
        {
            best_radius = radius;
            best = e;
        }
    }
    const entt::entity chosen = best != entt::null ? best : environment;
    if(chosen == entt::null)
    {
        return result;
    }
    auto& probe_comp = run.scn.registry->get<reflection_probe_component>(chosen);
    const auto& probe = probe_comp.get_probe();
    const auto& cubemap = probe_comp.get_cubemap_prefiltered();
    const auto& world = run.scn.registry->get<transform_component>(chosen).get_transform_global();
    const auto& scale = world.get_scale();
    const float intensity = probe.intensity * get_pre_exposure(run.rview).value;
    const float prefiltered = probe_comp.get_apply_prefilter() ? 1.0f : 0.0f;
    result.cubemap = cubemap->native_handle();
    result.data[1] = math::vec4(float(cubemap->info.numMips), intensity, prefiltered, 0.0f);
    if(probe.type == probe_type::sphere)
    {
        result.type = forward_probe::kind::sphere;
        result.data[0] = math::vec4(world.get_position(), math::max(scale.x, math::max(scale.y, scale.z)) * probe.sphere_data.range);
        return result;
    }
    math::transform box = world;
    box.scale(probe.box_data.extents);
    result.type = forward_probe::kind::box;
    result.data[0] = math::vec4(world.get_position(), math::length(box.get_scale() + probe.box_data.transition_distance));
    result.data[2] = math::vec4(probe.box_data.extents, probe.box_data.transition_distance);
    result.inv_world = math::inverse(box).get_matrix();
    return result;
}

namespace
{
/// The blend state of a surface of the translucency pass (fs_material_graph_forward.sh), with the reactive target's MAX
/// blend when @p has_reactive_target.
auto get_translucent_blend_state(material_blend_mode mode, bool has_reactive_target) -> uint64_t
{
    uint64_t blend = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    if(mode == material_blend_mode::additive)
    {
        blend = BGFX_STATE_BLEND_ADD;
    }
    else if(mode == material_blend_mode::modulate)
    {
        blend = BGFX_STATE_BLEND_MULTIPLY;
    }
    return blend | (has_reactive_target ? BGFX_STATE_BLEND_INDEPENDENT : 0);
}
} // namespace

void deferred::draw_translucent_model(bgfx::ViewId view, size_t index, const run_context& run, bool order_independent)
{
    const auto& element = translucent_models_[index];
    const auto& entity = element.entity;
    if(!entity.valid() || !entity.all_of<model_component, transform_component>())
    {
        return;
    }
    auto& model_comp = entity.get<model_component>();
    const auto& model = model_comp.get_model();
    if(!model.is_valid())
    {
        return;
    }
    const auto& camera = run.cam;
    const auto& lod_data = element.lod_data;
    const auto& world_transform = entity.get<transform_component>().get_transform_global();
    const float transition_progress = lod_data.transition_time > 0.0f ? lod_data.current_time / lod_data.transition_time : 1.0f;
    const math::vec3 params{1.0f - transition_progress, 0.0f, 0.0f};
    const math::vec3 params_inv{-transition_progress, 0.0f, 0.0f};
    const math::bbox bounds = model_comp.get_world_bounds();
    const forward_probe& probe = translucent_model_probes_[index];
    const bool has_reactive_target = forward_has_reactive_target_;
    const auto camera_pos = camera.get_position();
    const auto clip_planes = ANONYMOUS::make_geometry_clip_planes(camera);
    auto extras = model_comp.get_submit_extras(false);
    extras.material_pass = model_material_pass::forward;
    math::vec3 lod_params = params;

    model::submit_callbacks callbacks;
    callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
    {
        geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;
        gfx::set_uniform(prog.u_camera_wpos, camera_pos);
        gfx::set_uniform(prog.u_camera_clip_planes, clip_planes);
    };
    callbacks.setup_params_per_instance = [&](const model::submit_callbacks::params& submit_params)
    {
        geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;
        gfx::set_uniform(prog.u_lod_params, lod_params);
    };
    callbacks.setup_params_per_submesh = [&](const model::submit_callbacks::params& submit_params, const material& mat)
    {
        const uint8_t discard = submit_params.preserve_state ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL;
        const auto* graph = mat.safe_cast<graph_material>();
        geom_program& prog = submit_params.skinned ? geom_program_skinned_ : geom_program_;
        // The sorted draws and the order-independent ones each take their own surfaces.
        const bool is_drawn = graph != nullptr && graph->is_forward() && graph->is_order_independent() == order_independent;
        // Lit surfaces take the GI's light in their "gi" variants where the run's GI updated its radiance cache; until
        // that variant is compiled they draw without it.
        bool uses_gi = is_drawn && draws_with_gi(*graph, index);
        const char* plain_variant = order_independent ? "oit" : gfx::shader::DEFAULT_VARIANT;
        gpu_program* program = uses_gi ? graph_material_programs_.begin(*graph, prog.vertex_variant, order_independent ? "oit_gi" : "gi")
                                       : nullptr;
        if(program == nullptr)
        {
            uses_gi = false;
            program = is_drawn ? graph_material_programs_.begin(*graph, prog.vertex_variant, plain_variant) : nullptr;
        }
        if(program == nullptr)
        {
            // Not compiled yet: the surface draws once its shader is there.
            bgfx::discard(discard);
            return;
        }
        graph_material_programs_.submit_values(*graph);
        bind_forward_lighting(bounds,
                              graph->get_shading_model() == material_shading_model::default_lit,
                              probe,
                              graph->get_scene_textures(),
                              uses_gi);
        if(order_independent)
        {
            // Weighed sums in any order (fs_material_graph_forward.sh FORWARD_OIT): one blend serves both targets.
            const uint64_t sums = BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,
                                                                 BGFX_STATE_BLEND_ONE,
                                                                 BGFX_STATE_BLEND_ZERO,
                                                                 BGFX_STATE_BLEND_INV_SRC_ALPHA);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | sums |
                           (graph->get_render_states(true, false, false) & BGFX_STATE_CULL_MASK));
            bgfx::submit(view, program->native_handle(), 0, discard);
            stats_.drawn_translucent_submeshes++;
            return;
        }
        const uint32_t blend_rgba = has_reactive_target ? ps_soa::reactive_blend_rgba : 0u;
        if(graph->has_depth_prepass())
        {
            // The depth first, then the colour where it is the nearest layer: both faces of a two-sided surface at once.
            const uint64_t cull = graph->get_cull_type() == cull_type::none
                                      ? 0
                                      : graph->get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
            bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA | cull);
            bgfx::submit(view, program->native_handle(), 0, BGFX_DISCARD_NONE);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA | cull |
                               get_translucent_blend_state(graph->get_blend_mode(), has_reactive_target),
                           blend_rgba);
            bgfx::submit(view, program->native_handle(), 0, discard);
            stats_.drawn_translucent_submeshes++;
            return;
        }
        const uint64_t base = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                              get_translucent_blend_state(graph->get_blend_mode(), has_reactive_target);
        if(graph->get_cull_type() == cull_type::none)
        {
            // Two sided: the back faces first, then the front ones over them.
            bgfx::setState(base | BGFX_STATE_CULL_CW, blend_rgba);
            bgfx::submit(view, program->native_handle(), 0, BGFX_DISCARD_NONE);
            stats_.drawn_translucent_submeshes++;
            bgfx::setState(base | BGFX_STATE_CULL_CCW, blend_rgba);
        }
        else
        {
            bgfx::setState(base | (graph->get_render_states(true, false, false) & BGFX_STATE_CULL_MASK), blend_rgba);
        }
        bgfx::submit(view, program->native_handle(), 0, discard);
        stats_.drawn_translucent_submeshes++;
    };
    callbacks.setup_end = [&](const model::submit_callbacks::params&)
    {
    };

    const auto& submesh_transforms = model_comp.get_submesh_transforms();
    const auto& bone_transforms = model_comp.get_bone_transforms();
    const auto& skinning_matrices = model_comp.get_skinning_transforms();
    const auto& view_frustum = camera.get_frustum();
    model.submit(world_transform,
                 submesh_transforms,
                 bone_transforms,
                 skinning_matrices,
                 lod_data.current_lod_index,
                 callbacks,
                 &view_frustum,
                 &camera,
                 extras);
    if(math::epsilonNotEqual(lod_data.current_time, 0.0f, math::epsilon<float>()))
    {
        lod_params = params_inv;
        model.submit(world_transform,
                     submesh_transforms,
                     bone_transforms,
                     skinning_matrices,
                     lod_data.target_lod_index,
                     callbacks,
                     &view_frustum,
                     &camera,
                     extras);
    }
}

} // namespace rendering
} // namespace unravel
