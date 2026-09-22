#include "inspectors.h"
#include "inspector.h"
#include "inspector_container_widgets.h"

#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "imgui_widgets/utils.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "reflection/reflection.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// The inspector of associative containers: maps, and sets, which have keys only. A key cannot
// change in place: a new key moves the entry (it is erased and inserted under the new key), and
// only once the edit of the key is done.
namespace unravel
{
namespace
{
// Sizes are in units of the font size.
// As wide as the grip of an array element, so keys line up with the names of array elements.
constexpr float MAP_WARNING_SLOT_WIDTH = 1.1f;
constexpr ImU32 MAP_WARNING_COLOR = IM_COL32(255, 190, 60, 255);
constexpr const char* MAP_NEW_TEXT_KEY = "Key";
constexpr const char* MAP_DUPLICATE_TOOLTIP =
    "Another entry has this key, so this entry keeps its own.\nClick to go back to it.";

/// A key being edited, kept across frames: the field writes it only while it is active, and it is
/// applied only once the edit is done, since a new key moves the entry. A key another entry has
/// stays here, with a warning, until it is changed or given up.
struct map_pending_key
{
    /// The key the entry has in the container.
    entt::meta_any original;
    entt::meta_any edited;
    bool is_duplicate{};
};

/// By the ImGui ID of the row. Only the rows whose key is being edited have one.
std::unordered_map<ImGuiID, map_pending_key> g_map_pending_keys;

/// A change to the entries. Found while they are drawn, it is applied after them: the entries
/// must stay where they are while the loop walks them.
struct map_edit
{
    enum class kind
    {
        none,
        add,
        remove,
        rename
    };

    kind type{kind::none};
    entt::meta_any key;
    /// Rename only.
    entt::meta_any new_key;
};

/// How the container may be edited, the same for all its entries.
struct map_state
{
    bool is_editable{};
    /// A map has a value per key; a set has only keys.
    bool has_values{};
};

/// The key cell of an entry row, filled in by the row's label callback.
struct map_key_cell
{
    ImGuiID row_id{};
    bool is_editable{};
    /// The cell on screen: the remove button goes to its right end.
    ImRect rect{};
};

auto is_free_key(entt::meta_associative_container& view, const entt::meta_any& key) -> bool
{
    return view.find(key) == view.end();
}

/// The first free key of those the container names, if it names any.
auto find_free_candidate_key(entt::meta_associative_container& view, const entt::meta_custom& custom) -> entt::meta_any
{
    const auto* candidates =
        entt::get_attribute(custom, ASSOCIATIVE_KEY_CANDIDATES_ATTRIBUTE).try_cast<std::vector<entt::meta_any>>();
    if(candidates == nullptr)
    {
        return {};
    }
    const auto it = std::find_if(candidates->begin(),
                                 candidates->end(),
                                 [&](const entt::meta_any& candidate)
                                 {
                                     return is_free_key(view, candidate);
                                 });
    return it != candidates->end() ? *it : entt::meta_any{};
}

//-----------------------------------------------------------------------------
/// <summary>
/// A key no entry has yet, for a new entry. When the container names the keys it may take, the
/// first free one of them. Otherwise the default key when it is free, or else the first free
/// number for a numeric key, the first free "Key N" for a text key, the first free value of an
/// enum key.
/// </summary>
/// <returns>The key, empty when none is found</returns>
//-----------------------------------------------------------------------------
auto make_free_key(entt::meta_associative_container& view, const entt::meta_custom& custom) -> entt::meta_any
{
    if(entt::get_attribute(custom, ASSOCIATIVE_KEY_CANDIDATES_ATTRIBUTE))
    {
        return find_free_candidate_key(view, custom);
    }
    const entt::meta_type key_type = view.key_type();
    entt::meta_any key = key_type.construct();
    if(!key || is_free_key(view, key))
    {
        return key;
    }
    // One more candidate than there are entries: one of them is free.
    const auto candidate_count = static_cast<std::int64_t>(view.size()) + 1;
    if(key_type.is_arithmetic())
    {
        for(std::int64_t number = 1; number <= candidate_count; ++number)
        {
            entt::meta_any candidate{number};
            if(candidate.allow_cast(key_type) && is_free_key(view, candidate))
            {
                return candidate;
            }
        }
    }
    else if(key_type == entt::resolve<std::string>())
    {
        for(std::int64_t number = 1; number <= candidate_count; ++number)
        {
            entt::meta_any candidate{fmt::format("{} {}", MAP_NEW_TEXT_KEY, number)};
            if(is_free_key(view, candidate))
            {
                return candidate;
            }
        }
    }
    else if(key_type.is_enum())
    {
        for(auto&& [id, data] : key_type.data())
        {
            entt::meta_any candidate = data.get({});
            if(candidate && is_free_key(view, candidate))
            {
                return candidate;
            }
        }
    }
    return {};
}

//-----------------------------------------------------------------------------
/// <summary>
/// Moves an entry to a new key: its value is copied, the entry erased and inserted anew. Nothing
/// changes when the new key is taken or cannot be a key, so no entry is lost on the way.
/// </summary>
//-----------------------------------------------------------------------------
auto rename_map_key(entt::meta_associative_container& view,
                    bool has_values,
                    const entt::meta_any& key,
                    const entt::meta_any& new_key) -> bool
{
    entt::meta_any converted_key = new_key;
    if(!converted_key.allow_cast(view.key_type()) || !is_free_key(view, converted_key))
    {
        return false;
    }
    auto it = view.find(key);
    if(it == view.end())
    {
        return false;
    }
    entt::meta_any value{};
    if(has_values)
    {
        const auto entry = *it;
        value = entry.second;
        if(!value)
        {
            return false;
        }
    }
    view.erase(key);
    return view.insert(converted_key, value);
}

auto apply_map_edit(entt::meta_associative_container& view,
                    const map_state& state,
                    const map_edit& edit,
                    const entt::meta_custom& custom) -> bool
{
    switch(edit.type)
    {
        case map_edit::kind::add:
        {
            const entt::meta_any key = make_free_key(view, custom);
            if(!key)
            {
                return false;
            }
            return view.insert(key, state.has_values ? view.mapped_type().construct() : entt::meta_any{});
        }
        case map_edit::kind::remove:
            return view.erase(edit.key) > 0;
        case map_edit::kind::rename:
            return rename_map_key(view, state.has_values, edit.key, edit.new_key);
        case map_edit::kind::none:
        default:
            return false;
    }
}

/// The pending key of a row, if it is still about the entry the row shows. Rows are placed by
/// position, so a row that shows another entry now drops what was pending for the old one.
auto find_pending_key(ImGuiID row_id, const entt::meta_any& key) -> map_pending_key*
{
    const auto it = g_map_pending_keys.find(row_id);
    if(it == g_map_pending_keys.end())
    {
        return nullptr;
    }
    if(!(it->second.original == key))
    {
        g_map_pending_keys.erase(it);
        return nullptr;
    }
    return &it->second;
}

/// The key edit of a row is done: back to its key, rename the entry, or, when another entry has
/// the key, keep it pending with a warning.
void commit_pending_key(entt::meta_associative_container& view, const entt::meta_any& key, ImGuiID row_id, map_edit& edit)
{
    const auto it = g_map_pending_keys.find(row_id);
    if(it == g_map_pending_keys.end())
    {
        return;
    }
    map_pending_key& pending = it->second;
    if(pending.edited == key)
    {
        g_map_pending_keys.erase(it);
        return;
    }
    if(!is_free_key(view, pending.edited))
    {
        pending.is_duplicate = true;
        return;
    }
    edit.type = map_edit::kind::rename;
    edit.key = key;
    edit.new_key = pending.edited;
    g_map_pending_keys.erase(it);
}

//-----------------------------------------------------------------------------
/// <summary>
/// The key cell of an entry: a warning slot, then the key's own field. The field edits a copy of
/// the key; the entry follows only once the edit is done.
/// </summary>
//-----------------------------------------------------------------------------
void draw_map_key_cell(rtti::context& ctx,
                       entt::meta_associative_container& view,
                       const entt::meta_any& key,
                       map_key_cell& cell,
                       map_edit& edit)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    cell.rect = ImRect(min, ImVec2(min.x + ImGui::GetContentRegionAvail().x, min.y + height));
    const ImRect warning_rect(min, ImVec2(min.x + container_widgets::to_pixels(MAP_WARNING_SLOT_WIDTH), min.y + height));
    const map_pending_key* pending = find_pending_key(cell.row_id, key);
    entt::meta_any edited_key = pending != nullptr ? pending->edited : key;

    const float remove_width = cell.is_editable ? container_widgets::get_row_button_width() : 0.0f;
    const float key_width = cell.rect.Max.x - warning_rect.Max.x - remove_width - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorScreenPos(ImVec2(warning_rect.Max.x, min.y));
    var_info key_info{};
    key_info.read_only = !cell.is_editable;
    ImGui::PushID("##key");
    ImGui::PushItemWidth(ImMax(1.0f, key_width));
    const inspect_result key_result = inspect_var(ctx, edited_key, make_proxy(edited_key), key_info);
    ImGui::PopItemWidth();
    ImGui::PopID();

    if(key_result.changed)
    {
        map_pending_key& changed = g_map_pending_keys[cell.row_id];
        changed.original = key;
        changed.edited = edited_key;
        changed.is_duplicate = false;
    }
    if(key_result.edit_finished)
    {
        commit_pending_key(view, key, cell.row_id, edit);
    }
    pending = find_pending_key(cell.row_id, key);
    if(pending != nullptr && pending->is_duplicate)
    {
        const container_widgets::icon_button restore{"##restore_key", ICON_MDI_ALERT, MAP_DUPLICATE_TOOLTIP, false, MAP_WARNING_COLOR};
        if(container_widgets::draw_icon_button(restore, warning_rect))
        {
            g_map_pending_keys.erase(cell.row_id);
        }
    }
}

/// A text for the key in the name of a proxy, for the undo history.
auto describe_map_key(const entt::meta_any& key) -> std::string
{
    if(const auto* text = key.try_cast<std::string>())
    {
        return *text;
    }
    entt::meta_any number = key;
    if(number.allow_cast<std::int64_t>())
    {
        return std::to_string(number.cast<std::int64_t>());
    }
    return "?";
}

/// Reaches the value of an entry by its key, each time anew.
auto make_map_value_proxy(const meta_any_proxy& var_proxy, const entt::meta_any& key, const entt::meta_any& value)
    -> meta_any_proxy
{
    const entt::meta_any key_copy = key;
    meta_any_proxy value_proxy;
    value_proxy.impl->parent = var_proxy.impl;
    value_proxy.impl->type_name = entt::get_pretty_name(value.type());
    value_proxy.impl->name = fmt::format("{}[{}]", var_proxy.impl->name, describe_map_key(key));
    value_proxy.impl->getter = [parent_proxy = var_proxy, key_copy](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            auto view = var.as_associative_container();
            auto it = view.find(key_copy);
            if(it != view.end())
            {
                // A copy: the container is gone once the getter returns.
                const auto entry = *it;
                result = entry.second;
                return true;
            }
        }
        return false;
    };
    value_proxy.impl->setter =
        [parent_proxy = var_proxy, key_copy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            auto view = var.as_associative_container();
            auto it = view.find(key_copy);
            if(it != view.end())
            {
                auto entry = *it;
                entry.second.assign(value);
                return parent_proxy.impl->setter(parent_proxy, var, execution_count);
            }
        }
        return false;
    };
    return value_proxy;
}

//-----------------------------------------------------------------------------
/// <summary>
/// One entry: its key in the label cell, its value in the value cell, and, while the row is
/// pointed at, a remove button. A remove or a new key is only noted in edit.
/// </summary>
//-----------------------------------------------------------------------------
auto inspect_map_entry(rtti::context& ctx,
                       entt::meta_associative_container& view,
                       std::pair<entt::meta_any, entt::meta_any>& entry,
                       int index,
                       const meta_any_proxy& var_proxy,
                       const var_info& info,
                       const entt::meta_custom& custom,
                       const map_state& state,
                       map_edit& edit) -> inspect_result
{
    inspect_result result{};
    const entt::meta_any& key = entry.first;
    ImGui::PushID(index);
    ImGui::Separator();

    map_key_cell cell{};
    cell.row_id = ImGui::GetID("##entry");
    cell.is_editable = state.is_editable;
    const std::string entry_name = "Entry " + std::to_string(index);

    ImGui::BeginGroup();
    {
        property_layout layout(entry_name, [&]() { draw_map_key_cell(ctx, view, key, cell, edit); });
        if(state.has_values)
        {
            entt::meta_any value = entry.second.as_ref();
            const meta_any_proxy value_proxy = make_map_value_proxy(var_proxy, key, value);
            ImGui::PushReadonly(info.read_only);
            result |= inspect_var(ctx, value, value_proxy, info, custom);
            ImGui::PopReadonly();
        }
    }
    ImGui::EndGroup();
    const ImRect row_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    if(state.is_editable && container_widgets::draw_row_remove_button(cell.rect, row_rect, "Remove the entry"))
    {
        edit.type = map_edit::kind::remove;
        edit.key = key;
    }

    ImGui::PopID();
    return result;
}
} // namespace

auto inspect_associative_container(rtti::context& ctx,
                                   entt::meta_any& var,
                                   const meta_any_proxy& var_proxy,
                                   const std::string& name,
                                   const std::string& tooltip,
                                   const var_info& info,
                                   const entt::meta_custom& custom) -> inspect_result
{
    auto view = var.as_associative_container();
    inspect_result result{};
    map_state state{};
    state.is_editable = !info.read_only;
    state.has_values = static_cast<bool>(view.mapped_type());

    property_layout_group group(name);

    ImGui::BeginGroup();
    {
        property_layout layout;
        layout.set_data(name, tooltip);

        const bool open = layout.push_tree_layout();

        container_widgets::header_controls controls{};
        controls.size = view.size();
        controls.can_add = state.is_editable;
        controls.count_tooltip = "Number of entries";
        controls.add_tooltip = "Add an entry";
        map_edit edit{};
        if(container_widgets::draw_header_controls(controls).is_add_pressed)
        {
            edit.type = map_edit::kind::add;
        }

        if(open)
        {
            layout.pop_layout();

            if(view.size() == 0)
            {
                container_widgets::draw_empty_hint();
            }

            int index = 0;
            for(auto entry : view)
            {
                result |= inspect_map_entry(ctx, view, entry, index, var_proxy, info, custom, state, edit);
                ++index;
            }
        }

        if(apply_map_edit(view, state, edit, custom))
        {
            result.changed = true;
            result.edit_finished = true;
        }
    }
    ImGui::EndGroup();
    ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

    return result;
}

auto inspect_associative_container(rtti::context& ctx,
                                   entt::meta_any& var,
                                   const meta_any_proxy& var_proxy,
                                   const entt::meta_data& prop,
                                   const var_info& info,
                                   const entt::meta_custom& custom) -> inspect_result
{
    auto name = entt::get_pretty_name(prop);

    auto tooltip = entt::get_attribute_as<std::string>(prop, "tooltip");

    return inspect_associative_container(ctx, var, var_proxy, name, tooltip, info, custom);
}

} // namespace unravel
