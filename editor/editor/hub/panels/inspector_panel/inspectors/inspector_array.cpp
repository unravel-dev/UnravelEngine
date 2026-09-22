#include "inspectors.h"
#include "inspector.h"
#include "inspector_container_widgets.h"

#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "editor/imgui/integration/imgui_style.h"
#include "imgui_widgets/utils.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "reflection/reflection.h"

#include <algorithm>
#include <cstring>
#include <string>

// The inspector of sequence containers: C++ vectors and arrays, and the C# arrays and lists the
// script inspector hands over as vectors.
namespace unravel
{
namespace
{
// Sizes are in units of the font size, so the list follows the UI scale of the editor.
constexpr float ARRAY_GRIP_WIDTH = 1.1f;
constexpr float ARRAY_DROP_LINE_THICKNESS = 2.0f;
// The grip stays faint until its label is pointed at.
constexpr float ARRAY_GRIP_ALPHA = 0.3f;
constexpr float ARRAY_GRIP_HOVERED_ALPHA = 0.8f;
constexpr const char* ARRAY_ELEMENT_PAYLOAD = "inspector_array_element";

/// What a dragged element carries: the array it comes from and where it is in it.
struct array_element_payload
{
    ImGuiID array_id{};
    int index{};
};

/// How an array may be edited, the same for all its elements.
struct array_state
{
    /// Tells the array apart from the others a drag may come from.
    ImGuiID id{};
    /// The first elements are read only: they are not removed or moved, and nothing moves above them.
    int readonly_count{};
    /// Elements can be added, removed and moved.
    bool is_resizeable{};
    /// The count cannot be changed.
    bool is_readonly{};
    /// What an element is called before its position, "Element" unless the array says otherwise.
    std::string element_label;
};

/// A change to the number or the order of the elements. Found while the elements are drawn, it
/// is applied after them: they must stay where they are while the loop draws them.
struct array_edit
{
    enum class kind
    {
        none,
        remove,
        move
    };

    kind type{kind::none};
    int index{-1};
    /// Move only: the position the element goes before, counted before the move.
    int insert_before{-1};
};

/// The label cell of an element row, filled in by the row's label callback.
struct array_element_row
{
    ImGuiID array_id{};
    int index{};
    std::string label;
    bool is_movable{};
    bool is_foldout{};
    bool is_open{};
    /// The label cell on screen: the remove button goes to its right end.
    ImRect label_rect{};
    /// Where the name starts on screen: the properties of a foldout line up under it.
    float text_x{};
};

/// Applies what the header asked for: one more element, or the typed count.
auto resize_array(entt::meta_sequence_container& view, std::size_t& size, const array_state& state,
                  const container_widgets::header_request& request) -> inspect_result
{
    inspect_result result{};
    if(!request.is_add_pressed && !request.is_count_entered)
    {
        return result;
    }
    const int requested_size = request.is_add_pressed ? static_cast<int>(size) + 1 : request.entered_count;
    const int new_size = std::max(state.readonly_count, requested_size);
    if(view.resize(static_cast<std::size_t>(new_size)))
    {
        size = static_cast<std::size_t>(new_size);
        result.changed = true;
    }
    result.edit_finished = true;
    return result;
}

/// An element with an inspector of its own is one widget: it shares the row with its label.
/// Anything else is a set of properties and folds out under its label.
auto is_inline_array_element(rtti::context& ctx, const entt::meta_type& type) -> bool
{
    return type.is_enum() || get_inspector(ctx, type) != nullptr;
}

/// An element folding out is named after its first property when that is a string with text in
/// it, as Unity names them. Any other element is named after its position.
auto make_array_element_label(entt::meta_any& value, std::size_t index, bool is_foldout, const std::string& element_label)
    -> std::string
{
    std::string label = element_label + " " + std::to_string(index);
    if(!is_foldout)
    {
        return label;
    }
    auto properties = value.type().data();
    if(properties.begin() == properties.end())
    {
        return label;
    }
    const entt::meta_data first_property = properties.begin()->second;
    if(first_property.type() != entt::resolve<std::string>())
    {
        return label;
    }
    const entt::meta_any first_value = first_property.get(value);
    const auto* text = first_value.try_cast<std::string>();
    return text != nullptr && !text->empty() ? *text : label;
}

/// Reaches the element by its position in the array, each time anew.
auto make_array_element_proxy(const meta_any_proxy& var_proxy, const entt::meta_any& value, std::size_t i)
    -> meta_any_proxy
{
    meta_any_proxy value_proxy;
    value_proxy.impl->parent = var_proxy.impl;
    value_proxy.impl->type_name = entt::get_pretty_name(value.type());
    value_proxy.impl->name = [&]()
    {
        const auto& name = var_proxy.impl->name;
        if(name.empty())
        {
            return fmt::format("[{}]", i);
        }
        return fmt::format("{}[{}]", name, i);
    }();
    value_proxy.impl->getter = [parent_proxy = var_proxy, i](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            auto view = var.as_sequence_container();
            if(view.size() > static_cast<std::size_t>(i))
            {
                auto value = view[i];
                result = value;
                return true;
            }
        }
        return false;
    };
    value_proxy.impl->setter = [parent_proxy = var_proxy, i](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            auto view = var.as_sequence_container();
            if(view.size() > static_cast<std::size_t>(i))
            {
                // get iterator to i
                auto it = view.begin();
                std::advance(it, static_cast<std::ptrdiff_t>(i));

                // remove old element
                it = view.erase(it);

                // insert new element at position i
                view.insert(it, value);

                // If the getter returned a copy, write back; if it was a ref, this is harmless.
                return parent_proxy.impl->setter(parent_proxy, var, execution_count);
            }
        }
        return false;
    };
    return value_proxy;
}

/// Makes the last item a handle to drag the element by.
void begin_array_element_drag(const array_element_row& row)
{
    if(!row.is_movable || !ImGui::BeginDragDropSource())
    {
        return;
    }
    const array_element_payload payload{row.array_id, row.index};
    ImGui::SetDragDropPayload(ARRAY_ELEMENT_PAYLOAD, &payload, sizeof(payload));
    ImGui::TextUnformatted(row.label.c_str());
    ImGui::EndDragDropSource();
}

/// The label cell of an element: a grip to drag it by, then its name, or a foldout for an element
/// with properties. The name leaves room at the right end for the remove button.
void draw_array_element_label(array_element_row& row)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    row.label_rect = ImRect(min, ImVec2(min.x + ImGui::GetContentRegionAvail().x, min.y + height));
    const ImRect grip_rect(min, ImVec2(min.x + container_widgets::to_pixels(ARRAY_GRIP_WIDTH), min.y + height));
    if(row.is_foldout)
    {
        if(row.is_movable)
        {
            ImGui::InvisibleButton("##grip", grip_rect.GetSize());
            begin_array_element_drag(row);
        }
        ImGui::SetCursorScreenPos(ImVec2(grip_rect.Max.x, min.y));
        // The grip took the line: the node centers its text on the frame again.
        ImGui::AlignTextToFramePadding();
        row.text_x = grip_rect.Max.x + ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
        // The node is the last item, so the property menu of the row stays on it.
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_AllowOverlap |
                                         ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        row.is_open = ImGui::TreeNodeEx("##element", flags, "%s", row.label.c_str());
        begin_array_element_drag(row);
    }
    else
    {
        // The whole cell is the handle; the remove button is laid over its right end later.
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##element", row.label_rect.GetSize());
        begin_array_element_drag(row);
        const float text_max_x = row.label_rect.Max.x - (row.is_movable ? container_widgets::get_row_button_width() : 0.0f);
        const ImVec2 text_pos(grip_rect.Max.x, ImFloor(min.y + (height - ImGui::GetFontSize()) * 0.5f));
        row.text_x = text_pos.x;
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),
                                  text_pos,
                                  ImVec2(text_max_x, row.label_rect.Max.y),
                                  text_max_x,
                                  row.label.c_str(),
                                  nullptr,
                                  nullptr);
    }
    if(row.is_movable)
    {
        const bool is_hovered = ImGui::IsMouseHoveringRect(row.label_rect.Min, row.label_rect.Max);
        const ImU32 grip_color = ImGui::GetColorU32(ImGuiCol_Text, is_hovered ? ARRAY_GRIP_HOVERED_ALPHA : ARRAY_GRIP_ALPHA);
        const ImVec2 grip_size = ImGui::CalcTextSize(ICON_MDI_DRAG_VERTICAL);
        ImGui::GetWindowDrawList()->AddText(ImFloor(grip_rect.GetCenter() - grip_size * 0.5f), grip_color, ICON_MDI_DRAG_VERTICAL);
    }
}

//-----------------------------------------------------------------------------
/// <summary>
/// The element takes an element dragged from its own array: over its upper half the dragged one
/// goes before it, over its lower half after it. A line shows where.
/// </summary>
//-----------------------------------------------------------------------------
void handle_array_element_drop(const array_state& state, int index, const ImRect& element_rect, array_edit& edit)
{
    if(!ImGui::BeginDragDropTargetCustom(element_rect, ImGui::GetID("##element_drop")))
    {
        return;
    }
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    array_element_payload dragged{};
    const bool is_element = payload != nullptr && payload->IsDataType(ARRAY_ELEMENT_PAYLOAD) &&
                            payload->DataSize == static_cast<int>(sizeof(dragged));
    if(is_element)
    {
        std::memcpy(&dragged, payload->Data, sizeof(dragged));
    }
    const bool is_below = ImGui::GetMousePos().y > element_rect.GetCenter().y;
    const int insert_before = index + (is_below ? 1 : 0);
    const bool is_own_array = is_element && dragged.array_id == state.id;
    const bool is_moving = insert_before != dragged.index && insert_before != dragged.index + 1;
    if(is_own_array && is_moving && insert_before >= state.readonly_count)
    {
        const float line_y = is_below ? element_rect.Max.y : element_rect.Min.y;
        ImVec4 line_color = imgui_style::get_accent_color();
        line_color.w = 1.0f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(element_rect.Min.x, line_y),
                                            ImVec2(element_rect.Max.x, line_y),
                                            ImGui::GetColorU32(line_color),
                                            ARRAY_DROP_LINE_THICKNESS);
        if(ImGui::AcceptDragDropPayload(ARRAY_ELEMENT_PAYLOAD, ImGuiDragDropFlags_AcceptNoDrawDefaultRect) != nullptr)
        {
            edit.type = array_edit::kind::move;
            edit.index = dragged.index;
            edit.insert_before = insert_before;
        }
    }
    ImGui::EndDragDropTarget();
}

/// Moves one element before a position, both counted before the move.
auto move_array_element(entt::meta_sequence_container& view, int from, int insert_before) -> bool
{
    const int size = static_cast<int>(view.size());
    const bool is_valid = from >= 0 && from < size && insert_before >= 0 && insert_before <= size;
    if(!is_valid || insert_before == from || insert_before == from + 1)
    {
        return false;
    }
    // A copy, taken before the erase: the element itself goes with it. An element that cannot be
    // copied stays where it is rather than be lost.
    const entt::meta_any element_ref = view[static_cast<std::size_t>(from)];
    const entt::meta_any element = element_ref;
    if(!element)
    {
        return false;
    }
    auto from_it = view.begin();
    std::advance(from_it, from);
    view.erase(from_it);
    const int target = insert_before > from ? insert_before - 1 : insert_before;
    auto target_it = view.begin();
    std::advance(target_it, target);
    return static_cast<bool>(view.insert(target_it, element));
}

auto apply_array_edit(entt::meta_sequence_container& view, const array_edit& edit) -> bool
{
    switch(edit.type)
    {
        case array_edit::kind::remove:
        {
            auto it = view.begin();
            std::advance(it, edit.index);
            view.erase(it);
            return true;
        }
        case array_edit::kind::move:
            return move_array_element(view, edit.index, edit.insert_before);
        case array_edit::kind::none:
        default:
            return false;
    }
}

//-----------------------------------------------------------------------------
/// <summary>
/// One element: its label cell (grip, name or foldout), its value, and, while the row is pointed
/// at, a remove button. A remove or a drop onto the row is only noted in edit.
/// </summary>
//-----------------------------------------------------------------------------
auto inspect_array_element(rtti::context& ctx,
                           entt::meta_sequence_container& view,
                           std::size_t index,
                           const meta_any_proxy& var_proxy,
                           const var_info& info,
                           const entt::meta_custom& custom,
                           const array_state& state,
                           array_edit& edit) -> inspect_result
{
    inspect_result result{};
    auto value = view[index];
    const int element_index = static_cast<int>(index);
    auto item_info = info;
    item_info.read_only |= element_index < state.readonly_count;
    const bool is_structure_editable = !item_info.read_only && state.is_resizeable;

    ImGui::PushID(element_index);
    ImGui::Separator();

    // Track array index in property path
    auto& override_ctx = ctx.get_cached<prefab_override_context>();
    const std::string array_index_segment = "[" + std::to_string(index) + "]";
    override_ctx.push_segment(array_index_segment, array_index_segment);

    const meta_any_proxy value_proxy = make_array_element_proxy(var_proxy, value, index);
    array_element_row row{};
    row.array_id = state.id;
    row.index = element_index;
    row.is_movable = is_structure_editable;
    row.is_foldout = !is_inline_array_element(ctx, value.type());
    row.label = make_array_element_label(value, index, row.is_foldout, state.element_label);
    const std::string element_name = "Element " + std::to_string(index);
    const float element_x = ImGui::GetCursorScreenPos().x;

    ImGui::BeginGroup();
    if(row.is_foldout)
    {
        {
            property_layout layout(element_name, [&]() { draw_array_element_label(row); });
        }
        if(row.is_open)
        {
            const float indent = ImMax(0.0f, row.text_x - element_x);
            ImGui::Indent(indent);
            ImGui::PushReadonly(item_info.read_only);
            result |= inspect_var(ctx, value, value_proxy, item_info, custom);
            ImGui::PopReadonly();
            ImGui::Unindent(indent);
        }
    }
    else
    {
        property_layout layout(element_name, [&]() { draw_array_element_label(row); });
        ImGui::PushReadonly(item_info.read_only);
        result |= inspect_var(ctx, value, value_proxy, item_info, custom);
        ImGui::PopReadonly();
    }
    ImGui::EndGroup();
    const ImRect element_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    override_ctx.pop_segment();

    if(is_structure_editable)
    {
        if(container_widgets::draw_row_remove_button(row.label_rect, element_rect, "Remove the element"))
        {
            edit.type = array_edit::kind::remove;
            edit.index = element_index;
        }
        handle_array_element_drop(state, element_index, element_rect, edit);
    }

    ImGui::PopID();
    return result;
}
} // namespace

auto inspect_array(rtti::context& ctx,
                   entt::meta_any& var,
                   const meta_any_proxy& var_proxy,
                   const std::string& name,
                   const std::string& tooltip,
                   const var_info& info,
                   const entt::meta_custom& custom) -> inspect_result
{
    auto view = var.as_sequence_container();
    auto size = view.size();
    inspect_result result{};

    property_layout_group group(name);

    ImGui::BeginGroup();
    {
        property_layout layout;
        layout.set_data(name, tooltip);

        bool open = layout.push_tree_layout();

        array_state state{};
        state.id = ImGui::GetID("##array_elements");
        state.readonly_count = entt::get_attribute_as<int>(custom, "readonly_count");
        state.element_label = entt::get_attribute_as<std::string>(custom, "element_label");
        if(state.element_label.empty())
        {
            state.element_label = "Element";
        }
        const bool is_fixed_size_array = entt::get_attribute_as<bool>(custom, "is_fixed_size_array");
        state.is_resizeable = !is_fixed_size_array && view.resize(size);
        state.is_readonly = info.read_only || !state.is_resizeable;

        container_widgets::header_controls controls{};
        controls.size = size;
        controls.is_count_editable = !state.is_readonly;
        controls.can_add = !state.is_readonly;
        controls.count_tooltip = "Number of elements";
        controls.add_tooltip = "Add an element";
        result |= resize_array(view, size, state, container_widgets::draw_header_controls(controls));

        if(open)
        {
            layout.pop_layout();

            if(size == 0)
            {
                container_widgets::draw_empty_hint();
            }

            array_edit edit{};
            for(std::size_t i = 0; i < size; ++i)
            {
                result |= inspect_array_element(ctx, view, i, var_proxy, info, custom, state, edit);
            }

            if(apply_array_edit(view, edit))
            {
                result.changed = true;
                result.edit_finished = true;
            }
        }
    }
    ImGui::EndGroup();
    ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

    return result;
}

auto inspect_array(rtti::context& ctx,
                   entt::meta_any& var,
                   const meta_any_proxy& var_proxy,
                   const entt::meta_data& prop,
                   const var_info& info,
                   const entt::meta_custom& custom) -> inspect_result
{
    auto name = entt::get_pretty_name(prop);

    auto tooltip = entt::get_attribute_as<std::string>(prop, "tooltip");

    return inspect_array(ctx, var, var_proxy, name, tooltip, info, custom);
}

} // namespace unravel
