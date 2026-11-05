#include "ragdoll_builder.h"

#include <engine/animation/animation_humanoid.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/physics/ecs/systems/ragdoll_poses.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/mesh.h>

#include <hpp/optional.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace ragdoll_builder_detail
{
const math::vec3 up_axis{0.0f, 1.0f, 0.0f};
const math::vec3 z_axis{0.0f, 0.0f, 1.0f};
/// Share of the vertices around a limb inside the radius.
constexpr float limb_radius_percentile = 0.75f;
/// Hands and necks are flat or thin: the middle distance fits them better.
constexpr float flat_radius_percentile = 0.5f;
/// Percentiles that bound a body's vertices, so a few stray vertices do not widen it.
constexpr float extent_low = 0.05f;
constexpr float extent_high = 0.95f;
/// Feet and heads bound their vertices more tightly: the sole must reach the floor.
constexpr float tight_low = 0.02f;
constexpr float tight_high = 0.98f;
/// Ends of a limb segment left out of its radius fit: joints bulge with the neighbouring part's vertices.
constexpr float limb_fit_margin = 0.12f;
/// Fewer vertices than this and a shape is sized from the bone lengths.
constexpr size_t min_fit_vertices = 12;
/// Sizes relative to the character's height, for a skeleton without skin and for the smallest parts.
constexpr float min_radius_of_height = 0.012f;
constexpr float min_neck_of_height = 0.035f;
constexpr float overlap_margin_of_height = 0.006f;
/// A skeleton flatter than this is measured as this tall, so every size taken from the height stays positive.
constexpr float min_character_height = 0.1f;
/// A segment shorter than this share of the height has its end bone on its start: it has no direction to lie along.
constexpr float min_segment_of_height = 1e-3f;
/// How far a segment reaches, as a share of the height, when the bone it would reach to is missing: up for the
/// trunk, down for a limb, up to the elbow for a hand, ahead to the ball of the foot for a foot.
constexpr float missing_trunk_end_of_height = 0.1f;
constexpr float missing_limb_end_of_height = 0.2f;
constexpr float missing_forearm_of_height = 0.15f;
constexpr float missing_toes_of_height = 0.09f;
/// Squared lengths below this count as zero in the distance between two segments.
constexpr float min_squared_length = 1e-8f;

/**
 * @brief How a capsule from joint to joint is sized: its radius as a share of its length when there are no vertices
 *        to fit, the share of the vertices around it inside the radius, and the largest radius its length allows.
 */
struct capsule_fit
{
    float radius_of_length;
    float percentile;
    float max_radius_of_length;
};

/// A limb capsule is never thicker than this share of its length.
constexpr float max_limb_radius_of_length = 0.45f;
/// Limb radii by the proportions of a human body.
constexpr capsule_fit upper_arm_fit{0.18f, limb_radius_percentile, max_limb_radius_of_length};
constexpr capsule_fit lower_arm_fit{0.15f, limb_radius_percentile, max_limb_radius_of_length};
constexpr capsule_fit upper_leg_fit{0.19f, limb_radius_percentile, max_limb_radius_of_length};
constexpr capsule_fit lower_leg_fit{0.13f, limb_radius_percentile, max_limb_radius_of_length};
/// A neck is short and thick: its capsule may be a ball wider than the neck is long.
constexpr capsule_fit neck_fit{0.5f, flat_radius_percentile, 0.6f};
/// A torso without vertices: a spine segment is about half as tall as the body is wide, and a little less deep.
constexpr float torso_width_of_segment = 2.0f;
constexpr float torso_depth_of_segment = 1.2f;
/// A head without vertices: narrower than tall, a little deeper than wide, and in front of the neck.
constexpr float head_height_of_height = 0.13f;
constexpr float head_width_of_head_height = 0.75f;
constexpr float head_depth_of_width = 1.1f;
constexpr float head_forward_of_width = 0.1f;
/// A hand without vertices is this share of its forearm long; a fitted one still reaches this share of that guess,
/// so fingers curled in the bind pose do not shorten it to the palm.
constexpr float hand_length_of_forearm = 0.7f;
constexpr float min_hand_reach_of_guess = 0.5f;
/// Hands are flat: thin against their length.
constexpr float hand_radius_of_length = 0.2f;
constexpr float max_hand_radius_of_length = 0.3f;
/// A foot without vertices: the ball of the foot is about this share of the foot's length from the ankle, and the
/// foot is never shorter than this share of the height.
constexpr float foot_length_of_ankle_to_ball = 1.6f;
constexpr float min_foot_of_height = 0.1f;
/// The ankle stands at least this share of the height above the floor.
constexpr float min_ankle_of_height = 0.03f;
/// The heel reaches behind the ankle and the toes ahead of it by these shares of the foot's length; the foot is
/// twice as wide as this share of it.
constexpr float heel_of_foot_length = 0.25f;
constexpr float toes_of_foot_length = 0.75f;
constexpr float foot_half_width_of_length = 0.2f;
/// The foot box stops below the ankle, by this share of the ankle's height without vertices and at this share of the
/// fitted height with them: the shin capsule covers the top of the ankle.
constexpr float foot_top_below_ankle = 0.3f;
constexpr float foot_box_of_fitted_height = 0.7f;

/// Mass shares of the body segments, from Winter, Biomechanics and Motor Control of Human Movement (table 4.1).
constexpr float head_mass_share = 0.081f;
constexpr float neck_share_of_head = 0.15f;
constexpr float pelvis_mass_share = 0.142f;
constexpr float abdomen_mass_share = 0.139f;
constexpr float thorax_mass_share = 0.216f;
/// The upper chest carries the ribs and the shoulders: the larger part of the thorax.
constexpr float chest_share_of_thorax = 0.4f;
constexpr float upper_arm_mass_share = 0.028f;
constexpr float forearm_mass_share = 0.016f;
constexpr float hand_mass_share = 0.006f;
constexpr float thigh_mass_share = 0.100f;
constexpr float shank_mass_share = 0.0465f;
constexpr float foot_mass_share = 0.0145f;

/**
 * @brief A ball joint's cone and twist range in degrees, and its share of the ragdoll's joint friction.
 */
struct ball_range
{
    float swing;
    float twist_lower;
    float twist_upper;
    float friction_scale;
};

/**
 * @brief A hinge's range in degrees and its share of the ragdoll's joint friction.
 */
struct hinge_range
{
    float lower;
    float upper;
    float friction_scale;
};

/// Box3D asserts on a cone wider than this, and the engine clamps cones to it.
constexpr float max_swing_degrees = 90.0f;
/// Each spine joint bends a little, and stiffly, so the trunk does not fold like a chain.
constexpr ball_range torso_range{22.0f, -18.0f, 18.0f, 2.0f};
constexpr ball_range neck_range{35.0f, -35.0f, 35.0f, 1.0f};
/// A head on a neck body turns on top of the neck's range; without one its joint carries both.
constexpr ball_range head_on_neck_range{30.0f, -30.0f, 30.0f, 0.8f};
constexpr ball_range head_range{50.0f, -60.0f, 60.0f, 0.8f};
/// The shoulder is the widest joint; its cone centres away from the bind pose (shoulder_cone_from_hanging_degrees).
constexpr ball_range shoulder_range{80.0f, -60.0f, 60.0f, 0.8f};
/// Elbows and knees fold one way, and barely past straight.
constexpr hinge_range elbow_range{-3.0f, 140.0f, 0.6f};
constexpr hinge_range knee_range{-3.0f, 140.0f, 1.0f};
/// A light, loose wrist.
constexpr ball_range wrist_range{50.0f, -60.0f, 60.0f, 0.3f};
/// The hip flexes through its twist, far forward and a little back; the cone holds the leg's sideways swing. Legs are
/// heavy, so their friction is high.
constexpr ball_range hip_range{30.0f, -20.0f, 110.0f, 1.5f};
constexpr ball_range ankle_range{18.0f, -40.0f, 25.0f, 0.5f};
/// The shoulder cone centres on the arm this far from hanging, halfway to held out level, so a hanging arm and a
/// T-pose both lie well inside it.
constexpr float shoulder_cone_from_hanging_degrees = 45.0f;
/// The least room a rest pose keeps inside a cone, so it does not start on the limit.
constexpr float min_cone_margin_degrees = 10.0f;
/// The elbow folds the forearm forward and a little up: the lift settles which way for a forearm pointing forward.
constexpr float elbow_fold_lift = 0.2f;
/// Directions closer than about 18 degrees make a poor cross product: a frame takes another reference then.
constexpr float parallel_cosine = 0.95f;
/// A cross product shorter than this has no direction: its two vectors are parallel.
constexpr float min_axis_length = 1e-4f;

/// Every cone the builder makes, checked against Box3D's limit at compile time.
constexpr std::array<ball_range, 8> ball_ranges{torso_range,
                                                neck_range,
                                                head_on_neck_range,
                                                head_range,
                                                shoulder_range,
                                                wrist_range,
                                                hip_range,
                                                ankle_range};
static_assert(std::all_of(ball_ranges.begin(),
                          ball_ranges.end(),
                          [](const ball_range& range)
                          {
                              return range.swing <= max_swing_degrees;
                          }),
              "Box3D asserts on cones wider than 90 degrees.");
static_assert(shoulder_range.swing - min_cone_margin_degrees >= shoulder_cone_from_hanging_degrees,
              "A hanging arm lies inside the shoulder cone with room to spare.");

enum class body_kind : uint8_t
{
    torso,
    neck,
    head,
    upper_arm,
    lower_arm,
    hand,
    upper_leg,
    lower_leg,
    foot,
};

/**
 * @brief A limb segment's role and the role of the bone it reaches to.
 */
struct role_link
{
    humanoid_bone from;
    humanoid_bone to;
};

/// Each limb segment reaches from its bone to the next one down the limb.
constexpr std::array<role_link, 10> limb_links{{
    {humanoid_bone::left_upper_arm, humanoid_bone::left_lower_arm},
    {humanoid_bone::left_lower_arm, humanoid_bone::left_hand},
    {humanoid_bone::right_upper_arm, humanoid_bone::right_lower_arm},
    {humanoid_bone::right_lower_arm, humanoid_bone::right_hand},
    {humanoid_bone::left_upper_leg, humanoid_bone::left_lower_leg},
    {humanoid_bone::left_lower_leg, humanoid_bone::left_foot},
    {humanoid_bone::left_foot, humanoid_bone::left_toes},
    {humanoid_bone::right_upper_leg, humanoid_bone::right_lower_leg},
    {humanoid_bone::right_lower_leg, humanoid_bone::right_foot},
    {humanoid_bone::right_foot, humanoid_bone::right_toes},
}};

struct body_plan
{
    humanoid_bone role{humanoid_bone::hips};
    body_kind kind{body_kind::torso};
    int bone{-1};
    int parent{-1};
    float mass_share{0.0f};
    math::vec3 start{0.0f};
    math::vec3 end{0.0f};
    /// The shape in the model's space as a capsule (or a box for feet).
    math::vec3 center{0.0f};
    math::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float radius{0.0f};
    float segment{0.0f};
    math::vec3 box_size{0.0f};
    bool is_box{false};
    bool is_fitted{false};
    /// Joint frame in the model's space; X is the twist or hinge axis.
    math::quat joint_frame{1.0f, 0.0f, 0.0f, 0.0f};
    /// Turn of the cone's centre away from the bind pose, Euler angles in degrees in the joint frame.
    math::vec3 swing_offset{0.0f};
    ragdoll_joint_type joint{ragdoll_joint_type::ball};
    float swing{30.0f};
    float twist_lower{-20.0f};
    float twist_upper{20.0f};
    float friction_scale{1.0f};
    std::vector<math::vec3> vertices;
};

struct skeleton_view
{
    std::vector<std::string> names;
    std::vector<int> parents;
    std::vector<math::transform> bind;
    std::vector<math::vec3> positions;
    humanoid_map roles{make_empty_humanoid_map()};
    math::vec3 side{1.0f, 0.0f, 0.0f};
    math::vec3 forward{0.0f, 0.0f, 1.0f};
    float height{1.8f};
};

struct character_frame
{
    math::vec3 side;
    math::vec3 up;
    math::vec3 forward;
};

/**
 * @brief What one Auto Setup reads and makes, step by step.
 */
struct build_context
{
    const model_component& model;
    entt::handle model_entity;
    const ragdoll_build_options& options;
    skeleton_view view{};
    std::vector<body_plan> plans{};
    ragdoll_build_result result{};
};

auto percentile(std::vector<float> values, float share) -> float
{
    if(values.empty())
    {
        return 0.0f;
    }
    const size_t index = std::min(values.size() - 1, size_t(share * float(values.size() - 1) + 0.5f));
    std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(index), values.end());
    return values[index];
}

/// Whether @p direction lies too close to the unit vector @p unit, either way, for a sound axis across both. A zero
/// vector counts as parallel.
auto is_parallel(const math::vec3& unit, const math::vec3& direction) -> bool
{
    const float length = math::length(direction);
    return length <= 0.0f || std::abs(math::dot(unit, direction)) > parallel_cosine * length;
}

auto make_frame(const math::vec3& x_axis, const math::vec3& hint) -> math::quat
{
    const math::vec3 x = math::normalize(x_axis);
    const math::vec3 fallback = is_parallel(x, up_axis) ? z_axis : up_axis;
    const math::vec3 reference = is_parallel(x, hint) ? fallback : hint;
    const math::vec3 z = math::normalize(math::cross(x, reference));
    const math::vec3 y = math::cross(z, x);
    return math::normalize(math::quat_cast(math::mat3(x, y, z)));
}

/// The rotation that turns local +Y onto @p axis.
auto make_y_rotation(const math::vec3& axis) -> math::quat
{
    return math::normalize(math::from_to_rotation(up_axis, math::normalize(axis)));
}

auto find_skin_mesh(const model_component& model) -> std::shared_ptr<mesh>
{
    const auto& lod = model.get_model().get_lod(0);
    return lod ? lod.try_get() : nullptr;
}

/// Why a model has no armature to build on: a mesh still loading gets one soon, a mesh without a skeleton never does.
auto describe_missing_armature(const model_component& model) -> std::string
{
    const auto& lod = model.get_model().get_lod(0);
    if(!lod)
    {
        return "The model has no mesh.";
    }
    const auto loaded = lod.try_get();
    if(!loaded)
    {
        return "The model's mesh is still loading. Try again once it has loaded.";
    }
    return loaded->get_armature() ? "The model's armature is not built yet. Try again in a moment."
                                  : "The model's mesh has no skeleton, so it cannot have a humanoid ragdoll.";
}

/// Names, parents and bind poses of the armature's bones, in the model's space.
void read_bones(const model_component& model, entt::handle owner, skeleton_view& view)
{
    const auto& armature = model.get_armature_entities();
    view.names.resize(armature.size());
    view.parents.assign(armature.size(), -1);
    view.bind = get_bind_poses_in_model_space(model, owner);
    view.positions.resize(armature.size());
    std::unordered_map<entt::entity, int> index_of;
    for(size_t i = 0; i < armature.size(); ++i)
    {
        if(armature[i])
        {
            index_of[armature[i].entity()] = int(i);
            if(const auto* tag = armature[i].try_get<tag_component>())
            {
                view.names[i] = tag->name;
            }
        }
    }
    for(size_t i = 0; i < armature.size(); ++i)
    {
        if(!armature[i])
        {
            continue;
        }
        const auto parent = armature[i].get<transform_component>().get_parent();
        const auto it = parent ? index_of.find(parent.entity()) : index_of.end();
        view.parents[i] = it != index_of.end() ? it->second : -1;
        view.positions[i] = view.bind[i].get_position();
    }
}

void find_roles(const model_component& model, skeleton_view& view)
{
    view.roles = find_humanoid_bones(view.names, view.parents, view.positions);
    if(const auto skin = find_skin_mesh(model))
    {
        apply_humanoid_overrides(skin->get_humanoid_overrides(), view.names, view.roles);
    }
}

/// The character's side and forward axes, and its height from the lowest bone to the highest.
void measure_character(skeleton_view& view)
{
    const math::vec3 side = find_humanoid_side_axis(view.roles, view.bind);
    if(math::length(side) > 0.0f)
    {
        view.side = math::normalize(side);
    }
    view.forward = math::normalize(math::cross(view.side, up_axis));
    float lowest = view.positions[size_t(view.roles[size_t(humanoid_bone::hips)])].y;
    float highest = lowest;
    for(const auto& position : view.positions)
    {
        lowest = std::min(lowest, position.y);
        highest = std::max(highest, position.y);
    }
    view.height = std::max(highest - lowest, min_character_height);
}

auto role_bone(const skeleton_view& view, humanoid_bone role) -> int
{
    return view.roles[size_t(role)];
}

/// The bone of the role after @p role down its limb; -1 when the skeleton lacks it.
auto find_next_bone(const skeleton_view& view, humanoid_bone role) -> int
{
    const auto link = std::find_if(limb_links.begin(),
                                   limb_links.end(),
                                   [role](const role_link& candidate)
                                   {
                                       return candidate.from == role;
                                   });
    return link != limb_links.end() ? role_bone(view, link->to) : -1;
}

/// The bone of the role before @p role up its limb; -1 when the skeleton lacks it.
auto find_previous_bone(const skeleton_view& view, humanoid_bone role) -> int
{
    const auto link = std::find_if(limb_links.begin(),
                                   limb_links.end(),
                                   [role](const role_link& candidate)
                                   {
                                       return candidate.to == role;
                                   });
    return link != limb_links.end() ? role_bone(view, link->from) : -1;
}

/// "Left Upper Arm bone 'upperarm_l'", for messages.
auto describe_bone(const skeleton_view& view, const body_plan& plan) -> std::string
{
    return std::string(get_humanoid_bone_name(plan.role)) + " bone '" + view.names[size_t(plan.bone)] + "'";
}

void add_plan(std::vector<body_plan>& plans, const skeleton_view& view, humanoid_bone role, body_kind kind, float mass)
{
    const int bone = role_bone(view, role);
    if(bone < 0)
    {
        return;
    }
    for(const auto& plan : plans)
    {
        if(plan.bone == bone)
        {
            return;
        }
    }
    body_plan plan;
    plan.role = role;
    plan.kind = kind;
    plan.bone = bone;
    plan.mass_share = mass;
    plans.push_back(plan);
}

auto make_plans(const skeleton_view& view, const ragdoll_build_options& options) -> std::vector<body_plan>
{
    std::vector<body_plan> plans;
    add_plan(plans, view, humanoid_bone::hips, body_kind::torso, pelvis_mass_share);
    add_plan(plans, view, humanoid_bone::spine, body_kind::torso, abdomen_mass_share);
    add_plan(plans, view, humanoid_bone::chest, body_kind::torso, thorax_mass_share * chest_share_of_thorax);
    add_plan(plans,
             view,
             humanoid_bone::upper_chest,
             body_kind::torso,
             thorax_mass_share * (1.0f - chest_share_of_thorax));
    const int neck = role_bone(view, humanoid_bone::neck);
    const int head = role_bone(view, humanoid_bone::head);
    if(options.include_neck && neck >= 0 && head >= 0 &&
       math::distance(view.positions[size_t(neck)], view.positions[size_t(head)]) > min_neck_of_height * view.height)
    {
        add_plan(plans, view, humanoid_bone::neck, body_kind::neck, head_mass_share * neck_share_of_head);
        add_plan(plans, view, humanoid_bone::head, body_kind::head, head_mass_share * (1.0f - neck_share_of_head));
    }
    else
    {
        add_plan(plans, view, humanoid_bone::head, body_kind::head, head_mass_share);
    }
    const std::array<std::array<humanoid_bone, 3>, 2> arms{{
        {humanoid_bone::left_upper_arm, humanoid_bone::left_lower_arm, humanoid_bone::left_hand},
        {humanoid_bone::right_upper_arm, humanoid_bone::right_lower_arm, humanoid_bone::right_hand},
    }};
    for(const auto& arm : arms)
    {
        add_plan(plans, view, arm[0], body_kind::upper_arm, upper_arm_mass_share);
        add_plan(plans, view, arm[1], body_kind::lower_arm, forearm_mass_share);
        if(options.include_hands)
        {
            add_plan(plans, view, arm[2], body_kind::hand, hand_mass_share);
        }
    }
    const std::array<std::array<humanoid_bone, 3>, 2> legs{{
        {humanoid_bone::left_upper_leg, humanoid_bone::left_lower_leg, humanoid_bone::left_foot},
        {humanoid_bone::right_upper_leg, humanoid_bone::right_lower_leg, humanoid_bone::right_foot},
    }};
    for(const auto& leg : legs)
    {
        add_plan(plans, view, leg[0], body_kind::upper_leg, thigh_mass_share);
        add_plan(plans, view, leg[1], body_kind::lower_leg, shank_mass_share);
        if(options.include_feet)
        {
            add_plan(plans, view, leg[2], body_kind::foot, foot_mass_share);
        }
    }
    return plans;
}

auto find_plan_of_bone(const std::vector<body_plan>& plans, int bone) -> int
{
    for(size_t i = 0; i < plans.size(); ++i)
    {
        if(plans[i].bone == bone)
        {
            return int(i);
        }
    }
    return -1;
}

void link_parents(std::vector<body_plan>& plans, const skeleton_view& view)
{
    for(auto& plan : plans)
    {
        plan.parent = -1;
        for(int bone = view.parents[size_t(plan.bone)]; bone >= 0; bone = view.parents[size_t(bone)])
        {
            const int parent = find_plan_of_bone(plans, bone);
            if(parent >= 0)
            {
                plan.parent = parent;
                break;
            }
        }
    }
}

/// The first body below @p index, by the bone hierarchy, closest to it.
auto find_child_plan(const std::vector<body_plan>& plans, int index) -> int
{
    for(size_t i = 0; i < plans.size(); ++i)
    {
        if(plans[i].parent == index &&
           (plans[i].kind == body_kind::torso || plans[i].kind == body_kind::neck || plans[i].kind == body_kind::head))
        {
            return int(i);
        }
    }
    return -1;
}

auto is_shoulder(const skeleton_view& view, int bone) -> bool
{
    return bone >= 0 &&
           (bone == role_bone(view, humanoid_bone::left_shoulder) || bone == role_bone(view, humanoid_bone::right_shoulder));
}

/// Gives each body the vertices that weigh most on its bone or on a bone without a body below it. Shoulder bones
/// keep theirs: they would widen the chest out to the arms.
void collect_vertices(std::vector<body_plan>& plans, const skeleton_view& view, const model_component& model)
{
    const auto skin = find_skin_mesh(model);
    if(!skin)
    {
        return;
    }
    const auto& bones = skin->get_skin_bind_data().get_bones();
    const uint8_t* vertex_data = skin->get_system_vb();
    const uint32_t vertex_count = skin->get_vertex_count();
    const auto& layout = skin->get_vertex_format();
    if(bones.empty() || vertex_data == nullptr || vertex_count == 0 || !layout.has(bgfx::Attrib::Position))
    {
        return;
    }
    std::vector<float> best_weight(vertex_count, 0.0f);
    std::vector<int> best_bone(vertex_count, -1);
    for(size_t b = 0; b < bones.size(); ++b)
    {
        for(const auto& influence : bones[b].influences)
        {
            if(influence.vertex_index < vertex_count && influence.weight > best_weight[influence.vertex_index])
            {
                best_weight[influence.vertex_index] = influence.weight;
                best_bone[influence.vertex_index] = int(b);
            }
        }
    }
    std::vector<int> armature_of_skin_bone(bones.size(), -1);
    std::vector<int> plan_of_skin_bone(bones.size(), -1);
    for(size_t b = 0; b < bones.size(); ++b)
    {
        const auto found = std::find(view.names.begin(), view.names.end(), bones[b].bone_id);
        if(found == view.names.end())
        {
            continue;
        }
        const int armature_index = int(std::distance(view.names.begin(), found));
        armature_of_skin_bone[b] = armature_index;
        for(int bone = armature_index; bone >= 0; bone = view.parents[size_t(bone)])
        {
            const int plan = find_plan_of_bone(plans, bone);
            if(plan >= 0)
            {
                plan_of_skin_bone[b] = plan;
                break;
            }
            if(is_shoulder(view, bone))
            {
                break;
            }
        }
    }
    for(uint32_t v = 0; v < vertex_count; ++v)
    {
        const int skin_bone = best_bone[v];
        if(skin_bone < 0 || plan_of_skin_bone[size_t(skin_bone)] < 0)
        {
            continue;
        }
        float position[4];
        bgfx::vertexUnpack(position, bgfx::Attrib::Position, layout, vertex_data, v);
        const math::vec3 mesh_point{position[0], position[1], position[2]};
        const auto& bone = bones[size_t(skin_bone)];
        const math::vec3 bone_point = bone.bind_pose_transform.transform_coord(mesh_point);
        const math::vec3 model_point =
            view.bind[size_t(armature_of_skin_bone[size_t(skin_bone)])].transform_coord(bone_point);
        plans[size_t(plan_of_skin_bone[size_t(skin_bone)])].vertices.push_back(model_point);
    }
}

auto distance_to_segment(const math::vec3& point, const math::vec3& a, const math::vec3& b) -> float
{
    const math::vec3 ab = b - a;
    const float length_squared = math::dot(ab, ab);
    const float t = length_squared > 0.0f ? math::clamp(math::dot(point - a, ab) / length_squared, 0.0f, 1.0f) : 0.0f;
    return math::distance(point, a + ab * t);
}

/// Radius of the vertices around the inner part of a segment, by percentile.
auto fit_segment_radius(const body_plan& plan, float share, float& radius) -> bool
{
    const math::vec3 axis = plan.end - plan.start;
    const float length = math::length(axis);
    if(length <= 0.0f)
    {
        return false;
    }
    const math::vec3 direction = axis / length;
    std::vector<float> distances;
    distances.reserve(plan.vertices.size());
    for(const auto& vertex : plan.vertices)
    {
        const float t = math::dot(vertex - plan.start, direction) / length;
        if(t < limb_fit_margin || t > 1.0f - limb_fit_margin)
        {
            continue;
        }
        distances.push_back(distance_to_segment(vertex, plan.start, plan.end));
    }
    if(distances.size() < min_fit_vertices)
    {
        return false;
    }
    radius = percentile(std::move(distances), share);
    return radius > 0.0f;
}

auto project_extent(const std::vector<math::vec3>& points,
                    const math::vec3& origin,
                    const math::vec3& axis,
                    float low,
                    float high) -> math::vec2
{
    std::vector<float> values;
    values.reserve(points.size());
    for(const auto& point : points)
    {
        values.push_back(math::dot(point - origin, axis));
    }
    return {percentile(values, low), percentile(values, high)};
}

void set_capsule(body_plan& plan, const math::vec3& a, const math::vec3& b, float radius)
{
    const math::vec3 axis = b - a;
    const float length = math::length(axis);
    plan.center = (a + b) * 0.5f;
    plan.radius = radius;
    plan.segment = std::max(length - 2.0f * radius, 0.0f);
    plan.rotation = length > 0.0f ? make_y_rotation(axis / length) : math::quat{1.0f, 0.0f, 0.0f, 0.0f};
}

auto get_limb_fit(body_kind kind) -> const capsule_fit&
{
    switch(kind)
    {
        case body_kind::upper_arm:
            return upper_arm_fit;
        case body_kind::lower_arm:
            return lower_arm_fit;
        case body_kind::upper_leg:
            return upper_leg_fit;
        default:
            return lower_leg_fit;
    }
}

/// A capsule from the plan's start to its end, its radius fitted to the vertices around it, else a share of its
/// length.
void fit_capsule(body_plan& plan, const skeleton_view& view, const capsule_fit& fit)
{
    const float length = math::distance(plan.start, plan.end);
    float radius = length * fit.radius_of_length;
    plan.is_fitted = fit_segment_radius(plan, fit.percentile, radius);
    radius = math::clamp(radius, min_radius_of_height * view.height, length * fit.max_radius_of_length);
    set_capsule(plan, plan.start, plan.end, radius);
}

/// A limb from its bone to the next one down the limb. Refuses a limb whose next bone sits on its start: it has no
/// length to size a capsule by and no direction for its joint.
auto fit_limb(body_plan& plan, const skeleton_view& view, std::string& error) -> bool
{
    const int next = find_next_bone(view, plan.role);
    plan.end =
        next >= 0 ? view.positions[size_t(next)] : plan.start - up_axis * (view.height * missing_limb_end_of_height);
    if(math::distance(plan.start, plan.end) <= min_segment_of_height * view.height)
    {
        error = "The " + describe_bone(view, plan) + " has no length: the next bone down the limb, '" +
                view.names[size_t(next)] + "', sits at its start.";
        return false;
    }
    fit_capsule(plan, view, get_limb_fit(plan.kind));
    return true;
}

/// Pelvis and spine: a capsule across the body, as wide as the body and as thick as it is deep.
void fit_torso(body_plan& plan, const skeleton_view& view, const character_frame& frame)
{
    // A spine bone on top of the one below it leaves no segment to size from: a typical segment stands in.
    const float segment = math::distance(plan.start, plan.end);
    const float height =
        segment > min_segment_of_height * view.height ? segment : view.height * missing_trunk_end_of_height;
    float width = height * torso_width_of_segment;
    float depth = height * torso_depth_of_segment;
    float side_center = 0.0f;
    float forward_center = 0.0f;
    if(plan.vertices.size() >= min_fit_vertices)
    {
        const math::vec2 across = project_extent(plan.vertices, plan.start, frame.side, extent_low, extent_high);
        const math::vec2 front = project_extent(plan.vertices, plan.start, frame.forward, extent_low, extent_high);
        width = across.y - across.x;
        depth = front.y - front.x;
        side_center = (across.x + across.y) * 0.5f;
        forward_center = (front.x + front.y) * 0.5f;
        plan.is_fitted = true;
    }
    const float radius = math::clamp(depth * 0.5f, min_radius_of_height * view.height, width * 0.5f);
    const float up_center = math::dot((plan.start + plan.end) * 0.5f - plan.start, frame.up);
    plan.center = plan.start + frame.side * side_center + frame.forward * forward_center + frame.up * up_center;
    plan.radius = radius;
    plan.segment = std::max(width - 2.0f * radius, 0.0f);
    plan.rotation = make_y_rotation(frame.side);
}

void fit_head(body_plan& plan, const skeleton_view& view, const character_frame& frame)
{
    float width = view.height * head_height_of_height * head_width_of_head_height;
    float depth = width * head_depth_of_width;
    math::vec2 vertical{0.0f, view.height * head_height_of_height};
    float side_center = 0.0f;
    float forward_center = width * head_forward_of_width;
    if(plan.vertices.size() >= min_fit_vertices)
    {
        const math::vec2 across = project_extent(plan.vertices, plan.start, frame.side, tight_low, tight_high);
        const math::vec2 front = project_extent(plan.vertices, plan.start, frame.forward, tight_low, tight_high);
        vertical = project_extent(plan.vertices, plan.start, frame.up, tight_low, tight_high);
        width = across.y - across.x;
        depth = front.y - front.x;
        side_center = (across.x + across.y) * 0.5f;
        forward_center = (front.x + front.y) * 0.5f;
        plan.is_fitted = true;
    }
    const float height = vertical.y - vertical.x;
    const float radius = math::clamp(std::min(width, depth) * 0.5f, min_radius_of_height * view.height, height * 0.5f);
    plan.center = plan.start + frame.side * side_center + frame.forward * forward_center +
                  frame.up * ((vertical.x + vertical.y) * 0.5f);
    plan.radius = radius;
    plan.segment = std::max(height - 2.0f * radius, 0.0f);
    plan.rotation = math::quat{1.0f, 0.0f, 0.0f, 0.0f};
    plan.end = plan.start + frame.up * vertical.y;
}

void fit_hand(body_plan& plan, const skeleton_view& view, const math::vec3& forearm_start)
{
    const math::vec3 forearm = plan.start - forearm_start;
    const math::vec3 direction = math::length(forearm) > 0.0f ? math::normalize(forearm) : -up_axis;
    float length = math::length(forearm) * hand_length_of_forearm;
    if(plan.vertices.size() >= min_fit_vertices)
    {
        const math::vec2 reach = project_extent(plan.vertices, plan.start, direction, tight_low, tight_high);
        length = std::max(reach.y, length * min_hand_reach_of_guess);
    }
    plan.end = plan.start + direction * length;
    float radius = length * hand_radius_of_length;
    plan.is_fitted = fit_segment_radius(plan, flat_radius_percentile, radius);
    radius = math::clamp(radius, min_radius_of_height * view.height, length * max_hand_radius_of_length);
    set_capsule(plan, plan.start, plan.end, radius);
}

/// Feet: a box on the sole, from the heel to the toes.
void fit_foot(body_plan& plan, const skeleton_view& view, const character_frame& frame, const math::vec3& toe_hint)
{
    math::vec3 forward = toe_hint - plan.start;
    forward.y = 0.0f;
    forward = math::length(forward) > 0.0f ? math::normalize(forward) : frame.forward;
    const math::vec3 side = math::normalize(math::cross(up_axis, forward));
    float lowest = plan.start.y;
    for(const auto& position : view.positions)
    {
        lowest = std::min(lowest, position.y);
    }
    const float ankle_height = std::max(plan.start.y - lowest, min_ankle_of_height * view.height);
    const float foot_length =
        std::max(math::distance(plan.start, toe_hint) * foot_length_of_ankle_to_ball, min_foot_of_height * view.height);
    math::vec2 along{-heel_of_foot_length * foot_length, toes_of_foot_length * foot_length};
    math::vec2 across{-foot_half_width_of_length * foot_length, foot_half_width_of_length * foot_length};
    math::vec2 vertical{-ankle_height, -foot_top_below_ankle * ankle_height};
    if(plan.vertices.size() >= min_fit_vertices)
    {
        along = project_extent(plan.vertices, plan.start, forward, tight_low, tight_high);
        across = project_extent(plan.vertices, plan.start, side, tight_low, tight_high);
        vertical = project_extent(plan.vertices, plan.start, up_axis, 0.0f, tight_high);
        vertical.y = vertical.x + (vertical.y - vertical.x) * foot_box_of_fitted_height;
        plan.is_fitted = true;
    }
    plan.is_box = true;
    plan.box_size = {across.y - across.x, vertical.y - vertical.x, along.y - along.x};
    plan.center = plan.start + side * ((across.x + across.y) * 0.5f) + up_axis * ((vertical.x + vertical.y) * 0.5f) +
                  forward * ((along.x + along.y) * 0.5f);
    plan.rotation = math::normalize(math::quat_cast(math::mat3(side, up_axis, forward)));
    plan.end = plan.start + forward * along.y;
}

/// Where a torso or neck body ends: at the trunk body above it, else a short way up.
auto find_trunk_end(const std::vector<body_plan>& plans, size_t index, const skeleton_view& view) -> math::vec3
{
    const int child = find_child_plan(plans, int(index));
    return child >= 0 ? view.positions[size_t(plans[size_t(child)].bone)]
                      : plans[index].start + up_axis * (view.height * missing_trunk_end_of_height);
}

/// Where a hand's forearm starts: at its elbow, else a forearm's length above the wrist.
auto find_forearm_start(const skeleton_view& view, const body_plan& hand) -> math::vec3
{
    const int elbow = find_previous_bone(view, hand.role);
    return elbow >= 0 ? view.positions[size_t(elbow)]
                      : hand.start + up_axis * (view.height * missing_forearm_of_height);
}

/// Where a foot's toes start: at its toe bone, else ahead of the ankle.
auto find_toes(const skeleton_view& view, const body_plan& foot, const character_frame& frame) -> math::vec3
{
    const int toes = find_next_bone(view, foot.role);
    return toes >= 0 ? view.positions[size_t(toes)]
                     : foot.start + frame.forward * (view.height * missing_toes_of_height);
}

auto fit_shape(build_context& ctx, size_t index, const character_frame& frame) -> bool
{
    auto& plan = ctx.plans[index];
    const auto& view = ctx.view;
    plan.start = view.positions[size_t(plan.bone)];
    switch(plan.kind)
    {
        case body_kind::torso:
            plan.end = find_trunk_end(ctx.plans, index, view);
            fit_torso(plan, view, frame);
            break;
        case body_kind::neck:
            plan.end = find_trunk_end(ctx.plans, index, view);
            fit_capsule(plan, view, neck_fit);
            break;
        case body_kind::head:
            fit_head(plan, view, frame);
            break;
        case body_kind::hand:
            fit_hand(plan, view, find_forearm_start(view, plan));
            break;
        case body_kind::foot:
            fit_foot(plan, view, frame, find_toes(view, plan, frame));
            break;
        case body_kind::upper_arm:
        case body_kind::lower_arm:
        case body_kind::upper_leg:
        case body_kind::lower_leg:
            return fit_limb(plan, view, ctx.result.error);
    }
    return true;
}

/// Flips @p axis so that turning @p direction about it by a positive angle moves it towards @p wanted.
auto orient_axis(const math::vec3& axis, const math::vec3& direction, const math::vec3& wanted) -> math::vec3
{
    // A small positive turn moves the direction along cross(axis, direction).
    return math::dot(math::cross(axis, direction), wanted) >= 0.0f ? axis : -axis;
}

/// The first axis across @p direction and one of @p references, in order, that is not near parallel to it.
auto find_axis_across(const math::vec3& direction, const std::array<math::vec3, 2>& references)
    -> hpp::optional<math::vec3>
{
    const auto usable = std::find_if(references.begin(),
                                     references.end(),
                                     [&direction](const math::vec3& reference)
                                     {
                                         return !is_parallel(direction, reference);
                                     });
    if(usable == references.end())
    {
        return hpp::nullopt;
    }
    return math::normalize(math::cross(direction, *usable));
}

auto get_segment_direction(const body_plan& plan) -> math::vec3
{
    const math::vec3 axis = plan.end - plan.start;
    return math::length(axis) > 0.0f ? math::normalize(axis) : up_axis;
}

/// Angle between two unit vectors, degrees.
auto get_angle_degrees(const math::vec3& a, const math::vec3& b) -> float
{
    return math::degrees(std::acos(math::clamp(math::dot(a, b), -1.0f, 1.0f)));
}

void set_ball(body_plan& plan, const ball_range& range)
{
    plan.joint = ragdoll_joint_type::ball;
    plan.swing = range.swing;
    plan.twist_lower = range.twist_lower;
    plan.twist_upper = range.twist_upper;
    plan.friction_scale = range.friction_scale;
}

void set_hinge(body_plan& plan, const hinge_range& range)
{
    plan.joint = ragdoll_joint_type::hinge;
    plan.twist_lower = range.lower;
    plan.twist_upper = range.upper;
    plan.friction_scale = range.friction_scale;
}

auto has_neck_body(const std::vector<body_plan>& plans, const body_plan& plan) -> bool
{
    return plan.parent >= 0 && plans[size_t(plan.parent)].kind == body_kind::neck;
}

/// Degrees to turn the shoulder cone's centre from an arm's bind direction, @p to_hanging degrees from hanging, towards
/// hanging: to halfway down, keeping the bind pose inside the cone, and hanging inside it above all.
auto find_shoulder_offset(float to_hanging) -> float
{
    const float reach = shoulder_range.swing - min_cone_margin_degrees;
    const float halfway = math::clamp(to_hanging - shoulder_cone_from_hanging_degrees, 0.0f, reach);
    return std::max(halfway, to_hanging - reach);
}

/// The shoulder's cone centres on the arm halfway between hanging and held out level, turned from the arm's bind
/// direction, so a T-pose and an A-pose rig get the same cone and a hanging arm lies well inside it. The turn is
/// about the joint frame's Y axis: the backend turns its own frame about Y, so the offset means the same on both.
void plan_shoulder(body_plan& plan, const math::vec3& forward)
{
    const math::vec3 direction = get_segment_direction(plan);
    const math::vec3 hanging = -up_axis;
    const math::vec3 turn_axis = math::cross(direction, hanging);
    const float offset = find_shoulder_offset(get_angle_degrees(direction, hanging));
    const bool has_offset = offset > 0.0f && math::length(turn_axis) > min_axis_length;
    plan.joint_frame = make_frame(direction, has_offset ? turn_axis : forward);
    plan.swing_offset = has_offset ? math::vec3{0.0f, offset, 0.0f} : math::vec3{0.0f};
    set_ball(plan, shoulder_range);
}

/// The elbow folds the forearm towards the front of the body; a forearm pointing forward folds up instead. Refuses
/// a forearm with no axis across it.
auto plan_elbow(body_plan& plan, const skeleton_view& view, std::string& error) -> bool
{
    const math::vec3 direction = get_segment_direction(plan);
    const auto hinge = find_axis_across(direction, {view.forward, up_axis});
    if(!hinge)
    {
        error = "The " + describe_bone(view, plan) +
                " gives the elbow no hinge axis: the forearm lies along both the character's forward and up axes.";
        return false;
    }
    const math::vec3 axis = orient_axis(*hinge, direction, view.forward + up_axis * elbow_fold_lift);
    plan.joint_frame = make_frame(axis, direction);
    set_hinge(plan, elbow_range);
    return true;
}

/// Human ranges of motion, with the twist axis and its sign chosen so positive angles bend the way the joint bends.
auto plan_joint(build_context& ctx, body_plan& plan) -> bool
{
    const math::vec3 forward = ctx.view.forward;
    const math::vec3 direction = get_segment_direction(plan);
    switch(plan.kind)
    {
        case body_kind::torso:
            plan.joint_frame = make_frame(direction, forward);
            set_ball(plan, torso_range);
            break;
        case body_kind::neck:
            plan.joint_frame = make_frame(direction, forward);
            set_ball(plan, neck_range);
            break;
        case body_kind::head:
            plan.joint_frame = make_frame(up_axis, forward);
            set_ball(plan, has_neck_body(ctx.plans, plan) ? head_on_neck_range : head_range);
            break;
        case body_kind::upper_arm:
            plan_shoulder(plan, forward);
            break;
        case body_kind::lower_arm:
            return plan_elbow(plan, ctx.view, ctx.result.error);
        case body_kind::hand:
            plan.joint_frame = make_frame(direction, forward);
            set_ball(plan, wrist_range);
            break;
        case body_kind::upper_leg:
            // The hip flexes through its twist, about the side axis. Legs hang in the bind pose, so the cone stays
            // centred on it.
            plan.joint_frame = make_frame(orient_axis(ctx.view.side, direction, forward), direction);
            set_ball(plan, hip_range);
            break;
        case body_kind::lower_leg:
            // The knee bends the shin back.
            plan.joint_frame = make_frame(orient_axis(ctx.view.side, direction, -forward), direction);
            set_hinge(plan, knee_range);
            break;
        case body_kind::foot:
        {
            const math::vec3 shin = plan.parent >= 0 ? get_segment_direction(ctx.plans[size_t(plan.parent)]) : -up_axis;
            plan.joint_frame = make_frame(orient_axis(ctx.view.side, shin, forward), shin);
            set_ball(plan, ankle_range);
            break;
        }
    }
    return true;
}

struct capsule_proxy
{
    math::vec3 a;
    math::vec3 b;
    float radius;
};

auto make_proxy(const body_plan& plan) -> capsule_proxy
{
    if(plan.is_box)
    {
        // The box's longest axis with the larger of the other two half sizes, which covers it.
        const math::vec3 half = plan.box_size * 0.5f;
        const math::vec3 axis = plan.rotation * math::vec3{0.0f, 0.0f, 1.0f};
        const float radius = std::max(half.x, half.y);
        const float reach = std::max(half.z - radius, 0.0f);
        return {plan.center - axis * reach, plan.center + axis * reach, radius};
    }
    const math::vec3 axis = plan.rotation * up_axis;
    return {plan.center - axis * (plan.segment * 0.5f), plan.center + axis * (plan.segment * 0.5f), plan.radius};
}

auto segment_distance(const math::vec3& p1, const math::vec3& q1, const math::vec3& p2, const math::vec3& q2) -> float
{
    const math::vec3 d1 = q1 - p1;
    const math::vec3 d2 = q2 - p2;
    const math::vec3 r = p1 - p2;
    const float a = math::dot(d1, d1);
    const float e = math::dot(d2, d2);
    const float f = math::dot(d2, r);
    float s = 0.0f;
    float t = 0.0f;
    if(a <= min_squared_length && e <= min_squared_length)
    {
        return math::distance(p1, p2);
    }
    if(a <= min_squared_length)
    {
        t = math::clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const float c = math::dot(d1, r);
        if(e <= min_squared_length)
        {
            s = math::clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const float b = math::dot(d1, d2);
            const float denominator = a * e - b * b;
            s = denominator > min_squared_length ? math::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if(t < 0.0f)
            {
                t = 0.0f;
                s = math::clamp(-c / a, 0.0f, 1.0f);
            }
            else if(t > 1.0f)
            {
                t = 1.0f;
                s = math::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    return math::distance(p1 + d1 * s, p2 + d2 * t);
}

auto find_disabled_pairs(const std::vector<body_plan>& plans, const skeleton_view& view) -> std::vector<ragdoll_bone_pair>
{
    std::vector<ragdoll_bone_pair> pairs;
    const float margin = overlap_margin_of_height * view.height;
    const auto add_pair = [&](size_t i, size_t j)
    {
        pairs.push_back({view.names[size_t(plans[i].bone)], view.names[size_t(plans[j].bone)]});
    };
    for(size_t i = 0; i < plans.size(); ++i)
    {
        for(size_t j = i + 1; j < plans.size(); ++j)
        {
            const bool adjacent = plans[i].parent == int(j) || plans[j].parent == int(i);
            if(adjacent)
            {
                continue;
            }
            const auto lhs = make_proxy(plans[i]);
            const auto rhs = make_proxy(plans[j]);
            const bool both_thighs = plans[i].kind == body_kind::upper_leg && plans[j].kind == body_kind::upper_leg;
            if(both_thighs || segment_distance(lhs.a, lhs.b, rhs.a, rhs.b) < lhs.radius + rhs.radius + margin)
            {
                add_pair(i, j);
            }
        }
    }
    return pairs;
}

/// The bone's scale as the backend applies it to shapes: its absolute value, with a vanished axis taken as 1.
auto get_shape_scale(const math::transform& transform) -> math::vec3
{
    const auto to_usable = [](float value)
    {
        const float size = std::abs(value);
        return size > 0.0f ? size : 1.0f;
    };
    const math::vec3& scale = transform.get_scale();
    return {to_usable(scale.x), to_usable(scale.y), to_usable(scale.z)};
}

auto get_uniform_scale(const math::vec3& shape_scale) -> float
{
    return (shape_scale.x + shape_scale.y + shape_scale.z) / 3.0f;
}

auto to_degrees(const math::quat& rotation) -> math::vec3
{
    return math::degrees(math::eulerAngles(math::normalize(rotation)));
}

auto make_body(const body_plan& plan, const skeleton_view& view, float mass_scale) -> ragdoll_body
{
    const auto& bone = view.bind[size_t(plan.bone)];
    const math::vec3 shape_scale = get_shape_scale(bone);
    const float scale = get_uniform_scale(shape_scale);
    const math::quat inverse_bone = glm::inverse(math::normalize(bone.get_rotation()));
    // The backend places a shape by the bone's rotation and the absolute value of its scale. The full inverse would
    // reflect a mirrored bone's centre through the bone.
    const math::vec3 center = (inverse_bone * (plan.center - bone.get_position())) / shape_scale;
    const math::vec3 rotation = to_degrees(inverse_bone * plan.rotation);
    ragdoll_body body;
    body.bone = view.names[size_t(plan.bone)];
    if(plan.is_box)
    {
        physics_box_shape box;
        box.center = center;
        box.extends = plan.box_size / scale;
        box.rotation = rotation;
        body.shape.shape = box;
    }
    else
    {
        physics_capsule_shape capsule;
        capsule.center = center;
        capsule.radius = plan.radius / scale;
        capsule.length = plan.segment / scale;
        capsule.rotation = rotation;
        body.shape.shape = capsule;
    }
    body.mass = mass_scale * plan.mass_share;
    body.joint = plan.joint;
    body.joint_rotation = to_degrees(inverse_bone * plan.joint_frame);
    body.swing_offset = plan.swing_offset;
    body.swing_limit = plan.swing;
    body.twist_lower = plan.twist_lower;
    body.twist_upper = plan.twist_upper;
    body.friction_scale = plan.friction_scale;
    return body;
}

auto is_finite(const math::vec3& value) -> bool
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

/// Finite and above zero.
auto is_positive(float value) -> bool
{
    return std::isfinite(value) && value > 0.0f;
}

auto is_usable_capsule(const physics_capsule_shape& capsule) -> bool
{
    const bool is_placed = is_finite(capsule.center) && is_finite(capsule.rotation);
    const bool has_length = std::isfinite(capsule.length) && capsule.length >= 0.0f;
    return is_placed && has_length && is_positive(capsule.radius);
}

auto is_usable_box(const physics_box_shape& box) -> bool
{
    const bool is_placed = is_finite(box.center) && is_finite(box.rotation);
    return is_placed && is_positive(box.extends.x) && is_positive(box.extends.y) && is_positive(box.extends.z);
}

auto has_usable_shape(const ragdoll_body& body) -> bool
{
    const auto* capsule = hpp::get_if<physics_capsule_shape>(&body.shape.shape);
    const auto* box = hpp::get_if<physics_box_shape>(&body.shape.shape);
    return capsule != nullptr ? is_usable_capsule(*capsule) : box != nullptr && is_usable_box(*box);
}

auto has_usable_joint(const ragdoll_body& body) -> bool
{
    return is_finite(body.joint_rotation) && is_finite(body.swing_offset);
}

/// Refuses a body whose shape or joint came out without a size or with a number that is not one, rather than save it.
auto check_body(const ragdoll_body& body, const body_plan& plan, const skeleton_view& view, std::string& error) -> bool
{
    if(!has_usable_shape(body))
    {
        error = "The " + describe_bone(view, plan) +
                " gives its body no usable shape: its bones and vertices leave it without a size.";
        return false;
    }
    if(!has_usable_joint(body))
    {
        error = "The " + describe_bone(view, plan) + " gives its joint no usable frame.";
        return false;
    }
    return true;
}

// --- build steps ----------------------------------------------------------------------------

/// Refuses a total mass the bodies cannot share out: zero, negative, infinite or not a number.
auto check_total_mass(build_context& ctx) -> bool
{
    const float total_mass = ctx.options.total_mass;
    if(std::isnormal(total_mass) && total_mass > 0.0f)
    {
        return true;
    }
    ctx.result.error = "The total mass must be a finite number of kilograms above zero.";
    return false;
}

auto read_skeleton(build_context& ctx) -> bool
{
    if(ctx.model.get_armature_entities().empty())
    {
        ctx.result.error = describe_missing_armature(ctx.model);
        return false;
    }
    read_bones(ctx.model, ctx.model_entity, ctx.view);
    find_roles(ctx.model, ctx.view);
    if(role_bone(ctx.view, humanoid_bone::hips) < 0)
    {
        ctx.result.error = "No humanoid skeleton was found: the hips (two legs below a spine) are missing.";
        return false;
    }
    measure_character(ctx.view);
    return true;
}

auto plan_bodies(build_context& ctx) -> bool
{
    ctx.plans = make_plans(ctx.view, ctx.options);
    link_parents(ctx.plans, ctx.view);
    if(!ctx.plans.empty())
    {
        return true;
    }
    ctx.result.error = "The humanoid skeleton has no bones for bodies.";
    return false;
}

auto collect_skin_vertices(build_context& ctx) -> bool
{
    if(ctx.options.fit_to_skin)
    {
        collect_vertices(ctx.plans, ctx.view, ctx.model);
    }
    return true;
}

auto fit_shapes(build_context& ctx) -> bool
{
    const character_frame frame{ctx.view.side, up_axis, ctx.view.forward};
    for(size_t i = 0; i < ctx.plans.size(); ++i)
    {
        if(!fit_shape(ctx, i, frame))
        {
            return false;
        }
    }
    return true;
}

auto plan_joints(build_context& ctx) -> bool
{
    for(auto& plan : ctx.plans)
    {
        if(!plan_joint(ctx, plan))
        {
            return false;
        }
    }
    return true;
}

/// Shares the total mass out by each body's share and turns the plans into checked bodies in their bones' space.
auto make_bodies(build_context& ctx) -> bool
{
    const float share_sum = std::accumulate(ctx.plans.begin(),
                                            ctx.plans.end(),
                                            0.0f,
                                            [](float sum, const body_plan& plan)
                                            {
                                                return sum + plan.mass_share;
                                            });
    const float mass_scale = ctx.options.total_mass / share_sum;
    std::vector<ragdoll_body> bodies;
    bodies.reserve(ctx.plans.size());
    for(const auto& plan : ctx.plans)
    {
        bodies.push_back(make_body(plan, ctx.view, mass_scale));
        if(!check_body(bodies.back(), plan, ctx.view, ctx.result.error))
        {
            return false;
        }
    }
    ctx.result.bodies = std::move(bodies);
    ctx.result.skin_fitted_bodies = int(std::count_if(ctx.plans.begin(),
                                                      ctx.plans.end(),
                                                      [](const body_plan& plan)
                                                      {
                                                          return plan.is_fitted;
                                                      }));
    return true;
}

auto disable_overlapping_pairs(build_context& ctx) -> bool
{
    ctx.result.disabled_collisions = find_disabled_pairs(ctx.plans, ctx.view);
    return true;
}

using build_step = auto (*)(build_context&) -> bool;

/// Auto Setup in order. A step that refuses the rig stops it and leaves the reason in the result.
constexpr std::array<build_step, 8> build_steps{
    check_total_mass,
    read_skeleton,
    plan_bodies,
    collect_skin_vertices,
    fit_shapes,
    plan_joints,
    make_bodies,
    disable_overlapping_pairs,
};
} // namespace ragdoll_builder_detail

auto build_humanoid_ragdoll(const model_component& model, entt::handle model_entity, const ragdoll_build_options& options)
    -> ragdoll_build_result
{
    namespace detail = ragdoll_builder_detail;
    detail::build_context ctx{model, model_entity, options};
    ctx.result.ok = std::all_of(detail::build_steps.begin(),
                                detail::build_steps.end(),
                                [&ctx](detail::build_step step)
                                {
                                    return step(ctx);
                                });
    return std::move(ctx.result);
}

auto setup_ragdoll(ragdoll_component& ragdoll, const ragdoll_build_options& options) -> ragdoll_build_result
{
    const auto owner = ragdoll.get_owner();
    const auto* model = owner ? owner.try_get<model_component>() : nullptr;
    if(model == nullptr)
    {
        ragdoll_build_result result;
        result.error = "The ragdoll's entity has no model component.";
        return result;
    }
    auto result = build_humanoid_ragdoll(*model, owner, options);
    if(result.ok)
    {
        ragdoll.set_bodies(result.bodies);
        ragdoll.set_disabled_collisions(result.disabled_collisions);
    }
    return result;
}

} // namespace unravel
