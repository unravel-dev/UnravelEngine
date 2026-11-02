#include "audio_device_watch.h"

#include <algorithm>

namespace unravel
{

auto audio_device_watch::update(bool is_connected, float seconds) -> audio_device_action
{
    if(is_connected)
    {
        *this = audio_device_watch{};
        return {};
    }
    audio_device_action action;
    action.is_newly_lost = !is_lost_;
    is_lost_ = true;
    wait_ -= seconds;
    if(wait_ <= 0.0f)
    {
        action.should_reconnect = true;
        wait_ = next_wait_;
        next_wait_ = std::min(next_wait_ * 2.0f, longest_retry_wait);
    }
    return action;
}

auto audio_device_watch::is_lost() const -> bool
{
    return is_lost_;
}

} // namespace unravel
