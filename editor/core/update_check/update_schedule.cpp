#include "update_schedule.h"

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

auto to_epoch_seconds(std::chrono::system_clock::time_point time) -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

auto is_check_due(const update_state& state, std::chrono::system_clock::time_point now) -> bool
{
    const std::int64_t now_seconds = to_epoch_seconds(now);
    if(state.last_attempt == 0 || state.last_attempt > now_seconds)
    {
        return true;
    }
    const bool was_answered = state.last_answer >= state.last_attempt;
    const std::chrono::seconds interval = was_answered ? std::chrono::seconds(CHECK_INTERVAL)
                                                       : std::chrono::seconds(RETRY_INTERVAL);
    return std::chrono::seconds(now_seconds - state.last_attempt) >= interval;
}

void record_outcome(update_state& state, const check_outcome& outcome, std::chrono::system_clock::time_point now)
{
    const std::int64_t now_seconds = to_epoch_seconds(now);
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
