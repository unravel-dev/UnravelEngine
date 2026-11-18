#include "audit_code.h"
#include "audit_rules.h"

#include <hpp/uuid.hpp>
#include <logging/logging.h>
#include <ser20/external/simdjson/simdjson.h>
#include <subprocess/subprocess.hpp>

#include <fstream>

namespace unravel::audit
{
namespace
{

/// The most the tool may print before the call ends it: its output goes to a file, so this holds errors only.
constexpr std::size_t TOOL_OUTPUT_LIMIT = 1024 * 1024;

auto get_string(const simdjson::dom::element& object, const char* name) -> std::string
{
    std::string_view value;
    if(object[name].get(value) != simdjson::SUCCESS)
    {
        return {};
    }
    return std::string(value);
}

auto get_unsigned(const simdjson::dom::element& object, const char* name) -> std::uint64_t
{
    std::uint64_t value{};
    if(object[name].get(value) != simdjson::SUCCESS)
    {
        std::int64_t signed_value{};
        if(object[name].get(signed_value) == simdjson::SUCCESS && signed_value > 0)
        {
            return static_cast<std::uint64_t>(signed_value);
        }
        return 0;
    }
    return value;
}

auto read_finding(const simdjson::dom::element& entry, const fs::path& project_root) -> code_finding
{
    code_finding finding;
    finding.rule = get_string(entry, "rule");
    finding.file = detail::make_project_key(project_root, get_string(entry, "file"));
    finding.line = static_cast<std::uint32_t>(get_unsigned(entry, "line"));
    finding.method = get_string(entry, "method");
    finding.target = get_string(entry, "target");
    finding.detail = get_string(entry, "detail");
    simdjson::dom::array path;
    if(entry["path"].get(path) == simdjson::SUCCESS)
    {
        for(const auto step : path)
        {
            std::string_view text;
            if(step.get(text) == simdjson::SUCCESS)
            {
                finding.path.emplace_back(text);
            }
        }
    }
    return finding;
}

auto read_entry_point(const simdjson::dom::element& entry, const fs::path& project_root) -> code_entry_point
{
    code_entry_point point;
    point.method = get_string(entry, "method");
    point.file = detail::make_project_key(project_root, get_string(entry, "file"));
    point.line = static_cast<std::uint32_t>(get_unsigned(entry, "line"));
    point.reachable = static_cast<std::size_t>(get_unsigned(entry, "reachable"));
    return point;
}

auto read_whole_text(const fs::path& file, std::string& text) -> bool
{
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        return false;
    }
    text = fs::read_stream_str(stream);
    return true;
}

auto get_last_lines(const std::string& output, std::size_t count) -> std::string
{
    std::size_t start = output.size();
    for(std::size_t found = 0; found < count && start > 0;)
    {
        start = output.rfind('\n', start - 1);
        if(start == std::string::npos)
        {
            return output;
        }
        found += start + 1 < output.size() ? 1 : 0;
    }
    return output.substr(start + 1);
}

} // namespace

auto read_code_analysis(const std::string& text, const fs::path& project_root, code_analysis& result, std::string& error) -> bool
{
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if(parser.parse(text).get(root) != simdjson::SUCCESS)
    {
        error = "Its output is not JSON.";
        return false;
    }
    std::int64_t version{};
    if(root["version"].get(version) != simdjson::SUCCESS || version != CODE_ANALYSIS_FORMAT_VERSION)
    {
        error = "Its output is of another format version: the tool and the editor are from different builds.";
        return false;
    }
    code_analysis read;
    read.ran = true;
    read.methods = static_cast<std::size_t>(get_unsigned(root, "methods"));
    simdjson::dom::array findings;
    if(root["findings"].get(findings) == simdjson::SUCCESS)
    {
        for(const auto entry : findings)
        {
            read.findings.push_back(read_finding(entry, project_root));
        }
    }
    simdjson::dom::array entry_points;
    if(root["entry_points"].get(entry_points) == simdjson::SUCCESS)
    {
        for(const auto entry : entry_points)
        {
            read.entry_points.push_back(read_entry_point(entry, project_root));
        }
    }
    simdjson::dom::array errors;
    if(root["errors"].get(errors) == simdjson::SUCCESS)
    {
        for(const auto entry : errors)
        {
            std::string_view message;
            if(entry.get(message) == simdjson::SUCCESS && read.error.empty())
            {
                read.error = std::string(message);
            }
        }
    }
    result = std::move(read);
    return true;
}

auto run_code_analysis(const code_tool_paths& paths, const std::function<bool()>& should_stop) -> code_analysis
{
    code_analysis result;
    fs::error_code ec;
    if(!fs::exists(paths.tool, ec))
    {
        result.error = fmt::format("The analysis tool is missing ({}). Build the editor's code_audit target.",
                                   paths.tool.generic_string());
        return result;
    }
    if(!fs::exists(paths.assembly, ec))
    {
        result.error = "The project's scripts have no compiled library yet.";
        return result;
    }
    const auto output = fs::temp_directory_path(ec) / ("unravel-code-audit-" + hpp::to_string(generate_uuid()) + ".json");
    std::vector<std::string> args{paths.dotnet, "exec", paths.tool.string(), "--assembly", paths.assembly.string()};
    for(const auto& folder : paths.reference_folders)
    {
        args.emplace_back("--references");
        args.push_back(folder.string());
    }
    args.emplace_back("--out");
    args.push_back(output.string());
    const auto call = subprocess::call(args, {}, {}, should_stop, TOOL_OUTPUT_LIMIT);
    std::string text;
    const bool has_output = read_whole_text(output, text);
    fs::remove(output, ec);
    if(should_stop && should_stop())
    {
        result.error = "The run was stopped.";
        return result;
    }
    if(!call.started)
    {
        result.error = fmt::format("The .NET host could not start ({}).", paths.dotnet);
        return result;
    }
    if(call.retcode != 0 || !has_output)
    {
        result.error = fmt::format("The tool ended with code {}: {}", call.retcode, get_last_lines(call.out_output, 3));
        return result;
    }
    std::string error;
    if(!read_code_analysis(text, paths.project_root, result, error))
    {
        result = {};
        result.error = error;
    }
    return result;
}

} // namespace unravel::audit
