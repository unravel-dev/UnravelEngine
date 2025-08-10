#include "os_key_map.hpp"
#include <ospp/event.h>

namespace input
{
//  ----------------------------------------------------------------------------
void initialize_os_key_map(bimap<key_code, int>& key_map)
{
    key_map.clear();

    for(int32_t code = os::key::code::unknown; code < os::key::code::count; ++code)
    {
        key_map.map(static_cast<key_code>(code), code);
    }
}
} // namespace input
