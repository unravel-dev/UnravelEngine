#include "cancellation.h"

#include <atomic>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
std::atomic<bool> is_cancel_requested{false};
} // namespace

void request_deploy_cancel()
{
    is_cancel_requested = true;
}

void reset_deploy_cancel()
{
    is_cancel_requested = false;
}

auto is_deploy_cancelled() -> bool
{
    return is_cancel_requested;
}

void throw_if_deploy_cancelled()
{
    if(is_deploy_cancelled())
    {
        throw std::runtime_error(CANCELLED_STEP_ERROR);
    }
}

} // namespace unravel::deployment
