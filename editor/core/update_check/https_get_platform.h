#pragma once

#include "https_get.h"

#include <chrono>
#include <string>

/// The platform's half of https_get: https_get_windows.cpp or https_get_posix.cpp.
namespace unravel::update_check::detail
{

/// How often a waiting request asks whether to end.
constexpr std::chrono::milliseconds STOP_POLL_INTERVAL{100};

/**
 * @brief Runs a GET of `url`, an https:// address, through the platform's client.
 * @param should_end True once the caller asked to stop or the request's time is up.
 */
auto get_over_https(const std::string& url, const https_limits& limits, const stop_check& should_end) -> https_result;

} // namespace unravel::update_check::detail
