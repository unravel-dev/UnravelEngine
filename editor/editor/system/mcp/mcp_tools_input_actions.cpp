#include "mcp_input_names.h"
#include "mcp_tools_common.h"

#include <editor/system/project_manager.h>
#include <engine/input/input.h>
#include <engine/settings/settings.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace unravel::mcp
{
namespace
{

/// Builds normalized-name -> value over an enum's range, using the input library's own
/// to_string so the names a caller passes are the names the editor shows.
template<typename Enum>
auto build_enum_table(uint32_t count) -> std::map<std::string, Enum>
{
    std::map<std::string, Enum> table;
    for(uint32_t i = 0; i < count; ++i)
    {
        const auto value = static_cast<Enum>(i);
        const auto name = input::to_string(value);
        if(name.empty() || name == "Unknown")
        {
            continue;
        }
        table.emplace(normalize_input_name(name), value);
    }
    return table;
}

auto mouse_button_table() -> const std::map<std::string, input::mouse_button>&
{
    static const auto table =
        build_enum_table<input::mouse_button>(static_cast<uint32_t>(input::mouse_button::count));
    return table;
}

auto gamepad_button_table() -> const std::map<std::string, input::gamepad_button>&
{
    static const auto table =
        build_enum_table<input::gamepad_button>(static_cast<uint32_t>(input::gamepad_button::count));
    return table;
}

auto gamepad_axis_table() -> const std::map<std::string, input::gamepad_axis>&
{
    static const auto table =
        build_enum_table<input::gamepad_axis>(static_cast<uint32_t>(input::gamepad_axis::count));
    return table;
}

auto mouse_axis_table() -> const std::map<std::string, input::mouse_axis>&
{
    // mouse_axis has no count entry; scroll is its last value.
    static const auto table =
        build_enum_table<input::mouse_axis>(static_cast<uint32_t>(input::mouse_axis::scroll) + 1);
    return table;
}

template<typename Map>
auto lookup(const Map& table, const std::string& name, typename Map::mapped_type& out) -> bool
{
    const auto found = table.find(normalize_input_name(name));
    if(found == table.end())
    {
        return false;
    }
    out = found->second;
    return true;
}

template<typename Map>
auto names_of(const Map& table, size_t limit) -> std::string
{
    std::vector<std::string> names;
    for(const auto& kvp : table)
    {
        names.push_back(kvp.first);
        if(names.size() >= limit)
        {
            break;
        }
    }
    return fmt::format("{}", fmt::join(names, ", "));
}

auto parse_axis_range(const std::string& name, input::axis_range& out) -> bool
{
    const auto normalized = normalize_input_name(name);
    if(normalized.empty() || normalized == "full")
    {
        out = input::axis_range::full;
        return true;
    }
    if(normalized == "positive")
    {
        out = input::axis_range::positive;
        return true;
    }
    if(normalized == "negative")
    {
        out = input::axis_range::negative;
        return true;
    }
    return false;
}

auto key_name_json(input::key_code code) -> std::string
{
    return make_json_string(os::key::to_string(static_cast<os::key::code>(code)));
}

auto keyboard_entry_json(const input::keyboard_action_map::key_entry& entry) -> std::string
{
    std::vector<std::string> modifiers;
    modifiers.reserve(entry.modifiers.size());
    for(const auto modifier : entry.modifiers)
    {
        modifiers.push_back(os::key::to_string(static_cast<os::key::code>(modifier)));
    }
    return fmt::format(R"({{"key":{},"modifiers":{},"analog_value":{}}})",
                       key_name_json(entry.key),
                       strings_to_json_array(modifiers),
                       entry.analog_value);
}

auto mouse_entry_json(const input::mouse_action_map::mouse_entry& entry) -> std::string
{
    if(entry.type == input::input_type::axis)
    {
        return fmt::format(R"({{"axis":{},"range":{}}})",
                           make_json_string(input::to_string(static_cast<input::mouse_axis>(entry.value))),
                           make_json_string(input::to_string(entry.range)));
    }
    return fmt::format(R"({{"button":{}}})",
                       make_json_string(input::to_string(static_cast<input::mouse_button>(entry.value))));
}

auto gamepad_entry_json(const input::gamepad_action_map::gamepad_entry& entry) -> std::string
{
    if(entry.type == input::input_type::axis)
    {
        return fmt::format(
            R"({{"axis":{},"range":{},"min_analog_value":{},"max_analog_value":{}}})",
            make_json_string(input::to_string(static_cast<input::gamepad_axis>(entry.value))),
            make_json_string(input::to_string(entry.range)),
            entry.min_analog_value,
            entry.max_analog_value);
    }
    return fmt::format(R"({{"button":{}}})",
                       make_json_string(input::to_string(static_cast<input::gamepad_button>(entry.value))));
}

template<typename Entries, typename Fn>
auto entries_json(const Entries& entries, const input::action_id_t& action, Fn&& to_json) -> std::string
{
    const auto found = entries.find(action);
    if(found == entries.end())
    {
        return "[]";
    }
    std::string json = "[";
    bool first = true;
    for(const auto& entry : found->second)
    {
        if(!first)
        {
            json += ",";
        }
        first = false;
        json += to_json(entry);
    }
    json += "]";
    return json;
}

auto collect_action_names(const input::action_map& actions) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(const auto& kvp : actions.keyboard_map.entries_by_action_id_)
    {
        names.push_back(kvp.first);
    }
    for(const auto& kvp : actions.mouse_map.entries_by_action_id_)
    {
        names.push_back(kvp.first);
    }
    for(const auto& kvp : actions.gamepad_map.entries_by_action_id_)
    {
        names.push_back(kvp.first);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

/// Drops every binding for `action` on one device. Returns how many went.
template<typename Entries>
auto erase_action(Entries& entries, const input::action_id_t& action) -> size_t
{
    const auto found = entries.find(action);
    if(found == entries.end())
    {
        return 0;
    }
    const auto count = found->second.size();
    entries.erase(found);
    return count;
}

} // namespace

void register_input_action_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "input_list_actions",
         .description =
             "List the project's input actions and what each is bound to on keyboard, mouse and "
             "gamepad. These names are the strings scripts pass to Input.GetAnalogValue / "
             "GetDigitalValue / IsPressed - an action a script asks for that is not listed here "
             "always reads zero. Optional action filter.",
         .input_schema_json =
             R"json({"type":"object","properties":{"action":{"type":"string","description":"Only this action"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             const auto& actions = pm->get_settings().input.actions;

             std::string filter;
             read_string(args, "action", filter);

             std::string json = "[";
             bool first = true;
             for(const auto& name : collect_action_names(actions))
             {
                 if(!filter.empty() && name != filter)
                 {
                     continue;
                 }
                 if(!first)
                 {
                     json += ",";
                 }
                 first = false;
                 json += fmt::format(
                     R"({{"action":{},"keyboard":{},"mouse":{},"gamepad":{}}})",
                     make_json_string(name),
                     entries_json(actions.keyboard_map.entries_by_action_id_, name, keyboard_entry_json),
                     entries_json(actions.mouse_map.entries_by_action_id_, name, mouse_entry_json),
                     entries_json(actions.gamepad_map.entries_by_action_id_, name, gamepad_entry_json));
             }
             json += "]";
             return {.text = fmt::format(R"({{"actions":{}}})", json), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "input_map_action",
         .description =
             "Bind an input to an action, creating the action if it does not exist, and save the "
             "settings. Give device plus one of key / button / axis. Keyboard: key (\"w\", \"space\", "
             "\"left shift\"), optional modifiers and analog_value (default 1; use -1 for the "
             "negative half of an axis pair, as \"a\" does for Horizontal). Mouse: button (left, "
             "right, middle) or axis (x, y, scroll). Gamepad: button (south, east, dpad_up, ...) or "
             "axis (left_x, right_trigger, ...) with optional range and min/max analog values. "
             "Bindings add up, so an action can have several.",
         .input_schema_json =
             R"json({"type":"object","properties":{"action":{"type":"string"},"device":{"type":"string","enum":["keyboard","mouse","gamepad"]},"key":{"type":"string"},"modifiers":{"type":"array","items":{"type":"string"}},"button":{"type":"string"},"axis":{"type":"string"},"range":{"type":"string","enum":["full","positive","negative"]},"analog_value":{"type":"number"},"min_analog_value":{"type":"number"},"max_analog_value":{"type":"number"}},"required":["action","device"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }

             std::string action;
             read_string(args, "action", action);
             if(action.empty())
             {
                 return {.text = "Provide action (the name scripts will query)", .is_error = true};
             }
             std::string device;
             read_string(args, "device", device);

             std::string key_name;
             const bool has_key = read_string(args, "key", key_name) && !key_name.empty();
             std::string button_name;
             const bool has_button = read_string(args, "button", button_name) && !button_name.empty();
             std::string axis_name;
             const bool has_axis = read_string(args, "axis", axis_name) && !axis_name.empty();

             std::string range_name;
             read_string(args, "range", range_name);
             input::axis_range range{};
             if(!parse_axis_range(range_name, range))
             {
                 return {.text = "range must be full, positive or negative", .is_error = true};
             }

             auto settings_copy = pm->get_settings();
             auto& actions = settings_copy.input.actions;

             if(device == "keyboard")
             {
                 if(!has_key)
                 {
                     return {.text = "keyboard bindings need key", .is_error = true};
                 }
                 os::key::code os_code{};
                 std::string key_error;
                 if(!parse_key_name(key_name, os_code, key_error))
                 {
                     return {.text = key_error, .is_error = true};
                 }
                 // input::key_code mirrors os::key::code, as the project settings panel's key
                 // pickers rely on.
                 const auto code = static_cast<input::key_code>(os_code);
                 std::vector<input::key_code> modifiers;
                 simdjson::dom::array modifier_items;
                 if(args["modifiers"].get_array().get(modifier_items) == simdjson::SUCCESS)
                 {
                     for(auto item : modifier_items)
                     {
                         std::string_view modifier_name;
                         if(item.get_string().get(modifier_name) != simdjson::SUCCESS)
                         {
                             return {.text = "modifiers must be key names", .is_error = true};
                         }
                         os::key::code os_modifier{};
                         std::string modifier_error;
                         if(!parse_key_name(std::string(modifier_name), os_modifier, modifier_error))
                         {
                             return {.text = "modifier: " + modifier_error, .is_error = true};
                         }
                         modifiers.push_back(static_cast<input::key_code>(os_modifier));
                     }
                 }
                 double analog_value = 1.0;
                 if(args["analog_value"].get(analog_value))
                 {
                     analog_value = 1.0;
                 }
                 if(modifiers.empty())
                 {
                     actions.keyboard_map.map(action, code, static_cast<float>(analog_value));
                 }
                 else
                 {
                     actions.keyboard_map.map(action, code, modifiers, static_cast<float>(analog_value));
                 }
             }
             else if(device == "mouse")
             {
                 if(has_button)
                 {
                     input::mouse_button button{};
                     if(!lookup(mouse_button_table(), button_name, button))
                     {
                         return {.text = "unknown mouse button \"" + button_name + "\"; expected one of " +
                                         names_of(mouse_button_table(), 6),
                                 .is_error = true};
                     }
                     actions.mouse_map.map(action, button);
                 }
                 else if(has_axis)
                 {
                     input::mouse_axis axis{};
                     if(!lookup(mouse_axis_table(), axis_name, axis))
                     {
                         return {.text = "unknown mouse axis \"" + axis_name + "\"; expected x, y or scroll",
                                 .is_error = true};
                     }
                     actions.mouse_map.map(action, axis, range);
                 }
                 else
                 {
                     return {.text = "mouse bindings need button or axis", .is_error = true};
                 }
             }
             else if(device == "gamepad")
             {
                 if(has_button)
                 {
                     input::gamepad_button button{};
                     if(!lookup(gamepad_button_table(), button_name, button))
                     {
                         return {.text = "unknown gamepad button \"" + button_name + "\"; expected one of " +
                                         names_of(gamepad_button_table(), 10),
                                 .is_error = true};
                     }
                     actions.gamepad_map.map(action, button);
                 }
                 else if(has_axis)
                 {
                     input::gamepad_axis axis{};
                     if(!lookup(gamepad_axis_table(), axis_name, axis))
                     {
                         return {.text = "unknown gamepad axis \"" + axis_name + "\"; expected one of " +
                                         names_of(gamepad_axis_table(), 6),
                                 .is_error = true};
                     }
                     double min_analog = -1.0;
                     double max_analog = 1.0;
                     if(args["min_analog_value"].get(min_analog))
                     {
                         min_analog = -1.0;
                     }
                     if(args["max_analog_value"].get(max_analog))
                     {
                         max_analog = 1.0;
                     }
                     actions.gamepad_map.map(action,
                                             axis,
                                             range,
                                             static_cast<float>(min_analog),
                                             static_cast<float>(max_analog));
                 }
                 else
                 {
                     return {.text = "gamepad bindings need button or axis", .is_error = true};
                 }
             }
             else
             {
                 return {.text = "device must be keyboard, mouse or gamepad", .is_error = true};
             }

             pm->get_settings() = std::move(settings_copy);
             pm->save_project_settings(ctx);

             return {.text = fmt::format(R"({{"ok":true,"action":{},"device":{}}})",
                                         make_json_string(action),
                                         make_json_string(device)),
                     .is_error = false};
         },
         .mutates_scene = true,
         .idempotent = true});

    registry.add(
        {.name = "input_unmap_action",
         .description =
             "Remove an action's bindings and save the settings. Without device it removes the "
             "action from every device; with device it clears only that one. A script querying an "
             "unmapped action reads zero rather than failing, so check input_list_actions after.",
         .input_schema_json =
             R"json({"type":"object","properties":{"action":{"type":"string"},"device":{"type":"string","enum":["keyboard","mouse","gamepad"]}},"required":["action"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }

             std::string action;
             read_string(args, "action", action);
             if(action.empty())
             {
                 return {.text = "Provide action", .is_error = true};
             }
             std::string device;
             read_string(args, "device", device);
             if(!device.empty() && device != "keyboard" && device != "mouse" && device != "gamepad")
             {
                 return {.text = "device must be keyboard, mouse or gamepad", .is_error = true};
             }

             auto settings_copy = pm->get_settings();
             auto& actions = settings_copy.input.actions;
             size_t removed = 0;
             if(device.empty() || device == "keyboard")
             {
                 removed += erase_action(actions.keyboard_map.entries_by_action_id_, action);
             }
             if(device.empty() || device == "mouse")
             {
                 removed += erase_action(actions.mouse_map.entries_by_action_id_, action);
             }
             if(device.empty() || device == "gamepad")
             {
                 removed += erase_action(actions.gamepad_map.entries_by_action_id_, action);
             }

             if(removed == 0)
             {
                 return {.text = "No bindings for action \"" + action + "\"; see input_list_actions",
                         .is_error = true};
             }

             pm->get_settings() = std::move(settings_copy);
             pm->save_project_settings(ctx);

             return {.text = fmt::format(R"({{"ok":true,"action":{},"removed_bindings":{}}})",
                                         make_json_string(action),
                                         removed),
                     .is_error = false};
         },
         .mutates_scene = true,
         .effect = tool_effect::destructive});
}

} // namespace unravel::mcp
