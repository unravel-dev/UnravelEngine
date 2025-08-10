#pragma once
#include <engine/engine_export.h>
#include <vector>
#include <array>
#include <string>

namespace unravel
{


enum layer_reserved
{
    nothing_layer = 0,
    default_layer = 1 << 0,
    static_static = 1 << 1,
    everything_layer = -1 // all bits sets
};


struct layer_mask
{
    int mask{layer_reserved::default_layer};
};

auto get_reserved_layers() -> const std::vector<std::string>&;
auto get_reserved_layers_as_array() -> const std::array<std::string, 32>&;

} // namespace unravel
