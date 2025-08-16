#include "scene_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "imgui_widgets/utils.h"
#include <editor/editing/editing_manager.h>
#include <editor/editing/picking_manager.h>
#include <editor/editing/thumbnail_manager.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/shortcuts.h>

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>

#include <engine/rendering/ecs/systems/model_system.h>
#include <engine/rendering/ecs/systems/rendering_system.h>

#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/material.h>
#include <engine/rendering/renderer.h>
#include <seq/seq.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/gizmo.h>


#include <algorithm>
#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <numeric>

namespace unravel
{
namespace
{

// Forward declarations
void restore_original_materials(entt::handle entity, const std::vector<asset_handle<material>>& original_materials);
void apply_material_preview(rtti::context& ctx, entt::handle entity, const std::string& material_path, 
                          entt::handle& last_preview_entity, std::vector<asset_handle<material>>& original_materials,
                          bool& is_previewing);
void manipulation_gizmos(bool& gizmo_at_center, entt::handle center, entt::handle editor_camera, editing_manager& em);
void handle_camera_movement(entt::handle camera, math::vec3& move_dir, float& acceleration, bool& is_dragging);

// Material preview state
struct material_preview_state
{
    entt::handle last_preview_entity;
    std::vector<asset_handle<material>> original_materials;
    bool is_previewing = false;
    std::string current_drag_material;
};

// Global preview state
static material_preview_state g_preview_state;

// Check if a material is being dragged and get its path
auto check_material_drag(std::string& out_material_path) -> bool
{
    for(const auto& type : ex::get_suported_formats<material>())
    {
        auto payload = ImGui::GetDragDropPayload();
        if(payload && payload->IsDataType(type.c_str()))
        {
            if(payload->Data)
            {
                out_material_path = std::string(reinterpret_cast<const char*>(payload->Data), std::size_t(payload->DataSize));
                out_material_path = fs::convert_to_protocol(fs::path(out_material_path)).generic_string();
                return true;
            }
        }
    }
    return false;
}

// Handle material preview during drag
void handle_material_preview(rtti::context& ctx, const camera_component& camera_comp, const std::string& material_path)
{
    auto& pick_manager = ctx.get_cached<picking_manager>();
    
    // Check if the material path changed
    if(g_preview_state.current_drag_material != material_path)
    {
        // Restore previous preview if there was one
        if(g_preview_state.is_previewing && g_preview_state.last_preview_entity)
        {
            restore_original_materials(g_preview_state.last_preview_entity, g_preview_state.original_materials);
        }
        
        // Update current material
        g_preview_state.current_drag_material = material_path;
        g_preview_state.is_previewing = false;
    }
    
    // Query for entity under cursor to show preview
    // picking_manager handles throttling internally
    auto cursor_pos = ImGui::GetMousePos();
    pick_manager.query_pick(
        math::vec2{cursor_pos.x, cursor_pos.y}, 
        camera_comp.get_camera(),
        [&ctx, material_path](entt::handle entity, const math::vec2& screen_pos) {
            apply_material_preview(ctx, entity, material_path, g_preview_state.last_preview_entity, 
                                  g_preview_state.original_materials, g_preview_state.is_previewing);
        }
    );
}

// Handle material drop on entity
void handle_material_drop(rtti::context& ctx, const camera_component& camera_comp, const std::string& material_path)
{
    auto cursor_pos = ImGui::GetMousePos();
    auto& pick_manager = ctx.get_cached<picking_manager>();
    auto& am = ctx.get_cached<asset_manager>();
    
    // Load the material asset
    auto material_asset = am.get_asset<material>(material_path);
    bool force = true;

    // Use the picking system to query what's under the cursor
    pick_manager.query_pick(
        math::vec2{cursor_pos.x, cursor_pos.y}, 
        camera_comp.get_camera(),
        [material_asset, &ctx](entt::handle entity, const math::vec2& screen_pos) {
            // Check if entity has a model component
            if(entity && entity.all_of<model_component>())
            {
                // Apply material to the model
                auto& model_comp = entity.get<model_component>();
                
                // Get a non-const model by setting it back to itself
                class model model_copy = model_comp.get_model();
                
                // Apply material to all submeshes
                for(size_t i = 0; i < model_copy.get_materials().size(); ++i)
                {
                    model_copy.set_material(material_asset, i);
                }
                
                // Update the model in the component
                model_comp.set_model(model_copy);
                
                prefab_override_context::mark_material_as_changed(entity);
                // Log success
                APPLOG_INFO("Applied material '{}' to {}", material_asset.id(), entity_panel::get_entity_name(entity) );

            }
            else if(entity)
            {
                APPLOG_WARNING("Cannot apply material to entity without model_component");
            }
        }, force
    );
}

// Handle mesh drop at cursor position
void handle_mesh_drop(rtti::context& ctx, const camera_component& camera_comp, const std::string& mesh_path)
{
    auto cursor_pos = ImGui::GetMousePos();
    auto& em = ctx.get_cached<editing_manager>();

    em.add_action("Drop Mesh",
        [&ctx, camera = camera_comp.get_camera(), mesh_path, cursor_pos]()
    {
        auto& em = ctx.get_cached<editing_manager>();
        auto target_scene = em.get_active_scene(ctx);
        
        auto object = defaults::create_mesh_entity_at(ctx,
                                                *target_scene,
                                                mesh_path,
                                                camera,
                                                math::vec2{cursor_pos.x, cursor_pos.y});
        em.select(object);
    });
}

// Handle prefab drop at cursor position
void handle_prefab_drop(rtti::context& ctx, const camera_component& camera_comp, const std::string& prefab_path)
{
    auto cursor_pos = ImGui::GetMousePos();
    auto& em = ctx.get_cached<editing_manager>();

    em.add_action("Drop Prefab",
        [&ctx, camera = camera_comp.get_camera(), prefab_path, cursor_pos]()
    {
        auto& em = ctx.get_cached<editing_manager>();
        auto target_scene = em.get_active_scene(ctx);
        
        auto object = defaults::create_prefab_at(ctx,
                                          *target_scene,
                                          prefab_path,
                                          camera,
                                          math::vec2{cursor_pos.x, cursor_pos.y});
        em.select(object);
    });
}

// Reset material preview state
void reset_preview_state()
{
    if(g_preview_state.is_previewing && g_preview_state.last_preview_entity)
    {
        restore_original_materials(g_preview_state.last_preview_entity, g_preview_state.original_materials);
        g_preview_state.is_previewing = false;
        g_preview_state.last_preview_entity = {};
        g_preview_state.original_materials.clear();
        g_preview_state.current_drag_material.clear();
    }
}

// ============================================================================
// Camera Movement Helper Functions
// ============================================================================

auto calculate_movement_speed(float base_speed, bool speed_boost_active, float multiplier) -> float
{
    float movement_speed = base_speed;
    if(speed_boost_active)
    {
        movement_speed *= multiplier;
    }
    return movement_speed;
}

void handle_middle_mouse_panning(entt::handle camera, float movement_speed, float dt)
{
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        return;
    }

    auto delta_move = ImGui::GetIO().MouseDelta;
    auto& transform = camera.get<transform_component>();

    if(delta_move.x != 0)
    {
        transform.move_by_local({-1 * delta_move.x * movement_speed * dt, 0.0f, 0.0f});
    }
    if(delta_move.y != 0)
    {
        transform.move_by_local({0.0f, delta_move.y * movement_speed * dt, 0.0f});
    }
}

auto collect_movement_input(float& max_hold) -> math::vec3
{
    math::vec3 movement_input{0.0f, 0.0f, 0.0f};

    auto is_key_down = [&](ImGuiKey k) -> bool
    {
        bool down = ImGui::IsKeyDown(k);
        if(down)
        {
            auto data = ImGui::GetKeyData(ImGui::GetCurrentContext(), k);
            max_hold = std::max(max_hold, data->DownDuration);
        }
        return down;
    };

    if(is_key_down(shortcuts::camera_forward))
    {
        movement_input.z += 1.0f;
    }
    if(is_key_down(shortcuts::camera_backward))
    {
        movement_input.z -= 1.0f;
    }
    if(is_key_down(shortcuts::camera_right))
    {
        movement_input.x += 1.0f;
    }
    if(is_key_down(shortcuts::camera_left))
    {
        movement_input.x -= 1.0f;
    }

    auto delta_wheel = ImGui::GetIO().MouseWheel;
    if(delta_wheel != 0)
    {
        movement_input.z += 15.0f * delta_wheel;
    }

    return movement_input;
}

auto handle_mouse_rotation(entt::handle camera, float rotation_speed)-> bool
{
    auto delta_move = ImGui::GetIO().MouseDelta;
    auto& transform = camera.get<transform_component>();

    if(delta_move.x != 0.0f || delta_move.y != 0.0f)
    {
        float dx = delta_move.x * rotation_speed;
        float dy = delta_move.y * rotation_speed;

        transform.rotate_by_euler_global({0.0f, dx, 0.0f});
        transform.rotate_by_euler_local({dy, 0.0f, 0.0f});
        return true;
    }
    return false;
}

void update_movement_acceleration(math::vec3& move_dir, float& acceleration, const math::vec3& input, bool any_input)
{
    if(any_input)
    {
        if(acceleration < 0.1f)
        {
            acceleration = 0.1f;
        }
        acceleration *= 1.5f;
        acceleration = std::min(1.0f, acceleration);
        move_dir.x = input.x;
        move_dir.z = input.z;
    }
    else if(acceleration > 0.0001f)
    {
        acceleration *= 0.75f;
    }
}

void apply_movement(entt::handle camera,
                    const math::vec3& move_dir,
                    float movement_speed,
                    float acceleration,
                    float max_hold,
                    float hold_speed,
                    float dt)
{
    if(acceleration <= 0.0001f)
    {
        return;
    }

    auto& transform = camera.get<transform_component>();

    if(!math::any(math::epsilonNotEqual(move_dir, math::vec3(0.0f, 0.0f, 0.0f), math::epsilon<float>())))
    {
        return;
    }

    float adjusted_dt = dt;
    if(math::epsilonNotEqual(move_dir.x, 0.0f, math::epsilon<float>()) ||
       math::epsilonNotEqual(move_dir.z, 0.0f, math::epsilon<float>()))
    {
        adjusted_dt += max_hold * hold_speed;
    }

    auto length = math::length(move_dir);
    transform.move_by_local(math::normalize(move_dir) * length * movement_speed * adjusted_dt * acceleration);


}

void handle_camera_movement(entt::handle camera, math::vec3& move_dir, float& acceleration, bool& is_dragging)
{
    if(!ImGui::IsWindowFocused())
    {
        return;
    }

    if(!ImGui::IsWindowHovered() && !is_dragging)
    {
        return;
    }

    // Movement parameters
    constexpr float base_movement_speed = 2.0f;
    constexpr float rotation_speed = 0.1f;
    constexpr float speed_multiplier = 5.0f;
    constexpr float hold_speed = 0.1f;
    constexpr float fixed_dt = 0.0166f; // Fixed delta time

    bool speed_boost_active = ImGui::IsKeyDown(shortcuts::modifier_camera_speed_boost);
    float movement_speed = calculate_movement_speed(base_movement_speed, speed_boost_active, speed_multiplier);

    // Handle middle mouse panning
    handle_middle_mouse_panning(camera, movement_speed, fixed_dt);

    // Handle right mouse dragging
    is_dragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);

    if(is_dragging)
    {
        ImGui::WrapMousePos();
        if(ImGui::IsWindowHovered())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Cross);
        }

        // Collect movement input
        float max_hold = 0.0f;
        math::vec3 movement_input = collect_movement_input(max_hold);
        bool any_input = math::any(math::epsilonNotEqual(movement_input, math::vec3(0.0f), math::epsilon<float>()));

        // Handle mouse rotation
        bool any_rotation = handle_mouse_rotation(camera, rotation_speed);

        // Update movement acceleration and direction
        update_movement_acceleration(move_dir, acceleration, movement_input, any_input);

        // Apply movement
        apply_movement(camera, move_dir, movement_speed, acceleration, max_hold, hold_speed, fixed_dt);

        
        if(any_input || any_rotation)
        {
            seq::scope::stop_all("camera_focus");
        }
    }
    else if(acceleration > 0.0001f)
    {
        // Continue movement with deceleration when not actively inputting
        apply_movement(camera, move_dir, movement_speed, acceleration, 0.0f, hold_speed, fixed_dt);
        acceleration *= 0.75f;
    }
}

// ============================================================================
// Gizmo Manipulation Helper Functions
// ============================================================================

void setup_gizmo_context(const camera_component& camera_comp)
{
    auto p = ImGui::GetItemRectMin();
    auto s = ImGui::GetItemRectSize();
    const auto& camera = camera_comp.get_camera();

    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(p.x, p.y, s.x, s.y);
    ImGuizmo::SetOrthographic(camera.get_projection_mode() == projection_mode::orthographic);
}

void handle_view_manipulator(entt::handle editor_camera, const camera_component& camera_comp)
{
    auto p = ImGui::GetItemRectMin();
    auto s = ImGui::GetItemRectSize();
    const auto& camera = camera_comp.get_camera();
    auto& camera_trans = editor_camera.get<transform_component>();

    auto view = camera.get_view().get_matrix();
    static const ImVec2 view_gizmo_sz(100.0f, 100.0f);

    ImGuizmo::ViewManipulate(value_ptr(view),
                             1.0f,
                             p + ImVec2(s.x - view_gizmo_sz.x, 0.0f),
                             view_gizmo_sz,
                             ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f)));

    math::transform tr = glm::inverse(view);
    camera_trans.set_rotation_local(tr.get_rotation());
}

void handle_gizmo_shortcuts(editing_manager& em)
{
    if(ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsAnyItemActive() || ImGuizmo::IsUsing())
    {
        return;
    }

    if(ImGui::IsKeyPressed(shortcuts::universal_tool))
    {
        em.operation = ImGuizmo::OPERATION::UNIVERSAL;
    }
    if(ImGui::IsKeyPressed(shortcuts::move_tool))
    {
        em.operation = ImGuizmo::OPERATION::TRANSLATE;
    }
    if(ImGui::IsKeyPressed(shortcuts::rotate_tool))
    {
        em.operation = ImGuizmo::OPERATION::ROTATE;
    }
    if(ImGui::IsKeyPressed(shortcuts::scale_tool))
    {
        em.operation = ImGuizmo::OPERATION::SCALE;
    }
    if(ImGui::IsKeyPressed(shortcuts::bounds_tool))
    {
        em.operation = ImGuizmo::OPERATION::BOUNDS;
    }
}

void setup_snap_data(editing_manager& em, float*& snap, float*& bounds_snap, float bounds_snap_data[3])
{
    snap = nullptr;
    bounds_snap = nullptr;

    if(!ImGui::IsKeyDown(shortcuts::modifier_snapping))
    {
        return;
    }

    bounds_snap = bounds_snap_data;

    if(em.operation == ImGuizmo::OPERATION::TRANSLATE)
    {
        snap = &em.snap_data.translation_snap[0];
    }
    else if(em.operation == ImGuizmo::OPERATION::ROTATE)
    {
        snap = &em.snap_data.rotation_degree_snap;
    }
    else if(em.operation == ImGuizmo::OPERATION::SCALE)
    {
        snap = &em.snap_data.scale_snap;
    }
}

auto calculate_center_pivot(const std::vector<entt::handle*>& selections) -> math::vec3
{
    math::vec3 pivot{0.0f, 0.0f, 0.0f};
    size_t points = 0;

    for(const auto& sel : selections)
    {
        if(sel && *sel)
        {
            auto& sel_transform_comp = sel->get<transform_component>();
            pivot += sel_transform_comp.get_position_global();
            points++;
        }
    }

    if(points > 0)
    {
        pivot /= static_cast<float>(points);
    }

    return pivot;
}

void setup_gizmo_pivot(bool gizmo_at_center,
                       entt::handle center,
                       const std::vector<entt::handle*>& selections,
                       entt::handle active_selection)
{
    auto& center_transform_comp = center.get<transform_component>();
    auto& transform_comp = active_selection.get<transform_component>();

    auto trans_global = transform_comp.get_transform_global();
    center_transform_comp.set_transform_global(trans_global);

    if(gizmo_at_center)
    {
        math::vec3 pivot = calculate_center_pivot(selections);
        center_transform_comp.set_position_global(pivot);
    }
}

auto handle_text_component_bounds_manipulation(entt::handle active_selection,
                                               entt::handle center,
                                               const camera_component& camera_comp,
                                               editing_manager& em,
                                               float* snap,
                                               float bounds_snap_data[3],
                                               float* bounds_snap) -> bool
{
    auto text_comp = active_selection.try_get<text_component>();
    if(!text_comp)
    {
        return false;
    }

    auto area = text_comp->get_area();
    if(!area.is_valid())
    {
        return false;
    }

    auto& center_transform_comp = center.get<transform_component>();
    const auto& camera = camera_comp.get_camera();
    
    // Local-space half-extents = 0.5 in X & Y, zero thickness in Z
    float bounds[6] = {
        -0.5f,
        -0.5f,
        0.0f, // min x, y, z
        0.5f,
        0.5f,
        0.0f // max x, y, z
    };

    math::transform model_tr;
    model_tr.set_position(center_transform_comp.get_position_global());
    model_tr.set_rotation(center_transform_comp.get_rotation_global());
    model_tr.set_scale(math::vec3(area.width, area.height, 1.0f));

    math::mat4 output = model_tr;

    int movetype = ImGuizmo::Manipulate(camera.get_view(),
                         camera.get_projection(),
                         ImGuizmo::BOUNDS,
                         em.mode,
                         math::value_ptr(output),
                         nullptr,
                         snap,
                         bounds,
                         bounds_snap);

    if(movetype != ImGuizmo::MT_NONE)
    {
        math::transform output_tr = output;
        const auto& scale = output_tr.get_scale();
        const auto& trans = output_tr.get_translation();
    
        // Update the text area dimensions
        area.width = scale.x;
        area.height = scale.y;
        text_comp->set_area(area);
        
        // Update the center position - the transform delta will be applied to all selections later
        center_transform_comp.set_position_global(trans);

        em.add_action("Bounds Manipulation",
            [active_selection]()
        {
            prefab_override_context::mark_transform_as_changed(active_selection, true, false, false, false);
            prefab_override_context::mark_text_area_as_changed(active_selection); 
        });

        return true;
    }

    return false;
}

void handle_inverse_kinematics(entt::handle selection, entt::handle center, editing_manager& em)
{
    if(ImGui::IsAnyItemActive())
    {
        return;
    }

    auto& center_transform_comp = center.get<transform_component>();

    if(ImGui::IsKeyDown(shortcuts::ik_ccd))
    {
        ik_set_position_ccd(selection, center_transform_comp.get_position_global(), em.ik_data.num_nodes);
    }
    else if(ImGui::IsKeyDown(shortcuts::ik_fabrik))
    {
        ik_set_position_fabrik(selection, center_transform_comp.get_position_global(), em.ik_data.num_nodes);
    }
}

void apply_transform_delta_to_selections(const std::vector<entt::handle>& top_level_selections,
                                         const std::vector<entt::handle>& original_parents,
                                         const math::mat4& center_delta)
{
    for(size_t i = 0; i < top_level_selections.size(); ++i)
    {
        auto& sel = top_level_selections[i];
        if(!sel)
        {
            continue;
        }

        auto& sel_transform_comp = sel.get<transform_component>();

        // "old_global" is the entity's transform BEFORE we moved the center.
        math::mat4 old_global = sel_transform_comp.get_transform_global();

        // Compute the new global by applying the same delta we applied to the center
        math::mat4 new_global = center_delta * old_global;

        // Convert that new global transform back into local space for the entity's
        // actual/original parent (which we never physically changed).
        entt::handle original_parent = original_parents[i];
        if(original_parent)
        {
            const auto& parent_transform = original_parent.get<transform_component>();
            math::mat4 parent_global = parent_transform.get_transform_global();
            math::mat4 parent_global_inv = glm::inverse(parent_global);

            math::mat4 new_local = parent_global_inv * new_global;
            sel_transform_comp.set_transform_local(math::transform(new_local));
        }
        else
        {
            // If no valid parent, the new local == new global
            sel_transform_comp.set_transform_local(math::transform(new_global));
        }
    }
}

auto handle_standard_gizmo_manipulation(entt::handle active_selection,
                                        entt::handle center,
                                        const camera_component& camera_comp,
                                        editing_manager& em,
                                        float* snap) -> int
{
    auto& center_transform_comp = center.get<transform_component>();
    const auto& camera = camera_comp.get_camera();

    math::mat4 output = center_transform_comp.get_transform_global();
    math::mat4 output_delta;

    ImGuizmo::AllowAxisFlip(false);

    int movetype = ImGuizmo::Manipulate(camera.get_view(),
                                        camera.get_projection(),
                                        em.operation,
                                        em.mode,
                                        math::value_ptr(output),
                                        math::value_ptr(output_delta),
                                        snap,
                                        nullptr,
                                        nullptr);

    if(movetype != ImGuizmo::MT_NONE)
    {
        math::transform delta = output_delta;

        auto perspective = center_transform_comp.get_perspective_local();
        auto skew = center_transform_comp.get_skew_local();

        if(ImGuizmo::IsScaleType(movetype))
        {
            center_transform_comp.scale_by_local(delta.get_scale());
        }
        if(ImGuizmo::IsRotateType(movetype))
        {
            center_transform_comp.rotate_by_global(delta.get_rotation());
        }
        if(ImGuizmo::IsTranslateType(movetype))
        {
            center_transform_comp.move_by_global(delta.get_translation());
        }

        center_transform_comp.set_skew_local(skew);
        center_transform_comp.set_perspective_local(perspective);
    }

    return movetype;
}

void manipulation_gizmos(bool& gizmo_at_center, entt::handle center, entt::handle editor_camera, editing_manager& em)
{
    auto& camera_trans = editor_camera.get<transform_component>();
    auto& camera_comp = editor_camera.get<camera_component>();

    setup_gizmo_context(camera_comp);
    handle_view_manipulator(editor_camera, camera_comp);
    handle_gizmo_shortcuts(em);

    auto active_sel = em.try_get_active_selection_as<entt::handle>();
    if(!active_sel || !active_sel->valid() || !active_sel->all_of<transform_component>())
    {
        return;
    }

    float bounds_snap_data[3] = {0.1f, 0.1f, 0.0f};
    float* snap = nullptr;
    float* bounds_snap = nullptr;

    setup_snap_data(em, snap, bounds_snap, bounds_snap_data);

    auto selections = em.try_get_selections_as<entt::handle>();
    setup_gizmo_pivot(gizmo_at_center, center, selections, *active_sel);
    
    // Store initial center transform before any manipulation
    auto& center_transform_comp = center.get<transform_component>();
    math::mat4 center_initial_global = center_transform_comp.get_transform_global();

    // Convert pointer vector to value vector for get_top_level_entities
    std::vector<entt::handle> selection_values;
    selection_values.reserve(selections.size());
    for(const auto& sel : selections)
    {
        if(sel && *sel)
        {
            selection_values.emplace_back(*sel);
        }
    }

    auto top_level_selections = transform_component::get_top_level_entities(selection_values);

    std::vector<entt::handle> original_parents;
    original_parents.reserve(top_level_selections.size());
    for(const auto& sel : top_level_selections)
    {
        if(sel)
        {
            auto& sel_transform_comp = sel.get<transform_component>();
            original_parents.emplace_back(sel_transform_comp.get_parent());
        }
    }

    bool bounds_changed = false;
    // Handle text component bounds manipulation for non-rotate/scale operations
    if(em.operation != ImGuizmo::ROTATE && em.operation != ImGuizmo::SCALE && top_level_selections.size() == 1)
    {
        bounds_changed = handle_text_component_bounds_manipulation(*active_sel,
                                                 center,
                                                 camera_comp,
                                                 em,
                                                 snap,
                                                 bounds_snap_data,
                                                 bounds_snap);
    }
    
    int movetype = ImGuizmo::MT_NONE;
    // Handle standard gizmo manipulation for non-bounds operations
    if(em.operation != ImGuizmo::BOUNDS)
    {
        movetype = handle_standard_gizmo_manipulation(*active_sel, center, camera_comp, em, snap);
    }
    
    // After all manipulations, compute the delta and apply it to all selections
    math::mat4 center_final_global = center_transform_comp.get_transform_global();
    math::mat4 center_delta = center_final_global * glm::inverse(center_initial_global);

    // Apply inverse kinematics if needed
    for(size_t i = 0; i < top_level_selections.size(); ++i)
    {
        auto& sel = top_level_selections[i];
        if(sel)
        {
            if(movetype != ImGuizmo::MT_NONE)
            {
                bool position = ImGuizmo::IsTranslateType(movetype);
                bool rotation = ImGuizmo::IsRotateType(movetype);
                bool scale = ImGuizmo::IsScaleType(movetype);
                bool skew = false;
                em.add_action("Transform Manipulation",
                    [sel, position, rotation, scale, skew]()
                {
                    prefab_override_context::mark_transform_as_changed(sel, position, rotation, scale, skew);
                });
            }

            handle_inverse_kinematics(sel, center, em);
            if(ImGui::IsKeyDown(shortcuts::ik_ccd) || ImGui::IsKeyDown(shortcuts::ik_fabrik))
            {
                // Skip standard transform if using IK
                continue;
            }
            
            // Apply transform delta to each selection
            auto& sel_transform_comp = sel.get<transform_component>();
            math::mat4 old_global = sel_transform_comp.get_transform_global();
            math::mat4 new_global = center_delta * old_global;
            
            // Convert to local space based on parent
            entt::handle original_parent = original_parents[i];
            if(original_parent)
            {
                const auto& parent_transform = original_parent.get<transform_component>();
                math::mat4 parent_global = parent_transform.get_transform_global();
                math::mat4 parent_global_inv = glm::inverse(parent_global);
                math::mat4 new_local = parent_global_inv * new_global;
                sel_transform_comp.set_transform_local(math::transform(new_local));
            }
            else
            {
                // If no valid parent, the new local == new global
                sel_transform_comp.set_transform_local(math::transform(new_global));
            }
        }
    }
}

// Process drag and drop for assets
void process_drag_drop_target(rtti::context& ctx, const camera_component& camera_comp)
{
    if(!ImGui::BeginDragDropTarget())
    {
        // If we were previewing and drag ended without dropping, restore materials
        reset_preview_state();
        return;
    }

    // Set cursor based on whether payload is being accepted
    if(ImGui::IsDragDropPayloadBeingAccepted())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        
        // Check for material drag and show preview
        std::string material_path;
        if(check_material_drag(material_path))
        {
            handle_material_preview(ctx, camera_comp, material_path);
        }
    }
    else
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
        reset_preview_state();
    }

    // Handle material drop
    for(const auto& type : ex::get_suported_formats<material>())
    {
        auto payload = ImGui::AcceptDragDropPayload(type.c_str());
        if(payload != nullptr)
        {
            std::string absolute_path(reinterpret_cast<const char*>(payload->Data), std::size_t(payload->DataSize));
            std::string key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();
            
            // Clear preview state since we're actually dropping now
            reset_preview_state();
            
            handle_material_drop(ctx, camera_comp, key);
        }
    }

    // Handle mesh drop
    for(const auto& type : ex::get_suported_formats<mesh>())
    {
        auto payload = ImGui::AcceptDragDropPayload(type.c_str());
        if(payload != nullptr)
        {
            // Clear preview state
            reset_preview_state();
            
            std::string absolute_path(reinterpret_cast<const char*>(payload->Data), std::size_t(payload->DataSize));
            std::string key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();

            handle_mesh_drop(ctx, camera_comp, key);
        }
    }

    // Handle prefab drop
    for(const auto& type : ex::get_suported_formats<prefab>())
    {
        auto payload = ImGui::AcceptDragDropPayload(type.c_str());
        if(payload != nullptr)
        {
            // Clear preview state
            reset_preview_state();
            
            std::string absolute_path(reinterpret_cast<const char*>(payload->Data), std::size_t(payload->DataSize));
            std::string key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();

            handle_prefab_drop(ctx, camera_comp, key);
        }
    }

    ImGui::EndDragDropTarget();
}

// Helper function to restore original materials
void restore_original_materials(entt::handle entity, const std::vector<asset_handle<material>>& original_materials)
{
    if(!entity || !entity.all_of<model_component>() || original_materials.empty())
        return;
        
    auto& model_comp = entity.get<model_component>();
    class model model_copy = model_comp.get_model();
    
    // Restore original materials
    for(size_t i = 0; i < original_materials.size() && i < model_copy.get_materials().size(); ++i)
    {
        model_copy.set_material(original_materials[i], i);
    }
    
    // Update the model
    model_comp.set_model(model_copy);
}

// Apply material preview to an entity and save original materials for restoration
void apply_material_preview(rtti::context& ctx, entt::handle entity, const std::string& material_path, 
                          entt::handle& last_preview_entity, std::vector<asset_handle<material>>& original_materials,
                          bool& is_previewing)
{
    // If entity is invalid, restore previous preview if there was one
    if (!entity)
    {
        if (is_previewing && last_preview_entity)
        {
            restore_original_materials(last_preview_entity, original_materials);
            is_previewing = false;
            last_preview_entity = {};
            original_materials.clear();
        }
        return;
    }
    
    // If entity changed, restore previous preview
    if(is_previewing && last_preview_entity && entity != last_preview_entity)
    {
        restore_original_materials(last_preview_entity, original_materials);
        is_previewing = false;
        original_materials.clear();
    }
    
    // If entity has model component and is different from last preview
    if(entity && entity.all_of<model_component>() && (!is_previewing || entity != last_preview_entity))
    {
        // Load material for preview
        auto& am = ctx.get_cached<asset_manager>();
        auto material_asset = am.get_asset<material>(material_path);
        
        // Store original materials for restoration
        auto& model_comp = entity.get<model_component>();
        auto& model = model_comp.get_model();
        
        // Save original materials if not already previewing this entity
        if(!is_previewing || entity != last_preview_entity)
        {
            original_materials.clear();
            for(const auto& mat : model.get_materials())
            {
                original_materials.push_back(mat);
            }
        }
        
        // Apply preview material
        class model model_copy = model;
        for(size_t i = 0; i < model_copy.get_materials().size(); ++i)
        {
            model_copy.set_material(material_asset, i);
        }
        model_comp.set_model(model_copy);
        
        // Update preview state
        is_previewing = true;
        last_preview_entity = entity;
    }
}

} // namespace

// ============================================================================
// Scene Panel Implementation
// ============================================================================

scene_panel::scene_panel(imgui_panels* parent) : entity_panel(parent)
{
}

void scene_panel::init(rtti::context& ctx)
{
    ctx.add<gizmo_registry>();
    gizmos_.init(ctx);

    //create editor camera
    defaults::create_camera_entity(ctx, panel_scene_, "Scene Camera");

    //create center entity
    panel_scene_.create_entity();
}

void scene_panel::deinit(rtti::context& ctx)
{
    gizmos_.deinit(ctx);
    ctx.remove<gizmo_registry>();
}


// ============================================================================
// Drag Selection Helper Functions
// ============================================================================

void scene_panel::handle_drag_selection(rtti::context& ctx, const camera& camera, editing_manager& em)
{
    if(!ImGui::IsAnyItemHovered() && !ImGuizmo::IsOver() && ImGui::IsWindowHovered())
    {
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            drag_start_pos_ = ImGui::GetMousePos();
        }  
        // Check if we should start drag selection
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            // Only start drag selection if we're not clicking on anything and not over a gizmo
            if(!is_drag_selecting_)
            {
                is_drag_selecting_ = true;
            }
        }
    }
    

    // Update drag selection
    if(is_drag_selecting_)
    {
        drag_current_pos_ = ImGui::GetMousePos();

        // End drag selection on mouse release
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            auto& pick_manager = ctx.get_cached<picking_manager>();
            pick_manager.cancel_pick();
            is_drag_selecting_ = false;
        }
    }
}

void scene_panel::draw_drag_selection_rect(const ImVec2& start_pos, const ImVec2& current_pos)
{
    if(start_pos.x == current_pos.x && start_pos.y == current_pos.y)
    {
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Calculate the rectangle bounds
    ImVec2 min_pos(std::min(start_pos.x, current_pos.x), std::min(start_pos.y, current_pos.y));
    ImVec2 max_pos(std::max(start_pos.x, current_pos.x), std::max(start_pos.y, current_pos.y));
    
    // Draw the selection rectangle
    ImU32 rect_color = ImGui::GetColorU32(ImVec4(0.2f, 0.6f, 1.0f, 0.3f)); // Semi-transparent blue
    ImU32 border_color = ImGui::GetColorU32(ImVec4(0.2f, 0.6f, 1.0f, 0.8f)); // Solid blue border
    
    // Fill rectangle
    draw_list->AddRectFilled(min_pos, max_pos, rect_color);
    
    // Border
    draw_list->AddRect(min_pos, max_pos, border_color, 0.0f, 0, 2.0f);
}


void scene_panel::handle_prefab_mode_changes(rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();
    bool is_prefab_mode = em.is_prefab_mode();

    // Detect when we enter prefab mode
    if(is_prefab_mode && !was_prefab_mode_)
    {
        defaults::focus_camera_on_entities(get_camera(), {em.prefab_entity}, 0.4);
    }
    // Detect when we exit prefab mode (e.g., due to external factors)
    else if(!is_prefab_mode && was_prefab_mode_)
    {
        // If we're exiting prefab mode and auto-save is enabled, save changes
        if(auto_save_prefab_ && em.edited_prefab)
        {
            em.save_prefab_changes(ctx);
        }
    }

    was_prefab_mode_ = is_prefab_mode;
}

void scene_panel::on_frame_update(rtti::context& ctx, delta_t dt)
{
    handle_prefab_mode_changes(ctx);

    if(!is_visible_)
    {
        return;
    }

    auto& path = ctx.get_cached<rendering_system>();
    path.on_frame_update(panel_scene_, dt);

    auto& em = ctx.get_cached<editing_manager>();
    if(em.is_prefab_mode())
    {
        path.on_frame_update(em.prefab_scene, dt);
    }
}

void scene_panel::on_frame_before_render(rtti::context& ctx, delta_t dt)
{
    auto& path = ctx.get_cached<rendering_system>();
    path.on_frame_before_render(panel_scene_, dt);

    auto& em = ctx.get_cached<editing_manager>();
    if(em.is_prefab_mode())
    {
        path.on_frame_before_render(em.prefab_scene, dt);
    }
}

void scene_panel::draw_scene(rtti::context& ctx, delta_t dt)
{
    auto& em = ctx.get_cached<editing_manager>();
    auto& path = ctx.get_cached<rendering_system>();
    auto handle = get_camera();
    auto& camera_comp = handle.get<camera_component>();

    // Use the appropriate scene based on mode
    auto target_scene = em.get_active_scene(ctx);

    if(target_scene)
    {
        path.render_scene(handle, camera_comp, *target_scene, dt);
        gizmos_.on_frame_render(ctx, *target_scene, handle);
    }
}

void scene_panel::on_frame_render(rtti::context& ctx, delta_t dt)
{
    if(!is_visible_)
    {
        return;
    }
    draw_scene(ctx, dt);
}

void scene_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    entity_panel::on_frame_ui_render();

    if(ImGui::Begin(name, nullptr, ImGuiWindowFlags_MenuBar))
    {
        is_focused_ = ImGui::IsWindowFocused();
        set_visible(true);
        draw_ui(ctx);
    }
    else
    {
        set_visible(false);
    }
    ImGui::End();
}

auto scene_panel::get_camera() -> entt::handle
{
    entt::handle camera_entity;
    panel_scene_.registry->view<camera_component>().each([&](auto e, auto&& camera_comp) 
    {
        camera_entity = panel_scene_.create_handle(e);
    });
    return camera_entity;
}

auto scene_panel::get_center() -> entt::handle
{
    entt::handle center_entity;

    auto view = panel_scene_.registry->view<root_component>(entt::exclude<camera_component>);
    view.each([&](auto e, auto&& comp) 
    {
        center_entity = panel_scene_.create_handle(e);
    });
    return center_entity;
}

void scene_panel::set_visible(bool visible)
{
    is_visible_ = visible;
}

auto scene_panel::is_focused() const -> bool
{
    return is_focused_;
}

auto scene_panel::get_auto_save_prefab() const -> bool
{
    return auto_save_prefab_;
}


// ============================================================================
// UI Drawing Functions
// ============================================================================

void scene_panel::draw_prefab_mode_header(rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();

    if(!em.is_prefab_mode())
    {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
    if(ImGui::Button(ICON_MDI_KEYBOARD_RETURN " Back to Scene"))
    {
        em.exit_prefab_mode(ctx,
                            auto_save_prefab_ ? editing_manager::save_option::yes
                                              : editing_manager::save_option::prompt);
    }
    ImGui::PopStyleColor();

    if(em.edited_prefab)
    {
        ImGui::SameLine();
        ImGui::Text("Editing Prefab: %s", fs::path(em.edited_prefab.id()).filename().string().c_str());

        ImGui::SameLine();
        if(ImGui::Button("Save"))
        {
            em.save_prefab_changes(ctx);
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto Save", &auto_save_prefab_);
        ImGui::SetItemTooltipEx("%s", "Automatically save changes when exiting prefab mode");
    }

    ImGui::Separator();
}

void scene_panel::draw_transform_tools(editing_manager& em)
{
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::MenuItem(ICON_MDI_CURSOR_MOVE, nullptr, em.operation == ImGuizmo::OPERATION::TRANSLATE))
    {
        em.operation = ImGuizmo::OPERATION::TRANSLATE;
    }
    ImGui::SetItemTooltipEx("%s", "Translate Tool");
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::MenuItem(ICON_MDI_ROTATE_3D_VARIANT, nullptr, em.operation == ImGuizmo::OPERATION::ROTATE))
    {
        em.operation = ImGuizmo::OPERATION::ROTATE;
    }
    ImGui::SetItemTooltipEx("%s", "Rotate Tool");
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::MenuItem(ICON_MDI_RELATIVE_SCALE, nullptr, em.operation == ImGuizmo::OPERATION::SCALE))
    {
        em.operation = ImGuizmo::OPERATION::SCALE;
        em.mode = ImGuizmo::MODE::LOCAL;
    }
    ImGui::SetItemTooltipEx("%s", "Scale Tool");
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::MenuItem(ICON_MDI_MOVE_RESIZE, nullptr, em.operation == ImGuizmo::OPERATION::UNIVERSAL))
    {
        em.operation = ImGuizmo::OPERATION::UNIVERSAL;
        em.mode = ImGuizmo::MODE::LOCAL;
    }
    ImGui::SetItemTooltipEx("%s", "Transform Tool");
}

void scene_panel::draw_gizmo_pivot_mode_menu(bool& gizmo_at_center)
{
    auto icon = gizmo_at_center ? ICON_MDI_SET_CENTER "Center" ICON_MDI_ARROW_DOWN_BOLD
                                : ICON_MDI_ROTATE_3D "Pivot" ICON_MDI_ARROW_DOWN_BOLD;
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::BeginMenu(icon))
    {
        if(ImGui::MenuItem(ICON_MDI_SET_CENTER "Center", nullptr, gizmo_at_center))
        {
            gizmo_at_center = true;
        }
        ImGui::SetItemTooltipEx("%s",
                                "The tool handle is placed at the center\n"
                                "of the selections' pivots.");

        if(ImGui::MenuItem(ICON_MDI_ROTATE_3D "Pivot", nullptr, !gizmo_at_center))
        {
            gizmo_at_center = false;
        }
        ImGui::SetItemTooltipEx("%s",
                                "The tool handle is placed at the\n"
                                "active object's pivot point.");

        ImGui::EndMenu();
    }
    ImGui::SetItemTooltipEx("%s", "Tool's Handle Position");
}

void scene_panel::draw_coordinate_system_menu(editing_manager& em)
{
    auto icon = em.mode == ImGuizmo::MODE::LOCAL ? ICON_MDI_CUBE "Local" ICON_MDI_ARROW_DOWN_BOLD
                                                 : ICON_MDI_WEB "Global" ICON_MDI_ARROW_DOWN_BOLD;
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::BeginMenu(icon))
    {
        if(ImGui::MenuItem(ICON_MDI_CUBE "Local",
                           ImGui::GetKeyName(shortcuts::toggle_local_global),
                           em.mode == ImGuizmo::MODE::LOCAL))
        {
            em.mode = ImGuizmo::MODE::LOCAL;
        }
        ImGui::SetItemTooltipEx("%s", "Local Coordinate System");

        if(ImGui::MenuItem(ICON_MDI_WEB "Global", nullptr, em.mode == ImGuizmo::MODE::WORLD))
        {
            em.mode = ImGuizmo::MODE::WORLD;
        }
        ImGui::SetItemTooltipEx("%s", "Global Coordinate System");

        ImGui::EndMenu();
    }
    ImGui::SetItemTooltipEx("%s", "Tool's Coordinate System");
}

void scene_panel::draw_grid_settings_menu(editing_manager& em)
{
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::MenuItem(ICON_MDI_GRID, nullptr, em.show_grid))
    {
        em.show_grid = !em.show_grid;
    }
    ImGui::SetItemTooltipEx("%s", "Show/Hide Grid");
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::BeginMenu(ICON_MDI_ARROW_DOWN_BOLD, em.show_grid))
    {
        ImGui::PushItemWidth(100.0f);

        ImGui::TextUnformatted("Grid Visual");
        ImGui::LabelText("Plane", "%s", "X Z");
        ImGui::SliderFloat("Opacity", &em.grid_data.opacity, 0.0f, 1.0f);
        ImGui::Checkbox("Depth Aware", &em.grid_data.depth_aware);
        ImGui::SetItemTooltipEx("%s", "Grid is depth aware.");

        ImGui::PopItemWidth();

        ImGui::EndMenu();
    }
    ImGui::SetItemTooltipEx("%s", "Grid Properties");
}

void scene_panel::draw_gizmos_settings_menu(editing_manager& em)
{
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::MenuItem(ICON_MDI_SELECTION_MARKER, nullptr, em.show_icon_gizmos))
    {
        em.show_icon_gizmos = !em.show_icon_gizmos;
    }
    ImGui::SetItemTooltipEx("%s", "Show/Hide Gizmos");
    ImGui::PushID("Billboard Gizmos");
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::BeginMenu(ICON_MDI_ARROW_DOWN_BOLD, em.show_icon_gizmos))
    {
        ImGui::PushItemWidth(100.0f);

        ImGui::TextUnformatted("Gizmos Visual");
        ImGui::SliderFloat("Opacity", &em.billboard_data.opacity, 0.0f, 1.0f);
        ImGui::SliderFloat("Size", &em.billboard_data.size, 0.1f, 1.0f);

        ImGui::Checkbox("Depth Aware", &em.billboard_data.depth_aware);
        ImGui::SetItemTooltipEx("%s", "Gizmos are depth aware.");

        ImGui::PopItemWidth();

        ImGui::EndMenu();
    }
    ImGui::SetItemTooltipEx("%s", "Gizmos Properties");
    ImGui::PopID();
}

void scene_panel::draw_visualization_menu()
{
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::BeginMenu(ICON_MDI_DRAWING_BOX ICON_MDI_ARROW_DOWN_BOLD))
    {
        ImGui::RadioButton("Full", &visualize_passes_, -1);
        ImGui::RadioButton("Base Color", &visualize_passes_, 0);
        ImGui::RadioButton("Diffuse Color", &visualize_passes_, 1);
        ImGui::RadioButton("Specular Color", &visualize_passes_, 2);
        ImGui::RadioButton("Indirect Specular Color", &visualize_passes_, 3);
        ImGui::RadioButton("Ambient Occlusion", &visualize_passes_, 4);
        ImGui::RadioButton("Normals (World Space)", &visualize_passes_, 5);
        ImGui::RadioButton("Roughness", &visualize_passes_, 6);
        ImGui::RadioButton("Metalness", &visualize_passes_, 7);
        ImGui::RadioButton("Emissive Color", &visualize_passes_, 8);
        ImGui::RadioButton("Subsurface Color", &visualize_passes_, 9);
        ImGui::RadioButton("Depth", &visualize_passes_, 10);

        ImGui::EndMenu();
    }
    ImGui::SetItemTooltipEx("%s", "Visualize Render Passes");
}

void scene_panel::draw_snapping_menu(editing_manager& em)
{
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::BeginMenu(ICON_MDI_GRID_LARGE ICON_MDI_ARROW_DOWN_BOLD))
    {
        ImGui::PushItemWidth(200.0f);
        ImGui::DragVecN("Translation Snap",
                        ImGuiDataType_Float,
                        math::value_ptr(em.snap_data.translation_snap),
                        em.snap_data.translation_snap.length(),
                        0.5f,
                        nullptr,
                        nullptr,
                        "%.2f");

        ImGui::DragFloat("Rotation Degree Snap", &em.snap_data.rotation_degree_snap);
        ImGui::DragFloat("Scale Snap", &em.snap_data.scale_snap);
        ImGui::PopItemWidth();
        ImGui::EndMenu();
    }
    ImGui::SetItemTooltipEx("%s", "Snapping Properties");
}

void scene_panel::draw_inverse_kinematics_menu(editing_manager& em)
{
    ImGui::SetNextWindowViewportToCurrent();

    if(ImGui::BeginMenu(ICON_MDI_CRANE ICON_MDI_ARROW_DOWN_BOLD))
    {
        ImGui::PushItemWidth(200.0f);
        ImGui::InputInt("Inverse Kinematic Nodes", &em.ik_data.num_nodes);

        ImGui::PopItemWidth();
        ImGui::EndMenu();
    }
    ImGui::SetItemTooltipEx("%s", "Inverse Kinematic Properties");
}

void scene_panel::draw_camera_settings_menu(rtti::context& ctx)
{
    ImGui::SetNextWindowSizeConstraints({}, {400.0f, ImGui::GetContentRegionAvail().y});
    ImGui::SetNextWindowViewportToCurrent();
    if(ImGui::BeginMenu(ICON_MDI_CAMERA ICON_MDI_ARROW_DOWN_BOLD))
    {
        ImGui::TextUnformatted("Scene Camera");

        rttr::variant cam = get_camera();
        inspect_var(ctx, cam);

        ImGui::EndMenu();
    }
    ImGui::SetItemTooltipEx("%s", "Settings for the Scene view camera.");
}

void scene_panel::handle_viewport_interaction(rtti::context& ctx, const camera& camera, editing_manager& em)
{
    bool is_using = ImGuizmo::IsUsing();
    bool is_over = ImGuizmo::IsOver();
    bool is_entity = em.is_selected_type<entt::handle>();

    // Handle drag selection
    handle_drag_selection(ctx, camera, em);

    if(is_drag_selection_active())
    {
        auto& pick_manager = ctx.get_cached<picking_manager>();
        auto bounds = get_drag_selection_bounds();

        math::vec2 area = {bounds.second.x - bounds.first.x, bounds.second.y - bounds.first.y};
        // Calculate the center of the drag selection area
        math::vec2 center = {
            bounds.first.x + area.x * 0.5f,
            bounds.first.y + area.y * 0.5f
        };

        pick_manager.request_pick(camera, em.get_select_mode(), center, area);
    }

    // Only handle single-click selection if we're not drag selecting
    if(ImGui::IsItemClicked(ImGuiMouseButton_Left) && !is_using && !is_drag_selecting_)
    {
        bool is_over_active_gizmo = is_over && is_entity;
        if(!is_over_active_gizmo)
        {
            ImGui::SetWindowFocus();
            auto& pick_manager = ctx.get_cached<picking_manager>();
            auto pos = ImGui::GetMousePos();

            pick_manager.request_pick(camera, em.get_select_mode(), {pos.x, pos.y});
        }
    }

    if(ImGui::IsItemClicked(ImGuiMouseButton_Middle) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        ImGui::SetWindowFocus();
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    if(ImGui::IsItemReleased(ImGuiMouseButton_Middle) || ImGui::IsItemReleased(ImGuiMouseButton_Right))
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }
}

void scene_panel::handle_keyboard_shortcuts(editing_manager& em)
{
    bool is_delete_pressed = ImGui::IsItemKeyPressed(shortcuts::delete_item);
    bool is_focus_pressed = ImGui::IsItemKeyPressed(shortcuts::focus_selected);
    bool is_duplicate_pressed = ImGui::IsItemCombinationKeyPressed(shortcuts::duplicate_item);

    auto selections = em.try_get_selections_as_copy<entt::handle>();

    if(is_delete_pressed)
    {
        delete_entities(selections);
    }

    if(is_focus_pressed)
    {
        focus_entities(get_camera(), selections);
    }

    if(is_duplicate_pressed)
    {
        duplicate_entities(selections);
    }
}

void scene_panel::setup_camera_viewport(camera_component& camera_comp, const ImVec2& size, const ImVec2& pos)
{
    if(size.x > 0 && size.y > 0)
    {
        camera_comp.get_camera().set_viewport_pos(
            {static_cast<std::uint32_t>(pos.x), static_cast<std::uint32_t>(pos.y)});
        camera_comp.set_viewport_size({static_cast<std::uint32_t>(size.x), static_cast<std::uint32_t>(size.y)});
    }
}

void scene_panel::draw_scene_viewport(rtti::context& ctx, const ImVec2& size)
{
    auto camera_entity = get_camera();
    if(!camera_entity)
    {
        return;
    }

    auto& em = ctx.get_cached<editing_manager>();
    auto& camera_comp = camera_entity.get<camera_component>();
    const auto& camera = camera_comp.get_camera();
    const auto& rview = camera_comp.get_render_view();
    const auto& obuffer = rview.fbo_safe_get("OBUFFER");

    draw_menubar(ctx);

    if(obuffer)
    {
        const auto& tex = obuffer->get_texture(0);
        ImGui::Image(ImGui::ToId(tex), size);
    }
    else
    {
        ImGui::Text("No render view");
    }

    if(em.is_prefab_mode())
    {
        ImVec2 padding(2.0f, 2.0f);
        auto color = ImGui::GetColorU32(ImGuiCol_ButtonActive);
        auto min = ImGui::GetItemRectMin() - padding;
        auto max = ImGui::GetItemRectMax() + padding;
        ImGui::RenderFocusFrame(min, max, color, 4.0f);
    }

    handle_viewport_interaction(ctx, camera, em);
    handle_keyboard_shortcuts(em);

    manipulation_gizmos(gizmo_at_center_, get_center(), camera_entity, em);
    handle_camera_movement(camera_entity, move_dir_, acceleration_, is_dragging_);
    draw_selected_camera(ctx, camera_entity, size);

    // Draw drag selection rectangle if active
    if(is_drag_selecting_)
    {
        draw_drag_selection_rect(drag_start_pos_, drag_current_pos_);
    }

    camera_comp.get_pipeline_data().get_pipeline()->set_debug_pass(visualize_passes_);
}

void scene_panel::draw_ui(rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();
    auto camera_entity = get_camera();

    bool has_edit_camera = camera_entity && camera_entity.all_of<transform_component, camera_component>();

    if(!has_edit_camera)
    {
        return;
    }

    auto size = ImGui::GetContentRegionAvail();
    if(size.x > 0 && size.y > 0)
    {
        auto pos = ImGui::GetCursorScreenPos();
        auto& camera_comp = camera_entity.get<camera_component>();

        setup_camera_viewport(camera_comp, size, pos);
        draw_scene_viewport(ctx, size);
        process_drag_drop_target(ctx, camera_comp);
    }
}

void scene_panel::draw_framerate_display()
{
    ImGui::PushFont(ImGui::Font::Mono);
    auto& io = ImGui::GetIO();

    auto fps_size = ImGui::CalcTextSize(fmt::format("{:.1f}", io.Framerate).c_str()).x;
    ImGui::PopFont();

    ImGui::SameLine();

    ImGui::AlignedItem(1.0f,
                       ImGui::GetContentRegionAvail().x,
                       fps_size,
                       [&]()
                       {
                           ImGui::PushFont(ImGui::Font::Mono);
                           ImGui::Text("%.1f", io.Framerate);
                           ImGui::PopFont();
                       });
}

void scene_panel::draw_menubar(rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();

    if(ImGui::BeginMenuBar())
    {
        draw_prefab_mode_header(ctx);
        draw_transform_tools(em);
        draw_gizmo_pivot_mode_menu(gizmo_at_center_);
        draw_coordinate_system_menu(em);
        draw_grid_settings_menu(em);
        draw_gizmos_settings_menu(em);
        draw_visualization_menu();
        draw_snapping_menu(em);
        draw_inverse_kinematics_menu(em);
        draw_camera_settings_menu(ctx);
        draw_framerate_display();

        ImGui::EndMenuBar();
    }
}

void scene_panel::draw_selected_camera(rtti::context& ctx, entt::handle editor_camera, const ImVec2& size)
{
    auto& em = ctx.get_cached<editing_manager>();

    if(auto sel = em.try_get_active_selection_as<entt::handle>())
    {
        if(sel && sel->valid() && sel->all_of<camera_component>())
        {
            const auto& selected_camera = sel->get<camera_component>();

            auto& game_panel = parent_->get_game_panel();
            game_panel.set_visible_force(true);

            const auto& camera = selected_camera.get_camera();
            const auto& render_view = selected_camera.get_render_view();
            const auto& viewport_size = camera.get_viewport_size();
            const auto& obuffer = render_view.fbo_safe_get("OBUFFER");

            if(!obuffer)
            {
                return;
            }
            float factor = std::min(size.x / float(viewport_size.width), size.y / float(viewport_size.height)) / 4.0f;
            ImVec2 bounds(viewport_size.width * factor, viewport_size.height * factor);
            // Calculate the position to place the image
            ImVec2 image_pos =
                ImVec2(ImGui::GetWindowSize().x - 20 - bounds.x, ImGui::GetWindowSize().y - 20 - bounds.y);

            // Move the cursor to the calculated position
            ImGui::SetCursorPos(image_pos);

            const auto& tex = obuffer->get_texture(0);
            ImGui::Image(ImGui::ToId(tex), bounds);

            if(ImGui::IsKeyChordPressed(shortcuts::snap_scene_camera_to_selected_camera))
            {
                auto& transform = editor_camera.get<transform_component>();
                auto& transform_selected = sel->get<transform_component>();
                transform_selected.set_transform_global(transform.get_transform_global());
            }
        }
    }
}

} // namespace unravel
