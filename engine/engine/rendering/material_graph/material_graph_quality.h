#pragma once

#include <cstdint>

namespace unravel
{

/// The quality a graph material compiles for: what its Quality Switch nodes pick. The editor draws High; a deploy cooks
/// the quality of its profile's Material Quality setting (settings::graphics_settings::material_quality).
enum class material_graph_quality : uint8_t
{
    high,
    low,
};

} // namespace unravel
