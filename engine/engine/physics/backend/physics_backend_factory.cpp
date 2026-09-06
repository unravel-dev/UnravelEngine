#include "physics_backend.h"

#include <logging/logging.h>

#include "box3d/box3d_backend.h"
#include "bullet/bullet_backend.h"

namespace unravel
{

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
