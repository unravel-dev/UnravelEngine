#include "inspector.h"
#include <algorithm>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <editor/imgui/integration/imgui_style.h>
#include <imgui/imgui_internal.h>
#include <string_utils/utils.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/meta/ecs/entity.hpp>
#include "imgui/imgui.h"
#include "inspectors.h"

namespace ImGui
{
    auto GetTintedStyleColor(ImGuiCol idx, float multiplier = 1.0f) -> ImVec4
    {
        auto color = ImGui::GetStyleColorVec4(idx);
        color.x *= multiplier;
        color.y *= multiplier;
        color.z *= multiplier;
        return color;
    }
}

namespace unravel
{

namespace
{
std::vector<property_layout*> stack;
void push_layout_to_stack(property_layout* l)
{
    stack.push_back(l);
}

void pop_layout_from_stack(property_layout* l)
{
    stack.pop_back();
}

/// Share of a window's width the label column takes until its divider is dragged.
constexpr float LABEL_COLUMN_SHARE = 0.4f;
constexpr float LABEL_COLUMN_SHARE_MIN = 0.15f;
constexpr float LABEL_COLUMN_SHARE_MAX = 0.75f;
/// The narrowest label and value, in font heights: a deep row or a narrow panel stops there.
constexpr float LABEL_MIN_WIDTH_EM = 4.0f;
constexpr float VALUE_MIN_WIDTH_EM = 6.0f;
/// A row drawn inside another row's value splits that value on its own.
constexpr float NESTED_LABEL_SHARE = 0.325f;
/// Below this width, in font heights, a window's rows stack their label over their value.
constexpr float STACK_BELOW_EM = 20.0f;
/// Half the width of the strip on a row's column boundary that drags the divider.
constexpr float DIVIDER_GRAB_HALF_WIDTH = 3.0f;
constexpr float DIVIDER_LINE_THICKNESS = 1.0f;
constexpr const char* HELP_MARKER_TEXT = "(?)";
/// The mark of an overridden row, at the window's content edge, and how far it keeps from the row's top and bottom.
constexpr float OVERRIDE_BAR_WIDTH = 2.0f;
constexpr float OVERRIDE_BAR_INSET = 3.0f;
/// How much lighter the revert button gets under the mouse.
constexpr float REVERT_HOVER_BRIGHTEN = 0.25f;
constexpr const char* REVERT_TO_PREFAB_LABEL = "Revert to Prefab";
/// Kept per window in its ImGui state storage: the label column share, and whether a divider is held.
const ImGuiID LABEL_SHARE_KEY = ImHashStr("property_layout/label_share");
const ImGuiID DIVIDER_HELD_KEY = ImHashStr("property_layout/divider_held");

/// The label cell of a row, and the divider it can drag.
struct label_column
{
    float width{};
    /// Screen x of the boundary between the row's label and its value.
    float divider_x{};
    bool has_divider{};
};

auto get_label_share() -> float
{
    return ImGui::GetStateStorage()->GetFloat(LABEL_SHARE_KEY, LABEL_COLUMN_SHARE);
}

/// One divider for every row of a window, so values line up at every depth: indentation narrows only the label.
auto make_label_column(bool is_nested) -> label_column
{
    const ImGuiWindow* window = ImGui::GetCurrentWindowRead();
    const float row_x = window->DC.CursorPos.x;
    const float right = window->WorkRect.Max.x;
    label_column column{};
    if(is_nested)
    {
        column.width = ImMax(0.0f, (right - row_x) * NESTED_LABEL_SHARE);
        return column;
    }
    const float em = ImGui::GetFontSize();
    const float left = window->WorkRect.Min.x;
    const float divider_x = left + (right - left) * get_label_share();
    // A deep row keeps a readable label; a narrow window keeps a usable value.
    const float widest = ImMax(0.0f, right - row_x - VALUE_MIN_WIDTH_EM * em);
    column.width = ImMin(ImMax(divider_x - row_x, LABEL_MIN_WIDTH_EM * em), widest);
    // The table pads both cells: the boundary is the middle of the gap between them.
    column.divider_x = row_x + column.width + ImGui::GetStyle().CellPadding.x;
    column.has_divider = true;
    return column;
}

/// Dragging moves the divider of every row in the window; double-clicking puts it back. Submitted before the
/// row's items, so its strip takes the hover from them.
void update_divider(const std::string& row_name, float divider_x)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float top = window->DC.CursorPos.y;
    const ImRect bb(divider_x - DIVIDER_GRAB_HALF_WIDTH,
                    top,
                    divider_x + DIVIDER_GRAB_HALF_WIDTH,
                    top + ImGui::GetFrameHeight());
    const ImGuiID id = window->GetID(("##label_divider" + row_name).c_str());
    if(!ImGui::ItemAdd(bb, id, nullptr, ImGuiItemFlags_NoNav))
    {
        return;
    }
    bool hovered = false;
    bool held = false;
    ImGui::ButtonBehavior(bb, id, &hovered, &held);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    // A click, the double-click that resets it included, leaves the divider where it is.
    if(held && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const float left = window->WorkRect.Min.x;
        const float width = ImMax(1.0f, window->WorkRect.Max.x - left);
        const float share = (ImGui::GetIO().MousePos.x - left) / width;
        storage->SetFloat(LABEL_SHARE_KEY, ImClamp(share, LABEL_COLUMN_SHARE_MIN, LABEL_COLUMN_SHARE_MAX));
        storage->SetBool(DIVIDER_HELD_KEY, true);
    }
    else if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        storage->SetBool(DIVIDER_HELD_KEY, false);
    }
    if(hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        storage->SetFloat(LABEL_SHARE_KEY, LABEL_COLUMN_SHARE);
    }
    if(hovered || held)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    // While one divider is dragged every row shows its own, which reads as one line down the window.
    if(hovered || held || storage->GetBool(DIVIDER_HELD_KEY))
    {
        const ImU32 color = ImGui::GetColorU32(hovered && !held ? ImGuiCol_SeparatorHovered : ImGuiCol_SeparatorActive);
        window->DrawList->AddLine(ImVec2(divider_x, bb.Min.y), ImVec2(divider_x, bb.Max.y), color, DIVIDER_LINE_THICKNESS);
    }
}

/// A label ending in an ellipsis when it is wider than max_width. One item, as the text was, so the tooltip and
/// the context menu attach to it.
void draw_label_text(const std::string& text, float max_width)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    const ImVec2 size(ImMax(0.0f, ImMin(text_size.x, max_width)), text_size.y);
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size, 0.0f);
    if(!ImGui::ItemAdd(bb, 0))
    {
        return;
    }
    ImGui::RenderTextEllipsis(window->DrawList, bb.Min, bb.Max, bb.Max.x, text.c_str(), nullptr, &text_size);
}

auto get_help_marker_width() -> float
{
    return ImGui::CalcTextSize(HELP_MARKER_TEXT).x + ImGui::GetStyle().ItemSpacing.x;
}
} // namespace


property_layout_group::property_layout_group(const std::string& name)
{
    // ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetTintedStyleColor(ImGuiCol_ChildBg, 0.8f));
    // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    // ImGui::BeginChild(name.c_str(), {-1.0f, 0.0f}, ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
    
}

property_layout_group::~property_layout_group()
{
    // ImGui::EndChild();
    // ImGui::PopStyleVar();
    // ImGui::PopStyleColor();
    // ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImGuiCol_Separator), 2.0f);

}

auto property_layout::get_current() -> property_layout*
{
    if(stack.empty())
    {
        return nullptr;
    }
    return stack.back();
}

property_layout::property_layout()
{
    push_layout_to_stack(this);
}

property_layout::property_layout(const entt::meta_data& prop, const entt::meta_any& object, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    set_data(prop, object, columns);

    push_layout();
}

property_layout::property_layout(const std::string& name, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    set_data(name, "", "", columns);

    push_layout();
}

property_layout::property_layout(const std::string& name, const std::string& tooltip, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    set_data(name, tooltip,  "", columns);

    push_layout();
}

property_layout::property_layout(const std::string& name, const std::string& tooltip, const std::string& hint, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    set_data(name, tooltip, hint, columns);

    push_layout();
}

property_layout::property_layout(const std::string& name, const std::function<void()>& callback, bool columns /*= true*/)
{
    push_layout_to_stack(this);

    callback_ = callback;
    set_data(name, "", "", columns);

    push_layout();
}


property_layout::~property_layout()
{  
    pop_layout();

    pop_layout_from_stack(this);
}

void property_layout::set_data(const entt::meta_data& prop, const entt::meta_any& object, bool columns)
{
    auto name = entt::get_pretty_name(prop);

    auto hint = entt::get_property_hint(object, prop);

    auto tooltip = entt::get_attribute_as<std::string>(prop, "tooltip");

    set_data(name, tooltip, hint, columns);

}

void property_layout::set_data(const std::string& name, const std::string& tooltip, const std::string& hint, bool columns)
{
    name_ = string_utils::capitalize(name);
    tooltip_ = tooltip;
    hint_ = hint;
    columns_ = columns;
}

auto property_layout::is_inside_value_cell(const property_layout* self) -> bool
{
    return std::any_of(stack.begin(),
                       stack.end(),
                       [self](const property_layout* layout)
                       {
                           return layout != self && layout->pushed_ && layout->columns_open_;
                       });
}

void property_layout::begin_columns()
{
    if(!columns_)
    {
        return;
    }
    const bool is_nested = is_inside_value_cell(this);
    const ImGuiWindow* window = ImGui::GetCurrentWindowRead();
    const float window_width = window->WorkRect.Max.x - window->WorkRect.Min.x;
    // Too narrow for two columns: the label takes the first line, the value the next, both the full width. The
    // group is the row, for its override mark.
    if(!is_nested && window_width < STACK_BELOW_EM * ImGui::GetFontSize())
    {
        stacked_ = true;
        has_gutter_ = true;
        ImGui::BeginGroup();
        return;
    }
    const label_column column = make_label_column(is_nested);
    if(column.has_divider)
    {
        update_divider(name_, column.divider_x);
    }
    // A row of the window keeps a gutter right of its value, where an overridden row has its revert button. The
    // table pads both cells, which comes out of the value too.
    has_gutter_ = column.has_divider;
    const float gutter = has_gutter_ ? ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.x * 2.0f : 0.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    columns_open_ = ImGui::BeginTable(("properties##" + name_).c_str(), 2);
    if(columns_open_)
    {
        ImGui::TableSetupColumn("##prop_column1", ImGuiTableColumnFlags_WidthFixed, column.width);
        ImGui::TableSetupColumn("##prop_column2",
                                ImGuiTableColumnFlags_WidthFixed,
                                ImMax(1.0f, avail - column.width - gutter));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
    }
}

void property_layout::push_layout(bool auto_proceed_to_next_column, float label_reserve)
{
    pushed_ = true;

    begin_columns();

    ImGui::AlignTextToFramePadding();
    // The divider's grab strip and the caller's buttons share the cell with the label; a stacked row's label line
    // ends where its revert button is.
    const float label_room = ImGui::GetContentRegionAvail().x - label_reserve -
                             (columns_open_ ? DIVIDER_GRAB_HALF_WIDTH : 0.0f) -
                             (stacked_ ? ImGui::GetFrameHeight() : 0.0f);
    const float text_width = ImGui::CalcTextSize(name_.c_str()).x;
    const bool show_help_marker = !tooltip_.empty() && text_width + get_help_marker_width() <= label_room;
    if(callback_)
    {
        callback_();
    }
    else
    {
        draw_label_text(name_, label_room);
    }

    draw_label_menu_and_tooltip(show_help_marker, !callback_ && text_width > label_room);

    if(auto_proceed_to_next_column)
    {
        prepare_for_item();
    }
}

void property_layout::draw_label_menu_and_tooltip(bool show_help_marker, bool is_label_shortened)
{
    if(ImGui::BeginPopupContextItem(("Property Context Menu##" + name_).c_str()))
    {
        {
            ImGui::ContextMenuStyleScope style_scope;
            const property_override_plain_scope plain(engine::context().get_cached<property_override_view>());

            auto& ctx = engine::context();
            auto& override_ctx = ctx.get_cached<prefab_override_context>();

            if(override_ctx.is_path_overridden())
            {
                if(ImGui::MenuItem(fmt::format("Reset {} to default", name_).c_str()))
                {
                    override_ctx.reset_override();
                }
            }
            const auto& profile_overrides = ctx.get_cached<property_override_view>();
            if(const auto& label = profile_overrides.get_revert_label(); !label.empty() && ImGui::MenuItem(label.c_str()))
            {
                profile_overrides.revert_current();
            }
        }
        ImGui::EndPopup();
    }

    // A settings profile's overrides add a line, so a property without a description gets a tooltip too, and a
    // shortened label gets one that names it in full.
    const auto& override_note = engine::context().get_cached<property_override_view>().get_tooltip_note();
    if(!tooltip_.empty() || !override_note.empty() || is_label_shortened)
    {
        auto tooltip_callback = [&]()
        {
            const property_override_plain_scope plain(engine::context().get_cached<property_override_view>());
            ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushFont(ImGui::Font::Bold);
            ImGui::PushWindowFontScale(1.2f);
            ImGui::Text("%s", name_.c_str());
            ImGui::PopWindowFontScale();
            ImGui::PopFont();
            if(!tooltip_.empty() || !override_note.empty())
            {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
            }
            if(!tooltip_.empty())
            {
                ImGui::Text("%s", tooltip_.c_str());
            }
            if(!override_note.empty())
            {
                ImGui::Spacing();
                ImGui::TextUnformatted(override_note.c_str());
            }
            ImGui::PopStyleColor();
        };
        ImGui::ItemTooltipEx(tooltip_callback);

        if(show_help_marker)
        {
            ImGui::SameLine();
            ImGui::HelpMarker(HELP_MARKER_TEXT, true, tooltip_callback);
        }
    }
}

void property_layout::prepare_for_item()
{
    if(columns_open_)
    {
        ImGui::TableNextColumn();
    }

    ImGui::PushID(name_.c_str());
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
}

auto property_layout::push_tree_layout(ImGuiTreeNodeFlags flags, bool default_open) -> bool
{
    pushed_ = true;

    begin_columns();

    ImGui::SetNextItemOpen(default_open, ImGuiCond_Appearing);
    open_next_node_while_filtering();
    ImGui::AlignTextToFramePadding();
    const float label_room = ImGui::GetContentRegionAvail().x - (columns_open_ ? DIVIDER_GRAB_HALF_WIDTH : 0.0f) -
                             (stacked_ ? ImGui::GetFrameHeight() : 0.0f);
    const float arrow_width = ImGui::GetTreeNodeToLabelSpacing();
    const float text_width = ImGui::CalcTextSize(name_.c_str()).x;
    const bool is_label_shortened = arrow_width + text_width > label_room;
    const bool show_help_marker = !tooltip_.empty() && arrow_width + text_width + get_help_marker_width() <= label_room;
    if(!is_label_shortened)
    {
        open_ = ImGui::TreeNodeEx(name_.c_str(), flags | ImGuiTreeNodeFlags_AllowOverlap);
    }
    else
    {
        // The node keeps its id and its open state, spans the cell, and leaves its text to an ellipsis drawn in
        // its place.
        open_ = ImGui::TreeNodeEx(name_.c_str(),
                                  flags | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth,
                                  "%s",
                                  "");
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 text_min(min.x + arrow_width, min.y + ImGui::GetStyle().FramePadding.y);
        const float text_max_x = min.x + label_room;
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),
                                  text_min,
                                  ImVec2(text_max_x, text_min.y + ImGui::GetFontSize()),
                                  text_max_x,
                                  name_.c_str(),
                                  nullptr,
                                  nullptr);
    }

    draw_label_menu_and_tooltip(show_help_marker, is_label_shortened);

    prepare_for_item();

    return open_;
}

void property_layout::pop_layout()
{
    if(!pushed_)
    {
        return;
    }

    ImGui::PopID();
    ImGui::PopItemWidth();

    if(open_)
    {
        open_ = false;
        ImGui::TreePop();
    }

    if(columns_)
    {
        columns_ = false;
        if(columns_open_ && ImGui::TableGetColumnCount() > 1)
        {
            ImGui::EndTable();
            if(has_gutter_)
            {
                draw_override_gutter();
            }
        }
        if(stacked_)
        {
            ImGui::EndGroup();
            draw_override_gutter();
        }
        columns_open_ = false;
        has_gutter_ = false;
        stacked_ = false;
    }

    

    
    if(!hint_.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.0f, 1.0f), "%s",hint_.c_str());
        ImGui::PopTextWrapPos();
    }
    // group_.reset();

    pushed_ = false;
}

void property_layout::draw_override_gutter()
{
    auto& ctx = engine::context();
    const auto& view = ctx.get_cached<property_override_view>();
    auto& prefab = ctx.get_cached<prefab_override_context>();
    // A settings profile's or a material instance's override is the more specific one.
    const bool is_view_overridden = view.get_current_mark() == override_mark::overridden;
    const bool is_prefab_overridden = !is_view_overridden && prefab.is_active && prefab.is_path_overridden();
    if(!is_view_overridden && !is_prefab_overridden)
    {
        return;
    }
    ImVec4 color = view.get_marker_color();
    std::string revert_label = view.get_revert_label();
    if(is_prefab_overridden)
    {
        color = imgui_style::get_accent_color();
        color.w = 1.0f;
        revert_label = REVERT_TO_PREFAB_LABEL;
    }

    // The table or group that just ended is the row; the bar goes beside it at the window's content edge, however
    // deep it is.
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImRect row(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const float bar_x = window->WorkRect.Min.x;
    window->DrawList->AddRectFilled(ImVec2(bar_x, row.Min.y + OVERRIDE_BAR_INSET),
                                    ImVec2(bar_x + OVERRIDE_BAR_WIDTH, row.Max.y - OVERRIDE_BAR_INSET),
                                    ImGui::GetColorU32(color));
    if(revert_label.empty())
    {
        return;
    }

    const float size = ImGui::GetFrameHeight();
    const ImRect button(ImVec2(window->WorkRect.Max.x - size, row.Min.y), ImVec2(window->WorkRect.Max.x, row.Min.y + size));
    const ImGuiID id = window->GetID(("##revert_override" + name_).c_str());
    if(!ImGui::ItemAdd(button, id))
    {
        return;
    }
    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(button, id, &hovered, &held);
    ImVec4 icon_color = color;
    if(hovered)
    {
        icon_color.x = ImMin(1.0f, icon_color.x + REVERT_HOVER_BRIGHTEN);
        icon_color.y = ImMin(1.0f, icon_color.y + REVERT_HOVER_BRIGHTEN);
        icon_color.z = ImMin(1.0f, icon_color.z + REVERT_HOVER_BRIGHTEN);
    }
    ImGui::RenderIconCentered(window->DrawList, button.GetCenter(), ICON_MDI_UNDO_VARIANT, ImGui::GetColorU32(icon_color));
    ImGui::SetItemTooltipEx("%s", revert_label.c_str());
    if(pressed)
    {
        if(is_view_overridden)
        {
            view.revert_current();
        }
        else
        {
            prefab.reset_override();
        }
    }
}

void inspector::before_inspect(const entt::meta_data& prop, const entt::meta_any& object)
{
    layout_ = std::make_unique<property_layout>(prop, object);
}

void inspector::after_inspect(const entt::meta_data& prop, const entt::meta_any& object)
{
    layout_.reset();
}


auto make_proxy(entt::meta_any& var, const std::string& name) -> meta_any_proxy
{
    meta_any_proxy proxy;
    proxy.impl->parent = nullptr;
    proxy.impl->type_name = entt::get_pretty_name(var.type());
    proxy.impl->name = name;
    proxy.impl->resolver = [var](entt::meta_any& result) mutable
    {
        result = var;
        return true;
    };
    proxy.impl->getter = [var = var.as_ref()](entt::meta_any& result) mutable
    {
        result = var.as_ref();
        return true;
    };
    proxy.impl->setter = [](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(proxy.impl->getter(var) && var)
        {
            return var.assign(value);
        }
        return false;
    };
    return proxy;
}

auto make_entity_proxy(entt::meta_any& var, const std::string& name) -> meta_any_proxy
{
    if(var.type() != entt::resolve<entt::uhandle>())
    {
        return make_proxy(var, name);
    }
    auto handle = var.cast<entt::uhandle>();
    
    meta_any_proxy proxy;
    proxy.impl->parent = nullptr;
    proxy.impl->type_name = entt::get_pretty_name(var.type());
    proxy.impl->name = name;
    proxy.impl->resolver = [handle](entt::meta_any& result) mutable -> bool
    {
        result = handle;
        return !!result;
    };
    proxy.impl->getter = [handle](entt::meta_any& result) mutable -> bool
    {
        result = handle;
        return !!result;
    };
    proxy.impl->setter = [](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var_slot;
        if(proxy.impl->getter(var_slot) && var_slot)
        {
            return var_slot.assign(value);
        }
        return false;
    };
    return proxy;
}

auto make_property_proxy(const meta_any_proxy& var_proxy, const entt::meta_data& prop) -> meta_any_proxy
{
    meta_any_proxy prop_proxy;
    prop_proxy.impl->parent = var_proxy.impl;
    prop_proxy.impl->type_name = entt::get_pretty_name(prop.type());
    prop_proxy.impl->name = make_property_proxy_name(var_proxy.impl->name, prop);
    prop_proxy.impl->getter = [parent_proxy = var_proxy, prop](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            result = prop.get(var);
            return true;
        }
        return false;
    };
    prop_proxy.impl->setter = [parent_proxy = var_proxy, prop](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            prop.set(var, value);
            return parent_proxy.impl->setter(parent_proxy, var, execution_count);
        }
        return false;
    };
    return prop_proxy;
}

void draw_property_header(const std::string& text, bool has_rows_above)
{
    // A marked property's font and colors are not its header's.
    const property_override_plain_scope plain(engine::context().get_cached<property_override_view>());
    ImGui::PushFont(ImGui::Font::Bold);
    if(has_rows_above)
    {
        ImGui::NewLine();
    }
    // Capitalized as the rows' labels are.
    ImGui::SeparatorText(string_utils::capitalize(text).c_str());
    ImGui::Spacing();
    ImGui::PopFont();
}

auto make_property_proxy_name(const std::string& parent_name, const entt::meta_data& prop) -> std::string
{
    if(parent_name.empty())
    {
        return entt::get_pretty_name(prop);
    }
    return fmt::format("{}/{}", parent_name, entt::get_pretty_name(prop));
}
} // namespace unravel
