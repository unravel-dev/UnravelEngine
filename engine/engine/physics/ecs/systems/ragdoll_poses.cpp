#include "ragdoll_poses.h"

#include <engine/ecs/components/basic_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace ragdoll_poses_detail
{
/// Bodies slower than this, in meters per second, count as resting.
constexpr float rest_speed = 0.15f;
/// Below this a blend back to the animation finishes at once.
constexpr float min_blend_seconds = 1e-3f;
/// Local transforms closer than this to what the ragdoll wrote are still its write.
constexpr float written_epsilon = 1e-6f;
/// An animated bone moving faster than this, in meters per second, was placed (a teleport, a get-up, an entity
/// turned back on): a body that starts simulating then inherits no velocity from it.
constexpr float teleport_speed = 30.0f;

auto get_parent(entt::handle entity) -> entt::handle
{
    const auto* transform = entity ? entity.try_get<transform_component>() : nullptr;
    return transform ? transform->get_parent() : entt::handle{};
}

auto get_depth(entt::handle entity, entt::handle owner) -> int
{
    int depth = 0;
    for(entt::handle current = get_parent(entity); current && current != owner; current = get_parent(current))
    {
        ++depth;
    }
    return depth;
}

auto is_same_local(const math::transform& lhs, const math::transform& rhs) -> bool
{
    const auto is_near = [](const math::vec3& a, const math::vec3& b)
    {
        return math::all(math::lessThanEqual(math::abs(a - b), math::vec3(written_epsilon)));
    };
    const math::quat qa = lhs.get_rotation();
    const math::quat qb = rhs.get_rotation();
    const bool same_rotation = std::abs(qa.x - qb.x) <= written_epsilon && std::abs(qa.y - qb.y) <= written_epsilon &&
                               std::abs(qa.z - qb.z) <= written_epsilon && std::abs(qa.w - qb.w) <= written_epsilon;
    return same_rotation && is_near(lhs.get_position(), rhs.get_position()) && is_near(lhs.get_scale(), rhs.get_scale());
}

auto make_transform(const math::vec3& position, const math::quat& rotation, const math::vec3& scale) -> math::transform
{
    math::transform result;
    result.set_position(position);
    result.set_rotation(rotation);
    result.set_scale(scale);
    return result;
}

/// The rest pose of every armature node in the model's space, worked out on demand.
class bind_pose_view
{
public:
    bind_pose_view(const model_component& model, entt::handle owner)
        : model_(model)
        , owner_(owner)
    {
        const auto& armature = model.get_armature_entities();
        cache_.resize(armature.size());
        is_cached_.assign(armature.size(), false);
        for(size_t i = 0; i < armature.size(); ++i)
        {
            if(armature[i])
            {
                index_of_[armature[i].entity()] = int(i);
            }
        }
    }

    auto index_of(entt::handle entity) const -> int
    {
        const auto it = entity ? index_of_.find(entity.entity()) : index_of_.end();
        return it != index_of_.end() ? it->second : -1;
    }

    auto get_model_space(int index) -> const math::transform&
    {
        if(is_cached_[size_t(index)])
        {
            return cache_[size_t(index)];
        }
        const auto node = model_.get_armature_by_index(size_t(index));
        math::transform local = node.get<transform_component>().get_transform_local();
        if(const auto* bind = model_.get_bind_pose().find_node(size_t(index)))
        {
            local = bind->transform;
        }
        const entt::handle parent = get_parent(node);
        const int parent_index = parent && parent != owner_ ? index_of(parent) : -1;
        if(parent_index >= 0)
        {
            cache_[size_t(index)] = get_model_space(parent_index) * local;
        }
        else if(parent && parent != owner_)
        {
            // A node between the armature and the model entity that the armature does not list.
            cache_[size_t(index)] = parent.get<transform_component>().get_transform_local() * local;
        }
        else
        {
            cache_[size_t(index)] = local;
        }
        is_cached_[size_t(index)] = true;
        return cache_[size_t(index)];
    }

private:
    const model_component& model_;
    entt::handle owner_;
    std::unordered_map<entt::entity, int> index_of_;
    std::vector<math::transform> cache_;
    std::vector<bool> is_cached_;
};


auto find_body_of_entity(const ragdoll_runtime& runtime, entt::handle entity) -> int
{
    for(size_t i = 0; i < runtime.bodies.size(); ++i)
    {
        if(runtime.bodies[i].bone == entity)
        {
            return int(i);
        }
    }
    return -1;
}

void find_parent_bodies(ragdoll_runtime& runtime, entt::handle owner)
{
    for(auto& body : runtime.bodies)
    {
        body.parent = -1;
        for(entt::handle current = get_parent(body.bone); current && current != owner; current = get_parent(current))
        {
            const int index = find_body_of_entity(runtime, current);
            if(index >= 0)
            {
                body.parent = index;
                break;
            }
        }
    }
}

void order_bodies(ragdoll_runtime& runtime, entt::handle owner)
{
    runtime.write_order.clear();
    std::vector<int> depths(runtime.bodies.size(), 0);
    for(size_t i = 0; i < runtime.bodies.size(); ++i)
    {
        if(runtime.bodies[i].bone)
        {
            depths[i] = get_depth(runtime.bodies[i].bone, owner);
            runtime.write_order.push_back(int(i));
        }
    }
    std::stable_sort(runtime.write_order.begin(),
                     runtime.write_order.end(),
                     [&](int lhs, int rhs)
                     {
                         return depths[size_t(lhs)] < depths[size_t(rhs)];
                     });
}

/// The topmost node above the root body, below the model entity: it carries the whole skeleton.
void find_root_follower(ragdoll_runtime& runtime, entt::handle owner)
{
    runtime.root_follower = {};
    if(runtime.write_order.empty())
    {
        return;
    }
    const auto& root = runtime.bodies[size_t(runtime.write_order.front())];
    entt::handle topmost{};
    for(entt::handle current = get_parent(root.bone); current && current != owner; current = get_parent(current))
    {
        topmost = current;
    }
    runtime.root_follower = topmost;
}

/**
 * @brief The animated pose of body @p index in the world, below @p above: the parent body's animated pose, or the
 *        model entity's pose for the root body. Bones between the two are taken as they stand, the root follower as
 *        animated.
 */
auto compose_anim_world(const ragdoll_runtime& runtime, int index, entt::handle owner, const math::transform& above)
    -> math::transform
{
    const auto& body = runtime.bodies[size_t(index)];
    const entt::handle stop = body.parent >= 0 ? runtime.bodies[size_t(body.parent)].bone : owner;
    math::transform below = body.anim_local;
    for(entt::handle current = get_parent(body.bone); current && current != stop && current != owner;
        current = get_parent(current))
    {
        const bool is_follower = current == runtime.root_follower;
        below = (is_follower ? runtime.root_follower_anim_local : current.get<transform_component>().get_transform_local()) *
                below;
    }
    return above * below;
}

/// What the animated pose of body @p index hangs from: its parent body's animated pose, else the model entity.
auto get_anim_parent_world(const ragdoll_runtime& runtime, int index, const math::transform& owner_world)
    -> const math::transform&
{
    const int parent = runtime.bodies[size_t(index)].parent;
    return parent >= 0 ? runtime.bodies[size_t(parent)].anim_world : owner_world;
}

/// Angular velocity that turns @p from into @p to over @p seconds.
auto get_angular_velocity(const math::quat& from, const math::quat& to, float seconds) -> math::vec3
{
    math::quat delta = math::normalize(to * glm::inverse(from));
    if(delta.w < 0.0f)
    {
        delta = -delta;
    }
    const float angle = 2.0f * std::acos(math::clamp(delta.w, -1.0f, 1.0f));
    const float sine = std::sqrt(math::max(1.0f - delta.w * delta.w, 0.0f));
    if(sine < 1e-6f || seconds <= 0.0f)
    {
        return math::vec3{0.0f};
    }
    const math::vec3 axis{delta.x / sine, delta.y / sine, delta.z / sine};
    return axis * (angle / seconds);
}

/// The velocity of the animated pose, which a body keeps when it starts simulating; a jump faster than any motion
/// (a teleport) gives none.
void update_anim_velocity(ragdoll_body_runtime& body, const math::transform& world, float frame_seconds)
{
    if(!body.has_anim_pose || frame_seconds <= 0.0f)
    {
        return;
    }
    const math::vec3 velocity = (world.get_position() - body.anim_world.get_position()) / frame_seconds;
    if(math::length(velocity) > teleport_speed)
    {
        body.anim_linear_velocity = math::vec3{0.0f};
        body.anim_angular_velocity = math::vec3{0.0f};
        return;
    }
    body.anim_linear_velocity = velocity;
    body.anim_angular_velocity = get_angular_velocity(body.anim_world.get_rotation(), world.get_rotation(), frame_seconds);
}

void capture_one(ragdoll_component& ragdoll, float frame_seconds)
{
    auto& runtime = ragdoll.get_runtime();
    const auto owner = ragdoll.get_owner();
    for(const int index : runtime.write_order)
    {
        auto& body = runtime.bodies[size_t(index)];
        const auto& local = body.bone.get<transform_component>().get_transform_local();
        if(!body.has_written || !is_same_local(local, body.written_local))
        {
            body.anim_local = local;
            body.has_written = false;
        }
    }
    if(runtime.root_follower)
    {
        const auto& local = runtime.root_follower.get<transform_component>().get_transform_local();
        if(!runtime.root_follower_has_written || !is_same_local(local, runtime.root_follower_written_local))
        {
            runtime.root_follower_anim_local = local;
            runtime.root_follower_has_written = false;
        }
    }
    const math::transform owner_world = owner.get<transform_component>().get_transform_global();
    // Parents come first in the write order, so each body hangs from its parent's pose of this frame.
    for(const int index : runtime.write_order)
    {
        auto& body = runtime.bodies[size_t(index)];
        const math::transform world =
            compose_anim_world(runtime, index, owner, get_anim_parent_world(runtime, index, owner_world));
        update_anim_velocity(body, world, frame_seconds);
        body.anim_world = world;
        body.has_anim_pose = true;
    }
}

/// Where a body's physics shows: between the last two steps while it simulates, its snapshot while it blends back.
auto get_source_pose(const ragdoll_body_runtime& body, float step_fraction, const math::transform& anim_world)
    -> math::transform
{
    if(body.simulated && body.has_physics_pose)
    {
        const math::vec3 position =
            math::mix(body.world_previous.get_position(), body.world.get_position(), step_fraction);
        const math::quat rotation =
            math::slerp(body.world_previous.get_rotation(), body.world.get_rotation(), step_fraction);
        return make_transform(position, rotation, math::vec3{1.0f});
    }
    if(body.simulated)
    {
        return anim_world;
    }
    return body.snapshot_world;
}

void advance_weights(ragdoll_component& ragdoll, float frame_seconds)
{
    auto& runtime = ragdoll.get_runtime();
    float speed = 0.0f;
    int simulated = 0;
    for(auto& body : runtime.bodies)
    {
        if(body.simulated)
        {
            body.weight = 1.0f;
            speed += math::length(body.linear_velocity);
            ++simulated;
        }
        else if(body.weight > 0.0f)
        {
            body.weight = body.blend_time > min_blend_seconds ? body.weight - frame_seconds / body.blend_time : 0.0f;
            body.weight = math::max(body.weight, 0.0f);
        }
    }
    const bool is_resting = simulated > 0 && speed / float(simulated) < rest_speed;
    runtime.rest_time = is_resting ? runtime.rest_time + frame_seconds : 0.0f;
}

void write_root_follower(ragdoll_runtime& runtime, float step_fraction, const math::transform& root_anim_world)
{
    if(!runtime.root_follower || runtime.write_order.empty())
    {
        return;
    }
    const auto& root = runtime.bodies[size_t(runtime.write_order.front())];
    auto& transform = runtime.root_follower.get<transform_component>();
    if(root.weight <= 0.0f)
    {
        if(runtime.root_follower_has_written)
        {
            transform.set_transform_local(runtime.root_follower_anim_local);
            runtime.root_follower_has_written = false;
        }
        return;
    }
    const entt::handle parent = get_parent(runtime.root_follower);
    const math::transform parent_world =
        parent ? parent.get<transform_component>().get_transform_global() : math::transform{};
    const math::transform anim_world = parent_world * runtime.root_follower_anim_local;
    const math::vec3 shift =
        (get_source_pose(root, step_fraction, root_anim_world).get_position() - root_anim_world.get_position()) *
        root.weight;
    math::transform local = runtime.root_follower_anim_local;
    local.set_position(parent_world.inverse_transform_coord(anim_world.get_position() + shift));
    transform.set_transform_local(local);
    runtime.root_follower_written_local = transform.get_transform_local();
    runtime.root_follower_has_written = true;
}

void write_body(ragdoll_body_runtime& body, float step_fraction, const math::transform& anim_world)
{
    auto& transform = body.bone.get<transform_component>();
    if(body.weight <= 0.0f)
    {
        if(body.has_written)
        {
            transform.set_transform_local(body.anim_local);
            body.has_written = false;
        }
        return;
    }
    const math::transform source = get_source_pose(body, step_fraction, anim_world);
    const entt::handle parent = get_parent(body.bone);
    const math::transform parent_world =
        parent ? parent.get<transform_component>().get_transform_global() : math::transform{};
    const math::quat physics_rotation = math::normalize(glm::inverse(parent_world.get_rotation()) * source.get_rotation());
    math::transform local = body.anim_local;
    local.set_rotation(math::normalize(math::slerp(body.anim_local.get_rotation(), physics_rotation, body.weight)));
    if(body.parent < 0)
    {
        // Only the root body takes its position from physics: the others keep their bone lengths exactly. The mix
        // is made in the world: the parent (the root follower) already carries its share of the shift, so a mix of
        // locals would count that share twice.
        const math::vec3 shown_position = math::mix(anim_world.get_position(), source.get_position(), body.weight);
        local.set_position(parent_world.inverse_transform_coord(shown_position));
    }
    transform.set_transform_local(local);
    body.written_local = transform.get_transform_local();
    body.has_written = true;
}

void show_one(ragdoll_component& ragdoll, float step_fraction, float frame_seconds)
{
    auto& runtime = ragdoll.get_runtime();
    advance_weights(ragdoll, frame_seconds);
    if(!runtime.write_order.empty())
    {
        write_root_follower(runtime, step_fraction, runtime.bodies[size_t(runtime.write_order.front())].anim_world);
    }
    for(const int index : runtime.write_order)
    {
        auto& body = runtime.bodies[size_t(index)];
        write_body(body, step_fraction, body.anim_world);
    }
    runtime.shown_owner_world = ragdoll.get_owner().get<transform_component>().get_transform_global();
    runtime.shown_step_fraction = step_fraction;
    runtime.has_shown = true;
}

/// Some body shows physics, simulating or blending back to the animation.
auto is_showing_physics(const ragdoll_runtime& runtime) -> bool
{
    return std::any_of(runtime.bodies.begin(),
                       runtime.bodies.end(),
                       [](const ragdoll_body_runtime& body)
                       {
                           return body.weight > 0.0f;
                       });
}

void show_again_if_moved(ragdoll_component& ragdoll)
{
    auto& runtime = ragdoll.get_runtime();
    const auto owner = ragdoll.get_owner();
    const math::transform owner_world = owner.get<transform_component>().get_transform_global();
    if(!runtime.has_shown || owner_world == runtime.shown_owner_world)
    {
        return;
    }
    runtime.shown_owner_world = owner_world;
    if(runtime.write_order.empty() || !is_showing_physics(runtime))
    {
        return;
    }
    // The animated pose moved with the character, so it is composed again here, into a pose list of its own: the one
    // the fixed steps read stays as it is, and the next frame measures the animation's velocity from it.
    std::vector<math::transform> moved(runtime.bodies.size());
    for(const int index : runtime.write_order)
    {
        const int parent = runtime.bodies[size_t(index)].parent;
        const math::transform& above = parent >= 0 ? moved[size_t(parent)] : owner_world;
        moved[size_t(index)] = compose_anim_world(runtime, index, owner, above);
    }
    write_root_follower(runtime, runtime.shown_step_fraction, moved[size_t(runtime.write_order.front())]);
    for(const int index : runtime.write_order)
    {
        write_body(runtime.bodies[size_t(index)], runtime.shown_step_fraction, moved[size_t(index)]);
    }
}
} // namespace ragdoll_poses_detail

auto get_bind_poses_in_model_space(const model_component& model, entt::handle model_entity)
    -> std::vector<math::transform>
{
    const auto& armature = model.get_armature_entities();
    std::vector<math::transform> poses(armature.size());
    ragdoll_poses_detail::bind_pose_view view(model, model_entity);
    for(size_t i = 0; i < armature.size(); ++i)
    {
        // A node the armature lost (a stripped bone) has no pose of its own.
        poses[i] = armature[i] ? view.get_model_space(int(i)) : math::transform{};
    }
    return poses;
}

auto find_ragdoll_bone_index(const model_component& model, const std::string& name) -> int
{
    const int index = model.get_armature_index_by_name_cached(name);
    return index >= 0 ? index : model.get_armature_index_by_base_name_cached(name);
}

auto find_ragdoll_bone(const model_component& model, const std::string& name) -> entt::handle
{
    const int index = find_ragdoll_bone_index(model, name);
    return index >= 0 ? model.get_armature_by_index(size_t(index)) : entt::handle{};
}

auto prepare_ragdoll_runtime(ragdoll_component& ragdoll) -> bool
{
    auto& runtime = ragdoll.get_runtime();
    const auto owner = ragdoll.get_owner();
    const auto* model = owner ? owner.try_get<model_component>() : nullptr;
    if(model == nullptr || model->get_armature_entities().empty())
    {
        runtime.is_prepared = false;
        return false;
    }
    const auto& bodies = ragdoll.get_bodies();
    if(runtime.is_prepared && !ragdoll.needs_rebuild() && runtime.armature_version == model->get_armature_version() &&
       runtime.bodies.size() == bodies.size())
    {
        return true;
    }
    // What play gave each body (simulated flags, weights, poses, velocities) stays: a definition changed during play
    // must not stop a falling ragdoll in mid-air.
    ragdoll.fit_runtime_to_definition();
    ragdoll_poses_detail::bind_pose_view bind(*model, owner);
    for(size_t i = 0; i < bodies.size(); ++i)
    {
        auto& body = runtime.bodies[i];
        const int index = find_ragdoll_bone_index(*model, bodies[i].bone);
        const entt::handle bone = index >= 0 ? model->get_armature_by_index(size_t(index)) : entt::handle{};
        if(body.bone != bone)
        {
            body.has_written = false;
            body.has_anim_pose = false;
        }
        body.bone = bone;
        body.bind_model = index >= 0 ? bind.get_model_space(index) : math::transform{};
    }
    ragdoll_poses_detail::find_parent_bodies(runtime, owner);
    ragdoll_poses_detail::order_bodies(runtime, owner);
    ragdoll_poses_detail::find_root_follower(runtime, owner);
    runtime.root_follower_has_written = false;
    if(!runtime.write_order.empty())
    {
        const auto& root = runtime.bodies[size_t(runtime.write_order.front())];
        const math::quat inverse_rest = glm::inverse(root.bind_model.get_rotation());
        runtime.root_forward_local = math::normalize(inverse_rest * math::vec3{0.0f, 0.0f, 1.0f});
        runtime.root_up_local = math::normalize(inverse_rest * math::vec3{0.0f, 1.0f, 0.0f});
    }
    runtime.armature_version = model->get_armature_version();
    runtime.is_prepared = true;
    runtime.is_built = false;
    return true;
}

void capture_ragdoll_animations(entt::registry& registry, float frame_seconds)
{
    APP_SCOPE_PERF("Physics/Ragdolls/Capture Animation");
    registry.view<ragdoll_component, active_component>().each(
        [&](auto /*entity*/, ragdoll_component& ragdoll, auto& /*active*/)
        {
            if(prepare_ragdoll_runtime(ragdoll))
            {
                ragdoll_poses_detail::capture_one(ragdoll, frame_seconds);
            }
        });
}

void show_ragdolls(entt::registry& registry, float step_fraction, float frame_seconds)
{
    APP_SCOPE_PERF("Physics/Ragdolls/Show");
    registry.view<ragdoll_component, active_component>().each(
        [&](auto /*entity*/, ragdoll_component& ragdoll, auto& /*active*/)
        {
            if(ragdoll.get_runtime().is_built)
            {
                ragdoll_poses_detail::show_one(ragdoll, step_fraction, frame_seconds);
            }
        });
}

void show_moved_ragdolls(entt::registry& registry)
{
    APP_SCOPE_PERF("Physics/Ragdolls/Show Moved");
    registry.view<ragdoll_component, active_component>().each(
        [&](auto /*entity*/, ragdoll_component& ragdoll, auto& /*active*/)
        {
            if(ragdoll.get_runtime().is_built)
            {
                ragdoll_poses_detail::show_again_if_moved(ragdoll);
            }
        });
}

} // namespace unravel
