#include "audit_types.h"

#include <algorithm>
#include <unordered_set>

namespace unravel::audit
{
namespace
{

template<typename Enum, std::size_t Count>
auto parse_by_name(hpp::string_view name, const std::array<Enum, Count>& values, const char* (*get_name)(Enum))
    -> hpp::optional<Enum>
{
    for(const auto value : values)
    {
        if(name == get_name(value))
        {
            return value;
        }
    }
    return hpp::nullopt;
}

constexpr std::array<severity, 3> ALL_SEVERITIES{severity::info, severity::warning, severity::error};
constexpr std::array<column_kind, 6> ALL_COLUMN_KINDS{column_kind::text,
                                                      column_kind::path,
                                                      column_kind::number,
                                                      column_kind::bytes,
                                                      column_kind::change,
                                                      column_kind::time};

constexpr std::array<subject_kind, 6> ALL_SUBJECT_KINDS{subject_kind::project,
                                                        subject_kind::asset,
                                                        subject_kind::setting,
                                                        subject_kind::script,
                                                        subject_kind::entity,
                                                        subject_kind::profile};
constexpr std::array<target_platform, PLATFORM_COUNT> ALL_PLATFORMS{target_platform::desktop,
                                                                    target_platform::android,
                                                                    target_platform::web};

} // namespace

auto subject::make_id() const -> std::string
{
    std::string id = get_subject_kind_name(kind);
    id += ':';
    id += key;
    if(!detail.empty())
    {
        id += '#';
        id += detail;
    }
    return id;
}

auto issue::get_id() const -> std::string
{
    std::string id = rule;
    id += '|';
    id += where.make_id();
    if(!discriminator.empty())
    {
        id += '|';
        id += discriminator;
    }
    return id;
}

auto report::find(const std::string& id) const -> const issue*
{
    const auto found = std::find_if(issues.begin(),
                                    issues.end(),
                                    [&id](const issue& item)
                                    {
                                        return item.get_id() == id;
                                    });
    return found != issues.end() ? &*found : nullptr;
}

auto report::find(const std::string& id) -> issue*
{
    return const_cast<issue*>(static_cast<const report&>(*this).find(id));
}

auto report::count(severity level) const -> std::size_t
{
    return static_cast<std::size_t>(std::count_if(issues.begin(),
                                                  issues.end(),
                                                  [level](const issue& item)
                                                  {
                                                      return !item.is_ignored && item.level == level;
                                                  }));
}

auto get_severity_name(severity level) -> const char*
{
    switch(level)
    {
        case severity::info:
            return "info";
        case severity::warning:
            return "warning";
        case severity::error:
            return "error";
    }
    return "warning";
}

auto parse_severity(hpp::string_view name) -> hpp::optional<severity>
{
    return parse_by_name(name, ALL_SEVERITIES, &get_severity_name);
}

auto get_area_name(area value) -> const char*
{
    switch(value)
    {
        case area::none:
            return "none";
        case area::correctness:
            return "correctness";
        case area::cpu:
            return "cpu";
        case area::gpu:
            return "gpu";
        case area::memory:
            return "memory";
        case area::download:
            return "download";
        case area::load_time:
            return "load_time";
        case area::quality:
            return "quality";
    }
    return "none";
}

auto parse_area(hpp::string_view name) -> hpp::optional<area>
{
    return parse_by_name(name, ALL_AREAS, &get_area_name);
}

auto get_area_names(area_mask mask) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(const auto value : ALL_AREAS)
    {
        if(has_area(mask, value))
        {
            names.emplace_back(get_area_name(value));
        }
    }
    return names;
}

auto get_subject_kind_name(subject_kind kind) -> const char*
{
    switch(kind)
    {
        case subject_kind::project:
            return "project";
        case subject_kind::asset:
            return "asset";
        case subject_kind::setting:
            return "setting";
        case subject_kind::script:
            return "script";
        case subject_kind::entity:
            return "entity";
        case subject_kind::profile:
            return "profile";
    }
    return "project";
}

auto parse_subject_kind(hpp::string_view name) -> hpp::optional<subject_kind>
{
    return parse_by_name(name, ALL_SUBJECT_KINDS, &get_subject_kind_name);
}

auto get_column_kind_name(column_kind kind) -> const char*
{
    switch(kind)
    {
        case column_kind::text:
            return "text";
        case column_kind::path:
            return "path";
        case column_kind::number:
            return "number";
        case column_kind::bytes:
            return "bytes";
        case column_kind::change:
            return "change";
        case column_kind::time:
            return "time";
    }
    return "text";
}

auto parse_column_kind(hpp::string_view name) -> hpp::optional<column_kind>
{
    return parse_by_name(name, ALL_COLUMN_KINDS, &get_column_kind_name);
}

auto insight_table::get_kind(std::size_t column) const -> column_kind
{
    return column < kinds.size() ? kinds[column] : column_kind::text;
}

auto get_rule_source_name(rule_source source) -> const char*
{
    switch(source)
    {
        case rule_source::assets:
            return "assets";
        case rule_source::settings:
            return "settings";
        case rule_source::content:
            return "content";
        case rule_source::scenes:
            return "scenes";
        case rule_source::scripts:
            return "scripts";
        case rule_source::code:
            return "code";
    }
    return "assets";
}

auto get_platform_name(target_platform platform) -> const char*
{
    switch(platform)
    {
        case target_platform::desktop:
            return "desktop";
        case target_platform::android:
            return "android";
        case target_platform::web:
            return "web";
    }
    return "desktop";
}

auto parse_platform(hpp::string_view name) -> hpp::optional<target_platform>
{
    return parse_by_name(name, ALL_PLATFORMS, &get_platform_name);
}

namespace
{

/// Whether the run checked the rule; a report from before the runs kept their rules counts every rule as checked.
auto has_checked(const report& value, const std::string& rule) -> bool
{
    return value.checked_rules.empty() ||
           std::find(value.checked_rules.begin(), value.checked_rules.end(), rule) != value.checked_rules.end();
}

} // namespace

auto compare_reports(const report& older, const report& newer) -> report_change
{
    std::unordered_set<std::string> older_ids;
    std::unordered_set<std::string> newer_ids;
    for(const auto& item : older.issues)
    {
        older_ids.insert(item.get_id());
    }
    report_change change;
    for(const auto& item : newer.issues)
    {
        auto id = item.get_id();
        if(older_ids.count(id) == 0 && has_checked(older, item.rule))
        {
            change.added.push_back(id);
        }
        newer_ids.insert(std::move(id));
    }
    for(const auto& item : older.issues)
    {
        auto id = item.get_id();
        if(newer_ids.count(id) == 0 && has_checked(newer, item.rule))
        {
            change.fixed.push_back(std::move(id));
        }
    }
    return change;
}

} // namespace unravel::audit
