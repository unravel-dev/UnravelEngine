#include "ragdoll_builder.h"

#include <engine/animation/animation_humanoid.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/physics/ecs/systems/ragdoll_poses.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace ragdoll_builder_detail
{
const math::vec3 up_axis{0.0f, 1.0f, 0.0f};
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
/// Limb radius as a share of the limb's length when there are no vertices to fit.
constexpr float upper_arm_radius_of_length = 0.18f;
constexpr float lower_arm_radius_of_length = 0.15f;
constexpr float upper_leg_radius_of_length = 0.19f;
constexpr float lower_leg_radius_of_length = 0.13f;
constexpr float hand_length_of_forearm = 0.7f;
constexpr float head_height_of_height = 0.13f;
/// A limb capsule is never thicker than this share of its length.
constexpr float max_radius_of_length = 0.45f;
constexpr float test_turn_degrees = 30.0f;

/// Mass shares of the body segments, from Winter, Biomechanics and Motor Control of Human Movement (table 4.1).
constexpr float head_mass_share = 0.081f;
constexpr float neck_share_of_head = 0.15f;
constexpr float pelvis_mass_share = 0.142f;
constexpr float abdomen_mass_share = 0.139f;
constexpr float thorax_mass_share = 0.216f;
constexpr float upper_arm_mass_share = 0.028f;
constexpr float forearm_mass_share = 0.016f;
constexpr float hand_mass_share = 0.006f;
constexpr float thigh_mass_share = 0.100f;
constexpr float shank_mass_share = 0.0465f;
constexpr float foot_mass_share = 0.0145f;

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

auto make_frame(const math::vec3& x_axis, const math::vec3& hint) -> math::quat
{
    const math::vec3 x = math::normalize(x_axis);
    math::vec3 reference = hint;
    if(std::abs(math::dot(x, math::normalize(reference))) > 0.95f)
    {
        reference = std::abs(x.y) < 0.9f ? up_axis : math::vec3{0.0f, 0.0f, 1.0f};
    }
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

auto read_skeleton(const model_component& model, entt::handle owner, skeleton_view& view) -> bool
{
    const auto& armature = model.get_armature_entities();
    if(armature.empty())
    {
        return false;
    }
    view.names.resize(armature.size());
    view.parents.assign(armature.size(), -1);
    view.bind.resize(armature.size());
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
        view.bind[i] = get_bind_pose_in_model_space(model, owner, int(i));
        view.positions[i] = view.bind[i].get_position();
    }
    view.roles = find_humanoid_bones(view.names, view.parents, view.positions);
    if(const auto skin = find_skin_mesh(model))
    {
        apply_humanoid_overrides(skin->get_humanoid_overrides(), view.names, view.roles);
    }
    if(view.roles[size_t(humanoid_bone::hips)] < 0)
    {
        return false;
    }
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
    view.height = std::max(highest - lowest, 0.1f);
    return true;
}

auto role_bone(const skeleton_view& view, humanoid_bone role) -> int
{
    return view.roles[size_t(role)];
}

auto role_position(const skeleton_view& view, humanoid_bone role) -> math::vec3
{
    return view.positions[size_t(role_bone(view, role))];
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
    add_plan(plans, view, humanoid_bone::chest, body_kind::torso, thorax_mass_share * 0.4f);
    add_plan(plans, view, humanoid_bone::upper_chest, body_kind::torso, thorax_mass_share * 0.6f);
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

auto limb_radius_of_length(body_kind kind) -> float
{
    switch(kind)
    {
        case body_kind::upper_arm:
            return upper_arm_radius_of_length;
        case body_kind::lower_arm:
            return lower_arm_radius_of_length;
        case body_kind::upper_leg:
            return upper_leg_radius_of_length;
        case body_kind::lower_leg:
            return lower_leg_radius_of_length;
        default:
            return lower_arm_radius_of_length;
    }
}

void fit_limb(body_plan& plan, const skeleton_view& view)
{
    const float length = math::distance(plan.start, plan.end);
    float radius = length * limb_radius_of_length(plan.kind);
    plan.is_fitted = fit_segment_radius(plan, limb_radius_percentile, radius);
    radius = math::clamp(radius, min_radius_of_height * view.height, length * max_radius_of_length);
    set_capsule(plan, plan.start, plan.end, radius);
}

/// Pelvis and spine: a capsule across the body, as wide as the body and as thick as it is deep.
void fit_torso(body_plan& plan, const skeleton_view& view, const character_frame& frame)
{
    const float height = math::distance(plan.start, plan.end);
    float width = height * 2.0f;
    float depth = height * 1.2f;
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
    const float segment = std::max(width - 2.0f * radius, 0.0f);
    const float up_center = math::dot((plan.start + plan.end) * 0.5f - plan.start, frame.up);
    plan.center = plan.start + frame.side * side_center + frame.forward * forward_center + frame.up * up_center;
    plan.radius = radius;
    plan.segment = segment;
    plan.rotation = make_y_rotation(frame.side);
}

void fit_head(body_plan& plan, const skeleton_view& view, const character_frame& frame)
{
    float width = view.height * head_height_of_height * 0.75f;
    float depth = width * 1.1f;
    math::vec2 vertical{0.0f, view.height * head_height_of_height};
    float side_center = 0.0f;
    float forward_center = width * 0.1f;
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
        length = std::max(reach.y, length * 0.5f);
    }
    plan.end = plan.start + direction * length;
    float radius = length * 0.2f;
    plan.is_fitted = fit_segment_radius(plan, flat_radius_percentile, radius);
    radius = math::clamp(radius, min_radius_of_height * view.height, length * 0.3f);
    set_capsule(plan, plan.start, plan.end, radius);
}

void fit_neck(body_plan& plan, const skeleton_view& view)
{
    const float length = math::distance(plan.start, plan.end);
    float radius = length * 0.5f;
    plan.is_fitted = fit_segment_radius(plan, flat_radius_percentile, radius);
    radius = math::clamp(radius, min_radius_of_height * view.height, length * 0.6f);
    // The neck capsule stops short of the head and the chest, which carry the rest of it.
    set_capsule(plan, plan.start, plan.end, radius);
    plan.segment = std::max(length - 2.0f * radius, 0.0f);
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
    const float ankle_height = std::max(plan.start.y - lowest, 0.03f * view.height);
    const float foot_length = std::max(math::distance(plan.start, toe_hint) * 1.6f, 0.1f * view.height);
    math::vec2 along{-0.25f * foot_length, 0.75f * foot_length};
    math::vec2 across{-0.2f * foot_length, 0.2f * foot_length};
    math::vec2 vertical{-ankle_height, -0.3f * ankle_height};
    if(plan.vertices.size() >= min_fit_vertices)
    {
        along = project_extent(plan.vertices, plan.start, forward, tight_low, tight_high);
        across = project_extent(plan.vertices, plan.start, side, tight_low, tight_high);
        vertical = project_extent(plan.vertices, plan.start, up_axis, 0.0f, tight_high);
        // The top of the ankle stays out of the box: the shin capsule covers it.
        vertical.y = vertical.x + (vertical.y - vertical.x) * 0.7f;
        plan.is_fitted = true;
    }
    plan.is_box = true;
    plan.box_size = {across.y - across.x, vertical.y - vertical.x, along.y - along.x};
    plan.center = plan.start + side * ((across.x + across.y) * 0.5f) + up_axis * ((vertical.x + vertical.y) * 0.5f) +
                  forward * ((along.x + along.y) * 0.5f);
    plan.rotation = math::normalize(math::quat_cast(math::mat3(side, up_axis, forward)));
    plan.end = plan.start + forward * along.y;
}

void fit_shapes(std::vector<body_plan>& plans, const skeleton_view& view)
{
    const character_frame frame{view.side, up_axis, view.forward};
    for(size_t i = 0; i < plans.size(); ++i)
    {
        auto& plan = plans[i];
        plan.start = view.positions[size_t(plan.bone)];
        switch(plan.kind)
        {
            case body_kind::torso:
            case body_kind::neck:
            {
                const int child = find_child_plan(plans, int(i));
                plan.end = child >= 0 ? view.positions[size_t(plans[size_t(child)].bone)]
                                      : plan.start + up_axis * (view.height * 0.1f);
                if(plan.kind == body_kind::torso)
                {
                    fit_torso(plan, view, frame);
                }
                else
                {
                    fit_neck(plan, view);
                }
                break;
            }
            case body_kind::head:
                fit_head(plan, view, frame);
                break;
            case body_kind::upper_arm:
            case body_kind::lower_arm:
            case body_kind::upper_leg:
            case body_kind::lower_leg:
            {
                const humanoid_bone next_role = humanoid_bone(size_t(plan.role) + 1);
                const int next = role_bone(view, next_role);
                plan.end = next >= 0 ? view.positions[size_t(next)] : plan.start - up_axis * (view.height * 0.2f);
                fit_limb(plan, view);
                break;
            }
            case body_kind::hand:
            {
                const humanoid_bone forearm = humanoid_bone(size_t(plan.role) - 1);
                const int forearm_bone = role_bone(view, forearm);
                fit_hand(plan, view, forearm_bone >= 0 ? view.positions[size_t(forearm_bone)] : plan.start + up_axis);
                break;
            }
            case body_kind::foot:
            {
                const humanoid_bone toes = humanoid_bone(size_t(plan.role) + 1);
                const int toes_bone = role_bone(view, toes);
                const math::vec3 hint = toes_bone >= 0 ? view.positions[size_t(toes_bone)] : plan.start + frame.forward;
                fit_foot(plan, view, frame, hint);
                break;
            }
        }
    }
}

/// Flips @p axis so that turning @p direction about it by a positive angle moves it towards @p wanted.
auto orient_axis(const math::vec3& axis, const math::vec3& direction, const math::vec3& wanted) -> math::vec3
{
    const math::vec3 turned = math::angleAxis(math::radians(test_turn_degrees), axis) * direction;
    return math::dot(turned - direction, wanted) >= 0.0f ? axis : -axis;
}

auto get_segment_direction(const body_plan& plan) -> math::vec3
{
    const math::vec3 axis = plan.end - plan.start;
    return math::length(axis) > 0.0f ? math::normalize(axis) : up_axis;
}

void set_ball(body_plan& plan, float swing, float twist_lower, float twist_upper, float friction)
{
    plan.joint = ragdoll_joint_type::ball;
    plan.swing = swing;
    plan.twist_lower = twist_lower;
    plan.twist_upper = twist_upper;
    plan.friction_scale = friction;
}

void set_hinge(body_plan& plan, float lower, float upper, float friction)
{
    plan.joint = ragdoll_joint_type::hinge;
    plan.twist_lower = lower;
    plan.twist_upper = upper;
    plan.friction_scale = friction;
}

/// Human ranges of motion, in degrees, with the twist axis and its sign chosen so positive angles bend the way
/// the joint bends.
void plan_joints(std::vector<body_plan>& plans, const skeleton_view& view)
{
    const math::vec3 forward = view.forward;
    for(auto& plan : plans)
    {
        const math::vec3 direction = get_segment_direction(plan);
        switch(plan.kind)
        {
            case body_kind::torso:
                plan.joint_frame = make_frame(direction, forward);
                set_ball(plan, 22.0f, -18.0f, 18.0f, 2.0f);
                break;
            case body_kind::neck:
                plan.joint_frame = make_frame(direction, forward);
                set_ball(plan, 35.0f, -35.0f, 35.0f, 1.0f);
                break;
            case body_kind::head:
            {
                const bool has_neck_body = plan.parent >= 0 && plans[size_t(plan.parent)].kind == body_kind::neck;
                plan.joint_frame = make_frame(up_axis, forward);
                set_ball(plan, has_neck_body ? 30.0f : 50.0f, has_neck_body ? -30.0f : -60.0f, has_neck_body ? 30.0f : 60.0f,
                         0.8f);
                break;
            }
            case body_kind::upper_arm:
                plan.joint_frame = make_frame(direction, forward);
                set_ball(plan, 80.0f, -60.0f, 60.0f, 0.8f);
                break;
            case body_kind::lower_arm:
            {
                // The elbow bends the forearm up towards the front of the body.
                const math::vec3 hinge = math::normalize(math::cross(direction, forward));
                const math::vec3 axis = orient_axis(hinge, direction, forward + up_axis * 0.2f);
                plan.joint_frame = make_frame(axis, direction);
                set_hinge(plan, -3.0f, 140.0f, 0.6f);
                break;
            }
            case body_kind::hand:
                plan.joint_frame = make_frame(direction, forward);
                set_ball(plan, 50.0f, -60.0f, 60.0f, 0.3f);
                break;
            case body_kind::upper_leg:
            {
                // The hip flexes through its twist, about the side axis: far forward, a little back. The cone holds
                // the leg's sideways swing and its turn about itself.
                const math::vec3 axis = orient_axis(view.side, direction, forward);
                plan.joint_frame = make_frame(axis, direction);
                set_ball(plan, 30.0f, -20.0f, 110.0f, 1.5f);
                break;
            }
            case body_kind::lower_leg:
            {
                // The knee bends the shin back.
                const math::vec3 axis = orient_axis(view.side, direction, -forward);
                plan.joint_frame = make_frame(axis, direction);
                set_hinge(plan, -3.0f, 140.0f, 1.0f);
                break;
            }
            case body_kind::foot:
            {
                const math::vec3 shin = plan.parent >= 0 ? get_segment_direction(plans[size_t(plan.parent)]) : -up_axis;
                const math::vec3 axis = orient_axis(view.side, shin, forward);
                plan.joint_frame = make_frame(axis, shin);
                set_ball(plan, 18.0f, -40.0f, 25.0f, 0.5f);
                break;
            }
        }
    }
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
    if(a <= 1e-8f && e <= 1e-8f)
    {
        return math::distance(p1, p2);
    }
    if(a <= 1e-8f)
    {
        t = math::clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const float c = math::dot(d1, r);
        if(e <= 1e-8f)
        {
            s = math::clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const float b = math::dot(d1, d2);
            const float denominator = a * e - b * b;
            s = denominator > 1e-8f ? math::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
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

auto get_uniform_scale(const math::transform& transform) -> float
{
    const math::vec3 scale = math::abs(transform.get_scale());
    const float value = (scale.x + scale.y + scale.z) / 3.0f;
    return value > 0.0f ? value : 1.0f;
}

auto to_degrees(const math::quat& rotation) -> math::vec3
{
    return math::degrees(math::eulerAngles(math::normalize(rotation)));
}

auto make_body(const body_plan& plan, const skeleton_view& view, float total_mass) -> ragdoll_body
{
    const auto& bone = view.bind[size_t(plan.bone)];
    const float scale = get_uniform_scale(bone);
    const math::quat inverse_bone = glm::inverse(math::normalize(bone.get_rotation()));
    const math::vec3 center = bone.inverse_transform_coord(plan.center);
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
    body.mass = total_mass * plan.mass_share;
    body.joint = plan.joint;
    body.joint_rotation = to_degrees(inverse_bone * plan.joint_frame);
    body.swing_limit = plan.swing;
    body.twist_lower = plan.twist_lower;
    body.twist_upper = plan.twist_upper;
    body.friction_scale = plan.friction_scale;
    return body;
}
} // namespace ragdoll_builder_detail

auto build_humanoid_ragdoll(const model_component& model, entt::handle model_entity, const ragdoll_build_options& options)
    -> ragdoll_build_result
{
    namespace detail = ragdoll_builder_detail;
    ragdoll_build_result result;
    detail::skeleton_view view;
    if(!detail::read_skeleton(model, model_entity, view))
    {
        result.error = model.get_armature_entities().empty()
                           ? "The model has no armature yet. Wait for its mesh to load."
                           : "No humanoid skeleton was found: the hips (two legs below a spine) are missing.";
        return result;
    }
    auto plans = detail::make_plans(view, options);
    detail::link_parents(plans, view);
    if(options.fit_to_skin)
    {
        detail::collect_vertices(plans, view, model);
    }
    detail::fit_shapes(plans, view);
    detail::plan_joints(plans, view);

    float share_sum = 0.0f;
    for(const auto& plan : plans)
    {
        share_sum += plan.mass_share;
    }
    const float mass_scale = share_sum > 0.0f ? options.total_mass / share_sum : 0.0f;
    for(const auto& plan : plans)
    {
        result.bodies.push_back(detail::make_body(plan, view, mass_scale));
        result.skin_fitted_bodies += plan.is_fitted ? 1 : 0;
    }
    result.disabled_collisions = detail::find_disabled_pairs(plans, view);
    result.ok = !result.bodies.empty();
    if(!result.ok)
    {
        result.error = "The humanoid skeleton has no bones for bodies.";
    }
    return result;
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
