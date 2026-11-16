#include "update_schedule.h"

#include <https/fetch_schedule.h>

namespace unravel::update_check
{

auto get_own_platform() -> hpp::string_view
{
    return UNRAVEL_PACKAGE_PLATFORM;
}

auto get_own_version() -> hpp::optional<version::engine_version>
{
    return version::parse(version::get_full());
}

auto is_check_due(const update_state& state, std::chrono::system_clock::time_point now) -> bool
{
    return https::is_fetch_due(state.last_attempt, state.last_answer, now, CHECK_INTERVAL, RETRY_INTERVAL);
}

void record_outcome(update_state& state, const check_outcome& outcome, std::chrono::system_clock::time_point now)
{
    const std::int64_t now_seconds = https::get_epoch_seconds(now);
    state.last_attempt = now_seconds;
    if(!outcome.has_answer)
    {
        return;
    }
    state.last_answer = now_seconds;
    state.newest_version = outcome.build ? outcome.build->version.to_string() : std::string();
    state.newest_tag = outcome.build ? outcome.build->tag : std::string();
}

auto get_build_to_announce(const update_state& state, const version::engine_version& own, bool is_manual)
    -> hpp::optional<release_build>
{
    const auto newest = parse_package_version(state.newest_version);
    if(!newest || !is_release_tag(state.newest_tag) || version::compare(own, *newest) >= 0)
    {
        return hpp::nullopt;
    }
    if(!is_manual && state.newest_version == state.dismissed_version)
    {
        return hpp::nullopt;
    }
    return release_build{std::string(get_own_platform()), *newest, state.newest_tag};
}

auto get_release_page_url(const std::string& tag) -> std::string
{
    return RELEASE_PAGE_URL_PREFIX + tag;
}

} // namespace unravel::update_check
