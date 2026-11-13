#pragma once

#include "https_get.h"
#include "update_schedule.h"

namespace unravel::update_check
{

/**
 * @brief Fetches latest.json from MANIFEST_URL and picks the build of this platform.
 *
 * Blocks the calling thread for up to the request's time limit; run it off the frame. `should_stop` ends it
 * earlier, with no answer. It does not throw: any failure is an outcome without an answer.
 */
auto fetch_update(const stop_check& should_stop) -> check_outcome;

} // namespace unravel::update_check
