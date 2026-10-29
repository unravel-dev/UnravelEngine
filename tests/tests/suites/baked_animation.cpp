/*
 * Validation suite for baked animation (BakedAnimation.Bake -> baked_animation_store, Graphics.DrawMeshInstanced with
 * states -> instanced_draw_list::add_baked).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "baked animation"
 *
 * A two-bone character (Hip, Arm on Hip one metre up) with a skinned body and a rigid prop on Arm, and a one-second
 * clip that turns Arm 90 degrees about Z. Pins:
 *   - a clip is sampled at fps per second plus its end; a state picks the nearest frame, times clamp, an unknown clip
 *     shows the first frame;
 *   - each frame's rows are every skinned palette's bone x inverse bind in the mesh's model space, then one row per
 *     rigid submesh: its animated node;
 *   - a baked draw packs one block per instance with its frame's and previous frame's rows, which every part reads:
 *     the skinned body by its palette, the rigid prop by its node row; the G-buffer and velocity data agree, and
 *     both tiles' instances of a part merge into one run;
 *   - every part of a baked draw with a material instance draws its resolved material;
 *   - the radius reaches the rigid prop the arm carries;
 *   - a mesh with an armature but no skin is not baked: the baked shaders read blend weights;
 *   - a released bake keeps its rows while a draw holds it; then the other bakes' rows move up intact, and releasing
 *     the last bake frees every row.
 */

#include "../tests.h"

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/rendering/baked_animation.h>
#include <engine/rendering/instanced_draw_list.h>
#include <engine/rendering/material.h>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/mesh.h>

#include <graphics/vertex_decl.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

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

constexpr const char* MESH_KEY = "engine:/embedded/baked_animation_test/character";
constexpr const char* UNSKINNED_MESH_KEY = "engine:/embedded/baked_animation_test/unskinned_character";
constexpr const char* CLIP_KEY = "engine:/embedded/baked_animation_test/raise_arm";
constexpr const char* MATERIAL_KEY = "engine:/embedded/baked_animation_test/material";
constexpr float FRAMES_PER_SECOND = 10.0f;
constexpr float CLIP_LENGTH = 1.0f;
constexpr float ARM_HEIGHT = 1.0f;
constexpr float TOLERANCE = 1e-4f;
constexpr uint32_t BODY_SUBMESH = 0;
constexpr uint32_t PROP_SUBMESH = 1;
constexpr uint32_t PROP_DATA_GROUP = 1;
/// How far the prop's tip (its vertex 6) stands out along the arm.
constexpr float PROP_TIP_HEIGHT = 0.5f;

void set_position(mesh::load_data& data, uint32_t vertex, const math::vec3& position)
{
    const float values[4] = {position.x, position.y, position.z, 1.0f};
    bgfx::vertexPack(values, false, bgfx::Attrib::Position, data.vertex_format, data.vertex_data.data(), vertex);
}

auto make_node(const std::string& name, int index, const math::vec3& position, std::vector<uint32_t> submeshes)
    -> std::unique_ptr<mesh::armature_node>
{
    auto node = std::make_unique<mesh::armature_node>();
    node->name = name;
    node->index = index;
    node->local_transform.set_position(position);
    node->submeshes = std::move(submeshes);
    return node;
}

auto get_arm_global(float time) -> math::mat4
{
    const float angle = math::half_pi<float>() * time / CLIP_LENGTH;
    return math::translate(math::mat4(1.0f), math::vec3(0.0f, ARM_HEIGHT, 0.0f)) *
           math::mat4_cast(math::angleAxis(angle, math::vec3(0.0f, 0.0f, 1.0f)));
}

/// Root(0) -> Hip(1) -> Arm(2, the prop); Root -> Body(3, the skinned body). The body's bottom row follows Hip, its
/// top row Arm. Without @p is_skinned the body has no bones and the mesh no skin.
auto make_character(bool is_skinned) -> std::shared_ptr<mesh>
{
    mesh::load_data data;
    data.vertex_format = gfx::mesh_vertex::get_layout();
    data.vertex_count = 7;
    data.vertex_data.resize(size_t(data.vertex_count) * data.vertex_format.getStride());
    set_position(data, 0, math::vec3(-0.1f, 0.0f, 0.0f));
    set_position(data, 1, math::vec3(0.1f, 0.0f, 0.0f));
    set_position(data, 2, math::vec3(-0.1f, ARM_HEIGHT, 0.0f));
    set_position(data, 3, math::vec3(0.1f, ARM_HEIGHT, 0.0f));
    set_position(data, 4, math::vec3(0.0f, 0.0f, 0.0f));
    set_position(data, 5, math::vec3(0.5f, 0.0f, 0.0f));
    set_position(data, 6, math::vec3(0.0f, PROP_TIP_HEIGHT, 0.0f));
    if(is_skinned)
    {
        skin_bind_data::bone_influence hip;
        hip.bone_id = "Hip";
        hip.influences = {{0, 1.0f}, {1, 1.0f}};
        data.skin_data.add_bone(hip);
        skin_bind_data::bone_influence arm;
        arm.bone_id = "Arm";
        arm.bind_pose_transform.set_position(math::vec3(0.0f, -ARM_HEIGHT, 0.0f));
        arm.influences = {{2, 1.0f}, {3, 1.0f}};
        data.skin_data.add_bone(arm);
    }
    data.triangle_data = {mesh::triangle{0, {0, 1, 2}},
                          mesh::triangle{0, {1, 3, 2}},
                          mesh::triangle{PROP_DATA_GROUP, {4, 5, 6}}};
    mesh::submesh body;
    body.vertex_start = 0;
    body.vertex_count = 4;
    body.face_start = 0;
    body.face_count = 2;
    body.skinned = is_skinned;
    mesh::submesh prop;
    prop.data_group_id = PROP_DATA_GROUP;
    prop.vertex_start = 4;
    prop.vertex_count = 3;
    prop.face_start = 2;
    prop.face_count = 1;
    data.submeshes = {body, prop};
    data.triangle_count = uint32_t(data.triangle_data.size());
    data.material_count = PROP_DATA_GROUP + 1;
    data.root_node = make_node("Root", 0, math::vec3(0.0f), {});
    auto hip_node = make_node("Hip", 1, math::vec3(0.0f), {});
    hip_node->children.push_back(make_node("Arm", 2, math::vec3(0.0f, ARM_HEIGHT, 0.0f), {PROP_SUBMESH}));
    data.root_node->children.push_back(std::move(hip_node));
    data.root_node->children.push_back(make_node("Body", 3, math::vec3(0.0f), {BODY_SUBMESH}));
    auto result = std::make_shared<mesh>();
    check(result->load_mesh(std::move(data), false), "the test character loads");
    return result;
}

auto make_clip() -> std::shared_ptr<animation_clip>
{
    using seconds_t = animation_clip::seconds_t;
    auto clip = std::make_shared<animation_clip>();
    clip->duration = seconds_t{CLIP_LENGTH};
    auto& arm = clip->channels.emplace_back();
    arm.node_name = "Arm";
    arm.node_index = 2;
    arm.position_keys = {{seconds_t{0.0f}, {0.0f, ARM_HEIGHT, 0.0f}},
                         {seconds_t{CLIP_LENGTH}, {0.0f, ARM_HEIGHT, 0.0f}}};
    arm.rotation_keys = {
        {seconds_t{0.0f}, math::identity<math::quat>()},
        {seconds_t{CLIP_LENGTH}, math::angleAxis(math::half_pi<float>(), math::vec3(0.0f, 0.0f, 1.0f))}};
    arm.scaling_keys = {{seconds_t{0.0f}, {1.0f, 1.0f, 1.0f}}};
    return clip;
}

auto is_near(const math::mat4& actual, const math::mat4& expected) -> bool
{
    for(int column = 0; column < 4; ++column)
    {
        for(int row = 0; row < 4; ++row)
        {
            if(std::abs(actual[column][row] - expected[column][row]) > TOLERANCE)
            {
                return false;
            }
        }
    }
    return true;
}

struct fixture
{
    asset_handle<mesh> character;
    asset_handle<mesh> unskinned_character;
    asset_handle<animation_clip> clip;
    asset_handle<material> surface;
};

auto make_fixture() -> fixture
{
    auto& am = engine::context().get_cached<asset_manager>();
    am.unload_asset<mesh>(MESH_KEY);
    am.unload_asset<mesh>(UNSKINNED_MESH_KEY);
    am.unload_asset<animation_clip>(CLIP_KEY);
    am.unload_asset<material>(MATERIAL_KEY);
    fixture result;
    result.character = am.get_asset_from_instance<mesh>(MESH_KEY, make_character(true));
    result.unskinned_character = am.get_asset_from_instance<mesh>(UNSKINNED_MESH_KEY, make_character(false));
    result.clip = am.get_asset_from_instance<animation_clip>(CLIP_KEY, make_clip());
    result.surface = am.get_asset_from_instance<material>(MATERIAL_KEY, std::make_shared<pbr_material>());
    return result;
}

/// The skinning matrix of skin bone @p bone at @p frame, read back from the store's rows.
/// The bone @p offset rows into @p frame's rows.
auto read_row(const baked_animation_store& store, const baked_animation& baked, uint32_t frame, uint32_t offset)
    -> math::mat4
{
    return skinning_palette_rows::unpack_bone(store.get_rows().data() + baked.get_frame_row(frame) + offset);
}

auto read_bone(const baked_animation_store& store,
               const baked_animation& baked,
               const mesh& character,
               uint32_t frame,
               uint32_t bone) -> math::mat4
{
    const auto& palette_bones = character.get_bone_palettes()[BODY_SUBMESH].get_bones();
    uint32_t slot = 0;
    while(slot < palette_bones.size() && palette_bones[slot] != bone)
    {
        ++slot;
    }
    return read_row(store,
                    baked,
                    frame,
                    baked.get_palette_offset(BODY_SUBMESH) + slot * skinning_palette_rows::ROWS_PER_BONE);
}

void test_bake(const fixture& assets)
{
    baked_animation_store store;
    const uint32_t id = store.create(assets.character, FRAMES_PER_SECOND, baked_animation_retarget::copy);
    check(id != 0, "a mesh with an armature starts a bake");
    check(store.add_clip(id, assets.clip) == 0, "the first clip is index 0");
    const auto baked = store.find(id);
    check(baked != nullptr, "the bake is found by its id");
    if(!baked)
    {
        return;
    }
    check(baked->get_frame_count() == 11u, "a one-second clip at 10 per second has 11 frames");
    check(std::abs(baked->get_clip_length(0) - CLIP_LENGTH) < TOLERANCE, "the clip keeps its length");
    check(baked->get_frame({0, 0.5f}) == 5u, "a state picks the nearest frame");
    check(baked->get_frame({0, 0.54f}) == 5u && baked->get_frame({0, 0.56f}) == 6u, "times round to the nearest frame");
    check(baked->get_frame({0, 7.0f}) == 10u && baked->get_frame({0, -1.0f}) == 0u, "times outside the clip clamp");
    check(baked->get_frame({5, 0.5f}) == 0u, "an unknown clip shows the first frame");
    const auto character = assets.character.get();
    const auto body_rows = uint32_t(character->get_bone_palettes()[BODY_SUBMESH].get_bones().size()) *
                           skinning_palette_rows::ROWS_PER_BONE;
    check(baked->get_palette_offset(BODY_SUBMESH) == 0u && !baked->is_rigid(BODY_SUBMESH),
          "the skinned body has the frame's first palette");
    check(baked->get_palette_offset(PROP_SUBMESH) == body_rows && baked->is_rigid(PROP_SUBMESH),
          "the rigid prop's node row follows the body's palette");
    const math::mat4 arm_unbind = math::translate(math::mat4(1.0f), math::vec3(0.0f, -ARM_HEIGHT, 0.0f));
    for(const uint32_t frame : {0u, 5u, 10u})
    {
        const float time = float(frame) / FRAMES_PER_SECOND;
        const std::string at = " at frame " + std::to_string(frame);
        check(is_near(read_bone(store, *baked, *character, frame, 0), math::mat4(1.0f)),
              "the resting hip skins nothing" + at);
        check(is_near(read_bone(store, *baked, *character, frame, 1), get_arm_global(time) * arm_unbind),
              "the arm's rows are its model transform x inverse bind" + at);
        check(is_near(read_row(store, *baked, frame, body_rows), get_arm_global(time)),
              "the prop's row is its animated node" + at);
    }
    check(baked->get_radius() >= ARM_HEIGHT, "the radius reaches the raised arm");
    check(baked->get_radius() >= ARM_HEIGHT + PROP_TIP_HEIGHT, "the radius reaches the rigid prop the arm carries");
    store.release(id);
    check(store.find(id) == nullptr, "a released bake is not found");
}

void test_unskinned_mesh(const fixture& assets)
{
    baked_animation_store store;
    check(store.create(assets.unskinned_character, FRAMES_PER_SECOND, baked_animation_retarget::copy) == 0,
          "a mesh with an armature but no skin is not baked");
}

void test_release_reclaims_rows(const fixture& assets)
{
    baked_animation_store store;
    const uint32_t first_id = store.create(assets.character, FRAMES_PER_SECOND, baked_animation_retarget::copy);
    store.add_clip(first_id, assets.clip);
    const uint32_t second_id = store.create(assets.character, FRAMES_PER_SECOND, baked_animation_retarget::copy);
    store.add_clip(second_id, assets.clip);
    const size_t row_count = store.get_rows().size();
    auto held = store.find(first_id);
    store.release(first_id);
    store.reclaim_released();
    check(store.get_rows().size() == row_count, "a released bake a draw holds keeps its rows");
    held.reset();
    store.reclaim_released();
    check(store.get_rows().size() == row_count / 2, "a released bake no draw holds gives its rows back");
    auto second = store.find(second_id);
    if(!second)
    {
        check(false, "the reclaim test keeps its second bake");
        return;
    }
    check(second->get_frame_row(0) == 0u, "the remaining bake's rows move to the front");
    const uint32_t last_frame = second->get_frame_count() - 1u;
    check(is_near(read_row(store, *second, last_frame, second->get_palette_offset(PROP_SUBMESH)),
                  get_arm_global(CLIP_LENGTH)),
          "the moved rows still hold the bake's frames");
    second.reset();
    store.release(second_id);
    store.reclaim_released();
    check(store.get_rows().empty(), "releasing the last bake frees every row");
}

/// The matrix whose top three rows @p rows hold (skinning_palette_rows::pack_bone).
auto unpack_rows(const math::vec4 (&rows)[3]) -> math::mat4
{
    math::mat4 matrix(1.0f);
    for(int column = 0; column < 4; ++column)
    {
        for(int row = 0; row < 3; ++row)
        {
            matrix[column][row] = rows[row][column];
        }
    }
    return matrix;
}

/// The list's packed data of the baked or static parts in @p layout, as the frame's instance buffer gets it.
auto write_packed(const instanced_draw_list& list, instanced_layout layout, bool is_baked) -> std::vector<uint8_t>
{
    const uint32_t count = list.get_packed_count(layout, is_baked);
    std::vector<uint8_t> packed(size_t(count) * instanced_draw_list::get_packed_stride(layout, is_baked));
    list.write_packed(layout, is_baked, packed.data(), count);
    return packed;
}

void test_draw(const fixture& assets)
{
    baked_animation_store store;
    const uint32_t id = store.create(assets.character, FRAMES_PER_SECOND, baked_animation_retarget::copy);
    store.add_clip(id, assets.clip);
    const auto baked = store.find(id);
    if(!baked)
    {
        check(false, "the draw test has a bake");
        return;
    }
    instanced_draw_list list;
    instanced_draw_list::draw_settings settings;
    settings.render_material = assets.surface;
    const std::vector<math::mat4> transforms{math::translate(math::mat4(1.0f), math::vec3(1.0f, 0.0f, 1.0f)),
                                             math::translate(math::mat4(1.0f), math::vec3(30.0f, 0.0f, 1.0f))};
    const std::vector<baked_animation_state> states{{0, 0.5f}, {0, 1.0f}};
    const std::vector<baked_animation_state> previous_states{{0, 0.4f}, {0, 0.9f}};
    list.add_baked(settings,
                   baked,
                   reinterpret_cast<const float*>(transforms.data()),
                   states.data(),
                   transforms.size(),
                   nullptr,
                   previous_states.data());
    const auto& clusters = list.get_clusters();
    check(clusters.size() == 2u, "two instances on two tiles make two clusters");
    instanced_run_list runs;
    for(const auto& cluster : clusters)
    {
        cluster.collect_mesh_runs(runs, instanced_layout::velocity);
    }
    runs.merge();
    const instanced_run* body = nullptr;
    const instanced_run* prop = nullptr;
    for(const auto& run : runs.get_runs())
    {
        (run.part->is_rigid ? prop : body) = &run;
    }
    check(runs.get_runs().size() == 2u && body != nullptr && prop != nullptr && body->part->is_baked &&
              prop->part->is_baked && body->part->submesh == BODY_SUBMESH && prop->part->submesh == PROP_SUBMESH &&
              body->first == prop->first,
          "both submeshes draw from one baked block: the body skinned, the prop rigid");
    check(list.get_packed_count(instanced_layout::base, true) == 2u &&
              list.get_packed_count(instanced_layout::base, false) == 0u,
          "a baked draw packs each instance once");
    if(body == nullptr || prop == nullptr || body->count != 2u || prop->count != 2u)
    {
        check(false, "each part draws both tiles' instances in one run");
        list.clear();
        store.release(id);
        return;
    }
    const auto packed_velocity = write_packed(list, instanced_layout::velocity, true);
    const auto* velocity = reinterpret_cast<const baked_velocity_instance_vertex_data*>(packed_velocity.data());
    bool are_rows_right = true;
    bool is_prop_right = true;
    for(uint32_t index = 0; index < body->count; ++index)
    {
        const auto& data = velocity[body->first + index];
        const math::mat4 world = unpack_rows(data.world);
        const bool is_first = std::abs(world[3].x - 1.0f) < TOLERANCE;
        const uint32_t frame = is_first ? 5u : 10u;
        const uint32_t previous_frame = is_first ? 4u : 9u;
        are_rows_right = are_rows_right && uint32_t(data.palette.x) == baked->get_frame_row(frame) &&
                         uint32_t(data.palette.y) == baked->get_frame_row(previous_frame) &&
                         is_near(world, transforms[is_first ? 0 : 1]) && is_near(unpack_rows(data.prev_world), world);
        const uint32_t prop_offset = prop->part->palette_offset;
        is_prop_right = is_prop_right &&
                        is_near(world * read_row(store, *baked, frame, prop_offset),
                                world * get_arm_global(is_first ? 0.5f : 1.0f)) &&
                        is_near(world * read_row(store, *baked, previous_frame, prop_offset),
                                world * get_arm_global(is_first ? 0.4f : 0.9f));
    }
    check(are_rows_right, "an instance draws its frame's rows under its own matrix, with last frame's rows");
    check(is_prop_right, "a rigid prop rides its animated node, now and last frame");
    const auto packed_base = write_packed(list, instanced_layout::base, true);
    const auto* base = reinterpret_cast<const baked_instance_vertex_data*>(packed_base.data());
    bool are_base_rows_right = true;
    for(uint32_t index = 0; index < body->count; ++index)
    {
        const auto& data = base[body->first + index];
        const auto& moving = velocity[body->first + index];
        are_base_rows_right = are_base_rows_right && data.palette == moving.palette &&
                              is_near(unpack_rows(data.world), unpack_rows(moving.world));
    }
    check(are_base_rows_right, "the G-buffer and shadows read the rows and matrices the velocity pass reads");
    list.clear();
    store.release(id);
}

void test_draw_material_instance(const fixture& assets)
{
    baked_animation_store store;
    const uint32_t id = store.create(assets.character, FRAMES_PER_SECOND, baked_animation_retarget::copy);
    store.add_clip(id, assets.clip);
    const auto baked = store.find(id);
    if(!baked)
    {
        check(false, "the material instance test has a bake");
        return;
    }
    auto instance = std::make_shared<material_instance>();
    instance->set_shared_material(assets.surface);
    instance->set_override(entt::hashed_string{"emissive_color"}, entt::meta_any{math::color(1.0f, 0.0f, 0.0f, 1.0f)});
    instanced_draw_list list;
    instanced_draw_list::draw_settings settings;
    settings.render_material = assets.surface;
    settings.render_material_instance = instance;
    const std::vector<math::mat4> transforms{math::translate(math::mat4(1.0f), math::vec3(1.0f, 0.0f, 1.0f))};
    const std::vector<baked_animation_state> states{{0, 0.5f}};
    list.add_baked(settings, baked, &transforms[0][0][0], states.data(), transforms.size(), nullptr, nullptr);
    instanced_run_list runs;
    for(const auto& cluster : list.get_clusters())
    {
        cluster.collect_mesh_runs(runs, instanced_layout::base);
    }
    runs.merge();
    bool is_resolved = runs.get_runs().size() == 2u && instance->get_resolved() != nullptr;
    for(const auto& run : runs.get_runs())
    {
        is_resolved = is_resolved && run.part->is_baked && run.part->material_ptr == instance->get_resolved();
    }
    check(is_resolved, "every part of a baked draw with a material instance draws its resolved material");
    list.clear();
    store.release(id);
}

} // namespace

auto run_baked_animation_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto assets = make_fixture();
    test_bake(assets);
    test_unskinned_mesh(assets);
    test_release_reclaims_rows(assets);
    test_draw(assets);
    test_draw_material_instance(assets);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("baked animation", run_baked_animation_suite)
