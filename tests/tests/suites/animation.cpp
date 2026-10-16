/*
 * Validation suite for the animation player, pose blending and blend spaces.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite animation
 *
 * These pin the correctness fixes from the 2026-08 animation review:
 * blend-space blending (the in-place alias bug produced the last clip's raw
 * pose), blend-space time advance, root-motion loop/wrap accounting, the
 * stop()/replay teleport, weight blending of root motion results, the
 * blend space parameter edge cases.
 *
 * And the 2026-10 feature pass: freeform 2D blend spaces (cartesian + directional gradient bands),
 * normalized phase sync inside blend spaces, layer weights / masks / override and additive modes,
 * layer fade in and out, clip events (crossing windows, weights, blend-space and crossfade rules),
 * curves blended with the pose, and clips bound onto a target skeleton.
 *
 * The IK solvers have their own suite - see ik.cpp.
 */

#include "../tests.h"

#include <engine/animation/animation_blend_space.h>
#include <engine/animation/animation_controller_instance.h>
#include <engine/animation/animation_humanoid.h>
#include <engine/meta/animation/animation.hpp>
#include <engine/meta/animation/animation_controller.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <engine/animation/animation_player.h>
#include <engine/threading/threader.h>
#include <uuid/uuid.h>

#include <logging/logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace unravel;

namespace
{

using seconds_t = animation_clip::seconds_t;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void check_near(float actual, float expected, float tolerance, const std::string& what)
{
    ++g_checks;
    if(!(std::fabs(actual - expected) <= tolerance))
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %.5f, expected %.5f +/- %.5f)\n", what.c_str(), actual, expected, tolerance);
    }
}

// The runner's threader owns the pool (and the tpp lifecycle around it); the
// suite entry point borrows it for the duration of the run.
tpp::thread_pool* g_pool = nullptr;

auto pool() -> tpp::thread_pool&
{
    return *g_pool;
}

auto make_clip_handle(const std::shared_ptr<animation_clip>& clip) -> asset_handle<animation_clip>
{
    asset_handle<animation_clip> handle;
    // Distinct uids matter: asset_handle equality compares uid/id, and
    // blend_to uses it to detect "already playing this clip".
    handle.set_internal_ids(generate_uuid());
    handle.set_internal_job(pool().schedule("test_clip", [clip]() { return clip; }).share());
    handle.get(); // block until the job resolves so tests are deterministic
    return handle;
}

// A single-node clip whose node moves linearly from x = start_x to x = end_x
// over `duration` seconds.
auto make_linear_clip(float start_x, float end_x, float duration, bool root_motion = false)
    -> std::shared_ptr<animation_clip>
{
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{duration};
    auto& channel = clip->channels.emplace_back();
    channel.node_name = "root";
    channel.node_index = 0;
    channel.position_keys = {{seconds_t{0.0f}, {start_x, 0.0f, 0.0f}}, {seconds_t{duration}, {end_x, 0.0f, 0.0f}}};
    channel.rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()},
                             {seconds_t{duration}, math::identity<math::quat>()}};
    channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    if(root_motion)
    {
        clip->root_motion.position_node_index = 0;
        clip->root_motion.position_node_name = "root";
    }
    return clip;
}

auto make_constant_clip(float x, float duration) -> std::shared_ptr<animation_clip>
{
    return make_linear_clip(x, x, duration);
}

auto make_pose_node(animation_pose& pose, size_t index, float x) -> animation_pose::node&
{
    auto& node = pose.nodes.emplace_back();
    node.desc.index = index;
    node.transform.set_position({x, 0.0f, 0.0f});
    node.transform.set_rotation(math::identity<math::quat>());
    node.transform.set_scale({1.0f, 1.0f, 1.0f});
    return node;
}

struct sampled_frame
{
    std::map<size_t, math::transform> nodes;
    animation_pose::root_motion_result motion;
    bool updated{};
};

/// Steps the player and records every bone it writes; `ref_pose` holds the bones' rest transforms.
auto step_player_on(animation_player& player,
                    float dt,
                    const animation_pose& ref_pose,
                    bool force = false,
                    bool extract_root_motion = true) -> sampled_frame
{
    sampled_frame frame;
    frame.updated = player.update_time(seconds_t{dt}, force);
    if(frame.updated)
    {
        player.update_poses(ref_pose,
                            animation_retargeting_mode::index_based,
                            extract_root_motion,
                            [&](const animation_pose::node_desc& desc,
                                const math::transform& transform,
                                const animation_pose::root_motion_result& motion_result)
                            {
                                frame.nodes[desc.index] = transform;
                                frame.motion = motion_result;
                            });
    }
    return frame;
}

auto step_player(animation_player& player, float dt, bool force = false, bool extract_root_motion = true)
    -> sampled_frame
{
    static const animation_pose empty_ref_pose{};
    return step_player_on(player, dt, empty_ref_pose, force, extract_root_motion);
}

/// Adds bone `node_index` to the clip, held at x = `x` for the whole clip.
void add_constant_bone(animation_clip& clip, size_t node_index, float x)
{
    auto& channel = clip.channels.emplace_back(clip.channels.front());
    channel.node_name = "bone_" + std::to_string(node_index);
    channel.node_index = node_index;
    channel.position_keys = {{seconds_t{0.0f}, {x, 0.0f, 0.0f}}};
}

/// Bone 0 rests at x = 0 and bone 1 at x = 4.
auto make_two_bone_rest_pose() -> animation_pose
{
    animation_pose rest_pose;
    make_pose_node(rest_pose, 0, 0.0f);
    make_pose_node(rest_pose, 1, 4.0f);
    return rest_pose;
}

void test_pairwise_blend_merges_sorted_nodes()
{
    std::printf("test_pairwise_blend_merges_sorted_nodes\n");
    animation_pose pose_a;
    make_pose_node(pose_a, 0, 0.0f);
    make_pose_node(pose_a, 1, 2.0f);
    animation_pose pose_b;
    make_pose_node(pose_b, 0, 10.0f);
    make_pose_node(pose_b, 2, 4.0f);

    animation_pose result;
    blend_poses(pose_a, pose_b, 0.5f, result);

    check(result.nodes.size() == 3, "union of both poses");
    check_near(result.nodes[0].transform.get_position().x, 5.0f, 1e-4f, "shared node blended");
    check_near(result.nodes[1].transform.get_position().x, 2.0f, 1e-4f, "pose_a-only node copied");
    check_near(result.nodes[2].transform.get_position().x, 4.0f, 1e-4f, "pose_b-only node copied");
}

void test_multiway_blend_weighted_average()
{
    std::printf("test_multiway_blend_weighted_average\n");
    std::vector<animation_pose> poses(2);
    make_pose_node(poses[0], 0, 0.0f);
    make_pose_node(poses[1], 0, 10.0f);
    const std::vector<float> weights{1.0f, 3.0f};

    animation_pose result;
    blend_poses(poses, weights, result);

    check(result.nodes.size() == 1, "single shared node");
    check_near(result.nodes[0].transform.get_position().x, 7.5f, 1e-4f, "weighted average position");
    check(result.nodes[0].desc.index == 0, "node index carried through multiway blend");
}

void test_blends_cover_a_bone_some_poses_lack()
{
    std::printf("test_blends_cover_a_bone_some_poses_lack\n");
    // A bone a pose does not animate leaves its share to what lies below: rest for the base layer.
    const animation_pose rest_pose = make_two_bone_rest_pose();
    animation_pose pose_a;
    make_pose_node(pose_a, 0, 0.0f);
    make_pose_node(pose_a, 1, 10.0f);
    animation_pose pose_b;
    make_pose_node(pose_b, 0, 20.0f);
    animation_pose result;
    blend_poses(pose_a, pose_b, 0.25f, result);
    check(result.nodes.size() == 2, "union of both poses");
    check_near(result.nodes[1].coverage, 0.75f, 1e-5f, "a bone only pose_a animates is covered by its share");
    check_near(result.nodes[1].transform.get_position().x, 10.0f, 1e-4f, "and keeps its value");
    check_near(resolve_coverage(result.nodes[1], rest_pose.find_node(1)).get_position().x, 8.5f, 1e-4f,
               "resolved against rest: a quarter of the way to rest");
    blend_poses(pose_b, pose_a, 0.25f, result);
    check_near(resolve_coverage(result.nodes[1], rest_pose.find_node(1)).get_position().x, 5.5f, 1e-4f,
               "a bone only pose_b animates is a quarter of the way from rest");
    check_near(resolve_coverage(result.nodes[1], nullptr).get_position().x, 10.0f, 1e-4f,
               "with nothing below, the animating pose decides");

    // Three blend points, one of which animates bone 1: half of the weight covers it.
    std::vector<animation_pose> poses(3);
    make_pose_node(poses[0], 0, 0.0f);
    make_pose_node(poses[1], 0, 0.0f);
    make_pose_node(poses[1], 1, 10.0f);
    make_pose_node(poses[2], 0, 0.0f);
    blend_poses(poses, {1.0f, 2.0f, 1.0f}, result);
    check(result.nodes.size() == 2 && result.nodes[1].desc.index == 1, "the bone is in the blend");
    check_near(result.nodes[1].coverage, 0.5f, 1e-5f, "half of the weight on the point animating it");
    check_near(resolve_coverage(result.nodes[1], rest_pose.find_node(1)).get_position().x, 7.0f, 1e-4f,
               "half way from rest to that point");

    // Additive changes count by their coverage: half covered is half the change.
    animation_pose delta_a;
    make_pose_node(delta_a, 1, 6.0f);
    animation_pose delta_b;
    make_pose_node(delta_b, 0, 2.0f);
    animation_pose deltas;
    blend_poses(delta_a, delta_b, 0.5f, deltas);
    animation_pose base;
    make_pose_node(base, 0, 0.0f);
    make_pose_node(base, 1, 0.0f);
    blend_layer(base, deltas, animation_layer_blend_mode::additive, 1.0f, {}, rest_pose, result);
    check_near(result.nodes[1].transform.get_position().x, 3.0f, 1e-4f, "half of an additive change");
    check_near(result.nodes[1].transform.get_scale().x, 1.5f, 1e-4f, "half of an additive scale change");
}

void test_blend_space_actually_blends()
{
    std::printf("test_blend_space_actually_blends\n");
    // Regression: blending in place aliased the accumulator, so a blend space
    // returned the LAST clip's raw pose instead of the weighted mix.
    auto clip_a = make_constant_clip(0.0f, 1.0f);
    auto clip_b = make_constant_clip(10.0f, 1.0f);

    auto blend_space = std::make_shared<blend_space_def>();
    blend_space->add_clip({0.0f}, make_clip_handle(clip_a));
    blend_space->add_clip({1.0f}, make_clip_handle(clip_b));

    animation_player player;
    player.set_blend_space(0, blend_space);
    player.set_blend_space_parameters(0, {0.5f});
    player.play();

    auto frame = step_player(player, 0.1f);
    check(frame.updated, "player updated");
    check_near(frame.nodes[0].get_position().x, 5.0f, 1e-3f, "50/50 blend of both clips");

    player.set_blend_space_parameters(0, {0.25f});
    frame = step_player(player, 0.1f);
    check_near(frame.nodes[0].get_position().x, 2.5f, 1e-3f, "25/75 blend of both clips");
}

void test_blend_space_time_advances_and_loops()
{
    std::printf("test_blend_space_time_advances_and_loops\n");
    // Regression: blend-space-only states never advanced their elapsed time.
    auto clip_a = make_linear_clip(0.0f, 1.0f, 1.0f);
    auto clip_b = make_linear_clip(0.0f, 1.0f, 1.0f);

    auto blend_space = std::make_shared<blend_space_def>();
    blend_space->add_clip({0.0f}, make_clip_handle(clip_a));
    blend_space->add_clip({1.0f}, make_clip_handle(clip_b));

    animation_player player;
    player.set_blend_space(0, blend_space);
    player.set_blend_space_parameters(0, {0.5f});
    player.play();

    auto frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().x, 0.5f, 1e-3f, "sampled at t=0.5, not frozen at t=0");

    // 0.5 + 0.75 = 1.25 wraps to 0.25 (duration becomes known after the first
    // pose update fills the active clip list).
    frame = step_player(player, 0.75f);
    check_near(frame.nodes[0].get_position().x, 0.25f, 1e-3f, "blend space time wraps at clip duration");
}

void test_blend_space_1d_param_edges()
{
    std::printf("test_blend_space_1d_param_edges\n");
    auto clip_a = make_constant_clip(0.0f, 1.0f);
    auto clip_b = make_constant_clip(10.0f, 1.0f);
    auto handle_a = make_clip_handle(clip_a);
    auto handle_b = make_clip_handle(clip_b);

    blend_space_def blend_space;
    blend_space.add_clip({0.0f}, handle_a);
    blend_space.add_clip({1.0f}, handle_b);

    std::vector<blend_space_weight> out;

    // Regression: a parameter below the range fell through to the LAST
    // interval and picked the wrong clip.
    blend_space.compute_blend({-5.0f}, out);
    float weight_a = 0.0f;
    float weight_sum = 0.0f;
    for(const auto& clip_weight : out)
    {
        weight_sum += clip_weight.weight;
        if(clip_weight.clip.get().get() == clip_a.get())
        {
            weight_a += clip_weight.weight;
        }
    }
    check_near(weight_sum, 1.0f, 1e-4f, "below-range weights sum to 1");
    check_near(weight_a, 1.0f, 1e-4f, "below-range clamps to the first clip");

    // No parameters supplied at all: fall back instead of reading OOB.
    blend_space.compute_blend({}, out);
    check(!out.empty(), "missing parameters fall back to a valid clip");
}

void test_blend_space_2d_edges()
{
    std::printf("test_blend_space_2d_edges\n");
    auto clip_00 = make_constant_clip(0.0f, 1.0f);
    auto clip_10 = make_constant_clip(1.0f, 1.0f);
    auto clip_01 = make_constant_clip(2.0f, 1.0f);
    auto clip_11 = make_constant_clip(3.0f, 1.0f);
    auto handle_00 = make_clip_handle(clip_00);
    auto handle_10 = make_clip_handle(clip_10);
    auto handle_01 = make_clip_handle(clip_01);
    auto handle_11 = make_clip_handle(clip_11);

    const auto weight_of = [](const std::vector<blend_space_weight>& out,
                              const std::shared_ptr<animation_clip>& clip)
    {
        float weight = 0.0f;
        for(const auto& clip_weight : out)
        {
            if(clip_weight.clip.get().get() == clip.get())
            {
                weight += clip_weight.weight;
            }
        }
        return weight;
    };
    const auto check_normalized = [](const std::vector<blend_space_weight>& out,
                                     const std::string& what)
    {
        float weight_sum = 0.0f;
        bool any_negative = false;
        for(const auto& clip_weight : out)
        {
            weight_sum += clip_weight.weight;
            any_negative |= clip_weight.weight < -1e-6f;
        }
        check(!out.empty(), what + ": produces clips");
        check(!any_negative, what + ": no negative weights");
        check_near(weight_sum, 1.0f, 1e-4f, what + ": weights sum to 1");
    };

    blend_space_def grid;
    grid.add_clip({0.0f, 0.0f}, handle_00);
    grid.add_clip({1.0f, 0.0f}, handle_10);
    grid.add_clip({0.0f, 1.0f}, handle_01);
    grid.add_clip({1.0f, 1.0f}, handle_11);

    std::vector<blend_space_weight> out;

    // Gradient bands are exact on every point.
    grid.compute_blend({1.0f, 0.0f}, out);
    check_normalized(out, "on a point");
    check_near(weight_of(out, clip_10), 1.0f, 1e-4f, "a parameter on a point plays that clip alone");

    // The centre of a full grid weighs the four corners equally.
    grid.compute_blend({0.5f, 0.5f}, out);
    check_normalized(out, "grid centre");
    check_near(weight_of(out, clip_00), 0.25f, 1e-3f, "grid centre is an even four-way blend");

    // Outside the layout: weights clamp, never extrapolate negative.
    grid.compute_blend({2.0f, 0.5f}, out);
    check_normalized(out, "outside the grid");
    check(weight_of(out, clip_00) < 1e-4f && weight_of(out, clip_01) < 1e-4f, "outside the grid only the near edge plays");

    // A sparse layout (no corner point) still blends between its neighbours instead of snapping
    // to the nearest point, which the grid-only scheme had to fall back to.
    blend_space_def sparse;
    sparse.add_clip({0.0f, 0.0f}, handle_00);
    sparse.add_clip({1.0f, 0.0f}, handle_10);
    sparse.add_clip({0.0f, 1.0f}, handle_01);
    sparse.compute_blend({0.9f, 0.1f}, out);
    check_normalized(out, "sparse layout");
    check(weight_of(out, clip_10) > 0.7f, "sparse layout leans on the nearest point");
    check(weight_of(out, clip_10) < 1.0f - 1e-3f, "sparse layout still blends");

    // A degenerate axis (every point shares one value) used to underflow size_t and read out of
    // bounds; the bands only need distinct points.
    blend_space_def degenerate;
    degenerate.add_clip({0.0f, 0.0f}, handle_00);
    degenerate.add_clip({1.0f, 0.0f}, handle_10);
    degenerate.compute_blend({0.9f, 0.0f}, out);
    check_normalized(out, "degenerate axis");
    check_near(weight_of(out, clip_10), 0.9f, 1e-3f, "degenerate axis interpolates along the line");
}

void test_blend_space_2d_directional()
{
    std::printf("test_blend_space_2d_directional\n");
    // Idle at the origin and four walk directions: the locomotion layout directional bands exist for.
    auto idle = make_constant_clip(0.0f, 1.0f);
    auto forward = make_constant_clip(1.0f, 1.0f);
    auto right = make_constant_clip(2.0f, 1.0f);
    auto back = make_constant_clip(3.0f, 1.0f);
    auto left = make_constant_clip(4.0f, 1.0f);

    blend_space_def space;
    space.set_2d_mode(blend_space_2d_mode::freeform_directional);
    space.add_clip({0.0f, 0.0f}, make_clip_handle(idle));
    space.add_clip({0.0f, 1.0f}, make_clip_handle(forward));
    space.add_clip({1.0f, 0.0f}, make_clip_handle(right));
    space.add_clip({0.0f, -1.0f}, make_clip_handle(back));
    space.add_clip({-1.0f, 0.0f}, make_clip_handle(left));

    std::vector<blend_space_weight> out;
    const auto weight_of = [&](const std::shared_ptr<animation_clip>& clip)
    {
        float weight = 0.0f;
        for(const auto& clip_weight : out)
        {
            if(clip_weight.clip.get().get() == clip.get())
            {
                weight += clip_weight.weight;
            }
        }
        return weight;
    };

    space.compute_blend({0.5f, 0.0f}, out);
    check_near(weight_of(idle), 0.5f, 1e-3f, "half speed to the right: idle fades radially");
    check_near(weight_of(right), 0.5f, 1e-3f, "half speed to the right: right fades in radially");

    const float diagonal = std::sqrt(0.5f);
    space.compute_blend({diagonal, diagonal}, out);
    check_near(weight_of(idle), 0.0f, 1e-3f, "full speed diagonal: no idle");
    check_near(weight_of(forward), 0.5f, 1e-3f, "full speed diagonal: half forward");
    check_near(weight_of(right), 0.5f, 1e-3f, "full speed diagonal: half right");
    check(weight_of(back) < 1e-4f && weight_of(left) < 1e-4f, "full speed diagonal: opposite directions silent");
}

void test_blend_space_phase_sync()
{
    std::printf("test_blend_space_phase_sync\n");
    // Clips of different lengths share one normalized phase: a 1 s and a 0.5 s cycle blended
    // 50/50 take 0.75 s per cycle and both sit at the same point of their cycle. Sampling at one
    // absolute time instead froze the short clip on its last key for half of every cycle.
    auto slow = make_linear_clip(0.0f, 1.0f, 1.0f);
    auto fast = make_linear_clip(0.0f, 1.0f, 0.5f);

    auto blend_space = std::make_shared<blend_space_def>();
    blend_space->add_clip({0.0f}, make_clip_handle(slow));
    blend_space->add_clip({1.0f}, make_clip_handle(fast));

    animation_player player;
    player.set_blend_space(0, blend_space);
    player.set_blend_space_parameters(0, {0.5f});
    player.play();

    auto frame = step_player(player, 0.375f);
    check_near(player.get_layers()[0].current_state.state.blend_duration.count(), 0.75f, 1e-4f,
               "cycle length is the weight-averaged clip length");
    check_near(frame.nodes[0].get_position().x, 0.5f, 1e-3f, "both clips half way through their cycle");

    frame = step_player(player, 0.75f);
    check_near(frame.nodes[0].get_position().x, 0.5f, 1e-3f, "one full cycle later both are back at the same phase");
    check(player.get_layers()[0].current_state.state.loop_count == 1, "the shared phase wrapped once");
}

void test_layer_override_with_mask()
{
    std::printf("test_layer_override_with_mask\n");
    auto make_two_node_clip = [](float x)
    {
        auto clip = make_constant_clip(x, 1.0f);
        auto& second = clip->channels.emplace_back(clip->channels.front());
        second.node_name = "second";
        second.node_index = 1;
        return clip;
    };

    animation_player player;
    player.blend_to(0, make_clip_handle(make_two_node_clip(0.0f)), seconds_t{0.0f}, true);
    player.blend_to(1, make_clip_handle(make_two_node_clip(10.0f)), seconds_t{0.0f}, true);
    player.set_layer_weight(1, 0.5f);
    player.set_layer_bone_mask(1, {0.0f, 1.0f});
    player.play();

    auto frame = step_player(player, 0.1f);
    check(player.get_layer_blend_mode(1) == animation_layer_blend_mode::override_pose, "upper layers override by default");
    check_near(frame.nodes[0].get_position().x, 0.0f, 1e-4f, "masked-out bone keeps the base pose");
    check_near(frame.nodes[1].get_position().x, 5.0f, 1e-4f, "masked-in bone takes layer weight x mask");

    player.set_layer_bone_mask(1, {});
    player.set_layer_weight(1, 1.0f);
    frame = step_player(player, 0.1f);
    check_near(frame.nodes[0].get_position().x, 10.0f, 1e-4f, "an empty mask covers every bone");
}

void test_layer_additive_from_first_frame()
{
    std::printf("test_layer_additive_from_first_frame\n");
    // An additive layer adds its change from its clip's FIRST frame, not its absolute pose.
    animation_player player;
    player.blend_to(0, make_clip_handle(make_constant_clip(2.0f, 1.0f)), seconds_t{0.0f}, true);
    player.blend_to(1, make_clip_handle(make_linear_clip(1.0f, 3.0f, 1.0f)), seconds_t{0.0f}, true);
    player.set_layer_blend_mode(1, animation_layer_blend_mode::additive);
    player.play();

    auto frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().x, 3.0f, 1e-3f, "base 2 + (2 - first frame 1) = 3");

    player.set_layer_weight(1, 0.5f);
    frame = step_player(player, 0.0f, true);
    check_near(frame.nodes[0].get_position().x, 2.5f, 1e-3f, "half weight adds half the change");
}

void test_layer_fades_in_and_out()
{
    std::printf("test_layer_fades_in_and_out\n");
    animation_player player;
    player.blend_to(0, make_clip_handle(make_constant_clip(0.0f, 1.0f)), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);

    // A layer starting from nothing fades its whole contribution in over the crossfade time.
    player.blend_to(1, make_clip_handle(make_constant_clip(10.0f, 1.0f)), seconds_t{1.0f}, true);
    auto frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().x, 5.0f, 1e-3f, "half way into the layer fade in");
    frame = step_player(player, 0.6f);
    check_near(frame.nodes[0].get_position().x, 10.0f, 1e-3f, "layer fully in after its fade");

    // Stopping with a duration fades it back out, then clears it.
    player.play_clip(1, asset_handle<animation_clip>{}, animation_play_params{seconds_t{1.0f}});
    frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().x, 5.0f, 1e-3f, "half way into the layer fade out");
    frame = step_player(player, 0.6f);
    check_near(frame.nodes[0].get_position().x, 0.0f, 1e-3f, "layer gone after its fade out");
    check(!player.get_layers()[1].has_motion(), "a faded out layer is cleared");
}

auto count_events(const animation_player& player, const std::string& name) -> int
{
    int count = 0;
    for(const auto& hit : player.get_fired_events())
    {
        count += hit.event.name == name ? 1 : 0;
    }
    return count;
}

auto make_event(const std::string& name, float position, float min_weight = 0.5f) -> animation_event
{
    animation_event event;
    event.name = name;
    event.position = position;
    event.min_weight = min_weight;
    return event;
}

void test_blend_space_points_keep_their_root_motion()
{
    std::printf("test_blend_space_points_keep_their_root_motion\n");
    // Walk, run and sprint travel 1, 3 and 6 per cycle. Moving the parameter from walk/run to
    // run/sprint changes which clips blend; each point keeps its own root-motion tracking, so the
    // entity moves on smoothly instead of measuring the run against where the walk was.
    auto blend_space = std::make_shared<blend_space_def>();
    blend_space->add_clip({0.0f}, make_clip_handle(make_linear_clip(0.0f, 1.0f, 1.0f, true)));
    blend_space->add_clip({1.0f}, make_clip_handle(make_linear_clip(0.0f, 3.0f, 1.0f, true)));
    blend_space->add_clip({2.0f}, make_clip_handle(make_linear_clip(0.0f, 6.0f, 1.0f, true)));
    animation_player player;
    player.set_blend_space(0, blend_space);
    player.set_blend_space_parameters(0, {0.5f});
    player.play();
    constexpr float step = 0.1f;
    for(int i = 0; i < 4; ++i)
    {
        step_player(player, step);
    }
    const auto walk_run = step_player(player, step);
    check_near(walk_run.motion.root_transform_delta.get_position().x, 0.2f, 1e-3f, "walk / run travel (1 + 3) / 2 per cycle");
    player.set_blend_space_parameters(0, {1.5f});
    const auto switched = step_player(player, step);
    // The run carries on; the sprint enters this frame and measures this step only, not from
    // where it left the blend.
    check_near(switched.motion.root_transform_delta.get_position().x, 0.45f, 1e-3f, "no jump when the blended points change");
    const auto run_sprint = step_player(player, step);
    check_near(run_sprint.motion.root_transform_delta.get_position().x, 0.45f, 1e-3f, "run / sprint travel (3 + 6) / 2 per cycle");
}

void test_layer_bone_missing_from_base_fades_from_rest()
{
    std::printf("test_layer_bone_missing_from_base_fades_from_rest\n");
    // The base animates bone 0 only; an override layer also animates bone 1. Half way in, bone 1
    // is half way from its rest pose to the layer's pose instead of fully at the layer's.
    auto layer_clip = make_constant_clip(10.0f, 1.0f);
    auto& second = layer_clip->channels.emplace_back(layer_clip->channels.front());
    second.node_name = "second";
    second.node_index = 1;
    animation_pose ref_pose;
    make_pose_node(ref_pose, 0, 0.0f);
    make_pose_node(ref_pose, 1, 4.0f);
    animation_player player;
    player.blend_to(0, make_clip_handle(make_constant_clip(0.0f, 1.0f)), seconds_t{0.0f}, true);
    player.blend_to(1, make_clip_handle(layer_clip), seconds_t{0.0f}, true);
    player.set_layer_weight(1, 0.5f);
    player.play();
    std::map<size_t, math::transform> nodes;
    player.update_time(seconds_t{0.1f});
    player.update_poses(ref_pose,
                        animation_retargeting_mode::index_based,
                        false,
                        [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto&)
                        {
                            nodes[desc.index] = transform;
                        });
    check_near(nodes[1].get_position().x, 7.0f, 1e-3f, "a bone only the layer animates blends from its rest pose");
}

void test_crossfade_from_a_target_never_shown()
{
    std::printf("test_crossfade_from_a_target_never_shown\n");
    // Two crossfades started in one frame: the first target was never posed, so the second one
    // starts from the playing clip rather than from an empty or stale blend pose.
    animation_player player;
    player.blend_to(0, make_clip_handle(make_constant_clip(0.0f, 1.0f)), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);
    player.blend_to(0, make_clip_handle(make_constant_clip(10.0f, 1.0f)), seconds_t{1.0f}, true);
    player.blend_to(0, make_clip_handle(make_constant_clip(20.0f, 1.0f)), seconds_t{1.0f}, true);
    auto frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().x, 10.0f, 1e-3f, "half way from the playing clip to the last target");
}

/// A clip that holds bone 0 at x = 0 and bone 1 at x = 10.
auto make_clip_with_extra_bone() -> std::shared_ptr<animation_clip>
{
    auto clip = make_constant_clip(0.0f, 1.0f);
    add_constant_bone(*clip, 1, 10.0f);
    return clip;
}

void test_crossfade_eases_a_bone_one_clip_lacks()
{
    std::printf("test_crossfade_eases_a_bone_one_clip_lacks\n");
    // Bone 1 rests at x = 4; only the first clip animates it (x = 10).
    const animation_pose rest_pose = make_two_bone_rest_pose();
    animation_player player;
    player.blend_to(0, make_clip_handle(make_clip_with_extra_bone()), seconds_t{0.0f}, true);
    player.play();
    step_player_on(player, 0.1f, rest_pose);
    player.blend_to(0, make_clip_handle(make_constant_clip(20.0f, 1.0f)), seconds_t{1.0f}, true);
    auto frame = step_player_on(player, 0.5f, rest_pose);
    check_near(frame.nodes[0].get_position().x, 10.0f, 1e-3f, "the shared bone crossfades");
    check_near(frame.nodes[1].get_position().x, 7.0f, 1e-3f, "a bone only the source animates eases toward rest");
    frame = step_player_on(player, 0.6f, rest_pose);
    check(frame.nodes.count(1) == 1, "the frame the crossfade ends still writes it");
    check_near(frame.nodes[1].get_position().x, 4.0f, 1e-4f, "and puts it at rest");
    frame = step_player_on(player, 0.1f, rest_pose);
    check(frame.nodes.count(1) == 0, "afterwards nothing writes it");

    // Back again: the bone eases in from rest instead of jumping to the clip's pose.
    player.blend_to(0, make_clip_handle(make_clip_with_extra_bone()), seconds_t{1.0f}, true);
    frame = step_player_on(player, 0.5f, rest_pose);
    check_near(frame.nodes[1].get_position().x, 7.0f, 1e-3f, "a bone only the target animates eases in from rest");
}

void test_switch_puts_a_bone_the_new_clip_lacks_at_rest()
{
    std::printf("test_switch_puts_a_bone_the_new_clip_lacks_at_rest\n");
    const animation_pose rest_pose = make_two_bone_rest_pose();
    animation_player player;
    player.blend_to(0, make_clip_handle(make_clip_with_extra_bone()), seconds_t{0.0f}, true);
    player.play();
    step_player_on(player, 0.1f, rest_pose);
    // A zero-length crossfade is a switch: nothing blends, so the switch itself leaves the bone at rest.
    player.blend_to(0, make_clip_handle(make_constant_clip(20.0f, 1.0f)), seconds_t{0.0f}, true);
    player.blend_to(0, make_clip_handle(make_constant_clip(30.0f, 1.0f)), seconds_t{0.0f}, true);
    auto frame = step_player_on(player, 0.1f, rest_pose);
    check_near(frame.nodes[0].get_position().x, 30.0f, 1e-4f, "the last switch plays");
    check(frame.nodes.count(1) == 1, "two switches in one frame still release the bone");
    check_near(frame.nodes[1].get_position().x, 4.0f, 1e-4f, "a bone the new clip lacks goes back to rest");
    frame = step_player_on(player, 0.1f, rest_pose);
    check(frame.nodes.count(1) == 0, "once");
}

void test_blend_space_point_animating_an_extra_bone()
{
    std::printf("test_blend_space_point_animating_an_extra_bone\n");
    const animation_pose rest_pose = make_two_bone_rest_pose();
    auto blend_space = std::make_shared<blend_space_def>();
    blend_space->add_clip({0.0f}, make_clip_handle(make_clip_with_extra_bone()));
    blend_space->add_clip({1.0f}, make_clip_handle(make_constant_clip(20.0f, 1.0f)));
    animation_player player;
    player.set_blend_space(0, blend_space);
    player.set_blend_space_parameters(0, {0.75f});
    player.play();
    auto frame = step_player_on(player, 0.1f, rest_pose);
    check_near(frame.nodes[0].get_position().x, 15.0f, 1e-3f, "the shared bone blends");
    check_near(frame.nodes[1].get_position().x, 5.5f, 1e-3f, "the extra bone follows its point's quarter share");
}

void test_reversed_crossfade_continues_with_any_easing()
{
    std::printf("test_reversed_crossfade_continues_with_any_easing\n");
    // Ease-in: the target's weight is t^2. Reversed half way, the pose must stay where it is
    // (target weight 0.25) and ease back from there, whatever easing the reversal asks for.
    const blend_easing_t ease_in = [](float t)
    {
        return t * t;
    };
    const auto clip_a = make_clip_handle(make_constant_clip(0.0f, 1.0f));
    animation_player player;
    player.blend_to(0, clip_a, seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);
    player.blend_to(0, make_clip_handle(make_constant_clip(10.0f, 1.0f)), seconds_t{1.0f}, true, false, ease_in);
    auto frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().x, 2.5f, 1e-3f, "ease-in half way: a quarter of the target");
    player.blend_to(0, clip_a, seconds_t{1.0f}, true, false, ease_in);
    frame = step_player(player, 0.001f);
    check_near(frame.nodes[0].get_position().x, 2.5f, 1e-2f, "the reversal continues from the pose on screen");
    frame = step_player(player, 0.499f);
    check_near(frame.nodes[0].get_position().x, 1.875f, 1e-3f, "and eases back from there");
    frame = step_player(player, 0.6f);
    check_near(frame.nodes[0].get_position().x, 0.0f, 1e-4f, "it ends on the requested clip");
}

void test_events_fire_once_per_crossing()
{
    std::printf("test_events_fire_once_per_crossing\n");
    auto clip = make_linear_clip(0.0f, 1.0f, 1.0f);
    clip->events = {make_event("start", 0.0f), make_event("middle", 0.5f)};

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();

    step_player(player, 0.3f);
    check(count_events(player, "start") == 1, "a marker on the first frame fires when playback starts");
    check(count_events(player, "middle") == 0, "a marker ahead of playback has not fired");

    step_player(player, 0.3f);
    check(count_events(player, "middle") == 1, "a crossed marker fires");
    check(count_events(player, "start") == 0, "a marker fires once, not every frame");

    step_player(player, 0.3f);
    check(player.get_fired_events().empty(), "no marker in (0.6, 0.9]");

    step_player(player, 0.3f);
    check(count_events(player, "start") == 1, "looping crosses the first-frame marker again");

    // A hitch spanning several loops reports each marker once.
    step_player(player, 2.5f);
    check(count_events(player, "middle") == 1, "a long step reports a marker once");

    // A seek jumps over markers without reporting them.
    player.seek(0, 0.75f);
    step_player(player, 0.1f);
    check(player.get_fired_events().empty(), "markers between the old and the seeked position stay silent");

    // A clip that does not loop reports its last-frame marker exactly once.
    auto one_shot = make_linear_clip(0.0f, 1.0f, 1.0f);
    one_shot->events = {make_event("end", 1.0f)};
    animation_player once;
    once.blend_to(0, make_clip_handle(one_shot), seconds_t{0.0f}, false);
    once.play();
    step_player(once, 0.6f);
    check(count_events(once, "end") == 0, "end marker not reached yet");
    step_player(once, 0.6f);
    check(count_events(once, "end") == 1, "end marker fires on reaching the end");
    step_player(once, 0.6f);
    check(count_events(once, "end") == 0, "a clip held on its last frame does not repeat the marker");
}

void test_events_respect_weights()
{
    std::printf("test_events_respect_weights\n");
    auto clip_a = make_linear_clip(0.0f, 1.0f, 1.0f);
    clip_a->events = {make_event("a", 0.25f)};
    auto clip_b = make_linear_clip(0.0f, 1.0f, 1.0f);
    clip_b->events = {make_event("b", 0.25f)};

    animation_player player;
    player.blend_to(0, make_clip_handle(clip_a), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.01f);

    // During a crossfade each side reports with its own weight: at a quarter of the fade the
    // outgoing clip still holds 3/4 of the pose and the incoming one 1/4.
    player.blend_to(0, make_clip_handle(clip_b), seconds_t{1.0f}, true);
    step_player(player, 0.25f);
    check(count_events(player, "a") == 1, "outgoing clip above the default weight reports");
    check(count_events(player, "b") == 0, "incoming clip below the default weight stays silent");

    // A blend space reports its highest weighted clip only.
    auto walk = make_linear_clip(0.0f, 1.0f, 1.0f);
    walk->events = {make_event("step", 0.5f)};
    auto run = make_linear_clip(0.0f, 1.0f, 1.0f);
    run->events = {make_event("step", 0.5f)};
    auto blend_space = std::make_shared<blend_space_def>();
    blend_space->add_clip({0.0f}, make_clip_handle(walk));
    blend_space->add_clip({1.0f}, make_clip_handle(run));
    animation_player space_player;
    space_player.set_blend_space(0, blend_space);
    space_player.set_blend_space_parameters(0, {0.4f});
    space_player.play();
    step_player(space_player, 0.6f);
    check(count_events(space_player, "step") == 1, "a blend space reports a shared marker once");
    check(!space_player.get_fired_events().empty() &&
              space_player.get_fired_events().front().clip.get().get() == walk.get(),
          "the marker comes from the highest weighted clip");

    // Layer weight scales the effective weight.
    animation_player layered;
    layered.blend_to(0, make_clip_handle(make_constant_clip(0.0f, 1.0f)), seconds_t{0.0f}, true);
    auto wave = make_linear_clip(0.0f, 1.0f, 1.0f);
    wave->events = {make_event("wave", 0.5f), make_event("always", 0.5f, 0.0f)};
    layered.blend_to(1, make_clip_handle(wave), seconds_t{0.0f}, true);
    layered.set_layer_weight(1, 0.3f);
    layered.play();
    step_player(layered, 0.6f);
    check(count_events(layered, "wave") == 0, "a light layer stays under the default threshold");
    check(count_events(layered, "always") == 1, "a zero threshold fires whenever the clip plays");
}

void test_curves_blend_with_the_pose()
{
    std::printf("test_curves_blend_with_the_pose\n");
    auto with_curve = make_constant_clip(0.0f, 1.0f);
    auto& curve = with_curve->curves.emplace_back();
    curve.name = "reach";
    curve.curve.add_point(1.0f, 0.0f);
    curve.curve.add_point(1.0f, 1.0f);
    auto without_curve = make_constant_clip(0.0f, 1.0f);
    const uint32_t reach = get_animation_curve_id("reach");

    animation_player player;
    player.blend_to(0, make_clip_handle(with_curve), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);
    check(player.has_curve(reach), "a playing clip's curve is present");
    check_near(player.get_curve_value(reach), 1.0f, 1e-4f, "curve sampled");

    // A curve one side of a crossfade lacks counts as 0 there, so it fades instead of jumping.
    player.blend_to(0, make_clip_handle(without_curve), seconds_t{1.0f}, true);
    step_player(player, 0.5f);
    check_near(player.get_curve_value(reach), 0.5f, 1e-3f, "curve fades across the crossfade");
    step_player(player, 0.6f);
    check(!player.has_curve(reach), "curve gone once its clip faded out");
    check_near(player.get_curve_value(reach, -1.0f), -1.0f, 1e-6f, "missing curve returns the fallback");
}

/// A clip's curves and events in both archives clips are stored in: the import settings are
/// associative, the compiled clip is binary, and binary archives read fields in the order they were
/// written.
void test_clip_curves_survive_both_archives()
{
    std::printf("test_clip_curves_survive_both_archives\n");
    animation_curve source;
    source.name = "HandIK";
    source.curve.add_point(0.0f, 0.0f);
    source.curve.add_point(1.0f, 0.34f);
    source.curve.add_point(1.0f, 0.6f);
    source.curve.add_point(0.0f, 0.82f);
    const float samples[] = {0.0f, 0.2f, 0.34f, 0.45f, 0.7f, 0.9f, 1.0f};

    const auto check_same = [&](const animation_curve& restored, const char* archive)
    {
        check(restored.name == source.name, std::string("curve name survives the ") + archive + " archive");
        for(float progress : samples)
        {
            check_near(restored.curve.sample(progress),
                       source.curve.sample(progress),
                       1e-5f,
                       std::string("curve value survives the ") + archive + " archive");
        }
    };

    {
        std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
        {
            ser20::oarchive_binary_t archive(stream);
            try_save(archive, ser20::make_nvp("curve", source));
        }
        animation_curve restored;
        ser20::iarchive_binary_t archive(stream);
        try_load(archive, ser20::make_nvp("curve", restored));
        check_same(restored, "binary");
    }
    {
        std::stringstream stream;
        {
            auto archive = ser20::create_oarchive_associative(stream);
            try_save(archive, ser20::make_nvp("curve", source));
        }
        animation_curve restored;
        auto archive = ser20::create_iarchive_associative(stream);
        try_load(archive, ser20::make_nvp("curve", restored));
        check_same(restored, "associative");
    }
}

void test_binding_maps_clips_onto_the_target_skeleton()
{
    std::printf("test_binding_maps_clips_onto_the_target_skeleton\n");
    // Channels resolve onto target indices, in target order, so clips exported from different
    // files (whose own indices disagree) blend bone by bone. Unresolved channels are skipped.
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    for(const auto& [name, x] : std::vector<std::pair<std::string, float>>{{"hand", 1.0f}, {"head", 2.0f}, {"tail", 3.0f}})
    {
        auto& channel = clip->channels.emplace_back();
        channel.node_name = name;
        channel.node_index = clip->channels.size() - 1;
        channel.position_keys = {{seconds_t{0.0f}, {x, 0.0f, 0.0f}}};
    }

    animation_skeleton_binding binding;
    binding.version = 7;
    binding.resolve = [](const animation_channel& channel)
    {
        if(channel.node_name == "hand")
            return 5;
        if(channel.node_name == "head")
            return 2;
        return -1;
    };

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();
    player.update_time(seconds_t{0.1f});
    std::vector<std::pair<size_t, float>> written;
    static const animation_pose empty_ref_pose{};
    player.update_poses(empty_ref_pose,
                        binding,
                        false,
                        [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto&)
                        {
                            written.emplace_back(desc.index, transform.get_position().x);
                        });
    check(written.size() == 2, "unresolved channel skipped");
    check(written.size() == 2 && written[0].first == 2 && written[1].first == 5, "nodes written in target order");
    check(written.size() == 2 && std::fabs(written[0].second - 2.0f) < 1e-4f, "head channel lands on target 2");
}

void test_root_motion_accumulates_across_loops()
{
    std::printf("test_root_motion_accumulates_across_loops\n");
    auto clip = make_linear_clip(0.0f, 1.0f, 1.0f, true /*root_motion*/);
    auto handle = make_clip_handle(clip);

    animation_player player;
    player.blend_to(0, handle, seconds_t{0.0f}, true /*loop*/);
    player.play();

    // The first step measures from the clip start, where the step began.
    auto frame = step_player(player, 0.25f);
    check_near(frame.motion.root_transform_delta.get_translation().x, 0.25f, 1e-4f, "first step moves by its own travel");

    frame = step_player(player, 0.5f);
    check_near(frame.motion.root_transform_delta.get_translation().x, 0.5f, 1e-3f, "delta matches elapsed time");

    // One big step across TWO loop boundaries: 0.75 + 1.5 = 2.25 -> wraps
    // twice, lands at 0.25. The delta must count every wrap (regression: only
    // one loop offset was ever added).
    frame = step_player(player, 1.5f);
    check_near(frame.motion.root_transform_delta.get_translation().x, 1.5f, 1e-3f, "multi-wrap delta is exact");
}

void test_root_motion_no_teleport_on_replay()
{
    std::printf("test_root_motion_no_teleport_on_replay\n");
    auto clip = make_linear_clip(0.0f, 1.0f, 1.0f, true /*root_motion*/);
    auto handle = make_clip_handle(clip);

    animation_player player;
    player.blend_to(0, handle, seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.25f);
    step_player(player, 0.35f); // now mid-clip with stale motion state

    // Regression: stop() kept the root-motion tracking state, so the next
    // playback's first sample computed a delta against the old position and
    // teleported the entity (with a bogus loop offset on top).
    player.stop();
    player.play();
    auto frame = step_player(player, 0.02f);
    check_near(frame.motion.root_transform_delta.get_translation().x, 0.02f, 1e-4f, "replay measures from the clip start");
    frame = step_player(player, 0.02f);
    check_near(frame.motion.root_transform_delta.get_translation().x, 0.02f, 1e-3f, "replay resumes normal deltas");
}

void test_phase_synced_blend_has_no_motion_spike()
{
    std::printf("test_phase_synced_blend_has_no_motion_spike\n");
    auto clip_a = make_linear_clip(0.0f, 1.0f, 1.0f, true);
    auto clip_b = make_linear_clip(0.0f, 1.0f, 1.0f, true);

    animation_player player;
    player.blend_to(0, make_clip_handle(clip_a), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.5f); // half way through clip_a

    // Phase-synced blend to clip_b starts sampling clip_b at t=0.5. The old
    // clip-start baseline turned that into a 0.5-unit teleport on frame one.
    player.blend_to(0, make_clip_handle(clip_b), seconds_t{0.2f}, true, true /*phase_sync*/);
    auto frame = step_player(player, 0.02f);
    check(std::fabs(frame.motion.root_transform_delta.get_translation().x) < 0.1f,
          "phase-synced blend start produces no root-motion spike");
}

void test_keep_in_place_preserves_sway()
{
    std::printf("test_keep_in_place_preserves_sway\n");
    // keep_in_place must discard the clip's TRAVEL - both the linearly
    // accrued start-to-end displacement AND the capture's speed asymmetry
    // along the travel line (mocap travel is not constant-speed; leaving the
    // asymmetry plays a fore-aft lunge once per loop) - while keeping the
    // lateral sway in the pose. The old behavior froze the bone's local X/Z
    // entirely (bone weight 0), which flattened every gait.
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    auto& channel = clip->channels.emplace_back();
    channel.node_name = "root";
    channel.node_index = 0;
    // Z carries travel 0 -> 1, running +0.1 ahead of constant speed at the
    // midpoint (asymmetric capture); X carries a +0.1 lateral sway bump.
    channel.position_keys = {{seconds_t{0.0f}, {0.0f, 0.0f, 0.0f}},
                             {seconds_t{0.5f}, {0.1f, 0.0f, 0.6f}},
                             {seconds_t{1.0f}, {0.0f, 0.0f, 1.0f}}};
    channel.rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()}};
    channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    clip->root_motion.position_node_index = 0;
    clip->root_motion.position_node_name = "root";
    clip->root_motion.keep_in_place = true;
    clip->root_motion.keep_position_y = true;

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();

    // t = 0.5: the lateral bump survives; the +0.1 fore-aft lead does not.
    auto frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().x, 0.1f, 1e-3f, "lateral sway survives in place");
    check_near(frame.nodes[0].get_position().z, 0.0f, 1e-3f, "travel-line asymmetry is removed");
    check_near(frame.motion.bone_position_weights.x, 1.0f, 1e-4f, "in-place bone x is written, not frozen");

    // Step 0.75 wraps 0.5 -> 0.25: sampled (0.05, 0, 0.3), lateral half-bump
    // kept, fore-aft removed. The residual matches at both key-range ends,
    // so the wrap produces no discontinuity.
    frame = step_player(player, 0.75f);
    check_near(frame.nodes[0].get_position().x, 0.05f, 1e-3f, "lateral residual continuous across loop wrap");
    check_near(frame.nodes[0].get_position().z, 0.0f, 1e-3f, "travel line stays removed after wrap");
    check_near(math::length(frame.motion.root_transform_delta.get_position()), 0.0f, 1e-4f, "in place moves no root translation");
}

void test_root_motion_extract_pose_is_blend_safe()
{
    std::printf("test_root_motion_extract_pose_is_blend_safe\n");
    // Extraction moves the FULL per-frame displacement into the root delta,
    // so the pose's horizontal position must be the constant clip-start
    // baseline written at bone weight 1 - not raw travel masked by a zero
    // weight. Pose blending ignores per-pose weights, so masked raw travel
    // leaked into crossfades against clips whose weight is 1: the model
    // lunged forward, then slid back as the blend completed.
    auto clip = make_linear_clip(0.0f, 1.0f, 1.0f, true /*root_motion*/);

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();

    step_player(player, 0.25f);
    auto frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().x, 0.0f, 1e-3f, "extracted pose holds the clip-start baseline");
    check_near(frame.motion.bone_position_weights.x, 1.0f, 1e-4f, "extracted pose is written, not weight-masked");
    check_near(frame.motion.root_transform_delta.get_translation().x, 0.5f, 1e-3f, "extraction still yields the full delta");

    // Crossfade into an in-place clip mid-travel: the blended pose must stay
    // at the baseline instead of leaking the traveling clip's raw position.
    auto in_place = make_linear_clip(0.0f, 1.0f, 1.0f, true);
    in_place->root_motion.keep_in_place = true;
    player.blend_to(0, make_clip_handle(in_place), seconds_t{0.2f}, true);
    frame = step_player(player, 0.1f);
    check(std::fabs(frame.nodes[0].get_position().x) < 0.01f, "mid-blend pose does not leak travel");
}

void test_root_motion_extract_rotation_is_blend_safe()
{
    std::printf("test_root_motion_extract_rotation_is_blend_safe\n");
    // Rotation extraction moves the FULL per-frame rotation change into the
    // root delta, so the pose must hold the constant clip-start rotation at
    // bone weight 1 - not raw traveling yaw masked by a zero weight, which
    // leaked into crossfades against kept-rotation clips as a heading twitch.
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    auto& channel = clip->channels.emplace_back();
    channel.node_name = "root";
    channel.node_index = 0;
    channel.position_keys = {{seconds_t{0.0f}, {0.0f, 0.0f, 0.0f}}};
    // Yaws 90 degrees over the loop; rotation extraction is on (keep_rotation
    // stays false), so the yaw belongs to the root delta, not the pose.
    channel.rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()},
                             {seconds_t{1.0f}, math::angleAxis(1.5707963f, math::vec3{0.0f, 1.0f, 0.0f})}};
    channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    clip->root_motion.position_node_index = 0;
    clip->root_motion.position_node_name = "root";
    clip->root_motion.rotation_node_index = 0;
    clip->root_motion.rotation_node_name = "root";

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();

    step_player(player, 0.25f);
    auto frame = step_player(player, 0.5f);
    check_near(frame.motion.bone_rotation_weight, 1.0f, 1e-4f, "extracted rotation pose is written, not weight-masked");
    check(std::fabs(frame.nodes[0].get_rotation().w) > 0.9999f, "extracted pose holds the clip-start rotation");
    // Delta over the 0.5s step is 45 degrees of yaw: +Z rotates toward +X.
    math::vec3 fwd = frame.motion.root_transform_delta.get_rotation() * math::vec3{0.0f, 0.0f, 1.0f};
    check_near(fwd.x, 0.70711f, 1e-3f, "extraction still yields the full rotation delta");

    // Crossfade into a clip without root rotation: the blended pose must stay
    // near identity instead of leaking the traveling clip's raw yaw.
    player.blend_to(0, make_clip_handle(make_linear_clip(0.0f, 0.0f, 1.0f, true)), seconds_t{0.2f}, true);
    frame = step_player(player, 0.1f);
    check(std::fabs(frame.nodes[0].get_rotation().w) > 0.999f, "mid-blend pose does not leak extracted yaw");
}

void test_root_motion_unapplied_extraction_keeps_authored_pose()
{
    std::printf("test_root_motion_unapplied_extraction_keeps_authored_pose\n");
    // The clip-start baseline is the pose complement of an APPLIED root
    // delta. When the consumer does not apply root motion the delta is
    // discarded, so pinning erased the authored motion: an imported Die clip
    // (hips translate and pitch to the ground, extraction mode, root motion
    // off by default) froze upright instead of falling. Without extraction
    // the raw channel must stay in the pose.
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    auto& channel = clip->channels.emplace_back();
    channel.node_name = "root";
    channel.node_index = 0;
    channel.position_keys = {{seconds_t{0.0f}, {0.0f, 0.0f, 0.0f}}, {seconds_t{1.0f}, {0.0f, 0.0f, -1.0f}}};
    channel.rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()},
                             {seconds_t{1.0f}, math::angleAxis(-1.5707963f, math::vec3{1.0f, 0.0f, 0.0f})}};
    channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    clip->root_motion.position_node_index = 0;
    clip->root_motion.position_node_name = "root";
    clip->root_motion.rotation_node_index = 0;
    clip->root_motion.rotation_node_name = "root";

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();

    step_player(player, 0.25f, false, false /*extract_root_motion*/);
    auto frame = step_player(player, 0.5f, false, false /*extract_root_motion*/);
    check_near(frame.nodes[0].get_position().z, -0.75f, 1e-3f, "unapplied extraction keeps authored travel in pose");
    // 3/4 through a -90 degree pitch: |w| = cos(33.75 deg), well away from 1.
    check(std::fabs(frame.nodes[0].get_rotation().w) < 0.99f, "unapplied extraction keeps authored rotation in pose");
    // The delta stays tracked so enabling root motion later does not teleport.
    check_near(frame.motion.root_transform_delta.get_translation().z, -0.5f, 1e-3f, "delta still tracked while unapplied");
}

/// A hips channel whose parent frame stands Z-up, the way FBX armatures sit below the node that
/// turns them upright: local +Z is up and local -Y is forward.
auto make_z_up_hips_clip() -> std::shared_ptr<animation_clip>
{
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    auto& channel = clip->channels.emplace_back();
    channel.node_name = "hips";
    channel.node_index = 0;
    channel.has_rest = true;
    channel.parent_offset.set_rotation(math::angleAxis(-1.5707963f, math::vec3{1.0f, 0.0f, 0.0f}));
    channel.rest_local.set_position({0.0f, 0.0f, 1.0f});
    channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    clip->root_motion.position_node_index = 0;
    clip->root_motion.position_node_name = "hips";
    clip->root_motion.rotation_node_index = 0;
    clip->root_motion.rotation_node_name = "hips";
    return clip;
}

void test_root_motion_splits_along_the_clip_up_axis()
{
    std::printf("test_root_motion_splits_along_the_clip_up_axis\n");
    // A crouch that steps forward: the hips drop from 1 to 0.5 while travelling one unit. With the
    // height kept in the pose (the import default), the pose must drop and only the travel may be
    // pinned. Splitting on the keys' own Y kept the travel and pinned the height instead - every
    // crouch, roll and landing of a Z-up skeleton hovered at standing height.
    auto clip = make_z_up_hips_clip();
    clip->root_motion.keep_position_y = true;
    clip->channels[0].position_keys = {{seconds_t{0.0f}, {0.0f, 0.0f, 1.0f}}, {seconds_t{1.0f}, {0.0f, -1.0f, 0.5f}}};
    clip->channels[0].rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()}};

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, false);
    player.play();
    step_player(player, 0.25f);
    auto frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().z, 0.625f, 1e-3f, "kept height follows the clip's up axis");
    check_near(frame.nodes[0].get_position().y, 0.0f, 1e-3f, "extracted travel is pinned to the clip start");
    check_near(frame.motion.bone_position_weights.y, 1.0f, 1e-4f, "pose position is written whole");
    check_near(frame.motion.root_transform_delta.get_translation().y, -0.5f, 1e-3f, "travel is extracted");

    // Extracted height: the pose holds the clip-start height and the drop goes to the delta.
    auto extracted = make_z_up_hips_clip();
    extracted->channels[0].position_keys = clip->channels[0].position_keys;
    extracted->channels[0].rotation_keys = clip->channels[0].rotation_keys;
    animation_player extracting;
    extracting.blend_to(0, make_clip_handle(extracted), seconds_t{0.0f}, false);
    extracting.play();
    step_player(extracting, 0.25f);
    frame = step_player(extracting, 0.5f);
    check_near(frame.nodes[0].get_position().z, 1.0f, 1e-3f, "extracted height holds the clip start");
    check_near(frame.motion.bone_position_weights.z, 1.0f, 1e-4f, "extracted height is written, not frozen");
    check_near(frame.motion.root_transform_delta.get_translation().z, -0.25f, 1e-3f, "height change is extracted");
}

void test_root_motion_root_bone_locks_to_rest()
{
    std::printf("test_root_motion_root_bone_locks_to_rest\n");
    // The second clip of a combo starts where the first ended: its floor-level root bone begins
    // 0.75 forward. Held at that first frame, the body stood 0.75 ahead of the entity for the whole
    // clip and slid back when the combo ended. A root bone at the floor holds its rest instead.
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    auto& root = clip->channels.emplace_back();
    root.node_name = "root";
    root.node_index = 0;
    root.has_rest = true;
    root.position_keys = {{seconds_t{0.0f}, {0.0f, 0.0f, 0.75f}}, {seconds_t{1.0f}, {0.0f, 0.0f, 1.25f}}};
    root.rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()}};
    root.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    auto& hips = clip->channels.emplace_back();
    hips.node_name = "hips";
    hips.node_index = 1;
    hips.has_rest = true;
    hips.parent_channel = 0;
    hips.rest_local.set_position({0.0f, 1.0f, 0.0f});
    hips.position_keys = {{seconds_t{0.0f}, {0.0f, 1.0f, 0.0f}}};
    hips.rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()}};
    hips.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    clip->root_motion.position_node_name = "root";
    clip->root_motion.keep_position_y = true;

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, false);
    player.play();
    step_player(player, 0.25f);
    auto frame = step_player(player, 0.5f);
    check_near(frame.nodes[0].get_position().z, 0.0f, 1e-3f, "a floor-level root holds its rest under the entity");
    check_near(frame.motion.root_transform_delta.get_translation().z, 0.25f, 1e-3f, "its travel still moves the entity");

    // Hips carrying root motion keep their first frame: it holds the clip's stance.
    auto hips_clip = make_z_up_hips_clip();
    hips_clip->channels[0].position_keys = {{seconds_t{0.0f}, {0.0f, 0.2f, 1.0f}}, {seconds_t{1.0f}, {0.0f, -0.8f, 1.0f}}};
    hips_clip->channels[0].rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()}};
    hips_clip->root_motion.keep_position_y = true;
    animation_player hips_player;
    hips_player.blend_to(0, make_clip_handle(hips_clip), seconds_t{0.0f}, false);
    hips_player.play();
    step_player(hips_player, 0.25f);
    frame = step_player(hips_player, 0.5f);
    check_near(frame.nodes[0].get_position().y, 0.2f, 1e-3f, "hips hold their first frame");
}

void test_root_motion_extracts_only_the_heading()
{
    std::printf("test_root_motion_extracts_only_the_heading\n");
    // Hips that turn and lean: only the turn about the clip's up axis moves the entity. Taking the
    // whole rotation laid the character over by the lean and froze the hips at the start pose.
    auto clip = make_z_up_hips_clip();
    clip->channels[0].position_keys = {{seconds_t{0.0f}, {0.0f, 0.0f, 1.0f}}};
    const math::quat lean_and_turn = math::angleAxis(1.5707963f, math::vec3{0.0f, 0.0f, 1.0f}) *
                                     math::angleAxis(0.5235988f, math::vec3{1.0f, 0.0f, 0.0f});
    clip->channels[0].rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()}, {seconds_t{1.0f}, lean_and_turn}};

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, false);
    player.play();
    step_player(player, 0.25f);
    auto frame = step_player(player, 0.5f);
    const math::vec3 local_up{0.0f, 0.0f, 1.0f};
    const math::quat delta = frame.motion.root_transform_delta.get_rotation();
    check(math::length(delta * local_up - local_up) < 1e-4f, "extracted delta only turns about the up axis");
    check(std::fabs(delta.w) < 0.999f, "the turn is extracted");
    const math::quat pose = frame.nodes[0].get_rotation();
    check(std::fabs(pose.w) < 0.9999f, "the lean stays in the pose");
    // Headings are measured upright: no turn about +Y once the parent frame is applied.
    const math::quat upright_pose = clip->channels[0].parent_offset.get_rotation() * pose;
    check(std::fabs(upright_pose.y) < 1e-4f, "the pose keeps the clip-start heading");
}

void test_zero_duration_blend_and_finite_output()
{
    std::printf("test_zero_duration_blend_and_finite_output\n");
    // A zero blend duration used to divide 0/0 -> NaN progress that never
    // completed. It must complete immediately and produce finite transforms.
    auto clip = make_linear_clip(0.0f, 1.0f, 1.0f);
    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();

    auto frame = step_player(player, 0.25f);
    check(frame.updated, "zero-duration blend still updates");
    check(std::isfinite(frame.nodes[0].get_position().x), "position is finite");
    check_near(frame.nodes[0].get_position().x, 0.25f, 1e-3f, "clip sampled at elapsed time");
}

void test_force_update_steps_while_paused()
{
    std::printf("test_force_update_steps_while_paused\n");
    auto clip = make_linear_clip(0.0f, 1.0f, 1.0f);
    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.25f);

    player.pause();
    auto frame = step_player(player, 0.25f);
    check(!frame.updated, "paused player does not update");

    // Regression: `force` was ignored entirely, so editor frame stepping was
    // a no-op.
    frame = step_player(player, 0.25f, true /*force*/);
    check(frame.updated, "forced update steps a paused player");
    check_near(frame.nodes[0].get_position().x, 0.5f, 1e-3f, "forced step advanced the time");
}

void test_blend_to_null_clears_layer()
{
    std::printf("test_blend_to_null_clears_layer\n");
    auto clip = make_linear_clip(0.0f, 1.0f, 1.0f);
    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();
    check(step_player(player, 0.1f).updated, "clip plays");

    // Regression: a null clip cleared only the current state and left an
    // in-flight crossfade target running.
    player.blend_to(0, asset_handle<animation_clip>{});
    check(!step_player(player, 0.1f).updated, "null clip stops the layer");
}

void test_retarget_blend_keeps_no_pop()
{
    std::printf("test_retarget_blend_keeps_no_pop\n");
    // Interrupting an A->B crossfade with a blend to C used to restart the
    // fade from A's raw pose - a visible pop. The current blended pose must be
    // frozen as the new source so the output stays continuous.
    auto clip_a = make_constant_clip(0.0f, 1.0f);
    auto clip_b = make_constant_clip(10.0f, 1.0f);
    auto clip_c = make_constant_clip(20.0f, 1.0f);

    animation_player player;
    player.blend_to(0, make_clip_handle(clip_a), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);

    player.blend_to(0, make_clip_handle(clip_b), seconds_t{1.0f}, true);
    auto frame = step_player(player, 0.5f); // half way into the A->B fade
    const float mid_blend_x = frame.nodes[0].get_position().x;
    check_near(mid_blend_x, 5.0f, 0.5f, "mid-crossfade pose is a mix of A and B");

    player.blend_to(0, make_clip_handle(clip_c), seconds_t{1.0f}, true);
    frame = step_player(player, 0.01f);
    // One tiny step into the new fade: the pose must still be near the frozen
    // A+B mix, not snapped back to A (0) or forward to C (20).
    check(std::fabs(frame.nodes[0].get_position().x - mid_blend_x) < 1.0f,
          "retargeting a crossfade does not pop");
}

void test_root_motion_result_weight_blending()
{
    std::printf("test_root_motion_result_weight_blending\n");
    // A pose without root motion has meaningless default weights. Blending
    // used to MULTIPLY weights, which could zero both the root and the bone
    // weight of an axis (position frozen). The other pose's weights must win.
    animation_pose::root_motion_result no_motion{};
    animation_pose::root_motion_result keep_xz{};
    keep_xz.root_position_node_index = 0;
    keep_xz.root_position_weights = {0.0f, 1.0f, 0.0f};
    keep_xz.bone_position_weights = {1.0f, 0.0f, 1.0f};

    auto result = blend(no_motion, keep_xz, 0.3f);
    check(result.root_position_node_index == 0, "root motion node carried");
    check_near(result.root_position_weights.y, 1.0f, 1e-4f, "root weight y kept");
    check_near(result.bone_position_weights.x, 1.0f, 1e-4f, "bone weight x kept (was zeroed by multiply)");
    check_near(result.bone_position_weights.z, 1.0f, 1e-4f, "bone weight z kept (was zeroed by multiply)");
}

void test_empty_tracks_have_sane_defaults()
{
    std::printf("test_empty_tracks_have_sane_defaults\n");
    // A channel with no rotation/scale keys used to produce a zero quaternion
    // (NaN after normalize) and a zero scale (collapsed bone).
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    auto& channel = clip->channels.emplace_back();
    channel.node_name = "root";
    channel.node_index = 0;
    channel.position_keys = {{seconds_t{0.0f}, {1.0f, 2.0f, 3.0f}}};

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();
    auto frame = step_player(player, 0.1f);

    const auto rotation = frame.nodes[0].get_rotation();
    check_near(math::length(rotation), 1.0f, 1e-4f, "missing rotation track yields a unit quaternion");
    check_near(frame.nodes[0].get_scale().x, 1.0f, 1e-4f, "missing scale track yields scale 1");
    check_near(frame.nodes[0].get_position().y, 2.0f, 1e-4f, "single position key sampled");
}

void test_target_skeleton_takes_the_bind_pose()
{
    std::printf("test_target_skeleton_takes_the_bind_pose\n");
    // A model's skeleton of three bones whose bind pose names two of them: every bone gets a bind
    // transform, so rest-aligned retargeting sees the whole skeleton.
    animation_pose bind_pose;
    bind_pose.nodes.resize(2);
    bind_pose.nodes[0].desc.index = 1;
    bind_pose.nodes[0].transform.set_position({0.0f, 0.9f, 0.0f});
    bind_pose.nodes[1].desc.index = 2;
    bind_pose.nodes[1].transform.set_position({0.0f, 0.15f, 0.0f});

    animation_target_skeleton skeleton;
    skeleton.set_bones({"root", "hips", "spine"}, {-1, 0, 1}, bind_pose);
    skeleton.compute_rest_globals();

    check(skeleton.names.size() == 3 && skeleton.parents.size() == 3, "names and parents kept");
    check(skeleton.bind_local.size() == 3, "one bind transform per bone");
    check(skeleton.rest_global.size() == 3, "one rest transform per bone");
    check_near(skeleton.bind_local[0].get_position().y, 0.0f, 1e-6f, "a bone the pose leaves out binds at identity");
    check_near(skeleton.bind_local[1].get_position().y, 0.9f, 1e-6f, "a bone takes its bind transform");
    check_near(skeleton.rest_global[2].get_position().y, 1.05f, 1e-5f, "rest globals chain the bind pose");
}

void test_rest_aligned_scale_follows_the_bone()
{
    std::printf("test_rest_aligned_scale_follows_the_bone\n");
    // The source bone runs along its local +Y, the target's along its local +X: a stretch along
    // the source bone stretches the target bone along its length, not across it.
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    const auto add_channel = [&](const std::string& name, int parent, const math::vec3& position, const math::vec3& scale)
    {
        auto& channel = clip->channels.emplace_back();
        channel.node_name = name;
        channel.node_index = clip->channels.size() - 1;
        channel.position_keys = {{seconds_t{0.0f}, position}};
        channel.rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()}};
        channel.scaling_keys = {{seconds_t{0.0f}, scale}};
        channel.has_rest = true;
        channel.rest_local.set_position(position);
        channel.parent_channel = parent;
    };
    add_channel("root", -1, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    add_channel("bone", 0, {0.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 1.0f});
    add_channel("side", 0, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});

    auto target = std::make_shared<animation_target_skeleton>();
    target->parents = {-1, 0, 0};
    target->bind_local.resize(3);
    target->bind_local[1].set_position({0.0f, 1.0f, 0.0f});
    target->bind_local[1].set_rotation(math::angleAxis(math::radians(90.0f), math::vec3(0.0f, 0.0f, 1.0f)));
    target->bind_local[2].set_position({1.0f, 0.0f, 0.0f});
    target->compute_rest_globals();

    animation_skeleton_binding binding;
    binding.version = 5;
    binding.resolve = [](const animation_channel& channel)
    {
        return channel.node_name == "root" ? 0 : channel.node_name == "bone" ? 1 : 2;
    };
    binding.rest_aligned_target = target;

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();
    player.update_time(seconds_t{0.1f});
    std::map<size_t, math::transform> written;
    static const animation_pose empty_ref_pose{};
    player.update_poses(empty_ref_pose,
                        binding,
                        false,
                        [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto&)
                        {
                            written[desc.index] = transform;
                        });
    const math::vec3 scale = written[1].get_scale();
    check_near(scale.x, 2.0f, 1e-4f, "the target bone stretches along its length, its local X");
    check_near(scale.y, 1.0f, 1e-4f, "and keeps its thickness");
}

void test_rest_aligned_retargeting()
{
    std::printf("test_rest_aligned_retargeting\n");
    // Source: an exporter's axis conversion sits on the root (-90 degrees about X, so the hips'
    // local +Z points up) and the hips stand 1 unit high. Target: no conversion, hips 2 units high,
    // a spine twice as long. A plain copy of local transforms would lay the target on its back.
    const auto rx = math::angleAxis(math::radians(-90.0f), math::vec3(1.0f, 0.0f, 0.0f));
    const auto local_yaw = math::angleAxis(math::radians(30.0f), math::vec3(0.0f, 0.0f, 1.0f));

    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    const auto add_channel = [&](const std::string& name, int parent, const math::transform& rest,
                                 const math::vec3& position, const math::quat& rotation)
    {
        auto& channel = clip->channels.emplace_back();
        channel.node_name = name;
        channel.node_index = clip->channels.size() - 1;
        channel.position_keys = {{seconds_t{0.0f}, position}};
        channel.rotation_keys = {{seconds_t{0.0f}, rotation}};
        channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
        channel.has_rest = true;
        channel.rest_local = rest;
        channel.parent_channel = parent;
    };
    math::transform root_rest;
    root_rest.set_rotation(rx);
    math::transform hips_rest;
    hips_rest.set_position({0.0f, 0.0f, 1.0f});
    math::transform spine_rest;
    spine_rest.set_position({0.0f, 0.0f, 0.5f});
    add_channel("root", -1, root_rest, {0.0f, 0.0f, 0.0f}, rx);
    // The hips bob 0.1 along their local +Y (world -Z here) and yaw 30 degrees about local Z (world up).
    add_channel("hips", 0, hips_rest, {0.0f, 0.1f, 1.0f}, local_yaw);
    add_channel("spine", 1, spine_rest, {0.0f, 0.0f, 0.5f}, math::identity<math::quat>());
    clip->root_motion.position_node_name = "hips";
    clip->root_motion.rotation_node_name = "hips";

    auto target = std::make_shared<animation_target_skeleton>();
    target->parents = {-1, 0, 1};
    target->bind_local.resize(3);
    target->bind_local[1].set_position({0.0f, 2.0f, 0.0f});
    target->bind_local[2].set_position({0.0f, 1.0f, 0.0f});
    target->compute_rest_globals();

    animation_skeleton_binding binding;
    binding.version = 3;
    binding.resolve = [](const animation_channel& channel)
    {
        return channel.node_name == "root" ? 0 : channel.node_name == "hips" ? 1 : 2;
    };
    binding.rest_aligned_target = target;

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();
    player.update_time(seconds_t{0.1f});
    std::map<size_t, math::transform> written;
    static const animation_pose empty_ref_pose{};
    player.update_poses(empty_ref_pose,
                        binding,
                        false,
                        [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto&)
                        {
                            written[desc.index] = transform;
                        });

    check(written.size() == 3, "every mapped bone written");
    const auto expected_yaw = math::angleAxis(math::radians(30.0f), math::vec3(0.0f, 1.0f, 0.0f));
    check(std::fabs(std::fabs(math::dot(written[0].get_rotation(), math::identity<math::quat>())) - 1.0f) < 1e-4f,
          "the source root's axis conversion does not reach the target root");
    check(std::fabs(std::fabs(math::dot(written[1].get_rotation(), expected_yaw)) - 1.0f) < 1e-4f,
          "the hips turn about the world up, as they did in the source");
    check_near(written[1].get_position().y, 2.0f, 1e-4f, "the hips keep the target's height");
    check_near(written[1].get_position().z, -0.2f, 1e-4f, "the bob is carried into the target frame and scaled by hip height");
    check_near(written[2].get_position().y, 1.0f, 1e-4f, "the spine keeps the target's length");
    check(std::fabs(std::fabs(math::dot(written[2].get_rotation(), math::identity<math::quat>())) - 1.0f) < 1e-4f,
          "a bone at its source rest stays at its target rest");

    // Without a recorded rest pose the clip falls back to copying local transforms.
    for(auto& channel : clip->channels)
    {
        channel.has_rest = false;
    }
    auto copy_clip = std::make_shared<animation_clip>(*clip);
    animation_player copy_player;
    copy_player.blend_to(0, make_clip_handle(copy_clip), seconds_t{0.0f}, true);
    copy_player.play();
    copy_player.update_time(seconds_t{0.1f});
    written.clear();
    copy_player.update_poses(empty_ref_pose,
                             binding,
                             false,
                             [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto&)
                             {
                                 written[desc.index] = transform;
                             });
    check_near(written[1].get_position().z, 1.0f, 1e-4f, "no rest pose: local transforms copied");
}

void test_rest_aligned_retargeting_carries_scaled_offsets()
{
    std::printf("test_rest_aligned_retargeting_carries_scaled_offsets\n");
    // The source of test_rest_aligned_retargeting in centimeters: the root that turns it upright
    // also scales it by 100, the way FBX armatures are exported, so the hips stand 0.01 local units
    // high. Offsets and root motion are lengths in that scaled frame: carried as directions only, a
    // crouch lowered the hips by a hundredth of the clip's drop and root motion barely moved.
    const auto rx = math::angleAxis(math::radians(-90.0f), math::vec3(1.0f, 0.0f, 0.0f));
    constexpr float centimeters = 100.0f;
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    const auto add_channel = [&](const std::string& name, int parent, const math::transform& rest,
                                 std::vector<animation_channel::key<math::vec3>> positions, const math::quat& rotation,
                                 float scale)
    {
        auto& channel = clip->channels.emplace_back();
        channel.node_name = name;
        channel.node_index = clip->channels.size() - 1;
        channel.position_keys = std::move(positions);
        channel.rotation_keys = {{seconds_t{0.0f}, rotation}};
        channel.scaling_keys = {{seconds_t{0.0f}, math::vec3(scale)}};
        channel.has_rest = true;
        channel.rest_local = rest;
        channel.parent_channel = parent;
    };
    math::transform root_rest;
    root_rest.set_rotation(rx);
    root_rest.set_scale(math::vec3(centimeters));
    math::transform hips_rest;
    hips_rest.set_position({0.0f, 0.0f, 0.01f});
    math::transform spine_rest;
    spine_rest.set_position({0.0f, 0.0f, 0.005f});
    add_channel("root", -1, root_rest, {{seconds_t{0.0f}, {0.0f, 0.0f, 0.0f}}}, rx, centimeters);
    // The hips drop to half height (local -Z is down) and travel 1 m forward (local -Y) over the clip.
    add_channel("hips", 0, hips_rest, {{seconds_t{0.0f}, {0.0f, 0.0f, 0.01f}}, {seconds_t{1.0f}, {0.0f, -0.01f, 0.005f}}},
                math::identity<math::quat>(), 1.0f);
    add_channel("spine", 1, spine_rest, {{seconds_t{0.0f}, {0.0f, 0.0f, 0.005f}}}, math::identity<math::quat>(), 1.0f);
    clip->root_motion.position_node_name = "hips";
    clip->root_motion.keep_position_y = true;

    // Target hips stand 2 m high: offsets scale by 2.
    auto target = std::make_shared<animation_target_skeleton>();
    target->parents = {-1, 0, 1};
    target->bind_local.resize(3);
    target->bind_local[1].set_position({0.0f, 2.0f, 0.0f});
    target->bind_local[2].set_position({0.0f, 1.0f, 0.0f});
    target->compute_rest_globals();
    animation_skeleton_binding binding;
    binding.version = 5;
    binding.resolve = [](const animation_channel& channel)
    {
        return channel.node_name == "root" ? 0 : channel.node_name == "hips" ? 1 : 2;
    };
    binding.rest_aligned_target = target;

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, false);
    player.play();
    std::map<size_t, math::transform> written;
    animation_pose::root_motion_result motion;
    const auto step = [&](float dt)
    {
        static const animation_pose empty_ref_pose{};
        player.update_time(seconds_t{dt});
        player.update_poses(empty_ref_pose,
                            binding,
                            true,
                            [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto& result)
                            {
                                written[desc.index] = transform;
                                motion = result;
                            });
    };
    step(0.25f);
    step(0.5f);
    // At 0.75 the source hips are 0.625 of their 1 m height: 1.25 m on the 2 m target.
    check_near(written[1].get_position().y, 1.25f, 1e-3f, "the drop is carried at the source's scale");
    check_near(written[1].get_position().z, 0.0f, 1e-3f, "extracted travel stays out of the pose");
    // 0.25 to 0.75 of 1 m forward, on a skeleton twice as tall.
    check_near(motion.root_transform_delta.get_position().z, 1.0f, 1e-3f, "root motion is carried at the source's scale");
}

/// A skeleton as names, parents and rest positions, built bone by bone.
struct test_skeleton
{
    std::vector<std::string> names;
    std::vector<int> parents;
    std::vector<math::vec3> positions;

    auto add(const std::string& name, const std::string& parent, const math::vec3& position) -> int
    {
        parents.push_back(index(parent));
        names.push_back(name);
        positions.push_back(position);
        return int(names.size()) - 1;
    }

    auto index(const std::string& name) const -> int
    {
        const auto it = std::find(names.begin(), names.end(), name);
        return it == names.end() ? -1 : int(it - names.begin());
    }
};

/// The UE mannequin layout in a T-pose, with an IK helper chain under the root.
auto make_ue_skeleton() -> test_skeleton
{
    test_skeleton s;
    s.add("root", "", {0.0f, 0.0f, 0.0f});
    s.add("pelvis", "root", {0.0f, 0.95f, 0.0f});
    s.add("spine_01", "pelvis", {0.0f, 1.05f, 0.0f});
    s.add("spine_02", "spine_01", {0.0f, 1.2f, 0.0f});
    s.add("spine_03", "spine_02", {0.0f, 1.35f, 0.0f});
    s.add("neck_01", "spine_03", {0.0f, 1.5f, 0.0f});
    s.add("Head", "neck_01", {0.0f, 1.6f, 0.0f});
    for(const auto& [side, x] : std::vector<std::pair<std::string, float>>{{"l", 1.0f}, {"r", -1.0f}})
    {
        // The collarbone starts in front of the spine, as on the female mannequin.
        s.add("clavicle_" + side, "spine_03", {0.02f * x, 1.46f, 0.07f});
        s.add("upperarm_" + side, "clavicle_" + side, {0.18f * x, 1.45f, 0.0f});
        s.add("upperarm_twist_01_" + side, "upperarm_" + side, {0.3f * x, 1.45f, 0.0f});
        s.add("lowerarm_" + side, "upperarm_" + side, {0.45f * x, 1.45f, 0.0f});
        s.add("hand_" + side, "lowerarm_" + side, {0.72f * x, 1.45f, 0.0f});
        s.add("middle_01_" + side, "hand_" + side, {0.8f * x, 1.45f, 0.0f});
        s.add("thigh_" + side, "pelvis", {0.09f * x, 0.93f, 0.0f});
        s.add("calf_" + side, "thigh_" + side, {0.09f * x, 0.52f, 0.0f});
        s.add("foot_" + side, "calf_" + side, {0.09f * x, 0.1f, 0.0f});
        s.add("ball_" + side, "foot_" + side, {0.09f * x, 0.02f, 0.12f});
    }
    s.add("ik_foot_root", "root", {0.0f, 0.0f, 0.0f});
    s.add("ik_foot_l", "ik_foot_root", {0.09f, 0.1f, 0.0f});
    s.add("ik_foot_r", "ik_foot_root", {-0.09f, 0.1f, 0.0f});
    return s;
}

/// A chibi rig named its own way: no shoulders or neck, two spine bones, a wrist before the hand,
/// and IK helper bones hanging from the root.
auto make_chibi_skeleton() -> test_skeleton
{
    test_skeleton s;
    s.add("root", "", {0.0f, 0.0f, 0.0f});
    s.add("hips", "root", {0.0f, 0.41f, 0.0f});
    s.add("spine", "hips", {0.0f, 0.6f, 0.0f});
    s.add("chest", "spine", {0.0f, 0.97f, 0.0f});
    s.add("head", "chest", {0.0f, 1.24f, 0.0f});
    // A helmet mesh hangs from the head bone, just below it.
    s.add("Knight_Helmet", "head", {0.0f, 1.215f, 0.0f});
    for(const auto& [side, x] : std::vector<std::pair<std::string, float>>{{"l", 1.0f}, {"r", -1.0f}})
    {
        s.add("upperarm." + side, "chest", {0.21f * x, 1.11f, 0.0f});
        s.add("lowerarm." + side, "upperarm." + side, {0.45f * x, 1.11f, 0.0f});
        s.add("wrist." + side, "lowerarm." + side, {0.71f * x, 1.11f, 0.0f});
        s.add("hand." + side, "wrist." + side, {0.78f * x, 1.11f, 0.0f});
        s.add("upperleg." + side, "hips", {0.17f * x, 0.52f, 0.0f});
        s.add("lowerleg." + side, "upperleg." + side, {0.17f * x, 0.29f, 0.0f});
        s.add("foot." + side, "lowerleg." + side, {0.17f * x, 0.15f, 0.0f});
        s.add("toes." + side, "foot." + side, {0.17f * x, 0.03f, 0.1f});
        s.add("kneeIK." + side, "root", {0.17f * x, 0.29f, 0.58f});
        s.add("handIK." + side, "root", {0.71f * x, 1.11f, 0.0f});
    }
    return s;
}

void test_humanoid_roles_from_structure()
{
    std::printf("test_humanoid_roles_from_structure\n");
    const auto check_role = [](const test_skeleton& s, const humanoid_map& map, humanoid_bone role, const char* expected)
    {
        const int bone = map[size_t(role)];
        const std::string actual = bone >= 0 ? s.names[size_t(bone)] : std::string("(none)");
        check(actual == expected, std::string(get_humanoid_bone_name(role)) + " is " + expected + ", got " + actual);
    };
    const auto ue = make_ue_skeleton();
    const auto ue_map = find_humanoid_bones(ue.names, ue.parents, ue.positions);
    check_role(ue, ue_map, humanoid_bone::hips, "pelvis");
    check_role(ue, ue_map, humanoid_bone::spine, "spine_01");
    check_role(ue, ue_map, humanoid_bone::chest, "spine_02");
    check_role(ue, ue_map, humanoid_bone::upper_chest, "spine_03");
    check_role(ue, ue_map, humanoid_bone::neck, "neck_01");
    check_role(ue, ue_map, humanoid_bone::head, "Head");
    check_role(ue, ue_map, humanoid_bone::left_shoulder, "clavicle_l");
    check_role(ue, ue_map, humanoid_bone::left_upper_arm, "upperarm_l");
    check_role(ue, ue_map, humanoid_bone::left_lower_arm, "lowerarm_l");
    check_role(ue, ue_map, humanoid_bone::left_hand, "hand_l");
    check_role(ue, ue_map, humanoid_bone::right_hand, "hand_r");
    check_role(ue, ue_map, humanoid_bone::left_upper_leg, "thigh_l");
    check_role(ue, ue_map, humanoid_bone::right_foot, "foot_r");
    check_role(ue, ue_map, humanoid_bone::left_toes, "ball_l");

    const auto chibi = make_chibi_skeleton();
    const auto chibi_map = find_humanoid_bones(chibi.names, chibi.parents, chibi.positions);
    check_role(chibi, chibi_map, humanoid_bone::hips, "hips");
    // A root bone a little above the lowest bone still has the legs below it, through the hips:
    // the hips are the ones standing at hip height.
    auto raised_root = chibi;
    raised_root.positions.front().y = 0.05f;
    const auto raised_map = find_humanoid_bones(raised_root.names, raised_root.parents, raised_root.positions);
    check_role(raised_root, raised_map, humanoid_bone::hips, "hips");
    check_role(raised_root, raised_map, humanoid_bone::spine, "spine");
    check_role(chibi, chibi_map, humanoid_bone::spine, "spine");
    check_role(chibi, chibi_map, humanoid_bone::chest, "(none)");
    check_role(chibi, chibi_map, humanoid_bone::upper_chest, "chest");
    check_role(chibi, chibi_map, humanoid_bone::neck, "(none)");
    check_role(chibi, chibi_map, humanoid_bone::head, "head");
    check_role(chibi, chibi_map, humanoid_bone::left_shoulder, "(none)");
    check_role(chibi, chibi_map, humanoid_bone::left_upper_arm, "upperarm.l");
    check_role(chibi, chibi_map, humanoid_bone::left_hand, "wrist.l");
    check_role(chibi, chibi_map, humanoid_bone::right_lower_arm, "lowerarm.r");
    check_role(chibi, chibi_map, humanoid_bone::right_upper_leg, "upperleg.r");
    check_role(chibi, chibi_map, humanoid_bone::left_toes, "toes.l");

    // Mirrored names win over positions: a rig facing the other way keeps its sides.
    auto mirrored = chibi;
    for(auto& position : mirrored.positions)
    {
        position.x = -position.x;
    }
    const auto mirrored_map = find_humanoid_bones(mirrored.names, mirrored.parents, mirrored.positions);
    check_role(mirrored, mirrored_map, humanoid_bone::left_upper_arm, "upperarm.l");

    test_skeleton prop;
    prop.add("base", "", {0.0f, 0.0f, 0.0f});
    prop.add("lid", "base", {0.0f, 0.5f, 0.0f});
    check(count_humanoid_bones(find_humanoid_bones(prop.names, prop.parents, prop.positions)) == 0,
          "a skeleton without legs and spine is not a humanoid");

    // Arbitrary hierarchies, including parents that do not come first, never hang or index out of
    // range, and every role found is a bone of the skeleton.
    std::mt19937 rng(77);
    std::uniform_real_distribution<float> coordinate(-1.0f, 2.0f);
    bool in_range = true;
    for(int round = 0; round < 300; ++round)
    {
        const size_t count = 1 + size_t(rng() % 60);
        std::vector<std::string> names(count);
        std::vector<int> parents(count);
        std::vector<math::vec3> positions(count);
        for(size_t i = 0; i < count; ++i)
        {
            names[i] = (rng() % 2 ? "l_" : "r_") + std::to_string(i);
            parents[i] = int(rng() % (count + 2)) - 1;
            positions[i] = {coordinate(rng), coordinate(rng), coordinate(rng)};
        }
        for(int bone : find_humanoid_bones(names, parents, positions))
        {
            in_range = in_range && bone >= -1 && bone < int(count);
        }
    }
    check(in_range, "random hierarchies give roles inside the skeleton");
}

/// One bone of a rig description: "{s}" in a name stands for the side token, x mirrors for the right.
struct rig_bone
{
    std::string name;
    std::string parent;
    math::vec3 position;
};

auto expand_side(std::string text, const std::string& side) -> std::string
{
    const auto at = text.find("{s}");
    return at == std::string::npos ? text : text.replace(at, 3, side);
}

auto make_rig(const std::vector<rig_bone>& center, const std::vector<rig_bone>& sided, const std::string& left,
              const std::string& right) -> test_skeleton
{
    test_skeleton s;
    for(const auto& bone : center)
    {
        s.add(bone.name, bone.parent, bone.position);
    }
    for(const auto& [side, mirror] : std::vector<std::pair<std::string, float>>{{left, 1.0f}, {right, -1.0f}})
    {
        for(const auto& bone : sided)
        {
            s.add(expand_side(bone.name, side),
                  expand_side(bone.parent, side),
                  {bone.position.x * mirror, bone.position.y, bone.position.z});
        }
    }
    return s;
}

void test_humanoid_roles_of_common_rigs()
{
    std::printf("test_humanoid_roles_of_common_rigs\n");
    // The layouts of widely used rigs, with their real bone names, in a T-pose. Each must find its
    // roles without a name table: Mixamo's prefixed names and end bones, Rigify's split twist
    // segments, Biped's arms on the neck and thighs on the spine (or on a triangle pelvis), VRM,
    // Character Creator's pelvis bone between hips and thighs, Daz's twist joints inside the limbs
    // and side-less names, the UE5 mannequin's five spine bones.
    struct rig_case
    {
        const char* label;
        test_skeleton skeleton;
        std::vector<std::pair<humanoid_bone, std::string>> expected;
    };
    std::vector<rig_case> cases;

    cases.push_back({"Mixamo",
                     make_rig({{"mixamorig:Hips", "", {0.0f, 1.0f, 0.0f}},
                               {"mixamorig:Spine", "mixamorig:Hips", {0.0f, 1.1f, 0.0f}},
                               {"mixamorig:Spine1", "mixamorig:Spine", {0.0f, 1.22f, 0.0f}},
                               {"mixamorig:Spine2", "mixamorig:Spine1", {0.0f, 1.36f, 0.0f}},
                               {"mixamorig:Neck", "mixamorig:Spine2", {0.0f, 1.5f, 0.0f}},
                               {"mixamorig:Head", "mixamorig:Neck", {0.0f, 1.6f, 0.0f}},
                               {"mixamorig:HeadTop_End", "mixamorig:Head", {0.0f, 1.78f, 0.0f}}},
                              {{"mixamorig:{s}Shoulder", "mixamorig:Spine2", {0.06f, 1.45f, 0.0f}},
                               {"mixamorig:{s}Arm", "mixamorig:{s}Shoulder", {0.18f, 1.45f, 0.0f}},
                               {"mixamorig:{s}ForeArm", "mixamorig:{s}Arm", {0.45f, 1.45f, 0.0f}},
                               {"mixamorig:{s}Hand", "mixamorig:{s}ForeArm", {0.72f, 1.45f, 0.0f}},
                               {"mixamorig:{s}HandIndex1", "mixamorig:{s}Hand", {0.8f, 1.45f, 0.0f}},
                               {"mixamorig:{s}UpLeg", "mixamorig:Hips", {0.09f, 0.95f, 0.0f}},
                               {"mixamorig:{s}Leg", "mixamorig:{s}UpLeg", {0.09f, 0.52f, 0.0f}},
                               {"mixamorig:{s}Foot", "mixamorig:{s}Leg", {0.09f, 0.1f, 0.0f}},
                               {"mixamorig:{s}ToeBase", "mixamorig:{s}Foot", {0.09f, 0.02f, 0.1f}},
                               {"mixamorig:{s}Toe_End", "mixamorig:{s}ToeBase", {0.09f, 0.02f, 0.18f}}},
                              "Left",
                              "Right"),
                     {{humanoid_bone::hips, "mixamorig:Hips"},
                      {humanoid_bone::spine, "mixamorig:Spine"},
                      {humanoid_bone::chest, "mixamorig:Spine1"},
                      {humanoid_bone::upper_chest, "mixamorig:Spine2"},
                      {humanoid_bone::neck, "mixamorig:Neck"},
                      {humanoid_bone::head, "mixamorig:Head"},
                      {humanoid_bone::left_shoulder, "mixamorig:LeftShoulder"},
                      {humanoid_bone::left_upper_arm, "mixamorig:LeftArm"},
                      {humanoid_bone::left_lower_arm, "mixamorig:LeftForeArm"},
                      {humanoid_bone::left_hand, "mixamorig:LeftHand"},
                      {humanoid_bone::right_hand, "mixamorig:RightHand"},
                      {humanoid_bone::left_upper_leg, "mixamorig:LeftUpLeg"},
                      {humanoid_bone::left_lower_leg, "mixamorig:LeftLeg"},
                      {humanoid_bone::left_foot, "mixamorig:LeftFoot"},
                      {humanoid_bone::left_toes, "mixamorig:LeftToeBase"}}});

    cases.push_back({"Rigify",
                     make_rig({{"DEF-spine", "", {0.0f, 1.0f, 0.0f}},
                               {"DEF-spine.001", "DEF-spine", {0.0f, 1.1f, 0.0f}},
                               {"DEF-spine.002", "DEF-spine.001", {0.0f, 1.22f, 0.0f}},
                               {"DEF-spine.003", "DEF-spine.002", {0.0f, 1.36f, 0.0f}},
                               {"DEF-spine.004", "DEF-spine.003", {0.0f, 1.5f, 0.0f}},
                               {"DEF-spine.005", "DEF-spine.004", {0.0f, 1.55f, 0.0f}},
                               {"DEF-spine.006", "DEF-spine.005", {0.0f, 1.62f, 0.0f}}},
                              {{"DEF-shoulder.{s}", "DEF-spine.003", {0.03f, 1.45f, 0.0f}},
                               {"DEF-upper_arm.{s}", "DEF-shoulder.{s}", {0.18f, 1.45f, 0.0f}},
                               {"DEF-upper_arm.{s}.001", "DEF-upper_arm.{s}", {0.31f, 1.45f, 0.0f}},
                               {"DEF-forearm.{s}", "DEF-upper_arm.{s}.001", {0.45f, 1.45f, 0.0f}},
                               {"DEF-forearm.{s}.001", "DEF-forearm.{s}", {0.58f, 1.45f, 0.0f}},
                               {"DEF-hand.{s}", "DEF-forearm.{s}.001", {0.72f, 1.45f, 0.0f}},
                               {"DEF-thigh.{s}", "DEF-spine", {0.09f, 0.95f, 0.0f}},
                               {"DEF-thigh.{s}.001", "DEF-thigh.{s}", {0.09f, 0.74f, 0.0f}},
                               {"DEF-shin.{s}", "DEF-thigh.{s}.001", {0.09f, 0.52f, 0.0f}},
                               {"DEF-shin.{s}.001", "DEF-shin.{s}", {0.09f, 0.31f, 0.0f}},
                               {"DEF-foot.{s}", "DEF-shin.{s}.001", {0.09f, 0.1f, 0.0f}},
                               {"DEF-toe.{s}", "DEF-foot.{s}", {0.09f, 0.02f, 0.1f}}},
                              "L",
                              "R"),
                     {{humanoid_bone::hips, "DEF-spine"},
                      {humanoid_bone::spine, "DEF-spine.001"},
                      {humanoid_bone::chest, "DEF-spine.002"},
                      {humanoid_bone::upper_chest, "DEF-spine.003"},
                      {humanoid_bone::neck, "DEF-spine.004"},
                      {humanoid_bone::head, "DEF-spine.006"},
                      {humanoid_bone::left_shoulder, "DEF-shoulder.L"},
                      {humanoid_bone::left_upper_arm, "DEF-upper_arm.L"},
                      {humanoid_bone::left_lower_arm, "DEF-forearm.L"},
                      {humanoid_bone::left_hand, "DEF-hand.L"},
                      {humanoid_bone::right_lower_arm, "DEF-forearm.R"},
                      {humanoid_bone::left_upper_leg, "DEF-thigh.L"},
                      {humanoid_bone::left_lower_leg, "DEF-shin.L"},
                      {humanoid_bone::left_foot, "DEF-foot.L"},
                      {humanoid_bone::left_toes, "DEF-toe.L"}}});

    cases.push_back({"Biped",
                     make_rig({{"Bip01", "", {0.0f, 1.0f, 0.0f}},
                               {"Bip01 Pelvis", "Bip01", {0.0f, 1.0f, 0.0f}},
                               {"Bip01 Spine", "Bip01 Pelvis", {0.0f, 1.08f, 0.0f}},
                               {"Bip01 Spine1", "Bip01 Spine", {0.0f, 1.22f, 0.0f}},
                               {"Bip01 Spine2", "Bip01 Spine1", {0.0f, 1.36f, 0.0f}},
                               {"Bip01 Neck", "Bip01 Spine2", {0.0f, 1.48f, 0.0f}},
                               {"Bip01 Head", "Bip01 Neck", {0.0f, 1.58f, 0.0f}},
                               {"Bip01 HeadNub", "Bip01 Head", {0.0f, 1.78f, 0.0f}}},
                              {{"Bip01 {s} Thigh", "Bip01 Spine", {0.09f, 0.95f, 0.0f}},
                               {"Bip01 {s} Calf", "Bip01 {s} Thigh", {0.09f, 0.52f, 0.0f}},
                               {"Bip01 {s} Foot", "Bip01 {s} Calf", {0.09f, 0.1f, 0.0f}},
                               {"Bip01 {s} Toe0", "Bip01 {s} Foot", {0.09f, 0.02f, 0.1f}},
                               {"Bip01 {s} Clavicle", "Bip01 Neck", {0.04f, 1.46f, 0.0f}},
                               {"Bip01 {s} UpperArm", "Bip01 {s} Clavicle", {0.18f, 1.45f, 0.0f}},
                               {"Bip01 {s} Forearm", "Bip01 {s} UpperArm", {0.45f, 1.45f, 0.0f}},
                               {"Bip01 {s} Hand", "Bip01 {s} Forearm", {0.72f, 1.45f, 0.0f}}},
                              "L",
                              "R"),
                     // The hips are the bone the thighs hang from: Biped's first spine link. The
                     // Pelvis above it carries legs and spine alike, as a root does.
                     {{humanoid_bone::hips, "Bip01 Spine"},
                      {humanoid_bone::spine, "Bip01 Spine1"},
                      {humanoid_bone::chest, "(none)"},
                      {humanoid_bone::upper_chest, "Bip01 Spine2"},
                      {humanoid_bone::neck, "Bip01 Neck"},
                      {humanoid_bone::head, "Bip01 Head"},
                      {humanoid_bone::left_shoulder, "Bip01 L Clavicle"},
                      {humanoid_bone::left_upper_arm, "Bip01 L UpperArm"},
                      {humanoid_bone::left_hand, "Bip01 L Hand"},
                      {humanoid_bone::right_upper_arm, "Bip01 R UpperArm"},
                      {humanoid_bone::left_upper_leg, "Bip01 L Thigh"},
                      {humanoid_bone::left_toes, "Bip01 L Toe0"}}});

    cases.push_back({"Biped triangle pelvis",
                     make_rig({{"Bip01", "", {0.0f, 1.0f, 0.0f}},
                               {"Bip01 Pelvis", "Bip01", {0.0f, 1.0f, 0.0f}},
                               {"Bip01 Spine", "Bip01 Pelvis", {0.0f, 1.08f, 0.0f}},
                               {"Bip01 Spine1", "Bip01 Spine", {0.0f, 1.22f, 0.0f}},
                               {"Bip01 Spine2", "Bip01 Spine1", {0.0f, 1.36f, 0.0f}},
                               {"Bip01 Neck", "Bip01 Spine2", {0.0f, 1.48f, 0.0f}},
                               {"Bip01 Head", "Bip01 Neck", {0.0f, 1.58f, 0.0f}}},
                              {{"Bip01 {s} Thigh", "Bip01 Pelvis", {0.09f, 0.95f, 0.0f}},
                               {"Bip01 {s} Calf", "Bip01 {s} Thigh", {0.09f, 0.52f, 0.0f}},
                               {"Bip01 {s} Foot", "Bip01 {s} Calf", {0.09f, 0.1f, 0.0f}},
                               {"Bip01 {s} Toe0", "Bip01 {s} Foot", {0.09f, 0.02f, 0.1f}},
                               {"Bip01 {s} Clavicle", "Bip01 Neck", {0.04f, 1.46f, 0.0f}},
                               {"Bip01 {s} UpperArm", "Bip01 {s} Clavicle", {0.18f, 1.45f, 0.0f}},
                               {"Bip01 {s} Forearm", "Bip01 {s} UpperArm", {0.45f, 1.45f, 0.0f}},
                               {"Bip01 {s} Hand", "Bip01 {s} Forearm", {0.72f, 1.45f, 0.0f}}},
                              "L",
                              "R"),
                     // The root above the pelvis holds the legs only through it.
                     {{humanoid_bone::hips, "Bip01 Pelvis"},
                      {humanoid_bone::spine, "Bip01 Spine"},
                      {humanoid_bone::upper_chest, "Bip01 Spine2"},
                      {humanoid_bone::left_upper_leg, "Bip01 L Thigh"}}});

    cases.push_back({"VRM",
                     make_rig({{"J_Bip_C_Hips", "", {0.0f, 1.0f, 0.0f}},
                               {"J_Bip_C_Spine", "J_Bip_C_Hips", {0.0f, 1.1f, 0.0f}},
                               {"J_Bip_C_Chest", "J_Bip_C_Spine", {0.0f, 1.22f, 0.0f}},
                               {"J_Bip_C_UpperChest", "J_Bip_C_Chest", {0.0f, 1.36f, 0.0f}},
                               {"J_Bip_C_Neck", "J_Bip_C_UpperChest", {0.0f, 1.5f, 0.0f}},
                               {"J_Bip_C_Head", "J_Bip_C_Neck", {0.0f, 1.6f, 0.0f}}},
                              {{"J_Bip_{s}_Shoulder", "J_Bip_C_UpperChest", {0.04f, 1.45f, 0.0f}},
                               {"J_Bip_{s}_UpperArm", "J_Bip_{s}_Shoulder", {0.18f, 1.45f, 0.0f}},
                               {"J_Bip_{s}_LowerArm", "J_Bip_{s}_UpperArm", {0.45f, 1.45f, 0.0f}},
                               {"J_Bip_{s}_Hand", "J_Bip_{s}_LowerArm", {0.72f, 1.45f, 0.0f}},
                               {"J_Bip_{s}_UpperLeg", "J_Bip_C_Hips", {0.09f, 0.95f, 0.0f}},
                               {"J_Bip_{s}_LowerLeg", "J_Bip_{s}_UpperLeg", {0.09f, 0.52f, 0.0f}},
                               {"J_Bip_{s}_Foot", "J_Bip_{s}_LowerLeg", {0.09f, 0.1f, 0.0f}},
                               {"J_Bip_{s}_ToeBase", "J_Bip_{s}_Foot", {0.09f, 0.02f, 0.1f}}},
                              "L",
                              "R"),
                     {{humanoid_bone::hips, "J_Bip_C_Hips"},
                      {humanoid_bone::chest, "J_Bip_C_Chest"},
                      {humanoid_bone::upper_chest, "J_Bip_C_UpperChest"},
                      {humanoid_bone::neck, "J_Bip_C_Neck"},
                      {humanoid_bone::head, "J_Bip_C_Head"},
                      {humanoid_bone::left_shoulder, "J_Bip_L_Shoulder"},
                      {humanoid_bone::left_lower_arm, "J_Bip_L_LowerArm"},
                      {humanoid_bone::right_hand, "J_Bip_R_Hand"},
                      {humanoid_bone::left_foot, "J_Bip_L_Foot"}}});

    cases.push_back({"Character Creator",
                     make_rig({{"CC_Base_BoneRoot", "", {0.0f, 0.0f, 0.0f}},
                               {"CC_Base_Hip", "CC_Base_BoneRoot", {0.0f, 1.0f, 0.0f}},
                               {"CC_Base_Pelvis", "CC_Base_Hip", {0.0f, 0.98f, 0.0f}},
                               {"CC_Base_Waist", "CC_Base_Hip", {0.0f, 1.08f, 0.0f}},
                               {"CC_Base_Spine01", "CC_Base_Waist", {0.0f, 1.22f, 0.0f}},
                               {"CC_Base_Spine02", "CC_Base_Spine01", {0.0f, 1.36f, 0.0f}},
                               {"CC_Base_NeckTwist01", "CC_Base_Spine02", {0.0f, 1.5f, 0.0f}},
                               {"CC_Base_NeckTwist02", "CC_Base_NeckTwist01", {0.0f, 1.55f, 0.0f}},
                               {"CC_Base_Head", "CC_Base_NeckTwist02", {0.0f, 1.62f, 0.0f}}},
                              {{"CC_Base_{s}_Thigh", "CC_Base_Pelvis", {0.09f, 0.95f, 0.0f}},
                               {"CC_Base_{s}_Calf", "CC_Base_{s}_Thigh", {0.09f, 0.52f, 0.0f}},
                               {"CC_Base_{s}_Foot", "CC_Base_{s}_Calf", {0.09f, 0.1f, 0.0f}},
                               {"CC_Base_{s}_ToeBase", "CC_Base_{s}_Foot", {0.09f, 0.02f, 0.1f}},
                               {"CC_Base_{s}_Clavicle", "CC_Base_Spine02", {0.04f, 1.45f, 0.0f}},
                               {"CC_Base_{s}_Upperarm", "CC_Base_{s}_Clavicle", {0.18f, 1.45f, 0.0f}},
                               {"CC_Base_{s}_UpperarmTwist01", "CC_Base_{s}_Upperarm", {0.3f, 1.45f, 0.0f}},
                               {"CC_Base_{s}_Forearm", "CC_Base_{s}_Upperarm", {0.45f, 1.45f, 0.0f}},
                               {"CC_Base_{s}_Hand", "CC_Base_{s}_Forearm", {0.72f, 1.45f, 0.0f}},
                               {"CC_Base_{s}_Mid1", "CC_Base_{s}_Hand", {0.8f, 1.45f, 0.0f}}},
                              "L",
                              "R"),
                     {{humanoid_bone::hips, "CC_Base_Hip"},
                      {humanoid_bone::spine, "CC_Base_Waist"},
                      {humanoid_bone::chest, "CC_Base_Spine01"},
                      {humanoid_bone::upper_chest, "CC_Base_Spine02"},
                      {humanoid_bone::neck, "CC_Base_NeckTwist01"},
                      {humanoid_bone::head, "CC_Base_Head"},
                      {humanoid_bone::left_shoulder, "CC_Base_L_Clavicle"},
                      {humanoid_bone::left_lower_arm, "CC_Base_L_Forearm"},
                      {humanoid_bone::left_upper_leg, "CC_Base_L_Thigh"},
                      {humanoid_bone::right_foot, "CC_Base_R_Foot"}}});

    // Daz Genesis: twist joints split the upper arm, forearm and thigh, and the side is a lowercase
    // prefix no token rule reads, so the toes decide it.
    const std::vector<rig_bone> daz_center{{"hip", "", {0.0f, 1.0f, 0.0f}},
                                           {"pelvis", "hip", {0.0f, 0.98f, 0.0f}},
                                           {"abdomenLower", "hip", {0.0f, 1.08f, 0.0f}},
                                           {"abdomenUpper", "abdomenLower", {0.0f, 1.18f, 0.0f}},
                                           {"chestLower", "abdomenUpper", {0.0f, 1.28f, 0.0f}},
                                           {"chestUpper", "chestLower", {0.0f, 1.38f, 0.0f}},
                                           {"neckLower", "chestUpper", {0.0f, 1.5f, 0.0f}},
                                           {"neckUpper", "neckLower", {0.0f, 1.55f, 0.0f}},
                                           {"head", "neckUpper", {0.0f, 1.62f, 0.0f}}};
    const std::vector<rig_bone> daz_sided{{"{s}ThighBend", "pelvis", {0.09f, 0.95f, 0.0f}},
                                          {"{s}ThighTwist", "{s}ThighBend", {0.09f, 0.74f, 0.0f}},
                                          {"{s}Shin", "{s}ThighTwist", {0.09f, 0.52f, 0.0f}},
                                          {"{s}Foot", "{s}Shin", {0.09f, 0.1f, 0.0f}},
                                          {"{s}Metatarsals", "{s}Foot", {0.09f, 0.04f, 0.06f}},
                                          {"{s}Toe", "{s}Metatarsals", {0.09f, 0.02f, 0.12f}},
                                          {"{s}Collar", "chestUpper", {0.04f, 1.45f, 0.0f}},
                                          {"{s}ShldrBend", "{s}Collar", {0.18f, 1.45f, 0.0f}},
                                          {"{s}ShldrTwist", "{s}ShldrBend", {0.3f, 1.45f, 0.0f}},
                                          {"{s}ForearmBend", "{s}ShldrTwist", {0.45f, 1.45f, 0.0f}},
                                          {"{s}ForearmTwist", "{s}ForearmBend", {0.58f, 1.45f, 0.0f}},
                                          {"{s}Hand", "{s}ForearmTwist", {0.72f, 1.45f, 0.0f}},
                                          {"{s}Mid1", "{s}Hand", {0.8f, 1.45f, 0.0f}}};
    const std::vector<std::pair<humanoid_bone, std::string>> daz_roles{{humanoid_bone::hips, "hip"},
                                                                       {humanoid_bone::spine, "abdomenLower"},
                                                                       {humanoid_bone::chest, "chestLower"},
                                                                       {humanoid_bone::upper_chest, "chestUpper"},
                                                                       {humanoid_bone::neck, "neckLower"},
                                                                       {humanoid_bone::head, "head"},
                                                                       {humanoid_bone::left_shoulder, "lCollar"},
                                                                       {humanoid_bone::left_upper_arm, "lShldrBend"},
                                                                       {humanoid_bone::left_lower_arm, "lForearmBend"},
                                                                       {humanoid_bone::left_hand, "lHand"},
                                                                       {humanoid_bone::right_lower_arm, "rForearmBend"},
                                                                       {humanoid_bone::left_upper_leg, "lThighBend"},
                                                                       {humanoid_bone::left_lower_leg, "lShin"},
                                                                       {humanoid_bone::left_foot, "lFoot"},
                                                                       {humanoid_bone::right_lower_leg, "rShin"}};
    cases.push_back({"Daz Genesis", make_rig(daz_center, daz_sided, "l", "r"), daz_roles});
    // The same rig facing -Z: its left is now -X, which the toes tell.
    auto turned_daz = make_rig(daz_center, daz_sided, "l", "r");
    for(auto& position : turned_daz.positions)
    {
        position.x = -position.x;
        position.z = -position.z;
    }
    cases.push_back({"Daz Genesis facing -Z", turned_daz, daz_roles});

    // A tail with more bones than a leg, hanging below the knees: the legs reach the floor.
    auto tailed = make_ue_skeleton();
    tailed.add("tail_01", "pelvis", {0.0f, 0.9f, -0.12f});
    for(int i = 2; i <= 8; ++i)
    {
        tailed.add("tail_0" + std::to_string(i), "tail_0" + std::to_string(i - 1), {0.0f, 0.9f - 0.08f * float(i - 1), -0.15f});
    }
    cases.push_back({"Tail beside the legs",
                     tailed,
                     {{humanoid_bone::left_upper_leg, "thigh_l"},
                      {humanoid_bone::right_upper_leg, "thigh_r"},
                      {humanoid_bone::left_foot, "foot_l"}}});

    cases.push_back({"UE5 mannequin",
                     make_rig({{"root", "", {0.0f, 0.0f, 0.0f}},
                               {"pelvis", "root", {0.0f, 1.0f, 0.0f}},
                               {"spine_01", "pelvis", {0.0f, 1.06f, 0.0f}},
                               {"spine_02", "spine_01", {0.0f, 1.14f, 0.0f}},
                               {"spine_03", "spine_02", {0.0f, 1.22f, 0.0f}},
                               {"spine_04", "spine_03", {0.0f, 1.3f, 0.0f}},
                               {"spine_05", "spine_04", {0.0f, 1.38f, 0.0f}},
                               {"neck_01", "spine_05", {0.0f, 1.5f, 0.0f}},
                               {"neck_02", "neck_01", {0.0f, 1.55f, 0.0f}},
                               {"head", "neck_02", {0.0f, 1.62f, 0.0f}}},
                              {{"clavicle_{s}", "spine_05", {0.03f, 1.45f, 0.0f}},
                               {"upperarm_{s}", "clavicle_{s}", {0.18f, 1.45f, 0.0f}},
                               {"lowerarm_{s}", "upperarm_{s}", {0.45f, 1.45f, 0.0f}},
                               {"hand_{s}", "lowerarm_{s}", {0.72f, 1.45f, 0.0f}},
                               {"thigh_{s}", "pelvis", {0.09f, 0.95f, 0.0f}},
                               {"calf_{s}", "thigh_{s}", {0.09f, 0.52f, 0.0f}},
                               {"foot_{s}", "calf_{s}", {0.09f, 0.1f, 0.0f}},
                               {"ball_{s}", "foot_{s}", {0.09f, 0.02f, 0.1f}}},
                              "l",
                              "r"),
                     {{humanoid_bone::hips, "pelvis"},
                      {humanoid_bone::spine, "spine_01"},
                      {humanoid_bone::chest, "spine_04"},
                      {humanoid_bone::upper_chest, "spine_05"},
                      {humanoid_bone::neck, "neck_01"},
                      {humanoid_bone::head, "head"},
                      {humanoid_bone::left_shoulder, "clavicle_l"},
                      {humanoid_bone::right_upper_arm, "upperarm_r"},
                      {humanoid_bone::left_toes, "ball_l"}}});

    // The female UE mannequin's own rest joints: a collarbone that starts 7 cm in front of the spine and
    // is nearly as long as the upper arm. Only its left-right span tells it from an upper arm.
    cases.push_back({"UE mannequin F",
                     make_rig({{"root", "", {0.0f, 0.0f, 0.0f}},
                               {"pelvis", "root", {0.0f, 0.92f, -0.04f}},
                               {"spine_01", "pelvis", {0.0f, 1.02f, -0.03f}},
                               {"spine_02", "spine_01", {0.0f, 1.16f, -0.02f}},
                               {"spine_03", "spine_02", {0.0f, 1.315f, -0.005f}},
                               {"neck_01", "spine_03", {0.0f, 1.5f, 0.0f}},
                               {"Head", "neck_01", {0.0f, 1.58f, 0.01f}}},
                              {{"clavicle_{s}", "spine_03", {0.019f, 1.458f, 0.071f}},
                               {"upperarm_{s}", "clavicle_{s}", {0.152f, 1.441f, -0.065f}},
                               {"lowerarm_{s}", "upperarm_{s}", {0.373f, 1.441f, -0.07f}},
                               {"hand_{s}", "lowerarm_{s}", {0.645f, 1.441f, -0.065f}},
                               {"thigh_{s}", "pelvis", {0.09f, 0.9f, 0.0f}},
                               {"calf_{s}", "thigh_{s}", {0.09f, 0.5f, 0.0f}},
                               {"foot_{s}", "calf_{s}", {0.09f, 0.1f, -0.03f}},
                               {"ball_{s}", "foot_{s}", {0.09f, 0.015f, 0.11f}}},
                              "l",
                              "r"),
                     {{humanoid_bone::left_shoulder, "clavicle_l"},
                      {humanoid_bone::left_upper_arm, "upperarm_l"},
                      {humanoid_bone::left_lower_arm, "lowerarm_l"},
                      {humanoid_bone::left_hand, "hand_l"},
                      {humanoid_bone::right_shoulder, "clavicle_r"}}});

    for(const auto& rig : cases)
    {
        const auto map = find_humanoid_bones(rig.skeleton.names, rig.skeleton.parents, rig.skeleton.positions);
        for(const auto& [role, expected] : rig.expected)
        {
            const int bone = map[size_t(role)];
            const std::string actual = bone >= 0 ? rig.skeleton.names[size_t(bone)] : std::string("(none)");
            check(actual == expected,
                  std::string(rig.label) + ": " + get_humanoid_bone_name(role) + " is " + expected + ", got " + actual);
        }
    }
}

void test_rest_aligned_matches_humanoid_roles()
{
    std::printf("test_rest_aligned_matches_humanoid_roles\n");
    // A UE-named clip on the chibi rig: no bone name matches but the roots, yet every humanoid bone
    // lands on its role. The left upper arm drops 45 degrees in the clip, and so does the chibi's.
    const auto ue = make_ue_skeleton();
    const auto chibi = make_chibi_skeleton();
    const math::quat drop = math::angleAxis(math::radians(-45.0f), math::vec3(0.0f, 0.0f, 1.0f));

    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    for(size_t i = 0; i < ue.names.size(); ++i)
    {
        const int parent = ue.parents[i];
        auto& channel = clip->channels.emplace_back();
        channel.node_name = ue.names[i];
        channel.node_index = i;
        channel.has_rest = true;
        channel.parent_channel = parent;
        channel.rest_local.set_position(ue.positions[i] - (parent >= 0 ? ue.positions[size_t(parent)] : math::vec3(0.0f)));
        channel.position_keys = {{seconds_t{0.0f}, channel.rest_local.get_position()}};
        channel.rotation_keys = {{seconds_t{0.0f}, ue.names[i] == "upperarm_l" ? drop : math::identity<math::quat>()}};
        channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    }

    auto target = std::make_shared<animation_target_skeleton>();
    target->names = chibi.names;
    target->parents = chibi.parents;
    for(size_t i = 0; i < chibi.names.size(); ++i)
    {
        const int parent = chibi.parents[i];
        auto& local = target->bind_local.emplace_back();
        local.set_position(chibi.positions[i] - (parent >= 0 ? chibi.positions[size_t(parent)] : math::vec3(0.0f)));
    }
    target->compute_rest_globals();
    target->compute_humanoid_bones();

    animation_skeleton_binding binding;
    binding.version = 11;
    binding.resolve = [&](const animation_channel& channel)
    {
        return chibi.index(channel.node_name);
    };
    binding.rest_aligned_target = target;

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();
    player.update_time(seconds_t{0.1f});
    std::map<size_t, math::transform> written;
    static const animation_pose empty_ref_pose{};
    player.update_poses(empty_ref_pose,
                        binding,
                        false,
                        [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto&)
                        {
                            written[desc.index] = transform;
                        });
    for(const char* bone : {"hips", "spine", "chest", "head", "upperarm.l", "wrist.l", "upperleg.r", "toes.l"})
    {
        check(written.count(size_t(chibi.index(bone))) == 1, std::string(bone) + " is driven by its role");
    }
    check(written.count(size_t(chibi.index("kneeIK.l"))) == 0, "helper bones without a role stay untouched");
    const auto& arm = written[size_t(chibi.index("upperarm.l"))];
    check(std::fabs(std::fabs(math::dot(arm.get_rotation(), drop)) - 1.0f) < 1e-3f, "the chibi's arm drops like the clip's");
}

/// The Mixamo character layout in a T-pose, in meters, its bones named with `prefix` ("mixamorig:").
auto make_mixamo_layout(const std::string& prefix) -> test_skeleton
{
    const auto named = [&](const char* name)
    {
        return name[0] == '\0' ? std::string() : prefix + name;
    };
    std::vector<rig_bone> center;
    for(const auto& [name, parent, position] : std::vector<std::tuple<const char*, const char*, math::vec3>>{
            {"Hips", "", {0.0f, 1.0f, 0.0f}},
            {"Spine", "Hips", {0.0f, 1.1f, 0.0f}},
            {"Spine1", "Spine", {0.0f, 1.22f, 0.0f}},
            {"Spine2", "Spine1", {0.0f, 1.36f, 0.0f}},
            {"Neck", "Spine2", {0.0f, 1.5f, 0.0f}},
            {"Head", "Neck", {0.0f, 1.6f, 0.0f}},
            {"HeadTop_End", "Head", {0.0f, 1.78f, 0.0f}}})
    {
        center.push_back({named(name), named(parent), position});
    }
    std::vector<rig_bone> sided;
    for(const auto& [name, parent, position] : std::vector<std::tuple<const char*, const char*, math::vec3>>{
            {"{s}Shoulder", "Spine2", {0.06f, 1.45f, 0.0f}},
            {"{s}Arm", "{s}Shoulder", {0.18f, 1.45f, 0.0f}},
            {"{s}ForeArm", "{s}Arm", {0.45f, 1.45f, 0.0f}},
            {"{s}Hand", "{s}ForeArm", {0.72f, 1.45f, 0.0f}},
            {"{s}HandIndex1", "{s}Hand", {0.8f, 1.45f, 0.0f}},
            {"{s}UpLeg", "Hips", {0.09f, 0.95f, 0.0f}},
            {"{s}Leg", "{s}UpLeg", {0.09f, 0.52f, 0.0f}},
            {"{s}Foot", "{s}Leg", {0.09f, 0.1f, 0.0f}},
            {"{s}ToeBase", "{s}Foot", {0.09f, 0.02f, 0.1f}},
            {"{s}Toe_End", "{s}ToeBase", {0.09f, 0.02f, 0.18f}}})
    {
        sided.push_back({named(name), named(parent), position});
    }
    return make_rig(center, sided, "Left", "Right");
}

/// The turn that carries +Y onto `direction`.
auto turn_y_onto(const math::vec3& direction) -> math::quat
{
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    const math::vec3 to = math::normalize(direction);
    const float cosine = math::dot(up, to);
    if(cosine < -0.9999f)
    {
        return math::angleAxis(math::pi<float>(), math::vec3(0.0f, 0.0f, 1.0f));
    }
    const math::vec3 axis = math::cross(up, to);
    return math::normalize(math::quat(1.0f + cosine, axis.x, axis.y, axis.z));
}

/// A Mixamo character's skeleton from its layout: no root bone above the hips, and every bone
/// turned so its local +Y runs to its first child, as Mixamo exports them.
auto make_mixamo_target(const test_skeleton& layout) -> std::shared_ptr<animation_target_skeleton>
{
    const size_t count = layout.names.size();
    std::vector<math::quat> global(count, math::identity<math::quat>());
    for(size_t i = 0; i < count; ++i)
    {
        int child = -1;
        for(size_t j = i + 1; j < count && child < 0; ++j)
        {
            child = layout.parents[j] == int(i) ? int(j) : -1;
        }
        const int parent = layout.parents[i];
        global[i] = child >= 0 ? turn_y_onto(layout.positions[size_t(child)] - layout.positions[i])
                               : (parent >= 0 ? global[size_t(parent)] : math::identity<math::quat>());
    }
    auto target = std::make_shared<animation_target_skeleton>();
    target->names = layout.names;
    target->parents = layout.parents;
    for(size_t i = 0; i < count; ++i)
    {
        const int parent = layout.parents[i];
        const math::quat parent_rotation = parent >= 0 ? global[size_t(parent)] : math::identity<math::quat>();
        const math::vec3 parent_position = parent >= 0 ? layout.positions[size_t(parent)] : math::vec3(0.0f);
        auto& local = target->bind_local.emplace_back();
        local.set_position(glm::inverse(parent_rotation) * (layout.positions[i] - parent_position));
        local.set_rotation(math::normalize(glm::inverse(parent_rotation) * global[i]));
    }
    target->compute_rest_globals();
    target->compute_humanoid_bones();
    return target;
}

void test_ue_clip_on_a_mixamo_rig()
{
    std::printf("test_ue_clip_on_a_mixamo_rig\n");
    // A clip of the UE mannequin (a root bone on the floor, every bone at identity rest) drives a
    // Mixamo character, whose bones run along their local +Y and which has no root bone: the bones
    // match by role, the arm turns in the world as the clip's does whatever the bone axes, and the
    // clip root the character lacks moves it through its hips, scaled by hip height.
    const auto ue = make_ue_skeleton();
    const math::quat drop = math::angleAxis(math::radians(-45.0f), math::vec3(0.0f, 0.0f, 1.0f));
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    for(size_t i = 0; i < ue.names.size(); ++i)
    {
        const int parent = ue.parents[i];
        auto& channel = clip->channels.emplace_back();
        channel.node_name = ue.names[i];
        channel.node_index = i;
        channel.has_rest = true;
        channel.parent_channel = parent;
        channel.rest_local.set_position(ue.positions[i] - (parent >= 0 ? ue.positions[size_t(parent)] : math::vec3(0.0f)));
        channel.position_keys = {{seconds_t{0.0f}, channel.rest_local.get_position()}};
        if(ue.names[i] == "root")
        {
            channel.position_keys.push_back({seconds_t{1.0f}, {0.0f, 0.0f, 1.0f}});
        }
        channel.rotation_keys = {{seconds_t{0.0f}, ue.names[i] == "upperarm_l" ? drop : math::identity<math::quat>()}};
        channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    }
    clip->root_motion.position_node_name = "root";
    clip->root_motion.position_node_index = 0;
    clip->root_motion.rotation_node_name = "root";
    clip->root_motion.rotation_node_index = 0;

    const auto mixamo = make_mixamo_layout("mixamorig:");
    const auto target = make_mixamo_target(mixamo);
    animation_skeleton_binding binding;
    binding.version = 13;
    binding.resolve = [&](const animation_channel& channel)
    {
        return mixamo.index(channel.node_name);
    };
    binding.rest_aligned_target = target;

    animation_player player;
    player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, true);
    player.play();
    player.update_time(seconds_t{0.1f});
    std::map<size_t, math::transform> written;
    animation_pose::root_motion_result motion;
    static const animation_pose empty_ref_pose{};
    player.update_poses(empty_ref_pose,
                        binding,
                        true,
                        [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto& result)
                        {
                            written[desc.index] = transform;
                            motion = result;
                        });
    const auto get_global = [&](int bone)
    {
        math::transform global;
        for(; bone >= 0; bone = mixamo.parents[size_t(bone)])
        {
            const auto it = written.find(size_t(bone));
            global = (it != written.end() ? it->second : target->bind_local[size_t(bone)]) * global;
        }
        return global;
    };
    const int arm = mixamo.index("mixamorig:LeftArm");
    const math::quat turned = get_global(arm).get_rotation() * glm::inverse(target->rest_global[size_t(arm)].get_rotation());
    check(std::fabs(std::fabs(math::dot(turned, drop)) - 1.0f) < 1e-3f, "the Mixamo arm drops in the world like the clip's");
    check(motion.root_position_node_index == mixamo.index("mixamorig:Hips"), "the clip root moves the character through its hips");
    check_near(motion.root_transform_delta.get_position().z, 0.1f * 1.0f / 0.95f, 1e-3f,
               "the travel is scaled by hip height (1.0 against the clip's 0.95)");
}

void test_names_match_across_namespaces()
{
    std::printf("test_names_match_across_namespaces\n");
    check(get_name_without_namespace("mixamorig:Hips") == "Hips", "a Mixamo name drops its namespace");
    check(get_name_without_namespace("mixamorig1:Hips") == get_name_without_namespace("mixamorig:Hips"),
          "two characters' namespaces name the same bone");
    check(get_name_without_namespace("Armature:rig:Hips") == "Hips", "nested namespaces drop together");
    check(get_name_without_namespace("pelvis") == "pelvis", "a name without namespace stays");
}

/**
 * A clip of a source skeleton turned Z-up by its root (-90 degrees about X): the hips travel, bob
 * and yaw over the clip and the spine bends. With `twist_degrees` an unmapped channel sits between
 * hips and spine and turns that much over the clip.
 */
auto make_source_skeleton_clip(float travel, float bob, float yaw_degrees, float bend_degrees, std::optional<float> twist_degrees = {})
    -> std::shared_ptr<animation_clip>
{
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    const auto add_channel = [&](const std::string& name, int parent, const math::vec3& rest_position,
                                 const math::quat& rest_rotation, const std::vector<animation_channel::key<math::vec3>>& positions,
                                 const std::vector<animation_channel::key<math::quat>>& rotations)
    {
        auto& channel = clip->channels.emplace_back();
        channel.node_name = name;
        channel.node_index = clip->channels.size() - 1;
        channel.position_keys = positions;
        channel.rotation_keys = rotations;
        channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
        channel.has_rest = true;
        channel.rest_local.set_position(rest_position);
        channel.rest_local.set_rotation(rest_rotation);
        channel.parent_channel = parent;
    };
    const auto turn = [](float degrees, const math::vec3& axis)
    {
        return math::angleAxis(math::radians(degrees), axis);
    };
    const math::vec3 x_axis(1.0f, 0.0f, 0.0f);
    const math::vec3 z_axis(0.0f, 0.0f, 1.0f);
    const auto identity = math::identity<math::quat>();
    const auto rx = turn(-90.0f, x_axis);
    add_channel("root", -1, {}, rx, {{seconds_t{0.0f}, {}}}, {{seconds_t{0.0f}, rx}});
    add_channel("hips",
                0,
                {0.0f, 0.0f, 1.0f},
                identity,
                {{seconds_t{0.0f}, {0.0f, 0.0f, 1.0f}}, {seconds_t{1.0f}, {0.0f, travel, 1.0f + bob}}},
                {{seconds_t{0.0f}, identity}, {seconds_t{1.0f}, turn(yaw_degrees, z_axis)}});
    int spine_parent = 1;
    if(twist_degrees)
    {
        add_channel("twist",
                    1,
                    {0.0f, 0.0f, 0.25f},
                    identity,
                    {{seconds_t{0.0f}, {0.0f, 0.0f, 0.25f}}},
                    {{seconds_t{0.0f}, identity}, {seconds_t{1.0f}, turn(*twist_degrees, z_axis)}});
        spine_parent = 2;
    }
    add_channel("spine",
                spine_parent,
                {0.0f, 0.0f, 0.5f},
                identity,
                {{seconds_t{0.0f}, {0.0f, 0.0f, 0.5f}}},
                {{seconds_t{0.0f}, identity}, {seconds_t{1.0f}, turn(bend_degrees, x_axis)}});
    clip->root_motion.position_node_name = "hips";
    clip->root_motion.rotation_node_name = "hips";
    return clip;
}

/// The bones and the root-motion delta a player wrote on its last pose update.
struct retargeted_frame
{
    std::map<size_t, math::transform> nodes;
    animation_pose::root_motion_result motion;
};

/**
 * Plays a 1D blend of two clips of one source skeleton, rest aligned onto a taller Y-up target, and
 * the same clips alone; the blend must be the blend of the clips' own retargets, bones and root
 * motion alike.
 */
void check_blend_matches_blend_of_retargets(const std::shared_ptr<animation_clip>& clip_a,
                                            const std::shared_ptr<animation_clip>& clip_b,
                                            const char* label)
{
    auto target = std::make_shared<animation_target_skeleton>();
    target->parents = {-1, 0, 1};
    target->bind_local.resize(3);
    target->bind_local[1].set_position({0.0f, 2.0f, 0.0f});
    target->bind_local[2].set_position({0.0f, 1.0f, 0.0f});
    target->compute_rest_globals();
    animation_skeleton_binding binding;
    binding.version = 5;
    binding.resolve = [](const animation_channel& channel)
    {
        return channel.node_name == "root" ? 0 : channel.node_name == "hips" ? 1 : channel.node_name == "spine" ? 2 : -1;
    };
    binding.rest_aligned_target = target;

    constexpr float STEP = 0.25f;
    constexpr int STEPS = 2;
    constexpr float WEIGHT_B = 0.25f;
    const auto play = [&](animation_player& player)
    {
        retargeted_frame frame;
        static const animation_pose empty_ref_pose{};
        player.play();
        for(int i = 0; i < STEPS; ++i)
        {
            player.update_time(seconds_t{STEP});
            player.update_poses(empty_ref_pose,
                                binding,
                                true,
                                [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto& motion)
                                {
                                    frame.nodes[desc.index] = transform;
                                    frame.motion = motion;
                                });
        }
        return frame;
    };
    const auto handle_a = make_clip_handle(clip_a);
    const auto handle_b = make_clip_handle(clip_b);
    animation_player alone_a;
    alone_a.blend_to(0, handle_a, seconds_t{0.0f}, true);
    animation_player alone_b;
    alone_b.blend_to(0, handle_b, seconds_t{0.0f}, true);
    const auto frame_a = play(alone_a);
    const auto frame_b = play(alone_b);

    auto space = std::make_shared<blend_space_def>();
    space->add_clip({0.0f}, handle_a);
    space->add_clip({1.0f}, handle_b);
    animation_player blended;
    blended.set_blend_space(0, space);
    blended.set_blend_space_parameters(0, {WEIGHT_B});
    const auto frame = play(blended);

    bool is_every_bone_matching = frame.nodes.size() == frame_a.nodes.size();
    for(const auto& [index, transform] : frame.nodes)
    {
        const auto expected = blend(frame_a.nodes.at(index), frame_b.nodes.at(index), WEIGHT_B);
        is_every_bone_matching = is_every_bone_matching &&
                                 math::length(transform.get_position() - expected.get_position()) < 1e-4f &&
                                 std::fabs(math::dot(transform.get_rotation(), expected.get_rotation())) > 1.0f - 1e-6f;
    }
    check(is_every_bone_matching, label);
    const auto expected_motion = blend(frame_a.motion, frame_b.motion, WEIGHT_B);
    check(math::length(frame.motion.root_transform_delta.get_position() - expected_motion.root_transform_delta.get_position()) < 1e-5f &&
              std::fabs(math::dot(frame.motion.root_transform_delta.get_rotation(), expected_motion.root_transform_delta.get_rotation())) >
                  1.0f - 1e-6f,
          "the root motion is the blend of the clips' own root motion");
    check(math::length(frame_a.motion.root_transform_delta.get_position()) > 1e-3f, "the clips move their root");
}

void test_blend_space_retargets_once_like_each_clip()
{
    std::printf("test_blend_space_retargets_once_like_each_clip\n");
    // Clips whose every unmapped channel holds still: blended in their own skeleton, then retargeted once.
    check_blend_matches_blend_of_retargets(make_source_skeleton_clip(0.4f, 0.1f, 30.0f, 20.0f),
                                           make_source_skeleton_clip(1.2f, -0.05f, -50.0f, 70.0f),
                                           "one retarget of the blend is the blend of each clip's retarget");
    // An unmapped channel between hips and spine turns differently in each clip: the blend of the
    // clips' own retargets is no longer the retarget of their blend, so each clip retargets alone.
    check_blend_matches_blend_of_retargets(make_source_skeleton_clip(0.4f, 0.1f, 30.0f, 0.0f, 0.0f),
                                           make_source_skeleton_clip(1.2f, -0.05f, -50.0f, 140.0f, 140.0f),
                                           "clips with a moving unmapped channel still blend after their own retargets");
}

void test_rest_aligned_onto_own_skeleton_is_a_copy()
{
    std::printf("test_rest_aligned_onto_own_skeleton_is_a_copy\n");
    // Played onto the skeleton it was authored on, rest-aligned retargeting must reproduce the
    // authored local transforms exactly - whatever the rest rotations, including ones near 90
    // degrees where Euler round trips lose precision - and the rest pose must survive the clip's
    // serialization.
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> angle(-3.1f, 3.1f);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const auto random_rotation = [&]()
    {
        return math::normalize(math::angleAxis(angle(rng), math::normalize(math::vec3(unit(rng), unit(rng), unit(rng)) +
                                                                          math::vec3(0.0f, 1e-3f, 0.0f))));
    };

    constexpr size_t bone_count = 12;
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    auto target = std::make_shared<animation_target_skeleton>();
    for(size_t i = 0; i < bone_count; ++i)
    {
        auto& channel = clip->channels.emplace_back();
        channel.node_name = "bone" + std::to_string(i);
        channel.node_index = i;
        channel.parent_channel = i == 0 ? -1 : int(i / 2);
        channel.has_rest = true;
        channel.rest_local.set_position({unit(rng), 0.5f + unit(rng) * 0.2f, unit(rng)});
        // Every third rest rotation sits right at a 90 degree yaw (Euler gimbal territory).
        channel.rest_local.set_rotation(i % 3 == 0 ? math::angleAxis(math::radians(90.0f), math::vec3(0, 1, 0)) *
                                                         math::angleAxis(0.3f, math::vec3(1, 0, 0))
                                                   : random_rotation());
        if(i == 0)
        {
            channel.parent_offset.set_rotation(math::angleAxis(math::radians(90.0f), math::vec3(1, 0, 0)));
            channel.parent_offset.set_scale({100.0f, 100.0f, 100.0f});
        }
        channel.position_keys = {{seconds_t{0.0f}, channel.rest_local.get_position() + math::vec3(0.0f, 0.05f, 0.0f)}};
        channel.rotation_keys = {{seconds_t{0.0f}, random_rotation()}};
        channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};

        // Target index i + 1: index 0 is the model's armature node, which carries an importer's
        // facing correction the source scene does not have.
        target->parents.push_back(i == 0 ? 0 : channel.parent_channel + 1);
        target->bind_local.push_back(channel.rest_local);
    }
    math::transform armature;
    armature.set_rotation(math::angleAxis(math::radians(180.0f), math::vec3(0, 1, 0)) *
                          clip->channels[0].parent_offset.get_rotation());
    armature.set_scale(clip->channels[0].parent_offset.get_scale());
    target->parents.insert(target->parents.begin(), -1);
    target->bind_local.insert(target->bind_local.begin(), armature);
    target->compute_rest_globals();

    // Round trip through the clip's own serialization first.
    animation_clip loaded;
    {
        std::stringstream stream;
        {
            auto ar = ser20::create_oarchive_associative(stream);
            try_save(ar, ser20::make_nvp("clip", *clip));
        }
        auto ar = ser20::create_iarchive_associative(stream);
        try_load(ar, ser20::make_nvp("clip", loaded));
    }
    float worst_rest = 0.0f;
    for(size_t i = 0; i < bone_count; ++i)
    {
        const float d = std::fabs(math::dot(loaded.channels[i].rest_local.get_rotation(), clip->channels[i].rest_local.get_rotation()));
        worst_rest = std::max(worst_rest, 1.0f - d);
    }
    check(worst_rest < 1e-6f, "rest rotations survive serialization");

    const auto sample = [&](bool rest_aligned)
    {
        animation_skeleton_binding binding;
        binding.version = rest_aligned ? 11 : 10;
        binding.resolve = [](const animation_channel& channel)
        {
            return int(channel.node_index) + 1;
        };
        if(rest_aligned)
        {
            binding.rest_aligned_target = target;
        }
        // The root channel's parent offset is not part of the target: the copy path writes the
        // root's own local, the rest-aligned one the root relative to the model root.
        animation_player player;
        player.blend_to(0, make_clip_handle(std::make_shared<animation_clip>(loaded)), seconds_t{0.0f}, true);
        player.play();
        player.update_time(seconds_t{0.1f});
        std::map<size_t, math::transform> written;
        static const animation_pose empty_ref_pose{};
        player.update_poses(empty_ref_pose,
                            binding,
                            false,
                            [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto&)
                            {
                                written[desc.index] = transform;
                            });
        return written;
    };
    const auto copied = sample(false);
    const auto aligned = sample(true);
    float worst_rotation = 0.0f;
    float worst_position = 0.0f;
    for(size_t i = 2; i <= bone_count; ++i)
    {
        const auto& a = copied.at(i);
        const auto& b = aligned.at(i);
        worst_rotation = std::max(worst_rotation, 1.0f - std::fabs(math::dot(a.get_rotation(), b.get_rotation())));
        worst_position = std::max(worst_position, math::length(a.get_position() - b.get_position()));
    }
    check(worst_rotation < 1e-5f, "rest-aligned onto the own skeleton reproduces every local rotation");
    check(worst_position < 1e-3f, "rest-aligned onto the own skeleton reproduces every local position");
    std::printf("    worst rotation %.8f, worst position %.6f\n", worst_rotation, worst_position);
}

// ---------------------------------------------------------------------------------
// Animation controller (state machine)
// ---------------------------------------------------------------------------------

auto make_clip_state(const std::string& name, const std::shared_ptr<animation_clip>& clip, bool loop = true)
    -> animation_state_def
{
    animation_state_def state;
    state.name = name;
    state.motion = animation_motion_type::clip;
    state.clip = make_clip_handle(clip);
    state.loop = loop;
    return state;
}

auto make_transition(const std::string& source,
                     const std::string& destination,
                     std::vector<animation_condition_def> conditions,
                     float duration = 0.0f) -> animation_transition_def
{
    animation_transition_def transition;
    transition.source = source;
    transition.destination = destination;
    transition.conditions = std::move(conditions);
    transition.duration = duration;
    return transition;
}

auto make_parameter(const std::string& name, animation_parameter_type type) -> animation_parameter_def
{
    animation_parameter_def parameter;
    parameter.name = name;
    parameter.type = type;
    return parameter;
}

struct controller_rig
{
    std::shared_ptr<animation_controller> controller = std::make_shared<animation_controller>();
    animation_controller_instance instance;
    animation_player player;
    animation_skeleton_desc skeleton;

    auto layer() -> animation_layer_def&
    {
        if(controller->layers.empty())
        {
            controller->layers.emplace_back().name = "Base";
        }
        return controller->layers.front();
    }

    void start()
    {
        instance.bind(controller);
        player.play();
    }

    auto step(float dt) -> sampled_frame
    {
        instance.update(player,
                        1,
                        [this](animation_skeleton_desc& desc)
                        {
                            desc = skeleton;
                        });
        return step_player(player, dt);
    }

    auto state(size_t layer_index = 0) -> std::string
    {
        instance.sync(player);
        return instance.get_state_name(layer_index, instance.get_layer_status(layer_index).current_state);
    }
};

void test_controller_enters_default_state_and_fires_conditions()
{
    std::printf("test_controller_enters_default_state_and_fires_conditions\n");
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Speed", animation_parameter_type::float_value)};
    rig.layer().states = {make_clip_state("Idle", make_constant_clip(0.0f, 1.0f)),
                          make_clip_state("Walk", make_constant_clip(10.0f, 1.0f))};
    rig.layer().transitions = {
        make_transition("Idle", "Walk", {{"Speed", animation_condition_mode::greater, 0.5f}}),
        make_transition("Walk", "Idle", {{"Speed", animation_condition_mode::less, 0.5f}})};
    check(validate_animation_controller(*rig.controller).empty(), "controller validates");
    rig.start();

    auto frame = rig.step(0.1f);
    check(rig.state() == "Idle", "first state of the layer entered by default");
    check_near(frame.nodes[0].get_position().x, 0.0f, 1e-4f, "default state plays");

    rig.instance.set_float("Speed", 1.0f);
    frame = rig.step(0.1f);
    check(rig.state() == "Walk", "condition met: transition fired");
    check_near(frame.nodes[0].get_position().x, 10.0f, 1e-4f, "zero-length transition shows the destination at once");

    rig.instance.set_float("Speed", 0.0f);
    rig.step(0.1f);
    check(rig.state() == "Idle", "back on the reverse condition");
    check(!rig.instance.set_float("Missing", 1.0f), "unknown parameter reported");
}

void test_controller_triggers_and_exit_time()
{
    std::printf("test_controller_triggers_and_exit_time\n");
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Jump", animation_parameter_type::trigger)};
    rig.layer().states = {make_clip_state("Idle", make_constant_clip(0.0f, 1.0f)),
                          make_clip_state("Jump", make_linear_clip(20.0f, 21.0f, 1.0f), false)};
    auto back = make_transition("Jump", "Idle", {});
    back.has_exit_time = true;
    back.exit_time = 1.0f;
    rig.layer().transitions = {make_transition("", "Jump", {{"Jump", animation_condition_mode::is_true, 0.0f}}), back};
    rig.start();
    rig.step(0.1f);

    rig.instance.set_trigger("Jump");
    rig.step(0.1f);
    check(rig.state() == "Jump", "Any State transition fired on the trigger");
    check(!rig.instance.get_bool("Jump"), "the firing transition consumed the trigger");

    rig.step(0.1f);
    check(rig.state() == "Jump", "a consumed trigger does not re-enter the state");

    for(int i = 0; i < 12; ++i)
    {
        rig.step(0.1f);
    }
    check(rig.state() == "Idle", "exit time 1 returns once the one-shot finished");
}

void test_controller_exit_time_waits_on_looping_state()
{
    std::printf("test_controller_exit_time_waits_on_looping_state\n");
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Wave", animation_parameter_type::bool_value)};
    rig.layer().states = {make_clip_state("Idle", make_linear_clip(0.0f, 1.0f, 1.0f)),
                          make_clip_state("Wave", make_constant_clip(5.0f, 1.0f))};
    auto to_wave = make_transition("Idle", "Wave", {{"Wave", animation_condition_mode::is_true, 0.0f}});
    to_wave.has_exit_time = true;
    to_wave.exit_time = 0.5f;
    rig.layer().transitions = {to_wave};
    rig.start();

    rig.step(0.2f); // Idle at 0.2
    rig.instance.set_bool("Wave", true);
    rig.step(0.2f); // evaluated at 0.2, advanced to 0.4
    check(rig.state() == "Idle", "condition met before the exit time: still waiting");
    rig.step(0.2f); // evaluated at 0.4, advanced to 0.6
    check(rig.state() == "Idle", "exit time not reached when evaluated at 0.4");
    rig.step(0.2f); // evaluated at 0.6 -> fires
    check(rig.state() == "Wave", "fires once the looping state passed its exit time");
}

void test_controller_exit_time_above_one_on_loops()
{
    std::printf("test_controller_exit_time_above_one_on_loops\n");
    // Exit time 1.5 on a looping state: from the middle of the second cycle on, then again from the
    // middle of every later one. A condition that comes true at 2.2 waits for 2.5, not fires at once.
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Wave", animation_parameter_type::bool_value)};
    rig.layer().states = {make_clip_state("Idle", make_linear_clip(0.0f, 1.0f, 1.0f)),
                          make_clip_state("Wave", make_constant_clip(5.0f, 1.0f))};
    auto to_wave = make_transition("Idle", "Wave", {{"Wave", animation_condition_mode::is_true, 0.0f}});
    to_wave.has_exit_time = true;
    to_wave.exit_time = 1.5f;
    rig.layer().transitions = {to_wave};
    rig.start();
    for(int i = 0; i < 11; ++i)
    {
        rig.step(0.2f);
    }
    rig.instance.set_bool("Wave", true);
    rig.step(0.2f); // evaluated at 2.2
    check(rig.state() == "Idle", "past the exit time but early in the cycle: waits");
    rig.step(0.2f); // evaluated at 2.4
    check(rig.state() == "Idle", "still before the exit point of this cycle");
    rig.step(0.2f); // evaluated at 2.6
    check(rig.state() == "Wave", "fires from the exit point of the cycle");
}

void test_controller_keeps_script_layer_weight_and_reload_state()
{
    std::printf("test_controller_keeps_script_layer_weight_and_reload_state\n");
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Speed", animation_parameter_type::float_value)};
    rig.layer().states = {make_clip_state("Idle", make_constant_clip(0.0f, 1.0f)),
                          make_clip_state("Walk", make_constant_clip(10.0f, 1.0f))};
    rig.layer().transitions = {make_transition("Idle", "Walk", {{"Speed", animation_condition_mode::greater, 0.5f}})};
    auto& upper = rig.controller->layers.emplace_back();
    upper.name = "Upper";
    upper.weight = 0.5f;
    upper.states = {make_clip_state("Wave", make_constant_clip(3.0f, 1.0f))};
    rig.start();

    // A script sets the weight before the controller's first update starts the layer.
    rig.instance.set_layer_weight(rig.player, 1, 0.2f);
    rig.step(0.1f);
    check_near(rig.player.get_layer_weight(1), 0.2f, 1e-5f, "a script weight survives the layer starting");

    rig.instance.set_float("Speed", 1.0f);
    rig.step(0.1f);
    check(rig.state() == "Walk", "walking before the reload");

    // A recompile while playing hands over a new definition of the same controller.
    auto reloaded = std::make_shared<animation_controller>(*rig.controller);
    rig.instance.bind(reloaded);
    rig.step(0.1f);
    check_near(rig.instance.get_float("Speed"), 1.0f, 1e-5f, "a reload keeps parameter values");
    check(rig.state() == "Walk", "a reload keeps the active state");
    check_near(rig.player.get_layer_weight(1), 0.2f, 1e-5f, "a reload keeps the script weight");

    // Unbinding first starts the next controller fresh.
    rig.instance.bind(nullptr);
    rig.instance.bind(reloaded);
    rig.step(0.1f);
    check_near(rig.instance.get_float("Speed"), 0.0f, 1e-5f, "a fresh bind starts from the defaults");
    check(rig.state() == "Idle", "a fresh bind starts in the default state");
}

void test_controller_setters_respect_parameter_types()
{
    std::printf("test_controller_setters_respect_parameter_types\n");
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Speed", animation_parameter_type::float_value),
                                  make_parameter("Armed", animation_parameter_type::bool_value),
                                  make_parameter("Jump", animation_parameter_type::trigger)};
    rig.layer().states = {make_clip_state("Idle", make_constant_clip(0.0f, 1.0f))};
    rig.start();
    check(!rig.instance.set_bool("Speed", true), "set_bool on a float parameter is refused");
    check(!rig.instance.set_trigger("Armed"), "set_trigger on a bool parameter is refused");
    check(!rig.instance.set_bool("Jump", true), "set_bool on a trigger is refused");
    check(rig.instance.set_trigger("Jump") && rig.instance.get_bool("Jump"), "a trigger reads as set");
    check(rig.instance.set_bool("Armed", true) && rig.instance.get_bool("Armed"), "a bool reads back");
    check(!rig.instance.get_bool("Speed"), "a float parameter is not a bool");
}

void test_controller_survives_binary_archives()
{
    std::printf("test_controller_survives_binary_archives\n");
    // Compiled controllers are binary, and binary archives read fields by position.
    animation_controller source;
    source.parameters = {make_parameter("Speed", animation_parameter_type::float_value),
                         make_parameter("Attack", animation_parameter_type::trigger)};
    source.parameters[0].default_float = 1.5f;
    auto& base = source.layers.emplace_back();
    base.name = "Base";
    base.default_state = "Idle";
    auto& idle = base.states.emplace_back();
    idle.name = "Idle";
    idle.tag = "Calm";
    idle.speed = 1.25f;
    auto attack = make_transition("", "Idle", {{"Attack", animation_condition_mode::is_true, 0.0f}}, 0.15f);
    attack.has_exit_time = true;
    attack.exit_time = 0.75f;
    base.transitions = {attack};
    base.mask = {{"spine_01", 0.5f, true}};

    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    {
        ser20::oarchive_binary_t archive(stream);
        try_save(archive, ser20::make_nvp("controller", source));
    }
    animation_controller loaded;
    ser20::iarchive_binary_t archive(stream);
    try_load(archive, ser20::make_nvp("controller", loaded));
    check(loaded.parameters == source.parameters, "parameters survive the binary archive");
    check(loaded.layers.size() == 1 && loaded.layers[0].states == source.layers[0].states,
          "states survive the binary archive");
    check(loaded.layers.size() == 1 && loaded.layers[0].transitions == source.layers[0].transitions,
          "transitions survive the binary archive");
    check(loaded.layers.size() == 1 && loaded.layers[0].mask == source.layers[0].mask, "masks survive the binary archive");

    // The controller window knows its own saves when they come back by their text: a saved text,
    // compiled to binary and loaded from it, must give the same text again.
    source.layers[0].states[0].speed = 0.1f;
    const std::string text = save_to_string(source);
    animation_controller from_text;
    check(load_from_string(text, from_text), "the saved text loads");
    std::stringstream compiled(std::ios::in | std::ios::out | std::ios::binary);
    {
        ser20::oarchive_binary_t compile_archive(compiled);
        try_save(compile_archive, ser20::make_nvp("animation_controller", from_text));
    }
    animation_controller from_binary;
    {
        ser20::iarchive_binary_t load_archive(compiled);
        try_load(load_archive, ser20::make_nvp("animation_controller", from_binary));
    }
    check(save_to_string(from_binary) == text, "text, compiled and loaded, gives the same text");
}

void test_controller_crossfade_and_blend_state()
{
    std::printf("test_controller_crossfade_and_blend_state\n");
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Speed", animation_parameter_type::float_value),
                                  make_parameter("Go", animation_parameter_type::bool_value)};
    animation_state_def locomotion;
    locomotion.name = "Locomotion";
    locomotion.motion = animation_motion_type::blend_1d;
    locomotion.parameter_x = "Speed";
    locomotion.points = {{make_clip_handle(make_constant_clip(0.0f, 1.0f)), {0.0f, 0.0f}},
                         {make_clip_handle(make_constant_clip(10.0f, 1.0f)), {1.0f, 0.0f}}};
    rig.layer().states = {make_clip_state("Idle", make_constant_clip(0.0f, 1.0f)), locomotion};
    rig.layer().transitions = {
        make_transition("Idle", "Locomotion", {{"Go", animation_condition_mode::is_true, 0.0f}}, 1.0f)};
    rig.start();
    rig.step(0.1f);

    rig.instance.set_float("Speed", 0.5f);
    rig.instance.set_bool("Go", true);
    auto frame = rig.step(0.5f);
    rig.instance.sync(rig.player);
    check(rig.instance.get_layer_status(0).next_state == 1, "crossfade running towards the blend state");
    check_near(frame.nodes[0].get_position().x, 2.5f, 0.05f, "half way into a crossfade to a 50% blend");
    frame = rig.step(0.6f);
    check(rig.state() == "Locomotion", "crossfade completed");
    check_near(frame.nodes[0].get_position().x, 5.0f, 1e-3f, "blend state follows its parameter");
    rig.instance.set_float("Speed", 1.0f);
    frame = rig.step(0.1f);
    check_near(frame.nodes[0].get_position().x, 10.0f, 1e-3f, "parameter change re-weights the blend");
}

void test_controller_layer_mask_and_curve_parameters()
{
    std::printf("test_controller_layer_mask_and_curve_parameters\n");
    const auto make_two_node_clip = [](float x)
    {
        auto clip = make_constant_clip(x, 1.0f);
        auto& second = clip->channels.emplace_back(clip->channels.front());
        second.node_name = "child";
        second.node_index = 1;
        return clip;
    };

    controller_rig rig;
    rig.skeleton.names = {"root", "child"};
    rig.skeleton.parents = {-1, 0};
    rig.controller->parameters = {make_parameter("Reach", animation_parameter_type::float_value)};
    rig.layer().states = {make_clip_state("Idle", make_two_node_clip(0.0f))};
    auto& upper = rig.controller->layers.emplace_back();
    upper.name = "Upper";
    upper.mask = {{"child", 1.0f, true}};
    auto wave = make_two_node_clip(10.0f);
    auto& curve = wave->curves.emplace_back();
    curve.name = "Reach";
    curve.curve.add_point(0.75f, 0.0f);
    curve.curve.add_point(0.75f, 1.0f);
    upper.states = {make_clip_state("Wave", wave)};
    rig.start();

    auto frame = rig.step(0.1f);
    check_near(frame.nodes[0].get_position().x, 0.0f, 1e-4f, "bone outside the mask keeps the base layer");
    check_near(frame.nodes[1].get_position().x, 10.0f, 1e-4f, "bone in the mask takes the upper layer");
    check(rig.instance.get_layer_index("Upper") == 1, "layer found by name");

    rig.step(0.1f);
    check_near(rig.instance.get_float("Reach"), 0.75f, 1e-4f, "a float parameter follows the curve of the same name");
}

void test_controller_validation_and_round_trip()
{
    std::printf("test_controller_validation_and_round_trip\n");
    animation_controller broken;
    broken.parameters = {make_parameter("Speed", animation_parameter_type::float_value),
                         make_parameter("Speed", animation_parameter_type::bool_value)};
    auto& layer = broken.layers.emplace_back();
    layer.name = "Base";
    auto& state = layer.states.emplace_back();
    state.name = "Idle";
    layer.transitions = {make_transition("Idle", "Nowhere", {{"Speed", animation_condition_mode::is_false, 0.0f}}),
                         make_transition("Idle", "Idle", {})};
    // An empty state is how a layer above the base rests; only the base layer must play something.
    auto& upper = broken.layers.emplace_back();
    upper.name = "Upper";
    upper.states.emplace_back().name = "Rest";
    upper.default_state = "Rest";
    const auto problems = validate_animation_controller(broken);
    const auto mentions = [&](const std::string& text)
    {
        return std::any_of(problems.begin(),
                           problems.end(),
                           [&](const std::string& problem)
                           {
                               return problem.find(text) != std::string::npos;
                           });
    };
    check(mentions("two parameters are named 'Speed'"), "duplicate parameter reported");
    check(mentions("layer 'Base' state 'Idle' has no clip"), "clip state without a clip reported on the base layer");
    check(!mentions("layer 'Upper' state 'Rest'"), "an empty state on an upper layer is valid");
    check(mentions("unknown destination state"), "unknown destination reported");
    check(mentions("can never fire"), "transition without conditions or exit time reported");

    animation_controller risky;
    risky.parameters = {make_parameter("Hurt", animation_parameter_type::bool_value)};
    auto& risky_layer = risky.layers.emplace_back();
    risky_layer.states = {make_clip_state("Hit", make_constant_clip(0.0f, 1.0f), false)};
    auto self = make_transition("", "Hit", {{"Hurt", animation_condition_mode::is_true, 0.0f}});
    self.can_transition_to_self = true;
    auto late = make_transition("Hit", "Hit", {});
    late.has_exit_time = true;
    late.exit_time = 1.5f;
    risky_layer.transitions = {self, late};
    const auto risky_problems = validate_animation_controller(risky);
    const auto risky_mentions = [&](const std::string& text)
    {
        return std::any_of(risky_problems.begin(),
                           risky_problems.end(),
                           [&](const std::string& problem)
                           {
                               return problem.find(text) != std::string::npos;
                           });
    };
    check(risky_mentions("a layer has no name"), "an unnamed layer is reported");
    check(risky_mentions("restarts the state on every frame"), "Any State to itself without a trigger is reported");
    check(risky_mentions("exit time above 1 on a state that does not loop"), "an unreachable exit time is reported");

    animation_controller source;
    source.parameters = {make_parameter("Speed", animation_parameter_type::float_value),
                         make_parameter("Attack", animation_parameter_type::trigger)};
    auto& base = source.layers.emplace_back();
    base.name = "Base";
    base.default_state = "Idle";
    auto& idle = base.states.emplace_back();
    idle.name = "Idle";
    idle.speed = 1.25f;
    idle.editor_position = {120.0f, -40.0f};
    auto attack = make_transition("", "Idle", {{"Attack", animation_condition_mode::is_true, 0.0f}}, 0.15f);
    attack.interruptible = true;
    base.transitions = {attack};
    base.mask = {{"spine_01", 0.5f, true}};
    base.blend_mode = animation_layer_blend_mode::additive;

    animation_controller loaded;
    check(load_from_string(save_to_string(source), loaded), "controller text parses back");
    check(loaded.parameters == source.parameters, "parameters round trip");
    check(loaded.layers.size() == 1 && loaded.layers[0].transitions == source.layers[0].transitions,
          "transitions round trip");
    check(loaded.layers.size() == 1 && loaded.layers[0].mask == source.layers[0].mask, "mask round trips");
    check(loaded.layers.size() == 1 && loaded.layers[0].blend_mode == animation_layer_blend_mode::additive,
          "blend mode round trips");
    check(loaded.layers.size() == 1 && !loaded.layers[0].states.empty() &&
              loaded.layers[0].states[0].editor_position == source.layers[0].states[0].editor_position,
          "graph positions round trip");

    animation_layer_def rename_layer = base;
    rename_animation_state(rename_layer, 0, "Rest");
    check(rename_layer.default_state == "Rest" && rename_layer.transitions[0].destination == "Rest",
          "renaming a state updates every reference");
    rename_animation_parameter(source, 1, "Strike");
    check(source.layers[0].transitions[0].conditions[0].parameter == "Strike", "renaming a parameter updates conditions");
}

} // namespace

// ---------------------------------------------------------------------------------

void test_crossfade_finishes_without_pose_updates()
{
    std::printf("test_crossfade_finishes_without_pose_updates\n");
    // Renderer-based culling steps time but skips poses: the crossfade still ends.
    const auto clip_b = make_clip_handle(make_constant_clip(10.0f, 1.0f));
    animation_player player;
    player.blend_to(0, make_clip_handle(make_constant_clip(0.0f, 1.0f)), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);
    player.blend_to(0, clip_b, seconds_t{0.2f}, true);
    for(int i = 0; i < 3; ++i)
    {
        player.update_time(seconds_t{0.1f});
    }
    const auto& layer = player.get_layers().front();
    check(!layer.target_state.is_valid(), "the crossfade ended in the time step");
    check(layer.current_state.state.clip == clip_b, "the target plays");
}

void test_reversal_keeps_the_requested_loop_and_speed()
{
    std::printf("test_reversal_keeps_the_requested_loop_and_speed\n");
    const auto clip_a = make_clip_handle(make_constant_clip(0.0f, 1.0f));
    const auto clip_b = make_clip_handle(make_constant_clip(10.0f, 1.0f));
    animation_player player;
    player.blend_to(0, clip_a, seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);
    animation_play_params to_b;
    to_b.duration = seconds_t{1.0f};
    to_b.speed = 1.5f;
    player.play_clip(0, clip_b, to_b);
    step_player(player, 0.25f);
    animation_play_params back_to_a;
    back_to_a.duration = seconds_t{1.0f};
    back_to_a.speed = 2.0f;
    back_to_a.loop = false;
    player.play_clip(0, clip_a, back_to_a);
    const auto& layer = player.get_layers().front();
    check(layer.target_state.state.clip == clip_a, "the crossfade heads back to A");
    check_near(layer.target_state.state.speed, 2.0f, 1e-6f, "A takes the requested speed");
    check(!layer.target_state.state.loop, "and the requested loop");
    check_near(layer.current_state.state.speed, 1.5f, 1e-6f, "B, fading out, keeps its own");
}

void test_phase_sync_follows_an_interrupted_target()
{
    std::printf("test_phase_sync_follows_an_interrupted_target\n");
    // A (1 s) crossfades to B (2 s), phase synced; cut in by C: C starts where B, the motion the
    // layer was heading for, is.
    animation_player player;
    player.blend_to(0, make_clip_handle(make_constant_clip(0.0f, 1.0f)), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.3f);
    player.blend_to(0, make_clip_handle(make_constant_clip(10.0f, 2.0f)), seconds_t{1.0f}, true, true);
    step_player(player, 0.2f);
    const float target_progress = player.get_layers().front().target_state.state.get_progress();
    check_near(target_progress, 0.4f, 1e-3f, "B is 0.6 s + 0.2 s into its 2 s");
    player.blend_to(0, make_clip_handle(make_constant_clip(20.0f, 1.0f)), seconds_t{1.0f}, true, true);
    check_near(player.get_layers().front().target_state.state.get_progress(), target_progress, 1e-3f,
               "C starts at B's phase, not at the frozen source's");
}

void test_reversing_a_crossfade_nobody_saw_drops_it()
{
    std::printf("test_reversing_a_crossfade_nobody_saw_drops_it\n");
    const auto clip_a = make_clip_handle(make_constant_clip(0.0f, 1.0f));
    animation_player player;
    player.blend_to(0, clip_a, seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);
    player.blend_to(0, make_clip_handle(make_constant_clip(10.0f, 1.0f)), seconds_t{1.0f}, true);
    player.blend_to(0, clip_a, seconds_t{1.0f}, true);
    const auto& layer = player.get_layers().front();
    check(!layer.target_state.is_valid(), "a target never posed is dropped, not faded out");
    check(layer.current_state.state.clip == clip_a, "the layer stays on A");
}

void test_override_layer_crossfade_keeps_the_base_for_a_bone_it_lacks()
{
    std::printf("test_override_layer_crossfade_keeps_the_base_for_a_bone_it_lacks\n");
    // The base holds bone 1 at 30; an override layer crossfades from a clip with bone 1 at 10 to one
    // without it. The bone eases from 10 to the base's 30 and stays there: no pop at the end.
    const animation_pose rest_pose = make_two_bone_rest_pose();
    auto base_clip = make_constant_clip(0.0f, 1.0f);
    add_constant_bone(*base_clip, 1, 30.0f);
    auto layer_clip = make_constant_clip(0.0f, 1.0f);
    add_constant_bone(*layer_clip, 1, 10.0f);
    animation_player player;
    player.blend_to(0, make_clip_handle(base_clip), seconds_t{0.0f}, true);
    player.blend_to(1, make_clip_handle(layer_clip), seconds_t{0.0f}, true);
    player.set_layer_weight(1, 1.0f);
    player.play();
    auto frame = step_player_on(player, 0.1f, rest_pose);
    check_near(frame.nodes[1].get_position().x, 10.0f, 1e-3f, "the layer overrides the bone");
    player.blend_to(1, make_clip_handle(make_constant_clip(0.0f, 1.0f)), seconds_t{1.0f}, true);
    frame = step_player_on(player, 0.5f, rest_pose);
    check_near(frame.nodes[1].get_position().x, 20.0f, 1e-3f, "half way: half the layer's, half the base's");
    frame = step_player_on(player, 0.6f, rest_pose);
    check_near(frame.nodes[1].get_position().x, 30.0f, 1e-3f, "the end: the base's, no pop");
}

void test_blend_space_narrowing_away_from_a_bone_puts_it_at_rest()
{
    std::printf("test_blend_space_narrowing_away_from_a_bone_puts_it_at_rest\n");
    const animation_pose rest_pose = make_two_bone_rest_pose();
    auto blend_space = std::make_shared<blend_space_def>();
    blend_space->add_clip({0.0f}, make_clip_handle(make_constant_clip(0.0f, 1.0f)));
    blend_space->add_clip({1.0f}, make_clip_handle(make_clip_with_extra_bone()));
    animation_player player;
    player.set_blend_space(0, blend_space);
    player.set_blend_space_parameters(0, {0.5f});
    player.play();
    auto frame = step_player_on(player, 0.1f, rest_pose);
    check_near(frame.nodes[1].get_position().x, 7.0f, 1e-3f, "half way between rest and the point animating it");
    player.set_blend_space_parameters(0, {0.0f});
    frame = step_player_on(player, 0.1f, rest_pose);
    check(frame.nodes.count(1) == 1, "the bone the blend no longer animates is written once");
    check_near(frame.nodes[1].get_position().x, 4.0f, 1e-4f, "at rest");
    frame = step_player_on(player, 0.1f, rest_pose);
    check(frame.nodes.count(1) == 0, "and left there");
}

void test_directional_blend_with_opposite_points()
{
    std::printf("test_directional_blend_with_opposite_points\n");
    // Idle, forward and back: walking back with a slight sideways input plays back, never half forward.
    blend_space_def blend_space;
    blend_space.set_2d_mode(blend_space_2d_mode::freeform_directional);
    blend_space.add_clip({0.0f, 0.0f}, make_clip_handle(make_constant_clip(0.0f, 1.0f)));
    blend_space.add_clip({0.0f, 1.0f}, make_clip_handle(make_constant_clip(1.0f, 1.0f)));
    blend_space.add_clip({0.0f, -1.0f}, make_clip_handle(make_constant_clip(2.0f, 1.0f)));
    for(const float side : {-0.001f, 0.001f})
    {
        std::vector<blend_space_weight> weights;
        blend_space.compute_blend({side, -1.0f}, weights);
        float forward = 0.0f;
        float back = 0.0f;
        for(const auto& weight : weights)
        {
            forward += weight.point == 1 ? weight.weight : 0.0f;
            back += weight.point == 2 ? weight.weight : 0.0f;
        }
        check_near(back, 1.0f, 1e-2f, "back plays");
        check_near(forward, 0.0f, 1e-2f, "forward does not");
    }
}

void test_override_layer_keeps_base_curves()
{
    std::printf("test_override_layer_keeps_base_curves\n");
    // An aiming layer without curves leaves the locomotion's curves alone.
    animation_pose base;
    make_pose_node(base, 0, 0.0f);
    base.curves = {{7u, 0.8f}};
    animation_pose layer;
    make_pose_node(layer, 0, 5.0f);
    animation_pose result;
    blend_layer(base, layer, animation_layer_blend_mode::override_pose, 1.0f, {}, {}, result);
    check(result.curves.size() == 1 && result.curves[0].id == 7u, "the base curve is kept");
    check_near(result.curves[0].value, 0.8f, 1e-6f, "at the base's value");
}

void test_seek_starts_root_motion_over()
{
    std::printf("test_seek_starts_root_motion_over\n");
    animation_player player;
    player.blend_to(0, make_clip_handle(make_linear_clip(0.0f, 1.5f, 1.0f, true)), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);
    step_player(player, 0.1f);
    player.seek(0, 0.9f);
    // The step after the seek measures from the seek position: the jump itself moves nothing.
    const auto frame = step_player(player, 0.05f);
    check_near(frame.motion.root_transform_delta.get_position().x, 0.075f, 1e-3f, "a seek does not move the entity by the jump");
    const auto next = step_player(player, 0.05f);
    check_near(next.motion.root_transform_delta.get_position().x, 0.075f, 1e-3f, "travel resumes from there");
}

/// Root travel over `seconds` of a 1.5 per second clip entered by a switch from a still one, at
/// steps of `dt`.
auto measure_switch_travel(float dt, float seconds) -> float
{
    animation_player player;
    player.blend_to(0, make_clip_handle(make_constant_clip(0.0f, 1.0f)), seconds_t{0.0f}, true);
    player.play();
    step_player(player, dt);
    animation_play_params params;
    params.duration = seconds_t{0.0f};
    params.loop = false;
    player.play_clip(0, make_clip_handle(make_linear_clip(0.0f, 1.5f, 1.0f, true)), params);
    float travel = 0.0f;
    const int steps = int(std::lround(seconds / dt));
    for(int i = 0; i < steps; ++i)
    {
        travel += step_player(player, dt).motion.root_transform_delta.get_position().x;
    }
    return travel;
}

void test_switch_travel_does_not_depend_on_the_frame_rate()
{
    std::printf("test_switch_travel_does_not_depend_on_the_frame_rate\n");
    // The step that starts the clip moves the entity too.
    check_near(measure_switch_travel(1.0f / 30.0f, 0.5f), 0.75f, 1e-3f, "half a second at 30 Hz");
    check_near(measure_switch_travel(1.0f / 144.0f, 0.5f), 0.75f, 1e-3f, "half a second at 144 Hz");
}

void test_crossfade_target_moves_on_its_first_step()
{
    std::printf("test_crossfade_target_moves_on_its_first_step\n");
    animation_player player;
    player.blend_to(0, make_clip_handle(make_constant_clip(0.0f, 1.0f)), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);
    player.blend_to(0, make_clip_handle(make_linear_clip(0.0f, 1.5f, 1.0f, true)), seconds_t{0.2f}, true);
    const auto frame = step_player(player, 0.1f);
    // Half way through the crossfade: half of the target's first 0.15.
    check_near(frame.motion.root_transform_delta.get_position().x, 0.075f, 1e-3f, "the first step counts at its weight");
}

void test_culled_character_returns_without_a_jump()
{
    std::printf("test_culled_character_returns_without_a_jump\n");
    // Steps without poses drop their motion; the step that shows the character again moves it by
    // that step only.
    animation_player player;
    player.blend_to(0, make_clip_handle(make_linear_clip(0.0f, 1.5f, 1.0f, true)), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.1f);
    for(int i = 0; i < 3; ++i)
    {
        player.update_time(seconds_t{0.1f});
        player.reset_root_motion();
    }
    const auto frame = step_player(player, 0.1f);
    check_near(frame.motion.root_transform_delta.get_position().x, 0.15f, 1e-3f, "the returning step moves by itself");
}

void test_loop_count_matches_the_time_on_the_clip()
{
    std::printf("test_loop_count_matches_the_time_on_the_clip\n");
    // A step of three clip lengths: three wraps, at the start of the cycle - not three wraps at its end.
    animation_player player;
    player.blend_to(0, make_clip_handle(make_linear_clip(0.0f, 1.0f, 0.1f)), seconds_t{0.0f}, true);
    player.play();
    step_player(player, 0.3f);
    const double position = player.get_layers().front().current_state.state.get_cycle_position();
    check(std::fabs(position - 3.0) < 1e-3, "three cycles");
}

/// A root that walks a quarter circle of radius 1 while turning 90 degrees, along its own heading.
auto make_turning_walk_clip() -> std::shared_ptr<animation_clip>
{
    constexpr int KEYS = 31;
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    auto& channel = clip->channels.emplace_back();
    channel.node_name = "root";
    channel.node_index = 0;
    for(int i = 0; i < KEYS; ++i)
    {
        const float t = float(i) / float(KEYS - 1);
        const float angle = math::radians(90.0f) * t;
        channel.position_keys.push_back({seconds_t{t}, {1.0f - std::cos(angle), 0.0f, std::sin(angle)}});
        channel.rotation_keys.push_back({seconds_t{t}, math::angleAxis(angle, math::vec3(0.0f, 1.0f, 0.0f))});
    }
    channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    clip->root_motion.position_node_index = 0;
    clip->root_motion.position_node_name = "root";
    clip->root_motion.rotation_node_index = 0;
    clip->root_motion.rotation_node_name = "root";
    return clip;
}

void test_turning_walk_root_motion_follows_the_clip_path()
{
    std::printf("test_turning_walk_root_motion_follows_the_clip_path\n");
    // The entity applies each step along its own (turned) axes, then turns: it must end where the
    // clip's root does, facing where it faces.
    constexpr int STEPS = 30;
    animation_player player;
    animation_play_params params;
    params.duration = seconds_t{0.0f};
    params.loop = false;
    player.play_clip(0, make_clip_handle(make_turning_walk_clip()), params);
    player.play();
    math::vec3 position(0.0f);
    math::quat rotation = math::identity<math::quat>();
    // A first sample at the start sets the baseline, so the steps after it carry the whole path.
    step_player(player, 0.0f);
    for(int i = 0; i < STEPS; ++i)
    {
        const auto frame = step_player(player, 1.0f / float(STEPS));
        position += rotation * frame.motion.root_transform_delta.get_position();
        rotation = math::normalize(rotation * frame.motion.root_transform_delta.get_rotation());
    }
    check_near(position.x, 1.0f, 0.03f, "ends a radius to the side");
    check_near(position.z, 1.0f, 0.03f, "and a radius ahead");
    check_near(math::dot(rotation * math::vec3(0.0f, 0.0f, 1.0f), math::vec3(1.0f, 0.0f, 0.0f)), 1.0f, 1e-3f,
               "facing where the clip faces");
}

void test_unmapped_root_moves_the_entity_through_the_hips()
{
    std::printf("test_unmapped_root_moves_the_entity_through_the_hips\n");
    // A floor root that carries the travel, on a target that starts at the hips (no root bone).
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{1.0f};
    const auto add_channel = [&](const std::string& name, int parent, const math::vec3& rest,
                                 const std::vector<animation_channel::key<math::vec3>>& positions)
    {
        auto& channel = clip->channels.emplace_back();
        channel.node_name = name;
        channel.node_index = clip->channels.size() - 1;
        channel.position_keys = positions;
        channel.rotation_keys = {{seconds_t{0.0f}, math::identity<math::quat>()}};
        channel.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
        channel.has_rest = true;
        channel.rest_local.set_position(rest);
        channel.parent_channel = parent;
    };
    add_channel("root", -1, {}, {{seconds_t{0.0f}, {}}, {seconds_t{1.0f}, {0.0f, 0.0f, 1.0f}}});
    add_channel("hips", 0, {0.0f, 1.0f, 0.0f}, {{seconds_t{0.0f}, {0.0f, 1.0f, 0.0f}}});
    add_channel("spine", 1, {0.0f, 0.5f, 0.0f}, {{seconds_t{0.0f}, {0.0f, 0.5f, 0.0f}}});
    clip->root_motion.position_node_name = "root";
    clip->root_motion.position_node_index = 0;
    auto target = std::make_shared<animation_target_skeleton>();
    target->parents = {-1, 0};
    target->bind_local.resize(2);
    target->bind_local[0].set_position({0.0f, 1.0f, 0.0f});
    target->bind_local[1].set_position({0.0f, 0.5f, 0.0f});
    target->compute_rest_globals();
    animation_skeleton_binding binding;
    binding.version = 7;
    binding.resolve = [](const animation_channel& channel)
    {
        return channel.node_name == "hips" ? 0 : channel.node_name == "spine" ? 1 : -1;
    };
    binding.rest_aligned_target = target;
    const auto play = [&](bool extract_root_motion)
    {
        static const animation_pose empty_ref_pose{};
        animation_player player;
        player.blend_to(0, make_clip_handle(clip), seconds_t{0.0f}, false);
        player.play();
        retargeted_frame frame;
        math::vec3 travel(0.0f);
        // A first sample at the start sets the baseline, so the four steps after it carry the whole travel.
        for(int i = 0; i < 5; ++i)
        {
            player.update_time(seconds_t{i == 0 ? 0.0f : 0.25f});
            player.update_poses(empty_ref_pose,
                                binding,
                                extract_root_motion,
                                [&](const animation_pose::node_desc& desc, const math::transform& transform, const auto& motion)
                                {
                                    frame.nodes[desc.index] = transform;
                                    frame.motion = motion;
                                });
            travel += frame.motion.root_transform_delta.get_position();
        }
        return std::make_pair(frame, travel);
    };
    const auto [extracted, travel] = play(true);
    check(extracted.motion.root_position_node_index == 0, "root motion reports through the hips");
    check_near(travel.z, 1.0f, 1e-3f, "the entity travels the root's way");
    check_near(extracted.nodes.at(0).get_position().z, 0.0f, 1e-3f, "the hips stay over the entity");
    const auto in_pose = play(false).first;
    check_near(in_pose.nodes.at(0).get_position().z, 1.0f, 1e-3f, "without root motion the hips carry the travel");
}

void test_controller_restarts_a_state_sharing_the_clip()
{
    std::printf("test_controller_restarts_a_state_sharing_the_clip\n");
    // Two attacks on one clip: the second starts the clip over instead of carrying on the first.
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Next", animation_parameter_type::trigger)};
    const auto slash = make_linear_clip(0.0f, 1.0f, 1.0f);
    auto attack_2 = make_clip_state("Attack2", slash, false);
    auto attack_1 = make_clip_state("Attack1", slash, false);
    attack_2.clip = attack_1.clip;
    rig.layer().states = {attack_1, attack_2};
    rig.layer().transitions = {make_transition("Attack1", "Attack2", {{"Next", animation_condition_mode::is_true, 0.0f}})};
    rig.start();
    for(int i = 0; i < 8; ++i)
    {
        rig.step(0.1f);
    }
    rig.instance.set_trigger("Next");
    auto frame = rig.step(0.1f);
    check(rig.state() == "Attack2", "the second attack runs");
    check_near(frame.nodes[0].get_position().x, 0.1f, 1e-3f, "from the clip's start");
}

void test_controller_edit_keeps_the_playing_time()
{
    std::printf("test_controller_edit_keeps_the_playing_time\n");
    controller_rig rig;
    rig.layer().states = {make_clip_state("Walk", make_linear_clip(0.0f, 1.0f, 1.0f))};
    rig.start();
    for(int i = 0; i < 5; ++i)
    {
        rig.step(0.1f);
    }
    rig.layer().states.front().speed = 1.0f;
    ++rig.controller->revision;
    auto frame = rig.step(0.1f);
    check(rig.state() == "Walk", "the state is kept");
    check_near(frame.nodes[0].get_position().x, 0.6f, 1e-3f, "and so is its time");
}

void test_controller_edit_dropping_a_layer_clears_it()
{
    std::printf("test_controller_edit_dropping_a_layer_clears_it\n");
    controller_rig rig;
    rig.layer().states = {make_clip_state("Idle", make_constant_clip(0.0f, 1.0f))};
    auto& upper = rig.controller->layers.emplace_back();
    upper.name = "Upper";
    upper.states = {make_clip_state("Wave", make_constant_clip(5.0f, 1.0f))};
    rig.start();
    rig.step(0.1f);
    check(rig.player.get_layers().size() == 2 && rig.player.get_layers()[1].has_motion(), "the upper layer plays");
    rig.controller->layers.pop_back();
    ++rig.controller->revision;
    rig.step(0.1f);
    check(!rig.player.get_layers()[1].has_motion(), "a layer the edit removed stops playing");
}

void test_controller_exit_time_one_waits_for_the_cycle_end()
{
    std::printf("test_controller_exit_time_one_waits_for_the_cycle_end\n");
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Stop", animation_parameter_type::bool_value)};
    rig.layer().states = {make_clip_state("Loop", make_linear_clip(0.0f, 1.0f, 1.0f)),
                          make_clip_state("Idle", make_constant_clip(5.0f, 1.0f))};
    auto to_idle = make_transition("Loop", "Idle", {{"Stop", animation_condition_mode::is_true, 0.0f}});
    to_idle.has_exit_time = true;
    to_idle.exit_time = 1.0f;
    rig.layer().transitions = {to_idle};
    rig.start();
    for(int i = 0; i < 14; ++i)
    {
        rig.step(0.1f);
    }
    rig.instance.set_bool("Stop", true);
    rig.step(0.1f);
    rig.step(0.1f);
    check(rig.state() == "Loop", "mid-cycle the loop carries on");
    for(int i = 0; i < 5; ++i)
    {
        rig.step(0.1f);
    }
    check(rig.state() == "Idle", "it leaves at the end of the cycle");
}

void test_controller_trigger_reaches_every_layer()
{
    std::printf("test_controller_trigger_reaches_every_layer\n");
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Hit", animation_parameter_type::trigger)};
    rig.layer().states = {make_clip_state("Idle", make_constant_clip(0.0f, 1.0f)),
                          make_clip_state("Flinch", make_constant_clip(1.0f, 1.0f))};
    rig.layer().transitions = {make_transition("Idle", "Flinch", {{"Hit", animation_condition_mode::is_true, 0.0f}})};
    auto& face = rig.controller->layers.emplace_back();
    face.name = "Face";
    face.states = {make_clip_state("Calm", make_constant_clip(0.0f, 1.0f)), make_clip_state("Pain", make_constant_clip(2.0f, 1.0f))};
    face.transitions = {make_transition("Calm", "Pain", {{"Hit", animation_condition_mode::is_true, 0.0f}})};
    rig.start();
    rig.step(0.1f);
    rig.instance.set_trigger("Hit");
    rig.step(0.1f);
    check(rig.state(0) == "Flinch", "the base layer takes the trigger");
    check(rig.state(1) == "Pain", "and so does the face layer");
    check(!rig.instance.get_bool("Hit"), "then it resets");
}

void test_controller_numbers_do_not_set_booleans()
{
    std::printf("test_controller_numbers_do_not_set_booleans\n");
    controller_rig rig;
    rig.controller->parameters = {make_parameter("Jump", animation_parameter_type::trigger),
                                  make_parameter("Crouch", animation_parameter_type::bool_value)};
    rig.layer().states = {make_clip_state("Idle", make_constant_clip(0.0f, 1.0f))};
    rig.start();
    check(!rig.instance.set_float("Jump", 1.0f), "a number does not fire a trigger");
    check(!rig.instance.set_int("Crouch", 1), "nor set a bool");
    check(!rig.instance.get_bool("Jump") && !rig.instance.get_bool("Crouch"), "both stay off");
    rig.instance.set_bool("Crouch", true);
    check(rig.instance.get_int("Crouch") == 1 && rig.instance.get_float("Crouch") == 1.0f, "a bool reads as 1 when on");
}

auto run_animation_suite(rtti::context& ctx) -> int
{
    g_pool = ctx.get_cached<threader>().pool.get();

    test_pairwise_blend_merges_sorted_nodes();
    test_multiway_blend_weighted_average();
    test_blends_cover_a_bone_some_poses_lack();
    test_blend_space_actually_blends();
    test_blend_space_time_advances_and_loops();
    test_blend_space_1d_param_edges();
    test_blend_space_2d_edges();
    test_blend_space_2d_directional();
    test_blend_space_phase_sync();
    test_blend_space_points_keep_their_root_motion();
    test_layer_bone_missing_from_base_fades_from_rest();
    test_crossfade_from_a_target_never_shown();
    test_crossfade_eases_a_bone_one_clip_lacks();
    test_switch_puts_a_bone_the_new_clip_lacks_at_rest();
    test_blend_space_point_animating_an_extra_bone();
    test_reversed_crossfade_continues_with_any_easing();
    test_layer_override_with_mask();
    test_layer_additive_from_first_frame();
    test_layer_fades_in_and_out();
    test_events_fire_once_per_crossing();
    test_events_respect_weights();
    test_curves_blend_with_the_pose();
    test_clip_curves_survive_both_archives();
    test_binding_maps_clips_onto_the_target_skeleton();
    test_target_skeleton_takes_the_bind_pose();
    test_rest_aligned_retargeting();
    test_rest_aligned_scale_follows_the_bone();
    test_rest_aligned_retargeting_carries_scaled_offsets();
    test_humanoid_roles_from_structure();
    test_humanoid_roles_of_common_rigs();
    test_rest_aligned_matches_humanoid_roles();
    test_ue_clip_on_a_mixamo_rig();
    test_names_match_across_namespaces();
    test_rest_aligned_onto_own_skeleton_is_a_copy();
    test_blend_space_retargets_once_like_each_clip();
    test_root_motion_accumulates_across_loops();
    test_root_motion_no_teleport_on_replay();
    test_phase_synced_blend_has_no_motion_spike();
    test_keep_in_place_preserves_sway();
    test_root_motion_extract_pose_is_blend_safe();
    test_root_motion_extract_rotation_is_blend_safe();
    test_root_motion_unapplied_extraction_keeps_authored_pose();
    test_root_motion_splits_along_the_clip_up_axis();
    test_root_motion_root_bone_locks_to_rest();
    test_root_motion_extracts_only_the_heading();
    test_zero_duration_blend_and_finite_output();
    test_force_update_steps_while_paused();
    test_blend_to_null_clears_layer();
    test_retarget_blend_keeps_no_pop();
    test_root_motion_result_weight_blending();
    test_empty_tracks_have_sane_defaults();
    test_controller_enters_default_state_and_fires_conditions();
    test_controller_triggers_and_exit_time();
    test_controller_exit_time_waits_on_looping_state();
    test_controller_crossfade_and_blend_state();
    test_controller_layer_mask_and_curve_parameters();
    test_controller_validation_and_round_trip();
    test_crossfade_finishes_without_pose_updates();
    test_reversal_keeps_the_requested_loop_and_speed();
    test_phase_sync_follows_an_interrupted_target();
    test_reversing_a_crossfade_nobody_saw_drops_it();
    test_override_layer_crossfade_keeps_the_base_for_a_bone_it_lacks();
    test_blend_space_narrowing_away_from_a_bone_puts_it_at_rest();
    test_directional_blend_with_opposite_points();
    test_override_layer_keeps_base_curves();
    test_seek_starts_root_motion_over();
    test_switch_travel_does_not_depend_on_the_frame_rate();
    test_crossfade_target_moves_on_its_first_step();
    test_culled_character_returns_without_a_jump();
    test_loop_count_matches_the_time_on_the_clip();
    test_turning_walk_root_motion_follows_the_clip_path();
    test_unmapped_root_moves_the_entity_through_the_hips();
    test_controller_restarts_a_state_sharing_the_clip();
    test_controller_edit_keeps_the_playing_time();
    test_controller_edit_dropping_a_layer_clears_it();
    test_controller_exit_time_one_waits_for_the_cycle_end();
    test_controller_trigger_reaches_every_layer();
    test_controller_numbers_do_not_set_booleans();
    test_controller_exit_time_above_one_on_loops();
    test_controller_keeps_script_layer_weight_and_reload_state();
    test_controller_setters_respect_parameter_types();
    test_controller_survives_binary_archives();

    g_pool = nullptr;

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("animation", run_animation_suite)
