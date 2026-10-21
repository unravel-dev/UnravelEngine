#include "project_settings_panel.h"
#include "../panel.h"
#include "../panels_defs.h"

#include <editor/hub/panels/deploy_panel/deploy_panel_widgets.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector_container_widgets.h>
#include <editor/hub/panels/profile_labels.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/system/project_manager.h>
#include <engine/engine.h>
#include <engine/meta/settings/settings.hpp>
#include <engine/input/input.h>
#include <engine/settings/boot_config.h>

#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <imgui_widgets/keyboard/imgui_keyboard.h>
#include <imgui_widgets/keyboard/imgui_mouse.h>
#include <imgui_widgets/keyboard/imgui_gamepad.h>

#include <editor/imgui/integration/backend/imgui_impl_ospp.h>

namespace unravel
{

namespace
{
// Sizes are in units of the font size.
constexpr float INPUT_ENUM_LIST_HEIGHT = 11.0f;
constexpr float INPUT_ENUM_FILTER_WIDTH = 8.0f;
constexpr float INPUT_MAPPING_GAP = 0.35f;
constexpr float INPUT_DRAG_SPEED = 0.05f;
constexpr float INPUT_MUTED_ALPHA = 0.55f;
constexpr const char* INPUT_NEW_ACTION_NAME = "New Action";
constexpr const char* REVERT_TO_PROJECT_LABEL = "Revert to Project Settings";
constexpr const char* PROJECT_SETTINGS_LABEL = "Project Settings";

auto to_pixels(float font_units) -> float
{
    return ImFloor(ImGui::GetFontSize() * font_units);
}

auto to_os_key(input::key_code code) -> os::key::code
{
    return static_cast<os::key::code>(code);
}

auto from_os_key(os::key::code code) -> input::key_code
{
    return static_cast<input::key_code>(code);
}

/// A muted line, wrapped at the window's edge.
void draw_muted_text(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_Text, INPUT_MUTED_ALPHA));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

/// An icon button that takes its place in the layout, unlike container_widgets::draw_icon_button.
auto draw_inline_icon_button(const char* id, const char* icon, const char* tooltip, bool is_destructive = false) -> bool
{
    const float side = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect rect(min, min + ImVec2(side, side));
    ImGui::ItemSize(rect);
    return container_widgets::draw_icon_button({id, icon, tooltip, is_destructive}, rect);
}

void mark_edited(inspect_result& result)
{
    result.changed = true;
    result.edit_finished = true;
}

template<typename EnumT, typename ToStringFn>
auto draw_enum_combo(const char* id, EnumT& current_value, const EnumT* all_values, size_t count, ToStringFn stringify)
    -> bool
{
    const std::string preview_text = stringify(current_value);
    bool is_changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::BeginCombo(id, preview_text.c_str()))
    {
        for(size_t i = 0; i < count; ++i)
        {
            const EnumT candidate = all_values[i];
            const bool is_selected = candidate == current_value;
            if(ImGui::Selectable(stringify(candidate).c_str(), is_selected) && !is_selected)
            {
                current_value = candidate;
                is_changed = true;
            }
            if(is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::DrawItemActivityOutline();
    return is_changed;
}

auto get_key_name(os::key::code key) -> std::string
{
    std::string name = os::key::to_string(key);
    return name.empty() ? std::string("None") : name;
}

/// A button naming the key; a click opens a keyboard to pick another one on.
auto draw_key_selector(const char* id, os::key::code& selected_value, float width) -> bool
{
    ImGui::PushID(id);
    const std::string current_name = get_key_name(selected_value);

    constexpr const char* popup_id = "Key Selector";
    bool is_changed = false;
    if(ImGui::Button(current_name.c_str(), ImVec2(width, ImGui::GetFrameHeight())))
    {
        ImGui::OpenPopup(popup_id);
    }
    ImGui::SetItemTooltipEx("%s", "Click, then press or click a key");

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiViewport* viewport = window->WasActive ? window->Viewport : ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if(ImGui::BeginPopup(popup_id, ImGuiWindowFlags_Popup))
    {
        ImKeyboard::Highlight(ImGui_ImplOSPP_KeycodeToImGuiKey(selected_value), true);
        ImKeyboard::Keyboard(ImKeyboard::ImGuiKeyboardLayout_Qwerty, ImKeyboard::ImGuiKeyboardFlags_Recordable);
        for(auto& key : ImKeyboard::GetRecordedKeys())
        {
            selected_value = ImGui_ImplOSPP_ImGuiKeyToKeycode(key);
            is_changed = true;
        }
        ImKeyboard::ClearHighlights();
        ImKeyboard::ClearRecorded();
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return is_changed;
}

/// A button naming the value; a click opens a searchable list of all of them.
template<typename EnumT, typename ToStringFn, typename FromIntFn, typename GetDescriptionFn>
auto draw_enum_selector(const char* id,
                        EnumT& selected_value,
                        int enum_count,
                        ToStringFn stringify,
                        FromIntFn from_int,
                        GetDescriptionFn get_description) -> bool
{
    static ImGuiTextFilter filter;

    ImGui::PushID(id);
    std::string current_name = stringify(selected_value);
    if(current_name.empty())
    {
        current_name = "None";
    }

    constexpr const char* popup_id = "Enum Selector";
    bool is_changed = false;
    if(ImGui::Button(current_name.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight())))
    {
        filter.Clear();
        ImGui::OpenPopup(popup_id);
    }
    const std::string description = get_description(selected_value);
    if(!description.empty())
    {
        ImGui::SetItemTooltipEx("%s", description.c_str());
    }

    if(ImGui::BeginPopup(popup_id))
    {
        if(ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::DrawFilterWithHint(filter, ICON_MDI_MAGNIFY " Search...", to_pixels(INPUT_ENUM_FILTER_WIDTH));
        ImGui::DrawItemActivityOutline();
        ImGui::Separator();

        if(ImGui::BeginChild("##enum_values", ImVec2(0.0f, to_pixels(INPUT_ENUM_LIST_HEIGHT)), ImGuiChildFlags_Borders))
        {
            for(int i = 0; i < enum_count; ++i)
            {
                const EnumT value = from_int(i);
                const std::string name = stringify(value);
                if(name.empty() || !filter.PassFilter(name.c_str()))
                {
                    continue;
                }
                if(ImGui::Selectable(name.c_str(), static_cast<int>(selected_value) == i))
                {
                    selected_value = value;
                    is_changed = true;
                    ImGui::CloseCurrentPopup();
                }
                const std::string value_description = get_description(value);
                if(!value_description.empty())
                {
                    ImGui::SetItemTooltipEx("%s", value_description.c_str());
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return is_changed;
}

auto draw_drag_float(const char* id, float& value) -> inspect_result
{
    inspect_result result{};
    ImGui::SetNextItemWidth(-FLT_MIN);
    result.changed = ImGui::DragFloat(id, &value, INPUT_DRAG_SPEED);
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DrawItemActivityOutline();
    return result;
}

auto draw_input_type_row(input::input_type& type) -> inspect_result
{
    inspect_result result{};
    property_layout layout("Type", "A button is down or up; an axis reads a value.");
    const input::input_type types[] = {input::input_type::button, input::input_type::axis};
    if(draw_enum_combo("##type",
                       type,
                       types,
                       IM_ARRAYSIZE(types),
                       [](input::input_type value)
                       {
                           return input::to_string(value);
                       }))
    {
        mark_edited(result);
    }
    return result;
}

auto draw_axis_range_row(input::axis_range& range) -> inspect_result
{
    inspect_result result{};
    property_layout layout("Range", "Which part of the axis moves the action.");
    const input::axis_range ranges[] = {input::axis_range::full, input::axis_range::positive, input::axis_range::negative};
    if(draw_enum_combo("##range",
                       range,
                       ranges,
                       IM_ARRAYSIZE(ranges),
                       [](input::axis_range value)
                       {
                           return input::to_string(value);
                       }))
    {
        mark_edited(result);
    }
    return result;
}

/// The keys that must be held as well: one button each, then a button to add one.
auto draw_modifiers(std::vector<input::key_code>& modifiers) -> inspect_result
{
    inspect_result result{};
    int index_to_remove = -1;
    const ImGuiStyle& style = ImGui::GetStyle();
    for(int i = 0; i < static_cast<int>(modifiers.size()); ++i)
    {
        ImGui::PushID(i);
        auto os_key = to_os_key(modifiers[i]);
        const float width = ImGui::CalcTextSize(get_key_name(os_key).c_str()).x + style.FramePadding.x * 4.0f;
        if(draw_key_selector("##modifier", os_key, width))
        {
            modifiers[i] = from_os_key(os_key);
            mark_edited(result);
        }
        ImGui::SameLine(0.0f, 0.0f);
        if(draw_inline_icon_button("##remove_modifier", ICON_MDI_CLOSE, "Remove the modifier", true))
        {
            index_to_remove = i;
        }
        ImGui::SameLine();
        ImGui::PopID();
    }
    if(draw_inline_icon_button("##add_modifier", ICON_MDI_PLUS, "Add a modifier key"))
    {
        modifiers.emplace_back();
        mark_edited(result);
    }
    if(index_to_remove >= 0)
    {
        modifiers.erase(modifiers.begin() + index_to_remove);
        mark_edited(result);
    }
    return result;
}

auto draw_keyboard_mapping(input::keyboard_action_map::key_entry& mapping) -> inspect_result
{
    inspect_result result{};
    {
        property_layout layout("Key", "The key that triggers the action.");
        auto os_key = to_os_key(mapping.key);
        if(draw_key_selector("##key", os_key, ImGui::GetContentRegionAvail().x))
        {
            mapping.key = from_os_key(os_key);
            mark_edited(result);
        }
    }
    {
        property_layout layout("Modifiers", "Keys that must be held down as well.");
        result |= draw_modifiers(mapping.modifiers);
    }
    {
        property_layout layout("Analog Value", "The value the action reads while the key is down.");
        result |= draw_drag_float("##analog_value", mapping.analog_value);
    }
    return result;
}

auto draw_gamepad_mapping(input::gamepad_action_map::gamepad_entry& mapping) -> inspect_result
{
    inspect_result result = draw_input_type_row(mapping.type);
    if(mapping.type == input::input_type::axis)
    {
        result |= draw_axis_range_row(mapping.range);
        {
            property_layout layout("Axis", "The stick or trigger that moves the action.");
            auto axis = static_cast<input::gamepad_axis>(mapping.value);
            if(draw_enum_selector(
                   "##axis",
                   axis,
                   static_cast<int>(input::gamepad_axis::count),
                   [](input::gamepad_axis value)
                   {
                       return input::to_string(value);
                   },
                   [](int value)
                   {
                       return static_cast<input::gamepad_axis>(value);
                   },
                   [](input::gamepad_axis)
                   {
                       return std::string{};
                   }))
            {
                mapping.value = static_cast<uint32_t>(axis);
                mark_edited(result);
            }
        }
        {
            property_layout layout("Min Analog Value", "The value the action reads at one end of the range.");
            result |= draw_drag_float("##min_analog_value", mapping.min_analog_value);
        }
        {
            property_layout layout("Max Analog Value", "The value the action reads at the other end of the range.");
            result |= draw_drag_float("##max_analog_value", mapping.max_analog_value);
        }
        return result;
    }

    property_layout layout("Button", "The button that triggers the action.");
    auto button = static_cast<input::gamepad_button>(mapping.value);
    if(draw_enum_selector(
           "##button",
           button,
           static_cast<int>(input::gamepad_button::count),
           [](input::gamepad_button value)
           {
               return input::to_string(value);
           },
           [](int value)
           {
               return static_cast<input::gamepad_button>(value);
           },
           [](input::gamepad_button value)
           {
               return std::string(input::get_description(value));
           }))
    {
        mapping.value = static_cast<uint32_t>(button);
        mark_edited(result);
    }
    return result;
}

auto draw_mouse_mapping(input::mouse_action_map::mouse_entry& mapping) -> inspect_result
{
    inspect_result result = draw_input_type_row(mapping.type);
    if(mapping.type == input::input_type::axis)
    {
        result |= draw_axis_range_row(mapping.range);
        property_layout layout("Axis", "The mouse movement that moves the action.");
        auto axis = static_cast<input::mouse_axis>(mapping.value);
        const input::mouse_axis axes[] = {input::mouse_axis::x, input::mouse_axis::y, input::mouse_axis::scroll};
        if(draw_enum_combo("##axis",
                           axis,
                           axes,
                           IM_ARRAYSIZE(axes),
                           [](input::mouse_axis value)
                           {
                               return input::to_string(value);
                           }))
        {
            mapping.value = static_cast<uint32_t>(axis);
            mark_edited(result);
        }
        return result;
    }

    property_layout layout("Button", "The button that triggers the action.");
    auto button = static_cast<input::mouse_button>(mapping.value);
    if(draw_enum_selector(
           "##button",
           button,
           static_cast<int>(input::mouse_button::count),
           [](input::mouse_button value)
           {
               return input::to_string(value);
           },
           [](int value)
           {
               return static_cast<input::mouse_button>(value);
           },
           [](input::mouse_button)
           {
               return std::string{};
           }))
    {
        mapping.value = static_cast<uint32_t>(button);
        mark_edited(result);
    }
    return result;
}

/// A change to the actions of a device. Found while they are drawn, it is applied after them:
/// the map must stay as it is while the loop walks it.
struct action_list_edit
{
    bool is_add_pressed{};
    std::string removed_action;
    std::string renamed_from;
    std::string renamed_to;
};

/// The name being typed into the name field of an action. A new name moves the action to
/// another key, so it is applied only once the edit is done.
struct pending_action_name
{
    ImGuiID field_id{};
    std::string text;
};

pending_action_name g_pending_action_name;
/// An action that folds out the next time it is drawn: one just added or renamed.
std::string g_action_to_open;

/// The name field of an action. Returns the new name once the edit is done, empty otherwise.
auto draw_action_name_field(const std::string& action) -> std::string
{
    const ImGuiID field_id = ImGui::GetID("##action_name");
    std::string text = g_pending_action_name.field_id == field_id ? g_pending_action_name.text : action;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::InputTextWidget<128>("##action_name", text))
    {
        g_pending_action_name = {field_id, text};
    }
    ImGui::DrawItemActivityOutline();
    if(!ImGui::IsItemDeactivated() || g_pending_action_name.field_id != field_id)
    {
        return {};
    }
    std::string new_name = g_pending_action_name.text;
    g_pending_action_name = {};
    return new_name;
}

template<typename Entry>
auto make_unique_action_name(const std::map<input::action_id_t, std::vector<Entry>>& entries) -> std::string
{
    std::string name = INPUT_NEW_ACTION_NAME;
    for(int number = 2; entries.contains(name); ++number)
    {
        name = fmt::format("{} {}", INPUT_NEW_ACTION_NAME, number);
    }
    return name;
}

/// The folding header of an action: its name, how many mappings it has, and a remove button.
auto draw_action_header(const std::string& action, std::size_t mapping_count, action_list_edit& edit) -> bool
{
    if(action == g_action_to_open)
    {
        ImGui::SetNextItemOpen(true);
        g_action_to_open.clear();
    }
    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                                     ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
    const bool is_open = ImGui::TreeNodeEx("##action", flags, "%s", action.c_str());
    const ImRect header_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    const float side = header_rect.GetHeight();
    const ImRect remove_rect(ImVec2(header_rect.Max.x - side, header_rect.Min.y), header_rect.Max);
    if(container_widgets::draw_icon_button({"##remove_action", ICON_MDI_DELETE_OUTLINE, "Remove the action", true}, remove_rect))
    {
        edit.removed_action = action;
    }
    const std::string count_text = fmt::format("{} {}", mapping_count, mapping_count == 1 ? "mapping" : "mappings");
    const ImVec2 count_size = ImGui::CalcTextSize(count_text.c_str());
    const ImVec2 count_pos(remove_rect.Min.x - ImGui::GetStyle().ItemSpacing.x - count_size.x,
                           header_rect.GetCenter().y - count_size.y * 0.5f);
    ImGui::GetWindowDrawList()->AddText(ImFloor(count_pos), ImGui::GetColorU32(ImGuiCol_Text, INPUT_MUTED_ALPHA), count_text.c_str());
    return is_open;
}

/// What an open action shows: its name, then each mapping under a line of its own.
template<typename Entry, typename DrawMapping>
auto draw_action_body(const std::string& action,
                      std::vector<Entry>& mappings,
                      DrawMapping& draw_mapping,
                      action_list_edit& edit) -> inspect_result
{
    inspect_result result{};
    {
        property_layout layout("Name", "What scripts ask the input system for.");
        const std::string new_name = draw_action_name_field(action);
        if(!new_name.empty() && new_name != action)
        {
            edit.renamed_from = action;
            edit.renamed_to = new_name;
        }
    }

    int index_to_remove = -1;
    for(int i = 0; i < static_cast<int>(mappings.size()); ++i)
    {
        ImGui::PushID(i);
        ImGui::Dummy(ImVec2(0.0f, to_pixels(INPUT_MAPPING_GAP)));
        ImGui::Separator();
        const ImVec2 row_min = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetFrameHeight();
        ImGui::AlignTextToFramePadding();
        draw_muted_text(fmt::format("Mapping {}", i + 1).c_str());
        const ImVec2 remove_min(row_min.x + ImGui::GetContentRegionAvail().x - side, row_min.y);
        if(container_widgets::draw_icon_button({"##remove_mapping", ICON_MDI_DELETE_OUTLINE, "Remove the mapping", true},
                                               ImRect(remove_min, remove_min + ImVec2(side, side))))
        {
            index_to_remove = i;
        }
        result |= draw_mapping(mappings[static_cast<std::size_t>(i)]);
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0.0f, to_pixels(INPUT_MAPPING_GAP)));
    if(ImGui::Button(ICON_MDI_PLUS " Add Mapping"))
    {
        mappings.emplace_back();
        mark_edited(result);
    }
    if(index_to_remove >= 0)
    {
        mappings.erase(mappings.begin() + index_to_remove);
        mark_edited(result);
    }
    return result;
}

template<typename Entry>
void apply_action_list_edit(std::map<input::action_id_t, std::vector<Entry>>& entries,
                            const action_list_edit& edit,
                            inspect_result& result)
{
    if(edit.is_add_pressed)
    {
        const std::string name = make_unique_action_name(entries);
        entries[name].emplace_back();
        g_action_to_open = name;
        mark_edited(result);
    }
    if(!edit.removed_action.empty())
    {
        entries.erase(edit.removed_action);
        mark_edited(result);
    }
    // A name another action has is refused: the rename would drop that action.
    if(!edit.renamed_to.empty() && !entries.contains(edit.renamed_to))
    {
        auto node = entries.extract(edit.renamed_from);
        if(!node.empty())
        {
            node.key() = edit.renamed_to;
            entries.insert(std::move(node));
            g_action_to_open = edit.renamed_to;
            mark_edited(result);
        }
    }
}

/// The actions of one device: how many there are and a button to add one, then every action,
/// folding out into its name and its mappings.
template<typename Entry, typename DrawMapping>
auto draw_action_list(std::map<input::action_id_t, std::vector<Entry>>& entries, DrawMapping draw_mapping)
    -> inspect_result
{
    inspect_result result{};
    action_list_edit edit{};

    const char* add_label = ICON_MDI_PLUS " Add Action";
    const float add_width = ImGui::CalcTextSize(add_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float row_x = ImGui::GetCursorPosX();
    const float row_width = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    draw_muted_text(fmt::format("{} {}", entries.size(), entries.size() == 1 ? "action" : "actions").c_str());
    ImGui::SameLine(row_x + row_width - add_width);
    edit.is_add_pressed = ImGui::Button(add_label);
    ImGui::Dummy(ImVec2(0.0f, to_pixels(INPUT_MAPPING_GAP)));

    for(auto& [action, mappings] : entries)
    {
        ImGui::PushID(action.c_str());
        if(draw_action_header(action, mappings.size(), edit))
        {
            result |= draw_action_body(action, mappings, draw_mapping, edit);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    apply_action_list_edit(entries, edit, result);
    return result;
}

/// Says that a category is shared by every profile, while a profile is edited; the category is then drawn
/// read only.
auto draw_shared_category_note(const settings_edit_target& target, const char* what) -> bool
{
    if(target.profile.empty())
    {
        return false;
    }
    draw_muted_text(fmt::format("Every profile shares {}. Edit them with {} selected above.", what, PROJECT_SETTINGS_LABEL).c_str());
    ImGui::Spacing();
    return true;
}

/// The tooltip line of a setting every profile shares, read-only while a profile is edited.
constexpr const char* SHARED_SETTING_NOTE = "Every profile shares this setting. Change it with Project Settings selected above.";

/**
 * @brief What the settings drawn are marked with: the edited profile's overrides, or, while the project
 * settings are edited, the overrides of the profile the editor previews, which hide those edits in the editor.
 * Empty (no get_mark) when nothing is marked.
 */
auto make_profile_marks(rtti::context& ctx, settings_edit_target& target) -> property_override_marks
{
    auto& pm = ctx.get_cached<project_manager>();
    property_override_marks marks;
    const bool is_profile_edited = !target.profile.empty();
    const std::string profile = is_profile_edited ? target.profile : pm.get_preview_profile();
    if(pm.get_profiles().find(profile) == nullptr)
    {
        return marks;
    }
    marks.get_mark = [&pm, profile](const std::string& path)
    {
        const auto* found = pm.get_profiles().find(profile);
        return found != nullptr ? get_override_mark(found->settings, path) : override_mark::none;
    };
    marks.revert = [&ctx, &pm, &target, profile](const std::string& path)
    {
        target.error = pm.revert_profile_settings(ctx, profile, path).error;
    };
    if(is_profile_edited)
    {
        marks.revert_label = REVERT_TO_PROJECT_LABEL;
        marks.overridden_note = fmt::format("Profile {} overrides this setting.", profile);
        marks.contains_note = fmt::format("Profile {} overrides settings in this group.", profile);
        marks.is_shared = [](const std::string& path)
        {
            return !deployment::is_overridable_settings_path(path);
        };
        marks.shared_note = SHARED_SETTING_NOTE;
        return marks;
    }
    marks.style = override_style::shadowing;
    marks.revert_label = fmt::format("Revert {} to Project Settings", profile);
    marks.overridden_note = fmt::format("The editor previews profile {}, which overrides this setting, so the editor "
                                        "shows {}'s value.",
                                        profile,
                                        profile);
    marks.contains_note = fmt::format("The editor previews profile {}, which overrides settings in this group.", profile);
    return marks;
}

/// How many settings within `root` the edited profile overrides.
auto count_profile_overrides(rtti::context& ctx, const settings_edit_target& target, const char* root) -> std::size_t
{
    const auto* profile = ctx.get_cached<project_manager>().get_profiles().find(target.profile);
    if(profile == nullptr)
    {
        return 0;
    }
    const auto& paths = profile->settings.get_paths();
    return static_cast<std::size_t>(std::count_if(paths.begin(),
                                                  paths.end(),
                                                  [root](const std::string& path)
                                                  {
                                                      return serialization::is_path_within(path, root);
                                                  }));
}

void draw_error_line(const std::string& error)
{
    ImGui::Spacing();
    deploy_widgets::draw_icon_line(ICON_MDI_ALERT_CIRCLE_OUTLINE,
                                   deploy_widgets::WARNING_COLOR,
                                   error.c_str(),
                                   ImGui::GetColorU32(ImGuiCol_Text));
}

void draw_input_settings(rtti::context& ctx, settings_edit_target& target)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& actions = pm.get_settings().input.actions;
    const bool is_shared = draw_shared_category_note(target, "the input actions");
    ImGui::PushReadonly(is_shared);

    inspect_result result{};
    if(ImGui::BeginTabBar("##input_devices"))
    {
        if(ImGui::BeginTabItem(ICON_MDI_KEYBOARD " Keyboard"))
        {
            result |= draw_action_list(actions.keyboard_map.entries_by_action_id_, &draw_keyboard_mapping);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(ICON_MDI_MOUSE " Mouse"))
        {
            result |= draw_action_list(actions.mouse_map.entries_by_action_id_, &draw_mouse_mapping);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem(ICON_MDI_GAMEPAD_VARIANT " Gamepad"))
        {
            result |= draw_action_list(actions.gamepad_map.entries_by_action_id_, &draw_gamepad_mapping);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::PopReadonly();
    if(result.edit_finished && !is_shared)
    {
        pm.save_project_settings(ctx);
    }
}

/// The settings a category draws: the project settings, or the copy of the profile's, taken again on every
/// frame no item is being edited.
auto get_shown_settings(rtti::context& ctx, settings_edit_target& target) -> settings&
{
    auto& pm = ctx.get_cached<project_manager>();
    if(target.profile.empty())
    {
        return pm.get_settings();
    }
    if(!ImGui::IsAnyItemActive())
    {
        target.shown = pm.get_profile_settings(target.profile);
        target.before = target.shown;
    }
    return target.shown;
}

/**
 * @brief Inspects one group of the settings and saves it once an edit is done.
 *
 * With a profile edited, the group drawn is the profile's, the properties it overrides are marked, and an
 * edit becomes an override of that profile.
 * @param root The group's path in the settings document, such as "assets/texture".
 * @param get The group in a settings object.
 */
template<typename Getter>
auto inspect_and_save(rtti::context& ctx, settings_edit_target& target, const char* root, Getter get) -> bool
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& shown = get_shown_settings(ctx, target);
    auto marks = make_profile_marks(ctx, target);
    hpp::optional<property_override_scope> marked;
    if(marks.get_mark)
    {
        marked.emplace(ctx, root, std::move(marks));
    }
    if(target.profile.empty())
    {
        if(!inspect(ctx, get(shown)).edit_finished)
        {
            return false;
        }
        pm.save_project_settings(ctx);
        return true;
    }
    inspect_result result;
    {
        // The shown settings are a copy, taken again every frame: an undo step could not reach them.
        const inspect_without_undo_scope no_undo;
        result = inspect(ctx, get(shown));
    }
    if(!result.edit_finished)
    {
        return false;
    }
    target.error = pm.override_profile_settings(ctx, target.profile, target.before, shown).error;
    return target.error.empty();
}

void ask_restart(const char* what_changed)
{
    ImBox::ShowQuestion("Restart required",
                        fmt::format("The {} was changed.\n\n"
                                    "The editor must restart for the new backend to take effect.\n"
                                    "Restart now?",
                                    what_changed),
                        [](ImBox::ModalResult result)
                        {
                            if(result == ImBox::ModalResult::Yes)
                            {
                                engine::request_restart();
                            }
                        });
}

void draw_application_settings(rtti::context& ctx, settings_edit_target& target)
{
    inspect_and_save(ctx, target, "app", [](settings& s) -> auto& { return s.app; });
}

void draw_resolution_settings(rtti::context& ctx, settings_edit_target& target)
{
    if(draw_shared_category_note(target, "the Game view's resolutions"))
    {
        ImGui::PushReadonly(true);
        inspect(ctx, ctx.get_cached<project_manager>().get_settings().resolution);
        ImGui::PopReadonly();
        return;
    }
    inspect_and_save(ctx, target, "resolution", [](settings& s) -> auto& { return s.resolution; });
}

void draw_asset_settings(rtti::context& ctx, settings_edit_target& target)
{
    inspect_and_save(ctx, target, "assets/texture", [](settings& s) -> auto& { return s.assets.texture; });
    if(!target.profile.empty())
    {
        draw_muted_text("The profile's deploys compile its textures with these defaults, into its own cache.");
        return;
    }
    property_layout layout("Textures", "Imports every texture again, with the defaults above.");
    if(ImGui::Button(ICON_MDI_REFRESH " Recompile All", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
    {
        editor_actions::recompile_textures();
    }
}

void draw_graphics_settings(rtti::context& ctx, settings_edit_target& target)
{
    auto& settings = ctx.get_cached<project_manager>().get_settings();
    const preferred_renderer renderer_before = settings.graphics.renderer.get_for_current_platform();
    // A profile's renderer is its builds'; the editor keeps the one it started with.
    if(inspect_and_save(ctx, target, "graphics", [](unravel::settings& s) -> auto& { return s.graphics; }) && target.profile.empty() &&
       settings.graphics.renderer.get_for_current_platform() != renderer_before)
    {
        ask_restart("renderer backend for this platform");
    }
}

void draw_global_illumination_settings(rtti::context& ctx, settings_edit_target& target)
{
    inspect_and_save(ctx, target, "global_illumination", [](settings& s) -> auto& { return s.global_illumination; });
}

void draw_splash_settings(rtti::context& ctx, settings_edit_target& target)
{
    inspect_and_save(ctx, target, "splash", [](settings& s) -> auto& { return s.splash; });
}

void draw_standalone_settings(rtti::context& ctx, settings_edit_target& target)
{
    inspect_and_save(ctx, target, "standalone", [](settings& s) -> auto& { return s.standalone; });
}

void draw_layers_settings(rtti::context& ctx, settings_edit_target& target)
{
    if(draw_shared_category_note(target, "the layers"))
    {
        ImGui::PushReadonly(true);
        inspect(ctx, ctx.get_cached<project_manager>().get_settings().layer);
        ImGui::PopReadonly();
        return;
    }
    inspect_and_save(ctx, target, "layer", [](settings& s) -> auto& { return s.layer; });
}

void draw_physics_settings(rtti::context& ctx, settings_edit_target& target)
{
    auto& settings = ctx.get_cached<project_manager>().get_settings();
    const physics_backend_type backend_before = settings.physics.backend;
    if(!target.profile.empty())
    {
        draw_muted_text("Every profile shares the physics backend, which gameplay is tuned to. The simulation step can "
                        "differ per profile.");
        ImGui::Spacing();
    }
    if(inspect_and_save(ctx, target, "physics", [](unravel::settings& s) -> auto& { return s.physics; }) && target.profile.empty() &&
       resolve_physics_backend(settings.physics.backend) != resolve_physics_backend(backend_before))
    {
        ask_restart("physics backend");
    }
}

auto make_project_settings_categories(rtti::context& ctx, settings_edit_target& target) -> std::vector<settings_category>
{
    const auto draws = [&target](void (*draw)(rtti::context&, settings_edit_target&))
    {
        return [&target, draw](rtti::context& ctx)
        {
            draw(ctx, target);
            if(!target.error.empty())
            {
                draw_error_line(target.error);
            }
        };
    };
    const auto counts = [&ctx, &target](const char* root)
    {
        return [&ctx, &target, root]()
        {
            return count_profile_overrides(ctx, target, root);
        };
    };
    return {
        {"Application",
         ICON_MDI_APPLICATION,
         "Who makes the game and what it is called.",
         "company product version name",
         draws(&draw_application_settings),
         counts("app")},
        {"Resolution",
         ICON_MDI_ASPECT_RATIO,
         "The resolutions and aspect ratios the Game view offers.",
         "aspect ratio width height screen game view",
         draws(&draw_resolution_settings),
         counts("resolution")},
        {"Assets",
         ICON_MDI_PACKAGE_VARIANT_CLOSED,
         "Defaults for importing assets.",
         "texture import compression max size recompile",
         draws(&draw_asset_settings),
         counts("assets")},
        {"Graphics",
         ICON_MDI_CHIP,
         "The renderer backend of each platform, the render scale, the features scenes may use, particle budgets, "
         "static mesh batching, and how GPU memory is paged.",
         "renderer backend directx vulkan opengl metal render scale resolution allowed features limits global "
         "illumination reflections ambient occlusion bloom anti-aliasing fxaa taa shadows soft contact particles "
         "static mesh batching instancing draw calls gpu memory eviction paging budget mobile quality",
         draws(&draw_graphics_settings),
         counts("graphics")},
        {"Global Illumination",
         ICON_MDI_WHITE_BALANCE_SUNNY,
         "The surface cache: its atlas, the card capture budget and refresh, and the card resolution rule.",
         "gi surface cache atlas cards capture refresh budget texel density resolution",
         draws(&draw_global_illumination_settings),
         counts("global_illumination")},
        {"Splash Screen",
         ICON_MDI_IMAGE_OUTLINE,
         "What the game shows while it starts.",
         "logo fade made with startup intro",
         draws(&draw_splash_settings),
         counts("splash")},
        {"Standalone",
         ICON_MDI_ROCKET,
         "How the built game starts.",
         "startup scene build player deploy",
         draws(&draw_standalone_settings),
         counts("standalone")},
        {"Layers",
         ICON_MDI_LAYERS_OUTLINE,
         "The layers entities can be on. The first ones are built in.",
         "layer mask culling names",
         draws(&draw_layers_settings),
         {}},
        {"Input",
         ICON_MDI_GAMEPAD_VARIANT,
         "Named actions, and the keys, buttons and axes that trigger them.",
         "keyboard mouse gamepad action mapping key button axis controls",
         draws(&draw_input_settings),
         {}},
        {"Physics",
         ICON_MDI_ATOM,
         "The physics backend and the simulation step.",
         "backend timestep fixed step solver iterations box3d bullet",
         draws(&draw_physics_settings),
         counts("physics")},
    };
}

/// The edit target's combo item for a profile: its label and how many settings it overrides.
auto get_target_label(const settings_profile& profile) -> std::string
{
    const auto count = profile.settings.get_paths().size();
    if(count == 0)
    {
        return profile_labels::get_label(profile);
    }
    return fmt::format("{}   ({} override{})", profile_labels::get_label(profile), count, count == 1 ? "" : "s");
}

/// Picks what the window edits; returns the profile edited, or null for the project settings.
auto draw_edit_target_combo(rtti::context& ctx, settings_edit_target& target) -> const settings_profile*
{
    const auto& profiles = ctx.get_cached<project_manager>().get_profiles();
    const auto* edited = profiles.find(target.profile);
    property_layout layout("Settings Of",
                           "The project settings, which every profile starts from, or a profile's. In a profile a "
                           "change becomes an override of that profile, and the rest follows the project settings.");
    const std::string project_label = std::string(ICON_MDI_COG " ") + PROJECT_SETTINGS_LABEL;
    const std::string preview = edited != nullptr ? get_target_label(*edited) : project_label;
    if(ImGui::BeginCombo("##settings_of", preview.c_str()))
    {
        if(ImGui::Selectable(project_label.c_str(), edited == nullptr))
        {
            target.profile.clear();
            target.missing_profile.clear();
            target.error.clear();
        }
        for(const auto& profile : profiles.get_all())
        {
            if(ImGui::Selectable(get_target_label(profile).c_str(), &profile == edited))
            {
                target.profile = profile.name;
                target.missing_profile.clear();
                target.error.clear();
            }
        }
        ImGui::EndCombo();
    }
    return profiles.find(target.profile);
}

/// Which settings the window edits: the project's, or a profile's; with a profile, how to preview it.
void draw_edit_target(rtti::context& ctx, settings_edit_target& target)
{
    auto& pm = ctx.get_cached<project_manager>();
    if(!target.profile.empty() && pm.get_profiles().find(target.profile) == nullptr)
    {
        // Deleted or renamed elsewhere: say so, rather than let the next edit land in the project settings unseen.
        target.missing_profile = target.profile;
        target.profile.clear();
    }
    const auto* edited = draw_edit_target_combo(ctx, target);
    if(edited == nullptr)
    {
        if(!target.missing_profile.empty())
        {
            draw_error_line(fmt::format("Profile {} no longer exists, so the project settings are shown.", target.missing_profile));
        }
        else if(const auto& previewed = pm.get_preview_profile(); !previewed.empty())
        {
            draw_muted_text(fmt::format("The editor previews profile {}. The settings it overrides are drawn in its color, "
                                        "and the editor shows its values for them.",
                                        previewed)
                                .c_str());
        }
        return;
    }
    // A profile is saved with each edit, outside the editor's undo history.
    draw_muted_text(fmt::format("Changes here apply to profile {} only and are saved at once, without undo. Overridden "
                                "settings are shown in bold. Right-click one to revert it to the project setting.",
                                edited->name)
                        .c_str());
    const bool is_previewed = pm.get_preview_profile() == edited->name;
    property_layout preview_layout("Preview", "Whether the editor runs with this profile's settings, as its builds do.");
    if(is_previewed ? ImGui::Button(ICON_MDI_EYE_OFF_OUTLINE " Stop Preview") : ImGui::Button(ICON_MDI_EYE_OUTLINE " Preview in Editor"))
    {
        target.error = pm.set_preview_profile(ctx, is_previewed ? std::string{} : edited->name).error;
    }
}

} // namespace

project_settings_panel::project_settings_panel(imgui_panels* parent)
    : parent_(parent)
    , edit_target_(std::make_unique<settings_edit_target>())
    , view_(make_project_settings_categories(engine::context(), *edit_target_))
{
}

void project_settings_panel::show(bool s, const std::string& hint)
{
    visible_ = s;
    view_.select(hint);
}

void project_settings_panel::show_profile(const std::string& profile)
{
    visible_ = true;
    edit_target_->profile = profile;
    edit_target_->missing_profile.clear();
    edit_target_->error.clear();
}

void project_settings_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(!visible_)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.5f, ImGuiCond_FirstUseEver);
    if(ImGui::Begin(name, &visible_))
    {
        draw_edit_target(ctx, *edit_target_);
        ImGui::Separator();
        view_.draw(ctx);
    }
    ImGui::End();
}

} // namespace unravel
