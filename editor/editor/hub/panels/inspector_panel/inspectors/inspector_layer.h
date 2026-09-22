#pragma once
#include "inspector.h"

#include <engine/physics/ecs/components/physics_component.h>

#include <string>

namespace unravel
{

struct inspector_layer : public crtp_meta_type<inspector_layer, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_layer, layer_mask)

/// The layers of a mask by their names in the project settings, or Everything / Nothing.
auto get_layer_mask_text(rtti::context& ctx, const layer_mask& mask) -> std::string;

//-----------------------------------------------------------------------------
/// <summary>
/// The items of a layer mask menu, for a popup or a combo the caller has open: a way to the
/// layer settings, Nothing, Everything, then a toggle per named layer. The menu stays open while
/// the layers are toggled. Every layer mask field of the editor edits through these items.
/// </summary>
/// <returns>True when the mask changed</returns>
//-----------------------------------------------------------------------------
auto draw_layer_mask_menu_items(rtti::context& ctx, layer_mask& mask) -> bool;

} // namespace unravel
