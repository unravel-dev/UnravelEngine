#include "profiler.h"

namespace unravel
{

auto get_app_profiler() -> performance_profiler*
{
    static performance_profiler profiler;
    return &profiler;
}

} // namespace unravel
