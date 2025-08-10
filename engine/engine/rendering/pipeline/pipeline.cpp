#include "pipeline.h"
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>

#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>

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

    return true;
}

auto pipeline::gather_visible_models(scene& scn, const math::frustum* frustum, visibility_flags query)
    -> visibility_set_models_t
{
    APP_SCOPE_PERF("Cull Models Legacy");

    auto view = scn.registry->view<transform_component, model_component, active_component>();
    
    // Pre-allocate with estimated size
    visibility_set_models_t result;
    
    // Thread-safe collection using mutex
    //std::mutex result_mutex;
    
    // Use parallel execution for visibility testing
    std::for_each(/*std::execution::par_unseq,*/ view.begin(), view.end(),
        [&](auto entity)
        {
            auto&& [transform_comp, model_comp, active_comp] = view.get(entity);
            
            // Early exit checks
            if(!model_comp.is_enabled()) return;
            if((query & visibility_query::is_static) && !model_comp.is_static()) return;
            if((query & visibility_query::is_reflection_caster) && !model_comp.casts_reflection()) return;
            if((query & visibility_query::is_shadow_caster) && !model_comp.casts_shadow()) return;
            
            bool is_visible = true;
            
            if(frustum)
            {
                const auto& world_transform = transform_comp.get_transform_global();
                const auto& local_bounds = model_comp.get_local_bounds();
                
                // Test the bounding box of the mesh
                is_visible = frustum->test_obb(local_bounds, world_transform);
                // Alternative: is_visible = frustum->test_aabb(model_comp.get_world_bounds());
            }
            
            if(is_visible)
            {
                // Thread-safe insertion
                //std::lock_guard<std::mutex> lock(result_mutex);
                result.emplace_back(scn.create_handle(entity));
            }
        });
    
    return result;
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
    APP_SCOPE_PERF("Rendering/UI Pass");

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

} // namespace rendering
} // namespace unravel
