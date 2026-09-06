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
#include <engine/rendering/ecs/components/gi_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
#include <engine/rendering/ecs/components/gtao_component.h>
#include <engine/rendering/pipeline/volume_resolver.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/ecs/systems/particle_system.h>
#include <RmlUi/Core/ElementDocument.h>

#include <graphics/graphics.h>
#include <graphics/vertex_decl.h>

#include <engine/profiler/profiler.h>
#include <concurrency/parallel.h>

#include <concurrency/concurrentqueue.h>

#include <algorithm>
#include <atomic>

namespace unravel
{
namespace rendering
{
namespace
{
auto particle_blend_bgfx_state(ps_soa::blend_mode mode) -> uint64_t
{
    switch(mode)
    {
    case ps_soa::blend_mode::additive:
        return BGFX_STATE_BLEND_ADD;
    case ps_soa::blend_mode::multiply:
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
    gtao_pass_.init(ctx);
    gi_clipmap_compose_pass_.init(ctx);
    gi_light_voxel_pass_.init(ctx);
    gi_world_probe_pass_.init(ctx);
    gi_resolve_pass_.init(ctx);
    gi_reflection_pass_.init(ctx);
    scene_history_pass_.init(ctx);
    sdf_debug_pass_.init(ctx);

    auto& am = ctx.get_cached<asset_manager>();

    auto load_program = [&](const std::string& vs, const std::string& fs)
    {
        auto vs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/" + vs + ".sc");
        auto fs_shadfer = am.get_asset<gfx::shader>("engine:/data/shaders/" + fs + ".sc");

        return std::make_unique<gpu_program>(vs_shader, fs_shadfer);
    };

    particle_program_instanced_ = load_program("particles/instanced/vs_particle_instanced", "particles/instanced/fs_particle_instanced");
    particle_program_instanced_mask_ = load_program("particles/instanced/vs_particle_instanced", "particles/instanced/fs_particle_instanced_mask");
    world_quad_program_ = load_program("rmlui_world/vs_world_quad", "rmlui_world/fs_world_quad");

    return true;
}

namespace
{
// LOD levels added on top of the main-view selection when rendering shadow maps.
std::atomic<float> shadow_lod_bias{1.0f};
} // namespace

auto pipeline::get_shadow_lod_bias() -> float
{
    return shadow_lod_bias.load(std::memory_order_relaxed);
}

void pipeline::set_shadow_lod_bias(float bias)
{
    shadow_lod_bias.store(bias, std::memory_order_relaxed);
}

void pipeline::gather_visible_models(scene& scn,
    const camera* cam, 
    visibility_flags query, 
    const layer_mask& render_mask, 
    delta_t dt,
    const std::function<void(entt::handle entity, const lod_data& lod_data)>& lod_data_callback,
    const camera* lod_reference_cam)
{
    
    APP_SCOPE_PERF(cam ? "Rendering/Cull Models  " : "Rendering/Gather Models");
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
    poolstl::for_each_par_if(true,
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

                // LOD and culling both measure the pose-aware world AABB (static bounds
                // unioned with the cached per-submesh/skinned pose bounds) - one source of
                // truth for "where the rendered geometry actually is". Refreshed by
                // model_system::on_frame_before_render, which runs before any render path.
                const auto& world_bounds = model_comp.get_world_bounds();

                if(!model.calculate_lod_data(current_lod_data, world_bounds, *cam, dt.count()))
                {
                    return;
                }

                // Bind-pose local bounds are never used for culling or LOD - they don't
                // track node/bone animation.
                is_visible = cam->get_frustum().test_aabb(world_bounds);
            }
            else if(lod_reference_cam)
            {
                // No frustum culling (e.g. shadow gathering renders casters outside the view),
                // but select a distance-appropriate LOD from the reference camera plus the
                // shadow bias instead of always rendering LOD 0.
                const auto& model = model_comp.get_model();

                if(model.is_valid())
                {
                    const auto lod = model.compute_lod_index(model_comp.get_world_bounds(),
                                                             *lod_reference_cam,
                                                             get_shadow_lod_bias());
                    current_lod_data.current_lod_index = lod;
                    current_lod_data.target_lod_index = lod;
                    current_lod_data.current_time = 0.0f;
                    current_lod_data.transition_time = 0.0f;
                }
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
        params.fill_taa_params = [camera_ent](taa_pass::run_params& p) -> void
        {
            if(auto c = camera_ent.try_get<taa_component>())
            {
                p.config = c->settings;
            }
        };
        params.apply_taa_params = [camera_ent](camera& cam, const usize32_t& viewport_size) -> void
        {
            if(auto c = camera_ent.try_get<taa_component>())
            {
                const std::uint32_t sample_count = (std::min)(std::uint32_t(16),
                                                                (std::max)(std::uint32_t(2), c->settings.temporal_sample_count));
                cam.set_aa_data(viewport_size,
                                  static_cast<std::uint32_t>(gfx::get_render_frame()),
                                  sample_count,
                                  c->settings.jitter_mode,
                                  c->settings.jitter_amplitude,
                                  c->settings.jitter_temporal_phase_scale);
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

    if(auto gtao_comp = camera_ent.try_get<gtao_component>(); gtao_comp && gtao_comp->enabled)
    {
        params.fill_gtao_params = [camera_ent](gtao_pass::run_params& params)
        {
            if(auto gtao_comp = camera_ent.try_get<gtao_component>())
            {
                params.config = gtao_comp->settings;
            }
        };
    }

    if(auto gi_comp = camera_ent.try_get<gi_component>(); gi_comp && gi_comp->enabled)
    {
        params.fill_gi_params = [camera_ent](gi_settings& gi)
        {
            if(auto gi_comp = camera_ent.try_get<gi_component>())
            {
                gi = gi_comp->settings;
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
                                resolved.has_fxaa || resolved.has_taa || resolved.has_ssr || resolved.has_assao || resolved.has_ssil || resolved.has_gtao ||
                                resolved.has_gi;
    if(!has_any_volume)
    {
        return params;
    }
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
        params.fill_taa_params = [s](taa_pass::run_params& p) -> void { p.config = s; };
        params.apply_taa_params = [s](camera& cam, const usize32_t& viewport_size) -> void
        {
            const std::uint32_t sample_count =
                (std::min)(std::uint32_t(16), (std::max)(std::uint32_t(2), s.temporal_sample_count));
            cam.set_aa_data(viewport_size,
                              static_cast<std::uint32_t>(gfx::get_render_frame()),
                              sample_count,
                              s.jitter_mode,
                              s.jitter_amplitude,
                              s.jitter_temporal_phase_scale);
        };
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
    if(resolved.has_gtao)
    {
        gtao_pass::settings s = resolved.gtao;
        params.fill_gtao_params = [s](gtao_pass::run_params& p) { p.config = s; };
    }
    if(resolved.has_gi)
    {
        gi_settings s = resolved.gi;
        params.fill_gi_params = [s](gi_settings& gi)
        {
            gi = s;
        };
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

    gfx::render_pass pass("World UI/Elements Pass");
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
    gfx::render_pass pass("Particles/Pass");
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
    

        // Primary: material key (blend / texture / texture mode) so same-material emitters coalesce.
        // Secondary: distance (far → near) within a material — does not split batches. Per-particle
        // depth sort for Normal still runs inside ps_soa::render_emitter_batch.
        struct sort_key
        {
            particle_emitter_component* component;
            ps_soa::blend_mode blend_mode;
            ps_soa::texture_mode texture_mode;
            hpp::uuid texture_uid;
            float distance;
        };
        hpp::small_vector<sort_key, 16> particle_emitters;

        auto blend_draw_order = [](ps_soa::blend_mode mode) -> int
        {
            // Order-independent blends first; alpha (Normal) last so depth-sorted particles composite on top.
            switch(mode)
            {
            case ps_soa::blend_mode::additive:
                return 0;
            case ps_soa::blend_mode::multiply:
                return 1;
            default:
                return 2;
            }
        };

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

                    // Renderer-based culling feedback (same as model_component).
                    particle_emitter_comp.set_last_render_frame(uint64_t(gfx::get_render_frame()));

                    const auto& tex = particle_emitter_comp.get_texture();
                    const float distance = math::distance2(bounds.get_center(), cam_pos);
                    particle_emitters.emplace_back(sort_key{&particle_emitter_comp,
                                                            particle_emitter_comp.get_blend_mode(),
                                                            particle_emitter_comp.get_texture_mode(),
                                                            tex.uid(),
                                                            distance});
            });
        }

        {
            APP_SCOPE_PERF("Rendering/Particle Pass/Sort Emitters by Material");
            std::sort(particle_emitters.begin(),
                      particle_emitters.end(),
                      [&](const sort_key& a, const sort_key& b)
                      {
                          const int ao = blend_draw_order(a.blend_mode);
                          const int bo = blend_draw_order(b.blend_mode);
                          if(ao != bo)
                          {
                              return ao < bo;
                          }
                          if(a.texture_uid != b.texture_uid)
                          {
                              return a.texture_uid < b.texture_uid;
                          }
                          if(a.texture_mode != b.texture_mode)
                          {
                              return a.texture_mode < b.texture_mode;
                          }
                          // Same material: back-to-front (farther first).
                          return a.distance > b.distance;
                      });
        }

        {
            APP_SCOPE_PERF("Rendering/Particle Pass/Submit Emitter Batches");
            hpp::small_vector<ps_soa::emitter_handle, 16> current_batch;
            asset_handle<gfx::texture> batch_texture;
            ps_soa::texture_mode batch_texture_mode = ps_soa::texture_mode::multi_channel;
            ps_soa::blend_mode batch_blend_mode = ps_soa::blend_mode::normal;
            bool batch_open = false;

            auto flush_particle_batch = [&]()
            {
                if(current_batch.empty() || !batch_texture.is_valid())
                {
                    current_batch.clear();
                    batch_open = false;
                    return;
                }
                APP_SCOPE_PERF("Rendering/Particle Pass/Flush Emitter Batch");
                const bgfx::ProgramHandle program = (batch_texture_mode == ps_soa::texture_mode::mask)
                    ? particle_program_instanced_mask_->native_handle()
                    : particle_program_instanced_->native_handle();
                auto texture = batch_texture.get()->native_handle();
                const uint64_t blend_state = particle_blend_bgfx_state(batch_blend_mode);
                // Additive / Multiply are order-independent; skip expensive per-particle depth sort.
                const bool sort_by_depth = (batch_blend_mode == ps_soa::blend_mode::normal);
                stats_.drawn_particles += ps_soa::render_emitter_batch(current_batch.data(),
                    static_cast<uint32_t>(current_batch.size()),
                    pass.id,
                    program,
                    cam_view,
                    cam_pos,
                    texture,
                    blend_state,
                    sort_by_depth);
                stats_.drawn_particles_batches++;
                current_batch.clear();
                batch_open = false;
            };

            for(const auto& particle_emitter : particle_emitters)
            {
                auto* comp = particle_emitter.component;
                const auto& tex = comp->get_texture();
                const ps_soa::texture_mode tm = particle_emitter.texture_mode;
                const ps_soa::blend_mode bm = particle_emitter.blend_mode;
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
                    if(ps_soa::is_valid(emitter_handle))
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
    drawn_static_submeshes += stats.drawn_static_submeshes;
    drawn_skinned_models += stats.drawn_skinned_models;
    drawn_skinned_submeshes += stats.drawn_skinned_submeshes;
    drawn_models_for_shadows += stats.drawn_models_for_shadows;
    drawn_submeshes_for_shadows += stats.drawn_submeshes_for_shadows;
    drawn_skinned_models_for_shadows += stats.drawn_skinned_models_for_shadows;
    drawn_skinned_submeshes_for_shadows += stats.drawn_skinned_submeshes_for_shadows;
    drawn_lights += stats.drawn_lights;
    drawn_lights_casting_shadows += stats.drawn_lights_casting_shadows;
    drawn_particles += stats.drawn_particles;
    drawn_particles_batches += stats.drawn_particles_batches;

    add_batch_stats(stats.batching_stats);
}

} // namespace rendering
} // namespace unravel
