#include "physics_backend.h"

#include <logging/logging.h>

#include "bullet/bullet_backend.h"

namespace unravel
{

auto create_physics_backend(physics_backend_type type) -> std::unique_ptr<physics_backend>
{
    switch(type)
    {
        case physics_backend_type::bullet:
            return std::make_unique<bullet_backend>();
    }

    APPLOG_ERROR("Unknown physics_backend_type value.");
    return nullptr;
}

} // namespace unravel
