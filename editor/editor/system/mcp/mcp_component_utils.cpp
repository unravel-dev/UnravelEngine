#include "mcp_component_utils.h"

#include "mcp_tools_common.h"

#include <engine/animation/animation.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_clip.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/rendering/ecs/components/bloom_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/ecs/components/text_component.h>
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
    if(wants_key(filter, "cloud_brightness"))
    {
        append_prop(json, first, "cloud_brightness", fmt::format("{:.6g}", comp.get_cloud_brightness()));
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
        else if(key == "cloud_brightness")
        {
            float v = 0.0f;
            if(!parse_number(value, v, error))
            {
                result.ok = false;
                result.errors.push_back(key + ": " + error);
                continue;
            }
            comp.set_cloud_brightness(v);
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

auto resolve_animation_clip(rtti::context& ctx, const std::string& key_or_uid, std::string& error)
    -> asset_handle<animation_clip>
{
    auto& am = ctx.get_cached<asset_manager>();
    if(key_or_uid.empty())
    {
        return {};
    }
    auto as_uuid = hpp::uuid::from_string(key_or_uid);
    if(as_uuid)
    {
        auto handle = am.get_asset<animation_clip>(*as_uuid);
        if(!handle)
        {
            error = "Animation clip not found for uid: " + key_or_uid;
        }
        return handle;
    }
    auto handle = am.get_asset<animation_clip>(key_or_uid);
    if(!handle)
    {
        error = "Animation clip not found: " + key_or_uid;
    }
    return handle;
}

template<typename Enum>
auto enum_from_string(std::string_view value, const std::initializer_list<std::pair<const char*, Enum>>& table, Enum& out)
    -> bool
{
    for(const auto& entry : table)
    {
        if(value == entry.first)
        {
            out = entry.second;
            return true;
        }
    }
    return false;
}

template<typename Enum>
auto enum_to_string(Enum value, const std::initializer_list<std::pair<const char*, Enum>>& table) -> const char*
{
    for(const auto& entry : table)
    {
        if(value == entry.second)
        {
            return entry.first;
        }
    }
    return "unknown";
}

#define MCP_EMITTER_SHAPE_TABLE                                                                                        \
    {                                                                                                                  \
        {"sphere", ps_soa::emitter_shape::sphere}, {"hemisphere", ps_soa::emitter_shape::hemisphere},                  \
        {"circle", ps_soa::emitter_shape::circle}, {"box", ps_soa::emitter_shape::box},                                \
        {"rect", ps_soa::emitter_shape::rect}                                                                          \
    }
#define MCP_EMITTER_DIRECTION_TABLE                                                                                    \
    {                                                                                                                  \
        {"up", ps_soa::emitter_direction::up}, {"outward", ps_soa::emitter_direction::outward},                        \
        {"inward", ps_soa::emitter_direction::inward}                                                                  \
    }
#define MCP_SPAWN_LOCATION_TABLE                                                                                       \
    {                                                                                                                  \
        {"inside", ps_soa::spawn_location::inside}, {"surface", ps_soa::spawn_location::surface}                       \
    }
#define MCP_SIMULATION_SPACE_TABLE                                                                                     \
    {                                                                                                                  \
        {"world", ps_soa::simulation_space::world}, {"local", ps_soa::simulation_space::local}                         \
    }
#define MCP_TEXTURE_MODE_TABLE                                                                                         \
    {                                                                                                                  \
        {"multi_channel", ps_soa::texture_mode::multi_channel}, {"mask", ps_soa::texture_mode::mask}                   \
    }
#define MCP_RENDER_MODE_TABLE                                                                                          \
    {                                                                                                                  \
        {"billboard", ps_soa::render_mode::billboard},                                                                 \
        {"horizontal_billboard", ps_soa::render_mode::horizontal_billboard},                                           \
        {"vertical_billboard", ps_soa::render_mode::vertical_billboard}                                                \
    }
#define MCP_BLEND_MODE_TABLE                                                                                           \
    {                                                                                                                  \
        {"normal", ps_soa::blend_mode::normal}, {"additive", ps_soa::blend_mode::additive},                            \
        {"multiply", ps_soa::blend_mode::multiply}                                                                     \
    }
#define MCP_BODY_TYPE_TABLE                                                                                            \
    {                                                                                                                  \
        {"static", rigidbody_type::static_body}, {"kinematic", rigidbody_type::kinematic},                             \
        {"dynamic", rigidbody_type::dynamic}                                                                           \
    }
#define MCP_PROBE_TYPE_TABLE                                                                                           \
    {                                                                                                                  \
        {"box", probe_type::box}, {"sphere", probe_type::sphere}                                                       \
    }
#define MCP_PROBE_UPDATE_MODE_TABLE                                                                                    \
    {                                                                                                                  \
        {"on_demand", probe_update_mode::on_demand}, {"once", probe_update_mode::once},                                \
        {"realtime", probe_update_mode::realtime}                                                                      \
    }
#define MCP_PROBE_RESOLUTION_TABLE                                                                                     \
    {                                                                                                                  \
        {"16", probe_resolution::res_16}, {"32", probe_resolution::res_32}, {"64", probe_resolution::res_64},          \
        {"128", probe_resolution::res_128}, {"256", probe_resolution::res_256}, {"512", probe_resolution::res_512},    \
        {"1024", probe_resolution::res_1024}                                                                           \
    }

auto alignment_to_string(const alignment& align) -> std::string
{
    std::string vertical = "top";
    if((align.flags & align::middle) != 0)
    {
        vertical = "middle";
    }
    else if((align.flags & align::bottom) != 0)
    {
        vertical = "bottom";
    }
    std::string horizontal = "left";
    if((align.flags & align::center) != 0)
    {
        horizontal = "center";
    }
    else if((align.flags & align::right) != 0)
    {
        horizontal = "right";
    }
    return vertical + "_" + horizontal;
}

auto alignment_from_string(std::string_view value, alignment& out) -> bool
{
    const auto sep = value.find('_');
    if(sep == std::string_view::npos)
    {
        return false;
    }
    const auto vertical = value.substr(0, sep);
    const auto horizontal = value.substr(sep + 1);
    uint32_t flags = 0;
    if(vertical == "top")
    {
        flags |= align::top;
    }
    else if(vertical == "middle")
    {
        flags |= align::middle;
    }
    else if(vertical == "bottom")
    {
        flags |= align::bottom;
    }
    else
    {
        return false;
    }
    if(horizontal == "left")
    {
        flags |= align::left;
    }
    else if(horizontal == "center")
    {
        flags |= align::center;
    }
    else if(horizontal == "right")
    {
        flags |= align::right;
    }
    else
    {
        return false;
    }
    out.flags = flags;
    return true;
}

auto color_gradient_to_json(const math::gradient<math::color>& gradient) -> std::string
{
    std::string json = "[";
    bool first = true;
    for(const auto& point : gradient.get_points())
    {
        if(!first)
        {
            json += ",";
        }
        first = false;
        json += fmt::format(R"({{"progress":{:.6g},"color":{}}})", point.progress, color_to_json(point.element));
    }
    json += "]";
    return json;
}

auto range_gradient_to_json(const math::gradient<frange_t>& gradient) -> std::string
{
    std::string json = "[";
    bool first = true;
    for(const auto& point : gradient.get_points())
    {
        if(!first)
        {
            json += ",";
        }
        first = false;
        json += fmt::format(R"({{"progress":{:.6g},"min":{:.6g},"max":{:.6g}}})",
                            point.progress,
                            point.element.min,
                            point.element.max);
    }
    json += "]";
    return json;
}

auto parse_color_gradient(const simdjson::dom::element& value, math::gradient<math::color>& out, std::string& error)
    -> bool
{
    simdjson::dom::array arr;
    if(value.get_array().get(arr) != simdjson::SUCCESS)
    {
        error = "expected array of {progress,color} points";
        return false;
    }
    math::gradient<math::color>::points_t points;
    for(auto el : arr)
    {
        simdjson::dom::object obj;
        if(el.get_object().get(obj) != simdjson::SUCCESS)
        {
            error = "expected object point";
            return false;
        }
        math::gradient_point<math::color> point;
        double progress{};
        if(obj["progress"].get_double().get(progress) != simdjson::SUCCESS)
        {
            error = "point missing numeric progress";
            return false;
        }
        point.progress = static_cast<float>(progress);
        simdjson::dom::element color_el;
        if(obj["color"].get(color_el) != simdjson::SUCCESS || !parse_color(color_el, point.element, error))
        {
            error = error.empty() ? "point missing color" : error;
            return false;
        }
        points.push_back(point);
    }
    out.set_points(points);
    return true;
}

auto parse_range_gradient(const simdjson::dom::element& value, math::gradient<frange_t>& out, std::string& error)
    -> bool
{
    simdjson::dom::array arr;
    if(value.get_array().get(arr) != simdjson::SUCCESS)
    {
        error = "expected array of {progress,min,max} points";
        return false;
    }
    math::gradient<frange_t>::points_t points;
    for(auto el : arr)
    {
        simdjson::dom::object obj;
        if(el.get_object().get(obj) != simdjson::SUCCESS)
        {
            error = "expected object point";
            return false;
        }
        math::gradient_point<frange_t> point;
        double progress{};
        double minv{};
        double maxv{};
        if(obj["progress"].get_double().get(progress) != simdjson::SUCCESS ||
           obj["min"].get_double().get(minv) != simdjson::SUCCESS ||
           obj["max"].get_double().get(maxv) != simdjson::SUCCESS)
        {
            error = "point needs numeric progress/min/max";
            return false;
        }
        point.progress = static_cast<float>(progress);
        point.element.min = static_cast<float>(minv);
        point.element.max = static_cast<float>(maxv);
        points.push_back(point);
    }
    out.set_points(points);
    return true;
}

auto particle_to_json(const particle_emitter_component& comp, const std::unordered_set<std::string>* filter)
    -> std::string
{
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "enabled"))
    {
        append_prop(json, first, "enabled", comp.is_enabled() ? "true" : "false");
    }
    if(wants_key(filter, "shape"))
    {
        append_prop(json, first, "shape", make_json_string(enum_to_string(comp.get_shape(), MCP_EMITTER_SHAPE_TABLE)));
    }
    if(wants_key(filter, "direction"))
    {
        append_prop(json,
                    first,
                    "direction",
                    make_json_string(enum_to_string(comp.get_direction(), MCP_EMITTER_DIRECTION_TABLE)));
    }
    if(wants_key(filter, "spawn_location"))
    {
        append_prop(json,
                    first,
                    "spawn_location",
                    make_json_string(enum_to_string(comp.get_spawn_location(), MCP_SPAWN_LOCATION_TABLE)));
    }
    if(wants_key(filter, "simulation_space"))
    {
        append_prop(json,
                    first,
                    "simulation_space",
                    make_json_string(enum_to_string(comp.get_simulation_space(), MCP_SIMULATION_SPACE_TABLE)));
    }
    if(wants_key(filter, "max_particles"))
    {
        append_prop(json, first, "max_particles", fmt::format("{}", comp.get_max_particles()));
    }
    if(wants_key(filter, "emission_rate"))
    {
        append_prop(json, first, "emission_rate", fmt::format("{:.6g}", comp.get_emission_rate()));
    }
    if(wants_key(filter, "lifetime"))
    {
        append_prop(json, first, "lifetime", fmt::format("{:.6g}", comp.get_lifetime().count()));
    }
    if(wants_key(filter, "emission_lifetime"))
    {
        append_prop(json, first, "emission_lifetime", fmt::format("{:.6g}", comp.get_emission_lifetime().count()));
    }
    if(wants_key(filter, "start_delay"))
    {
        append_prop(json, first, "start_delay", fmt::format("{:.6g}", comp.get_start_delay().count()));
    }
    if(wants_key(filter, "gravity_scale"))
    {
        append_prop(json, first, "gravity_scale", fmt::format("{:.6g}", comp.get_gravity_scale()));
    }
    if(wants_key(filter, "velocity_damping"))
    {
        append_prop(json, first, "velocity_damping", fmt::format("{:.6g}", comp.get_velocity_damping()));
    }
    if(wants_key(filter, "force_over_lifetime"))
    {
        append_prop(json, first, "force_over_lifetime", vec3_to_json(comp.get_force_over_lifetime()));
    }
    if(wants_key(filter, "emission_shape_position"))
    {
        append_prop(json, first, "emission_shape_position", vec3_to_json(comp.get_emission_shape_position()));
    }
    if(wants_key(filter, "emission_shape_scale"))
    {
        append_prop(json, first, "emission_shape_scale", vec3_to_json(comp.get_emission_shape_scale()));
    }
    if(wants_key(filter, "initial_scale_3d"))
    {
        append_prop(json, first, "initial_scale_3d", vec3_to_json(comp.get_initial_scale_3d()));
    }
    if(wants_key(filter, "opacity"))
    {
        append_prop(json, first, "opacity", fmt::format("{:.6g}", comp.get_opacity()));
    }
    if(wants_key(filter, "color_intensity"))
    {
        append_prop(json, first, "color_intensity", fmt::format("{:.6g}", comp.get_color_intensity()));
    }
    if(wants_key(filter, "color_gradient"))
    {
        append_prop(json, first, "color_gradient", color_gradient_to_json(comp.get_color_gradient()));
    }
    if(wants_key(filter, "velocity_gradient"))
    {
        append_prop(json, first, "velocity_gradient", range_gradient_to_json(comp.get_velocity_gradient()));
    }
    if(wants_key(filter, "scale_gradient"))
    {
        append_prop(json, first, "scale_gradient", range_gradient_to_json(comp.get_scale_gradient()));
    }
    if(wants_key(filter, "texture"))
    {
        append_prop(json, first, "texture", asset_handle_key_json(comp.get_texture()));
    }
    if(wants_key(filter, "texture_mode"))
    {
        append_prop(json,
                    first,
                    "texture_mode",
                    make_json_string(enum_to_string(comp.get_texture_mode(), MCP_TEXTURE_MODE_TABLE)));
    }
    if(wants_key(filter, "render_mode"))
    {
        append_prop(json,
                    first,
                    "render_mode",
                    make_json_string(enum_to_string(comp.get_render_mode(), MCP_RENDER_MODE_TABLE)));
    }
    if(wants_key(filter, "blend_mode"))
    {
        append_prop(json,
                    first,
                    "blend_mode",
                    make_json_string(enum_to_string(comp.get_blend_mode(), MCP_BLEND_MODE_TABLE)));
    }
    if(wants_key(filter, "texture_sheet_tiles"))
    {
        append_prop(json, first, "texture_sheet_tiles", vec2_to_json(comp.get_texture_sheet_tiles()));
    }
    if(wants_key(filter, "texture_sheet_cycles"))
    {
        append_prop(json, first, "texture_sheet_cycles", fmt::format("{:.6g}", comp.get_texture_sheet_cycles()));
    }
    if(wants_key(filter, "texture_sheet_randomize"))
    {
        append_prop(json, first, "texture_sheet_randomize", comp.get_texture_sheet_randomize() ? "true" : "false");
    }
    if(wants_key(filter, "loop"))
    {
        append_prop(json, first, "loop", comp.is_loop() ? "true" : "false");
    }
    if(wants_key(filter, "align_to_direction"))
    {
        append_prop(json, first, "align_to_direction", comp.get_align_to_direction() ? "true" : "false");
    }
    if(wants_key(filter, "pivot"))
    {
        append_prop(json, first, "pivot", vec2_to_json(comp.get_pivot()));
    }
    json += "}";
    return json;
}

auto apply_particle_properties(rtti::context& ctx,
                               particle_emitter_component& comp,
                               const simdjson::dom::object& properties,
                               component_apply_result& result) -> void
{
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        auto fail = [&](const std::string& message)
        {
            result.ok = false;
            result.errors.push_back(key + ": " + message);
        };
        auto applied = [&]()
        {
            result.applied.push_back(key);
        };
        if(key == "enabled")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_enabled(v), applied()) : fail(error);
        }
        else if(key == "shape")
        {
            std::string s;
            ps_soa::emitter_shape v{};
            (parse_string(value, s, error) && enum_from_string<ps_soa::emitter_shape>(s, MCP_EMITTER_SHAPE_TABLE, v))
                ? (comp.set_shape(v), applied())
                : fail("expected sphere|hemisphere|circle|box|rect");
        }
        else if(key == "direction")
        {
            std::string s;
            ps_soa::emitter_direction v{};
            (parse_string(value, s, error) &&
             enum_from_string<ps_soa::emitter_direction>(s, MCP_EMITTER_DIRECTION_TABLE, v))
                ? (comp.set_direction(v), applied())
                : fail("expected up|outward|inward");
        }
        else if(key == "spawn_location")
        {
            std::string s;
            ps_soa::spawn_location v{};
            (parse_string(value, s, error) && enum_from_string<ps_soa::spawn_location>(s, MCP_SPAWN_LOCATION_TABLE, v))
                ? (comp.set_spawn_location(v), applied())
                : fail("expected inside|surface");
        }
        else if(key == "simulation_space")
        {
            std::string s;
            ps_soa::simulation_space v{};
            (parse_string(value, s, error) &&
             enum_from_string<ps_soa::simulation_space>(s, MCP_SIMULATION_SPACE_TABLE, v))
                ? (comp.set_simulation_space(v), applied())
                : fail("expected world|local");
        }
        else if(key == "max_particles")
        {
            int v{};
            parse_int(value, v, error) ? (comp.set_max_particles(static_cast<uint32_t>(std::max(v, 1))), applied())
                                       : fail(error);
        }
        else if(key == "emission_rate")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_emission_rate(v), applied()) : fail(error);
        }
        else if(key == "lifetime")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_lifetime(std::chrono::duration<float>(v)), applied())
                                          : fail(error);
        }
        else if(key == "emission_lifetime")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_emission_lifetime(std::chrono::duration<float>(v)), applied())
                                          : fail(error);
        }
        else if(key == "start_delay")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_start_delay(std::chrono::duration<float>(v)), applied())
                                          : fail(error);
        }
        else if(key == "gravity_scale")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_gravity_scale(v), applied()) : fail(error);
        }
        else if(key == "velocity_damping")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_velocity_damping(v), applied()) : fail(error);
        }
        else if(key == "force_over_lifetime")
        {
            math::vec3 v{};
            parse_vec3(value, v, error) ? (comp.set_force_over_lifetime(v), applied()) : fail(error);
        }
        else if(key == "emission_shape_position")
        {
            math::vec3 v{};
            parse_vec3(value, v, error) ? (comp.set_emission_shape_position(v), applied()) : fail(error);
        }
        else if(key == "emission_shape_scale")
        {
            math::vec3 v{};
            parse_vec3(value, v, error) ? (comp.set_emission_shape_scale(v), applied()) : fail(error);
        }
        else if(key == "initial_scale_3d")
        {
            math::vec3 v{};
            parse_vec3(value, v, error) ? (comp.set_initial_scale_3d(v), applied()) : fail(error);
        }
        else if(key == "opacity")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_opacity(v), applied()) : fail(error);
        }
        else if(key == "color_intensity")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_color_intensity(v), applied()) : fail(error);
        }
        else if(key == "color_gradient")
        {
            math::gradient<math::color> gradient;
            parse_color_gradient(value, gradient, error) ? (comp.set_color_gradient(gradient), applied())
                                                         : fail(error);
        }
        else if(key == "velocity_gradient")
        {
            math::gradient<frange_t> gradient;
            parse_range_gradient(value, gradient, error) ? (comp.set_velocity_gradient(gradient), applied())
                                                         : fail(error);
        }
        else if(key == "scale_gradient")
        {
            math::gradient<frange_t> gradient;
            parse_range_gradient(value, gradient, error) ? (comp.set_scale_gradient(gradient), applied())
                                                         : fail(error);
        }
        else if(key == "texture")
        {
            std::string s;
            if(!parse_string(value, s, error))
            {
                fail(error);
                continue;
            }
            auto handle = resolve_texture(ctx, s, error);
            (!s.empty() && !handle) ? fail(error) : (comp.set_texture(handle), applied());
        }
        else if(key == "texture_mode")
        {
            std::string s;
            ps_soa::texture_mode v{};
            (parse_string(value, s, error) && enum_from_string<ps_soa::texture_mode>(s, MCP_TEXTURE_MODE_TABLE, v))
                ? (comp.set_texture_mode(v), applied())
                : fail("expected multi_channel|mask");
        }
        else if(key == "render_mode")
        {
            std::string s;
            ps_soa::render_mode v{};
            (parse_string(value, s, error) && enum_from_string<ps_soa::render_mode>(s, MCP_RENDER_MODE_TABLE, v))
                ? (comp.set_render_mode(v), applied())
                : fail("expected billboard|horizontal_billboard|vertical_billboard");
        }
        else if(key == "blend_mode")
        {
            std::string s;
            ps_soa::blend_mode v{};
            (parse_string(value, s, error) && enum_from_string<ps_soa::blend_mode>(s, MCP_BLEND_MODE_TABLE, v))
                ? (comp.set_blend_mode(v), applied())
                : fail("expected normal|additive|multiply");
        }
        else if(key == "texture_sheet_tiles")
        {
            math::vec2 v{};
            parse_vec2(value, v, error) ? (comp.set_texture_sheet_tiles(v), applied()) : fail(error);
        }
        else if(key == "texture_sheet_cycles")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_texture_sheet_cycles(v), applied()) : fail(error);
        }
        else if(key == "texture_sheet_randomize")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_texture_sheet_randomize(v), applied()) : fail(error);
        }
        else if(key == "loop")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_loop(v), applied()) : fail(error);
        }
        else if(key == "align_to_direction")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_align_to_direction(v), applied()) : fail(error);
        }
        else if(key == "pivot")
        {
            math::vec2 v{};
            parse_vec2(value, v, error) ? (comp.set_pivot(v), applied()) : fail(error);
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
    comp.stop_and_reset();
    comp.play();
}

auto physics_shapes_to_json(const physics_component& comp) -> std::string
{
    std::string json = "[";
    bool first = true;
    for(const auto& compound : comp.get_shapes())
    {
        if(!first)
        {
            json += ",";
        }
        first = false;
        hpp::visit(
            [&](const auto& shape)
            {
                using shape_t = std::decay_t<decltype(shape)>;
                if constexpr(std::is_same_v<shape_t, physics_box_shape>)
                {
                    json += fmt::format(R"({{"type":"box","center":{},"extents":{}}})",
                                        vec3_to_json(shape.center),
                                        vec3_to_json(shape.extends));
                }
                else if constexpr(std::is_same_v<shape_t, physics_sphere_shape>)
                {
                    json += fmt::format(R"({{"type":"sphere","center":{},"radius":{:.6g}}})",
                                        vec3_to_json(shape.center),
                                        shape.radius);
                }
                else if constexpr(std::is_same_v<shape_t, physics_capsule_shape>)
                {
                    json += fmt::format(R"({{"type":"capsule","center":{},"radius":{:.6g},"length":{:.6g}}})",
                                        vec3_to_json(shape.center),
                                        shape.radius,
                                        shape.length);
                }
                else if constexpr(std::is_same_v<shape_t, physics_cylinder_shape>)
                {
                    json += fmt::format(R"({{"type":"cylinder","center":{},"radius":{:.6g},"length":{:.6g}}})",
                                        vec3_to_json(shape.center),
                                        shape.radius,
                                        shape.length);
                }
                else
                {
                    json += fmt::format(R"({{"type":"mesh","center":{}}})", vec3_to_json(shape.center));
                }
            },
            compound.shape);
    }
    json += "]";
    return json;
}

auto parse_physics_shapes(const simdjson::dom::element& value,
                          std::vector<physics_compound_shape>& out,
                          std::string& error) -> bool
{
    simdjson::dom::array arr;
    if(value.get_array().get(arr) != simdjson::SUCCESS)
    {
        error = "expected array of shape objects";
        return false;
    }
    for(auto el : arr)
    {
        simdjson::dom::object obj;
        if(el.get_object().get(obj) != simdjson::SUCCESS)
        {
            error = "expected shape object";
            return false;
        }
        std::string_view type;
        if(obj["type"].get_string().get(type) != simdjson::SUCCESS)
        {
            error = "shape missing string type (box|sphere|capsule|cylinder)";
            return false;
        }
        math::vec3 center{0.0f, 0.0f, 0.0f};
        simdjson::dom::element center_el;
        if(obj["center"].get(center_el) == simdjson::SUCCESS && !parse_vec3(center_el, center, error))
        {
            return false;
        }
        auto read_float = [&](const char* name, float& out_value, float fallback) -> bool
        {
            out_value = fallback;
            simdjson::dom::element field_el;
            if(obj[name].get(field_el) != simdjson::SUCCESS)
            {
                return true;
            }
            return parse_number(field_el, out_value, error);
        };
        physics_compound_shape compound;
        if(type == "box")
        {
            physics_box_shape shape;
            shape.center = center;
            simdjson::dom::element extents_el;
            if(obj["extents"].get(extents_el) == simdjson::SUCCESS && !parse_vec3(extents_el, shape.extends, error))
            {
                return false;
            }
            compound.shape = shape;
        }
        else if(type == "sphere")
        {
            physics_sphere_shape shape;
            shape.center = center;
            if(!read_float("radius", shape.radius, 0.5f))
            {
                return false;
            }
            compound.shape = shape;
        }
        else if(type == "capsule")
        {
            physics_capsule_shape shape;
            shape.center = center;
            if(!read_float("radius", shape.radius, 0.5f) || !read_float("length", shape.length, 1.0f))
            {
                return false;
            }
            compound.shape = shape;
        }
        else if(type == "cylinder")
        {
            physics_cylinder_shape shape;
            shape.center = center;
            if(!read_float("radius", shape.radius, 0.5f) || !read_float("length", shape.length, 1.0f))
            {
                return false;
            }
            compound.shape = shape;
        }
        else
        {
            error = "unsupported shape type: " + std::string(type);
            return false;
        }
        out.push_back(compound);
    }
    return true;
}

auto bvec3_to_json(const math::bvec3& v) -> std::string
{
    return fmt::format("[{},{},{}]", v.x ? "true" : "false", v.y ? "true" : "false", v.z ? "true" : "false");
}

auto parse_bvec3(const simdjson::dom::element& value, math::bvec3& out, std::string& error) -> bool
{
    simdjson::dom::array arr;
    if(value.get_array().get(arr) != simdjson::SUCCESS || arr.size() != 3)
    {
        error = "expected [bool,bool,bool]";
        return false;
    }
    size_t idx = 0;
    for(auto el : arr)
    {
        bool v{};
        if(el.get_bool().get(v) != simdjson::SUCCESS)
        {
            error = "expected [bool,bool,bool]";
            return false;
        }
        (idx == 0 ? out.x : idx == 1 ? out.y : out.z) = v;
        ++idx;
    }
    return true;
}

auto physics_to_json(const physics_component& comp, const std::unordered_set<std::string>* filter) -> std::string
{
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "body_type"))
    {
        append_prop(json,
                    first,
                    "body_type",
                    make_json_string(enum_to_string(comp.get_body_type(), MCP_BODY_TYPE_TABLE)));
    }
    if(wants_key(filter, "is_using_gravity"))
    {
        append_prop(json, first, "is_using_gravity", comp.is_using_gravity() ? "true" : "false");
    }
    if(wants_key(filter, "is_autoscaled"))
    {
        append_prop(json, first, "is_autoscaled", comp.is_autoscaled() ? "true" : "false");
    }
    if(wants_key(filter, "is_sensor"))
    {
        append_prop(json, first, "is_sensor", comp.is_sensor() ? "true" : "false");
    }
    if(wants_key(filter, "mass"))
    {
        append_prop(json, first, "mass", fmt::format("{:.6g}", comp.get_mass()));
    }
    if(wants_key(filter, "freeze_position"))
    {
        append_prop(json, first, "freeze_position", bvec3_to_json(comp.get_freeze_position()));
    }
    if(wants_key(filter, "freeze_rotation"))
    {
        append_prop(json, first, "freeze_rotation", bvec3_to_json(comp.get_freeze_rotation()));
    }
    if(wants_key(filter, "shapes"))
    {
        append_prop(json, first, "shapes", physics_shapes_to_json(comp));
    }
    json += "}";
    return json;
}

auto apply_physics_properties(physics_component& comp,
                              const simdjson::dom::object& properties,
                              component_apply_result& result) -> void
{
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        auto fail = [&](const std::string& message)
        {
            result.ok = false;
            result.errors.push_back(key + ": " + message);
        };
        auto applied = [&]()
        {
            result.applied.push_back(key);
        };
        if(key == "body_type")
        {
            std::string s;
            rigidbody_type v{};
            (parse_string(value, s, error) && enum_from_string<rigidbody_type>(s, MCP_BODY_TYPE_TABLE, v))
                ? (comp.set_body_type(v), applied())
                : fail("expected static|kinematic|dynamic");
        }
        else if(key == "is_using_gravity")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_is_using_gravity(v), applied()) : fail(error);
        }
        else if(key == "is_autoscaled")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_is_autoscaled(v), applied()) : fail(error);
        }
        else if(key == "is_sensor")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_is_sensor(v), applied()) : fail(error);
        }
        else if(key == "mass")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_mass(v), applied()) : fail(error);
        }
        else if(key == "freeze_position")
        {
            math::bvec3 v{};
            parse_bvec3(value, v, error) ? (comp.set_freeze_position(v), applied()) : fail(error);
        }
        else if(key == "freeze_rotation")
        {
            math::bvec3 v{};
            parse_bvec3(value, v, error) ? (comp.set_freeze_rotation(v), applied()) : fail(error);
        }
        else if(key == "shapes")
        {
            std::vector<physics_compound_shape> shapes;
            parse_physics_shapes(value, shapes, error) ? (comp.set_shapes(shapes), applied()) : fail(error);
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
}

auto animation_to_json(const animation_component& comp, const std::unordered_set<std::string>* filter) -> std::string
{
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "animation"))
    {
        append_prop(json, first, "animation", asset_handle_key_json(comp.get_animation()));
    }
    if(wants_key(filter, "auto_play"))
    {
        append_prop(json, first, "auto_play", comp.get_autoplay() ? "true" : "false");
    }
    if(wants_key(filter, "apply_root_motion"))
    {
        append_prop(json, first, "apply_root_motion", comp.get_apply_root_motion() ? "true" : "false");
    }
    if(wants_key(filter, "speed"))
    {
        append_prop(json, first, "speed", fmt::format("{:.6g}", comp.get_speed()));
    }
    json += "}";
    return json;
}

auto apply_animation_properties(rtti::context& ctx,
                                animation_component& comp,
                                const simdjson::dom::object& properties,
                                component_apply_result& result) -> void
{
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        auto fail = [&](const std::string& message)
        {
            result.ok = false;
            result.errors.push_back(key + ": " + message);
        };
        auto applied = [&]()
        {
            result.applied.push_back(key);
        };
        if(key == "animation")
        {
            std::string s;
            if(!parse_string(value, s, error))
            {
                fail(error);
                continue;
            }
            auto handle = resolve_animation_clip(ctx, s, error);
            (!s.empty() && !handle) ? fail(error) : (comp.set_animation(handle), applied());
        }
        else if(key == "auto_play")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_autoplay(v), applied()) : fail(error);
        }
        else if(key == "apply_root_motion")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_apply_root_motion(v), applied()) : fail(error);
        }
        else if(key == "speed")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_speed(v), applied()) : fail(error);
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
}

auto text_to_json(const text_component& comp, const std::unordered_set<std::string>* filter) -> std::string
{
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "text"))
    {
        append_prop(json, first, "text", make_json_string(comp.get_text()));
    }
    if(wants_key(filter, "font_size"))
    {
        append_prop(json, first, "font_size", fmt::format("{}", comp.get_font_size()));
    }
    if(wants_key(filter, "auto_size"))
    {
        append_prop(json, first, "auto_size", comp.get_auto_size() ? "true" : "false");
    }
    if(wants_key(filter, "area"))
    {
        const auto& area = comp.get_area();
        append_prop(json, first, "area", fmt::format("[{:.6g},{:.6g}]", area.width, area.height));
    }
    if(wants_key(filter, "alignment"))
    {
        append_prop(json, first, "alignment", make_json_string(alignment_to_string(comp.get_alignment())));
    }
    if(wants_key(filter, "is_rich"))
    {
        append_prop(json, first, "is_rich", comp.get_is_rich_text() ? "true" : "false");
    }
    if(wants_key(filter, "text_color"))
    {
        append_prop(json, first, "text_color", color_to_json(comp.get_style().get_text_color()));
    }
    if(wants_key(filter, "opacity"))
    {
        append_prop(json, first, "opacity", fmt::format("{:.6g}", comp.get_style().opacity));
    }
    json += "}";
    return json;
}

auto apply_text_properties(text_component& comp,
                           const simdjson::dom::object& properties,
                           component_apply_result& result) -> void
{
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        auto fail = [&](const std::string& message)
        {
            result.ok = false;
            result.errors.push_back(key + ": " + message);
        };
        auto applied = [&]()
        {
            result.applied.push_back(key);
        };
        if(key == "text")
        {
            std::string v;
            parse_string(value, v, error) ? (comp.set_text(v), applied()) : fail(error);
        }
        else if(key == "font_size")
        {
            int v{};
            parse_int(value, v, error) ? (comp.set_font_size(static_cast<uint32_t>(std::max(v, 1))), applied())
                                       : fail(error);
        }
        else if(key == "auto_size")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_auto_size(v), applied()) : fail(error);
        }
        else if(key == "area")
        {
            math::vec2 v{};
            parse_vec2(value, v, error) ? (comp.set_area(fsize_t{v.x, v.y}), applied()) : fail(error);
        }
        else if(key == "alignment")
        {
            std::string s;
            alignment v{};
            (parse_string(value, s, error) && alignment_from_string(s, v))
                ? (comp.set_alignment(v), applied())
                : fail("expected <top|middle|bottom>_<left|center|right>");
        }
        else if(key == "is_rich")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_is_rich_text(v), applied()) : fail(error);
        }
        else if(key == "text_color")
        {
            math::color v{};
            if(!parse_color(value, v, error))
            {
                fail(error);
                continue;
            }
            auto style = comp.get_style();
            style.set_text_color(v);
            comp.set_style(style);
            applied();
        }
        else if(key == "opacity")
        {
            float v{};
            if(!parse_number(value, v, error))
            {
                fail(error);
                continue;
            }
            auto style = comp.get_style();
            style.opacity = v;
            comp.set_style(style);
            applied();
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
}

auto reflection_probe_to_json(const reflection_probe_component& comp, const std::unordered_set<std::string>* filter)
    -> std::string
{
    const auto& probe = comp.get_probe();
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "type"))
    {
        append_prop(json, first, "type", make_json_string(enum_to_string(probe.type, MCP_PROBE_TYPE_TABLE)));
    }
    if(wants_key(filter, "intensity"))
    {
        append_prop(json, first, "intensity", fmt::format("{:.6g}", probe.intensity));
    }
    if(wants_key(filter, "extents"))
    {
        append_prop(json, first, "extents", vec3_to_json(probe.box_data.extents));
    }
    if(wants_key(filter, "transition_distance"))
    {
        append_prop(json, first, "transition_distance", fmt::format("{:.6g}", probe.box_data.transition_distance));
    }
    if(wants_key(filter, "range"))
    {
        append_prop(json, first, "range", fmt::format("{:.6g}", probe.sphere_data.range));
    }
    if(wants_key(filter, "update_mode"))
    {
        append_prop(json,
                    first,
                    "update_mode",
                    make_json_string(enum_to_string(comp.get_update_mode(), MCP_PROBE_UPDATE_MODE_TABLE)));
    }
    if(wants_key(filter, "update_interval"))
    {
        append_prop(json, first, "update_interval", fmt::format("{:.6g}", comp.get_update_interval()));
    }
    if(wants_key(filter, "resolution"))
    {
        append_prop(json,
                    first,
                    "resolution",
                    make_json_string(enum_to_string(comp.get_resolution(), MCP_PROBE_RESOLUTION_TABLE)));
    }
    if(wants_key(filter, "capture_sky"))
    {
        append_prop(json, first, "capture_sky", comp.get_capture_sky() ? "true" : "false");
    }
    if(wants_key(filter, "capture_shadows"))
    {
        append_prop(json, first, "capture_shadows", comp.get_capture_shadows() ? "true" : "false");
    }
    json += "}";
    return json;
}

auto apply_reflection_probe_properties(reflection_probe_component& comp,
                                       const simdjson::dom::object& properties,
                                       component_apply_result& result) -> void
{
    auto probe = comp.get_probe();
    bool probe_dirty = false;
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        auto fail = [&](const std::string& message)
        {
            result.ok = false;
            result.errors.push_back(key + ": " + message);
        };
        auto applied = [&]()
        {
            result.applied.push_back(key);
        };
        if(key == "type")
        {
            std::string s;
            probe_type v{};
            if(parse_string(value, s, error) && enum_from_string<probe_type>(s, MCP_PROBE_TYPE_TABLE, v))
            {
                probe.type = v;
                probe_dirty = true;
                applied();
            }
            else
            {
                fail("expected box|sphere");
            }
        }
        else if(key == "intensity")
        {
            parse_number(value, probe.intensity, error) ? (probe_dirty = true, applied()) : fail(error);
        }
        else if(key == "extents")
        {
            parse_vec3(value, probe.box_data.extents, error) ? (probe_dirty = true, applied()) : fail(error);
        }
        else if(key == "transition_distance")
        {
            parse_number(value, probe.box_data.transition_distance, error) ? (probe_dirty = true, applied())
                                                                           : fail(error);
        }
        else if(key == "range")
        {
            parse_number(value, probe.sphere_data.range, error) ? (probe_dirty = true, applied()) : fail(error);
        }
        else if(key == "update_mode")
        {
            std::string s;
            probe_update_mode v{};
            (parse_string(value, s, error) && enum_from_string<probe_update_mode>(s, MCP_PROBE_UPDATE_MODE_TABLE, v))
                ? (comp.set_update_mode(v), applied())
                : fail("expected on_demand|once|realtime");
        }
        else if(key == "update_interval")
        {
            float v{};
            parse_number(value, v, error) ? (comp.set_update_interval(v), applied()) : fail(error);
        }
        else if(key == "resolution")
        {
            std::string s;
            probe_resolution v{};
            (parse_string(value, s, error) && enum_from_string<probe_resolution>(s, MCP_PROBE_RESOLUTION_TABLE, v))
                ? (comp.set_resolution(v), applied())
                : fail("expected 16|32|64|128|256|512|1024");
        }
        else if(key == "capture_sky")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_capture_sky(v), applied()) : fail(error);
        }
        else if(key == "capture_shadows")
        {
            bool v{};
            parse_bool(value, v, error) ? (comp.set_capture_shadows(v), applied()) : fail(error);
        }
        else
        {
            result.unknown.push_back(key);
        }
    }
    if(probe_dirty)
    {
        comp.set_probe(probe);
    }
}

auto bloom_to_json(const bloom_component& comp, const std::unordered_set<std::string>* filter) -> std::string
{
    std::string json = "{";
    bool first = true;
    if(wants_key(filter, "enabled"))
    {
        append_prop(json, first, "enabled", comp.enabled ? "true" : "false");
    }
    if(wants_key(filter, "threshold"))
    {
        append_prop(json, first, "threshold", fmt::format("{:.6g}", comp.settings.threshold));
    }
    if(wants_key(filter, "soft_knee"))
    {
        append_prop(json, first, "soft_knee", fmt::format("{:.6g}", comp.settings.soft_knee));
    }
    if(wants_key(filter, "clamp"))
    {
        append_prop(json, first, "clamp", fmt::format("{:.6g}", comp.settings.clamp));
    }
    if(wants_key(filter, "intensity"))
    {
        append_prop(json, first, "intensity", fmt::format("{:.6g}", comp.settings.intensity));
    }
    if(wants_key(filter, "scatter"))
    {
        append_prop(json, first, "scatter", fmt::format("{:.6g}", comp.settings.scatter));
    }
    json += "}";
    return json;
}

auto apply_bloom_properties(bloom_component& comp,
                            const simdjson::dom::object& properties,
                            component_apply_result& result) -> void
{
    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;
        auto fail = [&](const std::string& message)
        {
            result.ok = false;
            result.errors.push_back(key + ": " + message);
        };
        auto applied = [&]()
        {
            result.applied.push_back(key);
        };
        if(key == "enabled")
        {
            parse_bool(value, comp.enabled, error) ? applied() : fail(error);
        }
        else if(key == "threshold")
        {
            parse_number(value, comp.settings.threshold, error) ? applied() : fail(error);
        }
        else if(key == "soft_knee")
        {
            parse_number(value, comp.settings.soft_knee, error) ? applied() : fail(error);
        }
        else if(key == "clamp")
        {
            parse_number(value, comp.settings.clamp, error) ? applied() : fail(error);
        }
        else if(key == "intensity")
        {
            parse_number(value, comp.settings.intensity, error) ? applied() : fail(error);
        }
        else if(key == "scatter")
        {
            parse_number(value, comp.settings.scatter, error) ? applied() : fail(error);
        }
        else
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
    add("Particle Emitter", "enabled", "boolean");
    add("Particle Emitter", "shape", "string", R"("enum":["sphere","hemisphere","circle","box","rect"])");
    add("Particle Emitter", "direction", "string", R"("enum":["up","outward","inward"])");
    add("Particle Emitter", "spawn_location", "string", R"("enum":["inside","surface"])");
    add("Particle Emitter", "simulation_space", "string", R"("enum":["world","local"])");
    add("Particle Emitter", "max_particles", "integer");
    add("Particle Emitter", "emission_rate", "number");
    add("Particle Emitter", "lifetime", "number", R"("description":"Particle lifetime seconds")");
    add("Particle Emitter", "emission_lifetime", "number", R"("description":"Emitter run seconds per loop")");
    add("Particle Emitter", "start_delay", "number");
    add("Particle Emitter", "gravity_scale", "number");
    add("Particle Emitter", "velocity_damping", "number");
    add("Particle Emitter", "force_over_lifetime", "array", R"("items":"number","minItems":3,"maxItems":3)");
    add("Particle Emitter", "emission_shape_position", "array", R"("items":"number","minItems":3,"maxItems":3)");
    add("Particle Emitter", "emission_shape_scale", "array", R"("items":"number","minItems":3,"maxItems":3)");
    add("Particle Emitter", "initial_scale_3d", "array", R"("items":"number","minItems":3,"maxItems":3)");
    add("Particle Emitter", "opacity", "number");
    add("Particle Emitter", "color_intensity", "number");
    add("Particle Emitter",
        "color_gradient",
        "array",
        R"("description":"[{progress,color:[r,g,b,a]}] sorted by progress 0..1")");
    add("Particle Emitter",
        "velocity_gradient",
        "array",
        R"("description":"[{progress,min,max}] speed range over lifetime")");
    add("Particle Emitter",
        "scale_gradient",
        "array",
        R"("description":"[{progress,min,max}] size range over lifetime")");
    add("Particle Emitter", "texture", "string", R"("description":"Texture asset key/uid or null")");
    add("Particle Emitter", "texture_mode", "string", R"("enum":["multi_channel","mask"])");
    add("Particle Emitter",
        "render_mode",
        "string",
        R"("enum":["billboard","horizontal_billboard","vertical_billboard"])");
    add("Particle Emitter", "blend_mode", "string", R"("enum":["normal","additive","multiply"])");
    add("Particle Emitter", "texture_sheet_tiles", "array", R"("items":"number","minItems":2,"maxItems":2)");
    add("Particle Emitter", "texture_sheet_cycles", "number");
    add("Particle Emitter", "texture_sheet_randomize", "boolean");
    add("Particle Emitter", "loop", "boolean");
    add("Particle Emitter", "align_to_direction", "boolean");
    add("Particle Emitter", "pivot", "array", R"("items":"number","minItems":2,"maxItems":2)");
    add("Physics", "body_type", "string", R"("enum":["static","kinematic","dynamic"])");
    add("Physics", "is_using_gravity", "boolean");
    add("Physics", "is_autoscaled", "boolean");
    add("Physics", "is_sensor", "boolean");
    add("Physics", "mass", "number");
    add("Physics", "freeze_position", "array", R"("items":"boolean","minItems":3,"maxItems":3)");
    add("Physics", "freeze_rotation", "array", R"("items":"boolean","minItems":3,"maxItems":3)");
    add("Physics",
        "shapes",
        "array",
        R"("description":"[{type:box|sphere|capsule|cylinder,center:[3],extents:[3]?,radius?,length?}]")");
    add("Animation", "animation", "string", R"("description":"Animation clip asset key/uid or null")");
    add("Animation", "auto_play", "boolean");
    add("Animation", "apply_root_motion", "boolean");
    add("Animation", "speed", "number");
    add("Text", "text", "string");
    add("Text", "font_size", "integer");
    add("Text", "auto_size", "boolean");
    add("Text", "area", "array", R"("items":"number","minItems":2,"maxItems":2)");
    add("Text",
        "alignment",
        "string",
        R"("enum":["top_left","top_center","top_right","middle_left","middle_center","middle_right","bottom_left","bottom_center","bottom_right"])");
    add("Text", "is_rich", "boolean");
    add("Text", "text_color", "array", R"("items":"number","minItems":3,"maxItems":4)");
    add("Text", "opacity", "number");
    add("Reflection Probe", "type", "string", R"("enum":["box","sphere"])");
    add("Reflection Probe", "intensity", "number");
    add("Reflection Probe", "extents", "array", R"("items":"number","minItems":3,"maxItems":3)");
    add("Reflection Probe", "transition_distance", "number");
    add("Reflection Probe", "range", "number");
    add("Reflection Probe", "update_mode", "string", R"("enum":["on_demand","once","realtime"])");
    add("Reflection Probe", "update_interval", "number");
    add("Reflection Probe", "resolution", "string", R"("enum":["16","32","64","128","256","512","1024"])");
    add("Reflection Probe", "capture_sky", "boolean");
    add("Reflection Probe", "capture_shadows", "boolean");
    add("Bloom", "enabled", "boolean");
    add("Bloom", "threshold", "number");
    add("Bloom", "soft_knee", "number");
    add("Bloom", "clamp", "number");
    add("Bloom", "intensity", "number");
    add("Bloom", "scatter", "number");
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
           component_pretty_name == "Volume" || component_pretty_name == "Script" ||
           component_pretty_name == "Particle Emitter" || component_pretty_name == "Physics" ||
           component_pretty_name == "Animation" || component_pretty_name == "Text" ||
           component_pretty_name == "Reflection Probe" || component_pretty_name == "Bloom";
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
    if(component_pretty_name == "Particle Emitter")
    {
        auto* comp = entity.try_get<particle_emitter_component>();
        if(!comp)
        {
            error = "Component not present on entity: Particle Emitter";
            return {};
        }
        return particle_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Physics")
    {
        auto* comp = entity.try_get<physics_component>();
        if(!comp)
        {
            error = "Component not present on entity: Physics";
            return {};
        }
        return physics_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Animation")
    {
        auto* comp = entity.try_get<animation_component>();
        if(!comp)
        {
            error = "Component not present on entity: Animation";
            return {};
        }
        return animation_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Text")
    {
        auto* comp = entity.try_get<text_component>();
        if(!comp)
        {
            error = "Component not present on entity: Text";
            return {};
        }
        return text_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Reflection Probe")
    {
        auto* comp = entity.try_get<reflection_probe_component>();
        if(!comp)
        {
            error = "Component not present on entity: Reflection Probe";
            return {};
        }
        return reflection_probe_to_json(*comp, filter_ptr);
    }
    if(component_pretty_name == "Bloom")
    {
        auto* comp = entity.try_get<bloom_component>();
        if(!comp)
        {
            error = "Component not present on entity: Bloom";
            return {};
        }
        return bloom_to_json(*comp, filter_ptr);
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
    if(component_pretty_name == "Particle Emitter")
    {
        auto* comp = entity.try_get<particle_emitter_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Particle Emitter");
            return result;
        }
        apply_particle_properties(ctx, *comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Physics")
    {
        auto* comp = entity.try_get<physics_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Physics");
            return result;
        }
        apply_physics_properties(*comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Animation")
    {
        auto* comp = entity.try_get<animation_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Animation");
            return result;
        }
        apply_animation_properties(ctx, *comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Text")
    {
        auto* comp = entity.try_get<text_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Text");
            return result;
        }
        apply_text_properties(*comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Reflection Probe")
    {
        auto* comp = entity.try_get<reflection_probe_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Reflection Probe");
            return result;
        }
        apply_reflection_probe_properties(*comp, properties, result);
        return result;
    }
    if(component_pretty_name == "Bloom")
    {
        auto* comp = entity.try_get<bloom_component>();
        if(!comp)
        {
            result.ok = false;
            result.errors.push_back("Component not present on entity: Bloom");
            return result;
        }
        apply_bloom_properties(*comp, properties, result);
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
    try_add("Particle Emitter");
    try_add("Physics");
    try_add("Animation");
    try_add("Text");
    try_add("Reflection Probe");
    try_add("Bloom");
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
