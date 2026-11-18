#include "audit_run.h"
#include "audit_catalog.h"
#include "audit_code.h"
#include "audit_rules.h"

#include <logging/logging.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <unordered_map>

namespace unravel::audit
{
namespace detail
{

rule_context::rule_context(const input& in, const config& settings, std::vector<issue>& out)
    : in_(in)
    , settings_(settings)
    , out_(out)
{
}

auto rule_context::get_input() const -> const input&
{
    return in_;
}

auto rule_context::is_enabled(const char* rule_id) const -> bool
{
    return settings_.is_rule_enabled(rule_id);
}

auto rule_context::get_threshold(const char* rule_id, const char* name, target_platform platform) const -> std::int64_t
{
    const auto* owner = find_rule(rule_id);
    return owner != nullptr ? settings_.get_threshold(*owner, name, platform) : 0;
}

auto rule_context::make(const char* rule_id, subject where, std::string message) const -> issue
{
    issue item;
    item.rule = rule_id;
    item.where = std::move(where);
    item.message = std::move(message);
    if(const auto* owner = find_rule(rule_id))
    {
        item.level = owner->default_severity;
        item.areas = owner->areas;
    }
    return item;
}

void rule_context::add(issue item)
{
    if(is_enabled(item.rule.c_str()))
    {
        out_.push_back(std::move(item));
    }
}

void rule_context::count_unread()
{
    ++unread_;
}

auto rule_context::get_unread_count() const -> std::size_t
{
    return unread_;
}

auto is_shipped_by(const profile_record& profile, const std::string& key) -> bool
{
    const auto* entry = profile.content.find(key);
    return entry != nullptr && deployment::is_shipped(profile.deploy, *entry);
}

auto make_project_key(const fs::path& project_root, const std::string& file) -> std::string
{
    if(file.empty())
    {
        return file;
    }
    const fs::path path = fs::from_utf8_string(file);
    const auto data_folder = project_root / "data";
    fs::error_code ec;
    const auto relative = fs::relative(path, data_folder, ec);
    const auto relative_text = relative.generic_string();
    if(ec || relative.empty() || relative_text.rfind("..", 0) == 0 || path.is_relative())
    {
        return fs::path(file).generic_string();
    }
    return "app:/data/" + relative_text;
}

auto make_sentence(std::string text) -> std::string
{
    while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.pop_back();
    }
    if(text.empty())
    {
        return text;
    }
    text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    const char last = text.back();
    if(last != '.' && last != '!' && last != '?')
    {
        text += '.';
    }
    return text;
}

} // namespace detail

namespace
{

/// How often reading the compiled files reports its count: each report takes a lock the editor reads under.
constexpr std::size_t FACTS_REPORT_INTERVAL = 16;

struct run_steps
{
    const run_options& options;

    auto should_stop() const -> bool
    {
        return options.should_stop && options.should_stop();
    }

    void report(run_stage stage, std::size_t done = 0, std::size_t total = 0, std::string item = {}) const
    {
        if(options.on_progress)
        {
            options.on_progress({stage, done, total, std::move(item)});
        }
    }
};

void find_references(input& in, const run_steps& steps)
{
    steps.report(run_stage::references);
    in.references = deployment::find_content_references(in.source);
}

void find_profile_content(input& in, const run_steps& steps)
{
    for(std::size_t index = 0; index < in.profiles.size() && !steps.should_stop(); ++index)
    {
        auto& profile = in.profiles[index];
        steps.report(run_stage::content, index, in.profiles.size(), profile.name);
        auto source = in.source;
        source.settings_document = profile.settings_document;
        profile.content = deployment::detect_content(source, profile.deploy, profile.startup_scene);
    }
}

void load_deploy_reports(input& in)
{
    for(auto& profile : in.profiles)
    {
        deployment::deploy_report report;
        std::string error;
        if(deployment::load_deploy_report(deployment::get_deploy_report_path(in.project_root, profile.name), report, error))
        {
            profile.last_deploy = std::move(report);
        }
        if(deployment::load_deploy_report(deployment::get_previous_deploy_report_path(in.project_root, profile.name), report, error))
        {
            profile.previous_deploy = std::move(report);
        }
    }
}

void read_facts(input& in, const run_steps& steps)
{
    const auto count = in.assets.size();
    for(std::size_t index = 0; index < count && !steps.should_stop(); ++index)
    {
        auto& asset = in.assets[index];
        if(index % FACTS_REPORT_INTERVAL == 0)
        {
            steps.report(run_stage::assets, index, count, asset.key);
        }
        if(!asset.compiled_files.empty())
        {
            asset.facts = read_asset_facts(asset, steps.options.cache);
        }
    }
}

void run_code(input& in, const run_steps& steps)
{
    if(in.code_tool.tool.empty())
    {
        return;
    }
    steps.report(run_stage::scripts);
    in.code = run_code_analysis(in.code_tool, steps.options.should_stop);
}

/// Where a rule sits in the catalog, for sorting.
auto get_rule_rank(const std::string& id) -> std::size_t
{
    const auto& all = get_rules();
    for(std::size_t index = 0; index < all.size(); ++index)
    {
        if(all[index].id == id)
        {
            return index;
        }
    }
    return all.size();
}

void add_unique(std::vector<std::string>& list, const std::string& value)
{
    if(std::find(list.begin(), list.end(), value) == list.end())
    {
        list.push_back(value);
    }
}

/// One merged issue and the message each of its profiles gave.
struct merge_slot
{
    issue merged;
    std::vector<std::pair<std::string, std::string>> profile_messages;
};

/// Joins issues with one id: the highest severity, every profile, and each profile's own message when they differ.
auto merge_issues(std::vector<issue> items) -> std::vector<issue>
{
    std::vector<merge_slot> slots;
    std::unordered_map<std::string, std::size_t> index;
    for(auto& item : items)
    {
        const auto id = item.get_id();
        const auto found = index.find(id);
        if(found == index.end())
        {
            index.emplace(id, slots.size());
            merge_slot slot;
            for(const auto& profile : item.profiles)
            {
                slot.profile_messages.emplace_back(profile, item.message);
            }
            slot.merged = std::move(item);
            slots.push_back(std::move(slot));
            continue;
        }
        auto& slot = slots[found->second];
        auto& merged = slot.merged;
        merged.level = std::max(merged.level, item.level);
        for(const auto& profile : item.profiles)
        {
            add_unique(merged.profiles, profile);
            slot.profile_messages.emplace_back(profile, item.message);
        }
        for(const auto& line : item.details)
        {
            add_unique(merged.details, line);
        }
        if(merged.fix.empty())
        {
            merged.fix = item.fix;
        }
    }
    std::vector<issue> result;
    result.reserve(slots.size());
    for(auto& slot : slots)
    {
        const bool messages_differ = std::any_of(slot.profile_messages.begin(),
                                                 slot.profile_messages.end(),
                                                 [&slot](const auto& entry)
                                                 {
                                                     return entry.second != slot.merged.message;
                                                 });
        if(messages_differ)
        {
            std::vector<std::string> lines;
            for(const auto& [profile, message] : slot.profile_messages)
            {
                add_unique(lines, profile + ": " + message);
            }
            lines.insert(lines.end(), slot.merged.details.begin(), slot.merged.details.end());
            slot.merged.details = std::move(lines);
        }
        result.push_back(std::move(slot.merged));
    }
    return result;
}

void sort_issues(std::vector<issue>& items)
{
    std::stable_sort(items.begin(),
                     items.end(),
                     [](const issue& lhs, const issue& rhs)
                     {
                         if(lhs.level != rhs.level)
                         {
                             return lhs.level > rhs.level;
                         }
                         const auto lhs_rank = get_rule_rank(lhs.rule);
                         const auto rhs_rank = get_rule_rank(rhs.rule);
                         if(lhs_rank != rhs_rank)
                         {
                             return lhs_rank < rhs_rank;
                         }
                         if(lhs.where.key != rhs.where.key)
                         {
                             return lhs.where.key < rhs.where.key;
                         }
                         if(lhs.where.detail != rhs.where.detail)
                         {
                             return lhs.where.detail < rhs.where.detail;
                         }
                         return lhs.discriminator < rhs.discriminator;
                     });
}

/// The rules the run checked: turned on, with what they read there.
auto list_checked_rules(const input& in, const config& settings) -> std::vector<std::string>
{
    std::vector<std::string> checked;
    for(const auto& owner : get_rules())
    {
        const bool has_input = owner.source == rule_source::code     ? in.code.ran
                               : owner.source == rule_source::scenes ? in.scan.ran
                                                                     : true;
        if(has_input && settings.is_rule_enabled(owner.id))
        {
            checked.push_back(owner.id);
        }
    }
    return checked;
}

void add_notes(const input& in, const detail::rule_context& context, bool is_stopped, report& result)
{
    if(is_stopped)
    {
        result.notes.emplace_back("The run was stopped before it finished, so the report is incomplete.");
    }
    if(!in.scan.ran)
    {
        result.notes.emplace_back("Scenes, prefabs, materials and controllers were not loaded, so missing references, "
                                  "joints and components were not checked.");
    }
    if(!in.code.ran && !in.code_tool.tool.empty())
    {
        result.notes.push_back(in.code.error.empty() ? std::string("The script analysis did not run.")
                                                     : "The script analysis did not run: " + in.code.error);
    }
    if(!in.scripts_compiled && in.code.ran)
    {
        result.notes.emplace_back("The scripts do not compile, so the script analysis read the last library that did.");
    }
    if(in.code_tool.tool.empty())
    {
        result.notes.emplace_back("The script analysis was not asked for, so the update methods' code was not checked.");
    }
    if(context.get_unread_count() > 0)
    {
        result.notes.push_back(fmt::format("{} texture(s), clip(s) or mesh(es) have no readable compiled file yet, so their "
                                           "sizes were not judged. Let the editor finish compiling and run the audit "
                                           "again.",
                                           context.get_unread_count()));
    }
}

} // namespace

auto run_audit(input& in, const config& settings, const run_options& options) -> report
{
    const auto started = std::chrono::steady_clock::now();
    const run_steps steps{options};

    find_references(in, steps);
    if(!steps.should_stop())
    {
        find_profile_content(in, steps);
    }
    if(!steps.should_stop())
    {
        load_deploy_reports(in);
        read_facts(in, steps);
    }
    if(!steps.should_stop())
    {
        run_code(in, steps);
    }

    steps.report(run_stage::rules);
    std::vector<issue> found;
    detail::rule_context context(in, settings, found);
    const bool is_stopped = steps.should_stop();
    if(!is_stopped)
    {
        detail::check_content(context);
        detail::check_scenes(context);
        detail::check_scripts(context);
        detail::check_assets(context);
        detail::check_settings(context);
        detail::check_code(context);
    }

    report result;
    result.engine_version = in.engine_version;
    result.project = in.project_name;
    for(const auto& profile : in.profiles)
    {
        result.profiles.push_back(profile.name);
    }
    result.issues = merge_issues(std::move(found));
    sort_issues(result.issues);
    if(!is_stopped)
    {
        result.checked_rules = list_checked_rules(in, settings);
    }
    apply_ignores(settings, result);
    if(!is_stopped)
    {
        result.insights = detail::make_insights(in);
    }
    add_notes(in, context, is_stopped, result);
    result.finished_at = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                             .count();
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

auto get_run_stage_title(run_stage stage) -> const char*
{
    switch(stage)
    {
        case run_stage::snapshot:
            return "Reading the project";
        case run_stage::documents:
            return "Loading scenes and prefabs";
        case run_stage::references:
            return "Following references between assets";
        case run_stage::content:
            return "Finding what each profile ships";
        case run_stage::assets:
            return "Reading compiled assets";
        case run_stage::scripts:
            return "Analyzing scripts";
        case run_stage::rules:
        default:
            return "Checking the rules";
    }
}

} // namespace unravel::audit
