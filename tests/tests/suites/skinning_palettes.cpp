/*
 * Validation suite for the CPU side of the skinning palette buffer (instanced skinned draws).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "skinning palettes"
 *
 * Pins:
 *   - a bone packs into three rows that the shaders turn back into the same matrix (mtxFromRows), and the rows
 *     transform a point exactly as the matrix does;
 *   - blending the rows of four bones with the bottom row (0, 0, 0, sum of weights) gives the matrix the
 *     uniform-palette shaders got by blending whole matrices, also for weights that do not sum to one;
 *   - palettes get consecutive rows, an empty palette gets no rows;
 *   - a model's palettes are appended once per frame however many views ask for them, its previous palettes are
 *     appended on the first request that wants them, and a palette without a matching previous pose reuses its
 *     current rows;
 *   - pack_pending (parallel past a threshold) writes every acquired palette into the rows acquire gave it;
 *   - begin_frame forgets the previous frame.
 *
 * Headless safe: nothing here creates the GPU buffer (only bind and flush do).
 */

#include "../tests.h"

#include <engine/rendering/model.h>
#include <engine/rendering/skinning_palette_buffer.h>

#include <math/math.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

constexpr float MATRIX_TOLERANCE = 1.0e-5f;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto matrices_near(const math::mat4& actual, const math::mat4& expected, float tolerance) -> bool
{
    for(int column = 0; column < 4; ++column)
    {
        for(int row = 0; row < 4; ++row)
        {
            if(std::abs(actual[column][row] - expected[column][row]) > tolerance)
            {
                return false;
            }
        }
    }
    return true;
}

/// A bone with rotation, non-uniform scale and translation, different per seed.
auto make_bone(float seed) -> math::mat4
{
    const math::mat4 translation = math::translate(math::mat4(1.0f), math::vec3(seed, -2.0f * seed, 0.5f + seed));
    const math::mat4 rotation = math::rotate(math::mat4(1.0f), 0.3f + seed, math::normalize(math::vec3(1.0f, seed, -0.5f)));
    const math::mat4 scale = math::scale(math::mat4(1.0f), math::vec3(1.0f + seed, 0.5f, 2.0f - seed * 0.1f));
    return translation * rotation * scale;
}

auto make_palette(uint32_t bones, float seed) -> pose_mat4
{
    pose_mat4 palette;
    for(uint32_t index = 0; index < bones; ++index)
    {
        palette.transforms.push_back(make_bone(seed + float(index) * 0.25f));
    }
    return palette;
}

/// The shader's mtxFromRows(r0, r1, r2, r3) in glm terms.
auto matrix_from_rows(const math::vec4& r0, const math::vec4& r1, const math::vec4& r2, const math::vec4& r3)
    -> math::mat4
{
    return glm::transpose(math::mat4(r0, r1, r2, r3));
}

void test_pack_round_trips()
{
    const math::mat4 input_bone = make_bone(0.7f);
    std::array<math::vec4, skinning_palette_rows::ROWS_PER_BONE> rows{};
    skinning_palette_rows::pack_bone(input_bone, rows.data());
    const math::mat4 actual_bone = skinning_palette_rows::unpack_bone(rows.data());
    check(actual_bone == input_bone, "unpack_bone(pack_bone(m)) == m exactly");
    const math::mat4 actual_shader = matrix_from_rows(rows[0], rows[1], rows[2], math::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    check(actual_shader == input_bone, "mtxFromRows of the packed rows rebuilds the bone");
}

void test_rows_transform_like_the_matrix()
{
    const math::mat4 input_bone = make_bone(1.3f);
    std::array<math::vec4, skinning_palette_rows::ROWS_PER_BONE> rows{};
    skinning_palette_rows::pack_bone(input_bone, rows.data());
    const math::vec4 input_point(0.25f, -1.5f, 3.0f, 1.0f);
    const math::vec4 expected = input_bone * input_point;
    const math::vec3 actual(math::dot(rows[0], input_point), math::dot(rows[1], input_point), math::dot(rows[2], input_point));
    check(math::all(math::epsilonEqual(actual, math::vec3(expected), MATRIX_TOLERANCE)), "row i dot p == (M p)[i]");
}

void test_row_blend_matches_matrix_blend()
{
    const std::array<math::mat4, 4> input_bones = {make_bone(0.1f), make_bone(0.9f), make_bone(1.7f), make_bone(2.5f)};
    // Four influences kept without renormalizing: the sum is below one, as the importer leaves it.
    const math::vec4 input_weights(0.5f, 0.25f, 0.125f, 0.0625f);
    math::mat4 expected(0.0f);
    std::array<math::vec4, 3> blended_rows{};
    for(size_t bone = 0; bone < input_bones.size(); ++bone)
    {
        expected += input_weights[int(bone)] * input_bones[bone];
        std::array<math::vec4, skinning_palette_rows::ROWS_PER_BONE> rows{};
        skinning_palette_rows::pack_bone(input_bones[bone], rows.data());
        for(size_t row = 0; row < rows.size(); ++row)
        {
            blended_rows[row] += input_weights[int(bone)] * rows[row];
        }
    }
    const float weight_sum = input_weights.x + input_weights.y + input_weights.z + input_weights.w;
    const math::mat4 actual = matrix_from_rows(blended_rows[0],
                                               blended_rows[1],
                                               blended_rows[2],
                                               math::vec4(0.0f, 0.0f, 0.0f, weight_sum));
    check(matrices_near(actual, expected, MATRIX_TOLERANCE), "blended rows + (0,0,0,sum w) == blended matrices");
}

void test_append_assigns_consecutive_rows()
{
    skinning_palette_rows rows;
    const pose_mat4 input_a = make_palette(3, 0.0f);
    const pose_mat4 input_b = make_palette(5, 1.0f);
    const uint32_t actual_a = rows.append(input_a.transforms);
    const uint32_t actual_empty = rows.append({});
    const uint32_t actual_b = rows.append(input_b.transforms);
    check(actual_a == 0, "first palette starts at row 0");
    check(actual_empty == skinning_palette_rows::INVALID_ROW, "an empty palette gets no rows");
    check(actual_b == 3 * skinning_palette_rows::ROWS_PER_BONE, "second palette starts after the first one's rows");
    check(rows.get_row_count() == 8 * skinning_palette_rows::ROWS_PER_BONE, "8 bones = 24 rows");
    const math::mat4 actual_bone = skinning_palette_rows::unpack_bone(rows.get_rows().data() + actual_b + 2 * 3);
    check(actual_bone == input_b.transforms[2], "the third bone of the second palette sits at its rows");
}

void test_acquire_appends_once_per_frame()
{
    skinning_palette_buffer buffer;
    buffer.begin_frame();
    const std::vector<pose_mat4> input_palettes = {make_palette(4, 0.0f), make_palette(2, 1.0f)};
    const int owner = 0;
    const auto first = buffer.acquire(&owner, input_palettes, nullptr);
    const std::vector<uint32_t> expected_rows(first.current.begin(), first.current.end());
    const auto second = buffer.acquire(&owner, input_palettes, nullptr);
    check(expected_rows.size() == 2, "one first row per palette");
    check(expected_rows.size() == 2 && expected_rows[0] == 0 && expected_rows[1] == 4 * 3, "palettes are consecutive");
    check(std::vector<uint32_t>(second.current.begin(), second.current.end()) == expected_rows, "a second view reuses the rows");
    check(second.previous.empty(), "no previous rows unless asked for");
}

void test_previous_rows()
{
    skinning_palette_buffer buffer;
    buffer.begin_frame();
    const std::vector<pose_mat4> input_current = {make_palette(4, 0.0f), make_palette(2, 1.0f), make_palette(3, 2.0f)};
    // Palette 1 changed its bone count, palette 2 has no previous pose at all.
    const std::vector<pose_mat4> input_previous = {make_palette(4, 5.0f), make_palette(1, 6.0f)};
    const int owner = 0;
    const auto shadow_view = buffer.acquire(&owner, input_current, nullptr);
    const std::vector<uint32_t> current_rows(shadow_view.current.begin(), shadow_view.current.end());
    const auto camera_view = buffer.acquire(&owner, input_current, &input_previous);
    check(std::vector<uint32_t>(camera_view.current.begin(), camera_view.current.end()) == current_rows,
          "asking for previous rows later keeps the current rows");
    check(camera_view.previous.size() == 3, "one previous row per palette");
    if(camera_view.previous.size() == 3)
    {
        check(camera_view.previous[0] == 9 * 3, "a matching previous palette is appended after the current ones");
        check(camera_view.previous[1] == current_rows[1], "another bone count falls back to the current rows");
        check(camera_view.previous[2] == current_rows[2], "a missing previous pose falls back to the current rows");
    }
}

void test_pack_pending_fills_the_rows()
{
    skinning_palette_buffer buffer;
    buffer.begin_frame();
    // More palettes than the parallel threshold, so the pool packs them.
    std::vector<std::vector<pose_mat4>> input_models;
    for(int model = 0; model < 40; ++model)
    {
        input_models.push_back({make_palette(3, float(model)), make_palette(2, float(model) + 0.5f)});
    }
    std::vector<uint32_t> first_rows;
    for(const auto& palettes : input_models)
    {
        const auto rows = buffer.acquire(&palettes, palettes, &palettes);
        first_rows.insert(first_rows.end(), rows.current.begin(), rows.current.end());
    }
    buffer.pack_pending();
    const auto actual_rows = buffer.get_rows();
    bool all_match = true;
    for(size_t model = 0; model < input_models.size(); ++model)
    {
        for(size_t palette = 0; palette < 2; ++palette)
        {
            const auto& bones = input_models[model][palette].transforms;
            for(size_t bone = 0; bone < bones.size(); ++bone)
            {
                const uint32_t row = first_rows[model * 2 + palette] + uint32_t(bone) * skinning_palette_rows::ROWS_PER_BONE;
                all_match = all_match && skinning_palette_rows::unpack_bone(actual_rows.data() + row) == bones[bone];
            }
        }
    }
    check(all_match, "pack_pending writes every acquired palette into its rows");
    check(actual_rows.size() == 40 * 2 * 5 * skinning_palette_rows::ROWS_PER_BONE,
          "current and previous palettes of 40 models (5 bones each) = 1200 rows");
}

void test_owners_are_separate_and_frames_reset()
{
    skinning_palette_buffer buffer;
    buffer.begin_frame();
    const std::vector<pose_mat4> input_palettes = {make_palette(2, 0.0f)};
    const int owner_a = 0;
    const int owner_b = 0;
    const uint32_t actual_a = buffer.acquire(&owner_a, input_palettes, nullptr).current[0];
    const uint32_t actual_b = buffer.acquire(&owner_b, input_palettes, nullptr).current[0];
    check(actual_a != actual_b, "two models get their own rows");
    buffer.begin_frame();
    const uint32_t actual_next_frame = buffer.acquire(&owner_b, input_palettes, nullptr).current[0];
    check(actual_next_frame == 0, "begin_frame starts the rows over");
}

} // namespace

// ---------------------------------------------------------------------------------

auto run_skinning_palettes_suite(rtti::context& /*ctx*/) -> int
{
    test_pack_round_trips();
    test_rows_transform_like_the_matrix();
    test_row_blend_matches_matrix_blend();
    test_append_assigns_consecutive_rows();
    test_acquire_appends_once_per_frame();
    test_previous_rows();
    test_pack_pending_fills_the_rows();
    test_owners_are_separate_and_frames_reset();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("skinning palettes", run_skinning_palettes_suite)
