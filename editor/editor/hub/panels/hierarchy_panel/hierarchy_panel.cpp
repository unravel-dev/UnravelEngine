#include "hierarchy_panel.h"
#include "hierarchy_cells.h"
#include "../panel.h"
#include "../panel_toolbar.h"
#include "../panels_defs.h"
#include "imgui/imgui.h"
#include "imgui_widgets/tooltips.h"
#include <imgui/imgui_internal.h>

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/editing/editing_manager.h>
#include <editor/shortcuts.h>
#include <editor/events.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector_entity.h>

#include <engine/assets/impl/asset_extensions.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <editor/editing/authoring_root.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/scripting/ecs/components/script_component.h>

#include <filesystem/filesystem.h>
#include <hpp/utility.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace unravel
{

namespace
{

// ============================================================================
// Layout
// ============================================================================

enum class hierarchy_column : int
{
    active,
    name,
    static_flag,
    layer,
    tag,
    count
};

/// A column past the name, shown only while the panel has room for it.
struct hierarchy_value_column
{
    hierarchy_column column;
    const char* label;
    /// In units of the font size.
    float width;
};

// Widths in units of the font size, so the table follows the UI scale of the editor.
constexpr float HIERARCHY_TABLE_NAME_MIN_WIDTH = 12.0f;
constexpr float HIERARCHY_TABLE_CELL_PADDING_X = 0.25f;
// Room on either side of the tree arrow.
constexpr float HIERARCHY_TABLE_ARROW_PADDING_X = 0.2f;
// Left to right, in Unity's order.
constexpr std::array<hierarchy_value_column, 3> HIERARCHY_VALUE_COLUMNS{{
    {hierarchy_column::static_flag, "Static", 3.2f},
    {hierarchy_column::layer, "Layer", 5.0f},
    {hierarchy_column::tag, "Tag", 6.0f},
}};
// The order the value columns give way in when the panel narrows: the layer stays longest.
constexpr std::array<hierarchy_column, 3> HIERARCHY_COLUMN_PRIORITY{hierarchy_column::layer,
                                                                    hierarchy_column::tag,
                                                                    hierarchy_column::static_flag};
constexpr ImGuiTableFlags HIERARCHY_TABLE_FLAGS = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
                                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody;

constexpr float HIERARCHY_SEARCH_FIELD_MIN_WIDTH = 60.0f;
constexpr float HIERARCHY_SEARCH_FIELD_MAX_WIDTH = 100000.0f;
// The row under the pointer, in the wash the console uses.
constexpr float HIERARCHY_ROW_HOVERED_ALPHA = 0.45f;
// The scene heads the tree on a band of its own.
constexpr ImU32 HIERARCHY_SCENE_ROW_COLOR = IM_COL32(0, 0, 0, 36);
// While searching, the entities that only lead to a match stay in the tree, dimmed.
constexpr float HIERARCHY_UNMATCHED_ALPHA = 0.45f;
constexpr float HIERARCHY_HIGHLIGHT_ALPHA = 0.45f;
constexpr float HIERARCHY_HIGHLIGHT_ROUNDING = 0.2f;
constexpr float HIERARCHY_CLEAR_ICON_ALPHA = 0.6f;
constexpr ImVec4 HIERARCHY_FOCUS_FRAME_COLOR{1.0f, 1.0f, 0.0f, 1.0f};

// A search term after this prefix matches component names only, as in Unity ("t:Light").
constexpr const char* HIERARCHY_COMPONENT_TERM_PREFIX = "t:";
constexpr const char* HIERARCHY_SEARCH_TOOLTIP = "Finds entities by name and by component.\n"
                                                 "t:Light finds components only, -word leaves names out,\n"
                                                 "and commas separate terms.";
// An entity found by a component carries a chip with it at the end of its name cell: green, apart
// from the accent a matching name is marked with and the accent of the selection.
constexpr ImU32 HIERARCHY_CHIP_TEXT_COLOR = IM_COL32(140, 215, 155, 255);
constexpr ImU32 HIERARCHY_CHIP_FILL_COLOR = IM_COL32(140, 215, 155, 36);
constexpr float HIERARCHY_CHIP_PADDING_X = 0.35f;
constexpr float HIERARCHY_CHIP_INSET_Y = 0.2f;
constexpr float HIERARCHY_CHIP_ROUNDING = 0.25f;
constexpr float HIERARCHY_CHIP_GAP = 0.3f;
// Past this share of the name cell the chip keeps only its icon.
constexpr float HIERARCHY_CHIP_MAX_SHARE = 0.5f;

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

/// Takes what is dropped on the entity, or on the scene itself for a null entity. Call between
/// a successful BeginDragDropTarget*() and EndDragDropTarget().
void accept_drop_payloads(rtti::context& ctx, entt::handle entity)
{
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
}

void process_drag_drop_target(rtti::context& ctx, entt::handle entity)
{
    if(!ImGui::BeginDragDropTarget())
    {
        return;
    }
    accept_drop_payloads(ctx, entity);
    ImGui::EndDragDropTarget();
}

/// The whole table takes drops for the scene. The rows are smaller targets, so they win over it.
void process_scene_drop_target(rtti::context& ctx, const ImRect& rect)
{
    if(!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##scene_drop_target")))
    {
        return;
    }
    accept_drop_payloads(ctx, {});
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
            {"Sphere", probe_type::sphere},
            {"Box", probe_type::box}};
        
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

/// Opens the prefab of the instance the entity belongs to.
void open_prefab_of(rtti::context& ctx, entt::handle entity)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.queue_action("Open Prefab",
    [&ctx, entity]() mutable
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

/// The prefab can be opened from the instance root. Not from the prefab being edited: it would
/// open itself.
auto is_prefab_instance_root(entt::handle entity) -> bool
{
    return entity.all_of<prefab_component>() && !is_authoring_root(entity);
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
                    open_prefab_of(ctx, entity);
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
    // Rows and column headers have menus of their own.
    if(ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
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

/// What the rows of one frame share.
struct hierarchy_draw_context
{
    rtti::context& ctx;
    imgui_panels* panels{};
    const hierarchy_search& search;
    /// The panel has the keyboard focus: the selection wears the accent then, grey otherwise.
    bool is_focused{};
};

auto calc_hierarchy_table_pixels(float font_units) -> float
{
    return ImFloor(ImGui::GetFontSize() * font_units);
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

auto get_entity_tree_node_flags(bool is_selected, bool has_children) -> ImGuiTreeNodeFlags
{
    // The node spans the row, so a click anywhere on it selects. The value cells are laid over it.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_AllowOverlap |
                               ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DrawLinesToNodes;

    if(is_selected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if(!has_children)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    return flags;
}

auto get_entity_display_text(entt::handle entity) -> std::string
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

    return icon + name + badge;
}

/// The node draws no label: the row draws it, so that it can give way to the prefab button.
auto make_entity_tree_node_id(entt::handle entity) -> std::string
{
    return "###" + std::to_string(entt::to_integral(entity.entity()));
}

void set_entity_tooltip(entt::handle entity)
{
    if(!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        return;
    }
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

/// The rename field, over the label: its text starts where the label's does.
void draw_entity_name_editor(rtti::context& ctx, entt::handle entity, const ImVec2& pos)
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

auto has_shown_children(const hierarchy_search& search, const transform_component& transform) -> bool
{
    const auto& children = transform.get_children();
    return std::any_of(children.begin(),
                       children.end(),
                       [&](const entt::handle& child)
                       {
                           return child && search.is_shown(child.entity());
                       });
}

/// Call after TableNextRow(). The cells ask before the name cell has told who is hovered, and
/// they take the hover from the row: the row is hovered wherever the pointer is on it.
auto is_hierarchy_row_hovered() -> bool
{
    const ImGuiTable* table = ImGui::GetCurrentTable();
    const ImVec2 row_min(table->WorkRect.Min.x, table->RowPosY1);
    const ImVec2 row_max(table->WorkRect.Max.x, table->RowPosY1 + hierarchy_cells::get_row_height());
    return ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
           ImGui::IsMouseHoveringRect(row_min, row_max, false);
}

/// The node draws the selection. The hover is drawn for the whole row instead, since the value
/// cells take it from the node.
constexpr int HIERARCHY_ROW_STYLE_COLORS = 2;
void push_entity_row_colors(bool is_selected, bool is_focused)
{
    const ImVec4 selected_color = is_focused ? imgui_style::get_accent_color() : ImGui::GetStyleColorVec4(ImGuiCol_Header);
    ImGui::PushStyleColor(ImGuiCol_Header, selected_color);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, is_selected ? selected_color : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
}

/// Where the label of a tree node starts: past its arrow.
auto calc_tree_label_x(float cell_min_x) -> float
{
    return cell_min_x + ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
}

auto calc_row_text_y(float row_min_y) -> float
{
    return ImFloor(row_min_y + (hierarchy_cells::get_row_height() - ImGui::GetFontSize()) * 0.5f);
}

/// Marks the first place the search term occurs in the text.
void draw_search_highlight(const ImVec2& text_pos, const std::string& text, const std::string& term, float max_x)
{
    if(term.empty())
    {
        return;
    }
    const char* text_begin = text.c_str();
    const char* match = ImStristr(text_begin, text_begin + text.size(), term.c_str(), term.c_str() + term.size());
    if(match == nullptr)
    {
        return;
    }
    const float match_min_x = text_pos.x + ImGui::CalcTextSize(text_begin, match).x;
    const float match_max_x = ImMin(match_min_x + ImGui::CalcTextSize(match, match + term.size()).x, max_x);
    if(match_max_x <= match_min_x)
    {
        return;
    }
    ImVec4 color = imgui_style::get_accent_color();
    color.w = HIERARCHY_HIGHLIGHT_ALPHA;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(match_min_x, text_pos.y),
                                              ImVec2(match_max_x, text_pos.y + ImGui::GetFontSize()),
                                              ImGui::GetColorU32(color),
                                              calc_hierarchy_table_pixels(HIERARCHY_HIGHLIGHT_ROUNDING));
}

//-----------------------------------------------------------------------------
/// <summary>
/// The chip of the component a search found the entity by: its icon and name, right aligned at
/// max_x. With little room it keeps only the icon.
/// </summary>
/// <returns>Where the chip starts, or max_x when there is no room for it</returns>
//-----------------------------------------------------------------------------
auto draw_component_match_chip(const hierarchy_component_match& match, float label_x, float row_min_y, float max_x)
    -> float
{
    const std::string full_text = match.icon.empty() ? match.name : match.icon + " " + match.name;
    const float padding = calc_hierarchy_table_pixels(HIERARCHY_CHIP_PADDING_X);
    const float max_width = (max_x - label_x) * HIERARCHY_CHIP_MAX_SHARE;
    const bool is_full_fitting = ImGui::CalcTextSize(full_text.c_str()).x + 2.0f * padding <= max_width;
    const std::string text = is_full_fitting || match.icon.empty() ? full_text : match.icon;
    const float width = ImGui::CalcTextSize(text.c_str()).x + 2.0f * padding;
    if(width > max_width)
    {
        return max_x;
    }
    const float inset = calc_hierarchy_table_pixels(HIERARCHY_CHIP_INSET_Y);
    const ImRect chip(max_x - width, row_min_y + inset, max_x, row_min_y + hierarchy_cells::get_row_height() - inset);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(chip.Min, chip.Max, HIERARCHY_CHIP_FILL_COLOR, calc_hierarchy_table_pixels(HIERARCHY_CHIP_ROUNDING));
    draw_list->AddText(ImVec2(chip.Min.x + padding, calc_row_text_y(row_min_y)), HIERARCHY_CHIP_TEXT_COLOR, text.c_str());
    return chip.Min.x;
}

/// Icon, name and badge after the arrow, cut with an ellipsis at max_x. While searching, the name
/// that matches is marked, and a match by component gets its chip.
void draw_entity_label(const hierarchy_draw_context& draw_ctx, entt::handle entity, const ImVec2& cell_min, float max_x)
{
    const std::string text = get_entity_display_text(entity);
    const bool is_match = draw_ctx.search.is_match(entity.entity());
    ImVec4 color = entity_panel::get_entity_display_color(entity);
    if(!is_match)
    {
        color.w *= HIERARCHY_UNMATCHED_ALPHA;
    }
    const ImVec2 text_pos(calc_tree_label_x(cell_min.x), calc_row_text_y(cell_min.y));
    if(const hierarchy_component_match* component_match = draw_ctx.search.find_component_match(entity.entity()))
    {
        const float chip_min_x = draw_component_match_chip(*component_match, text_pos.x, cell_min.y, max_x);
        if(chip_min_x < max_x)
        {
            max_x = chip_min_x - calc_hierarchy_table_pixels(HIERARCHY_CHIP_GAP);
        }
    }
    if(draw_ctx.search.is_name_match(entity.entity()))
    {
        draw_search_highlight(text_pos, text, draw_ctx.search.highlight, max_x);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),
                              text_pos,
                              ImVec2(max_x, cell_min.y + hierarchy_cells::get_row_height()),
                              max_x,
                              text.c_str(),
                              nullptr,
                              nullptr);
    ImGui::PopStyleColor();
}

/// Everything the row's node answers to: focus, drag and drop, menu, selection and shortcuts.
/// Call right after the node, while it is the last item.
void handle_entity_row_input(const hierarchy_draw_context& draw_ctx, entt::handle entity)
{
    rtti::context& ctx = draw_ctx.ctx;
    auto& em = ctx.get_cached<editing_manager>();
    if(em.is_focused(entity))
    {
        ImGui::SetItemFocusFrame(ImGui::GetColorU32(HIERARCHY_FOCUS_FRAME_COLOR));
        if(!ImGui::IsItemVisible())
        {
            ImGui::SetScrollHereY();
        }
    }

    if(!is_editing_label())
    {
        check_drag(ctx, entity);
        check_context_menu(ctx, draw_ctx.panels, entity);
    }

    const bool is_item_focus_changed = ImGui::IsItemFocusChanged();
    const bool is_item_released_left = ImGui::IsItemReleased(ImGuiMouseButton_Left);
    const bool is_item_clicked_middle = ImGui::IsItemClicked(ImGuiMouseButton_Middle);
    const bool is_item_double_clicked_left = ImGui::IsItemDoubleClicked(ImGuiMouseButton_Left);
    if(is_item_released_left || is_item_focus_changed)
    {
        handle_entity_selection(ctx, entity);
    }

    if(em.is_selected(entity))
    {
        handle_entity_mouse_interactions(ctx, draw_ctx.panels, entity, is_item_clicked_middle, is_item_double_clicked_left);
        handle_entity_keyboard_shortcuts(ctx, draw_ctx.panels, entity);
    }
}

/// The tree node, its label, the rename field and the prefab button. Returns whether the node is
/// open; the caller pops the tree when it is.
auto draw_entity_name_cell(const hierarchy_draw_context& draw_ctx, entt::handle entity, bool has_children) -> bool
{
    rtti::context& ctx = draw_ctx.ctx;
    auto& em = ctx.get_cached<editing_manager>();
    const ImVec2 cell_min = ImGui::GetCursorScreenPos();
    const float cell_max_x = cell_min.x + ImGui::GetContentRegionAvail().x;
    const bool is_selected = em.is_selected(entity);
    if(draw_ctx.search.is_active)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    else if(is_parent_of_focused(ctx, entity))
    {
        ImGui::SetNextItemOpen(true, 0);
    }

    push_entity_row_colors(is_selected, draw_ctx.is_focused);
    ImGui::AlignTextToFramePadding();
    const bool is_open = ImGui::TreeNodeEx(make_entity_tree_node_id(entity).c_str(),
                                           get_entity_tree_node_flags(is_selected, has_children));
    ImGui::PopStyleColor(HIERARCHY_ROW_STYLE_COLORS);
    set_entity_tooltip(entity);
    handle_entity_row_input(draw_ctx, entity);

    const bool has_open_prefab = is_prefab_instance_root(entity);
    const float label_max_x = cell_max_x - (has_open_prefab ? hierarchy_cells::get_open_prefab_button_width() : 0.0f);
    const bool is_renaming = is_selected && is_editing_label();
    if(!is_renaming)
    {
        draw_entity_label(draw_ctx, entity, cell_min, label_max_x);
    }
    const ImVec2 editor_pos(calc_tree_label_x(cell_min.x) - ImGui::GetStyle().FramePadding.x, cell_min.y);
    draw_entity_name_editor(ctx, entity, editor_pos);

    if(has_open_prefab)
    {
        const ImRect cell_rect(cell_min, ImVec2(cell_max_x, cell_min.y + hierarchy_cells::get_row_height()));
        if(hierarchy_cells::draw_open_prefab_button(cell_rect))
        {
            open_prefab_of(ctx, entity);
        }
    }
    return is_open;
}

/// The cells besides the name. They come after the node that spans the row: an item laid over
/// one that allows overlap takes the hover from it only when it is submitted later.
void draw_entity_value_cells(rtti::context& ctx, const hierarchy_cells::row_state& row)
{
    if(ImGui::TableSetColumnIndex(static_cast<int>(hierarchy_column::active)))
    {
        hierarchy_cells::draw_active_cell(ctx, row);
    }
    if(ImGui::TableSetColumnIndex(static_cast<int>(hierarchy_column::static_flag)))
    {
        hierarchy_cells::draw_static_cell(ctx, row);
    }
    if(ImGui::TableSetColumnIndex(static_cast<int>(hierarchy_column::layer)))
    {
        hierarchy_cells::draw_layer_cell(ctx, row);
    }
    if(ImGui::TableSetColumnIndex(static_cast<int>(hierarchy_column::tag)))
    {
        hierarchy_cells::draw_tag_cell(ctx, row);
    }
}

void draw_entity(const hierarchy_draw_context& draw_ctx, entt::handle entity)
{
    if(!entity || !draw_ctx.search.is_shown(entity.entity()))
    {
        return;
    }

    ImGui::PushID(static_cast<int>(entity.entity()));
    auto& trans_comp = entity.get<transform_component>();
    const bool has_children = has_shown_children(draw_ctx.search, trans_comp);

    ImGui::TableNextRow(ImGuiTableRowFlags_None, hierarchy_cells::get_row_height());
    const hierarchy_cells::row_state row{entity, is_hierarchy_row_hovered()};
    if(row.is_hovered)
    {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_HeaderHovered, HIERARCHY_ROW_HOVERED_ALPHA));
    }
    ImGui::TableSetColumnIndex(static_cast<int>(hierarchy_column::name));
    const bool is_open = draw_entity_name_cell(draw_ctx, entity, has_children);
    draw_entity_value_cells(draw_ctx.ctx, row);

    if(is_open)
    {
        for(const auto& child : trans_comp.get_children())
        {
            draw_entity(draw_ctx, child);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

/// The scene heads the tree: a drop on it moves entities to the top level, its menu creates there.
auto draw_scene_row(const hierarchy_draw_context& draw_ctx, const std::string& scene_name, const char* icon) -> bool
{
    ImGui::TableNextRow(ImGuiTableRowFlags_None, hierarchy_cells::get_row_height());
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, HIERARCHY_SCENE_ROW_COLOR);
    ImGui::TableSetColumnIndex(static_cast<int>(hierarchy_column::name));
    const ImVec2 cell_min = ImGui::GetCursorScreenPos();
    const float cell_max_x = cell_min.x + ImGui::GetContentRegionAvail().x;
    if(draw_ctx.search.is_active)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    ImGui::AlignTextToFramePadding();
    const bool is_open = ImGui::TreeNodeEx("###scene_root",
                                           ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns |
                                               ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                               ImGuiTreeNodeFlags_DrawLinesToNodes);
    process_drag_drop_target(draw_ctx.ctx, {});
    if(ImGui::BeginPopupContextItem("##scene_context_menu"))
    {
        {
            ImGui::ContextMenuStyleScope style_scope;
            draw_common_menu_items(draw_ctx.ctx, {});
        }
        ImGui::EndPopup();
    }

    const std::string text = std::string(icon) + " " + scene_name;
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),
                              ImVec2(calc_tree_label_x(cell_min.x), calc_row_text_y(cell_min.y)),
                              ImVec2(cell_max_x, cell_min.y + hierarchy_cells::get_row_height()),
                              cell_max_x,
                              text.c_str(),
                              nullptr,
                              nullptr);
    ImGui::PopFont();
    return is_open;
}

void draw_no_match_row(const char* search_text)
{
    ImGui::TableNextRow(ImGuiTableRowFlags_None, hierarchy_cells::get_row_height());
    ImGui::TableSetColumnIndex(static_cast<int>(hierarchy_column::name));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("No entity matches \"%s\"", search_text);
}

//-----------------------------------------------------------------------------
/// <summary>
/// The columns of the table. The value columns the panel has no room for are switched off: the
/// name keeps its minimum width, and the others give way in HIERARCHY_COLUMN_PRIORITY order.
/// Columns the user hid take no room.
/// </summary>
//-----------------------------------------------------------------------------
void setup_hierarchy_columns(float table_width)
{
    const ImGuiTable* table = ImGui::GetCurrentTable();
    const float active_width = hierarchy_cells::get_row_height();
    std::array<bool, static_cast<size_t>(hierarchy_column::count)> is_disabled{};
    float room = table_width - active_width - calc_hierarchy_table_pixels(HIERARCHY_TABLE_NAME_MIN_WIDTH);
    for(const hierarchy_column column : HIERARCHY_COLUMN_PRIORITY)
    {
        const auto& desc = *std::find_if(HIERARCHY_VALUE_COLUMNS.begin(),
                                         HIERARCHY_VALUE_COLUMNS.end(),
                                         [&](const hierarchy_value_column& value_column)
                                         {
                                             return value_column.column == column;
                                         });
        if(!table->Columns[static_cast<int>(column)].IsUserEnabled)
        {
            continue;
        }
        const float width = calc_hierarchy_table_pixels(desc.width);
        is_disabled[static_cast<size_t>(column)] = room < width;
        room -= is_disabled[static_cast<size_t>(column)] ? 0.0f : width;
    }

    const ImGuiTableColumnFlags fixed_flags = ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable;
    ImGui::TableSetupColumn(ICON_MDI_EYE_OUTLINE "###active",
                            fixed_flags | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoHide,
                            active_width,
                            static_cast<ImGuiID>(hierarchy_column::active));
    ImGui::TableSetupColumn("Name",
                            ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_IndentEnable,
                            0.0f,
                            static_cast<ImGuiID>(hierarchy_column::name));
    for(const hierarchy_value_column& value_column : HIERARCHY_VALUE_COLUMNS)
    {
        const bool is_column_disabled = is_disabled[static_cast<size_t>(value_column.column)];
        ImGui::TableSetupColumn(value_column.label,
                                fixed_flags | (is_column_disabled ? ImGuiTableColumnFlags_Disabled : 0),
                                calc_hierarchy_table_pixels(value_column.width),
                                static_cast<ImGuiID>(value_column.column));
    }
    ImGui::TableSetupScrollFreeze(0, 1);
}

/// The header row, one row high with the names centered on it. The eye column shows its icon.
void draw_hierarchy_header_row()
{
    const float row_height = hierarchy_cells::get_row_height();
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, row_height);
    for(int column = 0; column < ImGui::TableGetColumnCount(); ++column)
    {
        if(!ImGui::TableSetColumnIndex(column))
        {
            continue;
        }
        const ImVec2 cell_min = ImGui::GetCursorScreenPos();
        const float cell_max_x = cell_min.x + ImGui::GetContentRegionAvail().x;
        const char* name = ImGui::TableGetColumnName(column);
        const char* name_end = ImGui::FindRenderedTextEnd(name);
        ImGui::PushID(column);
        ImGui::TableHeader("##header");
        ImGui::PopID();
        const bool is_active_column = column == static_cast<int>(hierarchy_column::active);
        const float name_width = ImGui::CalcTextSize(name, name_end).x;
        const float name_x = is_active_column ? ImFloor(cell_min.x + (cell_max_x - cell_min.x - name_width) * 0.5f) : cell_min.x;
        const float text_y = calc_row_text_y(cell_min.y);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),
                                  ImVec2(name_x, text_y),
                                  ImVec2(cell_max_x, cell_min.y + row_height),
                                  cell_max_x,
                                  name,
                                  name_end,
                                  nullptr);
        ImGui::PopStyleColor();
        if(is_active_column)
        {
            ImGui::SetItemTooltipEx("%s", "Active");
        }
    }
}

/// The terms of a search, split the way ImGuiTextFilter splits them.
struct hierarchy_search_terms
{
    /// A component whose name contains one of these matches, with the component prefix stripped.
    std::vector<std::string> component_terms;
    /// An entity whose name contains one of these is left out.
    std::vector<std::string> excluded_terms;
    /// The first term that is not for components only.
    std::string highlight;
};

auto parse_search_terms(const ImGuiTextFilter& filter) -> hierarchy_search_terms
{
    hierarchy_search_terms terms;
    const size_t prefix_length = std::strlen(HIERARCHY_COMPONENT_TERM_PREFIX);
    for(const ImGuiTextFilter::ImGuiTextRange& range : filter.Filters)
    {
        if(range.empty())
        {
            continue;
        }
        std::string term(range.b, range.e);
        if(term.front() == '-')
        {
            if(term.size() > 1)
            {
                terms.excluded_terms.push_back(term.substr(1));
            }
            continue;
        }
        const bool is_component_only = ImStrnicmp(term.c_str(), HIERARCHY_COMPONENT_TERM_PREFIX, prefix_length) == 0;
        if(is_component_only)
        {
            term.erase(0, prefix_length);
        }
        else if(terms.highlight.empty())
        {
            terms.highlight = term;
        }
        if(!term.empty())
        {
            terms.component_terms.push_back(term);
        }
    }
    return terms;
}

auto contains_search_term(const std::string& text, const std::vector<std::string>& terms) -> bool
{
    return std::any_of(terms.begin(),
                       terms.end(),
                       [&](const std::string& term)
                       {
                           return ImStristr(text.c_str(), text.c_str() + text.size(), term.c_str(), term.c_str() + term.size()) != nullptr;
                       });
}

/// A component type a search looks for.
struct hierarchy_component_kind
{
    std::string name;
    std::string icon;
    /// Appends the entities of the registry that have the component.
    void (*collect_entities)(entt::registry& registry, std::vector<entt::entity>& entities);
};

/// Every entity has these: searching by them would find everything.
template<typename T>
constexpr bool is_hierarchy_core_component = std::is_same_v<T, id_component> || std::is_same_v<T, tag_component> ||
                                             std::is_same_v<T, layer_component> ||
                                             std::is_same_v<T, transform_component> ||
                                             std::is_same_v<T, prefab_id_component>;

/// The components the inspector lists, the core ones aside.
auto get_searchable_components() -> const std::vector<hierarchy_component_kind>&
{
    static const std::vector<hierarchy_component_kind> kinds = []()
    {
        std::vector<hierarchy_component_kind> result;
        hpp::for_each_tuple_type<all_inspectable_components>(
            [&](auto index)
            {
                using ctype = std::tuple_element_t<decltype(index)::value, all_inspectable_components>;
                if constexpr(!is_hierarchy_core_component<ctype>)
                {
                    const entt::meta_type type = entt::resolve<ctype>();
                    result.push_back({entt::get_pretty_name(type),
                                      get_component_icon(type),
                                      [](entt::registry& registry, std::vector<entt::entity>& entities)
                                      {
                                          const auto view = registry.view<ctype>();
                                          entities.insert(entities.end(), view.begin(), view.end());
                                      }});
                }
            });
        return result;
    }();
    return kinds;
}

/// C# scripts are components to the user: they are searched by their class names.
auto find_matching_script_name(const script_component& scripts, const std::vector<std::string>& terms) -> std::string
{
    for(const auto& script : scripts.get_script_components())
    {
        if(!script.pinned)
        {
            continue;
        }
        const auto& object = script.pinned->get_object();
        if(!object.valid())
        {
            continue;
        }
        const auto& type = object.get_type();
        if(!type.valid())
        {
            continue;
        }
        std::string name = type.get_name();
        if(contains_search_term(name, terms))
        {
            return name;
        }
    }
    return {};
}

/// The entities of the registry with a component whose name matches, and the first such component
/// of each. An entity whose name holds an excluded term is left out.
void collect_component_matches(entt::registry& registry,
                               const hierarchy_search_terms& terms,
                               std::unordered_map<entt::entity, hierarchy_component_match>& matches)
{
    if(terms.component_terms.empty())
    {
        return;
    }
    const auto is_excluded = [&](entt::entity entity)
    {
        return !terms.excluded_terms.empty() &&
               contains_search_term(entity_panel::get_entity_name(entt::handle(registry, entity)), terms.excluded_terms);
    };
    std::vector<entt::entity> entities;
    for(const hierarchy_component_kind& kind : get_searchable_components())
    {
        if(!contains_search_term(kind.name, terms.component_terms))
        {
            continue;
        }
        entities.clear();
        kind.collect_entities(registry, entities);
        for(const entt::entity entity : entities)
        {
            if(!is_excluded(entity))
            {
                matches.try_emplace(entity, hierarchy_component_match{kind.name, kind.icon});
            }
        }
    }
    registry.view<script_component>().each(
        [&](entt::entity entity, const script_component& scripts)
        {
            const std::string script_name = find_matching_script_name(scripts, terms.component_terms);
            if(!script_name.empty() && !is_excluded(entity))
            {
                matches.try_emplace(entity, hierarchy_component_match{script_name, ICON_MDI_LANGUAGE_CSHARP});
            }
        });
}

/// A cross over the right end of the search field just submitted, which empties it.
void draw_search_clear_button(ImGuiTextFilter& filter)
{
    const ImRect field(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImRect bb(field.Max.x - field.GetHeight(), field.Min.y, field.Max.x, field.Max.y);
    const ImGuiID id = ImGui::GetID("##clear_search");
    if(!ImGui::ItemAdd(bb, id))
    {
        return;
    }
    bool is_hovered = false;
    bool is_held = false;
    if(ImGui::ButtonBehavior(bb, id, &is_hovered, &is_held))
    {
        filter.Clear();
    }
    const ImVec2 icon_size = ImGui::CalcTextSize(ICON_MDI_CLOSE);
    ImGui::GetWindowDrawList()->AddText(ImFloor(bb.GetCenter() - icon_size * 0.5f),
                                        ImGui::GetColorU32(ImGuiCol_Text, is_hovered ? 1.0f : HIERARCHY_CLEAR_ICON_ALPHA),
                                        ICON_MDI_CLOSE);
    ImGui::SetItemTooltipEx("%s", "Clear the search");
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

void hierarchy_panel::draw_toolbar(rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();
    if(panel_toolbar::begin_strip("##hierarchy_toolbar"))
    {
        if(em.is_prefab_mode())
        {
            if(panel_toolbar::button("##back_to_scene", ICON_MDI_ARROW_LEFT " Scene", "Save the prefab and go back to the scene", true))
            {
                em.exit_prefab_mode(ctx, editing_manager::save_option::yes);
            }
            panel_toolbar::separator();
        }
        draw_create_dropdown(ctx);
        draw_search_field();
    }
    panel_toolbar::end_strip();
}

void hierarchy_panel::draw_create_dropdown(rtti::context& ctx)
{
    if(!panel_toolbar::begin_dropdown("##create", ICON_MDI_PLUS, "Create an entity at the top level of the scene"))
    {
        return;
    }
    {
        ImGui::ContextMenuStyleScope style_scope;
        draw_common_menu_items(ctx, {});
    }
    panel_toolbar::end_dropdown();
}

void hierarchy_panel::draw_search_field()
{
    const float width = panel_toolbar::calc_flexible_width(HIERARCHY_SEARCH_FIELD_MIN_WIDTH, HIERARCHY_SEARCH_FIELD_MAX_WIDTH);
    panel_toolbar::begin_field(width);
    // The clear button is laid over the right end of the field.
    ImGui::SetNextItemAllowOverlap();
    ImGui::DrawFilterWithHint(filter_, ICON_MDI_MAGNIFY " Search...", width);
    ImGui::SetItemTooltipEx("%s", HIERARCHY_SEARCH_TOOLTIP);
    ImGui::DrawItemActivityOutline();
    if(filter_.IsActive())
    {
        draw_search_clear_button(filter_);
    }
    panel_toolbar::end_field();
}

void hierarchy_panel::update_search(scene& target_scene)
{
    search_.name_matches.clear();
    search_.component_matches.clear();
    search_.shown.clear();
    search_.is_active = filter_.IsActive();
    const hierarchy_search_terms terms = parse_search_terms(filter_);
    search_.highlight = terms.highlight;
    if(!search_.is_active)
    {
        return;
    }
    entt::registry& registry = *target_scene.registry;
    // A component-only term is in no name, so the filter matches names by the other terms alone.
    registry.view<transform_component>().each(
        [&](entt::entity entity, transform_component& transform)
        {
            if(filter_.PassFilter(entity_panel::get_entity_name(entt::handle(registry, entity)).c_str()))
            {
                search_.name_matches.insert(entity);
            }
        });
    collect_component_matches(registry, terms, search_.component_matches);
    for(const entt::entity entity : search_.name_matches)
    {
        add_search_path(registry, entity);
    }
    for(const auto& [entity, match] : search_.component_matches)
    {
        add_search_path(registry, entity);
    }
}

void hierarchy_panel::add_search_path(entt::registry& registry, entt::entity entity)
{
    // Every entity on the way up is shown too. The walk ends at one already shown: the entities
    // above it are as well.
    entt::handle current(registry, entity);
    while(current && search_.shown.insert(current.entity()).second)
    {
        const auto* transform = current.try_get<transform_component>();
        current = transform != nullptr ? transform->get_parent() : entt::handle{};
    }
}

void hierarchy_panel::draw_scene_table(rtti::context& ctx, scene& target_scene)
{
    const float table_width = ImGui::GetContentRegionAvail().x;
    const bool is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    // Each table row latches the cell padding: it stays pushed until the table ends.
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(calc_hierarchy_table_pixels(HIERARCHY_TABLE_CELL_PADDING_X), 0.0f));
    if(ImGui::BeginTable("##hierarchy_table", static_cast<int>(hierarchy_column::count), HIERARCHY_TABLE_FLAGS))
    {
        setup_hierarchy_columns(table_width);
        draw_hierarchy_header_row();
        draw_scene_rows(ctx, target_scene, is_focused);
        // After the rows, so that the menu knows whether one of them is under the pointer.
        draw_window_context_menu(ctx, parent_);
        handle_window_empty_click(ctx);
        const ImRect table_rect = ImGui::GetCurrentTable()->OuterRect;
        ImGui::EndTable();
        process_scene_drop_target(ctx, table_rect);
    }
    ImGui::PopStyleVar();
}

void hierarchy_panel::draw_scene_rows(rtti::context& ctx, scene& target_scene, bool is_focused)
{
    auto& em = ctx.get_cached<editing_manager>();
    const hierarchy_draw_context draw_ctx{ctx, parent_, search_, is_focused};
    const std::string scene_name = get_scene_display_name(em, &target_scene);
    // The frame padding sets the height of the tree nodes, one row, and the room around the arrow.
    const float frame_padding_y = ImFloor((hierarchy_cells::get_row_height() - ImGui::GetFontSize()) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(calc_hierarchy_table_pixels(HIERARCHY_TABLE_ARROW_PADDING_X), frame_padding_y));
    // A search unfolds its results on tree state of its own, so the folding of the plain tree is
    // as it was once the search is cleared.
    ImGui::PushID(search_.is_active ? "##search_tree" : "##tree");
    const char* scene_icon = em.is_prefab_mode() ? ICON_MDI_CUBE : ICON_MDI_MOVIE_OPEN_OUTLINE;
    if(draw_scene_row(draw_ctx, scene_name, scene_icon))
    {
        if(is_roots_order_changed())
        {
            target_scene.registry->sort<root_component>(
                [](auto const& lhs, auto const& rhs)
                {
                    // Return true if lhs should come before rhs
                    return lhs.order < rhs.order;
                });

            reset_roots_order_changed();
        }

        // lead by root_component, so that the order is determined by it.
        target_scene.registry->view<root_component, transform_component>().each(
            [&](auto e, auto&& root, auto&& comp)
            {
                draw_entity(draw_ctx, comp.get_owner());
            });

        if(search_.is_active && search_.is_empty())
        {
            draw_no_match_row(filter_.InputBuf);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
    ImGui::PopStyleVar();
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
    // The table scrolls on its own, under a toolbar that stays.
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void hierarchy_panel::draw_ui(rtti::context& ctx)
{
    draw_toolbar(ctx);

    auto& em = ctx.get_cached<editing_manager>();
    scene* target_scene = em.get_active_scene(ctx);
    if(target_scene == nullptr)
    {
        return;
    }
    update_search(*target_scene);
    draw_scene_table(ctx, *target_scene);
}

} // namespace unravel
