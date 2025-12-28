#include "pipeline.h"
#include "engine/rendering/camera.h"
#include <engine/rendering/batch_collector.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>

#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/ecs/systems/particle_system.h>
 
#include <engine/profiler/profiler.h>
#define POOLSTL_STD_SUPPLEMENT 1
#include <poolstl/poolstl.hpp>

namespace unravel
{
namespace rendering
{
auto pipeline::init(rtti::context& ctx) -> bool
{
    prefilter_pass_.init(ctx);
    blit_pass_.init(ctx);
    atmospheric_pass_.init(ctx);
    atmospheric_pass_perez_.init(ctx);
    atmospheric_pass_skybox_.init(ctx);
    fxaa_pass_.init(ctx);
    tonemapping_pass_.init(ctx);
    assao_pass_.init(ctx);
    ssr_pass_.init(ctx);
    hiz_pass_.init(ctx);

    auto& am = ctx.get_cached<asset_manager>();

    auto load_program = [&](const std::string& vs, const std::string& fs)
    {
        auto vs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/" + vs + ".sc");
        auto fs_shadfer = am.get_asset<gfx::shader>("engine:/data/shaders/" + fs + ".sc");

        return std::make_unique<gpu_program>(vs_shader, fs_shadfer);
    };

    particle_program_ = load_program("particles/vs_particle", "particles/fs_particle");
    particle_program_instanced_ = load_program("particles/instanced/vs_particle_instanced", "particles/instanced/fs_particle_instanced");
    particle_program_instanced_mask_ = load_program("particles/instanced/vs_particle_instanced", "particles/instanced/fs_particle_instanced_mask");

    return true;
}

void pipeline::gather_visible_models(scene& scn,
    const camera* cam, 
    visibility_flags query, 
    const layer_mask& render_mask, 
    delta_t dt,
    const std::function<void(entt::handle entity, const lod_data& lod_data)>& lod_data_callback)
{
    
    APP_SCOPE_PERF(cam ? "Rendering/Cull   Models" : "Rendering/Gather Models");

    auto view = scn.registry->view<transform_component, model_component, layer_component, active_component>();
    
    
    // Use parallel execution for visibility testing
    std::for_each(/*std::execution::par_unseq,*/ view.begin(), view.end(),
        [&](auto entity)
        {
            auto&& [transform_comp, model_comp, layer_comp, active_comp] = view.get(entity);
            
            // Get layer component if it exists, otherwise use default layer
            auto entity_layer = layer_comp.layers;
            
            // Layer filtering - check if entity's layer matches camera's render mask
            if((entity_layer.mask & render_mask.mask) == 0)
            {
                return; // Entity's layer is not visible to this camera
            }

            // Early exit checks
            if(!model_comp.is_enabled()) 
            {
                return;
            }
            if((query & visibility_query::is_static) && !model_comp.is_static()) 
            {
                return;
            }
            if((query & visibility_query::is_reflection_caster) && !model_comp.casts_reflection()) 
            {
                return;
            }
            if((query & visibility_query::is_shadow_caster) && !model_comp.casts_shadow()) 
            {
                return;
            }
            
            auto& current_lod_data = model_comp.get_lod_data_for_camera(cam, gfx::get_render_frame());
            bool is_visible = true;

            if(cam)
            {
                const auto& model = model_comp.get_model();


                if(!model.is_valid())
                {
                    return;
                }
                const auto& world_transform = transform_comp.get_transform_global();

                if(!model.calculate_lod_data(current_lod_data, world_transform, *cam, dt.count()))
                {
                    return;
                }

                const auto& local_bounds = model_comp.get_local_bounds(current_lod_data.current_lod_index);

                // Test the bounding box of the mesh
                is_visible = cam->test_obb(local_bounds, world_transform);
                 // Alternative: is_visible = frustum->test_aabb(model_comp.get_world_bounds());
            }

            if(is_visible)
            {
                lod_data_callback(scn.create_handle(entity), current_lod_data);
            }
            
        });
}


auto pipeline::create_run_params(entt::handle camera_ent) const -> rendering::pipeline::run_params
{
    rendering::pipeline::run_params params;

    if(auto assao_comp = camera_ent.try_get<assao_component>(); assao_comp && assao_comp->enabled)
    {
        params.fill_assao_params = [camera_ent](assao_pass::run_params& params)
        {
            if(auto assao_comp = camera_ent.try_get<assao_component>())
            {
                params.params = assao_comp->settings;
            }
        };
    }
    
    if(auto tonemapping_comp = camera_ent.try_get<tonemapping_component>(); tonemapping_comp && tonemapping_comp->enabled)
    {
        params.fill_hdr_params = [camera_ent](tonemapping_pass::run_params& params)
        {
            if(auto tonemapping_comp = camera_ent.try_get<tonemapping_component>())
            {
                params.config = tonemapping_comp->settings;
            }
        };
    }
    else
    {
        // Always set up tonemapping params but with disabled method if component is disabled
        params.fill_hdr_params = [camera_ent](tonemapping_pass::run_params& params)
        {
            params.config.method = tonemapping_method::none;
        };
    }
    
    if(auto fxaa_comp = camera_ent.try_get<fxaa_component>(); fxaa_comp && fxaa_comp->enabled)
    {
        params.fill_fxaa_params = [camera_ent](fxaa_pass::run_params& params)
        {
            if(auto fxaa_comp = camera_ent.try_get<fxaa_component>())
            {
                // Fill FXAA parameters
            }
        };
    }
    
    if(auto ssr_comp = camera_ent.try_get<ssr_component>(); ssr_comp && ssr_comp->enabled)
    {
        params.fill_ssr_params = [camera_ent](ssr_pass::run_params& params)
        {
            if(auto ssr_comp = camera_ent.try_get<ssr_component>())
            {
                params.settings = ssr_comp->settings;
            }
        };
    }

    return params;
}

void pipeline::ui_pass(scene& scn, const camera& camera, gfx::render_view& rview, const gfx::frame_buffer::ptr& output)
{
    APP_SCOPE_PERF("Rendering/3D Text Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    auto& fbo = rview.fbo_get("OBUFFER_DEPTH");


    gfx::render_pass pass("ui_elements_pass");
    pass.bind(fbo.get());
    pass.set_view_proj(view, proj);

    scn.registry->view<transform_component, text_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& text_comp, auto&& active)
        {
            const auto& world_transform = transform_comp.get_transform_global();
            auto bbox = text_comp.get_bounds();

            if(!camera.test_obb(bbox, world_transform))
            {
                return;
            }

            text_comp.submit(pass.id, world_transform, BGFX_STATE_DEPTH_TEST_LESS);
        });

    gfx::discard();
}


void pipeline::particle_pass(scene& scn, const camera& camera, gfx::render_view& rview, const gfx::frame_buffer::ptr& output)
{
    APP_SCOPE_PERF("Rendering/Particle Pass");

    auto lbuffer_depth = rview.fbo_get("LBUFFER_DEPTH");

    // Set up render pass to render particles to the output framebuffer
    gfx::render_pass pass("particle_pass");
    pass.bind(lbuffer_depth.get());
    
    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    pass.set_view_proj(view, proj);

    stats_.drawn_particles = 0;
    stats_.drawn_particles_batches = 0;

    if(particle_program_instanced_ && particle_program_instanced_mask_ && particle_program_instanced_->begin() && particle_program_instanced_mask_->begin())
    {
        // Render particles using the particle system
        auto cam_pos = camera.get_position();
        auto cam_view = camera.get_view();
    

        struct sort_key
        {
            particle_emitter_component* component;
            float distance;
        };
        hpp::small_vector<sort_key, 16> particle_emitters;

        {
            APP_SCOPE_PERF("Rendering/Particle Pass/Cull Emitters");
            scn.registry->view<transform_component, particle_emitter_component, active_component>().each(
                [&](auto e, auto&& transform_comp, auto&& particle_emitter_comp, auto&& active)
                {
                    const auto& bounds = particle_emitter_comp.get_world_bounds();
                    if(!particle_emitter_comp.is_enabled() || !camera.test_aabb(bounds))
                    {
                        return;
                    }
    
                    auto distance = math::distance(bounds.get_center(), cam_pos);
                    particle_emitters.emplace_back(sort_key{&particle_emitter_comp, distance});
            });
        }
       
        {
            APP_SCOPE_PERF("Rendering/Particle Pass/Sort Emitters");
            // Sort by distance first (back to front for proper alpha blending)
            std::sort(particle_emitters.begin(), particle_emitters.end(), [](const sort_key& a, const sort_key& b)
            {
                return a.distance < b.distance;
            });

        }

        {
            APP_SCOPE_PERF("Rendering/Particle Pass/Group Emitters by Texture");
            // Group by texture while maintaining distance order
            // This batches emitters with the same texture together for efficient rendering
            // while preserving the distance-sorted order for proper alpha blending
            hpp::small_vector<EmitterHandle, 16> current_batch;
            asset_handle<gfx::texture> current_texture;
            
            for(const auto& particle_emitter : particle_emitters)
            {
                // Check if we need to start a new batch (different texture)
                if(current_texture != particle_emitter.component->get_texture())
                {
                    // Render the current batch if it has emitters
                    if(!current_batch.empty())
                    {
                        auto texture = current_texture.get()->native_handle();
                        stats_.drawn_particles += psRenderEmitterBatch(current_batch.data(), static_cast<uint32_t>(current_batch.size()), 
                                        pass.id, particle_program_instanced_->native_handle(), particle_program_instanced_mask_->native_handle(), 
                                        cam_view, cam_pos, texture);
                        stats_.drawn_particles_batches++;
                    }
                    
                    // Start new batch
                    current_batch.clear();
                    current_texture = particle_emitter.component->get_texture();
                }
                
                // Add emitter to current batch (only if it's enabled and has valid handle)
                if(particle_emitter.component->is_enabled())
                {
                    auto emitter_handle = particle_emitter.component->get_emitter_handle();
                    if(isValid(emitter_handle))
                    {
                        current_batch.push_back(emitter_handle);
                    }
                }
            }
            
            // Render the final batch
            if(!current_batch.empty() && current_texture.is_valid())
            {
                auto texture = current_texture.get()->native_handle();
                stats_.drawn_particles += psRenderEmitterBatch(current_batch.data(), static_cast<uint32_t>(current_batch.size()), 
                                pass.id, particle_program_instanced_->native_handle(), particle_program_instanced_mask_->native_handle(), 
                                cam_view, cam_pos, texture);
                stats_.drawn_particles_batches++;
            }
        }

        particle_program_instanced_->end();
        particle_program_instanced_mask_->end();
    }
}

// pipeline_stats implementation
void pipeline_stats::add_batch_stats(const batch_stats& stats)
{
    batching_stats.total_batches += stats.total_batches;
    batching_stats.total_instances += stats.total_instances;
    batching_stats.collection_time_ms += stats.collection_time_ms;
    batching_stats.preparation_time_ms += stats.preparation_time_ms;
    batching_stats.submission_time_ms += stats.submission_time_ms;
    batching_stats.instance_buffer_memory_used += stats.instance_buffer_memory_used;
    batching_stats.split_batches += stats.split_batches;
    
    // Recalculate derived stats
    batching_stats.calculate_derived_stats();
}

} // namespace rendering
} // namespace unravel
