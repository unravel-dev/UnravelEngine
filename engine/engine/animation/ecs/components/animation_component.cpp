#include "animation_component.h"
#include <hpp/utility/overload.hpp>
namespace unravel
{

void animation_component::set_animation(const asset_handle<animation_clip>& animation)
{
    animation_ = animation;
}
auto animation_component::get_animation() const -> const asset_handle<animation_clip>&
{
    return animation_;
}

void animation_component::set_controller(const asset_handle<animation_controller>& controller)
{
    if(controller_ == controller)
    {
        return;
    }
    // The layers the previous controller played are its own; a new one starts them again.
    for(size_t layer = 0; layer < controller_instance_.get_layer_count(); ++layer)
    {
        player_.clear(layer);
    }
    controller_instance_.bind(nullptr);
    controller_ = controller;
    if(!controller_ && player_.is_playing() && animation_)
    {
        player_.blend_to(0, animation_);
    }
}

auto animation_component::get_controller() const -> const asset_handle<animation_controller>&
{
    return controller_;
}

auto animation_component::get_controller_instance() -> animation_controller_instance*
{
    return bind_controller(controller_ ? controller_.get() : nullptr);
}

auto animation_component::try_get_controller_instance() -> animation_controller_instance*
{
    // Still the bound definition: compared in place, so the definition every character shares is
    // not counted each frame, and then bound again as bind_controller binds it (a revision check).
    if(controller_ && controller_instance_.is_bound())
    {
        const auto& bound = controller_instance_.get_controller();
        const bool is_bound_current = controller_.visit(
            [&](animation_controller* definition)
            {
                return definition == bound.get();
            });
        if(is_bound_current)
        {
            controller_instance_.bind(bound);
            controller_instance_.sync(player_);
            return &controller_instance_;
        }
    }
    auto definition = controller_ ? controller_.try_get() : nullptr;
    if(controller_ && !definition)
    {
        // Still loading: keep whatever was bound until the definition arrives.
        return controller_instance_.is_bound() ? &controller_instance_ : nullptr;
    }
    return bind_controller(definition);
}

auto animation_component::bind_controller(const std::shared_ptr<animation_controller>& definition)
    -> animation_controller_instance*
{
    if(!definition || asset_handle<animation_controller>::is_empty(definition))
    {
        if(controller_instance_.is_bound())
        {
            controller_instance_.bind(nullptr);
        }
        return nullptr;
    }
    // Rebinds after a reload: the definition pointer changes when the asset is recompiled.
    controller_instance_.bind(definition);
    controller_instance_.sync(player_);
    return &controller_instance_;
}

auto animation_component::get_controller_instance() const -> const animation_controller_instance*
{
    return controller_instance_.is_bound() ? &controller_instance_ : nullptr;
}

void animation_component::set_autoplay(bool on)
{
    auto_play_ = on;
}

auto animation_component::get_autoplay() const -> bool
{
    return auto_play_;
}

void animation_component::set_apply_root_motion(bool on)
{
    apply_root_motion_ = on;
}

auto animation_component::get_apply_root_motion() const -> bool
{
    return apply_root_motion_;
}

void animation_component::set_culling_mode(const culling_mode& mode)
{
    culling_mode_ = mode;
}

auto animation_component::get_culling_mode() const -> const culling_mode&
{
    return culling_mode_;
}

void animation_component::set_speed(float speed)
{
    speed_ = std::max(0.0f, speed);
}

auto animation_component::get_speed() const -> float
{
    return speed_;
}

auto animation_component::get_player() const -> const animation_player&
{
    return player_;
}
auto animation_component::get_player() -> animation_player&
{
    return player_;
}

void animation_component::set_retargeting_mode(animation_retargeting_mode mode)
{
    retargeting_mode_ = mode;
}

auto animation_component::get_retargeting_mode() const -> animation_retargeting_mode
{
    return retargeting_mode_;
}

void animation_component::set_pose_retargeting(animation_pose_retargeting retargeting)
{
    pose_retargeting_ = retargeting;
}

auto animation_component::get_pose_retargeting() const -> animation_pose_retargeting
{
    return pose_retargeting_;
}

void animation_component::set_retarget_pose(animation_retarget_pose pose)
{
    if(retarget_pose_ != pose)
    {
        retarget_pose_ = pose;
        ++retarget_revision_;
    }
}

auto animation_component::get_retarget_pose() const -> animation_retarget_pose
{
    return retarget_pose_;
}

void animation_component::set_retarget_pose_offsets(const std::vector<humanoid_pose_offset>& offsets)
{
    if(retarget_pose_offsets_ != offsets)
    {
        retarget_pose_offsets_ = offsets;
        ++retarget_revision_;
    }
}

auto animation_component::get_retarget_pose_offsets() const -> const std::vector<humanoid_pose_offset>&
{
    return retarget_pose_offsets_;
}

auto animation_component::get_retarget_revision() const -> uint64_t
{
    return retarget_revision_;
}

auto animation_component::get_target_skeleton(uint64_t skeleton_version,
                                              const std::function<void(animation_target_skeleton&)>& build)
    -> const std::shared_ptr<const animation_target_skeleton>&
{
    if(!target_skeleton_ || target_skeleton_version_ != skeleton_version || target_skeleton_revision_ != retarget_revision_)
    {
        auto skeleton = std::make_shared<animation_target_skeleton>();
        build(*skeleton);
        skeleton->compute_rest_globals();
        skeleton->compute_humanoid_bones();
        skeleton->compute_reference_pose(retarget_pose_ == animation_retarget_pose::t_pose, retarget_pose_offsets_);
        target_skeleton_ = std::move(skeleton);
        target_skeleton_version_ = skeleton_version;
        target_skeleton_revision_ = retarget_revision_;
    }
    return target_skeleton_;
}

} // namespace unravel
