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
 * The IK solvers have their own suite - see ik.cpp.
 */

#include "../tests.h"

#include <engine/animation/animation_blend_space.h>
#include <engine/animation/animation_player.h>
#include <engine/threading/threader.h>
#include <uuid/uuid.h>

#include <logging/logging.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
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

auto make_pose_node(animation_pose& pose, size_t index, const std::string& name, float x) -> animation_pose::node&
{
    auto& node = pose.nodes.emplace_back();
    node.desc.index = index;
    node.desc.name = name;
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

auto step_player(animation_player& player, float dt, bool force = false) -> sampled_frame
{
    static const animation_pose empty_ref_pose{};
    sampled_frame frame;
    frame.updated = player.update_time(seconds_t{dt}, force);
    if(frame.updated)
    {
        player.update_poses(empty_ref_pose,
                            animation_retargeting_mode::index_based,
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

void test_pairwise_blend_merges_sorted_nodes()
{
    std::printf("test_pairwise_blend_merges_sorted_nodes\n");
    animation_pose pose_a;
    make_pose_node(pose_a, 0, "n0", 0.0f);
    make_pose_node(pose_a, 1, "n1", 2.0f);
    animation_pose pose_b;
    make_pose_node(pose_b, 0, "n0", 10.0f);
    make_pose_node(pose_b, 2, "n2", 4.0f);

    animation_pose result;
    blend_poses(pose_a, pose_b, 0.5f, result);

    check(result.nodes.size() == 3, "union of both poses");
    check_near(result.nodes[0].transform.get_position().x, 5.0f, 1e-4f, "shared node blended");
    check_near(result.nodes[1].transform.get_position().x, 2.0f, 1e-4f, "pose_a-only node copied");
    check_near(result.nodes[2].transform.get_position().x, 4.0f, 1e-4f, "pose_b-only node copied");
}

void test_multiway_blend_weighted_average_and_names()
{
    std::printf("test_multiway_blend_weighted_average_and_names\n");
    std::vector<animation_pose> poses(2);
    make_pose_node(poses[0], 0, "root", 0.0f);
    make_pose_node(poses[1], 0, "root", 10.0f);
    const std::vector<float> weights{1.0f, 3.0f};

    animation_pose result;
    blend_poses(poses, weights, result);

    check(result.nodes.size() == 1, "single shared node");
    check_near(result.nodes[0].transform.get_position().x, 7.5f, 1e-4f, "weighted average position");
    // Name-based retargeting resolves bones by desc.name - the multiway blend
    // must carry it (regression: it used to emit index-only descs).
    check(result.nodes[0].desc.name == "root", "node name carried through multiway blend");
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

    std::vector<std::pair<asset_handle<animation_clip>, float>> out;

    // Regression: a parameter below the range fell through to the LAST
    // interval and picked the wrong clip.
    blend_space.compute_blend({-5.0f}, out);
    float weight_a = 0.0f;
    float weight_sum = 0.0f;
    for(const auto& clip_weight : out)
    {
        weight_sum += clip_weight.second;
        if(clip_weight.first.get().get() == clip_a.get())
        {
            weight_a += clip_weight.second;
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

    blend_space_def grid;
    grid.add_clip({0.0f, 0.0f}, make_clip_handle(clip_00));
    grid.add_clip({1.0f, 0.0f}, make_clip_handle(clip_10));
    grid.add_clip({0.0f, 1.0f}, make_clip_handle(clip_01));
    grid.add_clip({1.0f, 1.0f}, make_clip_handle(clip_11));

    std::vector<std::pair<asset_handle<animation_clip>, float>> out;

    // Outside the grid: weights must clamp, never extrapolate negative.
    grid.compute_blend({2.0f, 0.5f}, out);
    float weight_sum = 0.0f;
    bool any_negative = false;
    for(const auto& clip_weight : out)
    {
        weight_sum += clip_weight.second;
        any_negative |= clip_weight.second < -1e-6f;
    }
    check(!out.empty(), "outside-grid params still produce clips");
    check(!any_negative, "no negative weights outside the grid");
    check_near(weight_sum, 1.0f, 1e-4f, "outside-grid weights sum to 1");

    // A sparse cell (missing corner) falls back to the nearest point instead
    // of silently freezing the pose with an empty result.
    blend_space_def sparse;
    sparse.add_clip({0.0f, 0.0f}, make_clip_handle(clip_00));
    sparse.add_clip({1.0f, 0.0f}, make_clip_handle(clip_10));
    sparse.add_clip({0.0f, 1.0f}, make_clip_handle(clip_01));
    sparse.compute_blend({0.9f, 0.1f}, out);
    check(out.size() == 1, "sparse cell falls back to a single clip");
    check(!out.empty() && out.front().first.get().get() == clip_10.get(), "sparse cell picks the nearest point");

    // A degenerate axis (single unique value) used to underflow size_t and
    // read out of bounds.
    blend_space_def degenerate;
    degenerate.add_clip({0.0f, 0.0f}, make_clip_handle(clip_00));
    degenerate.add_clip({1.0f, 0.0f}, make_clip_handle(clip_10));
    degenerate.compute_blend({0.9f, 0.0f}, out);
    check(out.size() == 1, "degenerate axis falls back to a single clip");
    check(!out.empty() && out.front().first.get().get() == clip_10.get(), "degenerate axis picks the nearest point");
}

void test_root_motion_accumulates_across_loops()
{
    std::printf("test_root_motion_accumulates_across_loops\n");
    auto clip = make_linear_clip(0.0f, 1.0f, 1.0f, true /*root_motion*/);
    auto handle = make_clip_handle(clip);

    animation_player player;
    player.blend_to(0, handle, seconds_t{0.0f}, true /*loop*/);
    player.play();

    // First sample establishes the baseline - no motion yet.
    auto frame = step_player(player, 0.25f);
    check_near(frame.motion.root_transform_delta.get_translation().x, 0.0f, 1e-4f, "first sample has no delta");

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
    check_near(frame.motion.root_transform_delta.get_translation().x, 0.0f, 1e-4f, "replay first sample has no delta");
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
    keep_xz.root_position_node_name = "root";
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

} // namespace

// ---------------------------------------------------------------------------------

auto run_animation_suite(rtti::context& ctx) -> int
{
    g_pool = ctx.get_cached<threader>().pool.get();

    test_pairwise_blend_merges_sorted_nodes();
    test_multiway_blend_weighted_average_and_names();
    test_blend_space_actually_blends();
    test_blend_space_time_advances_and_loops();
    test_blend_space_1d_param_edges();
    test_blend_space_2d_edges();
    test_root_motion_accumulates_across_loops();
    test_root_motion_no_teleport_on_replay();
    test_phase_synced_blend_has_no_motion_spike();
    test_zero_duration_blend_and_finite_output();
    test_force_update_steps_while_paused();
    test_blend_to_null_clears_layer();
    test_retarget_blend_keeps_no_pop();
    test_root_motion_result_weight_blending();
    test_empty_tracks_have_sane_defaults();

    g_pool = nullptr;

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("animation", run_animation_suite)
