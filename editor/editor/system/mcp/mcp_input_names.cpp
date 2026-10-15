#include "mcp_input_names.h"

#include "mcp_tools_common.h"

#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{

/// Normalized name -> code, for every key the platform layer can name.
auto key_name_table() -> const std::map<std::string, os::key::code>&
{
    static const auto table = []
    {
        std::map<std::string, os::key::code> result;
        for(uint32_t i = 0; i < static_cast<uint32_t>(os::key::code::count); ++i)
        {
            const auto code = static_cast<os::key::code>(i);
            const auto name = os::key::to_string(code);
            if(name.empty())
            {
                continue;
            }
            result.emplace(normalize_input_name(name), code);
        }
        return result;
    }();
    return table;
}

/// Short forms a caller is likely to reach for, mapped onto the platform's own names.
auto key_alias_table() -> const std::map<std::string, std::string>&
{
    static const std::map<std::string, std::string> aliases{
        {"esc", "escape"},
        {"enter", "return"},
        {"lshift", "leftshift"},
        {"rshift", "rightshift"},
        {"lctrl", "leftctrl"},
        {"rctrl", "rightctrl"},
        {"lcontrol", "leftctrl"},
        {"rcontrol", "rightctrl"},
        {"ctrl", "leftctrl"},
        {"control", "leftctrl"},
        {"shift", "leftshift"},
        {"alt", "leftalt"},
        {"lalt", "leftalt"},
        {"ralt", "rightalt"},
        {"del", "delete"},
        {"ins", "insert"},
        {"pgup", "pageup"},
        {"pgdn", "pagedown"},
    };
    return aliases;
}

auto suggest_key_names(const std::string& normalized) -> std::string
{
    if(normalized.empty())
    {
        return {};
    }
    std::vector<std::string> matches;
    const auto prefix = normalized.substr(0, 1);
    for(const auto& kvp : key_name_table())
    {
        if(kvp.first.compare(0, prefix.size(), prefix) == 0)
        {
            matches.push_back(os::key::to_string(kvp.second));
        }
        if(matches.size() >= 10)
        {
            break;
        }
    }
    if(matches.empty())
    {
        return " Names come from the platform layer: \"W\", \"Space\", \"Left Shift\", \"F1\".";
    }
    return " Names like it: " + fmt::format("{}", fmt::join(matches, ", ")) +
           " (matching ignores case, spaces and dashes).";
}

} // namespace

/// Key names come from the platform layer ("W", "Space", "Left Shift"), and its
/// from_string is an exact match with no failure value. Comparing on a normalized form
/// instead lets a caller write "w", "space" or "leftshift" and still land on the right key.
auto normalize_input_name(hpp::string_view name) -> std::string
{
    std::string out;
    out.reserve(name.size());
    for(const char c : name)
    {
        if(c == ' ' || c == '-' || c == '_')
        {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

auto parse_key_name(const std::string& name, os::key::code& out, std::string& error) -> bool
{
    auto normalized = normalize_input_name(name);
    const auto& aliases = key_alias_table();
    const auto alias = aliases.find(normalized);
    if(alias != aliases.end())
    {
        normalized = alias->second;
    }
    const auto& table = key_name_table();
    const auto found = table.find(normalized);
    if(found == table.end())
    {
        error = "unknown key \"" + name + "\"." + suggest_key_names(normalized);
        return false;
    }
    out = found->second;
    return true;
}

} // namespace unravel::mcp
