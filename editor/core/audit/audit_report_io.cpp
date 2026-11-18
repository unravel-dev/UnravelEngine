#include "audit_report_io.h"
#include "audit_catalog.h"
#include "audit_config.h"

#include <serialization/associative_archive.h>
#include <serialization/serialization.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <fstream>
#include <sstream>

namespace unravel::audit
{

SAVE(subject)
{
    try_save(ar, ser20::make_nvp("kind", std::string(get_subject_kind_name(obj.kind))));
    try_save(ar, ser20::make_nvp("key", obj.key));
    try_save(ar, ser20::make_nvp("detail", obj.detail));
    try_save(ar, ser20::make_nvp("label", obj.label));
    try_save(ar, ser20::make_nvp("line", obj.line));
}

LOAD(subject)
{
    std::string kind;
    try_load(ar, ser20::make_nvp("kind", kind));
    obj.kind = parse_subject_kind(kind).value_or(subject_kind::project);
    try_load(ar, ser20::make_nvp("key", obj.key));
    try_load(ar, ser20::make_nvp("detail", obj.detail));
    try_load(ar, ser20::make_nvp("label", obj.label));
    try_load(ar, ser20::make_nvp("line", obj.line));
}

SAVE(issue)
{
    try_save(ar, ser20::make_nvp("id", obj.get_id()));
    try_save(ar, ser20::make_nvp("rule", obj.rule));
    try_save(ar, ser20::make_nvp("severity", std::string(get_severity_name(obj.level))));
    try_save(ar, ser20::make_nvp("areas", get_area_names(obj.areas)));
    try_save(ar, ser20::make_nvp("subject", obj.where));
    try_save(ar, ser20::make_nvp("discriminator", obj.discriminator));
    try_save(ar, ser20::make_nvp("message", obj.message));
    try_save(ar, ser20::make_nvp("details", obj.details));
    try_save(ar, ser20::make_nvp("lines", obj.lines));
    try_save(ar, ser20::make_nvp("call_path", obj.call_path));
    try_save(ar, ser20::make_nvp("profiles", obj.profiles));
    try_save(ar, ser20::make_nvp("fix", obj.fix));
    try_save(ar, ser20::make_nvp("ignored", obj.is_ignored));
    try_save(ar, ser20::make_nvp("ignore_reason", obj.ignore_reason));
}

LOAD(issue)
{
    try_load(ar, ser20::make_nvp("rule", obj.rule));
    std::string level;
    try_load(ar, ser20::make_nvp("severity", level));
    obj.level = parse_severity(level).value_or(severity::warning);
    std::vector<std::string> areas;
    try_load(ar, ser20::make_nvp("areas", areas));
    obj.areas = 0;
    for(const auto& name : areas)
    {
        if(const auto value = parse_area(name))
        {
            obj.areas = static_cast<area_mask>(obj.areas | static_cast<area_mask>(*value));
        }
    }
    try_load(ar, ser20::make_nvp("subject", obj.where));
    try_load(ar, ser20::make_nvp("discriminator", obj.discriminator));
    try_load(ar, ser20::make_nvp("message", obj.message));
    try_load(ar, ser20::make_nvp("details", obj.details));
    try_load(ar, ser20::make_nvp("lines", obj.lines));
    try_load(ar, ser20::make_nvp("call_path", obj.call_path));
    try_load(ar, ser20::make_nvp("profiles", obj.profiles));
    try_load(ar, ser20::make_nvp("fix", obj.fix));
    try_load(ar, ser20::make_nvp("ignored", obj.is_ignored));
    try_load(ar, ser20::make_nvp("ignore_reason", obj.ignore_reason));
}

SAVE(insight_cell)
{
    try_save(ar, ser20::make_nvp("text", obj.text));
    try_save(ar, ser20::make_nvp("number", obj.number));
    try_save(ar, ser20::make_nvp("is_number", obj.is_number));
    try_save(ar, ser20::make_nvp("link", obj.link));
}

LOAD(insight_cell)
{
    try_load(ar, ser20::make_nvp("text", obj.text));
    try_load(ar, ser20::make_nvp("number", obj.number));
    try_load(ar, ser20::make_nvp("is_number", obj.is_number));
    try_load(ar, ser20::make_nvp("link", obj.link));
}

SAVE(insight_table)
{
    try_save(ar, ser20::make_nvp("id", obj.id));
    try_save(ar, ser20::make_nvp("title", obj.title));
    try_save(ar, ser20::make_nvp("description", obj.description));
    try_save(ar, ser20::make_nvp("columns", obj.columns));
    try_save(ar, ser20::make_nvp("rows", obj.rows));
    std::vector<std::string> kinds;
    for(const auto kind : obj.kinds)
    {
        kinds.emplace_back(get_column_kind_name(kind));
    }
    try_save(ar, ser20::make_nvp("kinds", kinds));
    try_save(ar, ser20::make_nvp("group", obj.group));
    try_save(ar, ser20::make_nvp("sort_column", obj.sort_column));
    try_save(ar, ser20::make_nvp("sort_descending", obj.sort_descending));
    try_save(ar, ser20::make_nvp("empty_text", obj.empty_text));
}

LOAD(insight_table)
{
    try_load(ar, ser20::make_nvp("id", obj.id));
    try_load(ar, ser20::make_nvp("title", obj.title));
    try_load(ar, ser20::make_nvp("description", obj.description));
    try_load(ar, ser20::make_nvp("columns", obj.columns));
    try_load(ar, ser20::make_nvp("rows", obj.rows));
    // Reports from before the columns had kinds read as text columns.
    std::vector<std::string> kinds;
    try_load(ar, ser20::make_nvp("kinds", kinds));
    obj.kinds.clear();
    for(const auto& name : kinds)
    {
        obj.kinds.push_back(parse_column_kind(name).value_or(column_kind::text));
    }
    try_load(ar, ser20::make_nvp("group", obj.group));
    try_load(ar, ser20::make_nvp("sort_column", obj.sort_column));
    try_load(ar, ser20::make_nvp("sort_descending", obj.sort_descending));
    try_load(ar, ser20::make_nvp("empty_text", obj.empty_text));
}

namespace
{

constexpr const char* REPORT_CACHE_FOLDER = "cache/audit";
constexpr const char* REPORT_FILE_NAME = "last_report.json";

/// A CSV field: quoted when it holds a separator, a quote or a line break, its quotes doubled.
auto csv_field(const std::string& text) -> std::string
{
    if(text.find_first_of(",\"\n\r") == std::string::npos)
    {
        return text;
    }
    std::string quoted = "\"";
    for(const char c : text)
    {
        quoted += c;
        if(c == '"')
        {
            quoted += '"';
        }
    }
    quoted += '"';
    return quoted;
}

auto join(const std::vector<std::string>& values, const char* separator) -> std::string
{
    std::string text;
    for(const auto& value : values)
    {
        if(!text.empty())
        {
            text += separator;
        }
        text += value;
    }
    return text;
}

/// The lines of an issue, "12, 40"; its subject's line when it lists none.
auto join_lines(const issue& item) -> std::string
{
    if(item.lines.empty())
    {
        return item.where.line > 0 ? std::to_string(item.where.line) : std::string{};
    }
    std::string text;
    for(const auto line : item.lines)
    {
        if(!text.empty())
        {
            text += ", ";
        }
        text += std::to_string(line);
    }
    return text;
}

} // namespace

auto write_report(const report& value) -> std::string
{
    std::ostringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("version", REPORT_FORMAT_VERSION));
        try_save(ar, ser20::make_nvp("engine_version", value.engine_version));
        try_save(ar, ser20::make_nvp("project", value.project));
        try_save(ar, ser20::make_nvp("finished_at", value.finished_at));
        try_save(ar, ser20::make_nvp("seconds", value.seconds));
        try_save(ar, ser20::make_nvp("profiles", value.profiles));
        try_save(ar, ser20::make_nvp("counts",
                                     std::vector<std::size_t>{value.count(severity::error),
                                                              value.count(severity::warning),
                                                              value.count(severity::info)}));
        try_save(ar, ser20::make_nvp("issues", value.issues));
        try_save(ar, ser20::make_nvp("insights", value.insights));
        try_save(ar, ser20::make_nvp("notes", value.notes));
        try_save(ar, ser20::make_nvp("checked_rules", value.checked_rules));
        try_save(ar, ser20::make_nvp("error", value.error));
    }
    return stream.str();
}

auto read_report(const std::string& text, report& value, std::string& error) -> bool
{
    report read;
    try
    {
        std::istringstream stream(text);
        auto ar = ser20::create_iarchive_associative(stream);
        std::uint32_t version{};
        if(!try_load(ar, ser20::make_nvp("version", version)))
        {
            error = "The file has no report version: it is not an audit report.";
            return false;
        }
        if(version > REPORT_FORMAT_VERSION)
        {
            error = "A newer engine wrote the report. Update the engine to read it.";
            return false;
        }
        try_load(ar, ser20::make_nvp("engine_version", read.engine_version));
        try_load(ar, ser20::make_nvp("project", read.project));
        try_load(ar, ser20::make_nvp("finished_at", read.finished_at));
        try_load(ar, ser20::make_nvp("seconds", read.seconds));
        try_load(ar, ser20::make_nvp("profiles", read.profiles));
        try_load(ar, ser20::make_nvp("issues", read.issues));
        try_load(ar, ser20::make_nvp("insights", read.insights));
        try_load(ar, ser20::make_nvp("notes", read.notes));
        try_load(ar, ser20::make_nvp("checked_rules", read.checked_rules));
        try_load(ar, ser20::make_nvp("error", read.error));
    }
    catch(const std::exception& e)
    {
        error = std::string("The report could not be read: ") + e.what();
        return false;
    }
    value = std::move(read);
    return true;
}

auto save_report(const fs::path& file, const report& value) -> bool
{
    return write_text_file(file, write_report(value));
}

auto load_report(const fs::path& file, report& value, std::string& error) -> bool
{
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        error = "The report file could not be opened.";
        return false;
    }
    return read_report(fs::read_stream_str(stream), value, error);
}

auto write_issues_csv(const report& value) -> std::string
{
    std::string csv = "Severity,Rule,Title,Subject,Detail,Lines,Profiles,Message,Details,Call Path,Ignored,Ignore Reason\n";
    for(const auto& item : value.issues)
    {
        const auto* owner = find_rule(item.rule);
        const std::vector<std::string> fields{get_severity_name(item.level),
                                              item.rule,
                                              owner != nullptr ? owner->title : std::string{},
                                              item.where.key,
                                              item.where.detail,
                                              join_lines(item),
                                              join(item.profiles, "; "),
                                              item.message,
                                              join(item.details, " | "),
                                              join(item.call_path, " > "),
                                              item.is_ignored ? "yes" : "no",
                                              item.ignore_reason};
        std::string row;
        for(const auto& field : fields)
        {
            if(!row.empty())
            {
                row += ',';
            }
            row += csv_field(field);
        }
        csv += row;
        csv += '\n';
    }
    return csv;
}

auto get_last_report_path(const fs::path& project_root) -> fs::path
{
    return project_root / REPORT_CACHE_FOLDER / REPORT_FILE_NAME;
}

} // namespace unravel::audit
