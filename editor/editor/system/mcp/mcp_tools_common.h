#pragma once

#include "mcp_protocol.h"
#include "mcp_tool_registry.h"

#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/play_mode.h>
#include <editor/editing/editing_manager.h>
#include <editor/system/project_manager.h>
#include <math/math.h>
#include <uuid/uuid.h>

#include <logging/logging.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace unravel::mcp
{

inline auto entity_id_string(entt::handle entity) -> std::string
{
    if(!entity)
    {
        return {};
    }
    if(auto* id = entity.try_get<id_component>())
    {
        return hpp::to_string(id->id);
    }
    return std::to_string(entt::to_integral(entity.entity()));
}

inline auto find_entity(scene& scn, const std::string& id) -> entt::handle
{
    if(id.empty() || !scn.registry)
    {
        return {};
    }

    if(auto uuid = hpp::uuid::from_string(id))
    {
        auto handle = scn.find_entity_by_uuid(*uuid);
        if(handle)
        {
            return handle;
        }
    }

    try
    {
        const auto integral = static_cast<entt::entity>(std::stoul(id));
        if(scn.registry->valid(integral))
        {
            return entt::handle(*scn.registry, integral);
        }
    }
    catch(...)
    {
    }
    return {};
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

} // namespace unravel::mcp
