#include "audit_catalog.h"
#include "audit_facts.h"
#include "audit_rules.h"

#include <logging/logging.h>

#include <map>

namespace unravel::audit::detail
{
namespace
{

auto make_profile_subject(const profile_record& profile, std::string detail = {}) -> subject
{
    return {subject_kind::profile, profile.name, std::move(detail), {}, 0};
}

void check_deploy_problems(rule_context& context, const profile_record& profile)
{
    for(const auto& problem : profile.deploy_problems)
    {
        auto item = context.make(rules::DEPLOY_PROBLEM, make_profile_subject(profile), problem);
        // Two problems of one profile are told apart by their text.
        item.discriminator = problem;
        // What keeps the deploy profile from building blocks the next deploy; another profile may never be built here.
        if(profile.name != context.get_input().deploy_profile)
        {
            item.level = severity::warning;
        }
        item.profiles.push_back(profile.name);
        context.add(std::move(item));
    }
}

void check_missing_scenes(rule_context& context, const profile_record& profile)
{
    for(const auto& scene : profile.content.missing_scenes)
    {
        auto item = context.make(rules::MISSING_SCENE,
                                 make_profile_subject(profile, scene),
                                 fmt::format("The deploy's scenes name {}, which is not in the project.", scene));
        item.profiles.push_back(profile.name);
        context.add(std::move(item));
    }
}

void check_excluded_but_used(rule_context& context, const profile_record& profile)
{
    for(const auto& entry : profile.content.entries)
    {
        if(!entry.is_used || deployment::find_rule(profile.deploy, entry.key) != deployment::content_rule::exclude)
        {
            continue;
        }
        const auto used_by = entry.used_by.empty() ? std::string("a deploy scene") : entry.used_by;
        auto item = context.make(rules::EXCLUDED_BUT_USED,
                                 {subject_kind::asset, entry.key, {}, {}, 0},
                                 fmt::format("An exclude rule leaves it out, but {} uses it.", used_by));
        item.profiles.push_back(profile.name);
        context.add(std::move(item));
    }
}

void check_orphan_rules(rule_context& context, const profile_record& profile)
{
    for(const auto& key : deployment::find_orphan_rules(profile.deploy, profile.content))
    {
        const bool is_include = deployment::has_own_rule(profile.deploy, key) &&
                                deployment::find_rule(profile.deploy, key) == deployment::content_rule::include;
        auto item = context.make(rules::ORPHAN_RULE,
                                 make_profile_subject(profile, key),
                                 fmt::format("The {} rule on {} names no asset or folder of the project.",
                                             is_include ? "include" : "exclude",
                                             key));
        item.profiles.push_back(profile.name);
        item.fix = fixes::REMOVE_CONTENT_RULE;
        context.add(std::move(item));
    }
}

void check_named_folders(rule_context& context, const profile_record& profile)
{
    if(!deployment::needs_walk(profile.deploy))
    {
        // Every asset ships, so what scripts load from a folder ships too.
        return;
    }
    for(const auto& folder : profile.content.named_folders)
    {
        if(deployment::find_rule(profile.deploy, folder.key) == deployment::content_rule::include)
        {
            continue;
        }
        auto item = context.make(rules::NAMED_FOLDER,
                                 {subject_kind::asset, folder.key, {}, {}, 0},
                                 fmt::format("{} script(s) build asset paths in it at run time, and no include rule "
                                             "covers it.",
                                             folder.scripts.size()));
        for(const auto& script : folder.scripts)
        {
            item.details.push_back("Named in " + script);
        }
        item.profiles.push_back(profile.name);
        context.add(std::move(item));
    }
}

auto describe_change(std::int64_t bytes) -> std::string
{
    const auto size = format_bytes(static_cast<std::uint64_t>(bytes < 0 ? -bytes : bytes));
    return bytes < 0 ? "-" + size : "+" + size;
}

void check_build_growth(rule_context& context, const profile_record& profile)
{
    if(!profile.last_deploy || !profile.previous_deploy)
    {
        return;
    }
    const auto change = deployment::compare_deploy_reports(*profile.previous_deploy, *profile.last_deploy);
    const auto limit = context.get_threshold(rules::BUILD_GREW, thresholds::MIN_GROWTH_BYTES, profile.platform);
    if(change.build_bytes <= 0 || change.build_bytes < limit)
    {
        return;
    }
    auto item = context.make(rules::BUILD_GREW,
                             make_profile_subject(profile),
                             fmt::format("The last deploy grew by {} to {} since the one before it.",
                                         format_bytes(static_cast<std::uint64_t>(change.build_bytes)),
                                         format_bytes(profile.last_deploy->build_bytes)));
    constexpr std::size_t LISTED_CHANGES = 5;
    for(std::size_t index = 0; index < change.categories.size() && index < LISTED_CHANGES; ++index)
    {
        item.details.push_back(fmt::format("{}: {}", change.categories[index].first, describe_change(change.categories[index].second)));
    }
    for(std::size_t index = 0; index < change.files.size() && index < LISTED_CHANGES; ++index)
    {
        item.details.push_back(fmt::format("{}: {}", change.files[index].first, describe_change(change.files[index].second)));
    }
    item.profiles.push_back(profile.name);
    context.add(std::move(item));
}

} // namespace

void check_content(rule_context& context)
{
    for(const auto& profile : context.get_input().profiles)
    {
        check_deploy_problems(context, profile);
        check_missing_scenes(context, profile);
        check_excluded_but_used(context, profile);
        check_orphan_rules(context, profile);
        check_named_folders(context, profile);
        check_build_growth(context, profile);
    }
}

void check_scripts(rule_context& context)
{
    const auto& in = context.get_input();
    for(const auto& message : in.script_messages)
    {
        const auto key = make_project_key(in.project_root, message.file);
        auto item = context.make(message.is_error ? rules::COMPILE_ERROR : rules::COMPILE_WARNING,
                                 {subject_kind::script, key, {}, {}, message.line},
                                 make_sentence(message.message));
        // One file's messages are told apart by their text; the line moves as the file is edited.
        item.discriminator = message.message;
        item.details.push_back(fmt::format("Line {}", message.line));
        context.add(std::move(item));
    }
    if(!in.scripts_compiled && in.script_messages.empty())
    {
        auto item = context.make(rules::COMPILE_ERROR,
                                 {subject_kind::project, in.project_name, {}, {}, 0},
                                 "The scripts did not compile, and the compiler named no file.");
        context.add(std::move(item));
    }
}

} // namespace unravel::audit::detail
