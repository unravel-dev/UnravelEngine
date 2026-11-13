#include "update_fetch.h"

#include <exception>

namespace unravel::update_check
{
namespace
{

auto fetch_platform_build(const stop_check& should_stop) -> check_outcome
{
    https_limits limits;
    limits.max_body_size = MAX_MANIFEST_SIZE;
    auto response = https_get(MANIFEST_URL, limits, should_stop);
    if(!response.body)
    {
        return {false, hpp::nullopt, std::move(response.error)};
    }
    const auto manifest = read_update_manifest(*response.body);
    if(!manifest)
    {
        return {false, hpp::nullopt, "The answer is not an update manifest."};
    }
    return {true, find_platform_build(*manifest, get_own_platform()), {}};
}

} // namespace

auto fetch_update(const stop_check& should_stop) -> check_outcome
{
    // The caller waits for an outcome on another thread: a failure of any kind is an outcome without an answer.
    try
    {
        return fetch_platform_build(should_stop);
    }
    catch(const std::exception& e)
    {
        return {false, hpp::nullopt, std::string("The check failed: ") + e.what()};
    }
}

} // namespace unravel::update_check
