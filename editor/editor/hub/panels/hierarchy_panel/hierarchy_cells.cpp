#include "hierarchy_cells.h"

#include <editor/editing/editing_manager.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector_layer.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <imgui_widgets/tooltips.h>

#include <array>
#include <set>
#include <string>

namespace unravel::hierarchy_cells
{
namespace
{
// Sizes are in units of the font size, so the rows follow the UI scale of the editor.
constexpr float HIERARCHY_ROW_HEIGHT = 1.5f;
// A dropdown sits inside its cell, this far from the edges of the row.
constexpr float HIERARCHY_PILL_INSET_Y = 0.15f;
constexpr float HIERARCHY_PILL_PADDING_X = 0.4f;
constexpr float HIERARCHY_PILL_ROUNDING = 0.25f;
constexpr float HIERARCHY_ICON_BUTTON_ROUNDING = 0.25f;
constexpr float HIERARCHY_MENU_MIN_WIDTH = 11.0f;

// The light washes of the fields on the panel toolbars.
constexpr ImU32 HIERARCHY_PILL_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 HIERARCHY_PILL_HOVERED_COLOR = IM_COL32(255, 255, 255, 28);
constexpr ImU32 HIERARCHY_ICON_HOVERED_COLOR = IM_COL32(255, 255, 255, 26);
// A default value shows only on the hovered row, dimmed: it is what the entity does not set.
constexpr float HIERARCHY_PLACEHOLDER_ALPHA = 0.4f;
constexpr float HIERARCHY_ICON_ALPHA = 0.55f;
constexpr float HIERARCHY_CHECKED_ALPHA = 0.85f;
constexpr float HIERARCHY_CARET_ALPHA = 0.5f;

constexpr size_t HIERARCHY_TAG_CAPACITY = 64;
constexpr const char* HIERARCHY_LAYER_MENU_ID = "##layer_menu";
constexpr const char* HIERARCHY_TAG_MENU_ID = "##tag_menu";
constexpr const char* HIERARCHY_UNTAGGED_TEXT = "Untagged";

// The field of the open tag menu. Only one menu is open at a time.
std::array<char, HIERARCHY_TAG_CAPACITY> g_hierarchy_new_tag{};

auto calc_hierarchy_pixels(float font_units) -> float
{
    return ImFloor(ImGui::GetFontSize() * font_units);
}

template<typename Action, typename... Args>
void do_hierarchy_action(rtti::context& ctx, Args&&... args)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.push_undo_stack_enabled(true);
    em.do_action<Action>({}, std::forward<Args>(args)...);
    em.pop_undo_stack_enabled();
}

/// The whole cell as one button. It is pressed on the click, the way a menu opens.
struct hierarchy_cell_button
{
    ImRect rect{};
    bool is_hovered{};
    bool is_pressed{};
};

auto add_hierarchy_cell_button(const char* id) -> hierarchy_cell_button
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImMax(ImGui::GetContentRegionAvail().x, 1.0f), get_row_height());
    hierarchy_cell_button button{};
    button.rect = ImRect(min, min + size);
    button.is_pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_PressedOnClick);
    button.is_hovered = ImGui::IsItemHovered();
    return button;
}

auto get_hierarchy_text_color(float alpha) -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, alpha);
}

/// An icon in the middle of the rectangle, on a square that lights up under the pointer.
void draw_hierarchy_icon(const ImRect& rect, const char* icon, ImU32 color, bool is_hovered)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 center = rect.GetCenter();
    if(is_hovered)
    {
        const float half_side = (rect.GetHeight() - 2.0f * calc_hierarchy_pixels(HIERARCHY_PILL_INSET_Y)) * 0.5f;
        const ImVec2 half(half_side, half_side);
        draw_list->AddRectFilled(center - half,
                                 center + half,
                                 HIERARCHY_ICON_HOVERED_COLOR,
                                 calc_hierarchy_pixels(HIERARCHY_ICON_BUTTON_ROUNDING));
    }
    const ImVec2 icon_size = ImGui::CalcTextSize(icon);
    draw_list->AddText(ImFloor(center - icon_size * 0.5f), color, icon);
}

/// A dropdown in the cell: the value, cut with an ellipsis, and a caret at the right end.
void draw_hierarchy_pill(const ImRect& cell, const char* text, bool is_placeholder, bool is_hovered)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float inset = calc_hierarchy_pixels(HIERARCHY_PILL_INSET_Y);
    const float padding = calc_hierarchy_pixels(HIERARCHY_PILL_PADDING_X);
    const ImRect pill(cell.Min.x, cell.Min.y + inset, cell.Max.x, cell.Max.y - inset);
    // A default value gets no pill of its own until it is pointed at.
    if(!is_placeholder || is_hovered)
    {
        draw_list->AddRectFilled(pill.Min,
                                 pill.Max,
                                 is_hovered ? HIERARCHY_PILL_HOVERED_COLOR : HIERARCHY_PILL_COLOR,
                                 calc_hierarchy_pixels(HIERARCHY_PILL_ROUNDING));
    }
    const float text_y = ImFloor(pill.GetCenter().y - ImGui::GetFontSize() * 0.5f);
    const float caret_x = pill.Max.x - padding * 0.5f - ImGui::CalcTextSize(ICON_MDI_MENU_DOWN).x;
    draw_list->AddText(ImVec2(caret_x, text_y), get_hierarchy_text_color(HIERARCHY_CARET_ALPHA), ICON_MDI_MENU_DOWN);
    const float text_max_x = caret_x - padding * 0.25f;
    ImGui::PushStyleColor(ImGuiCol_Text, get_hierarchy_text_color(is_placeholder ? HIERARCHY_PLACEHOLDER_ALPHA : 1.0f));
    ImGui::RenderTextEllipsis(draw_list,
                              ImVec2(pill.Min.x + padding, text_y),
                              ImVec2(text_max_x, pill.Max.y),
                              text_max_x,
                              text,
                              nullptr,
                              nullptr);
    ImGui::PopStyleColor();
}

//-----------------------------------------------------------------------------
/// <summary>
/// Begin the menu of a cell, hanging from the right end of the cell: the value columns sit at the
/// right of the panel, so the menu opens towards its middle. Pair with end_hierarchy_cell_menu()
/// when it returns true.
/// </summary>
//-----------------------------------------------------------------------------
auto begin_hierarchy_cell_menu(const char* popup_id, const ImRect& cell) -> bool
{
    ImGui::SetNextWindowPos(ImVec2(cell.Max.x, cell.Max.y), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    const float min_width = ImMax(cell.GetWidth(), calc_hierarchy_pixels(HIERARCHY_MENU_MIN_WIDTH));
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_width, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushContextMenuStyle();
    if(ImGui::BeginPopup(popup_id))
    {
        return true;
    }
    ImGui::PopContextMenuStyle();
    return false;
}

void end_hierarchy_cell_menu()
{
    ImGui::EndPopup();
    ImGui::PopContextMenuStyle();
}

/// The layer menu of the inspector, as an undoable edit of the entity.
void draw_layer_menu(rtti::context& ctx, entt::handle entity, const layer_mask& mask, const ImRect& cell)
{
    if(!begin_hierarchy_cell_menu(HIERARCHY_LAYER_MENU_ID, cell))
    {
        return;
    }
    layer_mask new_mask = mask;
    const bool is_changed = draw_layer_mask_menu_items(ctx, new_mask);
    end_hierarchy_cell_menu();
    if(is_changed)
    {
        do_hierarchy_action<entity_set_layers_action_t>(ctx, entity, mask.mask, new_mask.mask);
    }
}

/// Tags are free text: the menu offers those the scene already uses.
auto collect_scene_tags(const entt::registry& registry) -> std::set<std::string>
{
    std::set<std::string> tags;
    registry.view<const tag_component>().each(
        [&](const tag_component& tag)
        {
            if(!tag.tag.empty())
            {
                tags.insert(tag.tag);
            }
        });
    return tags;
}

/// A field for a new tag, then Untagged and the tags of the scene.
void draw_tag_menu(rtti::context& ctx, entt::handle entity, const std::string& tag, const ImRect& cell)
{
    if(!begin_hierarchy_cell_menu(HIERARCHY_TAG_MENU_ID, cell))
    {
        return;
    }
    std::string new_tag = tag;
    if(ImGui::IsWindowAppearing())
    {
        g_hierarchy_new_tag.fill('\0');
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool is_entered = ImGui::InputTextWithHint("##new_tag",
                                                     "New tag",
                                                     g_hierarchy_new_tag.data(),
                                                     g_hierarchy_new_tag.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
    if(is_entered && g_hierarchy_new_tag[0] != '\0')
    {
        new_tag = g_hierarchy_new_tag.data();
        ImGui::CloseCurrentPopup();
    }
    ImGui::Separator();
    if(ImGui::MenuItem(HIERARCHY_UNTAGGED_TEXT, nullptr, tag.empty()))
    {
        new_tag.clear();
    }
    for(const std::string& scene_tag : collect_scene_tags(*entity.registry()))
    {
        if(ImGui::MenuItem(scene_tag.c_str(), nullptr, scene_tag == tag))
        {
            new_tag = scene_tag;
        }
    }
    end_hierarchy_cell_menu();
    if(new_tag != tag)
    {
        do_hierarchy_action<entity_set_tag_action_t>(ctx, entity, tag, new_tag);
    }
}
} // namespace

auto get_row_height() -> float
{
    return calc_hierarchy_pixels(HIERARCHY_ROW_HEIGHT);
}

void draw_active_cell(rtti::context& ctx, const row_state& row)
{
    auto* transform = row.entity.try_get<transform_component>();
    if(transform == nullptr)
    {
        return;
    }
    const bool is_active = transform->is_active();
    const hierarchy_cell_button button = add_hierarchy_cell_button("##active");
    ImGui::SetItemTooltipEx("%s", is_active ? "Deactivate" : "Activate");
    if(!is_active || row.is_hovered)
    {
        const ImU32 color = get_hierarchy_text_color(button.is_hovered ? 1.0f : HIERARCHY_ICON_ALPHA);
        draw_hierarchy_icon(button.rect, is_active ? ICON_MDI_EYE : ICON_MDI_EYE_OFF, color, button.is_hovered);
    }
    if(button.is_pressed)
    {
        do_hierarchy_action<entity_set_active_action_t>(ctx, row.entity, is_active, !is_active);
    }
}

void draw_static_cell(rtti::context& ctx, const row_state& row)
{
    const auto* model = row.entity.try_get<model_component>();
    if(model == nullptr)
    {
        return;
    }
    const bool is_static = model->is_static();
    const hierarchy_cell_button button = add_hierarchy_cell_button("##static");
    ImGui::SetItemTooltipEx("%s", is_static ? "Static model (click for dynamic)" : "Dynamic model (click for static)");
    if(is_static || row.is_hovered)
    {
        // In the text color, not the accent: a selected row is filled with the accent.
        const float alpha = button.is_hovered ? 1.0f : (is_static ? HIERARCHY_CHECKED_ALPHA : HIERARCHY_ICON_ALPHA);
        draw_hierarchy_icon(button.rect,
                            is_static ? ICON_MDI_CHECKBOX_MARKED : ICON_MDI_CHECKBOX_BLANK_OUTLINE,
                            get_hierarchy_text_color(alpha),
                            button.is_hovered);
    }
    if(button.is_pressed)
    {
        do_hierarchy_action<entity_set_static_action_t>(ctx, row.entity, is_static, !is_static);
    }
}

void draw_layer_cell(rtti::context& ctx, const row_state& row)
{
    const auto* layer = row.entity.try_get<layer_component>();
    if(layer == nullptr)
    {
        return;
    }
    // Every row shows its layers, the default ones too: the column reads as a list of them.
    const layer_mask mask = layer->layers;
    const bool is_menu_open = ImGui::IsPopupOpen(HIERARCHY_LAYER_MENU_ID);
    const hierarchy_cell_button button = add_hierarchy_cell_button("##layer");
    const std::string text = get_layer_mask_text(ctx, mask);
    draw_hierarchy_pill(button.rect, text.c_str(), false, button.is_hovered || is_menu_open);
    if(button.is_pressed)
    {
        ImGui::OpenPopup(HIERARCHY_LAYER_MENU_ID);
    }
    draw_layer_menu(ctx, row.entity, mask, button.rect);
}

void draw_tag_cell(rtti::context& ctx, const row_state& row)
{
    const auto* tag = row.entity.try_get<tag_component>();
    if(tag == nullptr)
    {
        return;
    }
    const bool is_untagged = tag->tag.empty();
    const bool is_menu_open = ImGui::IsPopupOpen(HIERARCHY_TAG_MENU_ID);
    const hierarchy_cell_button button = add_hierarchy_cell_button("##tag");
    if(!is_untagged || row.is_hovered || is_menu_open)
    {
        const char* text = is_untagged ? HIERARCHY_UNTAGGED_TEXT : tag->tag.c_str();
        draw_hierarchy_pill(button.rect, text, is_untagged, button.is_hovered || is_menu_open);
    }
    if(button.is_pressed)
    {
        ImGui::OpenPopup(HIERARCHY_TAG_MENU_ID);
    }
    // A copy: the menu changes the tag through an action while it still compares against it.
    const std::string current_tag = tag->tag;
    draw_tag_menu(ctx, row.entity, current_tag, button.rect);
}

auto get_open_prefab_button_width() -> float
{
    return get_row_height();
}

auto draw_open_prefab_button(const ImRect& cell_rect) -> bool
{
    const ImRect bb(cell_rect.Max.x - get_open_prefab_button_width(), cell_rect.Min.y, cell_rect.Max.x, cell_rect.Max.y);
    const ImGuiID id = ImGui::GetID("##open_prefab");
    if(!ImGui::ItemAdd(bb, id))
    {
        return false;
    }
    bool is_hovered = false;
    bool is_held = false;
    const bool is_pressed = ImGui::ButtonBehavior(bb, id, &is_hovered, &is_held);
    draw_hierarchy_icon(bb,
                        ICON_MDI_CHEVRON_RIGHT,
                        get_hierarchy_text_color(is_hovered ? 1.0f : HIERARCHY_ICON_ALPHA),
                        is_hovered);
    ImGui::SetItemTooltipEx("%s", "Open the prefab");
    return is_pressed;
}

} // namespace unravel::hierarchy_cells
