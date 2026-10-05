#include "backend.h"

#include <atomic>

namespace raytracing
{
namespace
{

/// Set by the build when Embree is part of it (engine/core/raytracing/CMakeLists.txt).
#if defined(RAYTRACING_WITH_EMBREE)
constexpr bool k_has_embree = true;
#else
constexpr bool k_has_embree = false;
#endif

/// Read by every scene build, on any thread.
std::atomic<backend> default_backend{backend::native};

} // namespace

auto is_backend_available(backend value) -> bool
{
    return value != backend::embree || k_has_embree;
}

auto get_default_backend() -> backend
{
    return default_backend.load(std::memory_order_relaxed);
}

auto set_default_backend(backend value) -> bool
{
    if(!is_backend_available(value))
    {
        return false;
    }
    default_backend.store(value, std::memory_order_relaxed);
    return true;
}

auto to_string(backend value) -> hpp::string_view
{
    switch(value)
    {
        case backend::embree:
            return "embree";
        case backend::native:
        default:
            return "native";
    }
}

auto parse_backend(hpp::string_view name) -> hpp::optional<backend>
{
    for(const backend candidate : {backend::embree, backend::native})
    {
        if(name == to_string(candidate))
        {
            return candidate;
        }
    }
    return hpp::nullopt;
}

} // namespace raytracing
