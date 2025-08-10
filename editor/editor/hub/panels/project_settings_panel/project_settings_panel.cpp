#include "project_settings_panel.h"
#include "../panel.h"
#include "../panels_defs.h"

#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/system/project_manager.h>
#include <engine/input/input.h>

#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace unravel
{

namespace
{

auto to_os_key(input::key_code code) -> os::key::code
{
    return static_cast<os::key::code>(code);
}

auto to_os_key(int32_t code) -> os::key::code
{
    return static_cast<os::key::code>(code);
}

auto from_os_key(os::key::code code) -> input::key_code
{
    return static_cast<input::key_code>(code);
}

auto from_os_key(int32_t code) -> input::key_code
{
    return static_cast<input::key_code>(code);
}

template<typename EnumT, typename ToStringFn>
auto ImGuiEnumCombo(const char* label,
                    EnumT& current_value,
                    const EnumT* all_values,
                    size_t count,
                    ToStringFn stringify) -> bool
{
    // Convert current value to a display name
    std::string preview_text = stringify(current_value);

    bool changed = false;
    if(ImGui::BeginCombo(label, preview_text.c_str()))
    {
        for(size_t i = 0; i < count; ++i)
        {
            EnumT candidate = all_values[i];
            bool is_selected = (candidate == current_value);
            if(ImGui::Selectable(stringify(candidate).c_str(), is_selected))
            {
                // If user selected a different value
                if(candidate != current_value)
                {
                    current_value = candidate;
                    changed = true;
                }
            }

            // Set focus if this is the currently selected item
            if(is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    return changed;
}

template<typename EnumT, typename ToStringFn, typename FromIntFn, typename GetDescriptionFn>
auto ImGuiEnumSelector(const char* label,
                       EnumT& selected_value,
                       int enum_count,
                       ToStringFn stringify,
                       FromIntFn from_int,
                       GetDescriptionFn get_description,
                       const char* popup_id = "Enum Selector") -> bool
{
    std::vector<std::string> names;
    std::vector<const char*> names_cstr;

    static ImGuiTextFilter filter;

    ImGui::PushID(label);
    // Lazy initialization
    {
        names.reserve(enum_count);
        for(int i = 0; i < enum_count; ++i)
        {
            auto e = from_int(i);
            names.push_back(stringify(e));
        }

        // Build the const char* array
        names_cstr.reserve(names.size());
        for(const auto& name : names)
        {
            names_cstr.push_back(name.c_str());
        }
    }

    // The button text: what is the current selection's name?
    std::string current_name = stringify(selected_value);
    if(current_name.empty())
    {
        current_name = "None";
    }

    // If user clicks, open the popup
    bool selection_changed = false;
    if(ImGui::Button(current_name.c_str(), ImVec2(150.0f, ImGui::GetFrameHeight())))
    {
        // Clear the filter
        filter.Clear();
        ImGui::OpenPopup(popup_id);
    }
    std::string desc = get_description(selected_value);
    if(!desc.empty())
    {
        ImGui::SetItemTooltipEx("%s", desc.c_str());
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    // The actual popup with a filter input and a list of items
    if(ImGui::BeginPopup(popup_id))
    {
        if(ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }

        // Draw filter input box
        ImGui::DrawFilterWithHint(filter, ICON_MDI_SELECT_SEARCH " Search...", 150.0);
        ImGui::DrawItemActivityOutline();

        ImGui::Separator();

        // We create a scrolling region for the items
        if(ImGui::BeginChild("Enum Selector Context", ImVec2(0, 200.0f), true))
        {
            for(int i = 0; i < enum_count; ++i)
            {
                if(names[i].empty())
                {
                    continue;
                }

                // If it doesn't pass the filter, skip
                if(!filter.PassFilter(names[i].c_str()))
                {
                    continue;
                }

                // Check if it's the currently selected
                bool is_selected = (static_cast<int>(selected_value) == i);
                if(ImGui::Selectable(names[i].c_str(), is_selected))
                {
                    // The user picked something new
                    selected_value = from_int(i);
                    selection_changed = true;
                    ImGui::CloseCurrentPopup();
                }

                std::string desc = get_description(from_int(i));
                if(!desc.empty())
                {
                    ImGui::SetItemTooltipEx("%s", desc.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", "(?)");
                    // ImGui::HelpMarker(desc.c_str());
                }
            }
            ImGui::EndChild();
        }

        ImGui::EndPopup();
    }

    ImGui::PopID();

    return selection_changed;
}

void draw_application_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();

    ImGui::PushItemWidth(150.0f);

    if(inspect(ctx, settings.app).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    ImGui::PopItemWidth();
}

void draw_resolution_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();

    ImGui::PushItemWidth(150.0f);

    if(inspect(ctx, settings.resolution).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    ImGui::PopItemWidth();
}

void draw_graphics_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();

    ImGui::PushItemWidth(150.0f);

    if(inspect(ctx, settings.graphics).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    ImGui::PopItemWidth();
}

void draw_standalone_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();

    ImGui::PushItemWidth(150.0f);

    if(inspect(ctx, settings.standalone).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    ImGui::PopItemWidth();
}

void draw_layers_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();

    ImGui::PushItemWidth(150.0f);

    if(inspect(ctx, settings.layer).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    ImGui::PopItemWidth();
}

void draw_asset_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();

    ImGui::PushItemWidth(150.0f);

    if(inspect(ctx, settings.assets.texture).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    if(ImGui::Button("Recompile Textures"))
    {
        editor_actions::recompile_textures();
    }

    ImGui::PopItemWidth();
}

void draw_input_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();

    int total_inputs = 0;

    inspect_result result;
    ImGui::PushItemWidth(150.0f);

    if(ImGui::TreeNode("Keyboard"))
    {
        auto& entries = settings.input.actions.keyboard_map.entries_by_action_id_;

        if(ImGui::Button("Add Action"))
        {
            auto& mapping = entries["New Action"];
            mapping.emplace_back();

            result.changed = true;
            result.edit_finished = true;
        }

        std::string rename_from;
        std::string rename_to;
        std::string to_delete;

        for(auto& kvp : entries)
        {
            ImGui::PushID(total_inputs++);
            auto& action = kvp.first;
            auto& mappings = kvp.second;

            if(ImGui::Button(ICON_MDI_DELETE_ALERT))
            {
                to_delete = action;
            }

            ImGui::SameLine();

            if(ImGui::TreeNode(action.c_str()))
            {
                int i = 0;

                auto name = action;
                if(ImGui::InputTextWidget("Name", name, false, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    rename_from = action;
                    rename_to = name;
                }

                if(ImGui::Button("Add Mapping"))
                {
                    mappings.emplace_back();

                    result.changed = true;
                    result.edit_finished = true;
                }

                int index_to_remove = -1;
                for(auto& mapping : mappings)
                {
                    if(&mapping != &mappings.front())
                    {
                        ImGui::Separator();
                    }

                    ImGui::PushID(int32_t(i));

                    ImGui::PushID(int32_t(mapping.key));

                    if(ImGui::Button(ICON_MDI_DELETE))
                    {
                        index_to_remove = i;
                    }
                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    {
                        auto oskey = to_os_key(mapping.key);

                        if(ImGuiEnumSelector(
                               "Key",
                               oskey,
                               os::key::code::count,
                               [](os::key::code code)
                               {
                                   return os::key::to_string(code);
                               },
                               [](int code)
                               {
                                   return to_os_key(code);
                               },
                               [](os::key::code code)
                               {
                                   return "";
                               },
                               "Key Selector"))
                        {
                            mapping.key = from_os_key(oskey);
                            result.changed = true;
                            result.edit_finished = true;
                        }

                        int mod_i = 0;
                        int mod_index_to_remove = -1;
                        for(auto& modifier : mapping.modifiers)
                        {
                            ImGui::PushID(mod_i);
                            auto osmodifier = to_os_key(modifier);
                            if(ImGui::Button(ICON_MDI_DELETE_VARIANT))
                            {
                                mod_index_to_remove = mod_i;
                            }
                            ImGui::SameLine();

                            if(ImGuiEnumSelector(
                                   "Modifier",
                                   osmodifier,
                                   os::key::code::count,
                                   [](os::key::code code)
                                   {
                                       return os::key::to_string(code);
                                   },
                                   [](int code)
                                   {
                                       return to_os_key(code);
                                   },
                                   [](os::key::code code)
                                   {
                                       return "";
                                   },
                                   "Modifier Selector"))
                            {
                                modifier = from_os_key(osmodifier);
                                result.changed = true;
                                result.edit_finished = true;
                            }
                            ImGui::PopID();

                            mod_i++;
                        }

                        if(mod_index_to_remove != -1)
                        {
                            if(mod_index_to_remove < mapping.modifiers.size())
                            {
                                mapping.modifiers.erase(mapping.modifiers.begin() + mod_index_to_remove);
                            }
                        }

                        ImGui::Dummy(ImVec2(150.0f, ImGui::GetFrameHeight()));
                        ImGui::SameLine();
                        if(ImGui::Button("Add Modifier"))
                        {
                            mapping.modifiers.emplace_back();
                            result.changed = true;
                            result.edit_finished = true;
                        }

                        if(ImGui::DragFloat("Analog Value", &mapping.analog_value, 0.05))
                        {
                            result.changed = true;
                        }
                        result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    ImGui::EndGroup();

                    i++;
                    ImGui::PopID();
                    ImGui::PopID();
                }

                if(index_to_remove != -1)
                {
                    if(index_to_remove < mappings.size())
                    {
                        mappings.erase(mappings.begin() + index_to_remove);
                    }
                    result.changed = true;
                    result.edit_finished = true;
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        if(!rename_to.empty())
        {
            entries[rename_to] = entries[rename_from];
            entries.erase(rename_from);
        }

        if(!to_delete.empty())
        {
            entries.erase(to_delete);
        }

        ImGui::TreePop();
    }

    if(ImGui::TreeNode("Gamepad"))
    {
        auto& entries = settings.input.actions.gamepad_map.entries_by_action_id_;

        if(ImGui::Button("Add Action"))
        {
            auto& mapping = entries["New Action"];
            mapping.emplace_back();

            result.changed = true;
            result.edit_finished = true;
        }

        std::string rename_from;
        std::string rename_to;
        std::string to_delete;

        for(auto& kvp : settings.input.actions.gamepad_map.entries_by_action_id_)
        {
            auto& action = kvp.first;
            auto& mappings = kvp.second;

            ImGui::PushID(total_inputs++);

            if(ImGui::Button(ICON_MDI_DELETE_ALERT))
            {
                to_delete = action;
            }

            ImGui::SameLine();

            if(ImGui::TreeNode(action.c_str()))
            {
                int i = 0;

                auto name = action;
                if(ImGui::InputTextWidget("Name", name, false, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    rename_from = action;
                    rename_to = name;
                }

                if(ImGui::Button("Add Mapping"))
                {
                    mappings.emplace_back();

                    result.changed = true;
                    result.edit_finished = true;
                }

                int index_to_remove = -1;
                for(auto& mapping : mappings)
                {
                    if(&mapping != &mappings.front())
                    {
                        ImGui::Separator();
                    }

                    ImGui::PushID(int32_t(i));

                    ImGui::PushID(int32_t(mapping.type));

                    if(ImGui::Button(ICON_MDI_DELETE))
                    {
                        index_to_remove = i;
                    }

                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    {
                        input::input_type types[] = {input::input_type::button, input::input_type::axis};
                        if(ImGuiEnumCombo("Type",
                                          mapping.type,
                                          types,
                                          IM_ARRAYSIZE(types),
                                          [](input::input_type type)
                                          {
                                              return input::to_string(type);
                                          }))
                        {
                            result.changed = true;
                            result.edit_finished = true;
                        }

                        if(mapping.type == input::input_type::axis)
                        {
                            input::axis_range ranges[] = {input::axis_range::full,
                                                          input::axis_range::positive,
                                                          input::axis_range::negative};
                            if(ImGuiEnumCombo("Range",
                                              mapping.range,
                                              ranges,
                                              IM_ARRAYSIZE(ranges),
                                              [](input::axis_range range)
                                              {
                                                  return input::to_string(range);
                                              }))
                            {
                                result.changed = true;
                                result.edit_finished = true;
                            }

                            auto axis = static_cast<input::gamepad_axis>(mapping.value);

                            if(ImGuiEnumSelector(
                                   "Axis",
                                   axis,
                                   int(input::gamepad_axis::count),
                                   [](input::gamepad_axis button)
                                   {
                                       return input::to_string(button);
                                   },
                                   [](int val)
                                   {
                                       return static_cast<input::gamepad_axis>(val);
                                   },
                                   [](input::gamepad_axis button)
                                   {
                                       return "";
                                   },
                                   "Gamepad Axis Selector"))
                            {
                                mapping.value = static_cast<uint32_t>(axis);

                                result.changed = true;
                                result.edit_finished = true;
                            }

                            ImGui::DragFloat("Min Analog Value", &mapping.min_analog_value, 0.05);
                            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

                            ImGui::DragFloat("Max Analog Value", &mapping.max_analog_value, 0.05);
                            result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
                        }
                        else
                        {
                            auto button = static_cast<input::gamepad_button>(mapping.value);

                            if(ImGuiEnumSelector(
                                   "Button",
                                   button,
                                   int(input::gamepad_button::count),
                                   [](input::gamepad_button button)
                                   {
                                       return input::to_string(button);
                                   },
                                   [](int val)
                                   {
                                       return static_cast<input::gamepad_button>(val);
                                   },
                                   [](input::gamepad_button button)
                                   {
                                       return input::get_description(button);
                                   },
                                   "Gamepad Button Selector"))
                            {
                                mapping.value = static_cast<uint32_t>(button);

                                result.changed = true;
                                result.edit_finished = true;
                            }
                        }
                    }
                    ImGui::EndGroup();

                    i++;
                    ImGui::PopID();
                    ImGui::PopID();
                }

                if(index_to_remove != -1)
                {
                    if(index_to_remove < mappings.size())
                    {
                        mappings.erase(mappings.begin() + index_to_remove);
                    }
                    result.changed = true;
                    result.edit_finished = true;
                }

                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if(!rename_to.empty())
        {
            entries[rename_to] = entries[rename_from];
            entries.erase(rename_from);
        }

        if(!to_delete.empty())
        {
            entries.erase(to_delete);
        }

        ImGui::TreePop();
    }

    if(ImGui::TreeNode("Mouse"))
    {
        auto& entries = settings.input.actions.mouse_map.entries_by_action_id_;

        if(ImGui::Button("Add Action"))
        {
            auto& mapping = entries["New Action"];
            mapping.emplace_back();

            result.changed = true;
            result.edit_finished = true;
        }

        std::string rename_from;
        std::string rename_to;
        std::string to_delete;

        for(auto& kvp : entries)
        {
            const auto& action = kvp.first;
            auto& mappings = kvp.second;

            ImGui::PushID(total_inputs++);

            if(ImGui::Button(ICON_MDI_DELETE_ALERT))
            {
                to_delete = action;
            }

            ImGui::SameLine();

            if(ImGui::TreeNode(action.c_str()))
            {
                int i = 0;

                auto name = action;
                if(ImGui::InputTextWidget("Name", name, false, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    rename_from = action;
                    rename_to = name;
                }

                if(ImGui::Button("Add Mapping"))
                {
                    mappings.emplace_back();

                    result.changed = true;
                    result.edit_finished = true;
                }

                int index_to_remove = -1;
                for(auto& mapping : mappings)
                {
                    if(&mapping != &mappings.front())
                    {
                        ImGui::Separator();
                    }

                    ImGui::PushID(i);

                    if(ImGui::Button(ICON_MDI_DELETE))
                    {
                        index_to_remove = i;
                    }

                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    {
                        input::input_type types[] = {input::input_type::button, input::input_type::axis};
                        if(ImGuiEnumCombo("Type",
                                          mapping.type,
                                          types,
                                          IM_ARRAYSIZE(types),
                                          [](input::input_type type)
                                          {
                                              return input::to_string(type);
                                          }))
                        {
                            result.changed = true;
                            result.edit_finished = true;
                        }

                        if(mapping.type == input::input_type::axis)
                        {
                            input::axis_range ranges[] = {input::axis_range::full,
                                                          input::axis_range::positive,
                                                          input::axis_range::negative};
                            if(ImGuiEnumCombo("Range",
                                              mapping.range,
                                              ranges,
                                              IM_ARRAYSIZE(ranges),
                                              [](input::axis_range range)
                                              {
                                                  return input::to_string(range);
                                              }))
                            {
                                result.changed = true;
                                result.edit_finished = true;
                            }

                            auto axis = static_cast<input::mouse_axis>(mapping.value);
                            input::mouse_axis axes[] = {input::mouse_axis::x,
                                                        input::mouse_axis::y,
                                                        input::mouse_axis::scroll};
                            if(ImGuiEnumCombo("Axis",
                                              axis,
                                              axes,
                                              IM_ARRAYSIZE(axes),
                                              [](input::mouse_axis axis)
                                              {
                                                  return input::to_string(axis);
                                              }))
                            {
                                mapping.value = static_cast<uint32_t>(axis);

                                result.changed = true;
                                result.edit_finished = true;
                            }
                        }
                        else
                        {
                            auto button = static_cast<input::mouse_button>(mapping.value);

                            if(ImGuiEnumSelector(
                                   "Button",
                                   button,
                                   int(input::mouse_button::count),
                                   [](input::mouse_button button)
                                   {
                                       return input::to_string(button);
                                   },
                                   [](int val)
                                   {
                                       return static_cast<input::mouse_button>(val);
                                   },
                                   [](input::mouse_button button)
                                   {
                                       return "";
                                   },
                                   "Button Selector"))
                            {
                                mapping.value = static_cast<uint32_t>(button);

                                result.changed = true;
                                result.edit_finished = true;
                            }
                        }
                    }
                    ImGui::EndGroup();

                    i++;

                    ImGui::PopID();
                }
                ImGui::TreePop();

                if(index_to_remove != -1)
                {
                    if(index_to_remove < mappings.size())
                    {
                        mappings.erase(mappings.begin() + index_to_remove);
                    }
                    result.changed = true;
                    result.edit_finished = true;
                }
            }

            ImGui::PopID();
        }

        if(!rename_to.empty())
        {
            entries[rename_to] = entries[rename_from];
            entries.erase(rename_from);
        }

        if(!to_delete.empty())
        {
            entries.erase(to_delete);
        }

        ImGui::TreePop();
    }

    if(result.edit_finished)
    {
        pm.save_project_settings(ctx);
    }
}

void draw_time_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();

    ImGui::PushItemWidth(150.0f);

    if(inspect(ctx, settings.time).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    ImGui::PopItemWidth();
}

} // namespace

project_settings_panel::project_settings_panel(imgui_panels* parent) : parent_(parent)
{
}

void project_settings_panel::show(bool s, const std::string& hint)
{
    show_request_ = s;
    hint_ = hint;
}

void project_settings_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        ImGui::OpenPopup(name);
        show_request_ = false;
    }

    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.5f);
    bool show = true;
    if(ImGui::BeginPopupModal(name, &show))
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));

        draw_ui(ctx);

        ImGui::EndPopup();
    }
}

void project_settings_panel::draw_ui(rtti::context& ctx)
{
    // We'll create two child windows side by side:
    // 1) Left child: categories
    // 2) Right child: actual settings

    auto avail = ImGui::GetContentRegionAvail();
    if(avail.x < 1.0f || avail.y < 1.0f)
    {
        return;
    }

    static std::vector<setting_entry> categories{{"Application", &draw_application_settings},
                                                 {"Resolution", &draw_resolution_settings},
                                                 {"Assets", &draw_asset_settings},
                                                 {"Graphics", &draw_graphics_settings},
                                                 {"Standalone", &draw_standalone_settings},
                                                 {"Layers", &draw_layers_settings},
                                                 {"Input", &draw_input_settings},
                                                 {"Time", &draw_time_settings}};
    // Child A: the categories list
    // We fix the width of this child, so the right child uses the remaining space.
    ImGui::BeginChild("##LeftSidebar", avail * ImVec2(0.15f, 1.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    {
        // Display categories
        for(const auto& category : categories)
        {
            if(hint_ == category.id)
            {
                selected_entry_ = category;
                hint_.clear();
            }

            // 'Selectable' returns true if clicked
            if(ImGui::Selectable(category.id.c_str(), (selected_entry_.id == category.id)))
            {
                selected_entry_ = category;
            }
        }
    }
    ImGui::EndChild();

    // On the same line:
    ImGui::SameLine();

    // Child B: show settings for the selected category
    ImGui::BeginChild("##RightContent");
    {
        if(selected_entry_.callback)
        {
            selected_entry_.callback(ctx);
        }
    }
    ImGui::EndChild();
}

} // namespace unravel
