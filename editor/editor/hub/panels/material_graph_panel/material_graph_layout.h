#pragma once

#include <engine/rendering/material_graph/material_graph.h>

#include <math/math.h>

#include <functional>
#include <string>

namespace unravel
{

/**
 * @brief How big a node is on the canvas and where its pins sit, in graph units: what the canvas draws.
 */
struct material_graph_layout_metrics
{
    std::function<math::vec2(const material_graph_node&)> measure;
    /// Distance from the node's top to the pin an input entry names ("base_color", "base_color.r"), or to the output
    /// a link reads ("rgba", "rgba.r").
    std::function<float(const material_graph_node&, const std::string& pin, bool is_output)> get_pin_offset;
};

/**
 * @brief Arranges the graph in columns that flow right, into the surface output.
 *
 * A node goes one column left of the leftmost node that reads it (the longest path to the output, a layered
 * layout), and within its column as level as it can with the inputs it feeds, in their order, without overlapping.
 * Where links still cross, barycenter sweeps reorder the columns, and that order is kept when it crosses fewer links.
 * Nodes nothing reads start columns of their own below the output's. Comments are left where they are.
 */
void layout_material_graph(material_graph& graph, const material_graph_layout_metrics& metrics);

} // namespace unravel
