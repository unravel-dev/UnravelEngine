#include "entity_inspect.h"

#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/scripting/ecs/components/script_component.h>
#include <hpp/utility.hpp>
#include <math/math.h>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <logging/logging.h>
#include <sstream>

namespace unravel
{
namespace
{

auto json_escape(const std::string& value) -> std::string
{
    std::string out;
    out.reserve(value.size() + 8);
    for(char c : value)
    {
        switch(c)
        {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

auto make_json_string(const std::string& value) -> std::string
{
    return "\"" + json_escape(value) + "\"";
}

auto entity_name_tag(entt::handle entity, std::string& name, std::string& tag) -> void
{
    name.clear();
    tag.clear();
    if(auto* tag_comp = entity.try_get<tag_component>())
    {
        name = tag_comp->name;
        tag = tag_comp->tag;
    }
}

auto entity_parent_id(entt::handle entity) -> std::string
{
    if(auto* transform = entity.try_get<transform_component>())
    {
        if(auto parent = transform->get_parent())
        {
            return entity_id_string(parent);
        }
    }
    return {};
}

} // namespace

auto find_entity_by_id(scene& scn, const std::string& id) -> entt::handle
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

auto entity_id_string(entt::handle entity) -> std::string
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

auto collect_component_pretty_names(entt::handle entity) -> std::vector<std::string>
{
    std::vector<std::string> names;
    if(!entity)
    {
        return names;
    }
    hpp::for_each_tuple_type<all_inspectable_components>(
        [&](auto index)
        {
            using ctype = std::tuple_element_t<decltype(index)::value, all_inspectable_components>;
            if(entity.all_of<ctype>())
            {
                auto type = entt::resolve<ctype>();
                names.emplace_back(entt::get_pretty_name(type));
            }
        });
    if(entity.all_of<script_component>())
    {
        auto type = entt::resolve<script_component>();
        names.emplace_back(entt::get_pretty_name(type));
    }
    return names;
}

auto entity_has_component_pretty_name(entt::handle entity, const std::string& component_pretty_name) -> bool
{
    if(!entity || component_pretty_name.empty())
    {
        return false;
    }
    const auto names = collect_component_pretty_names(entity);
    return std::find(names.begin(), names.end(), component_pretty_name) != names.end();
}

auto entity_has_script_type(entt::handle entity, const std::string& script_type_name) -> bool
{
    if(!entity || script_type_name.empty())
    {
        return false;
    }
    auto* sc = entity.try_get<script_component>();
    if(!sc)
    {
        return false;
    }
    for(const auto& obj : sc->get_script_components())
    {
        if(!obj.pinned)
        {
            continue;
        }
        if(obj.pinned->get_object().get_type().get_fullname() == script_type_name)
        {
            return true;
        }
    }
    return false;
}

auto entity_to_lean_json(entt::handle entity, bool include_parent_id) -> std::string
{
    if(!entity)
    {
        return "null";
    }
    std::string name;
    std::string tag;
    entity_name_tag(entity, name, tag);
    if(!include_parent_id)
    {
        return fmt::format(R"({{"id":{},"name":{}}})",
                           make_json_string(entity_id_string(entity)),
                           make_json_string(name));
    }
    const auto parent_id = entity_parent_id(entity);
    return fmt::format(R"({{"id":{},"name":{},"parent_id":{}}})",
                       make_json_string(entity_id_string(entity)),
                       make_json_string(name),
                       parent_id.empty() ? "null" : make_json_string(parent_id));
}

auto entity_to_pose_json(entt::handle entity) -> std::string
{
    if(!entity)
    {
        return "null";
    }
    std::string name;
    std::string tag;
    entity_name_tag(entity, name, tag);
    std::string parent_id;
    bool active = true;
    math::vec3 position_world{0.0f};
    math::vec3 rotation_world{0.0f};
    math::vec3 scale_world{1.0f};
    math::vec3 position_local{0.0f};
    math::vec3 rotation_local{0.0f};
    math::vec3 scale_local{1.0f};
    if(auto* transform = entity.try_get<transform_component>())
    {
        active = transform->is_active();
        position_world = transform->get_position_global();
        rotation_world = transform->get_rotation_euler_global();
        scale_world = transform->get_scale_global();
        position_local = transform->get_position_local();
        rotation_local = transform->get_rotation_euler_local();
        scale_local = transform->get_scale_local();
        parent_id = entity_parent_id(entity);
    }
    return fmt::format(
        R"({{"id":{},"name":{},"tag":{},"active":{},"parent_id":{},"position":[{:.6g},{:.6g},{:.6g}],"rotation_euler":[{:.6g},{:.6g},{:.6g}],"scale":[{:.6g},{:.6g},{:.6g}],"position_local":[{:.6g},{:.6g},{:.6g}],"rotation_euler_local":[{:.6g},{:.6g},{:.6g}],"scale_local":[{:.6g},{:.6g},{:.6g}]}})",
        make_json_string(entity_id_string(entity)),
        make_json_string(name),
        make_json_string(tag),
        active ? "true" : "false",
        parent_id.empty() ? "null" : make_json_string(parent_id),
        position_world.x,
        position_world.y,
        position_world.z,
        rotation_world.x,
        rotation_world.y,
        rotation_world.z,
        scale_world.x,
        scale_world.y,
        scale_world.z,
        position_local.x,
        position_local.y,
        position_local.z,
        rotation_local.x,
        rotation_local.y,
        rotation_local.z,
        scale_local.x,
        scale_local.y,
        scale_local.z);
}

auto entity_hierarchy_node_json(entt::handle entity,
                                int depth,
                                int max_depth,
                                size_t& nodes_emitted,
                                size_t node_limit,
                                bool& truncated) -> std::string
{
    if(!entity)
    {
        return "null";
    }
    if(nodes_emitted >= node_limit)
    {
        truncated = true;
        return "null";
    }
    ++nodes_emitted;
    std::string name;
    std::string tag;
    entity_name_tag(entity, name, tag);
    std::string children_json = "[]";
    if(depth < max_depth)
    {
        if(auto* transform = entity.try_get<transform_component>())
        {
            children_json = "[";
            bool first = true;
            for(auto child : transform->get_children())
            {
                if(nodes_emitted >= node_limit)
                {
                    truncated = true;
                    break;
                }
                auto child_json =
                    entity_hierarchy_node_json(child, depth + 1, max_depth, nodes_emitted, node_limit, truncated);
                if(child_json == "null")
                {
                    continue;
                }
                if(!first)
                {
                    children_json += ",";
                }
                first = false;
                children_json += child_json;
            }
            children_json += "]";
        }
    }
    return fmt::format(R"({{"id":{},"name":{},"children":{}}})",
                       make_json_string(entity_id_string(entity)),
                       make_json_string(name),
                       children_json);
}

auto entity_to_summary_json(entt::handle entity, int depth, int max_depth) -> std::string
{
    if(!entity)
    {
        return "null";
    }
    std::string name;
    std::string tag;
    entity_name_tag(entity, name, tag);
    std::string parent_id;
    bool active = true;
    math::vec3 position_world{0.0f};
    math::vec3 rotation_world{0.0f};
    math::vec3 scale_world{1.0f};
    math::vec3 position_local{0.0f};
    math::vec3 rotation_local{0.0f};
    math::vec3 scale_local{1.0f};
    if(auto* transform = entity.try_get<transform_component>())
    {
        active = transform->is_active();
        position_world = transform->get_position_global();
        rotation_world = transform->get_rotation_euler_global();
        scale_world = transform->get_scale_global();
        position_local = transform->get_position_local();
        rotation_local = transform->get_rotation_euler_local();
        scale_local = transform->get_scale_local();
        parent_id = entity_parent_id(entity);
    }
    auto components = collect_component_pretty_names(entity);
    std::string components_json = "[";
    for(size_t i = 0; i < components.size(); ++i)
    {
        if(i > 0)
        {
            components_json += ",";
        }
        components_json += make_json_string(components[i]);
    }
    components_json += "]";
    std::string children_json = "[]";
    if(depth < max_depth)
    {
        if(auto* transform = entity.try_get<transform_component>())
        {
            children_json = "[";
            const auto& children = transform->get_children();
            for(size_t i = 0; i < children.size(); ++i)
            {
                if(i > 0)
                {
                    children_json += ",";
                }
                children_json += entity_to_summary_json(children[i], depth + 1, max_depth);
            }
            children_json += "]";
        }
    }
    return fmt::format(
        R"({{"id":{},"name":{},"tag":{},"active":{},"parent_id":{},"position":[{:.6g},{:.6g},{:.6g}],"rotation_euler":[{:.6g},{:.6g},{:.6g}],"scale":[{:.6g},{:.6g},{:.6g}],"position_local":[{:.6g},{:.6g},{:.6g}],"rotation_euler_local":[{:.6g},{:.6g},{:.6g}],"scale_local":[{:.6g},{:.6g},{:.6g}],"components":{},"children":{}}})",
        make_json_string(entity_id_string(entity)),
        make_json_string(name),
        make_json_string(tag),
        active ? "true" : "false",
        parent_id.empty() ? "null" : make_json_string(parent_id),
        position_world.x,
        position_world.y,
        position_world.z,
        rotation_world.x,
        rotation_world.y,
        rotation_world.z,
        scale_world.x,
        scale_world.y,
        scale_world.z,
        position_local.x,
        position_local.y,
        position_local.z,
        rotation_local.x,
        rotation_local.y,
        rotation_local.z,
        scale_local.x,
        scale_local.y,
        scale_local.z,
        components_json,
        children_json);
}

auto entity_components_serialized(entt::handle entity) -> std::string
{
    if(!entity)
    {
        return {};
    }
    // Full entity persistence path — pushes save_context (required for Transform links).
    std::stringstream stream;
    save_to_stream(stream, entt::const_handle{entity});
    return stream.str();
}

} // namespace unravel
