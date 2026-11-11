#include "variant_cook.h"
#include "cancellation.h"
#include "files.h"
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

auto split_assets(const asset_database::database_t& database, const asset_kind_filter& is_kind, const left_out_assets& left_out)
    -> asset_split
{
    asset_split split;
    for(const auto& [uid, meta] : database)
    {
        if(is_kind(meta.location))
        {
            (left_out.contains(meta.location) ? split.left_out : split.shipped).push_back(meta.location);
        }
    }
    return split;
}

auto cook_variant(const std::string& key, const fs::path& compiled, const fs::path& variants, const variant_compiler& compiler)
    -> cook_outcome
{
    cook_outcome outcome;
    const auto relative = get_compiled_relative_path(key);
    if(relative.empty() || (!compiler.needs_variant && compiler.is_editor_compiled(compiled / relative)))
    {
        return outcome;
    }
    outcome.files.push_back(get_cache_file_key(relative));
    const auto variant = variants / relative;
    if(compiler.is_profile_compiled(variant))
    {
        return outcome;
    }
    if(compiler.compile(variant))
    {
        ++outcome.compiled;
    }
    else
    {
        outcome.has_failed = true;
    }
    return outcome;
}

auto cook_variants(const asset_split& assets, const fs::path& variants, const asset_cook& cook, const variant_need& needs_variant)
    -> cook_report
{
    const auto outcomes = cook_all(assets.shipped,
                                   [&](std::size_t index)
                                   {
                                       return cook(assets.shipped[index]);
                                   });
    cook_report report;
    std::unordered_set<std::string> kept;
    for(std::size_t i = 0; i < outcomes.size(); ++i)
    {
        add_outcome(assets.shipped[i], outcomes[i], kept, report);
    }
    for(const auto& key : assets.left_out)
    {
        const auto relative = get_compiled_relative_path(key);
        if(!relative.empty() && needs_variant(key))
        {
            keep_cache_file(kept, get_cache_file_key(relative));
        }
    }
    sweep_cache_folder(variants, kept, report);
    return report;
}

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
                                                 if(!is_deploy_cancelled())
                                                 {
                                                     outcomes[index] = cook(index);
                                                 }
                                             });
    // Before the caller sweeps its cache by outcomes that are not all there.
    throw_if_deploy_cancelled();
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
