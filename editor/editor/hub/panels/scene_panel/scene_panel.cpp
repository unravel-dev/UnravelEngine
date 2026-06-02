#include "scene_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "../viewport_resolution.h"
#include "imgui_widgets/utils.h"
#include <editor/editing/actions/entity_actions.h>
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

#include <engine/rendering/ecs/systems/ik_solvers.h>
#include <engine/rendering/ecs/systems/model_system.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/renderer.h>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/gizmo.h>
#include <imgui_widgets/imcoolbar.h>
#include <seq/seq.h>

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
void apply_material_preview(rtti::context& ctx,
                            entt::handle entity,
                            const std::string& material_path,
                            entt::handle& last_preview_entity,
                            std::vector<asset_handle<material>>& original_materials,
                            bool& is_previewing);
void manipulation_gizmos(bool& gizmo_at_center,
                         bool& was_using_gizmo,
                         entt::handle center,
                         entt::handle editor_camera,
                         editing_manager& em);
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
                out_material_path =
                    std::string(reinterpret_cast<const char*>(payload->Data), std::size_t(payload->DataSize));
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
    pick_manager.query_pick(math::vec2{cursor_pos.x, cursor_pos.y},
                            camera_comp.get_camera(),
                            [&ctx, material_path](entt::handle entity, const math::vec2& screen_pos)
                            {
                                apply_material_preview(ctx,
                                                       entity,
                                                       material_path,
                                                       g_preview_state.last_preview_entity,
                                                       g_preview_state.original_materials,
                                                       g_preview_state.is_previewing);
                            });
}

// Handle material drop on entity
void handle_material_drop(rtti::context& ctx, const camera_component& camera_comp, const std::string& material_path)
{
    auto cursor_pos = ImGui::GetMousePos();
    auto& pick_manager = ctx.get_cached<picking_manager>();
    auto& am = ctx.get_cached<asset_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    // Load the material asset
    auto material_asset = am.get_asset<material>(material_path);
    bool force = true;

    // Use the picking system to query what's under the cursor
    pick_manager.query_pick(
        math::vec2{cursor_pos.x, cursor_pos.y},
        camera_comp.get_camera(),
        [material_asset, &em](entt::handle entity, const math::vec2& screen_pos)
        {
            // Check if entity has a model component
            if(entity && entity.all_of<model_component>())
            {
                // Get current materials to store as old state
                auto& model_comp = entity.get<model_component>();
                const auto& current_model = model_comp.get_model();
                auto old_materials = current_model.get_materials();

                em.push_undo_stack_enabled(true);

                // Create and execute the action
                em.queue_action<entity_set_materials_action_t>({}, entity, old_materials, material_asset);

                em.pop_undo_stack_enabled();
            }
            else if(entity)
            {
                APPLOG_WARNING("Cannot apply material to entity without model_component");
            }
        },
        force);
}

// Factory wrapper for viewport drops: snapshots the current cursor position and camera,
// runs the user-supplied producer inside an undoable create_entities_action_t, and selects
// the resulting entity. No parenting action is layered - viewport drops land at scene root.
void queue_create_at_cursor(rtti::context& ctx,
                            const camera_component& camera_comp,
                            const std::string& action_name,
                            std::function<entt::handle(rtti::context&, scene&, const camera&, const math::vec2&)> producer)
{
    auto cursor_pos = ImGui::GetMousePos();
    auto& em = ctx.get_cached<editing_manager>();

    em.push_undo_stack_enabled(true);
    em.queue_action<create_entities_action_t>(
        action_name,
        [&ctx, camera = camera_comp.get_camera(), cursor_pos, producer = std::move(producer)]() -> entt::handle
        {
            auto& em = ctx.get_cached<editing_manager>();
            auto* target_scene = em.get_active_scene(ctx);
            if(!target_scene)
            {
                return {};
            }

            auto object = producer(ctx, *target_scene, camera, math::vec2{cursor_pos.x, cursor_pos.y});
            if(!object)
            {
                return {};
            }
            em.select(object);
            return object;
        });
    em.pop_undo_stack_enabled();
}

// Handle mesh drop at cursor position
void handle_mesh_drop(rtti::context& ctx, const camera_component& camera_comp, const std::string& mesh_path)
{
    queue_create_at_cursor(ctx, camera_comp, "Drop Mesh",
        [mesh_path](rtti::context& ctx, scene& scn, const camera& cam, const math::vec2& cursor) -> entt::handle
        {
            return defaults::create_mesh_entity_at(ctx, scn, mesh_path, cam, cursor);
        });
}

// Handle prefab drop at cursor position
void handle_prefab_drop(rtti::context& ctx, const camera_component& camera_comp, const std::string& prefab_path)
{
    queue_create_at_cursor(ctx, camera_comp, "Drop Prefab",
        [prefab_path](rtti::context& ctx, scene& scn, const camera& cam, const math::vec2& cursor) -> entt::handle
        {
            return defaults::create_prefab_at(ctx, scn, prefab_path, cam, cursor);
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

auto collect_movement_input(float& max_hold, bool& is_dragging) -> math::vec3
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

    if(is_dragging)
    {
        float move_speed = 4.0f;
        if(is_key_down(shortcuts::camera_forward))
        {
            movement_input.z += move_speed;
        }
        if(is_key_down(shortcuts::camera_backward))
        {
            movement_input.z -= move_speed;
        }
        if(is_key_down(shortcuts::camera_right))
        {
            movement_input.x += move_speed;
        }
        if(is_key_down(shortcuts::camera_left))
        {
            movement_input.x -= move_speed;
        }
    }

    auto delta_wheel = ImGui::GetIO().MouseWheel;
    if(delta_wheel != 0)
    {
        movement_input.z += 15.0f * delta_wheel;
    }

    return movement_input;
}

auto handle_mouse_rotation(entt::handle camera, float rotation_speed, bool is_dragging) -> bool
{
    if(!is_dragging)
    {
        return false;
    }

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
        acceleration *= 0.85f;
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
    float fixed_dt = ImMin(0.0333f, ImGui::GetIO().DeltaTime); // Fixed delta time

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
    }

    // Collect movement input (works for both dragging and non-dragging)
    float max_hold = 0.0f;
    math::vec3 movement_input = collect_movement_input(max_hold, is_dragging);
    bool any_input = math::any(math::epsilonNotEqual(movement_input, math::vec3(0.0f), math::epsilon<float>()));

    // Handle mouse rotation (only when dragging)
    bool any_rotation = handle_mouse_rotation(camera, rotation_speed, is_dragging);

    // Process camera input with acceleration
    update_movement_acceleration(move_dir, acceleration, movement_input, any_input);

    if(any_input || any_rotation)
    {
        seq::scope::stop_all("camera_focus");
    }

    if(acceleration > 0.0001f)
    {
        // Continue movement with deceleration when not actively inputting
        apply_movement(camera, move_dir, movement_speed, acceleration, 0.0f, hold_speed, fixed_dt);
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
    if(!ImGui::IsWindowFocused())
    {
        return;
    }
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

struct bounds_manipulation_result_t
{
    fsize_t initial_area{};
    math::vec3 initial_position{};

    fsize_t new_area{};
    math::vec3 new_position{};
};

auto handle_component_bounds_manipulation(entt::handle active_selection,
                                          fsize_t area,
                                          const camera_component& camera_comp,
                                          editing_manager& em,
                                          float* snap,
                                          float bounds_snap_data[3],
                                          float* bounds_snap) -> hpp::optional<bounds_manipulation_result_t>
{
    auto& transform_comp = active_selection.get<transform_component>();
    const auto& camera = camera_comp.get_camera();

    // Store initial state for undo/redo
    fsize_t initial_area = area;
    math::vec3 initial_position = transform_comp.get_position_global();

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
    model_tr.set_position(transform_comp.get_position_global());
    model_tr.set_rotation(transform_comp.get_rotation_global());
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

        // Create new area and position
        fsize_t new_area{scale.x, scale.y};
        math::vec3 new_position = trans;
        bounds_manipulation_result_t result;
        result.initial_area = initial_area;
        result.initial_position = initial_position;
        result.new_area = new_area;
        result.new_position = new_position;
        return result;
    }

    return hpp::nullopt;
}

auto handle_text_component_bounds_manipulation(entt::handle active_selection,
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

    auto result = handle_component_bounds_manipulation(active_selection,
                                                       area,
                                                       camera_comp,
                                                       em,
                                                       snap,
                                                       bounds_snap_data,
                                                       bounds_snap);
    if(!result)
    {
        return false;
    }

    const auto& initial_area = result->initial_area;
    const auto& initial_position = result->initial_position;
    const auto& new_area = result->new_area;
    const auto& new_position = result->new_position;

    // Create composite action with both text bounds and transform changes
    auto composite_action = std::make_shared<composite_action_t>();

    // Add text bounds action
    composite_action->add_sub_action(
        std::make_shared<entity_set_text_bounds_action_t>(active_selection, initial_area, new_area));

    // Add global transform action for the center entity
    composite_action->add_sub_action(
        std::make_shared<transform_move_global_action_t>(active_selection, initial_position, new_position));

    // Execute the composite action
    em.push_undo_stack_enabled(true);
    em.do_action("Text Bounds Manipulation", composite_action);
    em.pop_undo_stack_enabled();

    return true;
}

auto handle_ui_document_component_bounds_manipulation(entt::handle active_selection,
                                                      const camera_component& camera_comp,
                                                      editing_manager& em,
                                                      float* snap,
                                                      float bounds_snap_data[3],
                                                      float* bounds_snap) -> bool
{
    auto ui_document_comp = active_selection.try_get<ui_document_component>();
    if(!ui_document_comp)
    {
        return false;
    }

    if(!ui_document_comp->size.is_valid())
    {
        return false;
    }
    auto area = ui_document_comp->get_world_space_scale();

    auto result = handle_component_bounds_manipulation(active_selection,
                                                       fsize_t(area.x, area.y),
                                                       camera_comp,
                                                       em,
                                                       snap,
                                                       bounds_snap_data,
                                                       bounds_snap);
    if(!result)
    {
        return false;
    }

    const auto& initial_area = fsize_t(result->initial_area.width * ui_document_comp->pixels_per_world_unit,
                                       result->initial_area.height * ui_document_comp->pixels_per_world_unit);
    const auto& initial_position = result->initial_position;
    const auto& new_area = fsize_t(result->new_area.width * ui_document_comp->pixels_per_world_unit,
                                   result->new_area.height * ui_document_comp->pixels_per_world_unit);
    const auto& new_position = result->new_position;

    // Create composite action with both text bounds and transform changes
    auto composite_action = std::make_shared<composite_action_t>();

    usize32_t initial_area_size = usize32_t(initial_area.width, initial_area.height);
    usize32_t new_area_size = usize32_t(new_area.width, new_area.height);
    // Add text bounds action
    composite_action->add_sub_action(
        std::make_shared<entity_set_ui_document_component_bounds_action_t>(active_selection,
                                                                           initial_area_size,
                                                                           new_area_size));

    // Add global transform action for the center entity
    composite_action->add_sub_action(
        std::make_shared<transform_move_global_action_t>(active_selection, initial_position, new_position));

    // Execute the composite action
    em.push_undo_stack_enabled(true);
    em.do_action("UI Document Size Manipulation", composite_action);
    em.pop_undo_stack_enabled();

    return true;
}

auto handle_inverse_kinematics(entt::handle selection, entt::handle center, editing_manager& em) -> bool
{
    // Allow IK when gizmo is being used, but block for other ImGui items (like text inputs, sliders, etc.)
    bool is_gizmo_active = ImGuizmo::IsUsing();
    bool is_other_item_active = ImGui::IsAnyItemActive() && !is_gizmo_active;

    if(is_other_item_active)
    {
        return false;
    }

    auto& center_transform_comp = center.get<transform_component>();

    if(ImGui::IsKeyDown(shortcuts::ik_ccd))
    {
        return ik_set_position_ccd(selection,
                                   center_transform_comp.get_position_global(),
                                   math::vec3(0.f),
                                   em.ik_data.num_nodes,
                                   100);
    }
    if(ImGui::IsKeyDown(shortcuts::ik_fabrik))
    {
        return ik_set_position_fabrik(selection,
                                      center_transform_comp.get_position_global(),
                                      math::vec3(0.f),
                                      em.ik_data.num_nodes,
                                      100);
    }
    if(ImGui::IsKeyDown(shortcuts::ik_two_bone))
    {
        return ik_set_position_two_bone(selection,
                                        center_transform_comp.get_position_global(),
                                        center_transform_comp.get_z_axis_global(),
                                        1.0f,
                                        1.0f);
    }
    return false;
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

void manipulation_gizmos(bool& gizmo_at_center,
                         bool& was_using_gizmo,
                         entt::handle center,
                         entt::handle editor_camera,
                         editing_manager& em)
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
    std::vector<math::transform> original_transforms;
    original_parents.reserve(top_level_selections.size());
    original_transforms.reserve(top_level_selections.size());

    // Store initial state before any manipulation
    for(const auto& sel : top_level_selections)
    {
        if(sel)
        {
            auto& sel_transform_comp = sel.get<transform_component>();
            original_parents.emplace_back(sel_transform_comp.get_parent());
            original_transforms.emplace_back(sel_transform_comp.get_transform_local());
        }
    }

    bool bounds_changed = false;
    // Handle text component bounds manipulation for non-rotate/scale operations
    if(em.operation != ImGuizmo::ROTATE && em.operation != ImGuizmo::SCALE && top_level_selections.size() == 1)
    {
        bounds_changed = handle_text_component_bounds_manipulation(*active_sel,
                                                                   camera_comp,
                                                                   em,
                                                                   snap,
                                                                   bounds_snap_data,
                                                                   bounds_snap);

        bounds_changed = handle_ui_document_component_bounds_manipulation(*active_sel,
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

    auto batch_action = std::make_shared<composite_action_t>();
    // Apply transforms and create undoable actions
    for(size_t i = 0; i < top_level_selections.size(); ++i)
    {
        auto& sel = top_level_selections[i];
        if(sel)
        {
            bool ik_keys_down = ImGui::IsKeyDown(shortcuts::ik_ccd) || ImGui::IsKeyDown(shortcuts::ik_fabrik) ||
                                ImGui::IsKeyDown(shortcuts::ik_two_bone);

            if(ik_keys_down)
            {
                // When IK is active, only the center entity (gizmo target) is moved by the gizmo.
                // IK algorithm adjusts parent bones to make the end effector reach the target.
                // Do NOT apply any direct transform to the selection - let IK handle it.
                handle_inverse_kinematics(sel, center, em);
                continue;
            }

            // Apply transform delta to each selection (normal case when IK is not active)
            auto& sel_transform_comp = sel.get<transform_component>();
            math::mat4 old_global = sel_transform_comp.get_transform_global();
            math::mat4 new_global = center_delta * old_global;

            // Convert to local space based on parent
            entt::handle original_parent = original_parents[i];
            math::transform new_local_transform;

            if(original_parent)
            {
                const auto& parent_transform = original_parent.get<transform_component>();
                math::mat4 parent_global = parent_transform.get_transform_global();
                math::mat4 parent_global_inv = glm::inverse(parent_global);
                math::mat4 new_local = parent_global_inv * new_global;
                new_local_transform = math::transform(new_local);
            }
            else
            {
                // If no valid parent, the new local == new global
                new_local_transform = math::transform(new_global);
            }

            // Apply the new transform
            sel_transform_comp.set_transform_local(new_local_transform);
            // Create undoable action if there was a manipulation
            if(movetype != ImGuizmo::MT_NONE)
            {
                // if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    bool position = ImGuizmo::IsTranslateType(movetype);
                    bool rotation = ImGuizmo::IsRotateType(movetype);
                    bool scale = ImGuizmo::IsScaleType(movetype);
                    bool skew = false;

                    if(top_level_selections.size() > 1)
                    {
                        position = true;
                    }

                    // batch_action->add_sub_action(std::make_shared<transform_manipulation_action_t>(
                    //     sel,
                    //     original_transforms[i],
                    //     new_local_transform,
                    //     position, rotation, scale, skew));

                    auto composite_action = std::make_shared<composite_action_t>();

                    if(position)
                    {
                        composite_action->add_sub_action(
                            std::make_shared<transform_move_action_t>(sel,
                                                                      original_transforms[i].get_position(),
                                                                      new_local_transform.get_position()));
                    }
                    if(rotation)
                    {
                        composite_action->add_sub_action(
                            std::make_shared<transform_rotate_action_t>(sel,
                                                                        original_transforms[i].get_rotation(),
                                                                        new_local_transform.get_rotation()));
                    }
                    if(scale)
                    {
                        composite_action->add_sub_action(
                            std::make_shared<transform_scale_action_t>(sel,
                                                                       original_transforms[i].get_scale(),
                                                                       new_local_transform.get_scale()));
                    }
                    if(skew)
                    {
                        composite_action->add_sub_action(
                            std::make_shared<transform_skew_action_t>(sel,
                                                                      original_transforms[i].get_skew(),
                                                                      new_local_transform.get_skew()));
                    }

                    batch_action->add_sub_action(composite_action);
                }
            }
        }
    }

    if(batch_action->sub_actions.size() > 0)
    {
        em.push_undo_stack_enabled(true);
        em.do_action("Transform Manipulation", batch_action);
        em.pop_undo_stack_enabled();
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
void apply_material_preview(rtti::context& ctx,
                            entt::handle entity,
                            const std::string& material_path,
                            entt::handle& last_preview_entity,
                            std::vector<asset_handle<material>>& original_materials,
                            bool& is_previewing)
{
    // If entity is invalid, restore previous preview if there was one
    if(!entity)
    {
        if(is_previewing && last_preview_entity)
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

scene_panel::scene_panel(imgui_panels* parent, const char* name)
    : entity_panel(parent, name)
    , fullscreen_name_(get_name() + " (Fullscreen)")
{
}

auto scene_panel::get_window_name() const -> const char*
{
    return is_fullscreen() ? fullscreen_name_.c_str() : panel_base::get_window_name();
}

void scene_panel::init(rtti::context& ctx)
{
    ctx.add<gizmo_registry>();
    gizmos_.init(ctx);

    // create editor camera
    defaults::create_camera_entity(ctx, panel_scene_, "Scene Camera");

    // create center entity
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
    ImU32 rect_color = ImGui::GetColorU32(ImVec4(0.2f, 0.6f, 1.0f, 0.3f));   // Semi-transparent blue
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
        std::array<entt::handle, 1> entities = {em.prefab_entity};
        defaults::focus_camera_on_entities(get_camera(), entities, 0.4);
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

    if(!is_visible())
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

    if(!handle)
    {
        return;
    }

    auto& camera_comp = handle.get<camera_component>();

    // Use the appropriate scene based on mode
    auto target_scene = em.get_active_scene(ctx);

    if(target_scene)
    {
        path.render_scene(handle, camera_comp, *target_scene, dt, false);
        gizmos_.on_frame_render(ctx, *target_scene, handle, dd_2d_);
    }
}

void scene_panel::on_frame_render(rtti::context& ctx, delta_t dt)
{
    if(m_skip_frames_ > 0)
    {
        m_skip_frames_--;
        return;
    }

    if(!is_visible())
    {
        auto handle = get_camera();
        if(handle)
        {    
            auto& camera_comp = handle.get<camera_component>();
            camera_comp.get_render_view() = {};
        }
        return;
    }
    draw_scene(ctx, dt);
}

void scene_panel::on_project_opened()
{
    m_skip_frames_ = 100;
}

auto scene_panel::get_window_flags() const -> ImGuiWindowFlags
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar;
    if(is_fullscreen())
    {
        flags |= ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    }
    return flags;
}

auto scene_panel::get_camera() -> entt::handle
{
    entt::handle camera_entity;
    panel_scene_.registry->view<camera_component>().each(
        [&](auto e, auto&& camera_comp)
        {
            camera_entity = panel_scene_.create_handle(e);
        });
    return camera_entity;
}

auto scene_panel::get_center() -> entt::handle
{
    entt::handle center_entity;

    auto view = panel_scene_.registry->view<root_component>(entt::exclude<camera_component>);
    view.each(
        [&](auto e, auto&& comp)
        {
            center_entity = panel_scene_.create_handle(e);
        });
    return center_entity;
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
        ImGui::KnobSliderScalarT("Opacity", &em.grid_data.opacity, 0.0f, 1.0f);
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
        ImGui::KnobSliderScalarT("Opacity", &em.billboard_data.opacity, 0.0f, 1.0f);
        ImGui::KnobSliderScalarT("Size", &em.billboard_data.size, 0.1f, 1.0f);

        ImGui::Checkbox("Depth Aware", &em.billboard_data.depth_aware);
        ImGui::SetItemTooltipEx("%s", "Gizmos are depth aware.");

        ImGui::Separator();
        ImGui::TextUnformatted("Billboard Filters");
        ImGui::Checkbox("Camera", &em.billboard_data.show_camera);
        ImGui::Checkbox("Light", &em.billboard_data.show_light);
        ImGui::Checkbox("Reflection Probe", &em.billboard_data.show_reflection_probe);
        ImGui::Checkbox("Audio Source", &em.billboard_data.show_audio_source);
        ImGui::Checkbox("Particle Emitter", &em.billboard_data.show_particle_emitter);

        ImGui::Separator();
        ImGui::TextUnformatted("Selection Gizmos");
        ImGui::Checkbox("Selection Outline", &em.gizmos.show_selection_outline);
        ImGui::Checkbox("Camera Gizmos", &em.gizmos.show_camera);
        ImGui::Checkbox("Model Gizmos", &em.gizmos.show_model);
        ImGui::Checkbox("Light Gizmos", &em.gizmos.show_light);
        ImGui::Checkbox("Reflection Probe Gizmos", &em.gizmos.show_reflection_probe);
        ImGui::Checkbox("Volume Gizmos", &em.gizmos.show_volume);
        ImGui::Checkbox("Text Gizmos", &em.gizmos.show_text);
        ImGui::Checkbox("Particle Emitter Gizmos", &em.gizmos.show_particle_emitter);
        ImGui::Checkbox("Component Gizmos", &em.gizmos.show_component_gizmos);

        ImGui::Separator();
        ImGui::TextUnformatted("Model Details");
        ImGui::Checkbox("World Bounds", &em.gizmos.show_model_bounds);
        ImGui::Checkbox("Local Bounds", &em.gizmos.show_model_local_bounds);
        ImGui::Checkbox("Submesh Local Bounds", &em.gizmos.show_model_submesh_local_bounds);
        ImGui::Checkbox("LOD", &em.gizmos.show_model_lod);

        ImGui::Separator();
        ImGui::TextUnformatted("Particle Emitter Details");
        ImGui::Checkbox("Bounds", &em.gizmos.show_particle_emitter_bounds);
        ImGui::Checkbox("Shape", &em.gizmos.show_particle_emitter_shape);
        ImGui::Checkbox("Direction", &em.gizmos.show_particle_emitter_direction);

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
        ImGui::RadioButton("Radiance", &visualize_passes_, 3);
        ImGui::RadioButton("Irradiance", &visualize_passes_, 4);
        ImGui::RadioButton("Ambient Occlusion", &visualize_passes_, 5);
        ImGui::RadioButton("Normals (World Space)", &visualize_passes_, 6);
        ImGui::RadioButton("Roughness", &visualize_passes_, 7);
        ImGui::RadioButton("Metalness", &visualize_passes_, 8);
        ImGui::RadioButton("Emissive Color", &visualize_passes_, 9);
        ImGui::RadioButton("Subsurface Color", &visualize_passes_, 10);
        ImGui::RadioButton("Depth", &visualize_passes_, 11);
        ImGui::RadioButton("SSIL", &visualize_passes_, 12);

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

        ImGui::Separator();
        ImGui::TextUnformatted("Inverse Kinematic Shortcuts");
        ImGui::Text("CCD: %s", shortcuts::get_shortcut_name(shortcuts::ik_ccd).c_str());
        ImGui::Text("Fabrik: %s", shortcuts::get_shortcut_name(shortcuts::ik_fabrik).c_str());
        ImGui::Text("Two Bone: %s", shortcuts::get_shortcut_name(shortcuts::ik_two_bone).c_str());
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
        if(ImGui::Button("Reset Camera"))
        {
            auto camera = get_camera();
            if(camera)
            {
                camera.destroy();
            }
            defaults::create_camera_entity(ctx, panel_scene_, "Scene Camera");
        }

        ImGui::SetItemTooltipEx("%s", "Reset the Scene camera.");

        entt::meta_any cam = get_camera();
        inspect_var(ctx, cam, make_proxy(cam));

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
        math::vec2 center = {bounds.first.x + area.x * 0.5f, bounds.first.y + area.y * 0.5f};

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
        camera_comp.get_camera().set_viewport_pos({static_cast<int32_t>(pos.x), static_cast<int32_t>(pos.y)});
        camera_comp.set_viewport_size({static_cast<std::uint32_t>(size.x), static_cast<std::uint32_t>(size.y)});
    }
}

void scene_panel::draw_scene_viewport(rtti::context& ctx, const ImVec2& size, const ImVec2& pos)
{
    auto& em = ctx.get_cached<editing_manager>();

    auto camera_entity = get_camera();
    if(!camera_entity)
    {
        return;
    }
    auto& camera_comp = camera_entity.get<camera_component>();
    const auto& camera = camera_comp.get_camera();
    const auto& rview = camera_comp.get_render_view();
    const auto& obuffer = rview.fbo_safe_get("OBUFFER");

    ImGui::SetCursorScreenPos(pos);
    if(obuffer && obuffer->get_attachment_count() > 0)
    {
        const auto& tex = obuffer->get_texture(0);
        ImGui::Image(ImGui::ToId(tex), size);
    }
    else
    {
        ImGui::Dummy(size);
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

    manipulation_gizmos(gizmo_at_center_, was_using_gizmo_, get_center(), camera_entity, em);
    handle_camera_movement(camera_entity, move_dir_, acceleration_, is_dragging_);
    draw_selected_camera(ctx, camera_entity, size);

    // {

    //     const float& ref_font_scale = ImGui::GetCurrentContext()->FontSizeBase;

    //     ImGui::ImCoolBarConfig config;
    //     config.normal_size = 50.0f;
    //     config.hovered_size = 80.0f;
    //     config.anchor = ImVec2(0.5f, 1.0f);
    //     config.anchor_area = ImRect(pos,  pos + size);

    //     if (ImGui::BeginCoolBar("CoolBarMainWin", ImCoolBarFlags_Horizontal, config))
    //     {
    //         if (ImGui::CoolBarItemGuard item{"imgui_demo"})
    //         {
    //             ImVec2 size(item.ctx.width, 0);
    //             ImGui::Button("Play", size);
    //         }

    //         if (ImGui::CoolBarItemGuard item{"imgui_demo1"})
    //         {
    //             ImVec2 size(item.ctx.width, 0);
    //             ImGui::Button("Pause", size);
    //         }

    //         if (ImGui::CoolBarItemGuard item{"imgui_demo2"})
    //         {
    //             ImVec2 size(item.ctx.width, 0);
    //             ImGui::Button("Stop", size);
    //         }

    //         if (ImGui::CoolBarItemGuard item{"imgui_demo3"})
    //         {
    //             ImVec2 size(item.ctx.width, 0);
    //             ImGui::Button("Stop & Reset", size);
    //         }
    //         ImGui::EndCoolBar();
    //     }
    // }
    // Draw drag selection rectangle if active
    if(is_drag_selecting_)
    {
        draw_drag_selection_rect(drag_start_pos_, drag_current_pos_);
    }

    camera_comp.get_pipeline_data().get_pipeline()->set_debug_pass(visualize_passes_);

    auto window = ImGui::GetCurrentWindow();
    auto draw_list = window->DrawList;
    auto clip_rect = window->ClipRect;
    clip_rect.Expand(-ImGui::GetStyle().FramePadding);
    draw_list->PushClipRect(clip_rect.Min, clip_rect.Max);

    auto callbacks = std::move(dd_2d_.callbacks);
    for(const auto& callback : callbacks)
    {
        callback();
    }

    draw_list->PopClipRect();
}

void scene_panel::draw_ui(rtti::context& ctx)
{
    draw_menubar(ctx);

    if(m_skip_frames_ > 0)
    {
        auto spinner_size = ImGui::GetContentRegionAvail().y * 0.2f;

        ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f - spinner_size * 0.5f);
        ImGui::AlignedItem(0.5f,
                           ImGui::GetContentRegionAvail().x,
                           spinner_size,
                           [spinner_size]()
                           {
                                ImSpinner::Spinner<ImSpinner::SpinnerTypeT::e_st_eclipse>("spinner", 
                                    ImSpinner::Radius{spinner_size * 0.5f},
                                    ImSpinner::Thickness{6.0f},
                                    ImSpinner::Color{ImSpinner::white},
                                    ImSpinner::Speed{6.0f});

                           });

        return;
    }

    auto camera_entity = get_camera();

    bool has_edit_camera = camera_entity && camera_entity.all_of<transform_component, camera_component>();

    if(!has_edit_camera)
    {
        return;
    }

    auto avail = ImGui::GetContentRegionAvail();
    if(avail.x <= 0 || avail.y <= 0)
    {
        return;
    }

    // Determine the fitted viewport rectangle for the editor camera based on
    // the resolution preset. We always fit the aspect within the available
    // area so that picking and gizmos map 1:1 between screen pixels and the
    // camera viewport. If no resolution is configured, the panel falls back
    // to the full available area.
    ImVec2 view_size = avail;
    if(const auto* current_res = viewport_resolution::get_resolution(ctx, current_resolution_index_))
    {
        view_size = viewport_resolution::compute_fitted_size(*current_res, avail);
    }

    const auto avail_origin = ImGui::GetCursorScreenPos();
    const ImVec2 view_pos(avail_origin.x + (avail.x - view_size.x) * 0.5f,
                          avail_origin.y + (avail.y - view_size.y) * 0.5f);

    auto& camera_comp = camera_entity.get<camera_component>();

    setup_camera_viewport(camera_comp, view_size, view_pos);
    draw_scene_viewport(ctx, view_size, view_pos);
    process_drag_drop_target(ctx, camera_comp);

    const auto& pstats = camera_comp.get_pipeline_data().get_pipeline()->get_stats();
    viewport_stats_overlay::draw(pstats, stats_overlay_state_, "scene");

    if(stats_overlay_state_.open_profiler_requested)
    {
        stats_overlay_state_.open_profiler_requested = false;
        parent_->get_profiler_timeline_panel().show(true);
    }
}

void scene_panel::draw_menubar(rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();

    if(ImGui::BeginMenuBar())
    {
        // Apply Unity-like styling - more prominent, tab-like appearance
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline));

        draw_prefab_mode_header(ctx);
        viewport_resolution::draw_menu(ctx, current_resolution_index_);
        draw_transform_tools(em);
        draw_gizmo_pivot_mode_menu(gizmo_at_center_);
        draw_coordinate_system_menu(em);
        draw_grid_settings_menu(em);
        draw_gizmos_settings_menu(em);
        draw_visualization_menu();
        draw_snapping_menu(em);
        draw_inverse_kinematics_menu(em);
        draw_camera_settings_menu(ctx);
        viewport_stats_overlay::draw_stats_toggle(stats_overlay_state_);

        ImGui::PopStyleColor(3);

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
