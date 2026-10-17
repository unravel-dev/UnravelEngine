#include "animation_retarget_check.h"

#include <editor/editing/editor_actions.h>
#include <engine/animation/animation_controller.h>
#include <engine/assets/asset_manager.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/systems/animation_system.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace
{
using match_kind = animation_binding_report::match_kind;

auto get_role_list(const std::vector<humanoid_bone>& roles) -> std::string
{
    std::string list;
    for(const auto role : roles)
    {
        list += (list.empty() ? "" : ", ") + std::string(get_humanoid_bone_name(role));
    }
    return list;
}

/// Humanoid roles of the target skeleton no channel of the clip reaches.
auto find_unreached_roles(const animation_binding_report& report, const animation_target_skeleton& skeleton)
    -> std::vector<humanoid_bone>
{
    std::vector<humanoid_bone> roles;
    for(size_t role = 0; role < HUMANOID_BONE_COUNT; ++role)
    {
        const int bone = skeleton.humanoid[role];
        if(bone >= 0 && std::find(report.unreached_bones.begin(), report.unreached_bones.end(), bone) !=
                            report.unreached_bones.end())
        {
            roles.push_back(humanoid_bone(role));
        }
    }
    return roles;
}

/// Channels a Rest Aligned binding would reach beyond those `binding` (a Copy one) reaches: bones matched by role.
auto count_role_matches_missed(const std::shared_ptr<animation_clip>& clip,
                               animation_skeleton_binding binding,
                               const animation_retarget_check& check) -> size_t
{
    binding.version += 1;
    binding.rest_aligned_target = check.skeleton;
    const auto aligned = animation_player::describe_binding(clip, binding, *check.skeleton);
    const auto matched = size_t(std::count_if(aligned.channels.begin(),
                                              aligned.channels.end(),
                                              [](const animation_binding_report::channel_entry& channel)
                                              {
                                                  return channel.match != match_kind::none;
                                              }));
    const size_t current = count_matched_channels(check);
    return matched > current ? matched - current : 0;
}

void add_setting_warnings(const animation_component& animation,
                          size_t role_matches_missed,
                          animation_retarget_check& check)
{
    const bool wants_rest_aligned = animation.get_pose_retargeting() == animation_pose_retargeting::rest_aligned;
    if(wants_rest_aligned && animation.get_retargeting_mode() != animation_retargeting_mode::name_based)
    {
        check.warnings.emplace_back("Rest Aligned needs Retargeting Mode Name Based: Index Based plays clips by node "
                                    "order, copied.");
    }
    else if(wants_rest_aligned && !check.report.has_source_rest)
    {
        check.warnings.emplace_back("The clip recorded no rest pose (imported before rest poses were kept), so it "
                                    "plays copied. Reimport its model to retarget it.");
    }
    if(!wants_rest_aligned && check.report.rest_mismatches > 0)
    {
        check.warnings.push_back(std::to_string(check.report.rest_mismatches) +
                                 " bones rest differently in the clip than on this model: it was made on another "
                                 "skeleton or another export. Copy keeps that skeleton's bone axes and lengths; set "
                                 "Pose Retargeting to Rest Aligned.");
    }
    if(role_matches_missed > 0)
    {
        check.warnings.push_back(std::to_string(role_matches_missed) +
                                 " more channels would reach a bone with Pose Retargeting Rest Aligned, which also "
                                 "matches bones by humanoid role where names differ.");
    }
}

void add_binding_warnings(animation_retarget_check& check)
{
    const auto& report = check.report;
    if(!report.channels.empty() && count_matched_channels(check) == 0)
    {
        check.warnings.emplace_back("No channel reaches a bone: the names differ, and no humanoid roles match them.");
    }
    for(const auto& difference : report.rest_differences)
    {
        if(difference.degrees <= REST_POSE_WARNING_DEGREES)
        {
            continue;
        }
        const std::string what = std::string(get_humanoid_bone_name(difference.role)) + " rests " +
                                 std::to_string(int(std::lround(difference.degrees))) + " degrees apart on the two skeletons";
        check.warnings.push_back(
            check.aligns_to_t_pose
                ? what + " even straightened to a T-pose: check its role in Humanoid Mapping on both sides."
                : what + ": they rest in different poses (a T-pose against an A-pose?) and every pose keeps that "
                         "offset. Set Retarget Pose to T-Pose to straighten both first.");
    }
    if(report.is_rest_aligned && check.skeleton)
    {
        const auto roles = find_unreached_roles(report, *check.skeleton);
        if(!roles.empty() && roles.size() < HUMANOID_BONE_COUNT)
        {
            check.warnings.push_back("The clip has no bone for these roles, which keep the bind pose: " +
                                     get_role_list(roles) + ".");
        }
    }
    const bool lost_root_motion = (report.root_position_channel >= 0 && report.root_position_bone < 0) ||
                                  (report.root_rotation_channel >= 0 && report.root_rotation_bone < 0);
    if(lost_root_motion)
    {
        check.warnings.emplace_back("The clip's root motion node reaches no bone here: its root motion moves nothing.");
    }
}
/// The clip a state plays: its own, or a blend state's first point.
auto get_state_clip(const animation_state_def& state) -> asset_handle<animation_clip>
{
    if(state.clip || state.points.empty())
    {
        return state.clip;
    }
    return state.points.front().clip;
}

/// The clip the controller's base layer starts in, when the controller is loaded (or `waits` for it).
auto get_controller_default_clip(const animation_component& animation, bool waits) -> asset_handle<animation_clip>
{
    const auto& handle = animation.get_controller();
    const auto controller = handle ? handle.try_get(waits) : nullptr;
    if(!controller || asset_handle<animation_controller>::is_empty(controller) || controller->layers.empty())
    {
        return {};
    }
    const auto& layer = controller->layers.front();
    const int state = find_animation_state(layer, layer.default_state);
    return state >= 0 ? get_state_clip(layer.states[size_t(state)]) : asset_handle<animation_clip>{};
}
} // namespace

auto check_animation_retarget(animation_component& animation,
                              const model_component& model,
                              const std::shared_ptr<animation_clip>& clip) -> animation_retarget_check
{
    animation_retarget_check check;
    check.skeleton = animation_system::get_target_skeleton(animation, model);
    if(!clip || !check.skeleton)
    {
        return check;
    }
    const animation_skeleton_binding binding = animation_system::make_skeleton_binding(animation, model);
    check.report = animation_player::describe_binding(clip, binding, *check.skeleton);
    check.aligns_to_t_pose = check.skeleton->aligns_to_t_pose;
    check.pose_offset_count = animation.get_retarget_pose_offsets().size();
    for(const auto& channel : check.report.channels)
    {
        ++check.match_counts[size_t(channel.match)];
    }
    const bool can_match_roles = animation.get_retargeting_mode() == animation_retargeting_mode::name_based &&
                                 !check.report.is_rest_aligned && check.report.has_source_rest;
    const size_t role_matches_missed = can_match_roles ? count_role_matches_missed(clip, binding, check) : 0;
    add_setting_warnings(animation, role_matches_missed, check);
    add_binding_warnings(check);
    return check;
}

auto get_default_retarget_clip(animation_component& animation, bool waits_for_controller)
    -> asset_handle<animation_clip>
{
    const auto& layers = animation.get_player().get_layers();
    if(!layers.empty())
    {
        const auto& state = layers.front().current_state.state;
        if(state.clip)
        {
            return state.clip;
        }
        if(!state.blend_clips.empty())
        {
            return state.blend_clips.front().clip;
        }
    }
    if(const auto clip = get_controller_default_clip(animation, waits_for_controller))
    {
        return clip;
    }
    return animation.get_animation();
}

auto get_match_kind_name(animation_binding_report::match_kind kind) -> const char*
{
    switch(kind)
    {
        case match_kind::index:
            return "index";
        case match_kind::name:
            return "name";
        case match_kind::name_without_namespace:
            return "name without namespace";
        case match_kind::role:
            return "role";
        case match_kind::none:
        default:
            return "none";
    }
}

auto get_retarget_bone_name(const animation_retarget_check& check, int bone) -> std::string
{
    if(!check.skeleton || bone < 0 || size_t(bone) >= check.skeleton->names.size())
    {
        return "-";
    }
    return check.skeleton->names[size_t(bone)];
}

auto count_matched_channels(const animation_retarget_check& check) -> size_t
{
    return check.report.channels.size() - check.match_counts[size_t(match_kind::none)];
}

auto get_editable_model_key(const model_component& model) -> std::string
{
    const auto lod = model.get_model().get_lod(0);
    std::string key = lod ? lod.id() : std::string{};
    return key.rfind("app:", 0) == 0 ? key : std::string{};
}

auto get_model_humanoid_overrides(rtti::context& ctx, const std::string& model_key) -> std::vector<humanoid_bone_override>
{
    const auto meta = ctx.get_cached<asset_manager>().get_metadata_for_key(model_key);
    const auto importer = std::dynamic_pointer_cast<mesh_importer_meta>(meta.meta.importer);
    return importer ? importer->rig.humanoid_overrides : std::vector<humanoid_bone_override>{};
}

auto set_model_humanoid_override(rtti::context& ctx,
                                 const std::string& model_key,
                                 humanoid_bone role,
                                 const std::optional<std::string>& bone) -> bool
{
    auto meta = ctx.get_cached<asset_manager>().get_metadata_for_key(model_key);
    const auto importer = std::dynamic_pointer_cast<mesh_importer_meta>(meta.meta.importer);
    if(!importer)
    {
        return false;
    }
    auto& overrides = importer->rig.humanoid_overrides;
    overrides.erase(std::remove_if(overrides.begin(),
                                   overrides.end(),
                                   [role](const humanoid_bone_override& entry)
                                   {
                                       return entry.role == role;
                                   }),
                    overrides.end());
    if(bone)
    {
        overrides.push_back({role, *bone});
    }
    return editor_actions::save_import_settings(model_key, meta.meta);
}

} // namespace unravel
