#include "asset_icons.h"

#include <hpp/utility.hpp>

namespace unravel
{

auto get_asset_icon_for_extension(const std::string& extension) -> const char*
{
    const char* icon = get_asset_icon<void>();
    bool is_found = false;
    hpp::for_each_type<gfx::texture,
                       scene_prefab,
                       material,
                       physics_material,
                       style_sheet,
                       audio_clip,
                       mesh,
                       prefab,
                       animation_clip,
                       animation_controller,
                       font>(
        [&](auto tag)
        {
            using asset_t = typename std::decay_t<decltype(tag)>::type;
            if(!is_found && ex::is_format<asset_t>(extension))
            {
                is_found = true;
                icon = get_asset_icon<asset_t>();
            }
        });
    return icon;
}

} // namespace unravel
