#pragma once

#include <context/context.hpp>
#include <engine/assets/asset_handle.h>
#include <engine/rendering/material_graph/material_graph.h>

namespace unravel
{
class material;

/// Puts @p after into graph material @p asset and saves it, as one undo step that puts @p before back: an edit of
/// the material made outside the Material Graph window.
void record_graph_material_edit(rtti::context& ctx, const asset_handle<material>& asset, material_graph before, material_graph after);

} // namespace unravel
