#pragma once

#include <context/context.hpp>
#include <entt/entity/handle.hpp>
#include <entt/meta/meta.hpp>

#include <uuid/uuid.h>

#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief Editing several entities through the Inspector at once.
 *
 * The Inspector draws one entity of the selection, the one selected last, and shows the components every selected
 * entity has. An edit of one of its component rows is carried to every other selected entity: the component's state
 * before and after the edit is compared, and only what changed under the edited row is loaded into the others
 * (dragging X of a position leaves each entity's Y and Z as they are). The edit of all of them is one undo step, and
 * on prefab instances each changed field becomes an override, as a single edit records it.
 *
 * Rows whose value differs between the selected entities are marked; the value shown is the drawn entity's.
 */
namespace multi_edit
{

/// Starts drawing @p shown as the face of @p others too. Ended by end().
void begin(entt::handle shown, std::vector<entt::handle> others);
void end();

/// Whether the Inspector is drawing a selection of several entities.
auto is_active() -> bool;

/// The entities an edit made on @p shown reaches: every selected one while @p shown is the face of a selection, else
/// @p shown alone.
auto get_targets(entt::handle shown) -> std::vector<entt::handle>;

/// How many entities the selection drawn holds; 1 when it is one entity.
auto get_count() -> size_t;

/// Whether every selected entity has a component of @p type.
auto all_have(const entt::meta_type& type) -> bool;

/// The native component whose rows are drawn now; edits of its rows reach every selected entity. Empty for rows
/// that edit the drawn entity alone.
void set_component(const entt::meta_type& type);

/// Whether an edit of a row drawn now is carried to the other selected entities.
auto covers_edit() -> bool;

/**
 * @brief Tells that a drag field's value changed this frame. While the mouse drags it, the edit moves each selected
 *        entity's value by the same amount, as the gizmo does; a typed value, a slider and every other widget give
 *        all of them the drawn entity's value. Drag widgets call it when they change.
 * @param min, max The field's range, which each entity's moved value is kept in.
 */
void note_drag_change(double min = -std::numeric_limits<double>::infinity(),
                      double max = std::numeric_limits<double>::infinity());

/**
 * @brief Makes an edit of the drawn component's row at @p row_path, and carries it to the others, as one undo step.
 *        Only what the edit changed is carried, element by element in arrays.
 * @param step_name The undo step's name: the row's proxy name, so undo and redo point at the row.
 * @param row_path The row's path inside the component, as its saved document names it: where each entity's prefab
 *        override is recorded, as the row's own edit records it.
 * @param pretty_row_path The row's path as the Inspector names it, for the changes list.
 * @param write Writes the edit into the drawn entity's component, as the row's own edit would.
 */
void record_edit(rtti::context& ctx,
                 const std::string& step_name,
                 const std::string& row_path,
                 const std::string& pretty_row_path,
                 const std::function<void()>& write);

/// Whether the value at @p row_path of the drawn component differs between the selected entities.
auto is_row_mixed(const std::string& row_path) -> bool;

/// A selected asset other than the one drawn.
struct asset_identity
{
    std::string key;
    hpp::uuid uid;
};

/// Starts drawing an asset as the face of @p others, the other selected assets of its type: Apply of its import
/// settings reaches them all. Ended by end_assets().
void begin_assets(std::vector<asset_identity> others);
void end_assets();

/// The other selected assets of the drawn asset's type; empty while one asset is drawn.
auto get_other_assets() -> const std::vector<asset_identity>&;

} // namespace multi_edit
} // namespace unravel
