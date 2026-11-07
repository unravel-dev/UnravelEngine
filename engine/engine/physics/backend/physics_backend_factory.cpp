#include "physics_backend.h"

#include <base/platform/thread.hpp>
#include <concurrency/threading_mode.h>
#include <logging/logging.h>

#include "box3d/box3d_backend.h"
#include "bullet/bullet_backend.h"

namespace unravel
{

auto get_physics_worker_count() -> int
{
    const uint32_t hardware_threads = is_single_threaded() ? 1 : platform::get_performance_core_count();
    return static_cast<int>(hardware_threads > 1 ? hardware_threads - 1 : 1);
}

auto create_physics_backend(physics_backend_type type) -> std::unique_ptr<physics_backend>
{
    switch(resolve_physics_backend(type))
    {
        case physics_backend_type::bullet:
            return std::make_unique<bullet_backend>();
        case physics_backend_type::box3d:
            return std::make_unique<box3d_backend>();
        case physics_backend_type::auto_detect:
            break;
    }

    APPLOG_ERROR("Unknown physics_backend_type value.");
    return nullptr;
}

} // namespace unravel
