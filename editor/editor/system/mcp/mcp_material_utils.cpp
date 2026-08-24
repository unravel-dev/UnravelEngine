#include "mcp_material_utils.h"
#include "mcp_tools_common.h"

#include <editor/editing/thumbnail_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/meta/rendering/material.hpp>
#include <engine/assets/impl/asset_writer.h>
#include <filesystem/filesystem.h>
#include <graphics/texture.h>
#include <logging/logging.h>
#include <math/color.h>
#include <uuid/uuid.h>

namespace unravel::mcp
{
namespace
{

auto cull_type_to_string(cull_type value) -> const char*
{
    switch(value)
    {
        case cull_type::none:
            return "none";
        case cull_type::clockwise:
            return "clockwise";
        case cull_type::counter_clockwise:
            return "counter_clockwise";
    }
    return "counter_clockwise";
}

auto cull_type_from_string(std::string_view value, cull_type& out) -> bool
{
    if(value == "none")
    {
        out = cull_type::none;
        return true;
    }
    if(value == "clockwise")
    {
        out = cull_type::clockwise;
        return true;
    }
    if(value == "counter_clockwise" || value == "counter-clockwise" || value == "ccw")
    {
        out = cull_type::counter_clockwise;
        return true;
    }
    return false;
}

auto alpha_mode_to_string(alpha_mode value) -> const char*
{
    switch(value)
    {
        case alpha_mode::opaque:
            return "opaque";
        case alpha_mode::mask:
            return "mask";
        case alpha_mode::blend:
            return "blend";
    }
    return "opaque";
}

auto alpha_mode_from_string(std::string_view value, alpha_mode& out) -> bool
{
    if(value == "opaque")
    {
        out = alpha_mode::opaque;
        return true;
    }
    if(value == "mask")
    {
        out = alpha_mode::mask;
        return true;
    }
    if(value == "blend")
    {
        out = alpha_mode::blend;
        return true;
    }
    return false;
}


auto resolve_texture(rtti::context& ctx, const simdjson::dom::element& el, asset_handle<gfx::texture>& out, std::string& error)
    -> bool
{
    if(el.is_null())
    {
        out = {};
        return true;
    }

    std::string_view key_view;
    if(el.get(key_view))
    {
        error = "Texture map value must be a string asset key or null";
        return false;
    }
    if(key_view.empty())
    {
        out = {};
        return true;
    }

    auto& am = ctx.get_cached<asset_manager>();
    out = am.get_asset<gfx::texture>(std::string(key_view));
    if(!out)
    {
        error = "Texture not found: " + std::string(key_view);
        return false;
    }
    return true;
}

} // namespace

auto list_material_property_schema_json() -> std::string
{
    return R"([
{"name":"cull_type","type":"string","enum":["none","clockwise","counter_clockwise"]},
{"name":"base_color","type":"array","items":"number","minItems":3,"maxItems":4},
{"name":"subsurface_color","type":"array","items":"number","minItems":3,"maxItems":4},
{"name":"emissive_color","type":"array","items":"number","minItems":3,"maxItems":4},
{"name":"emissive_intensity","type":"number"},
{"name":"roughness","type":"number"},
{"name":"metalness","type":"number"},
{"name":"bumpiness","type":"number"},
{"name":"alpha_mode","type":"string","enum":["opaque","mask","blend"]},
{"name":"alpha_cutoff","type":"number"},
{"name":"tiling","type":"array","items":"number","minItems":2,"maxItems":2},
{"name":"dither_threshold","type":"array","items":"number","minItems":2,"maxItems":2},
{"name":"color_map","type":"string","description":"Texture asset key or null"},
{"name":"normal_map","type":"string"},
{"name":"roughness_map","type":"string"},
{"name":"metalness_map","type":"string"},
{"name":"emissive_map","type":"string"},
{"name":"ao_map","type":"string"}
])";
}

auto normalize_material_key(const std::string& path_or_key) -> std::string
{
    auto key = path_or_key;
    if(key.empty())
    {
        return key;
    }
    // Allow absolute filesystem paths by converting to protocol when possible.
    fs::error_code ec;
    const fs::path as_path(key);
    if(as_path.is_absolute() && fs::exists(as_path, ec))
    {
        key = fs::convert_to_protocol(as_path).generic_string();
    }
    if(key.size() < 4 || key.substr(key.size() - 4) != ".mat")
    {
        key += ".mat";
    }
    return key;
}

auto resolve_material_asset(rtti::context& ctx, const std::string& key, const std::string& uid, std::string& error)
    -> asset_handle<material>
{
    auto& am = ctx.get_cached<asset_manager>();
    if(!key.empty())
    {
        auto handle = am.get_asset<material>(key);
        if(!handle)
        {
            error = "Material not found: " + key;
            return {};
        }
        return handle;
    }
    if(!uid.empty())
    {
        auto uuid = hpp::uuid::from_string(uid);
        if(!uuid)
        {
            error = "Invalid material uid";
            return {};
        }
        auto handle = am.get_asset<material>(*uuid);
        if(!handle)
        {
            error = "Material not found for uid: " + uid;
            return {};
        }
        return handle;
    }
    error = "Provide key or uid";
    return {};
}

auto as_pbr_material(const material::sptr& mat, std::string& error) -> std::shared_ptr<pbr_material>
{
    if(!mat)
    {
        error = "Null material";
        return {};
    }
    auto pbr = std::dynamic_pointer_cast<pbr_material>(mat);
    if(!pbr)
    {
        error = "Material is not a pbr_material";
        return {};
    }
    return pbr;
}

auto material_to_json(const material& mat) -> std::string
{
    std::string json = "{";
    json += fmt::format(R"("cull_type":{},)", make_json_string(std::string(cull_type_to_string(mat.get_cull_type()))));

    auto pbr = dynamic_cast<const pbr_material*>(&mat);
    if(!pbr)
    {
        json += R"("type":"material"})";
        return json;
    }

    json += R"("type":"pbr_material",)";
    json += fmt::format(R"("base_color":{},)", color_to_json(pbr->get_base_color()));
    json += fmt::format(R"("subsurface_color":{},)", color_to_json(pbr->get_subsurface_color()));
    json += fmt::format(R"("emissive_color":{},)", color_to_json(pbr->get_emissive_color()));
    json += fmt::format(R"("emissive_intensity":{:.6g},)", pbr->get_emissive_intensity());
    json += fmt::format(R"("roughness":{:.6g},)", pbr->get_roughness());
    json += fmt::format(R"("metalness":{:.6g},)", pbr->get_metalness());
    json += fmt::format(R"("bumpiness":{:.6g},)", pbr->get_bumpiness());
    json += fmt::format(R"("alpha_mode":{},)", make_json_string(std::string(alpha_mode_to_string(pbr->get_alpha_mode()))));
    json += fmt::format(R"("alpha_cutoff":{:.6g},)", pbr->get_alpha_cutoff());
    json += fmt::format(R"("tiling":{},)", vec2_to_json(pbr->get_tiling()));
    json += fmt::format(R"("dither_threshold":{},)", vec2_to_json(pbr->get_dither_threshold()));
    json += fmt::format(R"("color_map":{},)", asset_handle_key_json(pbr->get_color_map()));
    json += fmt::format(R"("normal_map":{},)", asset_handle_key_json(pbr->get_normal_map()));
    json += fmt::format(R"("roughness_map":{},)", asset_handle_key_json(pbr->get_roughness_map()));
    json += fmt::format(R"("metalness_map":{},)", asset_handle_key_json(pbr->get_metalness_map()));
    json += fmt::format(R"("emissive_map":{},)", asset_handle_key_json(pbr->get_emissive_map()));
    json += fmt::format(R"("ao_map":{})", asset_handle_key_json(pbr->get_ao_map()));
    json += "}";
    return json;
}

auto apply_material_properties(rtti::context& ctx, material::sptr mat, const simdjson::dom::object& properties)
    -> material_apply_result
{
    material_apply_result result;
    std::string cast_error;
    auto pbr = as_pbr_material(mat, cast_error);
    if(!pbr)
    {
        result.ok = false;
        result.errors.push_back(cast_error);
        return result;
    }

    for(auto field : properties)
    {
        const std::string key(field.key);
        const auto& value = field.value;
        std::string error;

        auto mark_applied = [&]()
        {
            result.applied.push_back(key);
        };
        auto mark_error = [&](const std::string& msg)
        {
            result.ok = false;
            result.errors.push_back(key + ": " + msg);
        };

        if(key == "cull_type")
        {
            std::string_view s;
            cull_type cull{};
            if(value.get(s) || !cull_type_from_string(s, cull))
            {
                mark_error("expected none|clockwise|counter_clockwise");
                continue;
            }
            pbr->set_cull_type(cull);
            mark_applied();
            continue;
        }
        if(key == "base_color")
        {
            math::color c;
            if(!parse_color(value, c, error))
            {
                mark_error(error);
                continue;
            }
            pbr->set_base_color(c);
            mark_applied();
            continue;
        }
        if(key == "subsurface_color")
        {
            math::color c;
            if(!parse_color(value, c, error))
            {
                mark_error(error);
                continue;
            }
            pbr->set_subsurface_color(c);
            mark_applied();
            continue;
        }
        if(key == "emissive_color")
        {
            math::color c;
            if(!parse_color(value, c, error))
            {
                mark_error(error);
                continue;
            }
            pbr->set_emissive_color(c);
            mark_applied();
            continue;
        }
        if(key == "emissive_intensity")
        {
            double v = 0.0;
            if(value.get(v))
            {
                mark_error("expected number");
                continue;
            }
            pbr->set_emissive_intensity(static_cast<float>(v));
            mark_applied();
            continue;
        }
        if(key == "roughness")
        {
            double v = 0.0;
            if(value.get(v))
            {
                mark_error("expected number");
                continue;
            }
            pbr->set_roughness(static_cast<float>(v));
            mark_applied();
            continue;
        }
        if(key == "metalness")
        {
            double v = 0.0;
            if(value.get(v))
            {
                mark_error("expected number");
                continue;
            }
            pbr->set_metalness(static_cast<float>(v));
            mark_applied();
            continue;
        }
        if(key == "bumpiness")
        {
            double v = 0.0;
            if(value.get(v))
            {
                mark_error("expected number");
                continue;
            }
            pbr->set_bumpiness(static_cast<float>(v));
            mark_applied();
            continue;
        }
        if(key == "alpha_mode")
        {
            std::string_view s;
            alpha_mode mode{};
            if(value.get(s) || !alpha_mode_from_string(s, mode))
            {
                mark_error("expected opaque|mask|blend");
                continue;
            }
            pbr->set_alpha_mode(mode);
            mark_applied();
            continue;
        }
        if(key == "alpha_cutoff")
        {
            double v = 0.0;
            if(value.get(v))
            {
                mark_error("expected number");
                continue;
            }
            pbr->set_alpha_cutoff(static_cast<float>(v));
            mark_applied();
            continue;
        }
        if(key == "tiling")
        {
            math::vec2 v{};
            if(!parse_vec2(value, v, error))
            {
                mark_error(error);
                continue;
            }
            pbr->set_tiling(v);
            mark_applied();
            continue;
        }
        if(key == "dither_threshold")
        {
            math::vec2 v{};
            if(!parse_vec2(value, v, error))
            {
                mark_error(error);
                continue;
            }
            pbr->set_dither_threshold(v);
            mark_applied();
            continue;
        }

        auto apply_map = [&](auto setter)
        {
            asset_handle<gfx::texture> tex;
            if(!resolve_texture(ctx, value, tex, error))
            {
                mark_error(error);
                return;
            }
            (pbr.get()->*setter)(tex);
            mark_applied();
        };

        if(key == "color_map")
        {
            apply_map(&pbr_material::set_color_map);
            continue;
        }
        if(key == "normal_map")
        {
            apply_map(&pbr_material::set_normal_map);
            continue;
        }
        if(key == "roughness_map")
        {
            apply_map(&pbr_material::set_roughness_map);
            continue;
        }
        if(key == "metalness_map")
        {
            apply_map(&pbr_material::set_metalness_map);
            continue;
        }
        if(key == "emissive_map")
        {
            apply_map(&pbr_material::set_emissive_map);
            continue;
        }
        if(key == "ao_map")
        {
            apply_map(&pbr_material::set_ao_map);
            continue;
        }

        result.unknown.push_back(key);
    }

    return result;
}

auto save_material_asset(rtti::context& ctx, const asset_handle<material>& handle, std::string& error) -> bool
{
    if(!handle)
    {
        error = "Invalid material handle";
        return false;
    }

    if(!asset_writer::atomic_save_to_file(handle.id(), handle))
    {
        error = "Failed to save material: " + handle.id();
        return false;
    }

    if(ctx.has<thumbnail_manager>())
    {
        ctx.get_cached<thumbnail_manager>().regenerate_thumbnail(handle.uid());
    }
    return true;
}

} // namespace unravel::mcp
