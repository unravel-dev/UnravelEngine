#include "mcp_protocol.h"
#include "mcp_tool_registry.h"
#include "mcp_tools_common.h"

#include <audit/audit_catalog.h>
#include <editor/audit/audit_manager.h>
#include <editor/system/mcp_manager.h>

#include <algorithm>
#include <chrono>

namespace unravel::mcp
{
namespace
{

constexpr int64_t AUDIT_DEFAULT_WAIT_MS = 120000;
constexpr int64_t AUDIT_MAX_WAIT_MS = 600000;
constexpr auto AUDIT_POLL_INTERVAL = std::chrono::milliseconds(100);
constexpr int64_t AUDIT_DEFAULT_LIMIT = 50;
constexpr int64_t AUDIT_MAX_LIMIT = 500;
constexpr std::size_t AUDIT_TOP_RULES = 8;

auto audit_counts_json(const audit::report& report) -> std::string
{
    std::size_t ignored = 0;
    for(const auto& item : report.issues)
    {
        ignored += item.is_ignored ? 1 : 0;
    }
    return fmt::format(R"({{"error":{},"warning":{},"info":{},"ignored":{}}})",
                       report.count(audit::severity::error),
                       report.count(audit::severity::warning),
                       report.count(audit::severity::info),
                       ignored);
}

auto audit_subject_json(const audit::subject& where) -> std::string
{
    return fmt::format(R"({{"kind":{},"key":{},"detail":{},"label":{},"line":{}}})",
                       make_json_string(audit::get_subject_kind_name(where.kind)),
                       make_json_string(where.key),
                       make_json_string(where.detail),
                       make_json_string(where.label),
                       where.line);
}

auto audit_lines_json(const std::vector<std::uint32_t>& lines) -> std::string
{
    std::string json = "[";
    for(const auto line : lines)
    {
        if(json.size() > 1)
        {
            json += ',';
        }
        json += std::to_string(line);
    }
    return json + "]";
}

auto audit_issue_json(const audit::issue& item, bool is_fixed) -> std::string
{
    const auto* owner = audit::find_rule(item.rule);
    return fmt::format(
        R"({{"id":{},"rule":{},"title":{},"severity":{},"areas":{},"subject":{},"message":{},"details":{},"lines":{},"call_path":{},"profiles":{},"fix":{},"fix_label":{},"fix_applied":{},"ignored":{},"ignore_reason":{}}})",
        make_json_string(item.get_id()),
        make_json_string(item.rule),
        make_json_string(owner != nullptr ? owner->title : std::string{}),
        make_json_string(audit::get_severity_name(item.level)),
        strings_to_json_array(audit::get_area_names(item.areas)),
        audit_subject_json(item.where),
        make_json_string(item.message),
        strings_to_json_array(item.details),
        audit_lines_json(item.lines),
        strings_to_json_array(item.call_path),
        strings_to_json_array(item.profiles),
        make_json_string(item.fix),
        make_json_string(audit::get_fix_label(item.fix)),
        bool_to_json(is_fixed),
        bool_to_json(item.is_ignored),
        make_json_string(item.ignore_reason));
}

auto audit_top_rules_json(const audit::report& report) -> std::string
{
    std::vector<std::pair<std::string, std::size_t>> counts;
    for(const auto& item : report.issues)
    {
        if(item.is_ignored)
        {
            continue;
        }
        auto found = std::find_if(counts.begin(),
                                  counts.end(),
                                  [&item](const auto& entry)
                                  {
                                      return entry.first == item.rule;
                                  });
        if(found == counts.end())
        {
            counts.emplace_back(item.rule, 1);
        }
        else
        {
            found->second++;
        }
    }
    std::stable_sort(counts.begin(),
                     counts.end(),
                     [](const auto& lhs, const auto& rhs)
                     {
                         return lhs.second > rhs.second;
                     });
    std::string json = "[";
    for(std::size_t index = 0; index < counts.size() && index < AUDIT_TOP_RULES; ++index)
    {
        const auto* owner = audit::find_rule(counts[index].first);
        json += fmt::format(R"({}{{"rule":{},"title":{},"count":{}}})",
                            index == 0 ? "" : ",",
                            make_json_string(counts[index].first),
                            make_json_string(owner != nullptr ? owner->title : std::string{}),
                            counts[index].second);
    }
    return json + "]";
}

/// What a report says about itself: when, how long, what it judged and could not check.
auto audit_report_json(const audit::report& report, const audit::report_change& change) -> std::string
{
    return fmt::format(
        R"({{"project":{},"engine_version":{},"finished_at":{},"seconds":{:.2f},"profiles":{},"counts":{},"top_rules":{},"since_previous":{{"new":{},"no_longer_found":{}}},"notes":{},"insights":{}}})",
        make_json_string(report.project),
        make_json_string(report.engine_version),
        report.finished_at,
        report.seconds,
        strings_to_json_array(report.profiles),
        audit_counts_json(report),
        audit_top_rules_json(report),
        change.added.size(),
        change.fixed.size(),
        strings_to_json_array(report.notes),
        [&report]()
        {
            std::vector<std::string> ids;
            for(const auto& table : report.insights)
            {
                ids.push_back(table.id);
            }
            return strings_to_json_array(ids);
        }());
}

auto audit_insight_json(const audit::insight_table& table, int64_t limit, int64_t offset) -> std::string
{
    std::string rows = "[";
    const auto start = static_cast<std::size_t>(std::max<int64_t>(offset, 0));
    const auto end = std::min(table.rows.size(), start + static_cast<std::size_t>(limit));
    for(std::size_t index = start; index < end; ++index)
    {
        std::vector<std::string> cells;
        for(const auto& cell : table.rows[index])
        {
            cells.push_back(cell.text);
        }
        rows += (index == start ? "" : ",") + strings_to_json_array(cells);
    }
    rows += "]";
    return fmt::format(R"({{"insight":{{"id":{},"title":{},"description":{},"columns":{},"total":{},"rows":{},"next_offset":{}}}}})",
                       make_json_string(table.id),
                       make_json_string(table.title),
                       make_json_string(table.description),
                       strings_to_json_array(table.columns),
                       table.rows.size(),
                       rows,
                       end < table.rows.size() ? std::to_string(end) : std::string("null"));
}

auto get_audit_manager(rtti::context& ctx, std::string& error) -> audit_manager*
{
    if(get_open_project(ctx, error) == nullptr)
    {
        return nullptr;
    }
    return &ctx.get_cached<audit_manager>();
}

auto require_report(audit_manager& manager, std::string& error) -> const audit::report*
{
    const auto* report = manager.get_report();
    if(report == nullptr)
    {
        error = "The project has no audit report yet. Run audit_run first.";
    }
    return report;
}

/// Whether an issue passes audit_get's filters.
struct issue_query
{
    hpp::optional<audit::severity> min_severity;
    std::string rule;
    std::string profile;
    hpp::optional<audit::area> area;
    std::string search;
    bool include_ignored{false};

    auto matches(const audit::issue& item) const -> bool
    {
        if(item.is_ignored && !include_ignored)
        {
            return false;
        }
        if(min_severity && item.level < *min_severity)
        {
            return false;
        }
        if(!rule.empty() && item.rule != rule)
        {
            return false;
        }
        if(!profile.empty() && !item.profiles.empty() &&
           std::find(item.profiles.begin(), item.profiles.end(), profile) == item.profiles.end())
        {
            return false;
        }
        if(area && !audit::has_area(item.areas, *area))
        {
            return false;
        }
        if(search.empty())
        {
            return true;
        }
        return contains_ci(item.message, search) || contains_ci(item.where.key, search) ||
               contains_ci(item.where.label, search) || contains_ci(item.where.detail, search) ||
               contains_ci(item.rule, search);
    }
};

auto read_issue_query(const simdjson::dom::object& args, issue_query& query, std::string& error) -> bool
{
    std::string text;
    if(read_string(args, "severity", text))
    {
        query.min_severity = audit::parse_severity(text);
        if(!query.min_severity)
        {
            error = "severity must be error, warning or info.";
            return false;
        }
    }
    if(read_string(args, "area", text))
    {
        query.area = audit::parse_area(text);
        if(!query.area)
        {
            error = "area must be one of correctness, cpu, gpu, memory, download, load_time, quality.";
            return false;
        }
    }
    read_string(args, "rule", query.rule);
    read_string(args, "profile", query.profile);
    read_string(args, "search", query.search);
    read_bool(args, "include_ignored", query.include_ignored);
    if(!query.rule.empty() && audit::find_rule(query.rule) == nullptr)
    {
        error = "No rule has the id " + query.rule + ". audit_get_rules lists them.";
        return false;
    }
    return true;
}

auto handle_audit_run(rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
{
    auto& mcp = ctx.get_cached<mcp_manager>();
    audit_manager::run_request request;
    read_bool(args, "scan_documents", request.scan_documents);
    read_bool(args, "analyze_scripts", request.analyze_code);
    bool wait = true;
    read_bool(args, "wait", wait);
    const auto timeout = read_timeout_ms(args, AUDIT_DEFAULT_WAIT_MS, AUDIT_MAX_WAIT_MS);

    std::string start_error;
    const auto started = mcp.invoke_on_main(
        [&ctx, &request, &start_error]() -> bool
        {
            std::string error;
            auto* manager = get_audit_manager(ctx, error);
            if(manager == nullptr || !manager->start(ctx, request, error))
            {
                start_error = error;
                return false;
            }
            return true;
        });
    if(!started)
    {
        return {.text = "The main thread did not take the call; it is safe to send it again.", .is_error = true};
    }
    if(!*started)
    {
        return {.text = start_error, .is_error = true};
    }
    auto is_running = [&]() -> hpp::optional<bool>
    {
        return mcp.invoke_on_main(
            [&ctx]() -> bool
            {
                return ctx.get_cached<audit_manager>().is_running();
            });
    };
    auto running = is_running();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while(wait && (!running || *running) && std::chrono::steady_clock::now() < deadline &&
          mcp.sleep_unless_stopping(AUDIT_POLL_INTERVAL))
    {
        running = is_running();
    }
    const auto result = mcp.invoke_on_main(
        [&ctx]() -> std::string
        {
            auto& manager = ctx.get_cached<audit_manager>();
            if(manager.is_running())
            {
                const auto progress = manager.get_progress();
                return fmt::format(
                    R"({{"running":true,"stage":{},"item":{},"done":{:.2f},"seconds":{:.1f},"note":"The audit is still running: call audit_run with wait:true again later, or audit_get once it is done."}})",
                    make_json_string(audit::get_run_stage_title(progress.current)),
                    make_json_string(progress.item),
                    progress.done,
                    progress.seconds);
            }
            if(!manager.get_run_error().empty())
            {
                return fmt::format(R"({{"running":false,"error":{}}})", make_json_string(manager.get_run_error()));
            }
            const auto* report = manager.get_report();
            if(report == nullptr)
            {
                return R"({"running":false,"error":"The run left no report."})";
            }
            return fmt::format(R"({{"running":false,"report":{},"next":"audit_get lists the issues, worst first."}})",
                               audit_report_json(*report, manager.get_change()));
        });
    if(!result)
    {
        return {.text = "The main thread did not answer; the audit may still be running. Call audit_get later.", .is_error = true};
    }
    return {.text = *result};
}

auto handle_audit_get(rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
{
    std::string error;
    auto* manager = get_audit_manager(ctx, error);
    const auto* report = manager != nullptr ? require_report(*manager, error) : nullptr;
    if(report == nullptr)
    {
        return {.text = error, .is_error = true};
    }
    const auto limit = std::clamp<int64_t>(read_int64_or(args, "limit", AUDIT_DEFAULT_LIMIT), 1, AUDIT_MAX_LIMIT);
    const auto offset = std::max<int64_t>(read_int64_or(args, "offset", 0), 0);
    std::string insight;
    if(read_string(args, "insight", insight))
    {
        for(const auto& table : report->insights)
        {
            if(table.id == insight)
            {
                return {.text = audit_insight_json(table, limit, offset)};
            }
        }
        return {.text = "The report has no insight " + insight + "; audit_get lists the ones it has.", .is_error = true};
    }
    std::string id;
    if(read_string(args, "id", id))
    {
        const auto* item = report->find(id);
        if(item == nullptr)
        {
            return {.text = "The report has no issue with id " + id + ".", .is_error = true};
        }
        const auto* owner = audit::find_rule(item->rule);
        return {.text = fmt::format(R"({{"issue":{},"why":{},"what_to_do":{}}})",
                                    audit_issue_json(*item, manager->is_fixed(id)),
                                    make_json_string(owner != nullptr ? owner->description : std::string{}),
                                    make_json_string(owner != nullptr ? owner->recommendation : std::string{}))};
    }
    issue_query query;
    if(!read_issue_query(args, query, error))
    {
        return {.text = error, .is_error = true};
    }
    std::vector<const audit::issue*> matched;
    for(const auto& item : report->issues)
    {
        if(query.matches(item))
        {
            matched.push_back(&item);
        }
    }
    std::string issues = "[";
    const auto start = std::min(static_cast<std::size_t>(offset), matched.size());
    const auto end = std::min(matched.size(), start + static_cast<std::size_t>(limit));
    for(std::size_t index = start; index < end; ++index)
    {
        issues += (index == start ? "" : ",") + audit_issue_json(*matched[index], manager->is_fixed(matched[index]->get_id()));
    }
    issues += "]";
    return {.text = fmt::format(R"({{"report":{},"total":{},"issues":{},"next_offset":{}}})",
                                audit_report_json(*report, manager->get_change()),
                                matched.size(),
                                issues,
                                end < matched.size() ? std::to_string(end) : std::string("null"))};
}

auto audit_rule_json(const audit::rule& owner, const audit::config& settings) -> std::string
{
    std::string thresholds = "[";
    for(std::size_t index = 0; index < owner.thresholds.size(); ++index)
    {
        const auto& item = owner.thresholds[index];
        thresholds += fmt::format(
            R"({}{{"name":{},"label":{},"unit":{},"description":{},"values":{{"desktop":{},"android":{},"web":{}}},"defaults":{{"desktop":{},"android":{},"web":{}}}}})",
            index == 0 ? "" : ",",
            make_json_string(item.name),
            make_json_string(item.label),
            make_json_string(item.unit),
            make_json_string(item.description),
            settings.get_threshold(owner, item.name, target_platform::desktop),
            settings.get_threshold(owner, item.name, target_platform::android),
            settings.get_threshold(owner, item.name, target_platform::web),
            item.values[0],
            item.values[1],
            item.values[2]);
    }
    thresholds += "]";
    return fmt::format(
        R"({{"id":{},"title":{},"severity":{},"areas":{},"source":{},"enabled":{},"per_profile":{},"description":{},"recommendation":{},"thresholds":{}}})",
        make_json_string(owner.id),
        make_json_string(owner.title),
        make_json_string(audit::get_severity_name(owner.default_severity)),
        strings_to_json_array(audit::get_area_names(owner.areas)),
        make_json_string(audit::get_rule_source_name(owner.source)),
        bool_to_json(settings.is_rule_enabled(owner.id)),
        bool_to_json(owner.per_profile),
        make_json_string(owner.description),
        make_json_string(owner.recommendation),
        thresholds);
}

auto handle_audit_get_rules(rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
{
    (void)args;
    std::string error;
    auto* manager = get_audit_manager(ctx, error);
    if(manager == nullptr)
    {
        return {.text = error, .is_error = true};
    }
    std::string rules = "[";
    bool first = true;
    for(const auto& owner : audit::get_rules())
    {
        rules += (first ? "" : ",") + audit_rule_json(owner, manager->get_config());
        first = false;
    }
    rules += "]";
    return {.text = fmt::format(R"({{"rules":{},"settings_error":{}}})", rules, make_json_string(manager->get_config_error()))};
}

/// One audit_set_rules item, checked before anything changes.
struct rule_change
{
    std::string rule;
    hpp::optional<bool> enabled;
    std::vector<std::tuple<std::string, target_platform, int64_t>> thresholds;
};

auto read_rule_change(const simdjson::dom::element& element, rule_change& change, std::string& error) -> bool
{
    simdjson::dom::object item;
    if(!read_object(element, item, error))
    {
        return false;
    }
    if(!read_string(item, "rule", change.rule))
    {
        error = "rule is required";
        return false;
    }
    const auto* owner = audit::find_rule(change.rule);
    if(owner == nullptr)
    {
        error = "no rule has the id " + change.rule;
        return false;
    }
    bool enabled = false;
    if(read_bool(item, "enabled", enabled))
    {
        change.enabled = enabled;
    }
    simdjson::dom::object thresholds;
    if(item["thresholds"].get(thresholds) != simdjson::SUCCESS)
    {
        return true;
    }
    for(const auto field : thresholds)
    {
        const std::string name(field.key);
        const bool has_threshold = std::any_of(owner->thresholds.begin(),
                                               owner->thresholds.end(),
                                               [&name](const audit::threshold& entry)
                                               {
                                                   return entry.name == name;
                                               });
        simdjson::dom::object values;
        if(!has_threshold || field.value.get(values) != simdjson::SUCCESS)
        {
            error = fmt::format("{} has no threshold {} (or its value is not an object of platforms)", change.rule, name);
            return false;
        }
        for(const auto platform_value : values)
        {
            const auto platform = audit::parse_platform(std::string(platform_value.key));
            int64_t value{};
            if(!platform || platform_value.value.get(value) != simdjson::SUCCESS || value < 0)
            {
                error = fmt::format("{}.{} takes desktop, android or web with a whole number of at least 0", change.rule, name);
                return false;
            }
            change.thresholds.emplace_back(name, *platform, value);
        }
    }
    return true;
}

auto handle_audit_set_rules(rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
{
    std::string error;
    auto* manager = get_audit_manager(ctx, error);
    if(manager == nullptr)
    {
        return {.text = error, .is_error = true};
    }
    simdjson::dom::array items;
    if(!read_required_array(args, "items", items, error))
    {
        return {.text = error, .is_error = true};
    }
    std::vector<rule_change> changes;
    std::size_t index = 0;
    for(const auto element : items)
    {
        rule_change change;
        if(!read_rule_change(element, change, error))
        {
            return {.text = make_batch_item_error(index, error, "Nothing changed"), .is_error = true};
        }
        changes.push_back(std::move(change));
        ++index;
    }
    for(const auto& change : changes)
    {
        if(change.enabled && !manager->set_rule_enabled(change.rule, *change.enabled, error))
        {
            return {.text = error, .is_error = true};
        }
        for(const auto& [name, platform, value] : change.thresholds)
        {
            if(!manager->set_threshold(change.rule, name, platform, value, error))
            {
                return {.text = error, .is_error = true};
            }
        }
    }
    return {.text = fmt::format(
                R"({{"changed":{},"note":"The project's audit settings (settings/audit.cfg) changed; the next audit_run applies them."}})",
                changes.size())};
}

auto handle_audit_fix(rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
{
    std::string error;
    auto* manager = get_audit_manager(ctx, error);
    const auto* report = manager != nullptr ? require_report(*manager, error) : nullptr;
    if(report == nullptr)
    {
        return {.text = error, .is_error = true};
    }
    std::vector<std::string> ids;
    if(!read_string_array(args, "ids", ids, error) || ids.empty())
    {
        return {.text = error.empty() ? std::string("ids must name at least one issue.") : error, .is_error = true};
    }
    for(std::size_t index = 0; index < ids.size(); ++index)
    {
        const auto* item = report->find(ids[index]);
        if(item == nullptr || item->fix.empty())
        {
            return {.text = make_batch_item_error(index,
                                                  item == nullptr ? "the last report has no issue with this id"
                                                                  : "this issue has no fix the editor can apply",
                                                  "Nothing changed"),
                    .is_error = true};
        }
    }
    std::vector<std::string> applied;
    for(const auto& id : ids)
    {
        if(!manager->apply_fix(ctx, id, error))
        {
            return {.text = fmt::format(R"({{"applied":{},"error":{}}})", strings_to_json_array(applied), make_json_string(error)),
                    .is_error = true};
        }
        applied.push_back(id);
    }
    return {.text = fmt::format(
                R"({{"applied":{},"note":"Run audit_run again to confirm the fixes; assets whose import settings changed compile first."}})",
                strings_to_json_array(applied))};
}

auto handle_audit_ignore(rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
{
    std::string error;
    auto* manager = get_audit_manager(ctx, error);
    const auto* report = manager != nullptr ? require_report(*manager, error) : nullptr;
    if(report == nullptr)
    {
        return {.text = error, .is_error = true};
    }
    std::vector<std::string> ids;
    if(!read_string_array(args, "ids", ids, error) || ids.empty())
    {
        return {.text = error.empty() ? std::string("ids must name at least one issue.") : error, .is_error = true};
    }
    bool ignore = true;
    read_bool(args, "ignore", ignore);
    std::string reason;
    read_string(args, "reason", reason);
    if(ignore && reason.empty())
    {
        return {.text = "reason is required to ignore: say why the issue is fine, for whoever reads the project's audit "
                        "settings. Nothing changed.",
                .is_error = true};
    }
    for(std::size_t index = 0; index < ids.size(); ++index)
    {
        const auto* item = report->find(ids[index]);
        const bool is_known = item != nullptr || manager->get_config().find_ignore(ids[index]) != nullptr;
        if(!is_known)
        {
            return {.text = make_batch_item_error(index, "the last report has no issue with this id", "Nothing changed"),
                    .is_error = true};
        }
        if(!ignore && manager->get_config().find_ignore(ids[index]) == nullptr)
        {
            return {.text = make_batch_item_error(index, "the issue is not ignored", "Nothing changed"), .is_error = true};
        }
    }
    for(const auto& id : ids)
    {
        const bool done = ignore ? manager->ignore(id, reason, error) : manager->unignore(id, error);
        if(!done)
        {
            return {.text = error, .is_error = true};
        }
    }
    return {.text = fmt::format(R"({{"{}":{},"note":"The project's audit settings (settings/audit.cfg) keep the change."}})",
                                ignore ? "ignored" : "no_longer_ignored",
                                strings_to_json_array(ids))};
}

} // namespace

void register_audit_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "audit_run",
         .description =
             "Runs the project audit: checks every asset, setting, scene, prefab, material, controller and the compiled "
             "scripts against each settings profile, as the Project Audit window's Run Audit does, and returns the "
             "report's summary: counts by severity, the rules with the most issues, what changed since the run before, "
             "and notes on what it could not check. Waits for the run (wait, default true; wait_ms, default 120000); "
             "a run still going is reported with its step. Refused while the game plays, while assets compile, or while "
             "a run is going. scan_documents false skips loading scenes and prefabs (missing references, joints, "
             "components); analyze_scripts false skips the script analysis. Then audit_get lists the issues.",
         .input_schema_json =
             R"json({"type":"object","properties":{"scan_documents":{"type":"boolean","description":"Load scenes, prefabs, materials and controllers; default true"},"analyze_scripts":{"type":"boolean","description":"Analyze the compiled scripts' update methods; default true"},"wait":{"type":"boolean","description":"Wait for the run to finish; default true"},"wait_ms":{"type":"integer","minimum":0,"maximum":600000,"description":"How long to wait; default 120000"}}})json",
         .handler = &handle_audit_run,
         .requires_main_thread = false,
         .effect = tool_effect::read_only});
    registry.add(
        {.name = "audit_get",
         .description =
             "Reads the last audit report, worst issues first: each issue's id, rule and title, severity (error, warning, "
             "info), what it costs (areas), its subject (asset key, entity of a document, script file and line, setting "
             "path or profile), the message with the numbers it was judged by, details, the lines of a script finding and "
             "its call path from the update method, the profiles whose builds have "
             "it, its fix (audit_fix applies it) and whether it is ignored. Filters: severity (the least to show), rule, "
             "profile, area, search, include_ignored; limit (default 50) and offset page through. id returns one issue "
             "with why it matters and what to do. insight returns one of the report's tables (largest_assets, "
             "assets_by_type, unused_assets, textures, audio, meshes, profiles, deploys, deploy_contents, update_methods) "
             "instead.",
         .input_schema_json =
             R"json({"type":"object","properties":{"severity":{"type":"string","enum":["error","warning","info"],"description":"The least severity to list; default info (all)"},"rule":{"type":"string","description":"Only this rule's issues (audit_get_rules lists ids)"},"profile":{"type":"string","description":"Only issues of this profile's builds"},"area":{"type":"string","enum":["correctness","cpu","gpu","memory","download","load_time","quality"]},"search":{"type":"string","description":"Text in the message, subject or rule, any case"},"include_ignored":{"type":"boolean","description":"List ignored issues too; default false"},"id":{"type":"string","description":"One issue, with why and what to do"},"insight":{"type":"string","description":"One of the report's tables instead of issues"},"limit":{"type":"integer","minimum":1,"maximum":500},"offset":{"type":"integer","minimum":0}}})json",
         .handler = &handle_audit_get,
         .effect = tool_effect::read_only});
    registry.add(
        {.name = "audit_get_rules",
         .description =
             "Lists every audit rule: id, title, severity, areas, what it reads, whether the project runs it, what it "
             "finds and what to do, and its thresholds with the project's values and the defaults for desktop, android "
             "and web.",
         .input_schema_json = R"json({"type":"object","properties":{}})json",
         .handler = &handle_audit_get_rules,
         .effect = tool_effect::read_only});
    registry.add(
        {.name = "audit_set_rules",
         .description =
             "Turns audit rules on or off and sets their thresholds per platform, in the project's audit settings "
             "(settings/audit.cfg, shared in version control), as the Project Audit window's Rules view does. items: "
             "[{\"rule\":\"texture.oversized\",\"enabled\":true,\"thresholds\":{\"max_side\":{\"android\":1024}}}]. "
             "Every item is checked before anything changes. The next audit_run applies the change.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"rule":{"type":"string"},"enabled":{"type":"boolean"},"thresholds":{"type":"object","description":"{threshold name: {desktop|android|web: whole number}}"}},"required":["rule"]}}},"required":["items"]})json",
         .handler = &handle_audit_set_rules,
         .effect = tool_effect::changes,
         .writes_project_files = true});
    registry.add(
        {.name = "audit_fix",
         .description =
             "Applies the fix of audit issues by id (audit_get gives ids and fix labels), as the Project Audit window's "
             "fix button does: import settings are written to the asset's .meta file as the inspector's Apply writes "
             "them and the asset compiles again; profile settings and content rules change as Project Settings and the "
             "Deploy window change them. Every id is checked first: an unknown id or an issue without a fix refuses the "
             "call and nothing changes. Refused while the game plays or a deploy runs. Run audit_run again to confirm.",
         .input_schema_json =
             R"json({"type":"object","properties":{"ids":{"type":"array","minItems":1,"items":{"type":"string"},"description":"Issue ids from audit_get"}},"required":["ids"]})json",
         .handler = &handle_audit_fix,
         .effect = tool_effect::destructive,
         .writes_project_files = true});
    registry.add(
        {.name = "audit_ignore",
         .description =
             "Ignores audit issues by id with a reason, or stops ignoring them (ignore false), in the project's audit "
             "settings (settings/audit.cfg, shared in version control), as the Project Audit window's Ignore does. An "
             "ignored issue stays in reports, marked, and leaves the counts. reason is required to ignore: say why the "
             "issue is fine. Every id is checked first; nothing changes when one is wrong.",
         .input_schema_json =
             R"json({"type":"object","properties":{"ids":{"type":"array","minItems":1,"items":{"type":"string"}},"reason":{"type":"string","description":"Why the issues are fine; required to ignore"},"ignore":{"type":"boolean","description":"false stops ignoring; default true"}},"required":["ids"]})json",
         .handler = &handle_audit_ignore,
         .effect = tool_effect::changes,
         .writes_project_files = true});
}

} // namespace unravel::mcp
