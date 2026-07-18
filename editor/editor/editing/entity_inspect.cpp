#include "entity_inspect.h"

#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/scripting/ecs/components/script_component.h>
#include <hpp/utility.hpp>
#include <math/math.h>
#include <uuid/uuid.h>

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

auto entity_to_summary_json(entt::handle entity, int depth, int max_depth) -> std::string
{
    if(!entity)
    {
        return "null";
    }
    std::string name;
    std::string tag;
    if(auto* tag_comp = entity.try_get<tag_component>())
    {
        name = tag_comp->name;
        tag = tag_comp->tag;
    }
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
        if(auto parent = transform->get_parent())
        {
            parent_id = entity_id_string(parent);
        }
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
    std::stringstream stream;
    save_to_stream(stream, entt::const_handle{entity});
    return stream.str();
}

} // namespace unravel
