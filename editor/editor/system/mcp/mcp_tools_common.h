#pragma once

#include "mcp_protocol.h"
#include "mcp_tool_registry.h"

#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/play_mode.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/entity_inspect.h>
#include <editor/system/project_manager.h>
#include <math/math.h>
#include <uuid/uuid.h>

#include <engine/assets/impl/asset_extensions.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <logging/logging.h>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace unravel::mcp
{

inline auto entity_id_string(entt::handle entity) -> std::string
{
    return unravel::entity_id_string(entity);
}

inline auto find_entity(scene& scn, const std::string& id) -> entt::handle
{
    return find_entity_by_id(scn, id);
}

inline auto require_edit_scene(rtti::context& ctx, scene*& out_scene, std::string& error) -> bool
{
    auto* play = ctx.has<play_mode>() ? &ctx.get_cached<play_mode>() : nullptr;
    if(play && play->is_active())
    {
        error = "Scene is in play mode; mutating tools are disabled";
        return false;
    }

    auto& em = ctx.get_cached<editing_manager>();
    out_scene = em.get_active_scene(ctx);
    if(!out_scene || !out_scene->registry)
    {
        error = "No active scene";
        return false;
    }
    return true;
}

inline auto read_string(const simdjson::dom::object& args, const char* key, std::string& out) -> bool
{
    std::string_view view;
    if(args[key].get(view))
    {
        return false;
    }
    out.assign(view);
    return true;
}

inline auto read_bool(const simdjson::dom::object& args, const char* key, bool& out) -> bool
{
    bool value = false;
    if(args[key].get(value))
    {
        return false;
    }
    out = value;
    return true;
}

inline auto read_double(const simdjson::dom::object& args, const char* key, double& out) -> bool
{
    double value = 0.0;
    if(args[key].get(value))
    {
        return false;
    }
    out = value;
    return true;
}

inline auto read_vec3(const simdjson::dom::object& args, const char* key, math::vec3& out) -> bool
{
    simdjson::dom::array arr;
    if(args[key].get(arr))
    {
        return false;
    }
    std::vector<double> values;
    for(auto el : arr)
    {
        double v = 0.0;
        if(el.get(v))
        {
            return false;
        }
        values.push_back(v);
    }
    if(values.size() != 3)
    {
        return false;
    }
    out = math::vec3{static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2])};
    return true;
}

inline auto empty_object_schema() -> std::string
{
    return R"({"type":"object","properties":{}})";
}

inline auto require_not_play_mode(rtti::context& ctx, std::string& error) -> bool
{
    auto* play = ctx.has<play_mode>() ? &ctx.get_cached<play_mode>() : nullptr;
    if(play && play->is_active())
    {
        error = "Editor is in play mode; this tool is disabled";
        return false;
    }
    return true;
}

inline auto require_open_project(rtti::context& ctx, std::string& error) -> bool
{
    if(!ctx.has<project_manager>() || !ctx.get_cached<project_manager>().has_open_project())
    {
        error = "No project open";
        return false;
    }
    return true;
}

/// Parse transform space: "world" (default) or "local". Returns false on invalid values.
inline auto read_transform_space(const simdjson::dom::object& args, bool& out_is_local, std::string& error) -> bool
{
    out_is_local = false;
    std::string space;
    if(!read_string(args, "space", space) || space.empty() || space == "world" || space == "global")
    {
        out_is_local = false;
        return true;
    }
    if(space == "local")
    {
        out_is_local = true;
        return true;
    }
    error = "Invalid space (use \"world\" or \"local\")";
    return false;
}

inline auto read_wait_ms(const simdjson::dom::object& args, int64_t default_ms = 1000) -> std::chrono::milliseconds
{
    int64_t wait_ms = default_ms;
    if(args["wait_ms"].get(wait_ms))
    {
        wait_ms = default_ms;
    }
    if(wait_ms < 0)
    {
        wait_ms = 0;
    }
    if(wait_ms > 15000)
    {
        wait_ms = 15000;
    }
    return std::chrono::milliseconds(wait_ms);
}

inline auto sleep_worker(std::chrono::milliseconds wait_ms) -> void
{
    if(wait_ms.count() > 0)
    {
        std::this_thread::sleep_for(wait_ms);
    }
}

inline auto to_lower_ascii(std::string value) -> std::string
{
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

inline auto contains_ci(std::string_view haystack, std::string_view needle) -> bool
{
    if(needle.empty())
    {
        return true;
    }
    const auto h = to_lower_ascii(std::string(haystack));
    const auto n = to_lower_ascii(std::string(needle));
    return h.find(n) != std::string::npos;
}

inline auto starts_with(std::string_view value, std::string_view prefix) -> bool
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

inline auto bbox_to_json(const math::bbox& bounds) -> std::string
{
    const auto center = bounds.get_center();
    const auto extents = bounds.get_extents();
    return fmt::format(
        R"({{"min":[{:.6g},{:.6g},{:.6g}],"max":[{:.6g},{:.6g},{:.6g}],"center":[{:.6g},{:.6g},{:.6g}],"extents":[{:.6g},{:.6g},{:.6g}]}})",
        bounds.min.x,
        bounds.min.y,
        bounds.min.z,
        bounds.max.x,
        bounds.max.y,
        bounds.max.z,
        center.x,
        center.y,
        center.z,
        extents.x,
        extents.y,
        extents.z);
}

inline auto normalize_asset_type_filter(std::string type) -> std::string
{
    type = to_lower_ascii(std::move(type));
    while(!type.empty() && (type.front() == '*' || type.front() == '.'))
    {
        if(type.front() == '*')
        {
            type.erase(type.begin());
            continue;
        }
        break;
    }
    if(!type.empty() && type.front() != '.')
    {
        type.insert(type.begin(), '.');
    }
    // Friendly names -> primary format from ex::get_suported_formats (first entry).
    if(type == ".prefab")
    {
        return ex::get_format<unravel::prefab>();
    }
    if(type == ".mesh" || type == ".model")
    {
        return ex::get_format<unravel::mesh>();
    }
    if(type == ".material")
    {
        return ex::get_format<unravel::material>();
    }
    if(type == ".texture" || type == ".tex")
    {
        return ex::get_format<gfx::texture>();
    }
    if(type == ".audio" || type == ".audioclip")
    {
        return ex::get_format<unravel::audio_clip>();
    }
    if(type == ".scene")
    {
        return ex::get_format<unravel::scene_prefab>();
    }
    if(type == ".physics" || type == ".phxmat" || type == ".physics_material")
    {
        return ex::get_format<unravel::physics_material>();
    }
    return type;
}

inline auto protocol_to_group(const std::string& protocol) -> std::string
{
    if(protocol == "app" || protocol == "app:/")
    {
        return "app:/";
    }
    if(protocol == "engine" || protocol == "engine:/")
    {
        return "engine:/";
    }
    if(protocol == "editor" || protocol == "editor:/")
    {
        return "editor:/";
    }
    return {};
}

} // namespace unravel::mcp
