#include "picking_manager.h"
#include "thumbnail_manager.h"

#include <graphics/debugdraw.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <logging/logging.h>

#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/events.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
namespace unravel
{

namespace
{
auto to_bx(const math::vec3& data) -> bx::Vec3
{
    return {data.x, data.y, data.z};
}

auto from_bx(const bx::Vec3& data) -> math::vec3
{
    return {data.x, data.y, data.z};
}

} // namespace

constexpr int picking_manager::tex_id_dim;
void picking_manager::on_frame_render(rtti::context& ctx, delta_t dt)
{
    on_frame_pick(ctx, dt);
}

void picking_manager::on_frame_pick(rtti::context& ctx, delta_t dt)
{
    auto& em = ctx.get_cached<editing_manager>();

    // Get the appropriate scene based on edit mode
    scene* target_scene = em.get_active_scene(ctx);
    
    if (!target_scene)
    {
        return;
    }

    const auto render_frame = gfx::get_render_frame();

    if(pick_camera_)
    {
        const auto& pick_camera = *pick_camera_;

        const auto& pick_view = pick_camera.get_view();
        const auto& pick_proj = pick_camera.get_projection();

        gfx::render_pass pass("picking_buffer_pass");
        // ID buffer clears to black, which represents clicking on nothing (background)
        pass.clear(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
        pass.set_view_proj(pick_view, pick_proj);
        pass.bind(surface_.get());

        bool anything_picked = false;
        target_scene->registry->view<transform_component, model_component, active_component>().each(
            [&](auto e, auto&& transform_comp, auto&& model_comp, auto&& active)
            {
                auto& model = model_comp.get_model();
                if(!model.is_valid())
                {
                    return;
                }

                const auto& world_transform = transform_comp.get_transform_global();

                auto lod = model.get_lod(0);
                if(!lod)
                {
                    return;
                }

                const auto& mesh = lod.get();
                const auto& bounds = mesh->get_bounds();

                // Test the bounding box of the mesh
                if(!pick_camera.test_obb(bounds, world_transform))
                    return;

                auto id = ENTT_ID_TYPE(e);
                std::uint32_t rr = (id) & 0xff;
                std::uint32_t gg = (id >> 8) & 0xff;
                std::uint32_t bb = (id >> 16) & 0xff;
                std::uint32_t aa = (id >> 24) & 0xff;

                math::vec4 color_id = {rr / 255.0f, gg / 255.0f, bb / 255.0f, aa / 255.0f};

                anything_picked = true;
                const auto& submesh_transforms = model_comp.get_submesh_transforms();
                const auto& bone_transforms = model_comp.get_bone_transforms();
                const auto& skinning_transforms = model_comp.get_skinning_transforms();

                model::submit_callbacks callbacks;
                callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
                {
                    auto& prog = submit_params.skinned ? program_skinned_ : program_;

                    prog->begin();
                };
                callbacks.setup_params_per_instance = [&](const model::submit_callbacks::params& submit_params)
                {
                    auto& prog = submit_params.skinned ? program_skinned_ : program_;

                    prog->set_uniform("u_id", math::value_ptr(color_id));
                };
                callbacks.setup_params_per_submesh =
                    [&](const model::submit_callbacks::params& submit_params, const material& mat)
                {
                    auto& prog = submit_params.skinned ? program_skinned_ : program_;

                    gfx::set_state(mat.get_render_states());
                    gfx::submit(pass.id, prog->native_handle(), 0, submit_params.preserve_state);
                };
                callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
                {
                    auto& prog = submit_params.skinned ? program_skinned_ : program_;

                    prog->end();
                };

                model.submit(world_transform, submesh_transforms, bone_transforms, skinning_transforms, 0, callbacks);
            });

        gfx::discard();

        if(program_gizmos_)
        {
            gfx::dd_raii dd(pass.id);

            target_scene->registry->view<transform_component, text_component, active_component>().each(
                [&](auto e, auto&& transform_comp, auto&& text_comp, auto&& active)
                {
                    if(!text_comp.can_be_rendered())
                    {
                        return;
                    }
                    const auto& world_transform = transform_comp.get_transform_global();
                    auto bbox = text_comp.get_bounds();

                    if(!pick_camera.test_obb(bbox, world_transform))
                    {
                        return;
                    }

                    auto id = ENTT_ID_TYPE(e);
                    math::color color(id);

                    dd.encoder.setColor(color);
                    dd.encoder.setState(true, true, false, true, false);

                    dd.encoder.pushTransform((const float*)world_transform);
                    bx::Aabb aabb;
                    aabb.min = to_bx(bbox.min);
                    aabb.max = to_bx(bbox.max);
                    dd.encoder.draw(aabb);
                    dd.encoder.popTransform();
                });

            if(em.show_icon_gizmos)
            {
                program_gizmos_->begin();
                dd.encoder.pushProgram(program_gizmos_->native_handle());

                auto& scn = *target_scene;
                hpp::for_each_type<camera_component,
                                   light_component,
                                   reflection_probe_component,
                                   audio_source_component>(
                    [&](auto tag)
                    {
                        using type_t = typename std::decay_t<decltype(tag)>::type;

                        scn.registry->view<type_t>().each(
                            [&](auto e, auto&& comp)
                            {
                                auto entity = scn.create_handle(e);

                                auto& tm = ctx.get_cached<thumbnail_manager>();

                                anything_picked = true;

                                auto id = ENTT_ID_TYPE(e);
                                math::color color(id);

                                dd.encoder.setColor(color);
                                dd.encoder.setState(true, true, false, true);
                                auto& transform_comp = entity.template get<transform_component>();
                                const auto& world_transform = transform_comp.get_transform_global();

                                auto icon = tm.get_gizmo_icon(entity);
                                if(icon)
                                {
                                    if(!pick_camera.test_billboard(em.billboard_data.size, world_transform))
                                        return; // completely outside → skip draw

                                    gfx::draw_billboard(dd.encoder,
                                                        icon->native_handle(),
                                                        to_bx(world_transform.get_position()),
                                                        to_bx(pick_camera.get_position()),
                                                        to_bx(pick_camera.z_unit_axis()),
                                                        em.billboard_data.size);
                                }
                            });
                    });


                dd.encoder.popProgram();
                program_gizmos_->end();
            }

        }

        pick_camera_.reset();
        start_readback_ = anything_picked;

        if(!anything_picked && !pick_callback_)
        {
            em.unselect();
        }
    }

    // If the user previously clicked, and we're done reading data from GPU, look at ID buffer on CPU
    // Whatever mesh has the most pixels in the ID buffer is the one the user clicked on.
    if((reading_ == 0u) && start_readback_)
    {
        bool blit_support = gfx::is_supported(BGFX_CAPS_TEXTURE_BLIT);

        if(blit_support == false)
        {
            APPLOG_WARNING("Texture blitting is not supported. Picking will not work");
            start_readback_ = false;
            return;
        }

        gfx::render_pass pass("picking_buffer_blit_pass");
        pass.touch();
        // Blit and read
        gfx::blit(pass.id, blit_tex_->native_handle(), 0, 0, surface_->get_texture()->native_handle());
        reading_ = gfx::read_texture(blit_tex_->native_handle(), blit_data_.data());
        start_readback_ = false;
    }

    if(reading_ && reading_ <= render_frame)
    {
        reading_ = 0;
        std::map<std::uint32_t, std::uint32_t> ids; // This contains all the IDs found in the buffer
        std::uint32_t max_amount = 0;
        for(std::uint8_t* x = &blit_data_.front(); x < &blit_data_.back();)
        {
            std::uint8_t rr = *x++;
            std::uint8_t gg = *x++;
            std::uint8_t bb = *x++;
            std::uint8_t aa = *x++;

            // Skip background
            // if(0 == (rr | gg | bb | aa))
            // {
            //     continue;
            // }

            auto hash_key = static_cast<std::uint32_t>(rr + (gg << 8) + (bb << 16) + (aa << 24));
            std::uint32_t amount = 1;
            auto map_iter = ids.find(hash_key);
            if(map_iter != ids.end())
            {
                amount = map_iter->second + 1;
            }

            // Amount of times this ID (color) has been clicked on in buffer
            ids[hash_key] = amount;
            max_amount = max_amount > amount ? max_amount : amount;
        }

        ENTT_ID_TYPE id_key = 0;
        if(max_amount != 0u)
        {
            for(auto& pair : ids)
            {
                if(pair.second == max_amount)
                {
                    id_key = pair.first;
                    process_pick_result(ctx, target_scene, id_key);
                    break;
                }
            }
        }
        else
        {
            // If nothing was picked, still call the process_pick_result with id_key = 0
            // This will create an invalid handle that will be passed to the callback
            if(pick_callback_)
            {
                process_pick_result(ctx, target_scene, id_key);
            }
            else
            {
                em.unselect();
            }
        }
        
        // Clear the callback after processing
        pick_callback_ = {};
    }
}

void picking_manager::process_pick_result(rtti::context& ctx, scene* target_scene, ENTT_ID_TYPE id_key)
{
    // Create entity handle (may be invalid if id_key is 0)
    auto entity = entt::entity(id_key);
    entt::handle picked_entity;
    
    // Only try to create a handle if the entity ID is valid
    if (id_key != 0)
    {
        picked_entity = target_scene->create_handle(entity);
    }
    
    if (pick_callback_)
    {
        // Call the custom callback with either a valid entity or an invalid handle
        // Do this because the callback can reassign the pick_callback_ variable
        auto callback = pick_callback_;
        callback(picked_entity, pick_position_);
    }
    else
    {
        // Use the traditional selection mechanism
        auto& em = ctx.get_cached<editing_manager>();
        if (picked_entity)
        {
            em.select(picked_entity, pick_mode_);
        }
        else
        {
            em.unselect();
        }
    }
}

picking_manager::picking_manager()
{
}

picking_manager::~picking_manager()
{
}

auto picking_manager::init(rtti::context& ctx) -> bool
{
    auto& ev = ctx.get_cached<events>();
    ev.on_frame_render.connect(sentinel_, 850, this, &picking_manager::on_frame_render);

    auto& am = ctx.get_cached<asset_manager>();

    // Set up ID buffer, which has a color target and depth buffer
    auto picking_rt =
        std::make_shared<gfx::texture>(tex_id_dim,
                                       tex_id_dim,
                                       false,
                                       1,
                                       gfx::texture_format::RGBA8,
                                       0 | BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                           BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

    auto picking_rt_depth =
        std::make_shared<gfx::texture>(tex_id_dim,
                                       tex_id_dim,
                                       false,
                                       1,
                                       gfx::texture_format::D24S8,
                                       0 | BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                           BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

    std::vector<std::shared_ptr<gfx::texture>> textures{picking_rt, picking_rt_depth};
    surface_ = std::make_shared<gfx::frame_buffer>(textures);

    // CPU texture for blitting to and reading ID buffer so we can see what was clicked on.
    // Impossible to read directly from a render target, you *must* blit to a CPU texture
    // first. Algorithm Overview: Render on GPU -> Blit to CPU texture -> Read from CPU
    // texture.
    blit_tex_ = std::make_shared<gfx::texture>(
        tex_id_dim,
        tex_id_dim,
        false,
        1,
        gfx::texture_format::RGBA8,
        0 | BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
            BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

    auto vs = am.get_asset<gfx::shader>("editor:/data/shaders/vs_picking_id.sc");
    auto vs_skinned = am.get_asset<gfx::shader>("editor:/data/shaders/vs_picking_id_skinned.sc");
    auto fs = am.get_asset<gfx::shader>("editor:/data/shaders/fs_picking_id.sc");

    program_ = std::make_unique<gpu_program>(vs, fs);
    program_skinned_ = std::make_unique<gpu_program>(vs_skinned, fs);

    auto vs_gizmos = am.get_asset<gfx::shader>("editor:/data/shaders/vs_picking_debugdraw_fill_texture.sc");
    auto fs_gizmos = am.get_asset<gfx::shader>("editor:/data/shaders/fs_picking_debugdraw_fill_texture.sc");
    program_gizmos_ = std::make_unique<gpu_program>(vs_gizmos, fs_gizmos);

    return true;
}

auto picking_manager::deinit(rtti::context& ctx) -> bool
{
    return true;
}

void picking_manager::setup_pick_camera(math::vec2 pos, const camera& cam)
{
    const auto near_clip = cam.get_near_clip();
    const auto far_clip = cam.get_far_clip();
    const auto& frustum = cam.get_frustum();
    math::vec3 pick_eye;
    math::vec3 pick_at;
    math::vec3 pick_up = cam.y_unit_axis();

    if(!cam.viewport_to_world(pos, frustum.planes[math::volume_plane::near_plane], pick_eye, true))
        return;

    if(!cam.viewport_to_world(pos, frustum.planes[math::volume_plane::far_plane], pick_at, true))
        return;

    camera pick_camera;
    pick_camera.set_aspect_ratio(1.0f);
    pick_camera.set_fov(1.0f);
    pick_camera.set_near_clip(near_clip);
    pick_camera.set_far_clip(far_clip);
    pick_camera.look_at(pick_eye, pick_at, pick_up);

    pick_camera_ = pick_camera;
    pick_position_ = pos;

    reading_ = 0;
    start_readback_ = true;
}

void picking_manager::request_pick(math::vec2 pos, const camera& cam, editing_manager::select_mode mode)
{
    setup_pick_camera(pos, cam);
    pick_mode_ = mode;
    pick_callback_ = {}; // Clear any existing callback
}

void picking_manager::query_pick(math::vec2 pos, const camera& cam, pick_callback callback, bool force)
{
    // If already picking, ignore this request
    if (!force && is_picking())
    {
        return;
    }
    
    // Set up the pick operation
    setup_pick_camera(pos, cam);
    pick_callback_ = callback;
}

auto picking_manager::is_picking() const -> bool
{
    return pick_camera_.has_value() || reading_ != 0;
}

auto picking_manager::get_pick_texture() const -> const std::shared_ptr<gfx::texture>&
{
    return blit_tex_;
}

} // namespace unravel
