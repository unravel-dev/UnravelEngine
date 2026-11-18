#include "audit_catalog.h"
#include "audit_rules.h"

#include <hpp/string_view.hpp>
#include <logging/logging.h>

#include <algorithm>
#include <map>
#include <tuple>

namespace unravel::audit::detail
{
namespace
{

/// "A", "A and B", "A, B and C".
auto join_names(const std::vector<std::string>& names) -> std::string
{
    std::string text;
    for(std::size_t index = 0; index < names.size(); ++index)
    {
        if(index > 0)
        {
            text += index + 1 == names.size() ? " and " : ", ";
        }
        text += names[index];
    }
    return text;
}

/// The finding in a sentence. Where it is (the method, the lines) and how it is reached are shown beside the message,
/// so the message says only what the code does. `targets` holds every member the issue's findings name.
auto describe_what(const code_finding& finding, const std::vector<std::string>& targets) -> std::string
{
    const auto& rule = finding.rule;
    const auto& target = finding.target;
    if(rule == rules::CODE_FIND_ENTITY)
    {
        return fmt::format("Calls {}, which walks every entity of the scene.", target);
    }
    if(rule == rules::CODE_CAST_ALL)
    {
        return fmt::format("Calls {}, which returns its results in a new array on every call.", target);
    }
    if(rule == rules::CODE_LINQ)
    {
        return fmt::format("Calls {} from System.Linq, which allocates an enumerator, and often a delegate, on every "
                           "call.",
                           target);
    }
    if(rule == rules::CODE_CLOSURE)
    {
        if(finding.detail == "closure")
        {
            return "Allocates the object that holds the variables a lambda captures, each time it runs.";
        }
        return fmt::format("Allocates a new {} delegate each time it runs.", target);
    }
    if(rule == rules::CODE_BOXING)
    {
        return fmt::format("Boxes a value of type {} into a new object each time it runs.", target);
    }
    if(rule == rules::CODE_STRING)
    {
        return fmt::format("Builds new strings with {} each time it runs.", join_names(targets));
    }
    if(rule == rules::CODE_ARRAY)
    {
        if(!finding.detail.empty())
        {
            return fmt::format("Allocates a new {} each time it runs, to pass the params argument of {}.",
                               target,
                               finding.detail);
        }
        return fmt::format("Allocates a new {} each time it runs.", target);
    }
    if(rule == rules::CODE_OBJECT)
    {
        return fmt::format("Creates a new {} each time it runs.", target);
    }
    if(rule == rules::CODE_REFLECTION)
    {
        return fmt::format("Calls {}, which searches the type's metadata, each time it runs.", target);
    }
    if(rule == rules::CODE_LOG)
    {
        return fmt::format("Calls {}, which formats and writes a log line, each time it runs.", target);
    }
    if(rule == rules::CODE_EMPTY_UPDATE)
    {
        return fmt::format("{} has an empty body, and the engine still calls it every frame.", finding.method);
    }
    if(rule == rules::CODE_ALLOCATING_CALL)
    {
        return fmt::format("Calls {}, which allocates {}.", target, finding.detail);
    }
    if(rule == rules::CODE_INSTANTIATE)
    {
        if(finding.detail == "by key")
        {
            return fmt::format("Calls {} with an asset key: each call finds the key in the asset database and makes "
                               "an entity.",
                               target);
        }
        return fmt::format("Calls {}, which makes {} each time it runs.",
                           target,
                           target == "Entity.AddComponent" ? "a component" : "an entity");
    }
    if(rule == rules::CODE_ASSET_LOOKUP)
    {
        return fmt::format("Calls {} with a string key, which searches the asset database each time it runs.", target);
    }
    if(rule == rules::CODE_INTERFACE_ENUMERATION)
    {
        return fmt::format("Loops over an {}, which allocates the enumerator of each loop.", target);
    }
    if(rule == rules::CODE_LOCAL_TIME)
    {
        return fmt::format("Reads {}, which converts the clock to the local time zone, each time it runs.", target);
    }
    if(rule == rules::CODE_GC_COLLECT)
    {
        return fmt::format("Calls {}, which runs a full garbage collection each time.", target);
    }
    if(rule == rules::CODE_BLOCKING_WAIT)
    {
        return fmt::format("Calls {}, which blocks the main thread until it returns.", target);
    }
    if(rule == rules::CODE_FILE_ACCESS)
    {
        return fmt::format("Calls {}, which waits on the storage device each time it runs.", target);
    }
    return fmt::format("Uses {}.", target);
}

/// Whether the code runs in a coroutine: its step runs each time the coroutine resumes, not every frame.
auto is_in_coroutine(const code_finding& finding) -> bool
{
    constexpr hpp::string_view ITERATOR_SUFFIX = "(iterator)";
    if(finding.path.empty())
    {
        return false;
    }
    const auto& root = finding.path.front();
    return root.size() >= ITERATOR_SUFFIX.size() &&
           root.compare(root.size() - ITERATOR_SUFFIX.size(), ITERATOR_SUFFIX.size(), ITERATOR_SUFFIX.data()) == 0;
}

auto describe_finding(const code_finding& finding, const std::vector<std::string>& targets) -> std::string
{
    auto text = describe_what(finding, targets);
    constexpr hpp::string_view EVERY_RUN = "each time it runs";
    const auto found = text.find(EVERY_RUN.data());
    if(found != std::string::npos && is_in_coroutine(finding))
    {
        text.replace(found, EVERY_RUN.size(), "each time the coroutine resumes");
    }
    return text;
}

/// What tells two findings of one rule in one method apart: the member and, for a params array or an instantiation
/// by key, which call it is. String building is one issue for each method: a concatenation is a Concat and the
/// ToString calls of its parts.
auto make_discriminator(const code_finding& finding) -> std::string
{
    if(finding.rule == rules::CODE_STRING)
    {
        return {};
    }
    const bool detail_tells_apart = finding.rule == rules::CODE_ARRAY || finding.rule == rules::CODE_INSTANTIATE;
    if(finding.detail.empty() || !detail_tells_apart)
    {
        return finding.target;
    }
    return finding.target + " (" + finding.detail + ")";
}

/// The findings of one rule, method and member, which make one issue.
struct finding_group
{
    const code_finding* first{nullptr};
    std::vector<std::uint32_t> lines;
    std::vector<std::string> targets;
};

} // namespace

void check_code(rule_context& context)
{
    const auto& code = context.get_input().code;
    if(!code.ran)
    {
        return;
    }
    std::map<std::tuple<std::string, std::string, std::string, std::string>, finding_group> groups;
    for(const auto& finding : code.findings)
    {
        if(find_rule(finding.rule) == nullptr)
        {
            continue;
        }
        auto& group = groups[{finding.rule, finding.file, finding.method, make_discriminator(finding)}];
        if(group.first == nullptr)
        {
            group.first = &finding;
        }
        if(finding.line > 0 && std::find(group.lines.begin(), group.lines.end(), finding.line) == group.lines.end())
        {
            group.lines.push_back(finding.line);
        }
        if(std::find(group.targets.begin(), group.targets.end(), finding.target) == group.targets.end())
        {
            group.targets.push_back(finding.target);
        }
    }
    for(auto& [key, group] : groups)
    {
        const auto& finding = *group.first;
        std::sort(group.lines.begin(), group.lines.end());
        auto item = context.make(finding.rule.c_str(),
                                 {subject_kind::script, finding.file, finding.method, {}, group.lines.empty() ? 0 : group.lines.front()},
                                 describe_finding(finding, group.targets));
        item.discriminator = std::get<3>(key);
        item.lines = std::move(group.lines);
        item.call_path = finding.path;
        context.add(std::move(item));
    }
}

} // namespace unravel::audit::detail
