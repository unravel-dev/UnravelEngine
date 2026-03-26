#include "picking_manager.h"
#include "thumbnail_manager.h"

#include <graphics/debugdraw.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <logging/logging.h>
#include <engine/profiler/profiler.h>

#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/events.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ui/ecs/components/ui_document_component.h>
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

// Helper functions for different picking types
namespace
{
// Type 1: Position-only picking
bool is_position_in_selection_area(const math::vec3& world_position,
                                   const camera& pick_camera,
                                   const math::vec2& pick_position,
                                   const math::vec2& pick_area)
{
    // Project position to screen space
    math::vec3 screen_pos = pick_camera.world_to_viewport(world_position);

    // Check if position is within the selection rectangle
    return (
        screen_pos.x >= pick_position.x - pick_area.x * 0.5f && screen_pos.x <= pick_position.x + pick_area.x * 0.5f &&
        screen_pos.y >= pick_position.y - pick_area.y * 0.5f && screen_pos.y <= pick_position.y + pick_area.y * 0.5f);
}

// Type 3: Global bounds (already world-space)
bool are_corners_in_selection_area(hpp::span<const math::vec3> corners,
                                   const camera& pick_camera,
                                   const math::vec2& pick_position,
                                   const math::vec2& pick_area)
{
    // Check if all corners are in selection area
    bool all_corners_in_selection = true;
    for(int i = 0; i < 8; ++i)
    {
        // Project to screen space (corners are already in world space)
        math::vec3 screen_pos = pick_camera.world_to_viewport(corners[i]);

        // Check if this corner is within the selection rectangle
        bool corner_in_selection = (screen_pos.x >= pick_position.x - pick_area.x * 0.5f &&
                                    screen_pos.x <= pick_position.x + pick_area.x * 0.5f &&
                                    screen_pos.y >= pick_position.y - pick_area.y * 0.5f &&
                                    screen_pos.y <= pick_position.y + pick_area.y * 0.5f);

        all_corners_in_selection &= corner_in_selection;
    }

    return all_corners_in_selection;
}

// Type 2: Local bounds with transform (existing logic)
bool are_local_bounds_in_selection_area(const math::bbox& local_bounds,
                                        const math::transform& world_transform,
                                        const camera& pick_camera,
                                        const math::vec2& pick_position,
                                        const math::vec2& pick_area)
{
    // Generate bounding box corners
    math::vec3 corners[8] = {
        world_transform.transform_coord({local_bounds.min.x, local_bounds.min.y, local_bounds.min.z}),
        world_transform.transform_coord({local_bounds.max.x, local_bounds.min.y, local_bounds.min.z}),
        world_transform.transform_coord({local_bounds.min.x, local_bounds.max.y, local_bounds.min.z}),
        world_transform.transform_coord({local_bounds.max.x, local_bounds.max.y, local_bounds.min.z}),
        world_transform.transform_coord({local_bounds.min.x, local_bounds.min.y, local_bounds.max.z}),
        world_transform.transform_coord({local_bounds.max.x, local_bounds.min.y, local_bounds.max.z}),
        world_transform.transform_coord({local_bounds.min.x, local_bounds.max.y, local_bounds.max.z}),
        world_transform.transform_coord({local_bounds.max.x, local_bounds.max.y, local_bounds.max.z})};

    return are_corners_in_selection_area(corners, pick_camera, pick_position, pick_area);
}

// Type 3: Global bounds (already world-space)
bool are_global_bounds_in_selection_area(const math::bbox& world_bounds,
                                         const camera& pick_camera,
                                         const math::vec2& pick_position,
                                         const math::vec2& pick_area)
{
    // Generate bounding box corners (already in world space)
    math::vec3 corners[8] = {{world_bounds.min.x, world_bounds.min.y, world_bounds.min.z},
                             {world_bounds.max.x, world_bounds.min.y, world_bounds.min.z},
                             {world_bounds.min.x, world_bounds.max.y, world_bounds.min.z},
                             {world_bounds.max.x, world_bounds.max.y, world_bounds.min.z},
                             {world_bounds.min.x, world_bounds.min.y, world_bounds.max.z},
                             {world_bounds.max.x, world_bounds.min.y, world_bounds.max.z},
                             {world_bounds.min.x, world_bounds.max.y, world_bounds.max.z},
                             {world_bounds.max.x, world_bounds.max.y, world_bounds.max.z}};

    return are_corners_in_selection_area(corners, pick_camera, pick_position, pick_area);
}
} // namespace

namespace
{
/**
 * @brief Finds the model component that owns the given armature entity.
 * @param registry The registry to search in.
 * @param armature_entity The entity that might be part of an armature.
 * @return Handle to the entity with model_component that owns this armature, or empty handle if not found.
 */
auto find_model_owner(entt::registry& registry, entt::handle armature_entity) -> entt::handle
{
    if(!armature_entity)
    {
        return {};
    }
    auto view = registry.view<model_component>();
    for(auto e : view)
    {
        entt::handle model_entity(registry, e);
        auto& model_comp = view.get<model_component>(e);
        const auto& armature_entities = model_comp.get_armature_entities();
        for(const auto& arm_ent : armature_entities)
        {
            if(arm_ent == armature_entity)
            {
                return model_entity;
            }
        }
    }
    return {};
}

/**
 * @brief Checks if two entities belong to the same imported model (armature).
 * @param registry The registry to search in.
 * @param entity1 First entity to check.
 * @param entity2 Second entity to check.
 * @return True if both entities belong to the same model's armature, false otherwise.
 */
auto are_in_same_model(entt::registry& registry, entt::handle entity1, entt::handle entity2) -> bool
{
    if(!entity1 || !entity2)
    {
        return false;
    }
    auto model_owner1 = find_model_owner(registry, entity1);
    auto model_owner2 = find_model_owner(registry, entity2);
    if(!model_owner1 || !model_owner2)
    {
        return false;
    }
    return model_owner1 == model_owner2;
}

/**
 * @brief Gets the logical top-level entity following Unity's selection logic.
 * Walks up the entire parent hierarchy to the root, records depths of markers,
 * then selects based on priority:
 * 1. First priority: Prefab root (prefab_component) - closest to root wins
 * 2. Second priority: Animation (animation_component) - closest to root wins
 * 3. Third priority: Root with no parent inside the same imported model (armature nodes)
 * @param registry The registry to search in.
 * @param entity The entity to start from.
 * @return The logical top-level entity handle.
 */
auto get_logical_top_level_entity(entt::registry& registry, entt::handle entity) -> entt::handle
{
    if(!entity)
    {
        return {};
    }
    entt::handle starting_model_owner = {};
    bool is_starting_in_armature = false;
    auto starting_model = find_model_owner(registry, entity);
    if(starting_model)
    {
        starting_model_owner = starting_model;
        is_starting_in_armature = true;
    }
    struct marker_info
    {
        entt::handle entity;
        int depth;
    };
    hpp::optional<marker_info> prefab_marker;
    hpp::optional<marker_info> animation_marker;
    hpp::optional<marker_info> model_root_marker;
    entt::handle last_in_same_model = entity;
    auto current = entity;
    int depth = 0;
    while(current)
    {
        if(current.try_get<prefab_component>())
        {
            if(!prefab_marker || prefab_marker->depth > depth)
            {
                prefab_marker = {current, depth};
            }
        }
        if(current.try_get<animation_component>())
        {
            if(!animation_marker || animation_marker->depth > depth)
            {
                animation_marker = {current, depth};
            }
        }
        auto* transform = current.try_get<transform_component>();
        if(!transform)
        {
            break;
        }
        auto parent = transform->get_parent();
        if(!parent)
        {
            if(is_starting_in_armature)
            {
                auto current_model = find_model_owner(registry, current);
                if(current_model == starting_model_owner)
                {
                    if(!model_root_marker || model_root_marker->depth > depth)
                    {
                        model_root_marker = {current, depth};
                    }
                }
            }
            else
            {
                if(!model_root_marker || model_root_marker->depth > depth)
                {
                    model_root_marker = {current, depth};
                }
            }
            break;
        }
        if(is_starting_in_armature)
        {
            if(are_in_same_model(registry, current, parent))
            {
                last_in_same_model = current;
            }
            else
            {
                if(!model_root_marker || model_root_marker->depth > depth)
                {
                    model_root_marker = {last_in_same_model, depth};
                }
                is_starting_in_armature = false;
            }
        }
        current = parent;
        depth++;
    }
    if(prefab_marker)
    {
        return prefab_marker->entity;
    }
    if(animation_marker)
    {
        return animation_marker->entity;
    }
    if(model_root_marker)
    {
        return model_root_marker->entity;
    }
    return entity;
}

/**
 * @brief Checks if a potential ancestor is an ancestor (direct or indirect parent) of a child entity.
 * @param potential_ancestor The entity that might be an ancestor.
 * @param child The entity to check ancestry for.
 * @return True if potential_ancestor is an ancestor of child, false otherwise.
 */
auto is_ancestor_of(entt::handle potential_ancestor, entt::handle child) -> bool
{
    if(!child || !potential_ancestor)
    {
        return false;
    }
    auto* transform = child.try_get<transform_component>();
    if(!transform)
    {
        return false;
    }
    entt::handle current = transform->get_parent();
    while(current)
    {
        if(current == potential_ancestor)
        {
            return true;
        }
        auto* current_transform = current.try_get<transform_component>();
        if(!current_transform)
        {
            break;
        }
        current = current_transform->get_parent();
    }
    return false;
}

/**
 * @brief Checks if selection should be skipped because a parent/ancestor is already selected in additive mode.
 * @param em The editing manager to check current selections from.
 * @param pick_mode The current pick mode (normal, ctrl, or shift).
 * @param picked_entity The entity that was picked.
 * @return True if selection should be skipped, false otherwise.
 */
auto should_skip_selection_for_additive_pick(const editing_manager& em,
                                             editing_manager::select_mode pick_mode,
                                             const math::vec2& pick_area,
                                             entt::handle picked_entity) -> bool
{
    bool is_area_picking = pick_area.x > 0.0f && pick_area.y > 0.0f;


    if(!is_area_picking)
    {
        return false;
    }
    if(!picked_entity)
    {
        return false;
    }
    if(pick_mode != editing_manager::select_mode::shift && pick_mode != editing_manager::select_mode::ctrl)
    {
        return false;
    }
    auto selections = em.get_selections();
    for(const auto& selected_obj : selections)
    {
        if(selected_obj.type() == entt::resolve<entt::handle>())
        {
            auto selected_entity = selected_obj.cast<const entt::handle&>();
            if(is_ancestor_of(selected_entity, picked_entity))
            {
                return true;
            }
        }
    }
    return false;
}

} // namespace

constexpr int picking_manager::tex_id_dim;
void picking_manager::on_frame_render(rtti::context& ctx, delta_t dt)
{
    on_frame_pick(ctx, dt);
}

void picking_manager::on_frame_pick(rtti::context& ctx, delta_t dt)
{
    APP_SCOPE_PERF("On Frame Pick");
    auto& em = ctx.get_cached<editing_manager>();

    // Get the appropriate scene based on edit mode
    scene* target_scene = em.get_active_scene(ctx);

    if(!target_scene)
    {
        return;
    }

    if(pick_area_.x > 0.0f && pick_area_.y > 0.0f && pick_camera_)
    {
        const auto& pick_camera = *pick_camera_;

        auto on_pick_failed = [&](auto e)
        {
            auto ue = target_scene->create_handle(e);


            if(std::find(picked_entities_.begin(), picked_entities_.end(), ue) == picked_entities_.end())
            {
                em.unselect(ue);
            }

        };

        auto on_pick_success = [&](auto e)
        {
            auto id = ENTT_ID_TYPE(e);
            process_pick_result(ctx, target_scene, id);
        };

        // Area picking supports three types of entities:
        // Type 1: Position-only picking for entities with just transform_component (no visual bounds)
        target_scene->registry->view<transform_component, active_component>().each(
            [&](auto e, auto&& transform_comp, auto&& active)
            {
                const auto& world_transform = transform_comp.get_transform_global();
                const auto& world_position = world_transform.get_position();

               

                if(!pick_camera.get_frustum().test_point(world_position))
                {
                    on_pick_failed(e);
                    return;
                }

                // Test if position is in selection area
                if(!is_position_in_selection_area(world_position, pick_camera, pick_position_, pick_area_))
                {
                    on_pick_failed(e);
                    return;
                }

                on_pick_success(e);
            });

        pick_camera_.reset();
        original_camera_.reset();
        pick_position_ = {};
        // pick_area_ = {};

        // picked_entities_.clear();
        return;
    }

    const auto render_frame = gfx::get_render_frame();

    picked_entities_.clear();
    if(pick_camera_ && original_camera_)
    {
        const auto& pick_camera = *pick_camera_;
        const auto& original_camera = *original_camera_;

        const auto& pick_view = pick_camera.get_view();
        const auto& pick_proj = pick_camera.get_projection();

        gfx::render_pass pass("Picking Buffer Pass");
        // ID buffer clears to black, which represents clicking on nothing (background)
        pass.clear(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
        pass.set_view_proj(pick_view, pick_proj);
        pass.bind(surface_.get());

        bool anything_picked = false;

        // Regular picking (render-to-texture) supports:
        // Type 2: Model components - rendered with actual geometry
        // Type 1 & 3: Other components - handled via gizmo icons (see gizmo section below)
        target_scene->registry->view<transform_component, model_component, active_component>().each(
            [&](auto e, auto&& transform_comp, auto&& model_comp, auto&& active)
            {
                auto& model = model_comp.get_model();
                if(!model.is_valid())
                {
                    return;
                }

                const auto& world_transform = transform_comp.get_transform_global();

                auto& current_lod_data = model_comp.get_lod_data_for_camera(&original_camera, gfx::get_render_frame());

                auto lod = model.get_lod(current_lod_data.current_lod_index);
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

                model.submit(world_transform, submesh_transforms, bone_transforms, skinning_transforms, current_lod_data.current_lod_index, callbacks);
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

            target_scene->registry->view<transform_component, ui_document_component, active_component>().each(
                [&](auto e, auto&& transform_comp, auto&& ui_document_comp, auto&& active)
                {
                    const auto& world_transform = transform_comp.get_transform_global();
                    auto size = ui_document_comp.get_world_space_scale();
                    math::bbox bbox;
                    bbox.min = math::vec3(-size.x * 0.5f, -size.y * 0.5f, 0.0f);
                    bbox.max = math::vec3(size.x * 0.5f, size.y * 0.5f, 0.0f);

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
                                   audio_source_component,
                                   particle_emitter_component>(
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

                                if constexpr(std::is_same<type_t, particle_emitter_component>())
                                {
                                    if(!em.billboard_data.show_particle_emitter)
                                    {
                                        return;
                                    }
                                }

                                if constexpr(std::is_same<type_t, audio_source_component>())
                                {
                                    if(!em.billboard_data.show_audio_source)
                                    {
                                        return;
                                    }
                                }
                                
                                if constexpr(std::is_same<type_t, reflection_probe_component>())
                                {
                                    if(!em.billboard_data.show_reflection_probe)
                                    {
                                        return;
                                    }
                                }
                                
                                
                                if constexpr(std::is_same<type_t, light_component>())
                                {
                                    if(!em.billboard_data.show_light)
                                    {
                                        return;
                                    }
                                }
                                
                                if constexpr(std::is_same<type_t, camera_component>())
                                {
                                    if(!em.billboard_data.show_camera)
                                    {
                                        return;
                                    }
                                }
                                
                                

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
        original_camera_.reset();
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

        gfx::render_pass pass("Picking Buffer Blit Pass");
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
    auto& em = ctx.get_cached<editing_manager>();

    // Only try to create a handle if the entity ID is valid
    if(id_key != 0)
    {
        picked_entity = target_scene->create_handle(entity);
        if(picked_entity)
        {
            auto logical_pick = get_logical_top_level_entity(*target_scene->registry, picked_entity);

            if(!em.is_selected(logical_pick))
            {
                picked_entity = logical_pick;
            }
        }
    }

    if(pick_callback_)
    {
        // Call the custom callback with either a valid entity or an invalid handle
        // Do this because the callback can reassign the pick_callback_ variable
        auto callback = pick_callback_;
        callback(picked_entity, pick_position_);
    }
    else
    {
        // Use the traditional selection mechanism
        if(picked_entity)
        {
            if(!should_skip_selection_for_additive_pick(em, pick_mode_, pick_area_, picked_entity))
            {
                em.select(picked_entity, pick_mode_);
            }
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

void picking_manager::setup_pick_camera(const camera& cam, math::vec2 pos, math::vec2 area)
{
    camera pick_camera;
    if(area.x > 0.0f && area.y > 0.0f)
    {
        // Area picking: copy the passed camera and adjust for the selection area
        pick_camera = cam; // Copy the passed camera
    }
    else
    {
        // Single point picking (existing logic)
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

        pick_camera.set_aspect_ratio(1.0f);
        pick_camera.set_fov(1.0f);
        pick_camera.set_near_clip(near_clip);
        pick_camera.set_far_clip(far_clip);
        pick_camera.look_at(pick_eye, pick_at, pick_up);
    }
    original_camera_ = cam;
    pick_camera_ = pick_camera;
    pick_position_ = pos;
    pick_area_ = area;
    reading_ = 0;
    start_readback_ = true;
}

void picking_manager::cancel_pick()
{
    pick_camera_.reset();
    original_camera_.reset();
    pick_position_ = {};
    pick_area_ = {};
    reading_ = 0;
    start_readback_ = false;
    picked_entities_.clear();
}

void picking_manager::request_pick(const camera& cam,
                                   editing_manager::select_mode mode,
                                   math::vec2 pos,
                                   math::vec2 area)
{
    bool was_area_picking = pick_area_.x > 0.0f && pick_area_.y > 0.0f;
    bool is_area_picking = area.x > 0.0f && area.y > 0.0f;

    setup_pick_camera(cam, pos, area);
    pick_mode_ = mode;

    if(is_area_picking)
    {
        if(!was_area_picking)
        {
            if(mode == editing_manager::select_mode::normal)
            {
                picked_entities_.clear();
            }
            else
            {
                auto& ctx = engine::context();
                auto& em = ctx.get_cached<editing_manager>();
                picked_entities_ = em.try_get_selections_as_copy<entt::handle>();
            }
        }
     
        
        pick_mode_ = editing_manager::select_mode::shift;
    }

    pick_callback_ = {}; // Clear any existing callback
}

void picking_manager::query_pick(math::vec2 pos, const camera& cam, pick_callback callback, bool force)
{
    // If already picking, ignore this request
    if(!force && is_picking())
    {
        return;
    }

    // Set up the pick operation
    setup_pick_camera(cam, pos);
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
