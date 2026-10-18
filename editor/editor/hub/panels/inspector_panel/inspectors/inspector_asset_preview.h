#pragma once

#include "inspector_asset_picker.h"

#include <editor/assets/asset_icons.h>
#include <editor/editing/thumbnail_manager.h>
#include <editor/imgui/integration/imgui.h>
#include <engine/assets/asset_handle.h>

namespace unravel::asset_picker
{

/// The thumbnail of an asset for a picker tile or field: the icon of its type when it has none, a clock
/// while it loads.
template<typename T>
auto make_preview(thumbnail_manager& tm, const asset_handle<T>& asset) -> preview
{
    preview result{};
    result.icon = get_asset_icon<T>();
    if(!asset)
    {
        return result;
    }
    // Asking for the thumbnail is also what starts loading an asset nobody uses yet.
    const auto& thumbnail = tm.get_thumbnail(asset);
    if(!asset.is_ready())
    {
        result.is_loading = true;
        return result;
    }
    result.texture = ImGui::ToId(thumbnail);
    result.texture_size = ImGui::GetSize(thumbnail);
    return result;
}

} // namespace unravel::asset_picker
