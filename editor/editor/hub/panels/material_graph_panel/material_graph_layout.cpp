#include "material_graph_layout.h"

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

namespace unravel
{

namespace
{
/// Space between columns and between nodes in a column, in graph units.
constexpr float LAYOUT_COLUMN_GAP = 80.0f;
constexpr float LAYOUT_ROW_GAP = 24.0f;
/// Barycenter sweeps of the crossing reduction, each one toward the output and one back.
constexpr int CROSSING_SWEEPS = 4;

/// One input that reads a node: the reading node and the entry of its input.
struct reader
{
    size_t node{};
    std::string pin;
};

/**
 * @brief The graph's links turned around: for each node, the inputs that read it, and the output the first reads.
 */
struct reader_index
{
    std::vector<std::vector<reader>> readers;
    /// The output (with the components it picks, "rgba.r") the first reader reads, per node.
    std::vector<std::string> outputs;
};

auto make_reader_index(const material_graph& graph) -> reader_index
{
    reader_index index;
    index.readers.resize(graph.nodes.size());
    index.outputs.resize(graph.nodes.size());
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        for(const auto& input : graph.nodes[i].inputs)
        {
            if(input.link.empty())
            {
                continue;
            }
            const auto end = parse_material_graph_link(input.link);
            const int source = find_material_graph_node(graph, end.node);
            if(source < 0 || size_t(source) == i)
            {
                continue;
            }
            if(index.readers[size_t(source)].empty())
            {
                index.outputs[size_t(source)] = end.components.empty() ? end.pin : end.pin + "." + end.components;
            }
            index.readers[size_t(source)].push_back({i, input.pin});
        }
    }
    return index;
}

/// The longest path from each node to a node nothing reads (0 for those). A loop is cut where it is found.
void assign_columns(const reader_index& index, size_t node, std::vector<int>& columns, std::vector<int>& state)
{
    if(state[node] != 0)
    {
        return;
    }
    state[node] = 1;
    int column = 0;
    for(const auto& read : index.readers[node])
    {
        if(state[read.node] == 1)
        {
            continue;
        }
        assign_columns(index, read.node, columns, state);
        column = std::max(column, columns[read.node] + 1);
    }
    columns[node] = column;
    state[node] = 2;
}

/**
 * @brief Places the nodes of one column, right-aligned at @p right, as level as possible with what reads them: in the
 * order of those heights, or in the column's own order when @p keeps_order.
 * @return The column's left edge.
 */
auto place_column(material_graph& graph,
                  const std::vector<size_t>& column,
                  const reader_index& index,
                  const material_graph_layout_metrics& metrics,
                  float right,
                  bool keeps_order) -> float
{
    struct placement
    {
        size_t node{};
        float desired_y{};
    };
    std::vector<placement> placements;
    float stacked_y = 0.0f;
    for(const size_t node : column)
    {
        const auto& readers = index.readers[node];
        float desired = stacked_y;
        if(!readers.empty())
        {
            float sum = 0.0f;
            for(const auto& read : readers)
            {
                const auto& reading = graph.nodes[read.node];
                sum += reading.position.y + metrics.get_pin_offset(reading, read.pin, false);
            }
            desired = sum / float(readers.size()) - metrics.get_pin_offset(graph.nodes[node], index.outputs[node], true);
        }
        else
        {
            stacked_y += metrics.measure(graph.nodes[node]).y + LAYOUT_ROW_GAP;
        }
        placements.push_back({node, desired});
    }
    if(!keeps_order)
    {
        std::stable_sort(placements.begin(),
                         placements.end(),
                         [](const placement& a, const placement& b)
                         {
                             return a.desired_y < b.desired_y;
                         });
    }
    float bottom = -FLT_MAX;
    float left = right;
    float drift = 0.0f;
    for(const auto& placed : placements)
    {
        auto& node = graph.nodes[placed.node];
        const math::vec2 size = metrics.measure(node);
        const float y = std::max(placed.desired_y, bottom);
        node.position = {right - size.x, y};
        bottom = y + size.y + LAYOUT_ROW_GAP;
        left = std::min(left, right - size.x);
        drift += y - placed.desired_y;
    }
    // Overlaps push nodes down; moving the whole column back up by the average keeps it centred on its readers.
    const float shift = placements.empty() ? 0.0f : drift / float(placements.size());
    for(const auto& placed : placements)
    {
        graph.nodes[placed.node].position.y -= shift;
    }
    return left;
}
/// Places every column, the output's first and leftward from there.
void place_columns(material_graph& graph,
                   const std::vector<std::vector<size_t>>& by_column,
                   const reader_index& index,
                   const material_graph_layout_metrics& metrics,
                   bool keeps_order)
{
    float right = 0.0f;
    for(const auto& column : by_column)
    {
        right = place_column(graph, column, index, metrics, right, keeps_order) - LAYOUT_COLUMN_GAP;
    }
}

/// A link as drawn: from its source's output to its reader's input, a straight segment between the two pins.
struct layout_link_line
{
    math::vec2 from;
    math::vec2 to;
};

auto make_layout_link_lines(const material_graph& graph, const reader_index& index, const material_graph_layout_metrics& metrics)
    -> std::vector<layout_link_line>
{
    std::vector<layout_link_line> segments;
    for(size_t source = 0; source < index.readers.size(); ++source)
    {
        const auto& node = graph.nodes[source];
        const math::vec2 from(node.position.x + metrics.measure(node).x,
                              node.position.y + metrics.get_pin_offset(node, index.outputs[source], true));
        for(const auto& read : index.readers[source])
        {
            const auto& reading = graph.nodes[read.node];
            segments.push_back({from, {reading.position.x, reading.position.y + metrics.get_pin_offset(reading, read.pin, false)}});
        }
    }
    return segments;
}

/// Where @p segment is at @p x, along its straight line.
auto get_height_at(const layout_link_line& segment, float x) -> float
{
    const float span = segment.to.x - segment.from.x;
    const float t = std::abs(span) < FLT_EPSILON ? 0.0f : (x - segment.from.x) / span;
    return segment.from.y + (segment.to.y - segment.from.y) * t;
}

/// How many pairs of links cross: two links whose spans overlap cross where their order changes over the overlap.
auto count_crossings(const material_graph& graph, const reader_index& index, const material_graph_layout_metrics& metrics) -> size_t
{
    const auto segments = make_layout_link_lines(graph, index, metrics);
    size_t crossings = 0;
    for(size_t a = 0; a < segments.size(); ++a)
    {
        for(size_t b = a + 1; b < segments.size(); ++b)
        {
            const float start = std::max(std::min(segments[a].from.x, segments[a].to.x), std::min(segments[b].from.x, segments[b].to.x));
            const float end = std::min(std::max(segments[a].from.x, segments[a].to.x), std::max(segments[b].from.x, segments[b].to.x));
            if(start >= end)
            {
                continue;
            }
            const float at_start = get_height_at(segments[a], start) - get_height_at(segments[b], start);
            const float at_end = get_height_at(segments[a], end) - get_height_at(segments[b], end);
            crossings += at_start * at_end < 0.0f ? 1 : 0;
        }
    }
    return crossings;
}

/// Each node's place in its column, from top to bottom.
auto make_ranks(const std::vector<std::vector<size_t>>& by_column, size_t node_count) -> std::vector<float>
{
    std::vector<float> ranks(node_count, 0.0f);
    for(const auto& column : by_column)
    {
        for(size_t rank = 0; rank < column.size(); ++rank)
        {
            ranks[column[rank]] = float(rank);
        }
    }
    return ranks;
}

/// Reorders @p column by the mean rank of @p neighbours of each node; nodes without any keep their rank. The output,
/// leading its column, stays first.
void sort_by_barycenter(std::vector<size_t>& column,
                        const std::vector<std::vector<size_t>>& neighbours,
                        const std::vector<float>& ranks,
                        int output)
{
    std::vector<std::pair<float, size_t>> keyed;
    for(const size_t node : column)
    {
        float key = ranks[node];
        if(!neighbours[node].empty())
        {
            float sum = 0.0f;
            for(const size_t neighbour : neighbours[node])
            {
                sum += ranks[neighbour];
            }
            key = sum / float(neighbours[node].size());
        }
        keyed.emplace_back(int(node) == output ? -FLT_MAX : key, node);
    }
    std::stable_sort(keyed.begin(),
                     keyed.end(),
                     [](const auto& a, const auto& b)
                     {
                         return a.first < b.first;
                     });
    for(size_t i = 0; i < keyed.size(); ++i)
    {
        column[i] = keyed[i].second;
    }
}

/**
 * @brief The columns reordered to cross fewer links: barycenter sweeps from the output toward the sources (each node
 * at the mean rank of what reads it) and back (each at the mean rank of what it reads), starting from the order the
 * heights of a first placement give.
 */
auto reduce_crossings(const material_graph& graph, const reader_index& index, std::vector<std::vector<size_t>> by_column, int output)
    -> std::vector<std::vector<size_t>>
{
    std::vector<std::vector<size_t>> readers(graph.nodes.size());
    std::vector<std::vector<size_t>> sources(graph.nodes.size());
    for(size_t source = 0; source < index.readers.size(); ++source)
    {
        for(const auto& read : index.readers[source])
        {
            readers[source].push_back(read.node);
            sources[read.node].push_back(source);
        }
    }
    for(auto& column : by_column)
    {
        std::stable_sort(column.begin(),
                         column.end(),
                         [&graph](size_t a, size_t b)
                         {
                             return graph.nodes[a].position.y < graph.nodes[b].position.y;
                         });
    }
    for(int sweep = 0; sweep < CROSSING_SWEEPS; ++sweep)
    {
        for(size_t column = 1; column < by_column.size(); ++column)
        {
            sort_by_barycenter(by_column[column], readers, make_ranks(by_column, graph.nodes.size()), output);
        }
        for(size_t column = by_column.size(); column-- > 0;)
        {
            sort_by_barycenter(by_column[column], sources, make_ranks(by_column, graph.nodes.size()), output);
        }
    }
    return by_column;
}

auto get_node_positions(const material_graph& graph) -> std::vector<math::vec2>
{
    std::vector<math::vec2> positions;
    positions.reserve(graph.nodes.size());
    for(const auto& node : graph.nodes)
    {
        positions.push_back(node.position);
    }
    return positions;
}

void set_node_positions(material_graph& graph, const std::vector<math::vec2>& positions)
{
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        graph.nodes[i].position = positions[i];
    }
}
} // namespace

void layout_material_graph(material_graph& graph, const material_graph_layout_metrics& metrics)
{
    const auto index = make_reader_index(graph);
    std::vector<int> columns(graph.nodes.size(), 0);
    std::vector<int> state(graph.nodes.size(), 0);
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        assign_columns(index, i, columns, state);
    }
    const int column_count = graph.nodes.empty() ? 0 : *std::max_element(columns.begin(), columns.end()) + 1;
    std::vector<std::vector<size_t>> by_column(static_cast<size_t>(column_count));
    // The output leads its column; the other nodes nothing reads stack below it in the graph's order.
    const int output = find_material_graph_output(graph);
    if(output >= 0)
    {
        by_column[size_t(columns[size_t(output)])].push_back(size_t(output));
    }
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        if(int(i) != output)
        {
            by_column[size_t(columns[i])].push_back(i);
        }
    }
    // Each column level with what reads it; then the columns reordered to cross fewer links, kept when they do.
    place_columns(graph, by_column, index, metrics, false);
    const auto level_positions = get_node_positions(graph);
    const size_t level_crossings = count_crossings(graph, index, metrics);
    if(level_crossings == 0)
    {
        return;
    }
    place_columns(graph, reduce_crossings(graph, index, by_column, output), index, metrics, true);
    if(count_crossings(graph, index, metrics) >= level_crossings)
    {
        set_node_positions(graph, level_positions);
    }
}

} // namespace unravel
