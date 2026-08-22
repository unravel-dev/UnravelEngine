#include "hierarchy_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "imgui/imgui.h"
#include "imgui_widgets/tooltips.h"
#include <imgui/imgui_internal.h>

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <editor/editing/editing_manager.h>
#include <editor/shortcuts.h>
#include <editor/events.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>

#include <engine/assets/impl/asset_extensions.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <editor/editing/authoring_root.h>
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

void start_editing_label(rtti::context& ctx, entt::handle entity)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.select(entity);
    edit_label_ = true;
}

void stop_editing_label(rtti::context& ctx, entt::handle entity)
{
    edit_label_ = false;
}

// ============================================================================
// Entity Creation Helper Functions
// ============================================================================

// Factory wrapper used by all create_* helpers below. Creation + parenting happen inside a single
// create_entities_action_t whose snapshot captures the parent link, so undo/redo treat the whole
// "create at parent" as one atomic step. When start_label_edit is true the new entity is selected
// and the rename field is opened; otherwise it is just selected (useful for drag-drop imports
// where immediate rename is undesirable).
void queue_create_with_parent(rtti::context& ctx,
                              entt::handle parent_entity,
                              const std::string& action_name,
                              std::function<entt::handle(rtti::context&, scene&)> producer,
                              bool start_label_edit = true)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.push_undo_stack_enabled(true);
    em.queue_action<create_entities_action_t>(
        action_name,
        [&ctx, parent_uh = entt::make_uhandle(parent_entity), producer = std::move(producer), start_label_edit]() -> entt::handle
        {
            auto& em = ctx.get_cached<editing_manager>();
            auto* active_scene = em.get_active_scene(ctx);
            if(!active_scene)
            {
                return {};
            }

            auto new_entity = producer(ctx, *active_scene);
            if(!new_entity)
            {
                return {};
            }

            // Set parent inline: create_entities_action_t snapshots the parent uhandle in its
            // subtree capture, so redo restores the hierarchy without a second action entry.
            if(auto parent = parent_uh.resolve())
            {
                if(auto* tr = new_entity.try_get<transform_component>())
                {
                    tr->set_parent(parent, false);
                }
            }

            if(start_label_edit)
            {
                start_editing_label(ctx, new_entity);
            }
            else
            {
                em.select(new_entity);
            }
            return new_entity;
        });
    em.pop_undo_stack_enabled();
}

void create_empty_entity(rtti::context& ctx, entt::handle parent_entity)
{
    queue_create_with_parent(ctx, parent_entity, "Create Entity",
        [](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return scn.create_entity();
        });
}

void create_empty_parent_entity(rtti::context& ctx, entt::handle child_entity)
{
    if(!child_entity)
    {
        return;
    }

    auto& em = ctx.get_cached<editing_manager>();
    auto current_parent = child_entity.get<transform_component>().get_parent();

    // Shared slot so the second step can reference the wrapper produced by step 1.
    auto created_uh = std::make_shared<entt::uhandle>();

    auto seq = std::make_shared<sequence_action_t>();

    // Step 1: create the wrapper entity and parent it under the child's original parent.
    // create_entities_action_t snapshots the parent link, so redo restores the hierarchy
    // without needing a separate set-parent action for the wrapper itself.
    seq->add_step(
        [&ctx, current_parent, created_uh]() -> std::shared_ptr<editing_action_t>
        {
            return std::make_shared<create_entities_action_t>(
                [&ctx, current_parent, created_uh]() -> entt::handle
                {
                    auto& em = ctx.get_cached<editing_manager>();
                    auto* active_scene = em.get_active_scene(ctx);
                    if(!active_scene)
                    {
                        return {};
                    }
                    auto new_entity = active_scene->create_entity();
                    if(!new_entity)
                    {
                        return {};
                    }
                    if(current_parent)
                    {
                        new_entity.get<transform_component>().set_parent(current_parent, false);
                    }
                    *created_uh = entt::make_uhandle(new_entity);
                    start_editing_label(ctx, new_entity);
                    return new_entity;
                });
        });

    // Step 2: reparent the original child under the new wrapper. Kept as a separate step
    // so undo unwinds it BEFORE the wrapper is destroyed (otherwise the child would be
    // swept up in the wrapper's subtree teardown).
    seq->add_step(
        [child_entity, current_parent, created_uh]() -> std::shared_ptr<editing_action_t>
        {
            auto new_wrapper = created_uh->resolve();
            if(!new_wrapper)
            {
                return nullptr;
            }
            return std::make_shared<transform_set_parent_action_t>(child_entity, current_parent, new_wrapper);
        });

    em.push_undo_stack_enabled(true);
    em.queue_action("Create Parent Entity", seq);
    em.pop_undo_stack_enabled();
}


void create_mesh_entity(rtti::context& ctx, entt::handle parent_entity, const std::string& mesh_name)
{
    queue_create_with_parent(ctx, parent_entity, "Create Mesh Entity",
        [mesh_name](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_embedded_mesh_entity(ctx, scn, mesh_name);
        });
}

void create_text_entity(rtti::context& ctx, entt::handle parent_entity)
{
    queue_create_with_parent(ctx, parent_entity, "Create Text Entity",
        [](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_text_entity(ctx, scn, "Text");
        });
}

void create_particle_emitter_entity(rtti::context& ctx, entt::handle parent_entity)
{
    queue_create_with_parent(ctx, parent_entity, "Create Particle Emitter Entity",
        [](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_particle_emitter_entity(ctx, scn, "Particle Emitter");
        });
}

void create_light_entity(rtti::context& ctx, entt::handle parent_entity, light_type type, const std::string& name)
{
    queue_create_with_parent(ctx, parent_entity, "Create Light Entity",
        [type, name](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_light_entity(ctx, scn, type, name);
        });
}

void create_reflection_probe_entity(rtti::context& ctx, entt::handle parent_entity, probe_type type, const std::string& name)
{
    queue_create_with_parent(ctx, parent_entity, "Create Reflection Probe Entity",
        [type, name](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_reflection_probe_entity(ctx, scn, type, name);
        });
}

void create_camera_entity(rtti::context& ctx, entt::handle parent_entity)
{
    queue_create_with_parent(ctx, parent_entity, "Create Camera Entity",
        [](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_camera_entity(ctx, scn, "Camera");
        });
}

void create_volume_entity(rtti::context& ctx, entt::handle parent_entity)
{
    queue_create_with_parent(ctx, parent_entity, "Create Volume Entity",
        [](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_volume_entity(ctx, scn, "Volume");
        });
}

void create_audio_source_entity(rtti::context& ctx, entt::handle parent_entity)
{
    queue_create_with_parent(ctx, parent_entity, "Create Audio Source Entity",
        [](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_audio_source_entity(ctx, scn, "Audio Source");
        });
}

void create_ui_document_entity(rtti::context& ctx, entt::handle parent_entity)
{
    queue_create_with_parent(ctx, parent_entity, "Create UI Document Entity",
        [](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_ui_document_entity(ctx, scn, "UI Document");
        });
}

void create_terrain_entity(rtti::context& ctx, entt::handle parent_entity)
{
    queue_create_with_parent(ctx, parent_entity, "Create Terrain Entity",
        [](rtti::context& ctx, scene& scn) -> entt::handle
        {
            return defaults::create_terrain(ctx, scn);
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

void handle_entity_drop(rtti::context& ctx, entt::handle target_entity, entt::handle dropped_entity)
{
    auto& em = ctx.get_cached<editing_manager>();

    auto do_action = [&](entt::handle dropped)
    {
        auto& em = ctx.get_cached<editing_manager>();
        auto action = std::make_shared<transform_set_parent_action_t>(dropped, dropped.get<transform_component>().get_parent(), target_entity);
        em.push_undo_stack_enabled(true);
        em.queue_action("", std::move(action));
        em.pop_undo_stack_enabled();
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
    queue_create_with_parent(ctx, entt::handle{}, "Drop Mesh",
        [absolute_path](rtti::context& ctx, scene& scn) -> entt::handle
        {
            std::string key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();
            return defaults::create_mesh_entity_at(ctx, scn, key);
        },
        false);
}

void handle_prefab_drop(rtti::context& ctx, const std::string& absolute_path)
{
    queue_create_with_parent(ctx, entt::handle{}, "Drop Prefab",
        [absolute_path](rtti::context& ctx, scene& scn) -> entt::handle
        {
            std::string key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();
            return defaults::create_prefab_at(ctx, scn, key);
        },
        false);
}

void process_drag_drop_target(rtti::context& ctx, entt::handle entity)
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
            handle_entity_drop(ctx, entity, dropped);
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

void check_drag(rtti::context& ctx, entt::handle entity)
{
    if(!process_drag_drop_source(entity))
    {
        process_drag_drop_target(ctx, entity);
    }
}

// ============================================================================
// Context Menu Functions
// ============================================================================

void draw_3d_objects_menu(rtti::context& ctx, entt::handle parent_entity)
{
    if(!ImGui::BeginMenuIcon(ICON_MDI_CUBE, "3D Objects"))
    {
        return;
    }

    static const std::vector<std::pair<std::string, std::vector<std::string>>> menu_objects = {
        {"Cube", {"Cube"}},
        {"Cube Rounded", {"Cube Rounded"}},
        {"Sphere", {"Sphere"}},
        {"Plane", {"Plane"}},
        {"Cylinder", {"Cylinder"}},
        {"Capsule_1m", {"Capsule_1m"}},
        {"Capsule_2m", {"Capsule_2m"}},
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
                create_mesh_entity(ctx, parent_entity, name);
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
                        create_mesh_entity(ctx, parent_entity, n);
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
        create_text_entity(ctx, parent_entity);
    }

    ImGui::NextLine();
    ImGui::Separator();

    if(ImGui::MenuItem("Terrain"))
    {
        create_terrain_entity(ctx, parent_entity);
    }

    ImGui::EndMenu();
}

void draw_lighting_menu(rtti::context& ctx, entt::handle parent_entity)
{
    if(!ImGui::BeginMenuIcon(ICON_MDI_LIGHTBULB_ON, "Lighting"))
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
                create_light_entity(ctx, parent_entity, type, name);
            }
        }
        ImGui::EndMenu();
    }

    // Reflection probes submenu
    if(ImGui::BeginMenu("Reflection Probes"))
    {
        static const std::vector<std::pair<std::string, probe_type>> reflection_probes = {
            {"", probe_type::sphere},
            {"", probe_type::box}};
        
        for(const auto& p : reflection_probes)
        {
            const auto& name = p.first;
            const auto& type = p.second;

            if(ImGui::MenuItem(name.c_str()))
            {
                create_reflection_probe_entity(ctx, parent_entity, type, name);
            }
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

void draw_common_menu_items(rtti::context& ctx, entt::handle parent_entity)
{
    if(ImGui::MenuItemIcon(ICON_MDI_PLUS_BOX_OUTLINE, "Create Empty"))
    {
        create_empty_entity(ctx, parent_entity);
    }

    draw_3d_objects_menu(ctx, parent_entity);
    draw_lighting_menu(ctx, parent_entity);

    if(ImGui::MenuItemIcon(ICON_MDI_CAMERA, "Camera"))
    {
        create_camera_entity(ctx, parent_entity);
    }

    if(ImGui::MenuItemIcon(ICON_MDI_RESIZE, "Volume"))
    {
        create_volume_entity(ctx, parent_entity);
    }

    if(ImGui::MenuItemIcon(ICON_MDI_VOLUME_HIGH, "Audio Source"))
    {
        create_audio_source_entity(ctx, parent_entity);
    }

    if(ImGui::MenuItemIcon(ICON_MDI_FLARE, "Particle Emitter"))
    {
        create_particle_emitter_entity(ctx, parent_entity);
    }

    if(ImGui::MenuItemIcon(ICON_MDI_FILE_DOCUMENT, "UI Document"))
    {
        create_ui_document_entity(ctx, parent_entity);
    }
}

void draw_entity_context_menu(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    if(ImGui::BeginPopupContextItem("Entity Context Menu"))
    {
        {
            ImGui::ContextMenuStyleScope style_scope;

            if(ImGui::MenuItemIcon(ICON_MDI_ARRANGE_BRING_FORWARD, "Create Empty Parent"))
            {
                create_empty_parent_entity(ctx, entity);
            }

            draw_common_menu_items(ctx, entity);

            ImGui::Separator();

            if(ImGui::MenuItemIcon(ICON_MDI_PENCIL, "Rename", ImGui::GetKeyName(shortcuts::rename_item)))
            {
                auto& em = ctx.get_cached<editing_manager>();
                em.queue_action("Rename Entity",
                    [ctx, entity]() mutable
                    {
                        start_editing_label(ctx, entity);
                    });
            }

            if(ImGui::MenuItemIcon(ICON_MDI_CONTENT_COPY,
                                   "Duplicate",
                                   ImGui::GetKeyCombinationName(shortcuts::duplicate_item).c_str()))
            {
                panels->get_scene_panel().duplicate_entities({entity});
            }

            if(ImGui::MenuItemIcon(ICON_MDI_DELETE, "Delete", ImGui::GetKeyName(shortcuts::delete_item)))
            {
                panels->get_scene_panel().delete_entities({entity});
            }

            if(ImGui::MenuItemIcon(ICON_MDI_CROSSHAIRS_GPS, "Focus", ImGui::GetKeyName(shortcuts::focus_selected)))
            {
                panels->get_scene_panel().focus_entities(panels->get_scene_panel().get_camera(), {entity});
            }

            ImGui::Separator();

            // Not for the prefab being edited: "Open Prefab" would open itself, and "Unlink"
            // would unpack the file's own content - strip its prefab ids - under the user.
            if(entity.any_of<prefab_component, prefab_id_component>() && !is_authoring_root(entity))
            {
                if(ImGui::MenuItemIcon(ICON_MDI_OPEN_IN_NEW, "Open Prefab"))
                {
                    auto& em = ctx.get_cached<editing_manager>();
                    em.queue_action("Open Prefab",
                    [&ctx, entity, panels]() mutable
                    {
                        auto prefab_root = prefab_override_context::find_prefab_root_entity(entity);
                        if(prefab_root)
                        {
                            auto prefab = prefab_root.get<prefab_component>().source;
                            if(prefab)
                            {
                                auto& em = ctx.get_cached<editing_manager>();
                                em.enter_prefab_mode(ctx, prefab, true);
                            }
                        }
                    });
                }

                if(ImGui::MenuItemIcon(ICON_MDI_LINK_OFF, "Unlink from Prefab"))
                {
                    auto& em = ctx.get_cached<editing_manager>();
                    em.queue_action("Unlink from Prefab",
                    [entity]() mutable
                    {
                        entity.remove<prefab_component, prefab_id_component>();
                    });
                }
            }
        }
        ImGui::EndPopup();
    }
}

void draw_window_context_menu(rtti::context& ctx, imgui_panels* panels)
{
    if(ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight))
    {
        {
            ImGui::ContextMenuStyleScope style_scope;

            draw_common_menu_items(ctx, {});
        }
        ImGui::EndPopup();
    }
}

void check_context_menu(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    if(entity)
    {
        draw_entity_context_menu(ctx, panels, entity);
    }
    else
    {
        draw_window_context_menu(ctx, panels);
    }
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

        em.push_undo_stack_enabled(true);

        em.queue_action<entity_set_active_action_t>({},
            entity,
            is_active_local,
            !is_active_local);

        em.pop_undo_stack_enabled();
        
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

    flags |= ImGuiTreeNodeFlags_DrawLinesToNodes;

    return flags;
}

auto get_entity_display_label(entt::handle entity) -> std::string
{
    auto name = entity_panel::get_entity_name(entity);
    auto icon = entity_panel::get_entity_icon(entity);

    // Badged only when it is *not* what the surrounding subtree implies. A prefab instance
    // inside a prefab is the ordinary case and stays clean; one added here is the exception,
    // and the exception is what a row has to call out.
    const char* badge = "";
    switch(entity_panel::get_entity_prefab_role(entity))
    {
        case entity_panel::prefab_role::local_instance:
            badge = " " ICON_MDI_PLUS_CIRCLE_OUTLINE;
            break;
        case entity_panel::prefab_role::local_content:
            badge = " " ICON_MDI_PLUS;
            break;
        default:
            break;
    }

    const auto ent = entity.entity();
    const auto id = entt::to_integral(ent);

    return icon + name + badge + "###" + std::to_string(id);
}

void handle_entity_selection(rtti::context& ctx, entt::handle entity)
{
    auto& em = ctx.get_cached<editing_manager>();
    auto mode = em.get_select_mode();
    em.queue_action("Select Entity",
        [&ctx, entity, mode]() mutable
        {
            stop_editing_label(ctx, entity);
            auto& em = ctx.get_cached<editing_manager>();
            em.select(entity, mode);
        });
}

void handle_entity_keyboard_shortcuts(rtti::context& ctx, imgui_panels* panels, entt::handle entity)
{
    if(ImGui::IsItemKeyPressed(shortcuts::rename_item))
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.queue_action("Rename Entity",
            [&ctx, entity]() mutable
            {
                start_editing_label(ctx, entity);
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
        panels->get_scene_panel().focus_entities(panels->get_scene_panel().get_camera(), {entity});
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
    auto old_name = edit_name;
    ImGui::InputTextWidget("##rename", edit_name, false, ImGuiInputTextFlags_AutoSelectAll);
    
    if(ImGui::IsItemDeactivatedAfterEdit())
    {
        
        auto& em = ctx.get_cached<editing_manager>();
        em.push_undo_stack_enabled(true);
        em.queue_action<entity_set_name_action_t>({},
            entity,
            old_name,
            edit_name);
        em.pop_undo_stack_enabled();
        stop_editing_label(ctx, entity);
    }

    ImGui::PopItemWidth();

    if(ImGui::IsItemDeactivated())
    {
        stop_editing_label(ctx, entity);
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
    }


    auto pos = ImGui::GetCursorScreenPos() + ImVec2(ImGui::GetTextLineHeightWithSpacing(), 0.0f);
    ImGui::AlignTextToFramePadding();

    auto label = get_entity_display_label(entity);
    auto col = entity_panel::get_entity_display_color(entity);

    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushStyleVarX(ImGuiStyleVar_ItemInnerSpacing, 0.0f);
    bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
    ImGui::PopStyleVar();

    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        const auto ent = entity.entity();
        const auto idx = entt::to_entity(ent);
        const auto ver = entt::to_version(ent);
        const auto id = entt::to_integral(ent);
    
        const char* provenance = entity_panel::describe_prefab_role(entity_panel::get_entity_prefab_role(entity));
        if(provenance[0] != '\0')
        {
            ImGui::SetItemTooltipEx("%s\n\nId: %d\nIndex: %d\nVersion: %d", provenance, id, idx, ver);
        }
        else
        {
            ImGui::SetItemTooltipEx("Id: %d\nIndex: %d\nVersion: %d", id, idx, ver);
        }
    }

    ImGui::PopStyleColor();

    if(em.is_focused(entity))
    {
        ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 1.0f)));
   
        if(!ImGui::IsItemVisible())
        {
            ImGui::SetScrollHereY();
        }
    
    }
    
    if(!is_editing_label())
    {
        check_drag(ctx, entity);
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
            handle_entity_selection(ctx, entity);
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

hierarchy_panel::hierarchy_panel(imgui_panels* parent, const char* name) : entity_panel(parent, name)
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

void hierarchy_panel::on_after_render(rtti::context& ctx)
{
    (void)ctx;
    update_editing();
}

auto hierarchy_panel::get_window_flags() const -> ImGuiWindowFlags
{
    return 0;
}

void hierarchy_panel::draw_ui(rtti::context& ctx)
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

    check_drag(ctx, {});
}

} // namespace unravel
