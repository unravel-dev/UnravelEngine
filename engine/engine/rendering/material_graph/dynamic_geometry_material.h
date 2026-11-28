#pragma once

#include "material_graph.h"

namespace unravel
{

/**
 * @brief The graph of the material a Line or a Trail draws without one of its own (STRIP_MATERIAL_KEY): the strip's
 * colour as its Base Color, its opacity dithered. It draws in the G-buffer, lit as any surface, on both of its sides.
 */
auto make_strip_material_graph() -> material_graph;

} // namespace unravel
