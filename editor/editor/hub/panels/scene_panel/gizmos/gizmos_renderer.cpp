#include "gizmos_renderer.h"
#include <editor/events.h>

#include <graphics/render_pass.h>

#include <engine/events.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>

#include <engine/physics/backend/bullet/bullet_backend.h>

#include "gizmos/gizmos.h"

#include <engine/audio/ecs/components/audio_source_component.h>

#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>

namespace unravel
{

void gizmos_renderer::draw_grid(uint32_t pass_id, const camera& cam, const editing_manager::grid& grid)
{
    grid_program_->begin();

    float grid_height = 0.0f;
    math::vec4 u_params(grid_height, cam.get_near_clip(), cam.get_far_clip(), grid.opacity);
    grid_program_->set_uniform("u_params", u_params);

    auto topology = gfx::clip_quad(1.0f);
    auto state = topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;

    if(grid.depth_aware)
    {
        state |= BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_WRITE_Z;
    }

    gfx::set_state(state);
    gfx::submit(pass_id, grid_program_->native_handle());
    gfx::set_state(BGFX_STATE_DEFAULT);

    grid_program_->end();
}

void gizmos_renderer::on_frame_render(rtti::context& ctx, scene& scn, entt::handle camera_entity)
{
    if(!camera_entity)
        return;

    auto& em = ctx.get_cached<editing_manager>();
    auto& camera_comp = camera_entity.get<camera_component>();
    const auto& rview = camera_comp.get_render_view();
    const auto& camera = camera_comp.get_camera();
    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& obuffer = rview.fbo_get("OBUFFER_DEPTH");

    gfx::render_pass pass("debug_draw_pass");
    pass.bind(obuffer.get());
    pass.set_view_proj(view, proj);

    gfx::dd_raii dd(pass.id);

    bullet_backend::draw_system_gizmos(ctx, camera, dd);

    draw_selection_gizmos(ctx, camera, dd);
    draw_selection_outlines(ctx, pass.id, camera, obuffer);
    draw_icon_gizmos(ctx, scn, camera, dd);

    if(em.show_grid)
    {
        draw_grid(pass.id, camera, em.grid_data);
    }
}

auto gizmos_renderer::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    {
        auto vs = am.get_asset<gfx::shader>("editor:/data/shaders/vs_wf_wireframe.sc");
        auto fs = am.get_asset<gfx::shader>("editor:/data/shaders/fs_wf_wireframe.sc");
        wireframe_program_ = std::make_unique<gpu_program>(vs, fs);
    }

    {
        auto vs = am.get_asset<gfx::shader>("editor:/data/shaders/vs_grid.sc");
        auto fs = am.get_asset<gfx::shader>("editor:/data/shaders/fs_grid.sc");
        grid_program_ = std::make_unique<gpu_program>(vs, fs);
    }

    {
        auto vs = am.get_asset<gfx::shader>("editor:/data/shaders/vs_outline_mask.sc");
        auto fs = am.get_asset<gfx::shader>("editor:/data/shaders/fs_outline_mask.sc");
        outline_mask_program_.program = std::make_unique<gpu_program>(vs, fs);
        outline_mask_program_.cache_uniforms();
    }

    {
        auto vs = am.get_asset<gfx::shader>("editor:/data/shaders/vs_outline_mask_skinned.sc");
        auto fs = am.get_asset<gfx::shader>("editor:/data/shaders/fs_outline_mask.sc");
        outline_mask_program_skinned_.program = std::make_unique<gpu_program>(vs, fs);
        outline_mask_program_skinned_.cache_uniforms();
    }

    {
        auto vs = am.get_asset<gfx::shader>("editor:/data/shaders/vs_clip_quad.sc");
        auto fs = am.get_asset<gfx::shader>("editor:/data/shaders/fs_outline_detect.sc");
        outline_program_.program = std::make_unique<gpu_program>(vs, fs);
        outline_program_.cache_uniforms();
    }

    return true;
}

void gizmos_renderer::draw_selection_gizmos(rtti::context& ctx, const camera& camera, gfx::dd_raii& dd)
{
    auto& em = ctx.get_cached<editing_manager>();
    
    for(auto& s : em.get_selections())
    {
        draw_gizmo_var(ctx, s, camera, dd);
    }
}

void gizmos_renderer::draw_selection_outlines(rtti::context& ctx, uint32_t pass_id, const camera& camera, const gfx::frame_buffer::ptr& obuffer)
{
    auto size = obuffer->get_size();
    
    // Pass 1: Selection mask
    resize_selection_mask_rt(size.width, size.height);
    draw_selection_mask_pass(ctx, camera, selection_mask_);
    
    // Pass 2: Outline
    draw_outline_pass(pass_id, selection_mask_, obuffer);
}

void gizmos_renderer::draw_selection_mask_pass(rtti::context& ctx, const camera& camera, const gfx::frame_buffer::ptr& selection_mask)
{
    auto& em = ctx.get_cached<editing_manager>();
    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    
    gfx::render_pass pass("selection_mask_pass");
    pass.bind(selection_mask.get());
    pass.set_view_proj(view, proj);

    gfx::set_view_clear(pass.id,
                        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                        0x00000000, // clear R8 to zero
                        1.0f,
                        0);

    for(auto& obj : em.get_selections())
    {
        if(obj.is_type<entt::handle>())
        {
            auto& e = obj.get_value<entt::handle>();
            if(!e.valid())
            {
                continue;
            }
            auto transform_comp = e.try_get<transform_component>();

            if(!transform_comp)
            {
                continue;
            }
            const auto& world_transform = transform_comp->get_transform_global();

            if(auto model_comp = e.try_get<model_component>())
            {
                auto& model = model_comp->get_model();
                if(!model.is_valid())
                {
                    continue;
                }

                auto lod = model.get_lod(0);
                if(!lod)
                {
                    continue;
                }

                const auto& mesh = lod.get();
                const auto& bounds = mesh->get_bounds();

                // Test the bounding box of the mesh
                if(!camera.test_obb(bounds, world_transform))
                    continue;

                const auto& submesh_transforms = model_comp->get_submesh_transforms();
                const auto& bone_transforms = model_comp->get_bone_transforms();
                const auto& skinning_transforms = model_comp->get_skinning_transforms();

                model::submit_callbacks callbacks;
                callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
                {
                    auto& prog = submit_params.skinned ? outline_mask_program_skinned_.program
                                                       : outline_mask_program_.program;
                    prog->begin();
                };
                callbacks.setup_params_per_instance = [&](const model::submit_callbacks::params& submit_params)
                {
                    auto& prog = submit_params.skinned ? outline_mask_program_skinned_.program
                                                       : outline_mask_program_.program;
                };
                callbacks.setup_params_per_submesh =
                    [&](const model::submit_callbacks::params& submit_params, const material& mat)
                {
                    auto& prog = submit_params.skinned ? outline_mask_program_skinned_.program
                                                       : outline_mask_program_.program;
                    gfx::submit(pass.id, prog->native_handle(), 0, submit_params.preserve_state);
                };
                callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
                {
                    auto& prog = submit_params.skinned ? outline_mask_program_skinned_.program
                                                       : outline_mask_program_.program;
                    prog->end();
                };

                model.submit(world_transform,
                             submesh_transforms,
                             bone_transforms,
                             skinning_transforms,
                             0,
                             callbacks);
            }
        }
    }
}

void gizmos_renderer::draw_outline_pass(uint32_t pass_id, const gfx::frame_buffer::ptr& selection_mask, const gfx::frame_buffer::ptr& obuffer)
{
    auto size = obuffer->get_size();
    
    outline_program_.program->begin();
    
    // Bind the selection mask (R8) to sampler slot 0
    gfx::set_texture(outline_program_.s_tex, 0, selection_mask);

    float thickness = 3.0f;
    // Compute inverse‐pixel dims:
    float data[4] = {1.0f / float(size.width), 1.0f / float(size.height), thickness, 0.0f};
    gfx::set_uniform(outline_program_.u_data, data);

    // Outline color uniform:
    float outline_color[4] = {1.0f, 0.5f, 0.2f, 1.0f};
    gfx::set_uniform(outline_program_.u_outline_color, outline_color);

    // Draw a full‐screen quad
    auto topology = gfx::clip_quad(0.0f);

    // Alpha-blend the outline over existing scene
    gfx::set_state(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);

    gfx::submit(pass_id, outline_program_.program->native_handle());

    outline_program_.program->end();
}

void gizmos_renderer::draw_icon_gizmos(rtti::context& ctx, scene& scn, const camera& camera, gfx::dd_raii& dd)
{
    auto& em = ctx.get_cached<editing_manager>();
    
    if(!em.show_icon_gizmos)
        return;
        
    hpp::for_each_type<camera_component, light_component, reflection_probe_component, audio_source_component>(
        [&](auto tag)
        {
            using type_t = typename std::decay_t<decltype(tag)>::type;

            scn.registry->view<type_t>().each(
                [&](auto e, auto&& comp)
                {
                    auto entity = scn.create_handle(e);
                    rttr::variant s = entity;
                    draw_gizmo_billboard_var(ctx, s, camera, dd);
                });
        });
}

auto gizmos_renderer::deinit(rtti::context& ctx) -> bool
{
    outline_mask_program_ = {};
    outline_mask_program_skinned_ = {};
    outline_program_ = {};
    wireframe_program_.reset();
    grid_program_.reset();
    return true;
}
} // namespace unravel
