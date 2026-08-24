#pragma once

#include "mcp_protocol.h"
#include "mcp_tool_registry.h"

#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/play_mode.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/entity_inspect.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/system/project_manager.h>
#include <math/color.h>
#include <math/math.h>
#include <uuid/uuid.h>

#include <engine/assets/asset_handle.h>
#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/filesystem.h>

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

inline auto require_active_scene(rtti::context& ctx, scene*& out_scene, std::string& error) -> bool
{
    auto& em = ctx.get_cached<editing_manager>();
    out_scene = em.get_active_scene(ctx);
    if(!out_scene || !out_scene->registry)
    {
        error = "No active scene";
        return false;
    }
    return true;
}

inline auto require_edit_scene(rtti::context& ctx, scene*& out_scene, std::string& error) -> bool
{
    auto* play = ctx.has<play_mode>() ? &ctx.get_cached<play_mode>() : nullptr;
    if(play && play->is_active())
    {
        error = "Scene is in play mode; mutating tools are disabled";
        return false;
    }
    return require_active_scene(ctx, out_scene, error);
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

inline auto parse_number(const simdjson::dom::element& value, float& out, std::string& error) -> bool
{
    double d = 0.0;
    if(!value.get(d))
    {
        out = static_cast<float>(d);
        return true;
    }
    int64_t i = 0;
    if(!value.get(i))
    {
        out = static_cast<float>(i);
        return true;
    }
    error = "Expected number";
    return false;
}

inline auto parse_vec2(const simdjson::dom::element& value, math::vec2& out, std::string& error) -> bool
{
    simdjson::dom::array arr;
    if(value.get(arr))
    {
        error = "Expected vec2 array [x,y]";
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
        error = "Vec2 must have 2 components";
        return false;
    }
    out = math::vec2{vals[0], vals[1]};
    return true;
}

inline auto parse_vec3(const simdjson::dom::element& value, math::vec3& out, std::string& error) -> bool
{
    simdjson::dom::array arr;
    if(value.get(arr))
    {
        error = "Expected vec3 array [x,y,z]";
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
    if(vals.size() != 3)
    {
        error = "Vec3 must have 3 components";
        return false;
    }
    out = math::vec3{vals[0], vals[1], vals[2]};
    return true;
}

inline auto parse_color(const simdjson::dom::element& value, math::color& out, std::string& error) -> bool
{
    simdjson::dom::array arr;
    if(value.get(arr))
    {
        error = "Expected color array [r,g,b] or [r,g,b,a]";
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
    if(vals.size() < 3 || vals.size() > 4)
    {
        error = "Color must have 3 or 4 components";
        return false;
    }
    const float a = vals.size() == 4 ? vals[3] : 1.0f;
    out = math::color{vals[0], vals[1], vals[2], a};
    return true;
}

inline auto vec2_to_json(const math::vec2& v) -> std::string
{
    return fmt::format("[{:.6g},{:.6g}]", v.x, v.y);
}

inline auto vec3_to_json(const math::vec3& v) -> std::string
{
    return fmt::format("[{:.6g},{:.6g},{:.6g}]", v.x, v.y, v.z);
}

inline auto color_to_json(const math::color& c) -> std::string
{
    const math::vec4 v = c;
    return fmt::format("[{:.6g},{:.6g},{:.6g},{:.6g}]", v.x, v.y, v.z, v.w);
}

inline auto bool_to_json(bool value) -> const char*
{
    return value ? "true" : "false";
}

inline auto strings_to_json_array(const std::vector<std::string>& items) -> std::string
{
    std::string json = "[";
    for(size_t i = 0; i < items.size(); ++i)
    {
        if(i > 0)
        {
            json += ",";
        }
        json += make_json_string(items[i]);
    }
    json += "]";
    return json;
}

inline auto parse_bool(const simdjson::dom::element& value, bool& out, std::string& error) -> bool
{
    bool b = false;
    if(!value.get(b))
    {
        out = b;
        return true;
    }
    error = "Expected boolean";
    return false;
}

inline auto parse_int(const simdjson::dom::element& value, int& out, std::string& error) -> bool
{
    int64_t i = 0;
    if(!value.get(i))
    {
        out = static_cast<int>(i);
        return true;
    }
    double d = 0.0;
    if(!value.get(d))
    {
        out = static_cast<int>(d);
        return true;
    }
    error = "Expected integer";
    return false;
}

inline auto parse_string(const simdjson::dom::element& value, std::string& out, std::string& error) -> bool
{
    if(value.is_null())
    {
        out.clear();
        return true;
    }
    std::string_view view;
    if(value.get(view))
    {
        error = "Expected string or null";
        return false;
    }
    out.assign(view);
    return true;
}

inline auto parse_string_array(const simdjson::dom::element& value,
                               std::vector<std::string>& out,
                               std::string& error,
                               const char* key) -> bool
{
    simdjson::dom::array arr;
    if(value.get(arr))
    {
        error = std::string(key) + " must be an array of strings";
        return false;
    }
    for(auto el : arr)
    {
        std::string_view view;
        if(el.get(view))
        {
            error = std::string(key) + " must be strings";
            return false;
        }
        out.emplace_back(view);
    }
    return true;
}

inline auto read_string_array(const simdjson::dom::object& args,
                              const char* key,
                              std::vector<std::string>& out,
                              std::string& error,
                              bool required = true) -> bool
{
    simdjson::dom::element el;
    if(args[key].get(el))
    {
        if(required)
        {
            error = std::string("Missing ") + key;
            return false;
        }
        return true;
    }
    return parse_string_array(el, out, error, key);
}

inline auto read_required_array(const simdjson::dom::object& args,
                                const char* key,
                                simdjson::dom::array& out,
                                std::string& error) -> bool
{
    if(args[key].get(out))
    {
        error = std::string("Missing ") + key;
        return false;
    }
    return true;
}

inline auto read_object(const simdjson::dom::element& value, simdjson::dom::object& out, std::string& error) -> bool
{
    if(value.get(out))
    {
        error = "Each item must be an object";
        return false;
    }
    return true;
}

inline auto resolve_entity(scene& scn, const std::string& id, std::string& error) -> entt::handle
{
    auto entity = find_entity(scn, id);
    if(!entity)
    {
        error = "Entity not found: " + id;
    }
    return entity;
}

inline auto resolve_entities(scene& scn,
                             const std::vector<std::string>& ids,
                             std::vector<entt::handle>& out,
                             std::string& error) -> bool
{
    for(const auto& id : ids)
    {
        auto entity = resolve_entity(scn, id, error);
        if(!entity)
        {
            return false;
        }
        out.push_back(entity);
    }
    return true;
}

inline auto read_entity_ids(const simdjson::dom::object& args,
                            scene& scn,
                            std::vector<entt::handle>& out,
                            std::string& error,
                            bool required = true) -> bool
{
    std::vector<std::string> ids;
    if(!read_string_array(args, "entity_ids", ids, error, required))
    {
        return false;
    }
    return resolve_entities(scn, ids, out, error);
}

inline auto resolve_entity_id_or_ids(scene& scn,
                                     const simdjson::dom::object& args,
                                     std::vector<entt::handle>& out,
                                     std::string& error) -> bool
{
    out.clear();
    std::string single_id;
    if(read_string(args, "entity_id", single_id) && !single_id.empty())
    {
        auto entity = resolve_entity(scn, single_id, error);
        if(!entity)
        {
            return false;
        }
        out.push_back(entity);
    }
    if(!read_entity_ids(args, scn, out, error, false))
    {
        return false;
    }
    if(out.empty())
    {
        error = "Provide entity_id or entity_ids";
        return false;
    }
    return true;
}

inline auto asset_key_json(const std::string& key) -> std::string
{
    if(key.empty())
    {
        return "null";
    }
    return make_json_string(key);
}

template<typename T>
inline auto asset_handle_key_json(const asset_handle<T>& handle) -> std::string
{
    if(!handle)
    {
        return "null";
    }
    return make_json_string(handle.id());
}

template<typename Result>
inline auto apply_result_to_json(const Result& result) -> std::string
{
    return fmt::format(R"({{"ok":{},"applied":{},"unknown":{},"errors":{}}})",
                       bool_to_json(result.ok),
                       strings_to_json_array(result.applied),
                       strings_to_json_array(result.unknown),
                       strings_to_json_array(result.errors));
}

inline auto read_int64_or(const simdjson::dom::object& args, const char* key, int64_t default_val) -> int64_t
{
    int64_t value = default_val;
    if(args[key].get(value))
    {
        return default_val;
    }
    return value;
}

inline auto read_clamped_int64(const simdjson::dom::object& args,
                               const char* key,
                               int64_t default_val,
                               int64_t min_val) -> int64_t
{
    int64_t value = read_int64_or(args, key, default_val);
    if(value < min_val)
    {
        value = min_val;
    }
    return value;
}

inline auto read_timeout_ms(const simdjson::dom::object& args,
                            int64_t default_ms,
                            int64_t max_ms,
                            const char* key = "wait_ms") -> std::chrono::milliseconds
{
    int64_t wait_ms = read_int64_or(args, key, default_ms);
    if(wait_ms < 0)
    {
        wait_ms = 0;
    }
    if(wait_ms > max_ms)
    {
        wait_ms = max_ms;
    }
    return std::chrono::milliseconds(wait_ms);
}

inline auto project_info_json(const project_manager& pm) -> std::string
{
    if(!pm.has_open_project())
    {
        return R"({"open":false})";
    }
    const auto& info = pm.get_project_info();
    return fmt::format(R"({{"open":true,"name":{},"path":{},"guid":{}}})",
                       make_json_string(pm.get_name()),
                       make_json_string(fs::resolve_protocol("app:/").generic_string()),
                       make_json_string(info.project_guid));
}

inline auto project_info_json(rtti::context& ctx) -> std::string
{
    if(!ctx.has<project_manager>())
    {
        return R"({"open":false})";
    }
    return project_info_json(ctx.get_cached<project_manager>());
}

inline auto play_phase_from_ctx(rtti::context& ctx) -> std::string
{
    if(!ctx.has<play_mode>())
    {
        return "inactive";
    }
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_splash())
    {
        return "splash";
    }
    if(play.is_simulation_running())
    {
        return "running";
    }
    if(play.is_active())
    {
        return "active";
    }
    return "inactive";
}

inline auto active_scene_status_json(rtti::context& ctx) -> std::string
{
    const auto phase = play_phase_from_ctx(ctx);
    auto& em = ctx.get_cached<editing_manager>();
    auto* scn = em.get_active_scene(ctx);
    if(!scn || !scn->registry)
    {
        return fmt::format(R"({{"has_scene":false,"play_phase":{}}})", make_json_string(phase));
    }
    const auto entity_count = scn->registry->storage<entt::entity>().size();
    const auto source = scn->source ? scn->source.id() : std::string{};
    return fmt::format(R"({{"has_scene":true,"tag":{},"source":{},"entity_count":{},"play_phase":{}}})",
                       make_json_string(scn->tag),
                       make_json_string(source),
                       entity_count,
                       make_json_string(phase));
}

inline auto selection_to_json(const std::string& active_entity_id, const std::vector<std::string>& entity_ids)
    -> std::string
{
    return fmt::format(R"({{"active_entity_id":{},"entity_ids":{}}})",
                       active_entity_id.empty() ? "null" : make_json_string(active_entity_id),
                       strings_to_json_array(entity_ids));
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

struct transform_snapshot
{
    math::vec3 position{};
    math::vec3 rotation_euler{};
    math::vec3 scale{1.0f, 1.0f, 1.0f};
    bool has_position{false};
    bool has_rotation{false};
    bool has_scale{false};
};

inline auto read_transform_snapshot(const simdjson::dom::object& obj, transform_snapshot& out) -> void
{
    out.has_position = read_vec3(obj, "position", out.position);
    out.has_rotation = read_vec3(obj, "rotation_euler", out.rotation_euler);
    out.has_scale = read_vec3(obj, "scale", out.scale);
}

inline void apply_pose_direct(entt::handle entity, const transform_snapshot& pose, bool is_local)
{
    if(!entity || !entity.all_of<transform_component>())
    {
        return;
    }
    auto& transform = entity.get<transform_component>();
    if(is_local)
    {
        if(pose.has_position)
        {
            transform.set_position_local(pose.position);
        }
        if(pose.has_rotation)
        {
            transform.set_rotation_euler_local(pose.rotation_euler);
        }
        if(pose.has_scale)
        {
            transform.set_scale_local(pose.scale);
        }
        if(pose.has_position || pose.has_rotation || pose.has_scale)
        {
            prefab_override_context::mark_transform_as_changed(entity,
                                                              pose.has_position,
                                                              pose.has_rotation,
                                                              pose.has_scale,
                                                              false);
        }
        return;
    }
    if(pose.has_position)
    {
        transform.set_position_global(pose.position);
    }
    if(pose.has_rotation)
    {
        transform.set_rotation_euler_global(pose.rotation_euler);
    }
    if(pose.has_scale)
    {
        transform.set_scale_global(pose.scale);
    }
    if(pose.has_position || pose.has_rotation || pose.has_scale)
    {
        prefab_override_context::mark_transform_global_as_changed(entity,
                                                                 pose.has_position,
                                                                 pose.has_rotation,
                                                                 pose.has_scale,
                                                                 false);
    }
}

inline auto read_wait_ms(const simdjson::dom::object& args, int64_t default_ms = 1000) -> std::chrono::milliseconds
{
    return read_timeout_ms(args, default_ms, 15000);
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

inline auto ends_with_ci(std::string_view value, std::string_view suffix) -> bool
{
    if(suffix.size() > value.size())
    {
        return false;
    }
    const auto v = value.substr(value.size() - suffix.size());
    return to_lower_ascii(std::string(v)) == to_lower_ascii(std::string(suffix));
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
