#include "mcp_tool_registry.h"

#include "mcp_protocol.h"

#include <filesystem/filesystem.h>
#include <logging/logging.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace unravel::mcp
{
namespace
{

/// Shipped skill files live in editor data so they can be updated without a rebuild.
constexpr const char* k_skills_protocol_dir = "editor:/data/mcp/skills";

/// One workflow skill loaded from an editor-data .md file (frontmatter + body).
struct mcp_skill
{
    std::string name;
    std::string title;
    std::string description;
    std::string content;
    int order{1000};
};

const std::string k_server_instructions =
    "This server remote-controls the Unravel game engine editor (scenes, entities, components, "
    "assets, materials, scripts, play mode, viewport captures).\n"
    "Rules:\n"
    "1. Never create or edit the project's serialized files (.spfb scenes, .pfb prefabs, .mat "
    "materials) directly on disk - always go through the tools, which run the editor's real "
    "setters and validation.\n"
    "2. Components must be added (scene_add_components_batch; C# scripts via "
    "scene_add_scripts_batch) before typed property writes "
    "(scene_set_component_properties_batch).\n"
    "3. Mutating tools refuse play mode; stop play before editing.\n"
    "Before any non-trivial task, call skills_list and fetch the relevant workflow guides with "
    "skills_get - they encode correct tool ordering and engine-specific knowledge.";

auto trim(std::string_view value) -> std::string_view
{
    while(!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
    {
        value.remove_prefix(1);
    }
    while(!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
    {
        value.remove_suffix(1);
    }
    return value;
}

/// Parse "---\nkey: value\n...---\nbody" into a skill. Returns false when the
/// file has no frontmatter block; such files are skipped (not valid skills).
auto parse_skill_file(const fs::path& path, mcp_skill& out) -> bool
{
    std::ifstream input(path, std::ios::binary);
    if(!input.is_open())
    {
        return false;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();
    std::istringstream lines(text);
    std::string line;
    if(!std::getline(lines, line) || trim(line) != "---")
    {
        return false;
    }
    bool closed = false;
    while(std::getline(lines, line))
    {
        const auto trimmed = trim(line);
        if(trimmed == "---")
        {
            closed = true;
            break;
        }
        const auto colon = trimmed.find(':');
        if(colon == std::string_view::npos)
        {
            continue;
        }
        const auto key = trim(trimmed.substr(0, colon));
        const auto value = std::string(trim(trimmed.substr(colon + 1)));
        if(key == "name")
        {
            out.name = value;
        }
        else if(key == "title")
        {
            out.title = value;
        }
        else if(key == "description")
        {
            out.description = value;
        }
        else if(key == "order")
        {
            out.order = std::atoi(value.c_str());
        }
    }
    if(!closed)
    {
        return false;
    }
    std::string body;
    std::getline(lines, body, '\0');
    out.content = body;
    if(out.name.empty())
    {
        out.name = path.stem().generic_string();
    }
    if(out.title.empty())
    {
        out.title = out.name;
    }
    return !out.content.empty();
}

/// Load every *.md skill from editor data, sorted by frontmatter order then name.
/// Read per call so shipped skill files can be edited without restarting the editor.
auto load_skills(std::string& error) -> std::vector<mcp_skill>
{
    std::vector<mcp_skill> skills;
    fs::error_code ec;
    const auto dir = fs::resolve_protocol(k_skills_protocol_dir);
    if(!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    {
        error = std::string("Skill directory missing: ") + k_skills_protocol_dir;
        return skills;
    }
    for(const auto& entry : fs::directory_iterator(dir, ec))
    {
        if(!entry.is_regular_file(ec) || entry.path().extension() != ".md")
        {
            continue;
        }
        mcp_skill skill;
        if(parse_skill_file(entry.path(), skill))
        {
            skills.push_back(std::move(skill));
        }
        else
        {
            APPLOG_WARNING("MCP skill file skipped (missing frontmatter or empty): {}",
                           entry.path().generic_string());
        }
    }
    if(skills.empty() && error.empty())
    {
        error = std::string("No skill files found in ") + k_skills_protocol_dir;
    }
    std::sort(skills.begin(),
              skills.end(),
              [](const mcp_skill& lhs, const mcp_skill& rhs)
              {
                  if(lhs.order != rhs.order)
                  {
                      return lhs.order < rhs.order;
                  }
                  return lhs.name < rhs.name;
              });
    return skills;
}

} // namespace

auto get_server_instructions() -> const std::string&
{
    return k_server_instructions;
}

void register_skill_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "skills_list",
         .description =
             "List the workflow skills (agent guides) served by this editor. Call once at session "
             "start, then fetch the relevant guides with skills_get before non-trivial work - they "
             "encode correct tool ordering, dependencies between tools, and engine-specific "
             "knowledge.",
         .input_schema_json = R"json({"type":"object","properties":{}})json",
         .handler =
             [](rtti::context& /*ctx*/, const simdjson::dom::object& /*args*/) -> tool_result
         {
             std::string error;
             const auto skills = load_skills(error);
             if(skills.empty())
             {
                 return {.text = error, .is_error = true};
             }
             std::string json = "[";
             bool first = true;
             for(const auto& skill : skills)
             {
                 if(!first)
                 {
                     json += ",";
                 }
                 first = false;
                 json += fmt::format(R"({{"name":{},"title":{},"description":{}}})",
                                     make_json_string(skill.name),
                                     make_json_string(skill.title),
                                     make_json_string(skill.description));
             }
             json += "]";
             return {.text = fmt::format(R"({{"skills":{},"usage":"skills_get {{\"names\":[...]}}"}})", json),
                     .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "skills_get",
         .description =
             "Fetch one or more workflow skills by name (see skills_list). Returns the guides as "
             "markdown. Follow them while working - they are the editor's source of truth for "
             "correct MCP usage.",
         .input_schema_json =
             R"json({"type":"object","properties":{"name":{"type":"string"},"names":{"type":"array","items":{"type":"string"}}}})json",
         .handler =
             [](rtti::context& /*ctx*/, const simdjson::dom::object& args) -> tool_result
         {
             std::vector<std::string> names;
             std::string_view single;
             if(args["name"].get_string().get(single) == simdjson::SUCCESS)
             {
                 names.emplace_back(single);
             }
             simdjson::dom::array arr;
             if(args["names"].get_array().get(arr) == simdjson::SUCCESS)
             {
                 for(auto el : arr)
                 {
                     std::string_view value;
                     if(el.get_string().get(value) == simdjson::SUCCESS)
                     {
                         names.emplace_back(value);
                     }
                 }
             }
             if(names.empty())
             {
                 return {.text = "Pass name or names[]; see skills_list for available skills",
                         .is_error = true};
             }
             std::string error;
             const auto skills = load_skills(error);
             if(skills.empty())
             {
                 return {.text = error, .is_error = true};
             }
             std::string text;
             std::string missing;
             for(const auto& name : names)
             {
                 const auto it = std::find_if(skills.begin(),
                                              skills.end(),
                                              [&](const mcp_skill& skill)
                                              {
                                                  return skill.name == name;
                                              });
                 if(it == skills.end())
                 {
                     if(!missing.empty())
                     {
                         missing += ", ";
                     }
                     missing += name;
                     continue;
                 }
                 if(!text.empty())
                 {
                     text += "\n\n---\n\n";
                 }
                 text += it->content;
             }
             if(text.empty())
             {
                 return {.text = "Unknown skill(s): " + missing + " - see skills_list",
                         .is_error = true};
             }
             if(!missing.empty())
             {
                 text += "\n\n(Unknown skill(s) skipped: " + missing + ")";
             }
             return {.text = text, .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false});
}

} // namespace unravel::mcp
