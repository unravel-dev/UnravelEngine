#include "animation_humanoid.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace unravel
{
namespace
{
/// The hips stand above the floor by more than this fraction of the character's height.
constexpr float MIN_HIPS_HEIGHT = 0.2f;
/// A leg reaches below this fraction of its hips' height above the floor.
constexpr float LEG_REACH = 0.4f;
/// A spine rises above the hips by at least this fraction of their height.
constexpr float SPINE_RISE = 0.2f;
/// Arms reach sideways from the spine by at least this fraction of the hips' height.
constexpr float ARM_REACH = 0.45f;
/// A first arm joint nearer the spine than this fraction of the next joint's distance is a shoulder.
constexpr float SHOULDER_SPAN = 0.4f;
/// So is a first arm bone shorter than this fraction of the bone after it.
constexpr float SHOULDER_LENGTH = 0.7f;
/// Joints a limb or the spine is followed for at most.
constexpr size_t MAX_CHAIN = 32;
/// Two legs reach equally low when their lowest points differ by less than this fraction of the
/// hips' height.
constexpr float LEG_REACH_TIE = 0.02f;
/// Below this length the toes give no direction.
constexpr float MIN_TOE_DIRECTION = 1e-4f;

constexpr const char* BONE_NAMES[HUMANOID_BONE_COUNT] = {
    "Hips",           "Spine",           "Chest",           "Upper Chest",    "Neck",
    "Head",           "Left Shoulder",   "Left Upper Arm",  "Left Lower Arm", "Left Hand",
    "Right Shoulder", "Right Upper Arm", "Right Lower Arm", "Right Hand",     "Left Upper Leg",
    "Left Lower Leg", "Left Foot",       "Left Toes",       "Right Upper Leg", "Right Lower Leg",
    "Right Foot",     "Right Toes",
};

/// Per-bone facts about the subtree below each bone.
struct skeleton_tree
{
    const std::vector<std::string>& names;
    const std::vector<int>& parents;
    const std::vector<math::vec3>& positions;
    std::vector<std::vector<int>> children;
    /// Lowest and highest rest height in the subtree.
    std::vector<float> lowest;
    std::vector<float> highest;
    /// Bones in the subtree, and joints on its longest chain (the bone itself included).
    std::vector<int> size;
    std::vector<int> depth;
    float floor{};
};

auto build_tree(const std::vector<std::string>& names,
                const std::vector<int>& parents,
                const std::vector<math::vec3>& positions,
                size_t count) -> skeleton_tree
{
    skeleton_tree tree{names, parents, positions};
    tree.children.resize(count);
    tree.lowest.resize(count);
    tree.highest.resize(count);
    tree.size.assign(count, 1);
    tree.depth.assign(count, 1);
    tree.floor = std::numeric_limits<float>::max();
    for(size_t i = 0; i < count; ++i)
    {
        tree.lowest[i] = tree.highest[i] = positions[i].y;
        tree.floor = std::min(tree.floor, positions[i].y);
        if(parents[i] >= 0 && size_t(parents[i]) < i)
        {
            tree.children[size_t(parents[i])].push_back(int(i));
        }
    }
    // Parents come first, so walking backwards finishes every subtree before its parent.
    for(size_t i = count; i-- > 0;)
    {
        for(int child : tree.children[i])
        {
            tree.lowest[i] = std::min(tree.lowest[i], tree.lowest[size_t(child)]);
            tree.highest[i] = std::max(tree.highest[i], tree.highest[size_t(child)]);
            tree.size[i] += tree.size[size_t(child)];
            tree.depth[i] = std::max(tree.depth[i], tree.depth[size_t(child)] + 1);
        }
    }
    return tree;
}

auto get_horizontal_distance(const math::vec3& a, const math::vec3& b) -> float
{
    return std::hypot(a.x - b.x, a.z - b.z);
}

auto to_lower(const std::string& name) -> std::string
{
    std::string lower(name);
    std::transform(lower.begin(),
                   lower.end(),
                   lower.begin(),
                   [](unsigned char c)
                   {
                       return char(std::tolower(c));
                   });
    return lower;
}

/// `name` continues the bone `base` as a further segment (Rigify's `DEF-forearm.L.001`).
auto is_segment_of(const std::string& name, const std::string& base) -> bool
{
    if(base.empty() || name.size() < base.size() + 2 || name.compare(0, base.size(), base) != 0)
    {
        return false;
    }
    const char separator = name[base.size()];
    return (separator == '.' || separator == '_') &&
           std::all_of(name.begin() + std::ptrdiff_t(base.size()) + 1,
                       name.end(),
                       [](unsigned char c)
                       {
                           return std::isdigit(c) != 0;
                       });
}

/// Widest horizontal distance of the subtree from the vertical axis through `axis`.
auto get_reach(const skeleton_tree& tree, int bone, const math::vec3& axis) -> float
{
    float reach = get_horizontal_distance(tree.positions[size_t(bone)], axis);
    for(int child : tree.children[size_t(bone)])
    {
        reach = std::max(reach, get_reach(tree, child, axis));
    }
    return reach;
}

/// +1 left, -1 right, 0 when the name does not say (`upperarm_l`, `hand.R`, `LeftFoot`, `Bip L Thigh`).
auto get_side_from_name(const std::string& name) -> int
{
    const std::string lower = to_lower(name);
    if(lower.find("left") != std::string::npos)
    {
        return 1;
    }
    if(lower.find("right") != std::string::npos)
    {
        return -1;
    }
    std::string token;
    for(size_t i = 0; i <= lower.size(); ++i)
    {
        if(i < lower.size() && std::isalnum(static_cast<unsigned char>(lower[i])))
        {
            token += lower[i];
            continue;
        }
        if(token == "l")
        {
            return 1;
        }
        if(token == "r")
        {
            return -1;
        }
        token.clear();
    }
    return 0;
}

/// The side a limb hangs on: its names, else the side of `center` it starts on along `left`.
auto get_side(const skeleton_tree& tree, int limb, const math::vec3& center, const math::vec3& left) -> int
{
    for(int bone = limb, steps = 0; bone >= 0 && steps < 3; ++steps)
    {
        if(const int side = get_side_from_name(tree.names[size_t(bone)]); side != 0)
        {
            return side;
        }
        const auto& children = tree.children[size_t(bone)];
        bone = children.empty() ? -1 : children.front();
    }
    return math::dot(tree.positions[size_t(limb)] - center, left) >= 0.0f ? 1 : -1;
}

/// A twist joint splits a limb bone to spread its roll along it (Daz's `lShldrTwist`): the limb
/// does not bend there.
auto is_twist_bone(const std::string& name) -> bool
{
    return to_lower(name).find("twist") != std::string::npos;
}

/// The child that carries a limb on: the longest chain, then the most bones (not twist or helper bones).
auto find_main_child(const skeleton_tree& tree, int bone) -> int
{
    int best = -1;
    for(int child : tree.children[size_t(bone)])
    {
        if(best < 0 || tree.depth[size_t(child)] > tree.depth[size_t(best)] ||
           (tree.depth[size_t(child)] == tree.depth[size_t(best)] && tree.size[size_t(child)] > tree.size[size_t(best)]))
        {
            best = child;
        }
    }
    return best;
}

/// The joints of a limb from `start`, one per bone: further segments of a bone split for twist
/// (`upper_arm.L.001`) and twist joints inside it are passed over.
auto follow_chain(const skeleton_tree& tree, int start, size_t length) -> std::vector<int>
{
    std::vector<int> chain;
    for(int bone = start; bone >= 0 && chain.size() < length;)
    {
        const int next = find_main_child(tree, bone);
        const std::string& name = tree.names[size_t(bone)];
        const bool is_joint = chain.empty() || (!is_segment_of(name, tree.names[size_t(chain.back())]) &&
                                                !(is_twist_bone(name) && next >= 0));
        if(is_joint)
        {
            chain.push_back(bone);
        }
        bone = next;
    }
    return chain;
}

/// The best left and the best right of `candidates`, where `is_better(a, b)` prefers a to b; -1
/// where a side has none.
template<typename Better>
auto pick_pair(const skeleton_tree& tree,
               const std::vector<int>& candidates,
               const math::vec3& center,
               const math::vec3& left_axis,
               Better is_better) -> std::pair<int, int>
{
    int left = -1;
    int right = -1;
    for(int candidate : candidates)
    {
        int& slot = get_side(tree, candidate, center, left_axis) > 0 ? left : right;
        if(slot < 0 || is_better(candidate, slot))
        {
            slot = candidate;
        }
    }
    return {left, right};
}

/// The character's left in the rest frame, for limbs whose names do not say: toes point forward
/// from the feet, and left is up x forward. +X (a character facing +Z) when no leg ends in toes.
auto find_left_axis(const skeleton_tree& tree, const std::vector<int>& legs) -> math::vec3
{
    constexpr size_t LEG_JOINTS = 4;
    math::vec3 forward(0.0f);
    for(int leg : legs)
    {
        const auto chain = follow_chain(tree, leg, LEG_JOINTS);
        if(chain.size() < LEG_JOINTS)
        {
            continue;
        }
        math::vec3 toes = tree.positions[size_t(chain[3])] - tree.positions[size_t(chain[2])];
        toes.y = 0.0f;
        if(math::length(toes) > MIN_TOE_DIRECTION)
        {
            forward += math::normalize(toes);
        }
    }
    if(math::length(forward) <= MIN_TOE_DIRECTION)
    {
        return {1.0f, 0.0f, 0.0f};
    }
    forward = math::normalize(forward);
    return {forward.z, 0.0f, -forward.x};
}

/// Two legs below a bone: children reaching the floor on either side. Legs reach lowest: a tail or
/// a skirt strand stops above the floor however many bones it has.
auto pick_legs(const skeleton_tree& tree, const std::vector<int>& down, const math::vec3& center, float hips_height)
    -> std::pair<int, int>
{
    const float tie = LEG_REACH_TIE * hips_height;
    return pick_pair(tree,
                     down,
                     center,
                     find_left_axis(tree, down),
                     [&](int candidate, int current)
                     {
                         const float lowest = tree.lowest[size_t(candidate)];
                         const float current_lowest = tree.lowest[size_t(current)];
                         if(std::fabs(lowest - current_lowest) > tie)
                         {
                             return lowest < current_lowest;
                         }
                         return tree.size[size_t(candidate)] > tree.size[size_t(current)];
                     });
}

/// The two legs below the hips: children reaching the floor on either side, or the children of
/// one centered pelvis bone they hang from (Character Creator's Hip -> Pelvis -> thighs).
auto find_legs(const skeleton_tree& tree, int hips) -> std::pair<int, int>
{
    const math::vec3& position = tree.positions[size_t(hips)];
    const float hips_height = position.y - tree.floor;
    const float floor_reach = tree.floor + LEG_REACH * hips_height;
    const auto reaching_down = [&](int bone)
    {
        std::vector<int> down;
        for(int child : tree.children[size_t(bone)])
        {
            if(tree.lowest[size_t(child)] < floor_reach)
            {
                down.push_back(child);
            }
        }
        return down;
    };
    const auto down = reaching_down(hips);
    auto legs = pick_legs(tree, down, position, hips_height);
    if(legs.first >= 0 && legs.second >= 0)
    {
        return legs;
    }
    for(int pelvis : down)
    {
        legs = pick_legs(tree, reaching_down(pelvis), position, hips_height);
        if(legs.first >= 0 && legs.second >= 0)
        {
            return legs;
        }
    }
    return {-1, -1};
}

auto find_spine_start(const skeleton_tree& tree, int hips) -> int
{
    const math::vec3& position = tree.positions[size_t(hips)];
    const float rise = SPINE_RISE * (position.y - tree.floor);
    int best = -1;
    for(int child : tree.children[size_t(hips)])
    {
        if(tree.highest[size_t(child)] > position.y + rise &&
           (best < 0 || tree.highest[size_t(child)] > tree.highest[size_t(best)]))
        {
            best = child;
        }
    }
    return best;
}

/// The hips: two legs hang down from them, on either side, and a spine rises. Hips the legs hang
/// from directly win over a bone above them that holds the legs through a pelvis (a root above a
/// Biped's triangle pelvis); then the largest such subtree, so a knee or a helper bone never does.
auto find_hips(const skeleton_tree& tree, size_t count) -> int
{
    int best = -1;
    bool best_holds_legs = false;
    for(size_t i = 0; i < count; ++i)
    {
        // The hips stand well above the floor: a root bone near the feet, whose only child holds
        // the legs, is not them.
        const float height = tree.positions[i].y - tree.floor;
        if(height <= MIN_HIPS_HEIGHT * (tree.highest[i] - tree.floor))
        {
            continue;
        }
        const auto legs = find_legs(tree, int(i));
        if(legs.first < 0 || legs.second < 0 || find_spine_start(tree, int(i)) < 0)
        {
            continue;
        }
        const bool holds_legs = tree.parents[size_t(legs.first)] == int(i);
        const bool is_better = best < 0 || (holds_legs && !best_holds_legs) ||
                               (holds_legs == best_holds_legs && tree.size[i] > tree.size[size_t(best)]);
        if(is_better)
        {
            best = int(i);
            best_holds_legs = holds_legs;
        }
    }
    return best;
}

/// Children of `bone` reaching sideways like arms, as the best left and right one.
auto find_arms(const skeleton_tree& tree, int bone, const math::vec3& axis, const math::vec3& left_axis, float hips_height)
    -> std::pair<int, int>
{
    std::vector<int> sideways;
    for(int child : tree.children[size_t(bone)])
    {
        if(get_reach(tree, child, axis) > ARM_REACH * hips_height)
        {
            sideways.push_back(child);
        }
    }
    return pick_pair(tree,
                     sideways,
                     tree.positions[size_t(bone)],
                     left_axis,
                     [&](int candidate, int current)
                     {
                         return get_reach(tree, candidate, axis) > get_reach(tree, current, axis);
                     });
}

void set(humanoid_map& map, humanoid_bone role, int bone)
{
    map[size_t(role)] = bone;
}

void assign_leg(const skeleton_tree& tree, int start, humanoid_bone upper, humanoid_map& map)
{
    const auto chain = follow_chain(tree, start, 4);
    for(size_t i = 0; i < chain.size(); ++i)
    {
        set(map, humanoid_bone(size_t(upper) + i), chain[i]);
    }
}

/// An arm from its first bone: shoulder (when it starts near the spine or is short), upper arm,
/// lower arm, hand. Spans are measured along the body's left-right axis only: a collarbone may start
/// well in front of the spine.
void assign_arm(const skeleton_tree& tree,
                int start,
                const math::vec3& spine,
                const math::vec3& lateral,
                humanoid_bone shoulder,
                humanoid_map& map)
{
    const auto chain = follow_chain(tree, start, 5);
    size_t first = 0;
    if(chain.size() >= 4)
    {
        const auto& p = tree.positions;
        const float span0 = std::fabs(math::dot(p[size_t(chain[0])] - spine, lateral));
        const float span1 = std::fabs(math::dot(p[size_t(chain[1])] - spine, lateral));
        const float length0 = math::length(p[size_t(chain[1])] - p[size_t(chain[0])]);
        const float length1 = math::length(p[size_t(chain[2])] - p[size_t(chain[1])]);
        if(span0 < SHOULDER_SPAN * span1 || length0 < SHOULDER_LENGTH * length1)
        {
            set(map, shoulder, chain[0]);
            first = 1;
        }
    }
    for(size_t i = 0; i < 3 && first + i < chain.size(); ++i)
    {
        set(map, humanoid_bone(size_t(shoulder) + 1 + i), chain[first + i]);
    }
}

/// An end marker exporters put at the tip of a chain (Mixamo's HeadTop_End, 3ds Max's HeadNub).
auto is_end_bone(const std::string& name) -> bool
{
    const std::string lower = to_lower(name);
    return lower.find("end") != std::string::npos || lower.find("top") != std::string::npos ||
           lower.find("nub") != std::string::npos;
}

/// Neck and head above the upper chest: single children up to the head, which ends the chain or
/// branches into face bones.
void assign_head(const skeleton_tree& tree, int upper_chest, std::pair<int, int> arms, humanoid_map& map)
{
    const float chest_height = tree.positions[size_t(upper_chest)].y;
    int start = -1;
    for(int child : tree.children[size_t(upper_chest)])
    {
        if(child != arms.first && child != arms.second && tree.highest[size_t(child)] > chest_height &&
           (start < 0 || tree.highest[size_t(child)] > tree.highest[size_t(start)]))
        {
            start = child;
        }
    }
    if(start < 0)
    {
        return;
    }
    std::vector<int> chain{start};
    while(chain.size() < MAX_CHAIN && tree.children[size_t(chain.back())].size() == 1)
    {
        const int next = tree.children[size_t(chain.back())].front();
        // A helmet or other attachment hangs from the head; the head chain only climbs.
        const bool climbs = tree.positions[size_t(next)].y > tree.positions[size_t(chain.back())].y;
        if(!climbs || (tree.children[size_t(next)].empty() && is_end_bone(tree.names[size_t(next)])))
        {
            break;
        }
        chain.push_back(next);
    }
    set(map, humanoid_bone::head, chain.back());
    if(chain.size() >= 2)
    {
        set(map, humanoid_bone::neck, chain.front());
    }
}
} // namespace

auto find_humanoid_bones(const std::vector<std::string>& names,
                         const std::vector<int>& parents,
                         const std::vector<math::vec3>& rest_positions) -> humanoid_map
{
    humanoid_map map;
    map.fill(-1);
    const size_t count = std::min({names.size(), parents.size(), rest_positions.size()});
    if(count == 0)
    {
        return map;
    }
    const auto tree = build_tree(names, parents, rest_positions, count);
    const int hips = find_hips(tree, count);
    if(hips < 0)
    {
        return map;
    }
    set(map, humanoid_bone::hips, hips);
    const math::vec3& hips_position = rest_positions[size_t(hips)];
    const float hips_height = hips_position.y - tree.floor;

    const auto legs = find_legs(tree, hips);
    assign_leg(tree, legs.first, humanoid_bone::left_upper_leg, map);
    assign_leg(tree, legs.second, humanoid_bone::right_upper_leg, map);
    // Left to right, from the thighs: the body's own lateral axis whatever way it faces.
    math::vec3 lateral = rest_positions[size_t(legs.first)] - rest_positions[size_t(legs.second)];
    lateral.y = 0.0f;
    lateral = math::length(lateral) > 0.0f ? math::normalize(lateral) : math::vec3(1.0f, 0.0f, 0.0f);

    // Up the spine to the bone the arms branch from.
    std::vector<int> spine{find_spine_start(tree, hips)};
    auto arms = find_arms(tree, spine.back(), hips_position, lateral, hips_height);
    while((arms.first < 0 || arms.second < 0) && spine.size() < MAX_CHAIN)
    {
        int next = -1;
        for(int child : tree.children[size_t(spine.back())])
        {
            if(next < 0 || tree.highest[size_t(child)] > tree.highest[size_t(next)])
            {
                next = child;
            }
        }
        if(next < 0)
        {
            break;
        }
        spine.push_back(next);
        arms = find_arms(tree, next, hips_position, lateral, hips_height);
    }
    // 3ds Max's Biped hangs the arms from the neck: a branch bone named so is the neck, and the
    // spine bone below it the upper chest.
    int head_base = spine.back();
    if(spine.size() >= 2 && to_lower(names[size_t(spine.back())]).find("neck") != std::string::npos)
    {
        set(map, humanoid_bone::neck, spine.back());
        spine.pop_back();
    }
    const int upper_chest = spine.back();
    set(map, humanoid_bone::upper_chest, upper_chest);
    if(spine.size() >= 2)
    {
        set(map, humanoid_bone::spine, spine.front());
    }
    if(spine.size() >= 3)
    {
        set(map, humanoid_bone::chest, spine[spine.size() - 2]);
    }
    if(arms.first >= 0 && arms.second >= 0)
    {
        const math::vec3& axis = rest_positions[size_t(upper_chest)];
        assign_arm(tree, arms.first, axis, lateral, humanoid_bone::left_shoulder, map);
        assign_arm(tree, arms.second, axis, lateral, humanoid_bone::right_shoulder, map);
    }
    assign_head(tree, head_base, arms, map);
    return map;
}

auto get_humanoid_bone_name(humanoid_bone bone) -> const char*
{
    return bone < humanoid_bone::count ? BONE_NAMES[size_t(bone)] : "";
}

auto count_humanoid_bones(const humanoid_map& map) -> size_t
{
    return size_t(std::count_if(map.begin(),
                                map.end(),
                                [](int bone)
                                {
                                    return bone >= 0;
                                }));
}

} // namespace unravel
