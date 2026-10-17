#pragma once

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <engine/assets/impl/asset_extensions.h>

#include <string>
#include <type_traits>

namespace unravel
{

/// The icon of an asset type, for a field, a picker tile or a list row without a thumbnail.
template<typename T>
auto get_asset_icon() -> const char*
{
    if constexpr(std::is_same_v<T, gfx::texture>)
    {
        return ICON_MDI_IMAGE_OUTLINE;
    }
    else if constexpr(std::is_same_v<T, material>)
    {
        return ICON_MDI_CIRCLE_HALF_FULL;
    }
    else if constexpr(std::is_same_v<T, mesh>)
    {
        return ICON_MDI_CUBE_OUTLINE;
    }
    else if constexpr(std::is_same_v<T, animation_clip>)
    {
        return ICON_MDI_ANIMATION_OUTLINE;
    }
    else if constexpr(std::is_same_v<T, animation_controller>)
    {
        return ICON_MDI_STATE_MACHINE;
    }
    else if constexpr(std::is_same_v<T, prefab>)
    {
        return ICON_MDI_PACKAGE_VARIANT_CLOSED;
    }
    else if constexpr(std::is_same_v<T, scene_prefab>)
    {
        return ICON_MDI_MAP_OUTLINE;
    }
    else if constexpr(std::is_same_v<T, physics_material>)
    {
        return ICON_MDI_ATOM;
    }
    else if constexpr(std::is_same_v<T, audio_clip>)
    {
        return ICON_MDI_MUSIC_NOTE;
    }
    else if constexpr(std::is_same_v<T, font>)
    {
        return ICON_MDI_FORMAT_FONT;
    }
    else if constexpr(std::is_same_v<T, style_sheet>)
    {
        return ICON_MDI_LANGUAGE_CSS3;
    }
    else
    {
        return ICON_MDI_FILE_DOCUMENT_OUTLINE;
    }
}

/// The icon of the asset type a file extension belongs to (".png": the texture icon); the document
/// icon for other files.
auto get_asset_icon_for_extension(const std::string& extension) -> const char*;

} // namespace unravel
