#include "variant_cook.h"
#include "keys.h"
#include "profile_cache.h"

#include <concurrency/parallel.h>
#include <logging/logging.h>

#include <cstdint>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
/// Failed assets named in the step's error before "and N more".
constexpr std::size_t LISTED_FAILURES_MAX = 10;
constexpr const char* MANIFEST_SUFFIX = ".manifest";

auto get_source_size(const std::string& key) -> std::uintmax_t
{
    fs::error_code ec;
    const auto size = fs::file_size(fs::resolve_protocol(key), ec);
    return ec ? 0 : size;
}
} // namespace

auto get_cache_file_key(const fs::path& relative) -> std::string
{
    return fs::make_path_key(relative.generic_string());
}

void keep_cache_file(std::unordered_set<std::string>& kept, const std::string& file_key)
{
    kept.insert(file_key);
    kept.insert(file_key + MANIFEST_SUFFIX);
}

auto cook_all(const std::vector<std::string>& keys, const std::function<cook_outcome(std::size_t index)>& cook)
    -> std::vector<cook_outcome>
{
    std::vector<cook_outcome> outcomes(keys.size());
    std::vector<std::uintmax_t> costs;
    costs.reserve(keys.size());
    for(const auto& key : keys)
    {
        costs.push_back(get_source_size(key));
    }
    const poolstl::background_scope background;
    poolstl::for_each_costliest_first_par_if(true,
                                             costs,
                                             [&](std::size_t index)
                                             {
                                                 outcomes[index] = cook(index);
                                             });
    return outcomes;
}

void add_outcome(const std::string& key, const cook_outcome& outcome, std::unordered_set<std::string>& kept, cook_report& report)
{
    for(const auto& file : outcome.files)
    {
        keep_cache_file(kept, file);
    }
    report.variants += outcome.files.size();
    report.compiled += outcome.compiled;
    if(outcome.has_failed)
    {
        report.failed.push_back(key);
    }
}

void sweep_cache_folder(const fs::path& folder, const std::unordered_set<std::string>& kept, cook_report& report)
{
    auto swept = sweep_folder(folder, kept);
    report.removed += swept.removed;
    report.undeletable.insert(report.undeletable.end(), swept.failed.begin(), swept.failed.end());
}

void report_cook_or_throw(const std::string& profile, const cook_report& report, const cook_wording& wording)
{
    APPLOG_INFO("Deploy: profile {} ships {} {}, {} compiled now; {} file(s) of its {} that nothing needs were deleted.",
                profile,
                report.variants,
                wording.files,
                report.compiled,
                report.removed,
                wording.cache);
    if(!report.failed.empty())
    {
        throw std::runtime_error(fmt::format("{} {} could not be compiled for profile {} (the log says why): {}",
                                             report.failed.size(),
                                             wording.assets,
                                             profile,
                                             list_keys(report.failed, LISTED_FAILURES_MAX)));
    }
    if(!report.undeletable.empty())
    {
        throw std::runtime_error(fmt::format("{} file(s) of profile {}'s {} that nothing needs could not be deleted, and "
                                             "the deploy would ship them, for example {}. Close the program that holds "
                                             "them, then deploy again.",
                                             report.undeletable.size(),
                                             profile,
                                             wording.cache,
                                             report.undeletable.front().generic_string()));
    }
}

} // namespace unravel::deployment
