#pragma once

#include "../script_interop.h"

#include <engine/rendering/material.h>

#include <memory>

namespace unravel
{

inline auto get_material_properties(const material::sptr& material)
    -> dotnetpp_backend::managed_interface::material_properties
{
    using converter = dotnet::managed_interface::converter;
    dotnetpp_backend::managed_interface::material_properties props;
    if(material->is<pbr_material>())
    {
        const auto pbr = std::static_pointer_cast<pbr_material>(material);
        props.base_color =
            converter::convert<math::color, dotnetpp_backend::managed_interface::color>(pbr->get_base_color());
        props.emissive_color =
            converter::convert<math::color, dotnetpp_backend::managed_interface::color>(pbr->get_emissive_color());
        props.tiling =
            converter::convert<math::vec2, dotnetpp_backend::managed_interface::vector2>(pbr->get_tiling());
        props.roughness = pbr->get_roughness();
        props.metalness = pbr->get_metalness();
        props.bumpiness = pbr->get_bumpiness();
        props.valid = true;
    }
    return props;
}

inline void set_material_properties(const material::sptr& material,
                                    const dotnetpp_backend::managed_interface::material_properties& props)
{
    using converter = dotnet::managed_interface::converter;
    if(material->is<pbr_material>())
    {
        auto pbr = std::static_pointer_cast<pbr_material>(material);
        auto base_color =
            converter::convert<dotnetpp_backend::managed_interface::color, math::color>(props.base_color);
        pbr->set_base_color(base_color);
        auto emissive_color =
            converter::convert<dotnetpp_backend::managed_interface::color, math::color>(props.emissive_color);
        pbr->set_emissive_color(emissive_color);
        auto tiling =
            converter::convert<dotnetpp_backend::managed_interface::vector2, math::vec2>(props.tiling);
        pbr->set_tiling(tiling);
        pbr->set_metalness(props.metalness);
        pbr->set_bumpiness(props.bumpiness);
    }
}

} // namespace unravel
