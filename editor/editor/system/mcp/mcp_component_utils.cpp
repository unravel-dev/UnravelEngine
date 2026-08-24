#include "mcp_component_utils.h"

#include "mcp_tools_common.h"

#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_clip.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/volume_component.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <graphics/texture.h>
#include <math/color.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <unordered_set>

namespace unravel::mcp
{
namespace
{

auto resolve_texture(rtti::context& ctx, const std::string& key_or_uid, std::string& error)
    -> asset_handle<gfx::texture>
{
    auto& am = ctx.get_cached<asset_manager>();
    if(key_or_uid.empty())
    {
        return {};
    }
    auto as_uuid = hpp::uuid::from_string(key_or_uid);
    if(as_uuid)
    {
        auto handle = am.get_asset<gfx::texture>(*as_uuid);
        if(!handle)
        {
            error = "Texture not found for uid: " + key_or_uid;
        }
        return handle;
    }
    auto handle = am.get_asset<gfx::texture>(key_or_uid);
    if(!handle)
    {
        error = "Texture not found: " + key_or_uid;
    }
    return handle;
}

auto resolve_audio_clip(rtti::context& ctx, const std::string& key_or_uid, std::string& error)
    -> asset_handle<audio_clip>
{
    auto& am = ctx.get_cached<asset_manager>();
    if(key_or_uid.empty())
    {
        return {};
    }
    auto as_uuid = hpp::uuid::from_string(key_or_uid);
    if(as_uuid)
    {
        auto handle = am.get_asset<audio_clip>(*as_uuid);
        if(!handle)
        {
            error = "Audio clip not found for uid: " + key_or_uid;
        }
        return handle;
    }
    auto handle = am.get_asset<audio_clip>(key_or_uid);
    if(!handle)
    {
        error = "Audio clip not found: " + key_or_uid;
    }
    return handle;
}

auto light_type_to_string(light_type type) -> const char*
{
    switch(type)
    {
        case light_type::spot:
            return "spot";
        case light_type::point:
            return "point";
        case light_type::directional:
            return "directional";
        default:
            return "directional";
    }
}

auto light_type_from_string(std::string_view value, light_type& out) -> bool
{
    if(value == "spot")
    {
        out = light_type::spot;
        return true;
    }
    if(value == "point")
    {
        out = light_type::point;
        return true;
    }
    if(value == "directional")
    {
        out = light_type::directional;
        return true;
    }
    return false;
}

auto sm_resolution_to_string(sm_resolution value) -> const char*
{
    switch(value)
    {
        case sm_resolution::low:
            return "low";
        case sm_resolution::medium:
            return "medium";
        case sm_resolution::high:
            return "high";
        case sm_resolution::very_high:
            return "very_high";
        default:
            return "high";
    }
}

auto sm_resolution_from_string(std::string_view value, sm_resolution& out) -> bool
{
    if(value == "low")
    {
        out = sm_resolution::low;
        return true;
    }
    if(value == "medium")
    {
        out = sm_resolution::medium;
        return true;
    }
    if(value == "high")
    {
        out = sm_resolution::high;
        return true;
    }
    if(value == "very_high")
    {
        out = sm_resolution::very_high;
        return true;
    }
    return false;
}

auto sky_mode_to_string(skylight_component::sky_mode mode) -> const char*
{
    switch(mode)
    {
        case skylight_component::sky_mode::perez:
            return "perez";
        case skylight_component::sky_mode::skybox:
            return "skybox";
        default:
            return "perez";
    }
}

auto sky_mode_from_string(std::string_view value, skylight_component::sky_mode& out) -> bool
{
    if(value == "perez")
    {
        out = skylight_component::sky_mode::perez;
        return true;
    }
    if(value == "skybox")
    {
        out = skylight_component::sky_mode::skybox;
        return true;
    }
    return false;
}

auto cloud_mode_to_string(skylight_component::cloud_mode mode) -> const char*
{
    switch(mode)
    {
        case skylight_component::cloud_mode::none:
            return "none";
        case skylight_component::cloud_mode::flat:
            return "flat";
        case skylight_component::cloud_mode::volumetric:
            return "volumetric";
        default:
            return "none";
    }
}

auto cloud_mode_from_string(std::string_view value, skylight_component::cloud_mode& out) -> bool
{
    if(value == "none")
    {
        out = skylight_component::cloud_mode::none;
        return true;
    }
    if(value == "flat")
    {
        out = skylight_component::cloud_mode::flat;
        return true;
    }
    if(value == "volumetric")
    {
        out = skylight_component::cloud_mode::volumetric;
        return true;
    }
    return false;
}

auto irradiance_quality_to_string(skylight_component::irradiance_quality q) -> const char*
{
    return q == skylight_component::irradiance_quality::flat ? "flat" : "directional";
}

auto irradiance_quality_from_string(std::string_view value, skylight_component::irradiance_quality& out) -> bool
{
    if(value == "flat")
    {
        out = skylight_component::irradiance_quality::flat;
        return true;
    }
    if(value == "directional")
    {
        out = skylight_component::irradiance_quality::directional;
        return true;
    }
    return false;
}

auto volume_mode_to_string(volume_mode mode) -> const char*
{
    return mode == volume_mode::global ? "global" : "local";
}

auto volume_mode_from_string(std::string_view value, volume_mode& out) -> bool
{
    if(value == "local")
    {
        out = volume_mode::local;
        return true;
    }
    if(value == "global")
    {
        out = volume_mode::global;
        return true;
    }
    return false;
}

auto projection_mode_to_string(projection_mode mode) -> const char*
{
    return mode == projection_mode::orthographic ? "orthographic" : "perspective";
}

auto projection_mode_from_string(std::string_view value, projection_mode& out) -> bool
{
    if(value == "perspective")
    {
        out = projection_mode::perspective;
        return true;
    }
    if(value == "orthographic" || value == "ortho")
    {
        out = projection_mode::orthographic;
        return true;
    }
    return false;
}

auto wants_key(const std::unordered_set<std::string>* filter, const char* key) -> bool
{
    return !filter || filter->empty() || filter->count(key) > 0;
}

auto append_prop(std::string& json, bool& first, const char* key, const std::string& value_json) -> void
{
    if(!first)
    {
        json += ",";
    }
    first = false;
    json += make_json_string(key);
    json += ":";
    json += value_json;
}

auto light_to_json(const light_component& comp, const std::unordered_set<std::string>* filter) -> std::string
{
    const auto& l = comp.get_light();
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "type"))
    {
        append_prop(json, first, "type", make_json_string(light_type_to_string(l.type)));
    }
    if(wants_key(filter, "intensity"))
    {
        append_prop(json, first, "intensity", fmt::format("{:.6g}", l.intensity));
    }
    if(wants_key(filter, "color"))
    {
        append_prop(json, first, "color", color_to_json(l.color));
    }
    if(wants_key(filter, "casts_shadows"))
    {
        append_prop(json, first, "casts_shadows", l.casts_shadows ? "true" : "false");
    }
    if(wants_key(filter, "range"))
    {
        const float range = (l.type == light_type::spot) ? l.spot_data.get_range() : l.point_data.range;
        append_prop(json, first, "range", fmt::format("{:.6g}", range));
    }
    if(wants_key(filter, "exponent_falloff"))
    {
        append_prop(json, first, "exponent_falloff", fmt::format("{:.6g}", l.point_data.exponent_falloff));
    }
    if(wants_key(filter, "outer_angle"))
    {
        append_prop(json, first, "outer_angle", fmt::format("{:.6g}", l.spot_data.get_outer_angle()));
    }
    if(wants_key(filter, "inner_angle"))
    {
        append_prop(json, first, "inner_angle", fmt::format("{:.6g}", l.spot_data.get_inner_angle()));
    }
    if(wants_key(filter, "shadow_bias"))
    {
        append_prop(json, first, "shadow_bias", fmt::format("{:.6g}", l.shadow_params.bias));
    }
    if(wants_key(filter, "shadow_normal_bias"))
    {
        append_prop(json, first, "shadow_normal_bias", fmt::format("{:.6g}", l.shadow_params.normal_bias));
    }
    if(wants_key(filter, "shadow_slope_bias"))
    {
        append_prop(json, first, "shadow_slope_bias", fmt::format("{:.6g}", l.shadow_params.slope_bias));
    }
    if(wants_key(filter, "shadow_near_plane"))
    {
        append_prop(json, first, "shadow_near_plane", fmt::format("{:.6g}", l.shadow_params.near_plane));
    }
    if(wants_key(filter, "shadow_far_plane"))
    {
        append_prop(json, first, "shadow_far_plane", fmt::format("{:.6g}", l.shadow_params.far_plane));
    }
    if(wants_key(filter, "shadow_resolution"))
    {
        append_prop(json, first, "shadow_resolution", make_json_string(sm_resolution_to_string(l.shadow_params.resolution)));
    }
    if(wants_key(filter, "contact_shadow_enabled"))
    {
        append_prop(json, first, "contact_shadow_enabled", l.contact_shadow.enabled ? "true" : "false");
    }
    if(wants_key(filter, "contact_shadow_ray_length"))
    {
        append_prop(json, first, "contact_shadow_ray_length", fmt::format("{:.6g}", l.contact_shadow.ray_length));
    }
    if(wants_key(filter, "contact_shadow_thickness"))
    {
        append_prop(json, first, "contact_shadow_thickness", fmt::format("{:.6g}", l.contact_shadow.thickness));
    }
    if(wants_key(filter, "contact_shadow_max_distance"))
    {
        append_prop(json, first, "contact_shadow_max_distance", fmt::format("{:.6g}", l.contact_shadow.max_distance));
    }
    if(wants_key(filter, "contact_shadow_opacity"))
    {
        append_prop(json, first, "contact_shadow_opacity", fmt::format("{:.6g}", l.contact_shadow.opacity));
    }
    if(wants_key(filter, "split_distribution"))
    {
        append_prop(json, first, "split_distribution", fmt::format("{:.6g}", l.directional_shadow_params.split_distribution));
    }
    if(wants_key(filter, "num_splits"))
    {
        append_prop(json, first, "num_splits", fmt::format("{}", static_cast<int>(l.directional_shadow_params.num_splits)));
    }
    if(wants_key(filter, "stabilize"))
    {
        append_prop(json, first, "stabilize", l.directional_shadow_params.stabilize ? "true" : "false");
    }
    json += "}";
    return json;
}

auto apply_light_properties(light_component& comp, const simdjson::dom::object& properties, component_apply_result& result)
    -> void
{
    auto light = comp.get_light();
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        if(key == "type")
        {
            std::string s;
            if(!parse_string(value, s, error) || !light_type_from_string(s, light.type))
            {
                result.ok = false;
                result.errors.push_back(key + ": expected spot|point|directional");
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "intensity")
        {
            if(!parse_number(value, light.intensity, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "color")
        {
            if(!parse_color(value, light.color, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "casts_shadows")
        {
            if(!parse_bool(value, light.casts_shadows, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "range")
        {
            float range = 0.0f;
            if(!parse_number(value, range, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            light.spot_data.set_range(range);
            light.point_data.range = range;
            result.applied.push_back(key);
        }
        else if(key == "exponent_falloff")
        {
            if(!parse_number(value, light.point_data.exponent_falloff, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "outer_angle")
        {
            float angle = 0.0f;
            if(!parse_number(value, angle, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            light.spot_data.set_outer_angle(angle);
            result.applied.push_back(key);
        }
        else if(key == "inner_angle")
        {
            float angle = 0.0f;
            if(!parse_number(value, angle, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            light.spot_data.set_inner_angle(angle);
            result.applied.push_back(key);
        }
        else if(key == "shadow_bias")
        {
            if(!parse_number(value, light.shadow_params.bias, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "shadow_normal_bias")
        {
            if(!parse_number(value, light.shadow_params.normal_bias, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "shadow_slope_bias")
        {
            if(!parse_number(value, light.shadow_params.slope_bias, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "shadow_near_plane")
        {
            if(!parse_number(value, light.shadow_params.near_plane, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "shadow_far_plane")
        {
            if(!parse_number(value, light.shadow_params.far_plane, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "shadow_resolution")
        {
            std::string s;
            if(!parse_string(value, s, error) || !sm_resolution_from_string(s, light.shadow_params.resolution))
            {
                result.ok = false;
                result.errors.push_back(key + ": expected low|medium|high|very_high");
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "contact_shadow_enabled")
        {
            if(!parse_bool(value, light.contact_shadow.enabled, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "contact_shadow_ray_length")
        {
            if(!parse_number(value, light.contact_shadow.ray_length, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "contact_shadow_thickness")
        {
            if(!parse_number(value, light.contact_shadow.thickness, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "contact_shadow_max_distance")
        {
            if(!parse_number(value, light.contact_shadow.max_distance, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "contact_shadow_opacity")
        {
            if(!parse_number(value, light.contact_shadow.opacity, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "split_distribution")
        {
            if(!parse_number(value, light.directional_shadow_params.split_distribution, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "num_splits")
        {
            int splits = 0;
            if(!parse_int(value, splits, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            light.directional_shadow_params.num_splits = static_cast<uint8_t>(std::clamp(splits, 1, 4));
            result.applied.push_back(key);
        }
        else if(key == "stabilize")
        {
            if(!parse_bool(value, light.directional_shadow_params.stabilize, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
    comp.set_light(light);
}

auto skylight_to_json(const skylight_component& comp, const std::unordered_set<std::string>* filter) -> std::string
{
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "mode"))
    {
        append_prop(json, first, "mode", make_json_string(sky_mode_to_string(comp.get_mode())));
    }
    if(wants_key(filter, "sky_brightness"))
    {
        append_prop(json, first, "sky_brightness", fmt::format("{:.6g}", comp.get_sky_brightness()));
    }
    if(wants_key(filter, "turbidity"))
    {
        append_prop(json, first, "turbidity", fmt::format("{:.6g}", comp.get_turbidity()));
    }
    if(wants_key(filter, "cloud_mode"))
    {
        append_prop(json, first, "cloud_mode", make_json_string(cloud_mode_to_string(comp.get_cloud_mode())));
    }
    if(wants_key(filter, "cloud_coverage"))
    {
        append_prop(json, first, "cloud_coverage", fmt::format("{:.6g}", comp.get_cloud_coverage()));
    }
    if(wants_key(filter, "cloud_speed"))
    {
        append_prop(json, first, "cloud_speed", fmt::format("{:.6g}", comp.get_cloud_speed()));
    }
    if(wants_key(filter, "cloud_macro_variation"))
    {
        append_prop(json, first, "cloud_macro_variation", fmt::format("{:.6g}", comp.get_cloud_macro_variation()));
    }
    if(wants_key(filter, "cloud_base_altitude"))
    {
        append_prop(json, first, "cloud_base_altitude", fmt::format("{:.6g}", comp.get_cloud_base_altitude()));
    }
    if(wants_key(filter, "cloud_thickness"))
    {
        append_prop(json, first, "cloud_thickness", fmt::format("{:.6g}", comp.get_cloud_thickness()));
    }
    if(wants_key(filter, "cloud_size"))
    {
        append_prop(json, first, "cloud_size", fmt::format("{:.6g}", comp.get_cloud_size()));
    }
    if(wants_key(filter, "cloud_density"))
    {
        append_prop(json, first, "cloud_density", fmt::format("{:.6g}", comp.get_cloud_density()));
    }
    if(wants_key(filter, "cloud_shadow_strength"))
    {
        append_prop(json, first, "cloud_shadow_strength", fmt::format("{:.6g}", comp.get_cloud_shadow_strength()));
    }
    if(wants_key(filter, "cloud_world_space_altitude"))
    {
        append_prop(json, first, "cloud_world_space_altitude", comp.get_cloud_world_space_altitude() ? "true" : "false");
    }
    if(wants_key(filter, "cloud_shadows"))
    {
        append_prop(json, first, "cloud_shadows", comp.get_cloud_shadows() ? "true" : "false");
    }
    if(wants_key(filter, "cloud_shadow_opacity"))
    {
        append_prop(json, first, "cloud_shadow_opacity", fmt::format("{:.6g}", comp.get_cloud_shadow_opacity()));
    }
    if(wants_key(filter, "cloud_softness"))
    {
        append_prop(json, first, "cloud_softness", fmt::format("{:.6g}", comp.get_cloud_softness()));
    }
    if(wants_key(filter, "cloud_detail_erode"))
    {
        append_prop(json, first, "cloud_detail_erode", fmt::format("{:.6g}", comp.get_cloud_detail_erode()));
    }
    if(wants_key(filter, "cloud_wind_direction"))
    {
        append_prop(json, first, "cloud_wind_direction", fmt::format("{:.6g}", comp.get_cloud_wind_direction()));
    }
    if(wants_key(filter, "irradiance_intensity"))
    {
        append_prop(json, first, "irradiance_intensity", fmt::format("{:.6g}", comp.get_irradiance_intensity()));
    }
    if(wants_key(filter, "irradiance_tint"))
    {
        append_prop(json, first, "irradiance_tint", color_to_json(comp.get_irradiance_tint()));
    }
    if(wants_key(filter, "irradiance_quality"))
    {
        append_prop(json, first, "irradiance_quality", make_json_string(irradiance_quality_to_string(comp.get_irradiance_quality())));
    }
    if(wants_key(filter, "irradiance_use_sky"))
    {
        append_prop(json, first, "irradiance_use_sky", comp.get_irradiance_use_sky() ? "true" : "false");
    }
    if(wants_key(filter, "cubemap"))
    {
        append_prop(json, first, "cubemap", asset_handle_key_json(comp.get_cubemap()));
    }
    json += "}";
    return json;
}

auto apply_skylight_properties(rtti::context& ctx,
                               skylight_component& comp,
                               const simdjson::dom::object& properties,
                               component_apply_result& result) -> void
{
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        if(key == "mode")
        {
            std::string s;
            skylight_component::sky_mode mode{};
            if(!parse_string(value, s, error) || !sky_mode_from_string(s, mode))
            {
                result.ok = false;
                result.errors.push_back(key + ": expected perez|skybox");
                continue;
            }
            comp.set_mode(mode);
            result.applied.push_back(key);
        }
        else if(key == "sky_brightness")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_sky_brightness(v);
            result.applied.push_back(key);
        }
        else if(key == "turbidity")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_turbidity(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_mode")
        {
            std::string s;
            skylight_component::cloud_mode mode{};
            if(!parse_string(value, s, error) || !cloud_mode_from_string(s, mode))
            {
                result.ok = false;
                result.errors.push_back(key + ": expected none|flat|volumetric");
                continue;
            }
            comp.set_cloud_mode(mode);
            result.applied.push_back(key);
        }
        else if(key == "cloud_coverage")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_coverage(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_speed")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_speed(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_macro_variation")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_macro_variation(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_base_altitude")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_base_altitude(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_thickness")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_thickness(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_size")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_size(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_density")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_density(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_shadow_strength")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_shadow_strength(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_world_space_altitude")
        {
            bool v = false;
            if(!parse_bool(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_world_space_altitude(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_shadows")
        {
            bool v = false;
            if(!parse_bool(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_shadows(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_shadow_opacity")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_shadow_opacity(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_softness")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_softness(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_detail_erode")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_detail_erode(v);
            result.applied.push_back(key);
        }
        else if(key == "cloud_wind_direction")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_wind_direction(v);
            result.applied.push_back(key);
        }
        else if(key == "irradiance_intensity")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_irradiance_intensity(v);
            result.applied.push_back(key);
        }
        else if(key == "irradiance_tint")
        {
            math::color c{};
            if(!parse_color(value, c, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_irradiance_tint(c);
            result.applied.push_back(key);
        }
        else if(key == "irradiance_quality")
        {
            std::string s;
            skylight_component::irradiance_quality q{};
            if(!parse_string(value, s, error) || !irradiance_quality_from_string(s, q))
            {
                result.ok = false;
                result.errors.push_back(key + ": expected flat|directional");
                continue;
            }
            comp.set_irradiance_quality(q);
            result.applied.push_back(key);
        }
        else if(key == "irradiance_use_sky")
        {
            bool v = false;
            if(!parse_bool(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_irradiance_use_sky(v);
            result.applied.push_back(key);
        }
        else if(key == "cubemap")
        {
            std::string s;
            if(!parse_string(value, s, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            if(s.empty())
            {
                comp.set_cubemap({});
                result.applied.push_back(key);
                continue;
            }
            auto handle = resolve_texture(ctx, s, error);
            if(!handle && !s.empty())
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cubemap(handle);
            result.applied.push_back(key);
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
}

auto audio_to_json(const audio_source_component& comp, const std::unordered_set<std::string>* filter) -> std::string
{
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "auto_play"))
    {
        append_prop(json, first, "auto_play", comp.get_autoplay() ? "true" : "false");
    }
    if(wants_key(filter, "loop"))
    {
        append_prop(json, first, "loop", comp.is_looping() ? "true" : "false");
    }
    if(wants_key(filter, "volume"))
    {
        append_prop(json, first, "volume", fmt::format("{:.6g}", comp.get_volume()));
    }
    if(wants_key(filter, "pitch"))
    {
        append_prop(json, first, "pitch", fmt::format("{:.6g}", comp.get_pitch()));
    }
    if(wants_key(filter, "volume_rolloff"))
    {
        append_prop(json, first, "volume_rolloff", fmt::format("{:.6g}", comp.get_volume_rolloff()));
    }
    if(wants_key(filter, "range_min") || wants_key(filter, "range_max"))
    {
        const auto& range = comp.get_range();
        if(wants_key(filter, "range_min"))
        {
            append_prop(json, first, "range_min", fmt::format("{:.6g}", range.min));
        }
        if(wants_key(filter, "range_max"))
        {
            append_prop(json, first, "range_max", fmt::format("{:.6g}", range.max));
        }
    }
    if(wants_key(filter, "mute"))
    {
        append_prop(json, first, "mute", comp.is_muted() ? "true" : "false");
    }
    if(wants_key(filter, "clip"))
    {
        append_prop(json, first, "clip", asset_handle_key_json(comp.get_clip()));
    }
    json += "}";
    return json;
}

auto apply_audio_properties(rtti::context& ctx,
                            audio_source_component& comp,
                            const simdjson::dom::object& properties,
                            component_apply_result& result) -> void
{
    auto range = comp.get_range();
    bool range_dirty = false;
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        if(key == "auto_play")
        {
            bool v = false;
            if(!parse_bool(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_autoplay(v);
            result.applied.push_back(key);
        }
        else if(key == "loop")
        {
            bool v = false;
            if(!parse_bool(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_loop(v);
            result.applied.push_back(key);
        }
        else if(key == "volume")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_volume(v);
            result.applied.push_back(key);
        }
        else if(key == "pitch")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_pitch(v);
            result.applied.push_back(key);
        }
        else if(key == "volume_rolloff")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_volume_rolloff(v);
            result.applied.push_back(key);
        }
        else if(key == "range_min")
        {
            if(!parse_number(value, range.min, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            range_dirty = true;
            result.applied.push_back(key);
        }
        else if(key == "range_max")
        {
            if(!parse_number(value, range.max, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            range_dirty = true;
            result.applied.push_back(key);
        }
        else if(key == "mute")
        {
            bool v = false;
            if(!parse_bool(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_mute(v);
            result.applied.push_back(key);
        }
        else if(key == "clip")
        {
            std::string s;
            if(!parse_string(value, s, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            if(s.empty())
            {
                comp.set_clip({});
                result.applied.push_back(key);
                continue;
            }
            auto handle = resolve_audio_clip(ctx, s, error);
            if(!handle)
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_clip(handle);
            result.applied.push_back(key);
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
    if(range_dirty)
    {
        if(range.min > range.max)
        {
            std::swap(range.min, range.max);
        }
        comp.set_range(range);
    }
}

auto camera_to_json(const camera_component& comp, const std::unordered_set<std::string>* filter) -> std::string
{
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "fov"))
    {
        append_prop(json, first, "fov", fmt::format("{:.6g}", comp.get_fov()));
    }
    if(wants_key(filter, "near_clip"))
    {
        append_prop(json, first, "near_clip", fmt::format("{:.6g}", comp.get_near_clip()));
    }
    if(wants_key(filter, "far_clip"))
    {
        append_prop(json, first, "far_clip", fmt::format("{:.6g}", comp.get_far_clip()));
    }
    if(wants_key(filter, "projection_mode"))
    {
        append_prop(json, first, "projection_mode", make_json_string(projection_mode_to_string(comp.get_projection_mode())));
    }
    json += "}";
    return json;
}

auto apply_camera_properties(camera_component& comp,
                             const simdjson::dom::object& properties,
                             component_apply_result& result) -> void
{
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        if(key == "fov")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_fov(v);
            result.applied.push_back(key);
        }
        else if(key == "near_clip")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_near_clip(v);
            result.applied.push_back(key);
        }
        else if(key == "far_clip")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_far_clip(v);
            result.applied.push_back(key);
        }
        else if(key == "projection_mode")
        {
            std::string s;
            projection_mode mode{};
            if(!parse_string(value, s, error) || !projection_mode_from_string(s, mode))
            {
                result.ok = false;
                result.errors.push_back(key + ": expected perspective|orthographic");
                continue;
            }
            comp.set_projection_mode(mode);
            result.applied.push_back(key);
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
}

auto volume_to_json(const volume_component& comp, const std::unordered_set<std::string>* filter) -> std::string
{
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "mode"))
    {
        append_prop(json, first, "mode", make_json_string(volume_mode_to_string(comp.mode)));
    }
    if(wants_key(filter, "priority"))
    {
        append_prop(json, first, "priority", fmt::format("{}", comp.priority));
    }
    if(wants_key(filter, "weight"))
    {
        append_prop(json, first, "weight", fmt::format("{:.6g}", comp.weight));
    }
    if(wants_key(filter, "blend_distance"))
    {
        append_prop(json, first, "blend_distance", fmt::format("{:.6g}", comp.blend_distance));
    }
    if(wants_key(filter, "extents"))
    {
        append_prop(json, first, "extents", vec3_to_json(comp.extents));
    }
    json += "}";
    return json;
}

auto apply_volume_properties(volume_component& comp,
                             const simdjson::dom::object& properties,
                             component_apply_result& result) -> void
{
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        if(key == "mode")
        {
            std::string s;
            if(!parse_string(value, s, error) || !volume_mode_from_string(s, comp.mode))
            {
                result.ok = false;
                result.errors.push_back(key + ": expected local|global");
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "priority")
        {
            if(!parse_int(value, comp.priority, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "weight")
        {
            if(!parse_number(value, comp.weight, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "blend_distance")
        {
            if(!parse_number(value, comp.blend_distance, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else if(key == "extents")
        {
            if(!parse_vec3(value, comp.extents, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            result.applied.push_back(key);
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
}

auto resolve_script_type(rtti::context& ctx, const std::string& script_type, std::string& error) -> dotnet::type
{
    if(script_type.empty())
    {
        error = "script_type is required for Script component";
        return {};
    }
    if(!ctx.has<script_system>())
    {
        error = "Script system unavailable";
        return {};
    }
    auto& ss = ctx.get_cached<script_system>();
    auto type = ss.get_type_by_fullname(script_type);
    if(type.valid())
    {
        return type;
    }
    // Allow short names (FlyOrbit) by scanning scriptable types.
    for(const auto& candidate : ss.get_all_scriptable_components())
    {
        if(candidate.get_name() == script_type || candidate.get_fullname() == script_type)
        {
            return candidate;
        }
    }
    error = "Unknown script type: " + script_type;
    return {};
}

auto script_field_to_json(const dotnet::object& obj, const dotnet::field& field) -> std::optional<std::string>
{
    const auto& field_type = field.get_type();
    const auto type_name = field_type.get_name();
    if(type_name == "Single" || type_name == "Double")
    {
        auto invoker = dotnet::make_field_invoker<float>(field);
        return fmt::format("{:.6g}", invoker.get_value(obj));
    }
    if(type_name == "Int32" || type_name == "Int64" || type_name == "Int16" || type_name == "Byte" || type_name == "SByte")
    {
        auto invoker = dotnet::make_field_invoker<int32_t>(field);
        return fmt::format("{}", invoker.get_value(obj));
    }
    if(type_name == "Boolean")
    {
        auto invoker = dotnet::make_field_invoker<bool>(field);
        return invoker.get_value(obj) ? std::string("true") : std::string("false");
    }
    if(type_name == "String")
    {
        auto invoker = dotnet::make_field_invoker<std::string>(field);
        return make_json_string(invoker.get_value(obj));
    }
    if(type_name == "Vector2")
    {
        auto invoker = dotnet::make_field_invoker<math::vec2>(field);
        const auto v = invoker.get_value(obj);
        return fmt::format("[{:.6g},{:.6g}]", v.x, v.y);
    }
    if(type_name == "Vector3")
    {
        auto invoker = dotnet::make_field_invoker<math::vec3>(field);
        return vec3_to_json(invoker.get_value(obj));
    }
    if(type_name == "Vector4")
    {
        auto invoker = dotnet::make_field_invoker<math::vec4>(field);
        const auto v = invoker.get_value(obj);
        return fmt::format("[{:.6g},{:.6g},{:.6g},{:.6g}]", v.x, v.y, v.z, v.w);
    }
    if(type_name == "Color")
    {
        auto invoker = dotnet::make_field_invoker<math::color>(field);
        return color_to_json(invoker.get_value(obj));
    }
    return std::nullopt;
}

auto apply_script_field(dotnet::object& obj,
                        const dotnet::field& field,
                        const simdjson::dom::element& value,
                        std::string& error) -> bool
{
    const auto& field_type = field.get_type();
    const auto type_name = field_type.get_name();
    if(type_name == "Single" || type_name == "Double")
    {
        float v = 0.0f;
        if(!parse_number(value, v, error))
        {
            return false;
        }
        auto invoker = dotnet::make_field_invoker<float>(field);
        invoker.set_value(obj, v);
        return true;
    }
    if(type_name == "Int32" || type_name == "Int64" || type_name == "Int16" || type_name == "Byte" || type_name == "SByte")
    {
        int v = 0;
        if(!parse_int(value, v, error))
        {
            return false;
        }
        auto invoker = dotnet::make_field_invoker<int32_t>(field);
        invoker.set_value(obj, static_cast<int32_t>(v));
        return true;
    }
    if(type_name == "Boolean")
    {
        bool v = false;
        if(!parse_bool(value, v, error))
        {
            return false;
        }
        auto invoker = dotnet::make_field_invoker<bool>(field);
        invoker.set_value(obj, v);
        return true;
    }
    if(type_name == "String")
    {
        std::string v;
        if(!parse_string(value, v, error))
        {
            return false;
        }
        auto invoker = dotnet::make_field_invoker<std::string>(field);
        invoker.set_value(obj, v);
        return true;
    }
    if(type_name == "Vector2")
    {
        simdjson::dom::array arr;
        if(value.get(arr))
        {
            error = "Expected [x,y]";
            return false;
        }
        std::vector<float> vals;
        for(auto el : arr)
        {
            float f = 0.0f;
            if(!parse_number(el, f, error))
            {
                return false;
            }
            vals.push_back(f);
        }
        if(vals.size() != 2)
        {
            error = "Vector2 needs 2 components";
            return false;
        }
        auto invoker = dotnet::make_field_invoker<math::vec2>(field);
        invoker.set_value(obj, math::vec2{vals[0], vals[1]});
        return true;
    }
    if(type_name == "Vector3")
    {
        math::vec3 v{};
        if(!parse_vec3(value, v, error))
        {
            return false;
        }
        auto invoker = dotnet::make_field_invoker<math::vec3>(field);
        invoker.set_value(obj, v);
        return true;
    }
    if(type_name == "Vector4" || type_name == "Color")
    {
        math::color c{};
        if(!parse_color(value, c, error))
        {
            return false;
        }
        if(type_name == "Color")
        {
            auto invoker = dotnet::make_field_invoker<math::color>(field);
            invoker.set_value(obj, c);
        }
        else
        {
            const math::vec4 v = c;
            auto invoker = dotnet::make_field_invoker<math::vec4>(field);
            invoker.set_value(obj, v);
        }
        return true;
    }
    error = "Unsupported script field type: " + type_name;
    return false;
}

auto script_to_json(rtti::context& ctx,
                    script_component& comp,
                    const std::string& script_type,
                    const std::unordered_set<std::string>* filter,
                    std::string& error) -> std::string
{
    auto type = resolve_script_type(ctx, script_type, error);
    if(!type.valid())
    {
        return {};
    }
    auto script_obj = comp.get_script_component(type);
    if(!script_obj.pinned || !script_obj.pinned->get_object().valid())
    {
        error = "Script type not present on entity: " + script_type;
        return {};
    }
    auto obj = script_obj.pinned->get_object();
    std::string json = "{";
    bool first = true;
    append_prop(json, first, "script_type", make_json_string(type.get_fullname()));
    for(auto& field : type.get_fields(true))
    {
        if(field.get_visibility() != dotnet::visibility::vis_public || field.is_static() ||
           field.has_attribute("HideAttribute"))
        {
            continue;
        }
        const auto name = field.get_name();
        if(!wants_key(filter, name.c_str()))
        {
            continue;
        }
        auto value_json = script_field_to_json(obj, field);
        if(!value_json)
        {
            continue;
        }
        append_prop(json, first, name.c_str(), *value_json);
    }
    json += "}";
    return json;
}

auto apply_script_properties(rtti::context& ctx,
                             script_component& comp,
                             const std::string& script_type,
                             const simdjson::dom::object& properties,
                             component_apply_result& result) -> void
{
    std::string error;
    auto type = resolve_script_type(ctx, script_type, error);
    if(!type.valid())
    {
        result.ok = false;
        result.errors.push_back(error);
        return;
    }
    auto script_obj = comp.get_script_component(type);
    if(!script_obj.pinned || !script_obj.pinned->get_object().valid())
    {
        result.ok = false;
        result.errors.push_back("Script type not present on entity: " + script_type);
        return;
    }
    auto obj = script_obj.pinned->get_object();
    for(auto field_el : properties)
    {
        const std::string key(field_el.key);
        if(key == "script_type")
        {
            continue;
        }
        bool found = false;
        for(auto& candidate : type.get_fields(true))
        {
            if(candidate.get_name() != key)
            {
                continue;
            }
            found = true;
            if(candidate.get_visibility() != dotnet::visibility::vis_public || candidate.is_static())
            {
                result.ok = false;
                result.errors.push_back(key + ": field is not a public instance field");
                break;
            }
            std::string field_error;
            if(!apply_script_field(obj, candidate, field_el.value, field_error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + field_error);
                break;
            }
            result.applied.push_back(key);
            break;
        }
        if(!found)
        {
            result.unknown.push_back(key);
        }
    }
}

auto to_filter_set(const std::vector<std::string>* filter) -> std::unique_ptr<std::unordered_set<std::string>>
{
    if(!filter || filter->empty())
    {
        return nullptr;
    }
    return std::make_unique<std::unordered_set<std::string>>(filter->begin(), filter->end());
}

} // namespace

auto list_component_property_schema_json(const std::string& component_filter) -> std::string
{
    const bool all = component_filter.empty();
    std::string json = "[";
    bool first = true;
    auto add = [&](const char* component, const char* name, const char* type, const char* extra = nullptr)
    {
        if(!all && component_filter != component)
        {
            return;
        }
        if(!first)
        {
            json += ",";
        }
        first = false;
        json += "{";
        json += fmt::format(R"("component":{},"name":{},"type":{})",
                            make_json_string(component),
                            make_json_string(name),
                            make_json_string(type));
        if(extra && extra[0] != '\0')
        {
            json += ",";
            json += extra;
        }
        json += "}";
    };
    add("Light", "type", "string", R"("enum":["spot","point","directional"])");
    add("Light", "intensity", "number");
    add("Light", "color", "array", R"("items":"number","minItems":3,"maxItems":4)");
    add("Light", "casts_shadows", "boolean");
    add("Light", "range", "number");
    add("Light", "exponent_falloff", "number");
    add("Light", "outer_angle", "number");
    add("Light", "inner_angle", "number");
    add("Light", "shadow_bias", "number");
    add("Light", "shadow_normal_bias", "number");
    add("Light", "shadow_slope_bias", "number");
    add("Light", "shadow_near_plane", "number");
    add("Light", "shadow_far_plane", "number");
    add("Light", "shadow_resolution", "string", R"("enum":["low","medium","high","very_high"])");
    add("Light", "contact_shadow_enabled", "boolean");
    add("Light", "contact_shadow_ray_length", "number");
    add("Light", "contact_shadow_thickness", "number");
    add("Light", "contact_shadow_max_distance", "number");
    add("Light", "contact_shadow_opacity", "number");
    add("Light", "split_distribution", "number");
    add("Light", "num_splits", "integer");
    add("Light", "stabilize", "boolean");
    add("Skylight", "mode", "string", R"("enum":["perez","skybox"])");
    add("Skylight", "sky_brightness", "number");
    add("Skylight", "turbidity", "number");
    add("Skylight", "cloud_mode", "string", R"("enum":["none","flat","volumetric"])");
    add("Skylight", "cloud_coverage", "number");
    add("Skylight", "cloud_speed", "number");
    add("Skylight", "cloud_macro_variation", "number");
    add("Skylight", "cloud_base_altitude", "number");
    add("Skylight", "cloud_thickness", "number");
    add("Skylight", "cloud_size", "number");
    add("Skylight", "cloud_density", "number");
    add("Skylight", "cloud_shadow_strength", "number");
    add("Skylight", "cloud_world_space_altitude", "boolean");
    add("Skylight", "cloud_shadows", "boolean");
    add("Skylight", "cloud_shadow_opacity", "number");
    add("Skylight", "cloud_softness", "number");
    add("Skylight", "cloud_detail_erode", "number");
    add("Skylight", "cloud_wind_direction", "number");
    add("Skylight", "irradiance_intensity", "number");
    add("Skylight", "irradiance_tint", "array", R"("items":"number","minItems":3,"maxItems":4)");
    add("Skylight", "irradiance_quality", "string", R"("enum":["flat","directional"])");
    add("Skylight", "irradiance_use_sky", "boolean");
    add("Skylight", "cubemap", "string", R"("description":"Texture asset key/uid or null")");
    add("Audio Source", "auto_play", "boolean");
    add("Audio Source", "loop", "boolean");
    add("Audio Source", "volume", "number");
    add("Audio Source", "pitch", "number");
    add("Audio Source", "volume_rolloff", "number");
    add("Audio Source", "range_min", "number");
    add("Audio Source", "range_max", "number");
    add("Audio Source", "mute", "boolean");
    add("Audio Source", "clip", "string", R"("description":"Audio clip asset key/uid or null")");
    add("Camera", "fov", "number");
    add("Camera", "near_clip", "number");
    add("Camera", "far_clip", "number");
    add("Camera", "projection_mode", "string", R"("enum":["perspective","orthographic"])");
    add("Volume", "mode", "string", R"("enum":["local","global"])");
    add("Volume", "priority", "integer");
    add("Volume", "weight", "number");
    add("Volume", "blend_distance", "number");
    add("Volume", "extents", "array", R"("items":"number","minItems":3,"maxItems":3)");
    if(all || component_filter == "Script")
    {
        if(!first)
        {
            json += ",";
        }
        json += R"({"component":"Script","name":"script_type","type":"string","description":"Required C# type full name or short name"},)";
        json += R"({"component":"Script","name":"*","type":"any","description":"Public instance fields: number, boolean, string, Vector2/3/4, Color"})";
        first = false;
    }
    json += "]";
    return json;
}

auto is_supported_component_pretty_name(const std::string& component_pretty_name) -> bool
{
    return component_pretty_name == "Light" || component_pretty_name == "Skylight" ||
           component_pretty_name == "Audio Source" || component_pretty_name == "Camera" ||
           component_pretty_name == "Volume" || component_pretty_name == "Script";
}

auto component_properties_to_json(rtti::context& ctx,
                                  entt::handle entity,
                                  const std::string& component_pretty_name,
                                  const std::string& script_type,
                                  const std::vector<std::string>* property_filter,
                                  std::string& error) -> std::string
{
    error.clear();
    if(!entity)
    {
        error = "Invalid entity";
        return {};
    }
    auto filter = to_filter_set(property_filter);
    const auto* filter_ptr = filter.get();
    if(component_pretty_name == "Light")
    {
        auto* comp = entity.try_get<light_component>();
        if(!comp)
        {
            error = "Component not present on entity: Light";
            return {};
        }
        return light_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Skylight")
    {
        auto* comp = entity.try_get<skylight_component>();
        if(!comp)
        {
            error = "Component not present on entity: Skylight";
            return {};
        }
        return skylight_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Audio Source")
    {
        auto* comp = entity.try_get<audio_source_component>();
        if(!comp)
        {
            error = "Component not present on entity: Audio Source";
            return {};
        }
        return audio_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Camera")
    {
        auto* comp = entity.try_get<camera_component>();
        if(!comp)
        {
            error = "Component not present on entity: Camera";
            return {};
        }
        return camera_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Volume")
    {
        auto* comp = entity.try_get<volume_component>();
        if(!comp)
        {
            error = "Component not present on entity: Volume";
            return {};
        }
        return volume_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Script")
    {
        auto* comp = entity.try_get<script_component>();
        if(!comp)
        {
            error = "Component not present on entity: Script";
            return {};
        }
        return script_to_json(ctx, *comp, script_type, filter_ptr, error);
    }
    error = "Unsupported component for typed properties: " + component_pretty_name;
    return {};
}

auto apply_component_properties(rtti::context& ctx,
                                entt::handle entity,
                                const std::string& component_pretty_name,
                                const std::string& script_type,
                                const simdjson::dom::object& properties) -> component_apply_result
{
    component_apply_result result;
    if(!entity)
    {
        result.ok = false;
        result.errors.push_back("Invalid entity");
        return result;
    }
    if(component_pretty_name == "Light")
    {
        auto* comp = entity.try_get<light_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Light");
            return result;
        }
        apply_light_properties(*comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Skylight")
    {
        auto* comp = entity.try_get<skylight_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Skylight");
            return result;
        }
        apply_skylight_properties(ctx, *comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Audio Source")
    {
        auto* comp = entity.try_get<audio_source_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Audio Source");
            return result;
        }
        apply_audio_properties(ctx, *comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Camera")
    {
        auto* comp = entity.try_get<camera_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Camera");
            return result;
        }
        apply_camera_properties(*comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Volume")
    {
        auto* comp = entity.try_get<volume_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Volume");
            return result;
        }
        apply_volume_properties(*comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Script")
    {
        auto* comp = entity.try_get<script_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Script");
            return result;
        }
        apply_script_properties(ctx, *comp, script_type, properties, result);
        return result;
    }
    result.ok = false;
    result.errors.push_back("Unsupported component for typed properties: " + component_pretty_name);
    return result;
}

auto entity_supported_component_properties_json(rtti::context& ctx,
                                                 entt::handle entity,
                                                 const std::vector<std::string>* component_filter) -> std::string
{
    std::string json = "{";
    bool first = true;
    auto try_add = [&](const char* pretty, const std::string& script_type = {})
    {
        if(component_filter && !component_filter->empty())
        {
            bool wanted = false;
            for(const auto& name : *component_filter)
            {
                if(name == pretty)
                {
                    wanted = true;
                    break;
                }
            }
            if(!wanted)
            {
                return;
            }
        }
        std::string error;
        auto props = component_properties_to_json(ctx, entity, pretty, script_type, nullptr, error);
        if(props.empty())
        {
            return;
        }
        if(!first)
        {
            json += ",";
        }
        first = false;
        json += make_json_string(pretty);
        json += ":";
        json += props;
    };
    try_add("Light");
    try_add("Skylight");
    try_add("Audio Source");
    try_add("Camera");
    try_add("Volume");
    // For Script, emit one bag per attached scriptable type.
    if(auto* script = entity.try_get<script_component>())
    {
        bool want_script = !component_filter || component_filter->empty();
        if(component_filter)
        {
            for(const auto& name : *component_filter)
            {
                if(name == "Script")
                {
                    want_script = true;
                    break;
                }
            }
        }
        if(want_script)
        {
            for(const auto& so : script->get_script_components())
            {
                if(!so.pinned)
                {
                    continue;
                }
                auto obj = so.pinned->get_object();
                if(!obj.valid())
                {
                    continue;
                }
                const auto type_name = obj.get_type().get_fullname();
                std::string error;
                auto props = component_properties_to_json(ctx, entity, "Script", type_name, nullptr, error);
                if(props.empty())
                {
                    continue;
                }
                if(!first)
                {
                    json += ",";
                }
                first = false;
                json += make_json_string(std::string("Script:") + type_name);
                json += ":";
                json += props;
            }
        }
    }
    json += "}";
    return json;
}

auto component_apply_result_to_json(const component_apply_result& result) -> std::string
{
    return apply_result_to_json(result);
}

} // namespace unravel::mcp
