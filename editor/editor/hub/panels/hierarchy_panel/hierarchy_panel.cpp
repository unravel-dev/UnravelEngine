#include "hierarchy_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "imgui/imgui.h"
#include "imgui_widgets/tooltips.h"
#include <imgui/imgui_internal.h>

#include <editor/editing/editing_manager.h>
#include <editor/events.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>

#include <engine/assets/impl/asset_extensions.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <filesystem/filesystem.h>

namespace unravel
{

namespace
{

// ============================================================================
// State Management
// ============================================================================

// Label editing state
bool prev_edit_label{};
bool edit_label_{};

auto update_editing() -> void
{
    prev_edit_label = edit_label_;
}

auto is_just_started_editing_label() -> bool
{
    return edit_label_ && edit_label_ != prev_edit_label;
}

auto is_editing_label() -> bool
{
    return edit_label_;
}

void start_editing_label(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.select(entity);
    edit_label_ = true;
}

void stop_editing_label(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    edit_label_ = false;
}

// ============================================================================
// Entity Creation Helper Functions
// ============================================================================

void create_empty_entity(rtti::context& ctx, imgui_panels* panels, entt::handle parent_entity)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Create Empty Entity",
        [&ctx, panels, parent_entity]() mutable
        {
            auto& em = ctx.get_cached<editing_manager>();
            auto* active_scene = em.get_active_scene(ctx);
            if (active_scene) {
                auto new_entity = active_scene->create_entity({}, parent_entity);
                start_editing_label(ctx, panels, new_entity);
            }
        });
}

void create_empty_parent_entity(rtti::context& ctx, imgui_panels* panels, entt::handle child_entity)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Create Empty Parent Entity",
        [&ctx, panels, child_entity]() mutable
        {
            auto current_parent = child_entity.get<transform_component>().get_parent();
            auto& em = ctx.get_cached<editing_manager>();
            auto* active_scene = em.get_active_scene(ctx);
            
            if (active_scene) {
                auto new_entity = active_scene->create_entity({}, current_parent);
                child_entity.get<transform_component>().set_parent(new_entity);
                start_editing_label(ctx, panels, new_entity);
            }
        });
}

void create_mesh_entity(rtti::context& ctx, imgui_panels* panels, entt::handle parent_entity, const std::string& mesh_name)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Create Mesh Entity",
        [&ctx, panels, parent_entity, mesh_name]() mutable
    {
        auto& em = ctx.get_cached<editing_manager>();
        auto* active_scene = em.get_active_scene(ctx);
        if (active_scene) {
            auto object = defaults::create_embedded_mesh_entity(ctx, *active_scene, mesh_name);

            if(object)
            {
                object.get<transform_component>().set_parent(parent_entity, false);
            }
            em.select(object);
            start_editing_label(ctx, panels, object);
        }
    });
}

void create_text_entity(rtti::context& ctx, imgui_panels* panels, entt::handle parent_entity)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Create Text Entity",
        [&ctx, panels, parent_entity]() mutable
    {
        auto& em = ctx.get_cached<editing_manager>();
        auto* active_scene = em.get_active_scene(ctx);
        auto object = defaults::create_text_entity(ctx, *active_scene, "Text");

        if(object)
        {
            object.get<transform_component>().set_parent(parent_entity, false);
        }
        em.select(object);
        start_editing_label(ctx, panels, object);
    });
}

void create_light_entity(rtti::context& ctx, imgui_panels* panels, entt::handle parent_entity, light_type type, const std::string& name)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Create Light Entity",
        [&ctx, panels, parent_entity, type, name]() mutable
    {
        auto& em = ctx.get_cached<editing_manager>();
        auto* active_scene = em.get_active_scene(ctx);
        auto object = defaults::create_light_entity(ctx, *active_scene, type, name);
        if(object)
        {
            object.get<transform_component>().set_parent(parent_entity, false);
        }
        em.select(object);
        start_editing_label(ctx, panels, object);
    });
}

void create_reflection_probe_entity(rtti::context& ctx, imgui_panels* panels, entt::handle parent_entity, probe_type type, const std::string& name)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Create Reflection Probe Entity",
        [&ctx, panels, parent_entity, type, name]() mutable
    {
        auto& em = ctx.get_cached<editing_manager>();
        auto* active_scene = em.get_active_scene(ctx);
        auto object = defaults::create_reflection_probe_entity(ctx, *active_scene, type, name);
        if(object)
        {
            object.get<transform_component>().set_parent(parent_entity, false);
        }
        em.select(object);
        start_editing_label(ctx, panels, object);
    });
}

void create_camera_entity(rtti::context& ctx, imgui_panels* panels, entt::handle parent_entity)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Create Camera Entity",
        [&ctx, panels, parent_entity]() mutable
    {
        auto& em = ctx.get_cached<editing_manager>();
        auto* active_scene = em.get_active_scene(ctx);
        auto object = defaults::create_camera_entity(ctx, *active_scene, "Camera");
        em.select(object);
        start_editing_label(ctx, panels, object);
    });
}

// ============================================================================
// Drag and Drop Operations
// ============================================================================

auto process_drag_drop_source(entt::handle entity) -> bool
{
    if(entity && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::TextUnformatted(entity_panel::get_entity_name(entity).c_str());
        ImGui::SetDragDropPayload("entity", &entity, sizeof(entity));
        ImGui::EndDragDropSource();
        return true;
    }

    return false;
}

void handle_entity_drop(rtti::context& ctx, imgui_panels* panels, entt::handle target_entity, entt::handle dropped_entity)
{
    auto& em = ctx.get_cached<editing_manager>();

    auto do_action = [&](entt::handle dropped)
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.add_action("Drop Entity",
            [&ctx, target_entity, dropped]() mutable
        {
            auto trans_comp = dropped.try_get<transform_component>();
            if(trans_comp)
            {
                trans_comp->set_parent(target_entity);
            }
        });
    };

    if(em.is_selected(dropped_entity))
    {
        for(auto e : em.try_get_selections_as<entt::handle>())
        {
            if(e)
            {
                do_action(*e);
            }
        }
    }
    else
    {
        do_action(dropped_entity);
    }
}

void handle_mesh_drop(rtti::context& ctx, const std::string& absolute_path)
{

    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Drop Mesh",
        [&ctx, absolute_path]() mutable
    {
        std::string key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();
        auto& em = ctx.get_cached<editing_manager>();
        auto* active_scene = em.get_active_scene(ctx);
        
        if (active_scene) 
        {
            auto object = defaults::create_mesh_entity_at(ctx, *active_scene, key);
            em.select(object);
        }
    });

}

void handle_prefab_drop(rtti::context& ctx, const std::string& absolute_path)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.add_action("Drop Prefab",
        [&ctx, absolute_path]() mutable
    {
        std::string key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();

        auto& em = ctx.get_cached<editing_manager>();
        auto* active_scene = em.get_active_scene(ctx);
    
        if (active_scene) 
        {
            auto object = defaults::create_prefab_at(ctx, *active_scene, key);
            em.select(object);
        }
    });
}

void process_drag_drop_target(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    if(!ImGui::BeginDragDropTarget())
    {
        return;
    }

    if(ImGui::IsDragDropPayloadBeingAccepted())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    else
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
    }

    // Handle entity drag and drop
    auto payload = ImGui::AcceptDragDropPayload("entity");
    if(payload != nullptr)
    {
        entt::handle dropped{};
        std::memcpy(&dropped, payload->Data, size_t(payload->DataSize));
        if(dropped)
        {
            handle_entity_drop(ctx, panels, entity, dropped);
        }
    }

    // Handle mesh drag and drop
    for(const auto& type : ex::get_suported_formats<mesh>())
    {
        auto mesh_payload = ImGui::AcceptDragDropPayload(type.c_str());
        if(mesh_payload != nullptr)
        {
            std::string absolute_path(reinterpret_cast<const char*>(mesh_payload->Data), std::size_t(mesh_payload->DataSize));
            handle_mesh_drop(ctx, absolute_path);
        }
    }

    // Handle prefab drag and drop
    for(const auto& type : ex::get_suported_formats<prefab>())
    {
        auto prefab_payload = ImGui::AcceptDragDropPayload(type.c_str());
        if(prefab_payload != nullptr)
        {
            std::string absolute_path(reinterpret_cast<const char*>(prefab_payload->Data), std::size_t(prefab_payload->DataSize));
            handle_prefab_drop(ctx, absolute_path);
        }
    }

    ImGui::EndDragDropTarget();
}

void check_drag(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    if(!process_drag_drop_source(entity))
    {
        process_drag_drop_target(ctx, panels, entity);
    }
}

// ============================================================================
// Context Menu Functions
// ============================================================================

void draw_3d_objects_menu(rtti::context& ctx, imgui_panels* panels, entt::handle parent_entity)
{
    if(!ImGui::BeginMenu("3D Objects"))
    {
        return;
    }

    static const std::vector<std::pair<std::string, std::vector<std::string>>> menu_objects = {
        {"Cube", {"Cube"}},
        {"Cube Rounded", {"Cube Rounded"}},
        {"Sphere", {"Sphere"}},
        {"Plane", {"Plane"}},
        {"Cylinder", {"Cylinder"}},
        {"Capsule", {"Capsule"}},
        {"Cone", {"Cone"}},
        {"Torus", {"Torus"}},
        {"Teapot", {"Teapot"}},
        {"Separator", {}},
        {"Polygon", {"Icosahedron", "Dodecahedron"}},
        {"Icosphere", {"Icosphere0",  "Icosphere1",  "Icosphere2",  "Icosphere3",  "Icosphere4",
                       "Icosphere5",  "Icosphere6",  "Icosphere7",  "Icosphere8",  "Icosphere9",
                       "Icosphere10", "Icosphere11", "Icosphere12", "Icosphere13", "Icosphere14",
                       "Icosphere15", "Icosphere16", "Icosphere17", "Icosphere18", "Icosphere19"}}};

    for(const auto& p : menu_objects)
    {
        const auto& name = p.first;
        const auto& objects_name = p.second;

        if(name == "Separator")
        {
            ImGui::Separator();
        }
        else if(name == "New Line")
        {
            ImGui::NextLine();
        }
        else if(objects_name.size() == 1)
        {
            if(ImGui::MenuItem(name.c_str()))
            {
                create_mesh_entity(ctx, panels, parent_entity, name);
            }
        }
        else
        {
            if(ImGui::BeginMenu(name.c_str()))
            {
                for(const auto& n : objects_name)
                {
                    if(ImGui::MenuItem(n.c_str()))
                    {
                        create_mesh_entity(ctx, panels, parent_entity, n);
                    }
                }
                ImGui::EndMenu();
            }
        }
    }

    ImGui::NextLine();
    ImGui::Separator();

    if(ImGui::MenuItem("Text"))
    {
        create_text_entity(ctx, panels, parent_entity);
    }

    ImGui::EndMenu();
}

void draw_lighting_menu(rtti::context& ctx, imgui_panels* panels, entt::handle parent_entity)
{
    if(!ImGui::BeginMenu("Lighting"))
    {
        return;
    }

    // Light submenu
    if(ImGui::BeginMenu("Light"))
    {
        static const std::vector<std::pair<std::string, light_type>> light_objects = {
            {"Directional", light_type::directional},
            {"Spot", light_type::spot},
            {"Point", light_type::point}};

        for(const auto& p : light_objects)
        {
            const auto& name = p.first;
            const auto& type = p.second;
            if(ImGui::MenuItem(name.c_str()))
            {
                create_light_entity(ctx, panels, parent_entity, type, name);
            }
        }
        ImGui::EndMenu();
    }

    // Reflection probes submenu
    if(ImGui::BeginMenu("Reflection Probes"))
    {
        static const std::vector<std::pair<std::string, probe_type>> reflection_probes = {
            {"Sphere", probe_type::sphere},
            {"Box", probe_type::box}};
        
        for(const auto& p : reflection_probes)
        {
            const auto& name = p.first;
            const auto& type = p.second;

            if(ImGui::MenuItem(name.c_str()))
            {
                create_reflection_probe_entity(ctx, panels, parent_entity, type, name);
            }
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

void draw_common_menu_items(rtti::context& ctx, imgui_panels* panels, entt::handle parent_entity)
{
    if(ImGui::MenuItem("Create Empty"))
    {
        create_empty_entity(ctx, panels, parent_entity);
    }

    draw_3d_objects_menu(ctx, panels, parent_entity);
    draw_lighting_menu(ctx, panels, parent_entity);

    if(ImGui::MenuItem("Camera"))
    {
        create_camera_entity(ctx, panels, parent_entity);
    }
}

void draw_entity_context_menu(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    if(!ImGui::BeginPopupContextItem("Entity Context Menu"))
    {
        return;
    }

    if(ImGui::MenuItem("Create Empty Parent"))
    {
        create_empty_parent_entity(ctx, panels, entity);
    }

    draw_common_menu_items(ctx, panels, entity);

    ImGui::Separator();

    if(ImGui::MenuItem("Rename", ImGui::GetKeyName(shortcuts::rename_item)))
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.add_action("Rename Entity",
            [ctx, panels, entity]() mutable
            {
                start_editing_label(ctx, panels, entity);
            });
    }

    if(ImGui::MenuItem("Duplicate", ImGui::GetKeyCombinationName(shortcuts::duplicate_item).c_str()))
    {
        panels->get_scene_panel().duplicate_entities({entity});
    }

    if(ImGui::MenuItem("Delete", ImGui::GetKeyName(shortcuts::delete_item)))
    {
        panels->get_scene_panel().delete_entities({entity});
    }

    if(ImGui::MenuItem("Focus", ImGui::GetKeyName(shortcuts::focus_selected)))
    {
        panels->get_scene_panel().focus_entities(panels->get_scene_panel().get_camera(), {entity});
    }

    ImGui::Separator();

    if(entity.any_of<prefab_component>())
    {
        if(ImGui::MenuItem("Unlink from Prefab"))
        {
            auto& em = ctx.get_cached<editing_manager>();
            em.add_action("Unlink from Prefab",
            [entity]() mutable
            {
                entity.remove<prefab_component>();
            });
        }
    }

    ImGui::EndPopup();
}

void draw_window_context_menu(rtti::context& ctx, imgui_panels* panels)
{
    if(!ImGui::BeginPopupContextWindowEx())
    {
        return;
    }

    draw_common_menu_items(ctx, panels, {});
    ImGui::EndPopup();
}

void check_context_menu(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_Text));

    if(entity)
    {
        draw_entity_context_menu(ctx, panels, entity);
    }
    else
    {
        draw_window_context_menu(ctx, panels);
    }

    ImGui::PopStyleColor();
}

// ============================================================================
// Entity Drawing and Interaction
// ============================================================================

void draw_activity(rtti::context& ctx, transform_component& trans_comp)
{
    bool is_active_local = trans_comp.is_active();
    if(!is_active_local)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    }

    if(ImGui::Button(is_active_local ? ICON_MDI_EYE : ICON_MDI_EYE_OFF))
    {
        trans_comp.set_active(!is_active_local);

        auto entity = trans_comp.get_owner();
        auto& em = ctx.get_cached<editing_manager>();
        em.add_action("Toggle Active",
            [entity]() mutable
        {
            prefab_override_context::mark_active_as_changed(entity);
        });
    }

    if(!is_active_local)
    {
        ImGui::PopStyleColor();
    }
}

auto is_parent_of_focused(rtti::context& ctx, entt::handle entity) -> bool
{
    auto& em = ctx.get_cached<editing_manager>();
    auto focus = em.try_get_active_focus_as<entt::handle>();
    if(focus)
    {
        if(transform_component::is_parent_of(entity, *focus))
        {
            return true;
        }
    }

    return false;
}

auto get_entity_tree_node_flags(rtti::context& ctx, entt::handle entity, bool has_children) -> ImGuiTreeNodeFlags
{
    auto& em = ctx.get_cached<editing_manager>();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_OpenOnArrow;

    if(em.is_selected(entity))
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if(!has_children)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    return flags;
}

auto get_entity_display_color(entt::handle entity) -> ImVec4
{
    auto& trans_comp = entity.get<transform_component>();
    bool is_bone = entity.all_of<bone_component>();
    bool is_submesh = entity.all_of<submesh_component>();
    bool is_active_global = trans_comp.is_active_global();
    bool has_source = entity.any_of<prefab_component, prefab_id_component>();
    bool has_broken_source = false;

    if(auto pfb = entity.try_get<prefab_component>())
    {
        if(!pfb->source)
        {
            has_source = false;
            has_broken_source = true;
        }
    }

    if(auto prefab_id = entity.try_get<prefab_id_component>())
    {
        auto root = prefab_override_context::find_prefab_root_entity(entity);
        if(root)
        {
            if(auto pfb = root.try_get<prefab_component>())
            {
                if(!pfb->source)
                {
                    has_broken_source = true;
                    has_source = false;
                }
            }
        }
    }

    auto col = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    col = ImLerp(col, ImVec4(0.5f, 0.85f, 1.0f, 1.0f), float(has_source) * 0.5f);
    col = ImLerp(col, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), float(has_broken_source) * 0.5f);
    col = ImLerp(col, ImVec4(0.5f, 0.85f, 1.0f, 1.0f), float(is_bone) * 0.5f);
    col = ImLerp(col, ImVec4(0.8f, 0.4f, 0.4f, 1.0f), float(is_submesh) * 0.5f);
    col = ImLerp(col, ImVec4(col.x * 0.75f, col.y * 0.75f, col.z * 0.75f, col.w * 0.75f), float(!is_active_global));

    return col;
}

auto get_entity_display_label(entt::handle entity) -> std::string
{
    const auto& name = entity_panel::get_entity_name(entity);
    bool is_bone = entity.all_of<bone_component>();
    bool has_source = entity.any_of<prefab_component>();

    auto icon = has_source ? ICON_MDI_CUBE " " : ICON_MDI_CUBE_OUTLINE " ";
    if(is_bone)
    {
        icon = ICON_MDI_BONE " ";
    }

    const auto ent = entity.entity();
    const auto id = entt::to_integral(ent);

    return icon + name +"###" + std::to_string(id);
}

void handle_entity_selection(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    auto& em = ctx.get_cached<editing_manager>();
    auto mode = em.get_select_mode();
    em.add_action("Select Entity",
        [&ctx, panels, entity, mode]() mutable
        {
            stop_editing_label(ctx, panels, entity);
            auto& em = ctx.get_cached<editing_manager>();
            em.select(entity, mode);
        });
}

void handle_entity_keyboard_shortcuts(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    if(ImGui::IsItemKeyPressed(shortcuts::rename_item))
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.add_action("Rename Entity",
            [&ctx, panels, entity]() mutable
            {
                start_editing_label(ctx, panels, entity);
            });
    }

    if(ImGui::IsItemKeyPressed(shortcuts::delete_item))
    {
        panels->get_scene_panel().delete_entities({entity});
    }

    if(ImGui::IsItemKeyPressed(shortcuts::focus_selected))
    {
        panels->get_scene_panel().focus_entities(panels->get_scene_panel().get_camera(), {entity});
    }

    if(ImGui::IsItemCombinationKeyPressed(shortcuts::duplicate_item))
    {
        panels->get_scene_panel().duplicate_entities({entity});
    }
}

void handle_entity_mouse_interactions(rtti::context& ctx, imgui_panels* panels, entt::handle entity, bool is_item_clicked_middle, bool is_item_double_clicked_left)
{
    if(is_item_clicked_middle)
    {
        panels->get_scene_panel().focus_entities(panels->get_scene_panel().get_camera(), {entity});
    }

    if(is_item_double_clicked_left)
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.add_action("Start Editing Label",
            [&ctx, panels, entity]() mutable
            {
                start_editing_label(ctx, panels, entity);
            });
    }
}

void draw_entity_name_editor(rtti::context& ctx, imgui_panels* panels, entt::handle entity, const ImVec2& pos)
{
    auto& em = ctx.get_cached<editing_manager>();
    if(!em.is_selected(entity) || !is_editing_label())
    {
        return;
    }

    if(is_just_started_editing_label())
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetCursorScreenPos(pos);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

    auto edit_name = entity_panel::get_entity_name(entity);
    ImGui::InputTextWidget("##rename", edit_name, false, ImGuiInputTextFlags_AutoSelectAll);
    
    if(ImGui::IsItemDeactivatedAfterEdit())
    {
        entity_panel::set_entity_name(entity, edit_name);
        stop_editing_label(ctx, panels, entity);
    }

    ImGui::PopItemWidth();

    if(ImGui::IsItemDeactivated())
    {
        stop_editing_label(ctx, panels, entity);
    }
}

void draw_entity(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    if(!entity)
    {
        return;
    }

    auto& em = ctx.get_cached<editing_manager>();
    ImGui::PushID(static_cast<int>(entity.entity()));

    auto& trans_comp = entity.get<transform_component>();
    bool has_children = !trans_comp.get_children().empty();

    ImGuiTreeNodeFlags flags = get_entity_tree_node_flags(ctx, entity, has_children);

    if(is_parent_of_focused(ctx, entity))
    {
        ImGui::SetNextItemOpen(true, 0);
        ImGui::SetScrollHereY();
    }

    auto pos = ImGui::GetCursorScreenPos() + ImVec2(ImGui::GetTextLineHeightWithSpacing(), 0.0f);
    ImGui::AlignTextToFramePadding();

    auto label = get_entity_display_label(entity);
    auto col = get_entity_display_color(entity);

    ImGui::PushStyleColor(ImGuiCol_Text, col);
    bool opened = ImGui::TreeNodeEx(label.c_str(), flags);

    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        const auto ent = entity.entity();
        const auto idx = entt::to_entity(ent);
        const auto ver = entt::to_version(ent);
        const auto id = entt::to_integral(ent);
    
        ImGui::SetItemTooltipEx("Id: %d\nIndex: %d\nVersion: %d", id, idx, ver);
    }

    ImGui::PopStyleColor();

    if(em.is_focused(entity))
    {
        ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 1.0f)));
    }
    
    if(!is_editing_label())
    {
        check_drag(ctx, panels, entity);
        check_context_menu(ctx, panels, entity);
    }
    
    // Collect interaction states
    bool is_item_focus_changed = ImGui::IsItemFocusChanged();
    bool is_item_released_left = ImGui::IsItemReleased(ImGuiMouseButton_Left);
    bool is_item_clicked_middle = ImGui::IsItemClicked(ImGuiMouseButton_Middle);
    bool is_item_double_clicked_left = ImGui::IsItemDoubleClicked(ImGuiMouseButton_Left);
    bool activity_hovered = false;

    // Draw activity button
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::AlignedItem(1.0f,
                       ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x,
                       ImGui::GetFrameHeight(),
                       [&]()
                       {
                           draw_activity(ctx, trans_comp);
                           activity_hovered = ImGui::IsItemHovered();
                       });

    // Handle interactions (only if not hovering activity button)
    if(!activity_hovered)
    {
        if(is_item_released_left || is_item_focus_changed)
        {
            handle_entity_selection(ctx, panels, entity);
        }

        if(em.is_selected(entity))
        {
            handle_entity_mouse_interactions(ctx, panels, entity, is_item_clicked_middle, is_item_double_clicked_left);
            handle_entity_keyboard_shortcuts(ctx, panels, entity);
        }
    }

    // Draw name editor if in editing mode
    draw_entity_name_editor(ctx, panels, entity, pos);

    // Draw children
    if(opened)
    {
        if(has_children)
        {
            const auto& children = trans_comp.get_children();
            for(auto& child : children)
            {
                if(child)
                {
                    draw_entity(ctx, panels, child);
                }
            }
        }

        ImGui::TreePop();
    }

    ImGui::PopID();
}

} // namespace

// ============================================================================
// Hierarchy Panel Implementation
// ============================================================================

hierarchy_panel::hierarchy_panel(imgui_panels* parent) : entity_panel(parent)
{
}

void hierarchy_panel::init(rtti::context& ctx)
{
}

void hierarchy_panel::draw_prefab_mode_header(rtti::context& ctx) const
{
    auto& em = ctx.get_cached<editing_manager>();
    
    if(!em.is_prefab_mode())
    {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
    if (ImGui::Button(ICON_MDI_KEYBOARD_RETURN " Back to Scene"))
    {
        em.exit_prefab_mode(ctx, editing_manager::save_option::yes);
    }
    ImGui::PopStyleColor();
    
    if (em.edited_prefab)
    {
        ImGui::SameLine();
        ImGui::Text("Editing Prefab: %s", fs::path(em.edited_prefab.id()).filename().string().c_str());
    }
    
    ImGui::Separator();
}

auto hierarchy_panel::get_scene_display_name(const editing_manager& em, scene* target_scene) const -> std::string
{
    std::string name;
    
    if (em.is_prefab_mode())
    {
        name = fs::path(em.edited_prefab.id()).filename().string();
        if (name.empty())
        {
            name = "Prefab";
        }
    }
    else
    {
        name = target_scene->source.name();
        if (name.empty())
        {
            name = "Unnamed";
        }
        name.append(" ").append(ex::get_type<scene_prefab>());

        if(em.has_unsaved_changes())
        {
            name.append("*");
        }
    }

    return name;
}

void hierarchy_panel::draw_scene_hierarchy(rtti::context& ctx) const
{
    auto& em = ctx.get_cached<editing_manager>();
    scene* target_scene = em.get_active_scene(ctx);
    
    if (!target_scene)
    {
        return;
    }

    std::string scene_name = get_scene_display_name(em, target_scene);

    ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    if(ImGui::CollapsingHeader(scene_name.c_str()))
    {
        if(is_roots_order_changed())
        {
            target_scene->registry->sort<root_component>(
                [](auto const& lhs, auto const& rhs)
                {
                    // Return true if lhs should come before rhs
                    return lhs.order < rhs.order;
                });

            reset_roots_order_changed();
        }

        // lead by root_component, so that the order is determined by it.
        target_scene->registry->view<root_component, transform_component>().each(
            [&](auto e, auto&& root, auto&& comp)
            {
                draw_entity(ctx, parent_, comp.get_owner());
            });
    }

    handle_window_empty_click(ctx);
}

void hierarchy_panel::handle_window_empty_click(rtti::context& ctx) const
{
    auto& em = ctx.get_cached<editing_manager>();
    if(ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if(!ImGui::IsAnyItemHovered())
        {
            em.unselect();
        }
    }
}

void hierarchy_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    entity_panel::on_frame_ui_render();

    if(ImGui::Begin(name))
    {
        draw_prefab_mode_header(ctx);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;

        if(ImGui::BeginChild("hierarchy_content", ImGui::GetContentRegionAvail(), 0, flags))
        {
            check_context_menu(ctx, parent_, {});
            draw_scene_hierarchy(ctx);
        }
        ImGui::EndChild();
        
        check_drag(ctx, parent_, {});
    }
    ImGui::End();

    update_editing();
}

} // namespace unravel
