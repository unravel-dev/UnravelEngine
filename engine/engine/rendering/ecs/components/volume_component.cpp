#include "volume_component.h"

namespace unravel
{

auto volume_component::get_local_bounds() const -> math::bbox
{
    return math::bbox(-extents, extents);
}

} // namespace unravel
