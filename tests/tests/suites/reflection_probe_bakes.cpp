/*
 * Validation suite for how reflection probe bakes share the frames.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "reflection probe bakes"
 *
 * What it pins:
 *   - probe_face_budget gives a frame the faces it starts with, at least one, and refuses a face past them;
 *   - every probe baking at once (a scene loading, play starting) renders no more faces in a frame than the
 *     budget, one probe after another, so each probe's reflection arrives whole as early as the budget allows;
 *   - probes without a capture take a frame's faces first, then bakes asked for whole, then refreshes;
 *   - a refresh renders no more than its own Faces Per Frame, and no more than the frame has left;
 *   - a probe the view does not see takes no faces, and no face starts once the frame has no render passes left
 *     for one;
 *   - a render outside the frame's budget (an asset preview) bakes a new probe whole in one render, even with the
 *     frame's budget spent;
 *   - where the GI reflections own a view, only environment probes are baked there; a probe some view left out
 *     and none used is set aside as if disabled (no bake waits on it, its capture is released), a probe also
 *     used by another view is not, and a set-aside probe a view uses again bakes whole;
 *   - the graphics settings' face budget round trips through a settings document, and a document without it
 *     leaves the value as it was.
 *
 * The frames run through reflection_probe_system::bake_due_probes, the driver deferred::build_reflections runs,
 * with steps that count faces instead of rendering them.
 */

#include "../tests.h"

#include <engine/meta/settings/settings.hpp>
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/ecs/systems/reflection_probe_system.h>
#include <engine/rendering/reflection_probe.h>
#include <engine/settings/settings.h>
#include <serialization/sparse_document.h>

#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace unravel::test;

using namespace unravel;

namespace
{

using probe_list = std::vector<std::unique_ptr<reflection_probe_component>>;

auto make_probes(std::size_t count) -> probe_list
{
    probe_list probes;
    for(std::size_t i = 0; i < count; ++i)
    {
        probes.push_back(std::make_unique<reflection_probe_component>());
    }
    return probes;
}

void set_method(reflection_probe_component& probe_comp, reflect_method method)
{
    auto probe = probe_comp.get_probe();
    probe.method = method;
    probe_comp.set_probe(probe);
}

/// The faces each probe rendered in one view.
using frame_faces = std::vector<std::size_t>;

/// What a view is like for the bakes of one frame.
struct view_conditions
{
    /// The GI's traced reflections own the view.
    bool gi_owns_reflections = false;
    /// The faces the frame's render passes have room for.
    std::size_t faces_with_room = std::numeric_limits<std::size_t>::max();
    /// The probes the view sees, by index; empty: every probe.
    std::vector<bool> sees;
};

/// One view's bakes with the probe system's driver, the steps counting faces and marking the captures that completed.
auto bake_view(probe_list& probes, probe_face_budget& budget, std::uint64_t frame, const view_conditions& view = {})
    -> frame_faces
{
    std::vector<probe_bake_candidate> candidates;
    for(std::size_t i = 0; i < probes.size(); ++i)
    {
        candidates.push_back({static_cast<entt::entity>(i), probes[i].get()});
    }
    const auto index_of = [](const probe_bake_candidate& candidate)
    {
        return static_cast<std::size_t>(entt::to_integral(candidate.entity));
    };

    frame_faces rendered(probes.size(), 0);
    std::size_t room = view.faces_with_room;
    probe_bake_steps steps;
    steps.sees = [&](const probe_bake_candidate& candidate)
    {
        return view.sees.empty() || view.sees[index_of(candidate)];
    };
    steps.has_room_for_face = [&]
    {
        return room > 0;
    };
    steps.begin_probe = [](const probe_bake_candidate& /*candidate*/) {};
    steps.render_face = [&](const probe_bake_candidate& candidate, std::uint32_t /*face*/)
    {
        ++rendered[index_of(candidate)];
        --room;
    };
    steps.end_probe = [](const probe_bake_candidate& candidate, bool is_complete)
    {
        if(is_complete)
        {
            candidate.probe->mark_capture_ready();
        }
    };
    reflection_probe_system::bake_due_probes(candidates, budget, view.gi_owns_reflections, frame, steps);
    return rendered;
}

/// The probes' frame updates, as reflection_probe_system::on_frame_update runs them.
void update_probes(probe_list& probes)
{
    for(auto& probe : probes)
    {
        probe->update(1.0f / 60.0f);
    }
}

/// One frame of bakes: the budget refilled, one view's bakes, then the probes' updates.
auto run_frame(probe_list& probes,
               probe_face_budget& budget,
               std::uint32_t faces_per_frame,
               std::uint64_t frame,
               const view_conditions& view = {}) -> frame_faces
{
    budget.begin_frame(faces_per_frame);
    frame_faces rendered = bake_view(probes, budget, frame, view);
    update_probes(probes);
    return rendered;
}

auto count_faces(const frame_faces& rendered) -> std::size_t
{
    std::size_t total = 0;
    for(const std::size_t faces : rendered)
    {
        total += faces;
    }
    return total;
}

/// Bakes every probe until it has a capture and nothing pending.
void settle(probe_list& probes, probe_face_budget& budget, std::uint64_t& frame)
{
    for(int i = 0; i < 64; ++i)
    {
        run_frame(probes, budget, 6, ++frame);
    }
}

void test_budget()
{
    std::printf("test_budget\n");
    probe_face_budget budget;
    budget.begin_frame(3);
    check_eq(budget.get_faces_left(), 3, "a frame starts with the faces it is given");
    check(budget.try_take() && budget.try_take() && budget.try_take(), "it gives them");
    check(!budget.try_take() && !budget.has_faces_left(), "and refuses a face past them");
    budget.begin_frame(0);
    check_eq(budget.get_faces_left(), 1, "a frame always has a face, so bakes finish");
    budget.begin_frame(6);
    check_eq(budget.get_faces_left(), 6, "the next frame starts over");
    check_eq(settings{}.graphics.reflection_probe_faces_per_frame, probe_face_budget::default_faces_per_frame,
             "the project setting starts at the budget's default");
}

void test_every_probe_baking_at_once()
{
    std::printf("test_every_probe_baking_at_once\n");
    // Eleven probes, as a scene with eleven probes loads or enters play: each wants a whole first bake.
    auto probes = make_probes(11);
    probe_face_budget budget;
    std::uint64_t frame = 0;

    const frame_faces first = run_frame(probes, budget, 6, ++frame);
    check_eq(count_faces(first), 6, "the first frame renders the budget's six faces, not 66");
    check(first[0] == 6 && probes[0]->has_capture(), "all six go to one probe, whose reflection is whole at once");
    check(!probes[1]->has_capture(), "the next probe waits for the next frame");

    std::size_t frames = 1;
    bool within_budget = true;
    while(frames < 40)
    {
        bool all_captured = true;
        for(const auto& probe : probes)
        {
            all_captured &= probe->has_capture() && !probe->is_dirty();
        }
        if(all_captured)
        {
            break;
        }
        within_budget &= count_faces(run_frame(probes, budget, 6, ++frame)) <= 6;
        ++frames;
    }
    check(within_budget, "no frame renders more faces than the budget");
    check_eq(frames, 11, "eleven probes take eleven frames at six faces a frame");

    auto slow = make_probes(3);
    frames = 0;
    while(frames < 40 && !slow[2]->has_capture())
    {
        check(count_faces(run_frame(slow, budget, 2, ++frame)) <= 2, "a budget of two renders two faces a frame");
        ++frames;
    }
    check_eq(frames, 9, "three probes take nine frames at two faces a frame");
}

void test_bake_order()
{
    std::printf("test_bake_order\n");
    auto probes = make_probes(3);
    probe_face_budget budget;
    std::uint64_t frame = 0;
    settle(probes, budget, frame);
    require(probes[0]->has_capture() && probes[1]->has_capture() && !probes[0]->is_dirty(),
            "the probes start baked and at rest");

    // Probe 0 refreshes time-sliced, probe 1 is asked to bake whole, probe 2 loses its capture.
    probes[0]->set_faces_per_frame(1);
    probes[0]->mark_dirty(false);
    probes[1]->mark_dirty(true);
    probes[2]->release_resources();
    probes[2]->mark_dirty(true);
    check(reflection_probe_system::get_bake_rank(*probes[2]) == probe_bake_rank::first_capture &&
              reflection_probe_system::get_bake_rank(*probes[1]) == probe_bake_rank::whole_bake &&
              reflection_probe_system::get_bake_rank(*probes[0]) == probe_bake_rank::refresh,
          "a probe without a capture ranks first, a whole bake next, a refresh last");

    const frame_faces first = run_frame(probes, budget, 6, ++frame);
    check(first[2] == 6 && first[1] == 0 && first[0] == 0, "the probe without a capture takes the whole frame");
    const frame_faces second = run_frame(probes, budget, 6, ++frame);
    check(second[1] == 6 && second[0] == 0, "the whole bake comes next");
    const frame_faces third = run_frame(probes, budget, 6, ++frame);
    check_eq(third[0], 1, "the refresh renders its own one face a frame");
}

void test_refresh_slices()
{
    std::printf("test_refresh_slices\n");
    auto probes = make_probes(1);
    probe_face_budget budget;
    std::uint64_t frame = 0;
    settle(probes, budget, frame);

    probes[0]->set_faces_per_frame(3);
    probes[0]->mark_dirty(false);
    check_eq(run_frame(probes, budget, 6, ++frame)[0], 3, "a refresh renders its Faces Per Frame with room to spare");
    check_eq(run_frame(probes, budget, 2, ++frame)[0], 2, "and no more than the frame has left");
    check_eq(run_frame(probes, budget, 6, ++frame)[0], 1, "it finishes with the face it still needs");
    check(probes[0]->has_capture() && !probes[0]->is_dirty(), "and rests");
}

void test_unseen_probes_and_render_pass_room()
{
    std::printf("test_unseen_probes_and_render_pass_room\n");
    auto probes = make_probes(3);
    probe_face_budget budget;
    std::uint64_t frame = 0;

    view_conditions view;
    view.sees = {false, true, true};
    const frame_faces seen = run_frame(probes, budget, 6, ++frame, view);
    check(seen[0] == 0 && seen[1] == 6, "a probe the view does not see takes no faces, the next one does");

    auto more = make_probes(3);
    probe_face_budget whole = probe_face_budget::unlimited();
    view_conditions tight;
    tight.faces_with_room = 8;
    const frame_faces rendered = bake_view(more, whole, ++frame, tight);
    check_eq(count_faces(rendered), 8, "no face starts once the frame's render passes have no room for one");
    check(rendered[0] == 6 && more[0]->has_capture() && rendered[1] == 2 && !more[1]->has_capture(),
          "the first probe completes, the second keeps its two faces for a later frame");
}

void test_preview_bakes_whole()
{
    std::printf("test_preview_bakes_whole\n");
    probe_face_budget frame_budget;
    frame_budget.begin_frame(1);
    check(frame_budget.try_take() && !frame_budget.has_faces_left(), "the views spent the frame's budget");

    // An asset preview's probes are new, and its picture is taken in one render.
    auto preview = make_probes(2);
    probe_face_budget own = probe_face_budget::unlimited();
    const frame_faces rendered = bake_view(preview, own, 1);
    check(rendered[0] == 6 && rendered[1] == 6, "a render outside the frame's budget bakes every face at once");
    check(preview[0]->has_capture() && preview[1]->has_capture(), "so its captures are complete within the render");
    check(own.has_faces_left(), "and its own budget does not run out");
}

void test_gi_reflections_leave_static_probes_out()
{
    std::printf("test_gi_reflections_leave_static_probes_out\n");
    reflection_probe environment;
    environment.method = reflect_method::environment;
    reflection_probe local;
    local.method = reflect_method::static_only;
    check(reflection_probe_system::is_used_in_view(environment, false) &&
              reflection_probe_system::is_used_in_view(local, false),
          "without GI reflections a view bakes and draws every probe");
    check(reflection_probe_system::is_used_in_view(environment, true),
          "with them the environment probes stay, for the rays that miss");
    check(!reflection_probe_system::is_used_in_view(local, true), "and the static probes are left out");

    // Probe 0 is an environment probe, probe 1 a static one; both want their first bake.
    auto probes = make_probes(2);
    set_method(*probes[0], reflect_method::environment);
    set_method(*probes[1], reflect_method::static_only);
    probe_face_budget budget;
    std::uint64_t frame = 0;
    view_conditions gi_view;
    gi_view.gi_owns_reflections = true;
    budget.begin_frame(12);
    const frame_faces rendered = bake_view(probes, budget, ++frame, gi_view);
    check(rendered[0] == 6 && rendered[1] == 0, "a GI view bakes the environment probe and not the static one");

    // The probe pass reports each probe's use; the frame update settles it.
    probes[0]->note_view_use(true);
    probes[1]->note_view_use(false);
    update_probes(probes);
    check(!probes[0]->is_set_aside() && probes[1]->is_set_aside(), "the static probe is set aside");
    check(!probes[1]->is_dirty() && !probes[1]->has_capture(),
          "as a disabled probe: no bake waits on it, and it holds no capture");

    probes[1]->mark_dirty(true);
    update_probes(probes);
    check(probes[1]->is_set_aside() && !probes[1]->is_dirty(),
          "a bake asked for while it is set aside (play starting) does not wait on it, with no new report");

    probes[1]->note_view_use(false);
    probes[1]->note_view_use(true);
    update_probes(probes);
    check(!probes[1]->is_set_aside() && probes[1]->is_dirty() && probes[1]->is_whole_bake(),
          "a view that uses it again brings it back with a whole bake, even with another view leaving it out");

    budget.begin_frame(6);
    check_eq(bake_view(probes, budget, ++frame)[1], 6, "and a view without GI reflections bakes it");
}

void test_settings_round_trip()
{
    std::printf("test_settings_round_trip\n");
    settings saved;
    saved.graphics.reflection_probe_faces_per_frame = 3;
    const auto document = serialization::save_to_document(saved, "settings");
    settings loaded;
    check(serialization::load_from_document(document, "settings", loaded), "a settings document loads");
    check_eq(loaded.graphics.reflection_probe_faces_per_frame, 3, "the face budget round trips");

    const std::string key = "\"reflection_probe_faces_per_frame\"";
    std::string older = serialization::save_to_document(settings{}, "settings");
    const auto at = older.find(key);
    require(at != std::string::npos, "the document names the face budget");
    older.replace(at, key.size(), "\"unknown_setting\"");
    settings defaults;
    defaults.graphics.reflection_probe_faces_per_frame = 9;
    check(serialization::load_from_document(older, "settings", defaults), "a document without it loads");
    check_eq(defaults.graphics.reflection_probe_faces_per_frame, 9, "and leaves the value it had");
}

void run_reflection_probe_bakes_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_budget());
    RUN_CASE(test_every_probe_baking_at_once());
    RUN_CASE(test_bake_order());
    RUN_CASE(test_refresh_slices());
    RUN_CASE(test_unseen_probes_and_render_pass_room());
    RUN_CASE(test_preview_bakes_whole());
    RUN_CASE(test_gi_reflections_leave_static_probes_out());
    RUN_CASE(test_settings_round_trip());
}

} // namespace

REGISTER_TEST_SUITE("reflection probe bakes", run_reflection_probe_bakes_suite)
