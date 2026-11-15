#pragma once

#include "../script_interop.h"

#include <engine/assets/asset_handle.h>
#include <engine/rendering/material.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/standard_material.h>

#include <graphics/texture.h>
#include <reflection/reflection.h>

#include <hpp/optional.hpp>

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
    metallic,
    bumpiness,
    alpha_cutoff,
    tiling,
    color_map,
    normal_map,
    roughness_map,
    metallic_map,
    emissive_map,
    ao_map,
    alpha_mode,
    cull_mode,
};

using managed_color = dotnetpp_backend::managed_interface::color;
using managed_vector2 = dotnetpp_backend::managed_interface::vector2;
using managed_vector4 = dotnetpp_backend::managed_interface::vector4;

inline auto make_vector4(const managed_vector4& value) -> math::vec4
{
    using converter = dotnet::managed_interface::converter;
    return converter::convert<managed_vector4, math::vec4>(value);
}

inline auto make_managed_vector4(const math::vec4& value) -> managed_vector4
{
    using converter = dotnet::managed_interface::converter;
    return converter::convert<math::vec4, managed_vector4>(value);
}
using texture_handle = asset_handle<gfx::texture>;

/// The name a managed MaterialProperty has: the Standard property's key ("base_color"), or the material's cull type.
inline auto get_material_property_key(int32_t property) -> const char*
{
    switch(static_cast<material_property>(property))
    {
        case material_property::base_color:
            return "base_color";
        case material_property::emissive_color:
            return "emissive_color";
        case material_property::subsurface_color:
            return "subsurface_color";
        case material_property::emissive_intensity:
            return "emissive_intensity";
        case material_property::roughness:
            return "roughness";
        case material_property::metallic:
            return "metallic";
        case material_property::bumpiness:
            return "bumpiness";
        case material_property::alpha_cutoff:
            return "alpha_cutoff";
        case material_property::tiling:
            return "tiling";
        case material_property::color_map:
            return "color_map";
        case material_property::normal_map:
            return "normal_map";
        case material_property::roughness_map:
            return "roughness_map";
        case material_property::metallic_map:
            return "metallic_map";
        case material_property::emissive_map:
            return "emissive_map";
        case material_property::ao_map:
            return "ao_map";
        case material_property::alpha_mode:
            return "alpha_mode";
        case material_property::cull_mode:
            return "cull_type";
    }
    return "";
}

/// Whether a managed MaterialProperty is the alpha or the cull mode, which no Standard parameter keeps.
inline auto is_mode_property(int32_t property) -> bool
{
    const auto named = static_cast<material_property>(property);
    return named == material_property::alpha_mode || named == material_property::cull_mode;
}

/// The Standard graph's parameter or texture a managed MaterialProperty names (standard_material.h); null for the
/// alpha and cull modes.
inline auto find_script_standard_property(int32_t property) -> const standard_property*
{
    return is_mode_property(property) ? nullptr : find_standard_property(get_material_property_key(property));
}

/// Where script property @p property (a managed MaterialProperty) lives in @p mat (resolve_standard_property); no
/// target for the alpha and cull modes, or a property the graph does not hold.
inline auto resolve_script_property(const graph_material& mat, int32_t property) -> standard_property_target
{
    const auto* standard = find_script_standard_property(property);
    return standard != nullptr ? resolve_standard_property(mat, *standard) : standard_property_target{};
}

/// A script's value (math::color, float or math::vec2) as the four numbers make_standard_parameter_value reads.
inline auto get_script_vector(const entt::meta_any& value) -> hpp::optional<math::vec4>
{
    if(const auto* color = value.try_cast<math::color>())
    {
        return color->value;
    }
    if(const auto* number = value.try_cast<float>())
    {
        return math::vec4(*number);
    }
    if(const auto* pair = value.try_cast<math::vec2>())
    {
        return math::vec4(*pair, 0.0f, 0.0f);
    }
    return std::nullopt;
}

/// A Standard graph's parameter value @p parameter as the type scripts read for @p property.
inline auto make_script_value(const standard_property& property, const math::vec4& parameter) -> entt::meta_any
{
    const auto value = make_standard_property_value(property, parameter);
    switch(property.type)
    {
        case standard_property::kind::color:
            return math::color(value);
        case standard_property::kind::scalar:
            return value.x;
        case standard_property::kind::vector2:
            return math::vec2(value);
        case standard_property::kind::texture:
            break;
    }
    return {};
}

/// The alpha mode a graph material draws with, as a PBR material named it.
inline auto get_graph_alpha_mode(const graph_material& mat) -> alpha_mode
{
    if(mat.is_translucent())
    {
        return alpha_mode::blend;
    }
    return mat.uses_opacity_mask() ? alpha_mode::mask : alpha_mode::opaque;
}

/// What @p mat draws @p property (a managed MaterialProperty) with, typed as scripts read it; empty when its graph has no
/// such parameter or texture.
inline auto get_script_material_value(const graph_material& mat, int32_t property) -> entt::meta_any
{
    switch(static_cast<material_property>(property))
    {
        case material_property::cull_mode:
            return mat.get_cull_type();
        case material_property::alpha_mode:
            return get_graph_alpha_mode(mat);
        default:
            break;
    }
    const auto target = resolve_script_property(mat, property);
    if(target.property == nullptr)
    {
        return {};
    }
    if(target.property->type == standard_property::kind::texture)
    {
        return entt::meta_any{mat.get_texture(target.property->name)};
    }
    const auto parameter = mat.get_parameter(target.property->name);
    return parameter ? make_script_value(*target.property, apply_standard_inversion(target, *parameter)) : entt::meta_any{};
}

/**
 * @brief Gives @p mat @p value for @p property (a managed MaterialProperty), where resolve_script_property finds it
 * (Roughness as 1 - Glossiness on a specular material). False when its graph holds no such value, or for the alpha and
 * cull modes, which the graph decides (its coverage, its Two Sided and Cull Front Faces settings).
 */
inline auto set_script_material_value(graph_material& mat, int32_t property, const entt::meta_any& value) -> bool
{
    const auto target = resolve_script_property(mat, property);
    if(target.property == nullptr)
    {
        return false;
    }
    if(target.property->type == standard_property::kind::texture)
    {
        const auto* texture = value.try_cast<asset_handle<gfx::texture>>();
        return texture != nullptr && mat.set_texture(target.property->name, *texture);
    }
    const auto vector = get_script_vector(value);
    return vector && mat.set_parameter(target.property->name,
                                       make_standard_parameter_value(*target.property, apply_standard_inversion(target, *vector)));
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
void remember_shared_material_edit(const std::shared_ptr<graph_material>& shared);

/// Called when play stops: the shared materials scripts edited get the values they had by name back, in place. Their
/// graphs stay as the editor left them.
void restore_shared_material_edits();

/// Logs, once per material and property, that a script set @p property (a managed MaterialProperty) on @p mat, the
/// material @p handle names, whose graph holds no such value: the set changed nothing.
void warn_script_property_missing(const asset_handle<material>& handle, const graph_material& mat, int32_t property);

/// Logs, once per mode, that a script set @p property (AlphaMode or CullMode): the modes are the graph's (its coverage,
/// its Two Sided and Cull Front Faces settings), and a script setting them changes nothing.
void warn_script_mode_ignored(int32_t property);

} // namespace unravel
