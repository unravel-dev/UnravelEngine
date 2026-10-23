#pragma once

#include "../script_interop.h"

#include <engine/assets/asset_handle.h>
#include <engine/rendering/material.h>

#include <graphics/texture.h>
#include <reflection/reflection.h>

#include <cstdint>

namespace unravel
{

/// A material property as scripts name it: the managed MaterialProperty enum, in its order.
enum class material_property : int32_t
{
    base_color,
    emissive_color,
    subsurface_color,
    emissive_intensity,
    roughness,
    metalness,
    bumpiness,
    alpha_cutoff,
    tiling,
    color_map,
    normal_map,
    roughness_map,
    metalness_map,
    emissive_map,
    ao_map,
    alpha_mode,
    cull_mode,
};

using managed_color = dotnetpp_backend::managed_interface::color;
using managed_vector2 = dotnetpp_backend::managed_interface::vector2;
using texture_handle = asset_handle<gfx::texture>;

/// The reflected property of the material type that a managed MaterialProperty names.
inline auto get_material_property_id(int32_t property) -> entt::id_type
{
    switch(static_cast<material_property>(property))
    {
        case material_property::base_color:
            return "base_color"_hs;
        case material_property::emissive_color:
            return "emissive_color"_hs;
        case material_property::subsurface_color:
            return "subsurface_color"_hs;
        case material_property::emissive_intensity:
            return "emissive_intensity"_hs;
        case material_property::roughness:
            return "roughness"_hs;
        case material_property::metalness:
            return "metalness"_hs;
        case material_property::bumpiness:
            return "bumpiness"_hs;
        case material_property::alpha_cutoff:
            return "alpha_cutoff"_hs;
        case material_property::tiling:
            return "tiling"_hs;
        case material_property::color_map:
            return "color_map"_hs;
        case material_property::normal_map:
            return "normal_map"_hs;
        case material_property::roughness_map:
            return "roughness_map"_hs;
        case material_property::metalness_map:
            return "metalness_map"_hs;
        case material_property::emissive_map:
            return "emissive_map"_hs;
        case material_property::ao_map:
            return "ao_map"_hs;
        case material_property::alpha_mode:
            return "alpha_mode"_hs;
        case material_property::cull_mode:
            return "cull_type"_hs;
    }
    return {};
}

inline auto make_color_value(const managed_color& value) -> entt::meta_any
{
    using converter = dotnet::managed_interface::converter;
    return converter::convert<managed_color, math::color>(value);
}

inline auto make_vector_value(const managed_vector2& value) -> entt::meta_any
{
    using converter = dotnet::managed_interface::converter;
    return converter::convert<managed_vector2, math::vec2>(value);
}

/// The texture of @p uid; the empty handle (the engine's default map) for a nil uid.
auto make_texture_value(const hpp::uuid& uid) -> entt::meta_any;

/// The alpha or cull mode @p value names, by the property it is for.
inline auto make_mode_value(int32_t property, int32_t value) -> entt::meta_any
{
    if(static_cast<material_property>(property) == material_property::alpha_mode)
    {
        return static_cast<alpha_mode>(value);
    }
    return static_cast<cull_type>(value);
}

inline auto read_color_value(const entt::meta_any& value) -> managed_color
{
    using converter = dotnet::managed_interface::converter;
    const auto* color = value.try_cast<math::color>();
    return converter::convert<math::color, managed_color>(color ? *color : math::color{});
}

inline auto read_vector_value(const entt::meta_any& value) -> managed_vector2
{
    using converter = dotnet::managed_interface::converter;
    const auto* vector = value.try_cast<math::vec2>();
    return converter::convert<math::vec2, managed_vector2>(vector ? *vector : math::vec2{});
}

inline auto read_float_value(const entt::meta_any& value) -> float
{
    const auto* number = value.try_cast<float>();
    return number ? *number : 0.0f;
}

inline auto read_texture_value(const entt::meta_any& value) -> hpp::uuid
{
    const auto* texture = value.try_cast<texture_handle>();
    return texture ? texture->uid() : hpp::uuid{};
}

inline auto read_mode_value(const entt::meta_any& value) -> int32_t
{
    if(const auto* mode = value.try_cast<alpha_mode>())
    {
        return static_cast<int32_t>(*mode);
    }
    const auto* cull = value.try_cast<cull_type>();
    return cull ? static_cast<int32_t>(*cull) : 0;
}

/**
 * @brief Script edits of a shared material are runtime state: the first edit of a material keeps a copy of it, and
 * restore_shared_material_edits gives every edited material its values back.
 */
void remember_shared_material_edit(const material::sptr& shared);

/// Called when play stops: the shared materials scripts edited get their values back, in place.
void restore_shared_material_edits();

} // namespace unravel
