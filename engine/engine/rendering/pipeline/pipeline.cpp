#include "pipeline.h"
#include "bx/bx.h"
#include "engine/rendering/camera.h"
#include "glm/ext.hpp"
#include <engine/rendering/batch_collector.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/ui/ecs/components/ui_document_component.h>

#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/auto_exposure_component.h>
#include <engine/rendering/ecs/components/bloom_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/taa_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
#include <engine/rendering/pipeline/volume_resolver.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/ecs/systems/particle_system.h>
#include <RmlUi/Core/ElementDocument.h>

#include <graphics/graphics.h>
#include <graphics/vertex_decl.h>

#include <engine/profiler/profiler.h>
#define POOLSTL_STD_SUPPLEMENT 1
#include <poolstl/poolstl.hpp>

#include <concurrency/concurrentqueue.h>

#include <algorithm>

namespace unravel
{
namespace rendering
{
namespace
{
auto particle_blend_bgfx_state(BlendMode::Enum mode) -> uint64_t
{
    switch(mode)
    {
    case BlendMode::Additive:
        return BGFX_STATE_BLEND_ADD;
    case BlendMode::Multiply:
        return BGFX_STATE_BLEND_MULTIPLY;
    default:
        return BGFX_STATE_BLEND_NORMAL;
    }
}
} // namespace
auto pipeline::init(rtti::context& ctx) -> bool
{
    cache_entity_ = cache_registry_.create();

    prefilter_pass_.init(ctx);
    blit_pass_.init(ctx);
    atmospheric_pass_perez_.init(ctx);
    atmospheric_pass_skybox_.init(ctx);
    auto_exposure_pass_.init(ctx);
    bloom_pass_.init(ctx);
    fxaa_pass_.init(ctx);
    taa_pass_.init(ctx);
    tonemapping_pass_.init(ctx);
    assao_pass_.init(ctx);
    ssr_pass_.init(ctx);
    hiz_pass_.init(ctx);
    ssil_pass_.init(ctx);

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
    world_quad_program_ = load_program("rmlui_world/vs_world_quad", "rmlui_world/fs_world_quad");

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
    static const std::string thread_name = "Rendering/Gather Models Thread";
    tpp::this_thread::register_this_thread(thread_name, true);
    auto view = scn.registry->view<transform_component, model_component, layer_component, active_component>();
    
    moodycamel::ConcurrentQueue<std::pair<entt::entity, lod_data>> queue;

    if(cam)
    {
        // first force get the frustum to ensure it is up to date. it is internally cached and we don't want
        // to updated it in the loop since it is not thread safe. Force it here.
        auto& frustum = cam->get_frustum();
        BX_UNUSED(frustum);
    }

    //get_lod_data_for_camera is not thread safe but we are only operating on a single model once
    //so we can use parallel execution here
    std::for_each(poolstl::par,//std::execution::par,
        view.begin(), 
        view.end(),
        [&](auto entity)
        {
            tpp::this_thread::register_this_thread(thread_name, true);

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
                // lod_data_callback(scn.create_handle(entity), current_lod_data);
                queue.enqueue(std::make_pair(entity, current_lod_data));
            }
            
        });

    std::pair<entt::entity, lod_data> entity_lod_data;
    while(queue.try_dequeue_non_interleaved(entity_lod_data))
    {
        lod_data_callback(scn.create_handle(entity_lod_data.first), entity_lod_data.second);
    }
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
    
    if(auto ae_comp = camera_ent.try_get<auto_exposure_component>(); ae_comp && ae_comp->enabled)
    {
        params.fill_auto_exposure_params = [camera_ent](auto_exposure_pass::run_params& params)
        {
            if(auto ae_comp = camera_ent.try_get<auto_exposure_component>())
            {
                params.config = ae_comp->settings;
            }
        };
    }
    if(auto bloom_comp = camera_ent.try_get<bloom_component>(); bloom_comp && bloom_comp->enabled)
    {
        params.fill_bloom_params = [camera_ent](bloom_pass::run_params& params)
        {
            if(auto bloom_comp = camera_ent.try_get<bloom_component>())
            {
                params.config = bloom_comp->settings;
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
    
    if(auto taa_comp = camera_ent.try_get<taa_component>(); taa_comp && taa_comp->enabled)
    {
        params.taa_temporal_sample_count = std::max(std::uint32_t(2), taa_comp->settings.temporal_sample_count);
        params.taa_temporal_jitter_mode = taa_comp->settings.jitter_mode;
        params.taa_jitter_amplitude = taa_comp->settings.jitter_amplitude;
        params.taa_jitter_temporal_phase_scale = taa_comp->settings.jitter_temporal_phase_scale;
        params.fill_taa_params = [camera_ent](taa_pass::run_params& p)
        {
            if(auto c = camera_ent.try_get<taa_component>())
            {
                p.config = c->settings;
            }
        };
    }

    if(auto fxaa_comp = camera_ent.try_get<fxaa_component>(); fxaa_comp && fxaa_comp->enabled && !params.fill_taa_params)
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

    if(auto ssil_comp = camera_ent.try_get<ssil_component>(); ssil_comp && ssil_comp->enabled)
    {
        params.fill_ssil_params = [camera_ent](ssil_pass::run_params& params)
        {
            if(auto ssil_comp = camera_ent.try_get<ssil_component>())
            {
                params.settings = ssil_comp->settings;
            }
        };
    }

    return params;
}

auto pipeline::create_run_params(entt::handle camera_ent, scene* scn, const camera* cam) const -> rendering::pipeline::run_params
{
    auto params = create_run_params(camera_ent);
    if(!scn || !cam)
    {
        return params;
    }
    auto resolved = resolve_post_process_volumes(*scn, cam->get_position(), camera_ent);
    const bool has_any_volume = resolved.has_auto_exposure || resolved.has_bloom || resolved.has_tonemapping ||
                                resolved.has_fxaa || resolved.has_taa || resolved.has_ssr || resolved.has_assao || resolved.has_ssil;
    if(!has_any_volume)
    {
        return params;
    }
    params.volume_settings = resolved;
    if(resolved.has_auto_exposure)
    {
        auto_exposure_pass::settings s = resolved.auto_exposure;
        params.fill_auto_exposure_params = [s](auto_exposure_pass::run_params& p) { p.config = s; };
    }
    if(resolved.has_bloom)
    {
        bloom_pass::settings s = resolved.bloom;
        params.fill_bloom_params = [s](bloom_pass::run_params& p) { p.config = s; };
    }
    if(resolved.has_tonemapping)
    {
        tonemapping_pass::settings s = resolved.tonemapping;
        params.fill_hdr_params = [s](tonemapping_pass::run_params& p) { p.config = s; };
    }
    if(resolved.has_fxaa)
    {
        params.fill_fxaa_params = [](fxaa_pass::run_params&) {};
    }
    if(resolved.has_taa)
    {
        taa_pass::settings s = resolved.taa;
        params.taa_temporal_sample_count = std::max(std::uint32_t(2), s.temporal_sample_count);
        params.taa_temporal_jitter_mode = s.jitter_mode;
        params.taa_jitter_amplitude = s.jitter_amplitude;
        params.taa_jitter_temporal_phase_scale = s.jitter_temporal_phase_scale;
        params.fill_taa_params = [s](taa_pass::run_params& p) { p.config = s; };
    }
    if(resolved.has_ssr)
    {
        ssr_pass::ssr_settings s = resolved.ssr;
        params.fill_ssr_params = [s](ssr_pass::run_params& p) { p.settings = s; };
    }
    if(resolved.has_assao)
    {
        assao_pass::settings s = resolved.assao;
        params.fill_assao_params = [s](assao_pass::run_params& p) { p.params = s; };
    }
    if(resolved.has_ssil)
    {
        ssil_pass::ssil_settings s = resolved.ssil;
        params.fill_ssil_params = [s](ssil_pass::run_params& p) { p.settings = s; };
    }
    if(params.fill_taa_params)
    {
        params.fill_fxaa_params = {};
    }
    return params;
}

void pipeline::run_ui_pass(scene& scn, const camera& camera, gfx::render_view& rview, const gfx::frame_buffer::ptr& output)
{
    APP_SCOPE_PERF("Rendering/3D Text Pass");

    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    auto& fbo = rview.fbo_get("OBUFFER_DEPTH");

    gfx::render_pass pass("UI Elements Pass");
    pass.bind(fbo.get());
    pass.set_view_proj(view, proj);


    struct world_space_ui_entry
    {
        entt::handle handle;
        ui_document_component* comp = nullptr;
        transform_component* transform_comp = nullptr;
        float distance = 0.0f;
    };
    struct world_space_ui_cache
    {
        std::vector<world_space_ui_entry> entries;
    };
    auto& ui_cache = cache_registry_.get_or_emplace<world_space_ui_cache>(cache_entity_);
    ui_cache.entries.clear();

    // World-space UI documents: draw quad with framebuffer texture
    if(world_quad_program_ && world_quad_program_->begin())
    {
        constexpr uint64_t world_ui_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                                            BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_NORMAL;

        scn.registry->view<transform_component, ui_document_component, active_component>().each(
            [&](auto e, auto&& transform_comp, auto&& ui_comp, auto&&)
            {
                if(ui_comp.render_mode != ui_render_mode::world_space)
                {
                    return;
                }
                if(!ui_comp.is_enabled() || !ui_comp.framebuffer || !ui_comp.document || !ui_comp.document->IsVisible())
                {
                    return;
                }

                const auto& world_transform = transform_comp.get_transform_global();
                auto bbox = ui_comp.get_bounds();
                if(!camera.test_obb(bbox, world_transform))
                {
                    return;
                }
                auto handle = scn.create_handle(e);
                float dist = math::distance2(camera.get_position(), world_transform.get_position());
                ui_cache.entries.push_back({handle, &ui_comp, &transform_comp, dist});
            });

        std::sort(ui_cache.entries.begin(), ui_cache.entries.end(), [](const world_space_ui_entry& a, const world_space_ui_entry& b)
        {
            return a.distance > b.distance;
        });

        for(const auto& entry : ui_cache.entries)
        {
            auto& ui_comp = *entry.comp;
            auto handle = entry.handle;
            const auto& world_transform = entry.transform_comp->get_transform_global();
            const auto scale = ui_comp.get_world_space_scale();
            const auto model = world_transform * math::transform::scaling(scale);
            auto topology = gfx::clip_quad(0.0f, 0.5f, 0.5f);
            gfx::set_state(topology | world_ui_state);
            world_quad_program_->set_texture(0, "s_tex", ui_comp.framebuffer.get(), 0);
            gfx::set_world_transform(model);
            gfx::submit(pass.id, world_quad_program_->native_handle());        }

        ui_cache.entries.clear();

        world_quad_program_->end();
    }

    
    struct world_space_text_entry
    {
        entt::handle handle;
        text_component* comp = nullptr;
        transform_component* transform_comp = nullptr;
        float distance = 0.0f;
    };
    struct world_space_text_cache
    {
        std::vector<world_space_text_entry> entries;
    };
    
    //this approach preserves the capacities of the vector
    auto& text_cache = cache_registry_.get_or_emplace<world_space_text_cache>(cache_entity_);
    text_cache.entries.clear();

    scn.registry->view<transform_component, text_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& text_comp, auto&& active)
        {
            const auto& world_transform = transform_comp.get_transform_global();
            auto bbox = text_comp.get_bounds();

            if(!camera.test_obb(bbox, world_transform))
            {
                return;
            }

            float dist = math::distance2(camera.get_position(), world_transform.get_position());
            auto handle = scn.create_handle(e);
            text_cache.entries.push_back({handle, &text_comp, &transform_comp, dist});
        });

    std::sort(text_cache.entries.begin(), text_cache.entries.end(), [](const world_space_text_entry& a, const world_space_text_entry& b)
    {
        return a.distance > b.distance;
    });

    for(const auto& entry : text_cache.entries)
    {
        auto& text_comp = *entry.comp;
        auto handle = entry.handle;
        const auto& world_transform = entry.transform_comp->get_transform_global();
        text_comp.submit(pass.id, world_transform, BGFX_STATE_DEPTH_TEST_LESS);
    }

    text_cache.entries.clear();

    gfx::discard();
}


void pipeline::run_particle_pass(scene& scn, const camera& camera, gfx::render_view& rview, const gfx::frame_buffer::ptr& output)
{
    APP_SCOPE_PERF("Rendering/Particle Pass");

    auto lbuffer_depth = rview.fbo_get("LBUFFER_DEPTH");

    // Set up render pass to render particles to the output framebuffer
    gfx::render_pass pass("Particles Pass");
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
            APP_SCOPE_PERF("Rendering/Particle Pass/Group Emitters for batch");
            hpp::small_vector<EmitterHandle, 16> current_batch;
            asset_handle<gfx::texture> batch_texture;
            TextureMode::Enum batch_texture_mode = TextureMode::MultiChannel;
            BlendMode::Enum batch_blend_mode = BlendMode::Normal;
            bool batch_open = false;

            auto flush_particle_batch = [&]()
            {
                if(current_batch.empty() || !batch_texture.is_valid())
                {
                    current_batch.clear();
                    batch_open = false;
                    return;
                }
                const bgfx::ProgramHandle program = (batch_texture_mode == TextureMode::Mask)
                    ? particle_program_instanced_mask_->native_handle()
                    : particle_program_instanced_->native_handle();
                auto texture = batch_texture.get()->native_handle();
                const uint64_t blend_state = particle_blend_bgfx_state(batch_blend_mode);
                stats_.drawn_particles += psRenderEmitterBatch(current_batch.data(), static_cast<uint32_t>(current_batch.size()),
                    pass.id, program, cam_view, cam_pos, texture, blend_state);
                stats_.drawn_particles_batches++;
                current_batch.clear();
                batch_open = false;
            };

            for(const auto& particle_emitter : particle_emitters)
            {
                auto* comp = particle_emitter.component;
                const auto& tex = comp->get_texture();
                const TextureMode::Enum tm = comp->get_texture_mode();
                const BlendMode::Enum bm = comp->get_blend_mode();
                if(batch_open && (tex != batch_texture || tm != batch_texture_mode || bm != batch_blend_mode))
                {
                    flush_particle_batch();
                }
                if(!batch_open)
                {
                    batch_texture = tex;
                    batch_texture_mode = tm;
                    batch_blend_mode = bm;
                    batch_open = true;
                }
                if(comp->is_enabled())
                {
                    auto emitter_handle = comp->get_emitter_handle();
                    if(isValid(emitter_handle))
                    {
                        current_batch.push_back(emitter_handle);
                    }
                }
            }
            flush_particle_batch();
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

void pipeline_stats::add_stats(const pipeline_stats& stats)
{
    drawn_models += stats.drawn_models;
    drawn_skinned_models += stats.drawn_skinned_models;
    drawn_models_for_shadows += stats.drawn_models_for_shadows;
    drawn_skinned_models_for_shadows += stats.drawn_skinned_models_for_shadows;
    drawn_lights += stats.drawn_lights;
    drawn_lights_casting_shadows += stats.drawn_lights_casting_shadows;
    drawn_particles += stats.drawn_particles;
    drawn_particles_batches += stats.drawn_particles_batches;

    add_batch_stats(stats.batching_stats);
}

} // namespace rendering
} // namespace unravel
