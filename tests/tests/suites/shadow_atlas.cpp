/*
 * Validation suite for the shadow atlas layout (shadow::shadow_atlas_allocator): where each light's shadow map sits.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "shadow atlas"
 *
 * What it pins:
 *   - every light gets the edge it asks for while they fit, in tiles inside the atlas that never overlap;
 *   - the atlas is the smallest that holds its tiles, and grows when lights arrive;
 *   - tiles do not move from frame to frame while nothing changes (no generation bump);
 *   - past the largest atlas the least important lights step down first, and every light keeps a tile;
 *   - a new light under pressure takes its share at once, and only lights over their share give way;
 *   - a stepped-down light gets its resolution back only after regain_frames frames of wanting it, a change of
 *     ranking moves nothing before that, and the atlas shrinks only after shrink_frames frames of fitting a smaller one;
 *   - views that rank the lights differently within a frame do not undo each other;
 *   - a light no view asked for in the previous frame gives its tile back;
 *   - an edited resolution applies at once.
 */

#include "../tests.h"

#include <engine/rendering/shadow_atlas.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace unravel;
using namespace unravel::shadow;

namespace
{

int g_atlas_checks = 0;
int g_atlas_failures = 0;

void atlas_check(bool condition, const std::string& what)
{
    ++g_atlas_checks;
    if(!condition)
    {
        ++g_atlas_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

/// Requests for @p count lights (owners 1..count) of edge @p size, the higher owner the more important.
auto make_requests(std::uint32_t count, std::uint16_t size) -> std::vector<shadow_atlas_request>
{
    std::vector<shadow_atlas_request> requests;
    for(std::uint32_t i = 1; i <= count; ++i)
    {
        requests.push_back({std::uintptr_t(i), size, float(i)});
    }
    return requests;
}

/// Whether every requested light has a tile inside the atlas and no two tiles overlap.
auto is_valid_layout(const shadow_atlas_allocator& atlas, const std::vector<shadow_atlas_request>& requests) -> bool
{
    std::vector<shadow_atlas_tile> tiles;
    for(const auto& request : requests)
    {
        const auto tile = atlas.find(request.owner);
        if(!tile || tile->x + tile->size > atlas.get_size() || tile->y + tile->size > atlas.get_size())
        {
            return false;
        }
        tiles.push_back(*tile);
    }
    for(size_t i = 0; i < tiles.size(); ++i)
    {
        for(size_t j = i + 1; j < tiles.size(); ++j)
        {
            const auto& a = tiles[i];
            const auto& b = tiles[j];
            const bool apart = a.x + a.size <= b.x || b.x + b.size <= a.x || a.y + a.size <= b.y || b.y + b.size <= a.y;
            if(!apart)
            {
                return false;
            }
        }
    }
    return true;
}

void test_fits_and_grows()
{
    std::printf("test_fits_and_grows\n");
    shadow_atlas_allocator atlas;
    std::uint64_t frame = 1;
    const auto one = make_requests(1, 1024);
    atlas.reserve(frame, one);
    atlas_check(atlas.get_size() == 1024, "one 1024 light takes a 1024 atlas");
    atlas_check(atlas.find(1) && atlas.find(1)->size == 1024, "and keeps its edge");
    const auto four = make_requests(4, 1024);
    atlas.reserve(++frame, four);
    atlas_check(atlas.get_size() == 2048, "four 1024 lights take a 2048 atlas");
    atlas_check(is_valid_layout(atlas, four), "in separate tiles inside it");
    const auto generation = atlas.get_generation();
    const auto first_tile = *atlas.find(1);
    for(int i = 0; i < 10; ++i)
    {
        atlas_check(!atlas.reserve(++frame, four), "an unchanged set keeps its layout");
    }
    atlas_check(atlas.get_generation() == generation && *atlas.find(1) == first_tile, "and its tiles stay put");
    const auto five = make_requests(5, 1024);
    atlas_check(atlas.reserve(++frame, five), "a fifth light changes the layout");
    atlas_check(atlas.get_size() == 4096 && is_valid_layout(atlas, five), "the atlas grows to 4096 and holds all five");
}

void test_pressure_steps_down_least_important()
{
    std::printf("test_pressure_steps_down_least_important\n");
    shadow_atlas_allocator atlas;
    std::uint64_t frame = 1;
    const auto seventeen = make_requests(17, 1024);
    atlas.reserve(frame, seventeen);
    atlas_check(atlas.get_size() == shadow_atlas_allocator::max_atlas_size, "17 lights of 1024 fill the largest atlas");
    atlas_check(is_valid_layout(atlas, seventeen), "every light keeps a tile");
    // 17M texels for a 16M atlas: two lights have to step down, the two least important.
    atlas_check(atlas.find(1)->size == 512 && atlas.find(2)->size == 512, "the two least important step down to 512");
    atlas_check(atlas.find(3)->size == 1024 && atlas.find(17)->size == 1024, "the rest keep 1024");

    // The most important light goes. It keeps its tile through the next frame (a view may still ask for it); from the
    // frame after, the stepped-down lights have room and wait regain_frames frames before their resolution comes back.
    std::vector<shadow_atlas_request> sixteen = seventeen;
    sixteen.pop_back();
    atlas.reserve(++frame, sixteen);
    atlas_check(atlas.find(1)->size == 512, "the light stays stepped down while the departed light keeps its tile");
    for(std::uint32_t i = 1; i < shadow_atlas_allocator::regain_frames; ++i)
    {
        atlas.reserve(++frame, sixteen);
        atlas_check(atlas.find(1)->size == 512, "the light stays stepped down while the room is new");
    }
    atlas.reserve(++frame, sixteen);
    atlas_check(atlas.find(1)->size == 1024 && atlas.find(2)->size == 1024,
                "and gets its resolution back after regain_frames frames");
    atlas_check(is_valid_layout(atlas, sixteen), "in a valid layout");

    shadow_atlas_allocator crowded;
    const auto many = make_requests(1024, 1024);
    crowded.reserve(1, many);
    atlas_check(is_valid_layout(crowded, many), "1024 lights all keep a tile");
    atlas_check(crowded.find(1)->size == shadow_atlas_allocator::min_tile_size, "at the smallest edge");
}

void test_newcomer_takes_share()
{
    std::printf("test_newcomer_takes_share\n");
    shadow_atlas_allocator atlas;
    std::uint64_t frame = 1;
    auto requests = make_requests(16, 1024);
    atlas.reserve(frame, requests);
    requests.push_back({std::uintptr_t(17), 1024, 0.5f});
    atlas.reserve(++frame, requests);
    // The newcomer is the least important: it and light 1 share the room one more light needs.
    atlas_check(atlas.find(17)->size == 512 && atlas.find(1)->size == 512, "a new light under pressure takes its share");
    atlas_check(atlas.find(2)->size == 1024 && atlas.find(16)->size == 1024, "only the lights over their share give way");
    atlas_check(is_valid_layout(atlas, requests), "in a valid layout");
}

void test_ranking_change_waits()
{
    std::printf("test_ranking_change_waits\n");
    shadow_atlas_allocator atlas;
    std::uint64_t frame = 1;
    auto requests = make_requests(17, 1024);
    atlas.reserve(frame, requests);
    // Light 1 held 512 as the least important; now it is the most important, and light 3 is over its share.
    requests[0].importance = 100.0f;
    const auto generation = atlas.get_generation();
    for(std::uint32_t i = 1; i < shadow_atlas_allocator::regain_frames; ++i)
    {
        atlas.reserve(++frame, requests);
    }
    atlas_check(atlas.get_generation() == generation && atlas.find(1)->size == 512,
                "nothing moves while the new ranking is younger than regain_frames");
    atlas.reserve(++frame, requests);
    atlas_check(atlas.find(1)->size == 1024 && atlas.find(3)->size == 512,
                "then the light gets its share from the light now least important");
    atlas_check(atlas.find(2)->size == 512 && is_valid_layout(atlas, requests), "in a valid layout");
}

void test_views_agree()
{
    std::printf("test_views_agree\n");
    // Two views a frame rank 17 lights in opposite orders (a camera and a probe face). Taking the most importance any
    // view gives a light, they agree; each view ranking by its own would step down what the other just stepped up.
    shadow_atlas_allocator atlas;
    const auto view_a = make_requests(17, 1024);
    auto view_b = make_requests(17, 1024);
    for(auto& request : view_b)
    {
        request.importance = 18.0f - request.importance;
    }
    std::uint64_t frame = 0;
    std::uint32_t changes = 0;
    for(std::uint32_t i = 0; i < 3 * shadow_atlas_allocator::regain_frames; ++i)
    {
        ++frame;
        changes += atlas.reserve(frame, view_a) ? 1u : 0u;
        changes += atlas.reserve(frame, view_b) ? 1u : 0u;
    }
    atlas_check(changes <= 2, "the layout settles after the first frame and at most one swap");
    atlas_check(is_valid_layout(atlas, view_a), "in a valid layout");
    const auto generation = atlas.get_generation();
    for(std::uint32_t i = 0; i < 3 * shadow_atlas_allocator::regain_frames; ++i)
    {
        ++frame;
        atlas.reserve(frame, view_a);
        atlas.reserve(frame, view_b);
    }
    atlas_check(atlas.get_generation() == generation, "and then holds");
}

void test_eviction_and_shrink()
{
    std::printf("test_eviction_and_shrink\n");
    shadow_atlas_allocator atlas;
    std::uint64_t frame = 1;
    atlas.reserve(frame, make_requests(5, 1024));
    atlas_check(atlas.get_size() == 4096, "five lights take 4096");
    const auto one = make_requests(1, 1024);
    atlas.reserve(++frame, one);
    atlas_check(atlas.find(5).has_value(), "a light asked for in the previous frame keeps its tile");
    atlas.reserve(++frame, one);
    atlas_check(!atlas.find(5).has_value() && atlas.get_tile_count() == 1, "a light no view asked for gives its tile back");
    // That frame was the first to fit a smaller atlas.
    for(std::uint32_t i = 2; i < shadow_atlas_allocator::shrink_frames; ++i)
    {
        atlas.reserve(++frame, one);
    }
    atlas_check(atlas.get_size() == 4096, "the atlas keeps its size while the smaller fit is new");
    atlas.reserve(++frame, one);
    atlas_check(atlas.get_size() == 1024 && is_valid_layout(atlas, one), "and shrinks after shrink_frames frames");
}

void test_edited_resolution()
{
    std::printf("test_edited_resolution\n");
    shadow_atlas_allocator atlas;
    std::uint64_t frame = 1;
    atlas.reserve(frame, make_requests(2, 1024));
    auto edited = make_requests(2, 1024);
    edited[0].size = 256;
    atlas.reserve(++frame, edited);
    atlas_check(atlas.find(1)->size == 256, "a lowered resolution applies at once");
    edited[0].size = 2048;
    atlas.reserve(++frame, edited);
    atlas_check(atlas.find(1)->size == 2048, "a raised resolution applies at once");
    atlas_check(is_valid_layout(atlas, edited), "in a valid layout");
    atlas.release(1);
    atlas_check(!atlas.find(1).has_value() && atlas.find(2).has_value(), "a released light gives its tile back alone");
}

void test_random_frames_keep_valid_layouts()
{
    std::printf("test_random_frames_keep_valid_layouts\n");
    // Lights come and go, change resolution and importance, and several views a frame ask for overlapping sets: after
    // every reservation each asking light has a tile inside the atlas, and no two tiles overlap.
    shadow_atlas_allocator atlas;
    std::uint32_t seed = 12345u;
    const auto next = [&seed](std::uint32_t range) -> std::uint32_t
    {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) % range;
    };
    constexpr std::uint32_t light_pool = 64;
    std::vector<std::uint16_t> sizes(light_pool);
    std::vector<float> importances(light_pool);
    for(std::uint32_t i = 0; i < light_pool; ++i)
    {
        sizes[i] = std::uint16_t(128u << next(5));
        importances[i] = float(next(1000)) / 1000.0f;
    }
    int invalid_frames = 0;
    for(std::uint64_t frame = 1; frame <= 3000; ++frame)
    {
        if(next(10) == 0)
        {
            sizes[next(light_pool)] = std::uint16_t(128u << next(5));
        }
        for(auto& importance : importances)
        {
            importance = std::max(0.0f, importance + (float(next(200)) - 100.0f) / 2000.0f);
        }
        const std::uint32_t views = 1 + next(3);
        for(std::uint32_t view = 0; view < views; ++view)
        {
            std::vector<shadow_atlas_request> requests;
            const std::uint32_t first = next(light_pool);
            const std::uint32_t count = 8 + next(48);
            for(std::uint32_t i = 0; i < count; ++i)
            {
                const std::uint32_t light = (first + i) % light_pool;
                requests.push_back({std::uintptr_t(light + 1), sizes[light], importances[light] * float(1 + view)});
            }
            atlas.reserve(frame, requests);
            invalid_frames += is_valid_layout(atlas, requests) ? 0 : 1;
        }
    }
    atlas_check(invalid_frames == 0, "every reservation of 3000 random frames leaves a valid layout (" +
                                         std::to_string(invalid_frames) + " invalid)");
}

void test_placement_serials()
{
    std::printf("test_placement_serials\n");
    shadow_atlas_allocator atlas;
    std::uint64_t frame = 1;
    const std::vector<shadow_atlas_request> both{{1, 512, 1.0f}, {2, 512, 0.5f}};
    atlas.reserve(frame, both);
    const std::uint64_t first = atlas.find_placement(1);
    atlas_check(first != 0 && atlas.find_placement(2) != 0 && first != atlas.find_placement(2),
                "every placed tile has a serial of its own");
    atlas_check(atlas.find_placement(99) == 0, "an owner without a tile has none");
    atlas.reserve(++frame, both);
    atlas_check(atlas.find_placement(1) == first, "a tile that stays put keeps its serial");
    // Light 1 leaves the view for two frames: its square is taken back, and a newcomer can render into it.
    const std::vector<shadow_atlas_request> without_first{{2, 512, 0.5f}, {3, 512, 0.25f}};
    atlas.reserve(++frame, without_first);
    atlas.reserve(++frame, without_first);
    atlas_check(atlas.find_placement(1) == 0, "a light no view asked for loses its tile");
    const std::vector<shadow_atlas_request> all{{1, 512, 1.0f}, {2, 512, 0.5f}, {3, 512, 0.25f}};
    atlas.reserve(++frame, all);
    const std::uint64_t again = atlas.find_placement(1);
    atlas_check(again != 0 && again != first, "a returning light gets a new serial, even on its old square");
}

auto run_shadow_atlas_suite(rtti::context& /*ctx*/) -> int
{
    g_atlas_checks = 0;
    g_atlas_failures = 0;
    test_fits_and_grows();
    test_pressure_steps_down_least_important();
    test_newcomer_takes_share();
    test_ranking_change_waits();
    test_views_agree();
    test_eviction_and_shrink();
    test_edited_resolution();
    test_random_frames_keep_valid_layouts();
    test_placement_serials();
    std::printf("shadow atlas: %d checks, %d failures\n", g_atlas_checks, g_atlas_failures);
    return g_atlas_failures;
}

} // namespace

REGISTER_TEST_SUITE("shadow atlas", run_shadow_atlas_suite)
