#pragma once

#include "../bimap.hpp"
#include "../key.hpp"

namespace input
{
void initialize_os_key_map(bimap<key_code, int>& key_map);
}
