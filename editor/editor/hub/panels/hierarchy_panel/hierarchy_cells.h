#pragma once

#include <context/context.hpp>
#include <engine/ecs/ecs.h>

#include <imgui/imgui_internal.h>

/// The cells of a hierarchy row besides the name: small inline editors in the look of the panel
/// toolbars. Each edits one value of the row's entity through an undoable action. The active,
/// static and tag cells stay blank for a default value until the pointer is over the row, so they
/// show what sets an entity apart; the layer cell shows the layers of every row.
namespace unravel::hierarchy_cells
{

/// What a cell knows about the row it sits in.
struct row_state
{
    entt::handle entity{};
    /// The pointer is over the row, its cells included.
    bool is_hovered{};
};

/// Height of a row. The cells fill it; the host sets up its rows with it.
auto get_row_height() -> float;

/// Eye that turns the entity on and off. Shown on the hovered row, and always while it is off.
void draw_active_cell(rtti::context& ctx, const row_state& row);

/// Checkbox of the static flag of the entity's model. Blank for an entity without a model.
void draw_static_cell(rtti::context& ctx, const row_state& row);

/// Layers of the entity, as a dropdown of the layers of the project.
void draw_layer_cell(rtti::context& ctx, const row_state& row);

/// Tag of the entity, as a dropdown of the tags the scene uses, with a field for a new one.
void draw_tag_cell(rtti::context& ctx, const row_state& row);

//-----------------------------------------------------------------------------
/// <summary>
/// Chevron at the right end of the name cell of a prefab instance, which opens the prefab. It is
/// laid over the row and takes no room. Returns true when clicked.
/// </summary>
/// <param name="cell_rect">Screen rectangle of the name cell, one row high</param>
//-----------------------------------------------------------------------------
auto draw_open_prefab_button(const ImRect& cell_rect) -> bool;

/// Width the open prefab button takes at the right end of a name cell.
auto get_open_prefab_button_width() -> float;

} // namespace unravel::hierarchy_cells
