#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <string_utils/utils.h>

namespace unravel
{
namespace
{


//-------------------------------------------------------------------------
/*

  _______ _____            _   _  _____ ______ ____  _____  __  __
 |__   __|  __ \     /\   | \ | |/ ____|  ____/ __ \|  __ \|  \/  |
    | |  | |__) |   /  \  |  \| | (___ | |__ | |  | | |__) | \  / |
    | |  |  _  /   / /\ \ | . ` |\___ \|  __|| |  | |  _  /| |\/| |
    | |  | | \ \  / ____ \| |\  |____) | |   | |__| | | \ \| |  | |
    |_|  |_|  \_\/_/    \_\_| \_|_____/|_|    \____/|_|  \_\_|  |_|


*/
//-------------------------------------------------------------------------
auto internal_m2n_get_children(entt::entity id) -> hpp::small_vector<entt::entity>
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& children = comp->get_children();
        hpp::small_vector<entt::entity> children_id;
        children_id.reserve(children.size());
        for(const auto& child : children)
        {
            children_id.emplace_back(child.entity());
        }
        return children_id;
    }

    return {};
}

// Helper structure carrying an entity and the count of path segments matched so far.
struct node_candidate
{
    entt::entity entity;
    size_t matched_index{}; // number of path segments matched so far
};

auto internal_m2n_get_child(entt::entity id, const std::string& path, bool recursive) -> entt::entity
{
    auto root = get_entity_from_id(id);
    if(!root || path.empty())
        return entt::null;

    // Tokenize the path once.
    const auto parts = string_utils::tokenize(path, "/");
    if(parts.empty())
        return entt::null;

    // Use a vector as a queue to reduce dynamic allocations.
    hpp::small_vector<node_candidate> queue;
    queue.reserve(4); // Reserve a reasonable number based on expected hierarchy size.
    queue.push_back({root, 0});

    // Process the vector as a queue.
    for(size_t idx = 0; idx < queue.size(); ++idx)
    {
        auto candidate = queue[idx];
        bool advanced = false;

        // Try matching current candidate.
        if(candidate.matched_index < parts.size())
        {
            if(auto tag_comp = safe_get_component<tag_component>(candidate.entity))
            {
                if(tag_comp->name == parts[candidate.matched_index])
                {
                    candidate.matched_index++;
                    advanced = true;
                    if(candidate.matched_index == parts.size())
                    {
                        return candidate.entity;
                    }
                }
            }
        }

        // Determine if we should enqueue children.
        // For recursive mode: allow children if no match yet or just advanced.
        // For non-recursive mode: allow children only if no match has started.
        bool should_enqueue = recursive ? (candidate.matched_index == 0 || advanced) : (candidate.matched_index == 0);

        if(should_enqueue)
        {
            if(auto trans_comp = safe_get_component<transform_component>(candidate.entity))
            {
                for(const auto& child : trans_comp->get_children())
                {
                    queue.push_back({child.entity(), candidate.matched_index});
                }
            }
        }
    }
    // No matching entity found.
    return entt::null;
}

auto internal_m2n_get_parent(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_parent().entity();
    }

    return {};
}

void internal_m2n_set_parent(entt::entity id, entt::entity new_parent, bool global_stays)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        auto parent = get_entity_from_id(new_parent);
        comp->set_parent(parent, global_stays);
    }
}

auto internal_m2n_get_position_global(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_position_global();
    }

    return {};
}

void internal_m2n_set_position_global(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_position_global(value);
    }
}

void internal_m2n_move_by_global(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->move_by_global(value);
    }
}

auto internal_m2n_get_position_local(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_position_local();
    }

    return {};
}

void internal_m2n_set_position_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_position_local(value);
    }
}

void internal_m2n_move_by_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->move_by_local(value);
    }
}

//--------------------------------------------------
auto internal_m2n_get_rotation_euler_global(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_rotation_euler_global();
    }

    return {};
}

void internal_m2n_rotate_by_euler_global(entt::entity id, const math::vec3& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_by_euler_global(amount);
    }
}

void internal_m2n_rotate_axis_global(entt::entity id, float degrees, const math::vec3& axis)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_axis_global(degrees, axis);
    }
}

auto internal_m2n_transform_vector_global(entt::entity id, const math::vec3& coord) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& global = comp->get_transform_global();
        return global.transform_coord(coord);
    }

    return {};
}

auto internal_m2n_inverse_transform_vector_global(entt::entity id, const math::vec3& coord) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& global = comp->get_transform_global();
        return global.inverse_transform_coord(coord);
    }

    return {};
}

auto internal_m2n_transform_direction_global(entt::entity id, const math::vec3& direction) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& global = comp->get_transform_global();
        return global.transform_normal(direction);
    }

    return {};
}

auto internal_m2n_inverse_transform_direction_global(entt::entity id, const math::vec3& direction) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& global = comp->get_transform_global();
        return global.inverse_transform_normal(direction);
    }

    return {};
}

void internal_m2n_look_at(entt::entity id, const math::vec3& point, const math::vec3& up)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->look_at(point, up);
    }
}

void internal_m2n_set_rotation_euler_global(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_rotation_euler_global(value);
    }
}

auto internal_m2n_get_rotation_euler_local(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_rotation_euler_local();
    }

    return {};
}

void internal_m2n_set_rotation_euler_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_rotation_euler_local(value);
    }
}

void internal_m2n_rotate_by_euler_local(entt::entity id, const math::vec3& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_by_euler_local(amount);
    }
}

auto internal_m2n_get_rotation_global(entt::entity id) -> math::quat
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_rotation_global();
    }

    return {};
}

void internal_m2n_set_rotation_global(entt::entity id, const math::quat& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_rotation_global(value);
    }
}

void internal_m2n_rotate_by_global(entt::entity id, const math::quat& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_by_global(amount);
    }
}

auto internal_m2n_get_rotation_local(entt::entity id) -> math::quat
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_rotation_local();
    }

    return {};
}

void internal_m2n_set_rotation_local(entt::entity id, const math::quat& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_rotation_local(value);
    }
}

void internal_m2n_rotate_by_local(entt::entity id, const math::quat& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_by_local(amount);
    }
}

//--------------------------------------------------
auto internal_m2n_get_scale_global(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_scale_global();
    }

    return {};
}

void internal_m2n_set_scale_global(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_scale_global(value);
    }
}

void internal_m2n_scale_by_global(entt::entity id, const math::vec3& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->scale_by_global(amount);
    }
}

auto internal_m2n_get_scale_local(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_scale_local();
    }

    return {};
}

void internal_m2n_set_scale_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_scale_local(value);
    }
}

void internal_m2n_scale_by_local(entt::entity id, const math::vec3& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->scale_by_local(amount);
    }
}

//--------------------------------------------------
auto internal_m2n_get_skew_global(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_skew_global();
    }

    return {};
}

void internal_m2n_setl_skew_globa(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_skew_global(value);
    }
}

auto internal_m2n_get_skew_local(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_skew_local();
    }

    return {};
}

void internal_m2n_set_skew_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_skew_local(value);
    }
}

} // namespace

void register_transform_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.TransformComponent");
    reg.add_internal_call("internal_m2n_get_children", dotnet_internal_call(internal_m2n_get_children));
    reg.add_internal_call("internal_m2n_get_child", dotnet_internal_call(internal_m2n_get_child));
    reg.add_internal_call("internal_m2n_get_parent", dotnet_internal_call(internal_m2n_get_parent));
    reg.add_internal_call("internal_m2n_set_parent", dotnet_internal_call(internal_m2n_set_parent));

    reg.add_internal_call("internal_m2n_get_position_global", dotnet_internal_call(internal_m2n_get_position_global));
    reg.add_internal_call("internal_m2n_set_position_global", dotnet_internal_call(internal_m2n_set_position_global));
    reg.add_internal_call("internal_m2n_move_by_global", dotnet_internal_call(internal_m2n_move_by_global));

    reg.add_internal_call("internal_m2n_get_position_local", dotnet_internal_call(internal_m2n_get_position_local));
    reg.add_internal_call("internal_m2n_set_position_local", dotnet_internal_call(internal_m2n_set_position_local));
    reg.add_internal_call("internal_m2n_move_by_local", dotnet_internal_call(internal_m2n_move_by_local));

    // Euler
    reg.add_internal_call("internal_m2n_get_rotation_euler_global",
                            dotnet_internal_call(internal_m2n_get_rotation_euler_global));
    reg.add_internal_call("internal_m2n_set_rotation_euler_global",
                            dotnet_internal_call(internal_m2n_set_rotation_euler_global));
    reg.add_internal_call("internal_m2n_rotate_by_euler_global",
                            dotnet_internal_call(internal_m2n_rotate_by_euler_global));

    reg.add_internal_call("internal_m2n_get_rotation_euler_local",
                            dotnet_internal_call(internal_m2n_get_rotation_euler_local));
    reg.add_internal_call("internal_m2n_set_rotation_euler_local",
                            dotnet_internal_call(internal_m2n_set_rotation_euler_local));
    reg.add_internal_call("internal_m2n_rotate_by_euler_local", dotnet_internal_call(internal_m2n_rotate_by_euler_local));

    // Quat
    reg.add_internal_call("internal_m2n_get_rotation_global", dotnet_internal_call(internal_m2n_get_rotation_global));
    reg.add_internal_call("internal_m2n_set_rotation_global", dotnet_internal_call(internal_m2n_set_rotation_global));
    reg.add_internal_call("internal_m2n_rotate_by_global", dotnet_internal_call(internal_m2n_rotate_by_global));

    reg.add_internal_call("internal_m2n_get_rotation_local", dotnet_internal_call(internal_m2n_get_rotation_local));
    reg.add_internal_call("internal_m2n_set_rotation_local", dotnet_internal_call(internal_m2n_set_rotation_local));
    reg.add_internal_call("internal_m2n_rotate_by_local", dotnet_internal_call(internal_m2n_rotate_by_local));

    // Other
    reg.add_internal_call("internal_m2n_rotate_axis_global", dotnet_internal_call(internal_m2n_rotate_axis_global));
    reg.add_internal_call("internal_m2n_look_at", dotnet_internal_call(internal_m2n_look_at));
    reg.add_internal_call("internal_m2n_transform_vector_global",
                            dotnet_internal_call(internal_m2n_transform_vector_global));
    reg.add_internal_call("internal_m2n_inverse_transform_vector_global",
                            dotnet_internal_call(internal_m2n_inverse_transform_vector_global));

    reg.add_internal_call("internal_m2n_transform_direction_global",
                            dotnet_internal_call(internal_m2n_transform_direction_global));
    reg.add_internal_call("internal_m2n_inverse_transform_direction_global",
                            dotnet_internal_call(internal_m2n_inverse_transform_direction_global));

    // Scale
    reg.add_internal_call("internal_m2n_get_scale_global", dotnet_internal_call(internal_m2n_get_scale_global));
    reg.add_internal_call("internal_m2n_set_scale_global", dotnet_internal_call(internal_m2n_set_scale_global));
    reg.add_internal_call("internal_m2n_scale_by_global", dotnet_internal_call(internal_m2n_scale_by_local));

    reg.add_internal_call("internal_m2n_get_scale_local", dotnet_internal_call(internal_m2n_get_scale_local));
    reg.add_internal_call("internal_m2n_set_scale_local", dotnet_internal_call(internal_m2n_set_scale_local));
    reg.add_internal_call("internal_m2n_scale_by_local", dotnet_internal_call(internal_m2n_scale_by_local));

    // Skew
    reg.add_internal_call("internal_m2n_get_skew_global", dotnet_internal_call(internal_m2n_get_skew_global));
    reg.add_internal_call("internal_m2n_set_skew_globa", dotnet_internal_call(internal_m2n_setl_skew_globa));
    reg.add_internal_call("internal_m2n_get_skew_local", dotnet_internal_call(internal_m2n_get_skew_local));
    reg.add_internal_call("internal_m2n_set_skew_local", dotnet_internal_call(internal_m2n_set_skew_local));
}

} // namespace unravel
