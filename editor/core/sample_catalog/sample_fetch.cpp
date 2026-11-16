#include "sample_fetch.h"
#include "sample_cache.h"

#include <https/fetch_schedule.h>

#include <exception>

namespace unravel::sample_catalog
{
namespace
{

/// Fetches one sample's picture into `path`. Empty when it arrived, else why not.
auto fetch_thumbnail(const sample_entry& sample, const fs::path& path, const https::stop_check& should_stop) -> std::string
{
    https::https_limits limits;
    limits.max_body_size = MAX_THUMBNAIL_SIZE;
    const std::string url = get_thumbnail_url(sample);
    auto response = https::https_get(url, limits, should_stop);
    if(!response.body)
    {
        return url + ": " + response.error;
    }
    if(!is_picture_data(*response.body))
    {
        return url + ": the answer is no JPEG or PNG file.";
    }
    if(!https::write_file_atomically(path, *response.body))
    {
        return path.string() + " could not be written.";
    }
    return {};
}

auto fetch_manifest_and_thumbnails(const https::stop_check& should_stop) -> fetch_outcome
{
    https::https_limits limits;
    limits.max_body_size = MAX_MANIFEST_SIZE;
    auto response = https::https_get(MANIFEST_URL, limits, should_stop);
    if(!response.body)
    {
        return {hpp::nullopt, std::move(response.error), {}};
    }
    auto manifest = read_sample_manifest(*response.body);
    if(!manifest)
    {
        return {hpp::nullopt, "The answer is not a sample manifest.", {}};
    }
    if(manifest->samples.empty())
    {
        return {hpp::nullopt, "The answer lists no sample this editor can show.", {}};
    }
    fetch_outcome outcome{std::move(manifest), {}, {}};
    if(!https::write_file_atomically(get_cached_manifest_path(), *response.body))
    {
        outcome.file_errors.push_back(get_cached_manifest_path().string() + " could not be written.");
    }
    auto thumbnail_errors = fetch_missing_thumbnails(*outcome.manifest, get_thumbnails_dir(), should_stop);
    outcome.file_errors.insert(outcome.file_errors.end(), thumbnail_errors.begin(), thumbnail_errors.end());
    return outcome;
}

} // namespace

auto fetch_missing_thumbnails(const sample_manifest& manifest, const fs::path& dir, const https::stop_check& should_stop)
    -> std::vector<std::string>
{
    std::vector<std::string> errors;
    for(const sample_entry& sample : manifest.samples)
    {
        if(should_stop && should_stop())
        {
            return errors;
        }
        const fs::path path = dir / get_thumbnail_file_name(sample);
        fs::error_code error;
        if(fs::exists(path, error))
        {
            continue;
        }
        if(auto failure = fetch_thumbnail(sample, path, should_stop); !failure.empty())
        {
            errors.push_back(std::move(failure));
        }
    }
    remove_unlisted_thumbnails(dir, manifest);
    return errors;
}

auto fetch_samples(const https::stop_check& should_stop) -> fetch_outcome
{
    // The caller waits for an outcome on another thread: a failure of any kind is an outcome without a manifest.
    try
    {
        return fetch_manifest_and_thumbnails(should_stop);
    }
    catch(const std::exception& e)
    {
        return {hpp::nullopt, std::string("The fetch failed: ") + e.what(), {}};
    }
}

} // namespace unravel::sample_catalog
