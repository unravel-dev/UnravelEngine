#include "transform_component.h"

#include <cstdint>
#include <logging/logging.h>

#include <algorithm>

namespace unravel
{
namespace
{
auto is_ancestor_of(entt::handle potential_parent, entt::handle child) -> bool
{
    if(!child)
    {
        return false;
    }
    if(!potential_parent)
    {
        return false;
    }
    // If there is no parent, we've reached the root without a match.
    auto& tc = child.get<transform_component>();
    entt::handle current = tc.get_parent();
    while(current)
    {
        if(current == potential_parent)
            return true;
        current = current.get<transform_component>().get_parent();
    }
    return false;
}

bool order_changed = false;

auto get_next_order() -> uint64_t
{
    static uint64_t order{};
    return order++;
}

} // namespace

auto is_roots_order_changed() -> bool
{
    return order_changed;
}
void reset_roots_order_changed()
{
    order_changed = false;
}

void root_component::on_create_component(entt::registry& r, entt::entity e)
{
    order_changed = true;
}

void root_component::on_update_component(entt::registry& r, entt::entity e)
{
    order_changed = true;
}

void root_component::on_destroy_component(entt::registry& r, entt::entity e)
{
    order_changed = true;
}

void transform_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);

    auto& component = entity.get<transform_component>();
    component.set_owner(entity);
}

void transform_component::on_destroy_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);

    auto& component = entity.get<transform_component>();

    if(component.parent_)
    {
        auto parent_transform = component.parent_.try_get<transform_component>();
        if(parent_transform)
        {
            parent_transform->remove_child(component.get_owner(), component);
        }
    }

    for(auto& child : component.children_)
    {
        if(child)
        {
            child.destroy();
        }
    }
}

auto transform_component::is_parent_of(entt::handle parent_to_test, entt::handle child) -> bool
{
    return is_ancestor_of(parent_to_test, child);
}

auto transform_component::get_top_level_entities(const std::vector<entt::handle*>& list) -> std::vector<entt::handle>
{
    std::unordered_set<entt::entity> entity_set;
    for(auto ent : list)
    {
        entity_set.insert(*ent);
    }

    std::vector<entt::handle> top_level;
    top_level.reserve(list.size());

    for(auto ent : list)
    {
        bool has_selected_ancestor = false;
        auto current = *ent;

        // Walk up the hierarchy. If any ancestor is also in the selection,
        // we skip this entity.
        while(true)
        {
            auto parent = current.get<transform_component>().get_parent();
            if(!parent)
            {
                // No more parents, so no selected ancestor found.
                break;
            }
            if(entity_set.find(parent) != entity_set.end())
            {
                // Found a parent in the selection; skip.
                has_selected_ancestor = true;
                break;
            }
            current = parent;
        }

        if(!has_selected_ancestor)
        {
            top_level.push_back(*ent);
        }
    }

    return top_level;
}

auto transform_component::get_top_level_entities(const std::vector<entt::handle>& list) -> std::vector<entt::handle>
{
    std::unordered_set<entt::entity> entity_set;
    for(auto ent : list)
    {
        entity_set.insert(ent);
    }

    std::vector<entt::handle> top_level;
    top_level.reserve(list.size());

    for(auto ent : list)
    {
        bool has_selected_ancestor = false;
        auto current = ent;

        // Walk up the hierarchy. If any ancestor is also in the selection,
        // we skip this entity.
        while(true)
        {
            auto parent = current.get<transform_component>().get_parent();
            if(!parent)
            {
                // No more parents, so no selected ancestor found.
                break;
            }
            if(entity_set.find(parent) != entity_set.end())
            {
                // Found a parent in the selection; skip.
                has_selected_ancestor = true;
                break;
            }
            current = parent;
        }

        if(!has_selected_ancestor)
        {
            top_level.push_back(ent);
        }
    }

    return top_level;
}

void transform_component::set_owner(entt::handle owner)
{
    base::set_owner(owner);

    if(owner)
    {
        auto& root = get_owner().emplace_or_replace<root_component>();
        root.order = get_next_order();
    }

    transform_dirty_.set();

    flags_t flags{};
    flags.set();
    flags_.set_dirty(this, false);
    flags_.set_value(this, flags);
}

//---------------------------------------------
/// TRANSFORMS
//---------------------------------------------
auto transform_component::get_transform_local() const noexcept -> const math::transform&
{
    return transform_.get_value(this);
}

void transform_component::set_transform_local(const math::transform& trans) noexcept
{
    transform_.set_value(this, trans);
}

void transform_component::resolve_transform_global() noexcept
{
    if(transform_.has_auto_resolve())
    {
        get_transform_global();
    }
    else
    {
        transform_.get_global_value(this, true);

        for(const auto& child : children_)
        {
            auto& component = child.get<transform_component>();
            component.resolve_transform_global();
        }
    }
}

auto transform_component::get_transform_global() const noexcept -> const math::transform&
{
    return transform_.get_global_value(this, false);
}

auto transform_component::set_transform_global(const math::transform& tr) noexcept -> bool
{
    return set_transform_global_epsilon(tr, math::epsilon<float>());
}

auto transform_component::set_transform_global_epsilon(const math::transform& tr, float epsilon) noexcept -> bool
{
    if(get_transform_global().compare(tr, epsilon) == 0)
    {
        return false;
    }

    apply_transform(tr);

    return true;
}

//---------------------------------------------
/// TRANSLATION
//---------------------------------------------
auto transform_component::get_position_global() const noexcept -> const math::vec3&
{
    return get_transform_global().get_position();
}

void transform_component::set_position_global(const math::vec3& position) noexcept
{
    const auto& this_pos = get_position_global();
    if(math::all(math::epsilonEqual(this_pos, position, math::epsilon<float>())))
    {
        return;
    }
    auto m = get_transform_global();
    m.set_position(position);

    apply_transform(m);
}

void transform_component::move_by_global(const math::vec3& amount) noexcept
{
    math::vec3 new_pos = get_position_global() + amount;
    set_position_global(new_pos);
}

void transform_component::reset_position_global() noexcept
{
    set_position_global(math::vec3(0.0f, 0.0f, 0.0f));
}

auto transform_component::get_position_local() const noexcept -> const math::vec3&
{
    return get_transform_local().get_position();
}

void transform_component::set_position_local(const math::vec3& position) noexcept
{
    transform_.value(this).set_position(position);
}

void transform_component::move_by_local(const math::vec3& amount) noexcept
{
    transform_.value(this).translate_local(amount);
}

void transform_component::reset_position_local() noexcept
{
    set_position_local(math::vec3(0.0f, 0.0f, 0.0f));
}

auto transform_component::set_position_and_rotation_global(const math::vec3& position,
                                                           const math::quat& rotation,
                                                           float epsilon) noexcept -> bool
{
    const auto& this_pos = get_position_global();
    const auto& this_rotation = get_rotation_global();
    bool same_position = math::all(math::epsilonEqual(this_pos, position, epsilon));

    if(same_position)
    {
        bool same_rotation = math::all(math::epsilonEqual(this_rotation, rotation, epsilon));

        if(same_rotation)
        {
            return false;
        }
    }

    auto m = get_transform_global();
    m.set_rotation(rotation);
    m.set_position(position);

    apply_transform(m);

    return true;
}

//---------------------------------------------
/// ROTATION
//---------------------------------------------

auto transform_component::get_rotation_global() const noexcept -> const math::quat&
{
    return get_transform_global().get_rotation();
}

void transform_component::set_rotation_global(const math::quat& rotation) noexcept
{
    const auto& this_rotation = get_rotation_global();
    if(math::all(math::epsilonEqual(this_rotation, rotation, math::epsilon<float>())))
    {
        return;
    }

    auto m = get_transform_global();
    m.set_rotation(rotation);

    apply_transform(m);
}

void transform_component::rotate_by_global(const math::quat& rotation) noexcept
{
    auto m = get_transform_global();
    m.rotate(rotation);

    set_transform_global(m);
}

void transform_component::reset_rotation_global() noexcept
{
    set_rotation_global(math::transform::quat_t{1, 0, 0, 0});
}

auto transform_component::get_rotation_local() const noexcept -> const math::quat&
{
    return get_transform_local().get_rotation();
}

void transform_component::set_rotation_local(const math::quat& rotation) noexcept
{
    transform_.value(this).set_rotation(rotation);
}

void transform_component::rotate_by_local(const math::quat& rotation) noexcept
{
    auto m = get_transform_local();
    m.rotate(rotation);

    set_transform_local(m);
}

void transform_component::reset_rotation_local() noexcept
{
    set_rotation_local(math::transform::quat_t{1, 0, 0, 0});
}

auto transform_component::get_rotation_euler_global() const noexcept -> math::vec3
{
    return math::degrees(math::eulerAngles(get_rotation_global()));
}

void transform_component::set_rotation_euler_global(math::vec3 rotation) noexcept
{
    set_rotation_global(math::transform::quat_t(math::radians(rotation)));
}

void transform_component::rotate_by_euler_global(math::vec3 rotation) noexcept
{
    auto m = get_transform_global();
    m.rotate(math::radians(rotation));

    set_transform_global(m);
}

auto transform_component::get_rotation_euler_local() const noexcept -> math::vec3
{
    return math::degrees(math::eulerAngles(get_rotation_local()));
}

void transform_component::set_rotation_euler_local(math::vec3 rotation) noexcept
{
    set_rotation_local(math::transform::quat_t(math::radians(rotation)));
}

void transform_component::rotate_by_euler_local(math::vec3 rotation) noexcept
{
    auto m = get_transform_local();
    m.rotate_local(math::radians(rotation));

    set_transform_local(m);
}

void transform_component::rotate_axis_global(float degrees, const math::vec3& axis) noexcept
{
    auto m = get_transform_global();
    m.rotate_axis(math::radians(degrees), axis);

    set_transform_global(m);
}

void transform_component::rotate_around_global(const math::vec3& point, const math::vec3& axis, float degrees)
{
    auto vector = get_position_global();
    auto quaternion = math::angleAxis(math::radians(degrees), axis);
    auto vector2 = vector - point;
    vector2 = quaternion * vector2;
    vector = point + vector2;

    set_position_global(vector);
    rotate_axis_global(degrees, axis);
}

void transform_component::rotate_around_global(const math::vec3& point, const math::quat& rotation)
{
    auto euler = math::eulerAngles(rotation);
    rotate_around_global(point, math::vec3(1, 0, 0), math::degrees(euler.x));
    rotate_around_global(point, math::vec3(0, 1, 0), math::degrees(euler.y));
    rotate_around_global(point, math::vec3(0, 0, 1), math::degrees(euler.z));
}

void transform_component::look_at(const math::vec3& point) noexcept
{
    look_at(point, math::vec3{0.0f, 1.0f, 0.0f});
}

void transform_component::look_at(const math::vec3& point, const math::vec3& up) noexcept
{
    auto eye = get_position_global();
    math::transform m = math::lookAt(eye, point, up);
    m = math::inverse(m);

    set_rotation_global(m.get_rotation());
}

//---------------------------------------------
/// SCALE
//---------------------------------------------

auto transform_component::get_scale_local() const noexcept -> const math::vec3&
{
    return get_transform_local().get_scale();
}

void transform_component::scale_by_local(const math::vec3& scale) noexcept
{
    transform_.value(this).scale(scale);
}

auto transform_component::get_skew_local() const noexcept -> const math::vec3&
{
    return get_transform_local().get_skew();
}

auto transform_component::get_perspective_local() const noexcept -> const math::vec4&
{
    return get_transform_local().get_perspective();
}

auto transform_component::get_x_axis_local() const noexcept -> math::vec3
{
    return get_transform_local().x_unit_axis();
}

auto transform_component::get_y_axis_local() const noexcept -> math::vec3
{
    return get_transform_local().y_unit_axis();
}

auto transform_component::get_z_axis_local() const noexcept -> math::vec3
{
    return get_transform_local().z_unit_axis();
}

auto transform_component::get_x_axis_global() const noexcept -> math::vec3
{
    return get_transform_global().x_unit_axis();
}

auto transform_component::get_y_axis_global() const noexcept -> math::vec3
{
    return get_transform_global().y_unit_axis();
}

auto transform_component::get_z_axis_global() const noexcept -> math::vec3
{
    return get_transform_global().z_unit_axis();
}

auto transform_component::get_scale_global() const noexcept -> const math::vec3&
{
    return get_transform_global().get_scale();
}

void transform_component::scale_by_global(const math::vec3& scale) noexcept
{
    auto m = get_transform_global();
    m.scale(scale);

    apply_transform(m);
}

auto transform_component::get_skew_global() const noexcept -> const math::vec3&
{
    return get_transform_global().get_skew();
}

auto transform_component::get_perspective_global() const noexcept -> const math::vec4&
{
    return get_transform_global().get_perspective();
}

void transform_component::set_scale_global(const math::vec3& scale) noexcept
{
    const auto& this_scale = get_scale_global();
    if(math::all(math::epsilonEqual(this_scale, scale, math::epsilon<float>())))
    {
        return;
    }

    auto m = get_transform_global();
    m.set_scale(scale);

    apply_transform(m);
}

void transform_component::set_scale_local(const math::vec3& scale) noexcept
{
    transform_.value(this).set_scale(scale);
}

void transform_component::set_skew_global(const math::vec3& skew) noexcept
{
    const auto& this_skew = get_skew_global();
    if(math::all(math::epsilonEqual(this_skew, skew, math::epsilon<float>())))
    {
        return;
    }

    auto m = get_transform_global();
    m.set_skew(skew);

    apply_transform(m);
}

void transform_component::set_skew_local(const math::vec3& skew) noexcept
{
    transform_.value(this).set_skew(skew);
}

void transform_component::set_perspective_global(const math::vec4& perspective) noexcept
{
    const auto& this_perspective = get_perspective_global();
    if(math::all(math::epsilonEqual(this_perspective, perspective, math::epsilon<float>())))
    {
        return;
    }

    auto m = get_transform_global();
    m.set_perspective(perspective);

    apply_transform(m);
}

void transform_component::set_perspective_local(const math::vec4& perspective) noexcept
{
    transform_.value(this).set_perspective(perspective);
}

void transform_component::reset_scale_global() noexcept
{
    set_scale_global(math::vec3{1.0f, 1.0f, 1.0f});
}

void transform_component::reset_scale_local() noexcept
{
    set_scale_local(math::vec3{1.0f, 1.0f, 1.0f});
}

void transform_component::_clear_relationships()
{
    children_.clear();
    parent_ = {};
}

auto transform_component::set_parent(const entt::handle& p, bool global_stays) -> bool
{
    auto new_parent = p;
    auto old_parent = parent_;

    if(new_parent == get_owner())
    {
        APPLOG_ERROR("Cannot set parent to self");
        return false;
    }

    // Skip if this is a no-op.
    if(old_parent == new_parent)
    {
        return false;
    }

    // Skip if the new parent is our descendant.
    if(is_ancestor_of(get_owner(), new_parent))
    {
        return false;
    }

    // Before we do anything, make sure that all pending math::transform
    // operations are resolved (including those applied to our parent).
    math::transform cached_transform_global;
    if(global_stays)
    {
        cached_transform_global = get_transform_global();
    }

    parent_ = new_parent;
    set_dirty(true);

    if(global_stays)
    {
        set_transform_global(cached_transform_global);
    }

    set_dirty(true);

    if(new_parent)
    {
        new_parent.get<transform_component>().attach_child(get_owner(), *this);

        if(!old_parent)
        {
            get_owner().remove<root_component>();
        }
    }
    else
    {
        auto& root = get_owner().emplace_or_replace<root_component>();
        root.order = get_next_order();
    }

    if(old_parent)
    {
        old_parent.get<transform_component>().remove_child(get_owner(), *this);
    }

    return true;
}

auto transform_component::get_parent() const noexcept -> entt::handle
{
    return parent_;
}

void transform_component::attach_child(const entt::handle& child, transform_component& child_transform)
{
    child_transform.sort_index_ = int32_t(children_.size());
    children_.push_back(child);
    sort_children();
    set_dirty(is_dirty());
}

auto transform_component::remove_child(const entt::handle& child, transform_component& child_transform) -> bool
{
    auto iter = std::remove_if(std::begin(children_),
                               std::end(children_),
                               [&child](const auto& other)
                               {
                                   return child == other;
                               });
    if(iter == std::end(children_))
    {
        return false;
    }

    assert(std::distance(iter, std::end(children_)) == 1);

    auto removed_idx = child_transform.sort_index_;

    children_.erase(iter, std::end(children_));

    // shift all bigger sort indices
    for(auto& c : children_)
    {
        auto& sort_idx = c.get<transform_component>().sort_index_;
        if(sort_idx > removed_idx)
        {
            sort_idx--;
        }
    }
    child_transform.sort_index_ = {-1};

    return true;
}

void transform_component::sort_children() noexcept
{
    std::sort(children_.begin(),
              children_.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  return lhs.template get<transform_component>().sort_index_ <
                         rhs.template get<transform_component>().sort_index_;
              });
}

void transform_component::apply_transform(const math::transform& tr) noexcept
{
    auto parent = get_parent();
    if(parent)
    {
        auto inv_parent_transform = inverse_parent_transform(parent);
        set_transform_local(inv_parent_transform * tr);
    }
    else
    {
        set_transform_local(tr);
    }
}

auto transform_component::inverse_parent_transform(const entt::handle& parent) noexcept -> math::transform
{
    const auto& parent_transform = parent.get<transform_component>().get_transform_global();
    return math::inverse(parent_transform);
}

auto transform_component::to_local(const math::vec3& point) const noexcept -> math::vec3
{
    return get_transform_global().inverse_transform_coord(point);
}

auto transform_component::is_active() const noexcept -> bool
{
    return flags_.get_value(this)[flags_types::active];
}

auto transform_component::is_active_global() const noexcept -> bool
{
    return flags_.get_global_value(this, false)[flags_types::active];
}

void transform_component::set_active(bool active) noexcept
{
    auto val = flags_.get_value(this);
    val[flags_types::active] = active;
    flags_.set_value(this, val);
}

auto transform_component::is_dirty() const noexcept -> bool
{
    return transform_.dirty;
}

void transform_component::set_dirty(bool dirty) noexcept
{
    transform_.set_dirty(this, dirty);
}

auto transform_component::is_dirty(uint8_t id) const noexcept -> bool
{
    return transform_dirty_[id];
}

void transform_component::set_dirty(uint8_t id, bool dirty) noexcept
{
    transform_dirty_.set(id, dirty);
}

auto transform_component::get_children() const noexcept -> const std::vector<entt::handle>&
{
    return children_;
}

void transform_component::set_children(const std::vector<entt::handle>& children)
{
    children_ = children;
}

void transform_component::on_dirty_transform(bool dirty) noexcept
{
    if(dirty)
    {
        transform_dirty_.set();
    }

    if(transform_.has_auto_resolve())
    {
        for(const auto& child : get_children())
        {
            auto component = child.try_get<transform_component>();
            if(component)
            {
                component->transform_.set_dirty(component, dirty);
            }
        }
    }
}

auto transform_component::resolve_global_value_transform() const noexcept -> math::transform
{
    auto parent = get_parent();

    if(parent)
    {
        const auto& parent_transform = parent.get<transform_component>().get_transform_global();
        const auto& local_transform = get_transform_local();

        return parent_transform * local_transform;
    }

    return get_transform_local();
}

void transform_component::on_dirty_flags(bool dirty) noexcept
{
    auto flags = flags_.get_global_value(this, false);
    on_flags_changed(flags);

    if(flags_.has_auto_resolve())
    {
        for(const auto& child : get_children())
        {
            auto component = child.try_get<transform_component>();
            if(component)
            {
                component->flags_.set_dirty(component, dirty);
            }
        }
    }
}

void transform_component::on_flags_changed(flags_t flags)
{
    if(flags[flags_types::active])
    {
        auto& comp = get_owner().get_or_emplace<active_component>();
        (void)comp;
    }
    else
    {
        get_owner().remove<active_component>();
    }
}

auto transform_component::get_flags_global() const noexcept -> flags_t
{
    return flags_.get_global_value(this, false);
}

auto transform_component::resolve_global_value_flags() const noexcept -> flags_t
{
    auto parent = get_parent();

    const auto& local_flags = flags_.get_value(this);
    if(parent)
    {
        const auto& parent_flags = parent.get<transform_component>().get_flags_global();

        return parent_flags & local_flags;
    }

    return local_flags;
}
} // namespace unravel
