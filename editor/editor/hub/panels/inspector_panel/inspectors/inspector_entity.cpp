#include "inspector_entity.h"
#include "entt/meta/meta.hpp"
#include "inspector.h"
#include "inspectors.h"
#include "reflection/reflection.h"

#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <editor/editing/editing_manager.h>
#include <editor/hub/panels/entity_panel.h>
#include <editor/imgui/imgui_interface.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/rendering/font.h>
#include <engine/scripting/ecs/systems/script_system.h>

#include <hpp/type_name.hpp>
#include <hpp/utility.hpp>
#include <hpp/finally.hpp>
#include <string_utils/utils.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <map>
namespace unravel
{

namespace
{
// Meta attribute holding a component's menu category; nested levels use the separator, e.g. "Physics/Colliders".
constexpr const char* COMPONENT_MENU_CATEGORY_ATTRIBUTE = "category";
constexpr const char* COMPONENT_MENU_CATEGORY_SEPARATOR = "/";
// C# script components carry no meta attribute; they are listed under this category.
constexpr const char* SCRIPT_COMPONENT_MENU_CATEGORY = "Scripting";
// Menu metrics: sizes in font-size units scale with the UI font; paddings are pixels like the rest of the style.
constexpr float COMPONENT_MENU_WIDTH_EM = 20.0f;
constexpr float COMPONENT_MENU_LIST_HEIGHT_EM = 20.0f;
constexpr float COMPONENT_MENU_ROW_PADDING_EM = 0.35f;
constexpr float COMPONENT_MENU_ICON_COLUMN_EM = 1.7f;
constexpr float COMPONENT_MENU_ROW_GAP = 2.0f;
constexpr float COMPONENT_MENU_ROUNDING = 4.0f;
constexpr float COMPONENT_MENU_SEARCH_PADDING_X = 8.0f;
constexpr float COMPONENT_MENU_SEARCH_PADDING_Y = 6.0f;
// Secondary elements (folder icons, chevrons, category hints) use the text color at this alpha.
constexpr float COMPONENT_MENU_MUTED_ALPHA = 0.6f;
// Drop-target frame drawn around the component list and the "Add Component" button while a script file is dragged.
constexpr float SCRIPT_DROP_FRAME_THICKNESS = 2.0f;
constexpr ImVec4 DRAG_DROP_TARGET_COLOR{1.0f, 1.0f, 0.0f, 1.0f};

template<typename T>
auto get_component_icon() -> std::string
{
    // Core components
    if constexpr(std::is_same<T, id_component>::value)
    {
        return ICON_MDI_IDENTIFIER;
    }
    else if constexpr(std::is_same<T, tag_component>::value)
    {
        return ICON_MDI_TAG;
    }
    else if constexpr(std::is_same<T, layer_component>::value)
    {
        return ICON_MDI_LAYERS;
    }
    else if constexpr(std::is_same<T, prefab_component>::value)
    {
        return ICON_MDI_CUBE;
    }
    else if constexpr(std::is_same<T, prefab_id_component>::value)
    {
        return ICON_MDI_CUBE_OUTLINE;
    }
    // Transform
    else if constexpr(std::is_same<T, transform_component>::value)
    {
        return ICON_MDI_AXIS_ARROW;
    }
    // Test/Debug
    else if constexpr(std::is_same<T, test_component>::value)
    {
        return ICON_MDI_BUG;
    }
    // Rendering components
    else if constexpr(std::is_same<T, model_component>::value)
    {
        return ICON_MDI_SHAPE;
    }
    else if constexpr(std::is_same<T, submesh_component>::value)
    {
        return ICON_MDI_SHAPE_OUTLINE;
    }
    else if constexpr(std::is_same<T, camera_component>::value)
    {
        return ICON_MDI_CAMERA;
    }
    else if constexpr(std::is_same<T, text_component>::value)
    {
        return ICON_MDI_TEXT;
    }
    // Animation
    else if constexpr(std::is_same<T, animation_component>::value)
    {
        return ICON_MDI_ANIMATION;
    }
    else if constexpr(std::is_same<T, bone_component>::value)
    {
        return ICON_MDI_BONE;
    }
    // Lighting
    else if constexpr(std::is_same<T, light_component>::value)
    {
        return ICON_MDI_LIGHTBULB;
    }
    else if constexpr(std::is_same<T, skylight_component>::value)
    {
        return ICON_MDI_WEATHER_SUNNY;
    }
    else if constexpr(std::is_same<T, reflection_probe_component>::value)
    {
        return ICON_MDI_REFLECT_HORIZONTAL;
    }
    // Physics
    else if constexpr(std::is_same<T, physics_component>::value)
    {
        return ICON_MDI_ATOM;
    }
    // Audio
    else if constexpr(std::is_same<T, audio_source_component>::value)
    {
        return ICON_MDI_VOLUME_HIGH;
    }
    else if constexpr(std::is_same<T, audio_listener_component>::value)
    {
        return ICON_MDI_EAR_HEARING;
    }
    // Scripting
    else if constexpr(std::is_same<T, script_component>::value)
    {
        return ICON_MDI_LANGUAGE_CSHARP;
    }
    // Post-processing effects (using similar icons for consistency)
    else if constexpr(std::is_same<T, auto_exposure_component>::value)
    {
        return ICON_MDI_BRIGHTNESS_AUTO;
    }
    else if constexpr(std::is_same<T, bloom_component>::value)
    {
        return ICON_MDI_WHITE_BALANCE_SUNNY;
    }
    else if constexpr(std::is_same<T, tonemapping_component>::value)
    {
        return ICON_MDI_BRIGHTNESS_5;
    }
    else if constexpr(std::is_same<T, fxaa_component>::value)
    {
        return ICON_MDI_FILTER;
    }
    else if constexpr(std::is_same<T, assao_component>::value)
    {
        return ICON_MDI_FILTER_OUTLINE;
    }
    else if constexpr(std::is_same<T, gtao_component>::value)
    {
        return ICON_MDI_CIRCLE_HALF_FULL;
    }
    else if constexpr(std::is_same<T, ssr_component>::value)
    {
        return ICON_MDI_MIRROR;
    }
    else if constexpr(std::is_same<T, volume_component>::value)
    {
        return ICON_MDI_VIEW_AGENDA;
    }
    else
    {
        // Default fallback icon
        return ICON_MDI_CUBE_OUTLINE;
    }
}

struct inspect_callbacks
{
    std::function<inspect_result()> on_inspect;
    std::function<void()> on_add;
    std::function<void()> on_remove;
    std::function<bool()> can_remove;
    std::function<bool()> can_merge;

    std::string icon;
};

auto inspect_component(const std::string& name, const inspect_callbacks& callbacks) -> inspect_result
{
    inspect_result result{};

    bool opened = true;

    ImGui::PushID(name.c_str());

    auto popup_str = "COMPONENT_SETTING";

    bool open_popup = false;
    bool open = true;
    if(!callbacks.can_merge())
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);

        auto pos = ImGui::GetCursorPos();
        auto col_header = ImGui::GetColorU32(ImGuiCol_Header);
        auto col_header_hovered = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
        auto col_header_active = ImGui::GetColorU32(ImGuiCol_HeaderActive);

        auto col_framebg = ImGui::GetColorU32(ImGuiCol_FrameBg);
        auto col_framebg_hovered = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
        auto col_framebg_active = ImGui::GetColorU32(ImGuiCol_FrameBgActive);

        ImGui::PushStyleColor(ImGuiCol_Header, col_framebg);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, col_framebg_hovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, col_framebg_active);

        ImGui::PushFont(ImGui::Font::SemiBold);
        open = ImGui::CollapsingHeader(fmt::format("     {}", name).c_str(), nullptr, ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::PopFont();

        ImGui::OpenPopupOnItemClick(popup_str);
        ImGui::PopStyleColor(3);

        ImGui::SetCursorPos(pos);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("       %s", callbacks.icon.c_str());

        ImGui::SameLine();
        auto settings_size = ImGui::CalcTextSize(ICON_MDI_COG).x + ImGui::GetStyle().FramePadding.x * 2.0f;

        auto avail = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().FramePadding.x;
        ImGui::AlignedItem(1.0f,
                           avail,
                           settings_size,
                           [&]()
                           {
                               if(ImGui::Button(ICON_MDI_COG))
                               {
                                   open_popup = true;
                               }
                           });
    }

    if(open)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
        ImGui::TreePush(name.c_str());

        result |= callbacks.on_inspect();

        ImGui::TreePop();
        ImGui::PopStyleVar();
    }
    if(open_popup)
    {
        ImGui::OpenPopup(popup_str);
    }

    bool is_popup_open = ImGui::IsPopupOpen(popup_str);
    if(is_popup_open && ImGui::BeginPopup(popup_str))
    {
        {
            ImGui::ContextMenuStyleScope style_scope;

            bool removal_allowed = callbacks.can_remove();
            if(ImGui::MenuItemIcon(ICON_MDI_RESTORE, "Reset", nullptr, removal_allowed))
            {
                callbacks.on_remove();
                callbacks.on_add();

                result.changed = true;
                result.edit_finished = true;
            }

            ImGui::Separator();
            if(ImGui::MenuItemIcon(ICON_MDI_DELETE, "Remove Component", nullptr, removal_allowed))
            {
                callbacks.on_remove();
                result.changed = true;
                result.edit_finished = true;
            }
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    if(!opened)
    {
        callbacks.on_remove();
        result.changed = true;
        result.edit_finished = true;
    }

    return result;
}

struct component_menu_entry
{
    std::string name;
    std::string icon;
    /// Category path as shown to the user, e.g. "Rendering / Post Processing".
    std::string category;
    std::function<void()> on_pick;
};

/// One level of the "Add Component" menu: its sub-categories and the components listed at this level.
struct component_menu_node
{
    std::map<std::string, component_menu_node> children;
    std::vector<component_menu_entry> entries;
};

auto add_component_menu_entry(component_menu_node& root, const std::string& category, component_menu_entry entry)
    -> void
{
    component_menu_node* node = &root;
    entry.category.clear();
    for(const auto& segment : string_utils::tokenize(category, COMPONENT_MENU_CATEGORY_SEPARATOR))
    {
        node = &node->children[segment];
        entry.category += entry.category.empty() ? segment : " / " + segment;
    }
    node->entries.emplace_back(std::move(entry));
}

auto find_component_menu_node(const component_menu_node& root, const std::vector<std::string>& path)
    -> const component_menu_node*
{
    const component_menu_node* node = &root;
    for(const auto& segment : path)
    {
        auto it = node->children.find(segment);
        if(it == node->children.end())
        {
            return nullptr;
        }
        node = &it->second;
    }
    return node;
}

auto sort_component_menu_entries(component_menu_node& node) -> void
{
    std::sort(node.entries.begin(),
              node.entries.end(),
              [](const component_menu_entry& lhs, const component_menu_entry& rhs)
              {
                  return lhs.name < rhs.name;
              });
    for(auto& [name, child] : node.children)
    {
        sort_component_menu_entries(child);
    }
}

auto build_component_menu(rtti::context& ctx, entt::handle& data, inspect_result& result) -> component_menu_node
{
    component_menu_node root;
    const auto& scr = ctx.get_cached<script_system>();
    for(const auto& type : scr.get_all_scriptable_components())
    {
        component_menu_entry entry;
        entry.name = type.get_fullname();
        entry.icon = ICON_MDI_LANGUAGE_CSHARP;
        entry.on_pick = [&ctx, &data, &result, name = entry.name]()
        {
            auto& em = ctx.get_cached<editing_manager>();
            em.do_action<entity_add_script_component_action_t>({}, data, name);
            result.changed |= true;
            result.edit_finished |= true;
        };
        add_component_menu_entry(root, SCRIPT_COMPONENT_MENU_CATEGORY, std::move(entry));
    }
    hpp::for_each_tuple_type<all_addable_components>(
        [&](auto index)
        {
            using ctype = std::tuple_element_t<decltype(index)::value, all_addable_components>;
            const auto type = entt::resolve<ctype>();
            component_menu_entry entry;
            entry.name = entt::get_pretty_name(type);
            entry.icon = get_component_icon<ctype>();
            entry.on_pick = [&ctx, &data, &result, type]()
            {
                auto& em = ctx.get_cached<editing_manager>();
                if(data.all_of<ctype>())
                {
                    em.do_action<entity_remove_component_action_t>({}, data, type);
                }
                em.do_action<entity_add_component_action_t>({}, data, type);
                result.changed |= true;
                result.edit_finished |= true;
            };
            const auto category = entt::get_attribute_as<std::string>(type, COMPONENT_MENU_CATEGORY_ATTRIBUTE);
            add_component_menu_entry(root, category, std::move(entry));
        });
    sort_component_menu_entries(root);
    return root;
}

auto get_component_menu_muted_color() -> ImU32
{
    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    color.w *= COMPONENT_MENU_MUTED_ALPHA;
    return ImGui::GetColorU32(color);
}

struct component_menu_row
{
    std::string id;
    std::string icon;
    ImU32 icon_color = 0;
    std::string label;
    /// Right-aligned muted text: a chevron for categories, the category path for search hits.
    std::string trailing;
    ImGui::Font::Enum font = ImGui::Font::Regular;
};

/// Draws one full-width row: a fixed icon column, the label, and optional trailing text, with a rounded hover highlight.
auto draw_component_menu_row(const component_menu_row& row) -> bool
{
    const auto& style = ImGui::GetStyle();
    const float font_size = ImGui::GetFontSize();
    const float row_height = font_size + COMPONENT_MENU_ROW_PADDING_EM * font_size * 2.0f;
    const float icon_column = COMPONENT_MENU_ICON_COLUMN_EM * font_size;
    // The selectable supplies the item, hover and click; its flat highlight is hidden in favor of a rounded one.
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
    const bool clicked = ImGui::Selectable(("##" + row.id).c_str(),
                                           false,
                                           ImGuiSelectableFlags_NoPadWithHalfSpacing,
                                           ImVec2(0.0f, row_height));
    ImGui::PopStyleColor(3);
    const ImVec2 row_min = ImGui::GetItemRectMin();
    const ImVec2 row_max = ImGui::GetItemRectMax();
    auto* draw_list = ImGui::GetWindowDrawList();
    if(ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        const ImGuiCol highlight = ImGui::IsItemActive() ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered;
        draw_list->AddRectFilled(row_min, row_max, ImGui::GetColorU32(highlight), COMPONENT_MENU_ROUNDING);
    }
    const float text_y = row_min.y + (row_height - font_size) * 0.5f;
    const float content_min_x = row_min.x + style.FramePadding.x;
    const float content_max_x = row_max.x - style.FramePadding.x;
    if(!row.icon.empty())
    {
        const float icon_width = ImGui::CalcTextSize(row.icon.c_str()).x;
        const ImVec2 icon_pos(content_min_x + (icon_column - icon_width) * 0.5f, text_y);
        draw_list->AddText(icon_pos, row.icon_color, row.icon.c_str());
    }
    ImGui::PushFont(row.font);
    draw_list->AddText(ImVec2(content_min_x + icon_column, text_y), ImGui::GetColorU32(ImGuiCol_Text), row.label.c_str());
    ImGui::PopFont();
    if(!row.trailing.empty())
    {
        const float trailing_width = ImGui::CalcTextSize(row.trailing.c_str()).x;
        const ImVec2 trailing_pos(content_max_x - trailing_width, text_y);
        draw_list->AddText(trailing_pos, get_component_menu_muted_color(), row.trailing.c_str());
    }
    return clicked;
}

auto draw_component_menu_separator() -> void
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

auto draw_component_menu_empty(const char* message) -> void
{
    const float avail_x = ImGui::GetContentRegionAvail().x;
    const float text_width = ImGui::CalcTextSize(message).x;
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_x - text_width) * 0.5f);
    ImGui::TextDisabled("%s", message);
}

auto draw_component_menu_search_field(ImGuiTextFilter& filter) -> void
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, COMPONENT_MENU_ROUNDING);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(COMPONENT_MENU_SEARCH_PADDING_X, COMPONENT_MENU_SEARCH_PADDING_Y));
    ImGui::DrawFilterWithHint(filter, ICON_MDI_MAGNIFY " Search components...", ImGui::GetContentRegionAvail().x);
    ImGui::DrawItemActivityOutline();
    ImGui::PopStyleVar(2);
}

auto draw_component_menu_entry(const component_menu_entry& entry, bool show_category) -> inspect_result
{
    inspect_result result{};
    component_menu_row row;
    row.id = entry.category + "/" + entry.name;
    row.icon = entry.icon;
    row.icon_color = ImGui::GetColorU32(ImGuiCol_Text);
    row.label = entry.name;
    if(show_category)
    {
        row.trailing = entry.category;
    }
    if(draw_component_menu_row(row))
    {
        entry.on_pick();
        result.changed = true;
        result.edit_finished = true;
        ImGui::CloseCurrentPopup();
    }
    return result;
}

auto draw_component_menu_category(const std::string& name) -> bool
{
    component_menu_row row;
    row.id = "category/" + name;
    row.icon = ICON_MDI_FOLDER_OUTLINE;
    row.icon_color = get_component_menu_muted_color();
    row.label = name;
    row.trailing = ICON_MDI_CHEVRON_RIGHT;
    return draw_component_menu_row(row);
}

auto draw_component_menu_back(const std::string& name) -> bool
{
    component_menu_row row;
    row.id = "back";
    row.icon = ICON_MDI_CHEVRON_LEFT;
    row.icon_color = get_component_menu_muted_color();
    row.label = name;
    row.font = ImGui::Font::SemiBold;
    return draw_component_menu_row(row);
}

/// Draws one menu level: a back row when inside a category, then sub-categories, then components.
auto draw_component_menu_level(const component_menu_node& node, std::vector<std::string>& path) -> inspect_result
{
    inspect_result result{};
    if(!path.empty())
    {
        if(draw_component_menu_back(path.back()))
        {
            path.pop_back();
        }
        draw_component_menu_separator();
    }
    for(const auto& [name, child] : node.children)
    {
        if(draw_component_menu_category(name))
        {
            path.push_back(name);
        }
    }
    if(!node.children.empty() && !node.entries.empty())
    {
        draw_component_menu_separator();
    }
    for(const auto& entry : node.entries)
    {
        result |= draw_component_menu_entry(entry, false);
    }
    return result;
}

auto count_component_menu_matches(const ImGuiTextFilter& filter, const component_menu_node& node) -> size_t
{
    size_t count = std::count_if(node.entries.begin(),
                                 node.entries.end(),
                                 [&filter](const component_menu_entry& entry)
                                 {
                                     return filter.PassFilter(entry.name.c_str());
                                 });
    for(const auto& [name, child] : node.children)
    {
        count += count_component_menu_matches(filter, child);
    }
    return count;
}

/// Flat list of every component whose name passes the filter, regardless of the opened category.
auto draw_component_menu_search(const ImGuiTextFilter& filter, const component_menu_node& node) -> inspect_result
{
    inspect_result result{};
    for(const auto& entry : node.entries)
    {
        if(filter.PassFilter(entry.name.c_str()))
        {
            result |= draw_component_menu_entry(entry, true);
        }
    }
    for(const auto& [name, child] : node.children)
    {
        result |= draw_component_menu_search(filter, child);
    }
    return result;
}

auto get_entity_pretty_name(entt::handle entity) -> const std::string&
{
    if(!entity)
    {
        static const std::string empty = "None (Entity)";
        return empty;
    }
    auto& tag = entity.get_or_emplace<tag_component>();
    return tag.name;
}

auto process_drag_drop_target(rtti::context& ctx, entt::handle& obj) -> bool
{
    if(ImGui::IsDragDropPossibleTargetForType("entity"))
    {
        ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 1.0f)));
    }

    bool result = false;

    if(ImGui::BeginDragDropTarget())
    {
        if(ImGui::IsDragDropPayloadBeingAccepted())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        else
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
        }

        {
            auto payload = ImGui::AcceptDragDropPayload("entity");
            if(payload != nullptr)
            {
                entt::handle dropped{};
                std::memcpy(&dropped, payload->Data, size_t(payload->DataSize));
                if(dropped)
                {
                    obj = dropped;
                    result = true;
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    return result;
}

/// Scriptable component types defined in a script source file: those whose ScriptSourceFile attribute
/// records the file, else those named after the file stem (the new-script template convention).
auto find_script_component_types(const script_system& scr, const fs::path& source_path) -> std::vector<dotnet::type>
{
    std::vector<dotnet::type> by_source;
    std::vector<dotnet::type> by_name;
    const std::string stem = source_path.stem().string();
    for(const auto& type : scr.get_all_scriptable_components())
    {
        const fs::path type_source = script_component::get_script_type_source_location(type);
        fs::error_code ec;
        if(!type_source.empty() && fs::equivalent(type_source, source_path, ec))
        {
            by_source.push_back(type);
        }
        else if(type.get_name() == stem)
        {
            by_name.push_back(type);
        }
    }
    return by_source.empty() ? by_name : by_source;
}

/// Attaches every script component type defined in the dropped source file, as the "Add Component" menu would.
auto add_script_components_from_source(rtti::context& ctx,
                                       entt::handle& data,
                                       const fs::path& source_path,
                                       inspect_result& result) -> void
{
    const auto& scr = ctx.get_cached<script_system>();
    const auto types = find_script_component_types(scr, source_path);
    if(types.empty())
    {
        APPLOG_WARNING("No compiled script component was found for '{}'. Check that it derives from "
                       "ScriptComponent and that the scripts compiled without errors.",
                       source_path.generic_string());
        return;
    }
    auto& em = ctx.get_cached<editing_manager>();
    for(const auto& type : types)
    {
        em.do_action<entity_add_script_component_action_t>({}, data, type.get_fullname());
    }
    result.changed |= true;
    result.edit_finished |= true;
}

auto is_script_drag_active() -> bool
{
    const auto& formats = ex::get_suported_formats<script>();
    return std::any_of(formats.begin(),
                       formats.end(),
                       [](const std::string& format)
                       {
                           return ImGui::IsDragDropPossibleTargetForType(format.c_str());
                       });
}

/// Makes the last item a drop target for script files dragged from the content browser.
/// The highlight sits just outside the item: a frame on its edge would be hidden beneath an opaque child window.
auto process_script_drop_target(rtti::context& ctx, entt::handle& data, inspect_result& result) -> void
{
    const bool is_script_drag = is_script_drag_active();
    if(is_script_drag)
    {
        ImRect frame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        frame.Expand(SCRIPT_DROP_FRAME_THICKNESS * 0.5f);
        ImGui::GetWindowDrawList()->AddRect(frame.Min,
                                            frame.Max,
                                            ImGui::GetColorU32(DRAG_DROP_TARGET_COLOR),
                                            ImGui::GetStyle().FrameRounding,
                                            0,
                                            SCRIPT_DROP_FRAME_THICKNESS);
    }
    if(!ImGui::BeginDragDropTarget())
    {
        return;
    }
    if(is_script_drag)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    for(const auto& format : ex::get_suported_formats<script>())
    {
        const auto* payload = ImGui::AcceptDragDropPayload(format.c_str());
        if(payload == nullptr)
        {
            continue;
        }
        const std::string absolute_path(reinterpret_cast<const char*>(payload->Data), std::size_t(payload->DataSize));
        add_script_components_from_source(ctx, data, fs::path(absolute_path), result);
    }
    ImGui::EndDragDropTarget();
}

auto render_entity_header(rtti::context& ctx, entt::handle data, prefab_override_context& override_ctx) -> inspect_result
{
    inspect_result result{};
    
    if(!data)
    {
        return result;
    }

    auto tag_comp = data.try_get<tag_component>();
    auto trans_comp = data.try_get<transform_component>();
    
    if(!tag_comp)
    {
        return result;
    }

    // Create a table for proper alignment
    if(ImGui::BeginTable("EntityHeader", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoClip))
    {
        ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 20.0f);
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 22.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        
        ImGui::TableNextRow();
        
        // Active checkbox column
        ImGui::TableSetColumnIndex(0);
        if(trans_comp)
        {
            bool is_active = trans_comp->is_active();
            
            // Track component type for prefab override context
            auto type = entt::resolve<transform_component>();
            auto name = entt::get_name(type);
            auto pretty_name = entt::get_pretty_name(type);
            auto prop = type.data("active"_hs);
            auto prop_name = entt::get_name(prop);
            auto prop_pretty_name = entt::get_pretty_name(prop);

            override_ctx.set_component_type(name, pretty_name);
            override_ctx.push_segment(prop_name, prop_pretty_name);

            bool old_active = is_active;
            if(ImGui::Checkbox("##active", &is_active))
            {
                result.changed = true;
                result.edit_finished = true;

                auto& em = ctx.get_cached<editing_manager>();
                em.do_action<entity_set_active_action_t>({},
                    data,
                    old_active,
                    is_active);
            }
            
            override_ctx.pop_segment();
        }
        
        auto col = entity_panel::get_entity_display_color(data);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        // Icon column
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        {
            auto icon = entity_panel::get_entity_icon(data);

            ImGui::Text("%s", icon.c_str());
        }
        // Name field column
        ImGui::TableSetColumnIndex(2);
        {
            auto type = entt::resolve<tag_component>();
            auto type_name = entt::get_name(type);
            auto pretty_name = entt::get_pretty_name(type);
            auto prop = type.data("name"_hs);
            auto prop_name = entt::get_name(prop);
            auto prop_pretty_name = entt::get_pretty_name(prop);
            
            override_ctx.set_component_type(type_name, pretty_name);
            override_ctx.push_segment(prop_name, prop_pretty_name);
                        
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::SetNextItemWidth(-1.0f);
            
            auto old_name = tag_comp->name;
            if(ImGui::InputTextWidget("##name", tag_comp->name, false))
            {
                result.changed = true;
                result.edit_finished = true;

                auto& em = ctx.get_cached<editing_manager>();      

                em.do_action<entity_set_name_action_t>({},
                    data,
                    old_name,
                    tag_comp->name);
            }
            
            ImGui::PopStyleVar();
            override_ctx.pop_segment();
        }
        ImGui::PopStyleColor();

        ImGui::EndTable();
    }
    
    // Tag field using traditional property_layout approach
    {
        auto type = entt::resolve<tag_component>();
        auto type_name = entt::get_name(type);
        auto pretty_name = entt::get_pretty_name(type);

        auto prop = type.data("tag"_hs);
        auto prop_name = entt::get_name(prop);
        auto prop_pretty_name = entt::get_pretty_name(prop);
        
        override_ctx.set_component_type(type_name, pretty_name);
        override_ctx.push_segment(prop_name, prop_pretty_name);

        property_layout layout(prop, tag_comp, true);

        var_info info;
        info.is_property = true;
        info.read_only = false;

        auto old_tag = tag_comp->tag;

        auto v_var = entt::forward_as_meta(tag_comp->tag);
        auto var_result = ::unravel::inspect_var(ctx, v_var, make_proxy(v_var), info);

        if(var_result.changed)
        {
            auto& em = ctx.get_cached<editing_manager>();      

            em.do_action<entity_set_tag_action_t>({},
                data,
                old_tag,
                tag_comp->tag);

        }

        result |= var_result;

        override_ctx.pop_segment();
    }
    
    return result;
}

} // namespace

auto inspector_entity::inspect_as_property(rtti::context& ctx, entt::handle& data) -> inspect_result
{
    auto name = get_entity_pretty_name(data);

    inspect_result result;

    if(ImGui::Button(ICON_MDI_DELETE, ImVec2(0.0f, ImGui::GetFrameHeight())))
    {
        if(data)
        {
            data = {};
            result.changed = true;
            result.edit_finished = true;
        }
    }

    ImGui::SameLine();
    auto id = fmt::format("{} {}", ICON_MDI_CUBE, name);
    if(ImGui::Button(id.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight())))
    {
        auto& em = ctx.get_cached<editing_manager>();

        em.focus(data);
    }

    ImGui::SetItemTooltipEx("%s", id.c_str());

    bool drag_dropped = process_drag_drop_target(ctx, data);
    result.changed |= drag_dropped;
    result.edit_finished |= drag_dropped;

    return result;
}

auto inspector_entity::inspect(rtti::context& ctx,
                               entt::meta_any& var,
                               const meta_any_proxy& var_proxy,
                               const var_info& info,
                               const entt::meta_custom& custom) -> inspect_result
{
    inspect_result result{};
    auto data = var.cast<entt::handle>();

    if(data)
    {
        auto& inspector_ctx = ctx.get_cached<inspector_context>();
        inspector_ctx.inspected_registry = data.registry();
    }

    auto cleanup = hpp::finally([&]()
    {
        auto& inspector_ctx = ctx.get_cached<inspector_context>();
        inspector_ctx.inspected_registry = nullptr;
    });

    if(info.is_property)
    {
        result = inspect_as_property(ctx, data);
    }
    else
    {
        if(!data)
        {
            return result;
        }

        auto& override_ctx = ctx.get_cached<prefab_override_context>();

        // Render Unity-style entity header (active checkbox, icon, name, tag)
        result |= render_entity_header(ctx, data, override_ctx);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();


        // Keep "Add Component" pinned below the scrollable component list.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float add_component_footer_height =
            style.ItemSpacing.y * 4.0f + style.WindowPadding.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.WindowRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.WindowPadding);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        ImGui::BeginChild("ENTITY_COMPONENTS",
                          ImVec2(0.0f, -add_component_footer_height),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        if(is_debug_view())
        {
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
            ImGui::TreePush("Entity");
            {
                property_layout layout("Entity");
                const auto ent = data.entity();
                const auto idx = entt::to_entity(ent);
                const auto ver = entt::to_version(ent);
                const auto id = entt::to_integral(ent);
    
                //ImGui::SetItemTooltipEx("Id: %d\nIndex: %d\nVersion: %d", id, idx, ver);
                ImGui::Text("Id: %u, Index: %u, Version: %u", id, idx, ver);
            }
            
            ImGui::TreePop();
            ImGui::PopStyleVar();
        }

        hpp::for_each_tuple_type<all_inspectable_components>(
            [&](auto index)
            {
                using ctype = std::tuple_element_t<decltype(index)::value, all_inspectable_components>;
                auto component = data.try_get<ctype>();

                if(!component)
                {
                    return;
                }
                
                // Skip tag_component as it's handled in the Unity-style header
                if constexpr(std::is_same_v<ctype, tag_component>)
                {
                    return;
                }

                auto type = entt::resolve<ctype>();
                auto name = entt::get_name(type);
                auto pretty_name = entt::get_pretty_name(type);
                
                // Track component type for prefab override context
                override_ctx.set_component_type(std::string(name), pretty_name);


                inspect_callbacks callbacks;

                callbacks.on_inspect = [&]() -> inspect_result
                {
                    
                    if constexpr(std::is_base_of<owned_component, ctype>::value)
                    {
                        if(is_debug_view())
                        {
                            property_layout layout("Owner");
                            ImGui::Text("%u", uint32_t(component->get_owner().entity()));
                        }
                    }

                    meta_any_proxy comp_var_proxy;
                    comp_var_proxy.impl->parent = var_proxy.impl;
                    comp_var_proxy.impl->type_name = entt::get_pretty_name(type);
                    comp_var_proxy.impl->name = [&]()
                    {
                        const auto& name = var_proxy.impl->name;
                        if(name.empty())
                        {
                            return pretty_name;
                        }
                        return fmt::format("{}/{}", name, pretty_name);
                    }();
                    comp_var_proxy.impl->getter = [parent_proxy = var_proxy](entt::meta_any& result)
                    {
                        entt::meta_any var;
                        if(parent_proxy.impl->getter(var) && var)
                        {
                            auto data = var.cast<entt::handle>();
                            if(data)
                            {
                                auto component = data.try_get<ctype>();
                                if(component)
                                {
                                    result = entt::forward_as_meta(*component);
                                    return true;
                                }
                            }
                        }
                        return false;
                    };
                    comp_var_proxy.impl->setter = [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
                    {
                        return parent_proxy.impl->setter(parent_proxy, value, execution_count);
                    };
                    // entt::meta_any comp_var;
                    // call_var_getter(comp_var, comp_var_getter);
                    auto comp_var = entt::forward_as_meta(*component);

                    var_info comp_info;
                    comp_info.is_copyable = false;
                    return ::unravel::inspect_var(ctx, comp_var, comp_var_proxy, comp_info);
                    
                };

                callbacks.on_add = [&]()
                {
                    // data.emplace<ctype>();

                    auto& em = ctx.get_cached<editing_manager>();
                    em.do_action<entity_add_component_action_t>({}, data, type);
                };

                callbacks.on_remove = [&]()
                {
                    if(data.all_of<ctype>())
                    {
                        auto& em = ctx.get_cached<editing_manager>();
                        em.do_action<entity_remove_component_action_t>({}, data, type);
                    }
                    
                };

                callbacks.can_remove = []()
                {
                    // prefab_component is deliberately not removable here: removing it is the
                    // unpack - the on_destroy hook strips prefab ids down the subtree - and the
                    // remove-component undo restores only the component, not those ids, so the
                    // next sync rebuilds the whole instance. Unlinking goes through the
                    // hierarchy's "Unlink from Prefab", which is what it is for.
                    return !std::is_same<ctype, id_component>::value && !std::is_same<ctype, tag_component>::value &&
                           !std::is_same<ctype, transform_component>::value && !std::is_same<ctype, prefab_id_component>::value &&
                           !std::is_same<ctype, prefab_component>::value &&
                           !std::is_same<ctype, layer_component>::value && !std::is_same<ctype, bone_component>::value &&
                           !std::is_same<ctype, submesh_component>::value;
                };

                callbacks.can_merge = []()
                {
                    return std::is_same<ctype, id_component>::value || std::is_same<ctype, tag_component>::value;
                };

                
                callbacks.icon = get_component_icon<ctype>();
                
                result |= inspect_component(pretty_name, callbacks);
            });

        auto script_comp = data.try_get<script_component>();
        if(script_comp)
        {
            const auto& comps = script_comp->get_script_components();

            int index_to_remove = -1;
            int index_to_add = -1;
            for(size_t i = 0; i < comps.size(); ++i)
            {
                ImGui::PushID(i);
                const auto& script = comps[i];
                const auto& mono_obj = script.pinned->get_object();
                if(!mono_obj.valid())
                {
                    APPLOG_ERROR("Script object is invalid for domain version: {}", script.pinned->get_domain_version());
                    continue;
                }
                const auto& type = mono_obj.get_type();
                if(!type.valid())
                {
                    APPLOG_ERROR("Script object type is invalid for domain version: {}", script.pinned->get_domain_version());
                    continue;
                }
                fs::path source_loc = script_comp->get_script_source_location(script);

                auto name = type.get_fullname();
                const auto& pretty_name = name;

                inspect_callbacks callbacks;
                callbacks.on_inspect = [&]() -> inspect_result
                {
                    inspect_result inspect_res{};

                    if(!source_loc.empty())
                    {
                        var_info field_info;
                        field_info.is_property = true;
                        field_info.read_only = true;
                        ImGui::PushReadonly(field_info.read_only);

                        std::string var = ICON_MDI_LANGUAGE_CSHARP " " + source_loc.stem().string();
                        {
                            property_layout layout("Script");

                            if(ImGui::Button(var.c_str(), ImVec2(-1.0f, ImGui::GetFrameHeight())))
                            {
                                auto& em = ctx.get_cached<editing_manager>();

                                em.focus(source_loc);
                                em.focus_path(source_loc.parent_path());
                            }

                            if(ImGui::IsItemDoubleClicked(ImGuiMouseButton_Left))
                            {
                                editor_actions::open_workspace_on_file(source_loc);
                            }
                        }
                        ImGui::PopReadonly();
                    }

                    meta_any_proxy obj_proxy;
                    obj_proxy.impl->parent = var_proxy.impl;
                    obj_proxy.impl->name = [&]()
                    {
                        const auto& name = var_proxy.impl->name;
                        if(name.empty())
                        {
                            return pretty_name;
                        }
                        return fmt::format("{}/{}", name, pretty_name);
                    }();
                    obj_proxy.impl->getter = [parent_proxy = var_proxy, i](entt::meta_any& result)
                    {
                        entt::meta_any var;
                        if(parent_proxy.impl->getter(var) && var)
                        {
                            auto data = var.cast<entt::handle>();
                            if(data)
                            {
                                auto script_comp = data.try_get<script_component>();
                                if(script_comp)
                                {
                                    const auto& comps = script_comp->get_script_components();
                                    if(i < comps.size())
                                    {
                                        auto& script = comps[i];
                                        result = script.pinned;//entt::forward_as_meta(script.pinned);
                                        return true;
                                    }
                                }
                            }
                        }
                        return false;
                    };
                    obj_proxy.impl->setter = [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
                    {
                        return parent_proxy.impl->setter(parent_proxy, value, execution_count);
                    };
                    // entt::meta_any obj_var;
                    // call_var_getter(obj_var, obj_getter);
                    entt::meta_any obj_var = script.pinned;//entt::forward_as_meta(*script.pinned);
                    obj_proxy.impl->type_name = entt::get_pretty_name(obj_var.type());

                    var_info obj_info;
                    obj_info.is_copyable = false;

                    inspect_res |= ::unravel::inspect_var(ctx, obj_var, obj_proxy, obj_info);
                    return inspect_res;
                };

                callbacks.on_add = [&]()
                {
                    index_to_add = i;
                };

                callbacks.on_remove = [&]()
                {
                    index_to_remove = i;
                };

                callbacks.can_remove = []()
                {
                    return true;
                };

                callbacks.can_merge = []()
                {
                    return false;
                };

                callbacks.icon = ICON_MDI_LANGUAGE_CSHARP;


                auto script_type = entt::resolve<script_component>();
                auto script_type_name = entt::get_name(script_type);
                auto script_type_pretty_name = entt::get_pretty_name(script_type);
                // Track component type for prefab override context
                override_ctx.set_component_type(std::string(script_type_name), script_type_pretty_name);

                std::string segment = fmt::format("script_components[{}]/{}", i, name);
                std::string pretty_segment = fmt::format("Scripts[{}]/{}", i, pretty_name);
                override_ctx.push_segment(segment, pretty_segment);

                result |= inspect_component(pretty_name, callbacks);

                override_ctx.pop_segment();

                ImGui::PopID();
            }

            if(index_to_remove != -1)
            {
                auto comp_to_remove = comps[index_to_remove];

                script_component::script_object comp_to_add;

                auto type = comp_to_remove.pinned->get_object().get_type();

                auto& em = ctx.get_cached<editing_manager>();
                auto script_type_name = type.get_fullname();
                em.do_action<entity_remove_script_component_action_t>({}, data, script_type_name, index_to_remove);

                // script_comp->remove_script_component(comp_to_remove.pinned->object);
                // script_comp->process_pending_deletions();

                if(index_to_add != -1)
                {
                    // script_comp->add_script_component(type);

                    em.do_action<entity_add_script_component_action_t>({}, data, script_type_name);
                }

                result.changed |= true;
                result.edit_finished |= true;
            }
        }

        ImGui::EndChild();
        process_script_drop_target(ctx, data, result);

        ImGui::Spacing();
        ImGui::Spacing();
        static const auto label = "Add Component";
        auto avail = ImGui::GetContentRegionAvail();
        ImVec2 size = ImGui::CalcItemSize(label);
        size.x *= 2.0f;
        ImVec2 button_pos{};
        ImGui::AlignedItem(0.5f,
                           avail.x,
                           size.x,
                           [&]()
                           {
                               button_pos = ImGui::GetCursorScreenPos();
                               if(ImGui::Button(label, size))
                               {
                                   ImGui::OpenPopup("COMPONENT_MENU");
                               }
                               process_script_drop_target(ctx, data, result);
                           });

        // The menu opens over the button like a dropdown, centered on it and pinned there while open.
        const float menu_width = COMPONENT_MENU_WIDTH_EM * ImGui::GetFontSize();
        ImGui::SetNextWindowPos(ImVec2(button_pos.x + (size.x - menu_width) * 0.5f, button_pos.y));
        ImGui::SetNextWindowSize(ImVec2(menu_width, 0.0f));
        {
            ImGui::ContextMenuStyleScope menu_style;
            if(ImGui::BeginPopup("COMPONENT_MENU"))
            {
                if(ImGui::IsWindowAppearing())
                {
                    ImGui::SetKeyboardFocusHere();
                    component_menu_path_.clear();
                }

                draw_component_menu_search_field(filter_);
                ImGui::Spacing();

                const float list_height = COMPONENT_MENU_LIST_HEIGHT_EM * ImGui::GetFontSize();
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                    ImVec2(ImGui::GetStyle().ItemSpacing.x, COMPONENT_MENU_ROW_GAP));
                ImGui::BeginChild("COMPONENT_MENU_CONTEXT", ImVec2(0.0f, list_height));

                const auto menu = build_component_menu(ctx, data, result);
                inspect_result menu_result{};
                if(filter_.IsActive())
                {
                    if(count_component_menu_matches(filter_, menu) == 0)
                    {
                        draw_component_menu_empty("No matching components");
                    }
                    else
                    {
                        menu_result = draw_component_menu_search(filter_, menu);
                    }
                }
                else
                {
                    const auto* level = find_component_menu_node(menu, component_menu_path_);
                    if(level == nullptr)
                    {
                        // The opened category no longer exists (e.g. scripts were reloaded); fall back to the root.
                        component_menu_path_.clear();
                        level = &menu;
                    }
                    menu_result = draw_component_menu_level(*level, component_menu_path_);
                }
                if(menu_result.changed)
                {
                    component_menu_path_.clear();
                }
                result |= menu_result;

                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::EndPopup();
            }
        }
    }

    if(result.changed)
    {
        if(data)
        {
            if(auto prefab = data.try_get<prefab_component>())
            {
                prefab->changed = true;
            }

            // Component data was edited in place - wake the indexed transform-dirty
            // consumers so derived render data is re-consumed (e.g. the model pose
            // refresh picks up submesh_component entry edits on armature nodes).
            if(auto transform_comp = data.try_get<transform_component>())
            {
                transform_comp->mark_consumers_dirty();
            }
        }
        
        var = data;
    }

    return result;
}
} // namespace unravel
