/*
 * Validation suite for Decal components, the receivers they lay onto and their placement.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "decals"
 *
 * What it pins:
 *   - the play lifetime: Fade In ramps a decal in, it holds for its Lifetime, Fade Out ramps it out, then it has faded
 *     out; a decal without a Lifetime stays, and its normalized age is 0; one that has not aged (edit mode) shows
 *     whole, a spawned one fades in from its first frame;
 *   - resize: the face, and a depth of half its smaller side;
 *   - the box: Size and Depth scale the unit box, centred on the entity, before the entity's transform;
 *   - placement on a surface: the box projects into it (its Z against the normal), its width runs level on a wall, a
 *     floor decal reads with its top away from world Z, and the roll turns it about the projection;
 *   - receivers: Receive Decals Auto takes every model but a skinned one, On and Off decide alone; a Post Process Mask
 *     stays within 0 to 255;
 *   - a scene round trip keeps a decal's fields and Receiver Layers, a model's Receive Decals and Post Process Mask.
 */

#include "../tests.h"

#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/ecs/components/decal_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <cstdio>
#include <sstream>
#include <string>

using namespace unravel::test;

using namespace unravel;

namespace
{

constexpr float DECAL_TEST_TOLERANCE = 0.0001f;

void decal_check_near(float actual, float expected, const std::string& what)
{
    check_near(actual, expected, DECAL_TEST_TOLERANCE, what);
}

void decal_check_near(const math::vec2& actual, const math::vec2& expected, const std::string& what)
{
    check(math::all(math::lessThanEqual(math::abs(actual - expected), math::vec2(DECAL_TEST_TOLERANCE))), what);
}

void decal_check_near(const math::vec3& actual, const math::vec3& expected, const std::string& what)
{
    check(math::all(math::lessThanEqual(math::abs(actual - expected), math::vec3(DECAL_TEST_TOLERANCE))), what);
}

void test_lifetime_fade()
{
    std::printf("test_lifetime_fade\n");
    decal_component decal;
    decal.fade_in = 1.0f;
    decal.lifetime = 2.0f;
    decal.fade_out = 1.0f;
    const auto fade_at = [&](float age)
    {
        decal.age = age;
        return decal.get_lifetime_fade();
    };
    decal_check_near(fade_at(0.0f), 0.0f, "a decal fading in starts unseen");
    decal_check_near(fade_at(0.5f), 0.5f, "it is half in halfway through Fade In");
    decal_check_near(fade_at(1.0f), 1.0f, "it is whole when Fade In ends");
    decal_check_near(fade_at(2.5f), 1.0f, "it holds for its Lifetime");
    decal_check_near(fade_at(3.5f), 0.5f, "it is half out halfway through Fade Out");
    decal_check_near(fade_at(4.0f), 0.0f, "it is gone when Fade Out ends");
    decal.age = 3.9f;
    check(!decal.has_faded_out(), "it has not faded out before Fade Out ends");
    decal.age = 4.0f;
    check(decal.has_faded_out(), "it has faded out when Fade Out ends");
    decal.age = 2.0f;
    decal_check_near(decal.get_normalized_age(), 0.5f, "the normalized age runs over Fade In, Lifetime and Fade Out");

    decal_component lasting;
    lasting.age = 100.0f;
    decal_check_near(lasting.get_lifetime_fade(), 1.0f, "a decal without a Lifetime stays");
    check(!lasting.has_faded_out(), "a decal without a Lifetime never fades out");
    decal_check_near(lasting.get_normalized_age(), 0.0f, "a decal without a Lifetime has no normalized age");

    decal_component cut;
    cut.lifetime = 1.0f;
    cut.fade_out = 0.0f;
    cut.age = 1.5f;
    decal_check_near(cut.get_lifetime_fade(), 0.0f, "without Fade Out a decal ends at once");
}

void test_edit_mode_fade()
{
    std::printf("test_edit_mode_fade\n");
    decal_component decal;
    decal.fade_in = 1.0f;
    decal.lifetime = 2.0f;
    decal_check_near(decal.get_play_fade(), 1.0f, "a decal that has not aged shows whole: edit mode");
    decal.age = 0.25f;
    decal_check_near(decal.get_play_fade(), 0.25f, "once it ages, it fades in");
    decal_component spawned;
    spawned.fade_in = 1.0f;
    spawned.is_transient = true;
    decal_check_near(spawned.get_play_fade(), 0.0f, "a spawned decal starts faded in from nothing");
}

void test_resize()
{
    std::printf("test_resize\n");
    decal_component decal;
    decal.resize(math::vec2(4.0f, 2.0f));
    decal_check_near(decal.size, math::vec2(4.0f, 2.0f), "resize sets the face");
    decal_check_near(decal.depth, 1.0f, "the depth is half the smaller side");
    decal.resize(math::vec2(0.02f, -1.0f));
    check(decal.size.y > 0.0f && decal.depth >= 0.05f, "a face too small keeps a size and a depth to project with");
}

void test_box()
{
    std::printf("test_box\n");
    decal_component decal;
    decal.size = math::vec2(2.0f, 1.0f);
    decal.depth = 0.5f;
    const auto bounds = decal.get_local_bounds();
    decal_check_near(bounds.min, math::vec3(-1.0f, -0.5f, -0.25f), "the box's lower corner is half of Size and Depth");
    decal_check_near(bounds.max, math::vec3(1.0f, 0.5f, 0.25f), "the box's upper corner is half of Size and Depth");
    math::transform world;
    world.set_position(math::vec3(10.0f, 0.0f, 0.0f));
    const math::mat4 box_to_world = decal.get_box_to_world(world);
    decal_check_near(math::vec3(box_to_world * math::vec4(0.5f, 0.5f, 0.5f, 1.0f)),
               math::vec3(11.0f, 0.5f, 0.25f),
               "the unit box's corner lands at the scaled corner, moved by the entity");
}

/// The world direction of local axis @p axis of @p entity.
auto get_world_axis(entt::handle entity, const math::vec3& axis) -> math::vec3
{
    return entity.get<transform_component>().get_rotation_global() * axis;
}

void test_placement()
{
    std::printf("test_placement\n");
    scene scn("decals");
    auto wall_decal = scn.create_entity("wall decal");
    defaults::place_decal_on_surface(wall_decal, math::vec3(1.0f, 2.0f, 3.0f), math::vec3(0.0f, 0.0f, -1.0f));
    decal_check_near(wall_decal.get<transform_component>().get_position_global(), math::vec3(1.0f, 2.0f, 3.0f), "the box sits at the point");
    decal_check_near(get_world_axis(wall_decal, math::vec3(0.0f, 0.0f, 1.0f)),
               math::vec3(0.0f, 0.0f, 1.0f),
               "the box projects into the surface, against its normal");
    decal_check_near(get_world_axis(wall_decal, math::vec3(0.0f, 1.0f, 0.0f)), math::vec3(0.0f, 1.0f, 0.0f), "on a wall its top is up");
    check(std::abs(get_world_axis(wall_decal, math::vec3(1.0f, 0.0f, 0.0f)).y) < 0.0001f, "on a wall its width runs level");

    auto floor_decal = scn.create_entity("floor decal");
    defaults::place_decal_on_surface(floor_decal, math::vec3(0.0f), math::vec3(0.0f, 1.0f, 0.0f));
    decal_check_near(get_world_axis(floor_decal, math::vec3(0.0f, 0.0f, 1.0f)), math::vec3(0.0f, -1.0f, 0.0f), "a floor decal projects down");
    check(std::abs(get_world_axis(floor_decal, math::vec3(0.0f, 1.0f, 0.0f)).y) < 0.0001f, "a floor decal lies flat");

    auto rolled = scn.create_entity("rolled decal");
    defaults::place_decal_on_surface(rolled, math::vec3(0.0f), math::vec3(0.0f, 0.0f, -1.0f), 90.0f);
    decal_check_near(get_world_axis(rolled, math::vec3(0.0f, 0.0f, 1.0f)), math::vec3(0.0f, 0.0f, 1.0f), "the roll keeps the projection");
    check(std::abs(get_world_axis(rolled, math::vec3(0.0f, 1.0f, 0.0f)).y) < 0.0001f, "a quarter roll lays the top sideways");
}

void test_receivers()
{
    std::printf("test_receivers\n");
    model_component model;
    check(model.get_decal_receive() == decal_receive::automatic && model.receives_decals(),
          "a model receives decals by default while it is not skinned");
    model.set_decal_receive(decal_receive::off);
    check(!model.receives_decals(), "Off keeps every decal off the model");
    model.set_decal_receive(decal_receive::on);
    check(model.receives_decals(), "On lays decals onto the model");
    check(model.get_post_process_mask() == 0, "a model is in no post-process mask by default");
    model.set_post_process_mask(300);
    check(model.get_post_process_mask() == 255, "a Post Process Mask past 255 is 255");
    model.set_post_process_mask(-4);
    check(model.get_post_process_mask() == 0, "a negative Post Process Mask is none");
    decal_component decal;
    check(decal.receiver_layers.mask == layer_reserved::everything_layer, "a decal lays onto every layer by default");
}

void test_round_trip()
{
    std::printf("test_round_trip\n");
    scene src("src");
    auto decal_entity = src.create_entity("decal");
    auto& decal = decal_entity.emplace<decal_component>();
    decal.size = math::vec2(2.0f, 3.0f);
    decal.depth = 0.75f;
    decal.opacity = 0.4f;
    decal.sort_order = 4;
    decal.receiver_layers.mask = 1 << 5;
    decal.angle_fade = math::vec2(40.0f, 70.0f);
    decal.lifetime = 5.0f;
    decal.remove_after_fade = true;
    auto model_entity = src.create_entity("model");
    auto& model = model_entity.emplace<model_component>();
    model.set_decal_receive(decal_receive::off);
    model.set_post_process_mask(7);

    std::stringstream stream;
    save_to_stream(stream, src);
    scene dst("dst");
    load_from_stream(stream, dst);

    const decal_component* loaded_decal = nullptr;
    dst.registry->view<decal_component>().each(
        [&](auto, decal_component& comp)
        {
            loaded_decal = &comp;
        });
    check(loaded_decal != nullptr, "the decal survives the round trip");
    if(loaded_decal != nullptr)
    {
        decal_check_near(loaded_decal->size, math::vec2(2.0f, 3.0f), "it keeps its Size");
        decal_check_near(loaded_decal->depth, 0.75f, "it keeps its Depth");
        decal_check_near(loaded_decal->opacity, 0.4f, "it keeps its Opacity");
        check(loaded_decal->sort_order == 4, "it keeps its Sort Order");
        check(loaded_decal->receiver_layers.mask == (1 << 5), "it keeps its Receiver Layers");
        decal_check_near(loaded_decal->angle_fade, math::vec2(40.0f, 70.0f), "it keeps its Angle Fade");
        check(loaded_decal->lifetime == 5.0f && loaded_decal->remove_after_fade, "it keeps its lifetime fields");
    }
    const model_component* loaded_model = nullptr;
    dst.registry->view<model_component>().each(
        [&](auto, model_component& comp)
        {
            loaded_model = &comp;
        });
    check(loaded_model != nullptr && loaded_model->get_decal_receive() == decal_receive::off &&
              loaded_model->get_post_process_mask() == 7,
          "a model keeps its Receive Decals and Post Process Mask");
}

void run_decals_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_lifetime_fade());
    RUN_CASE(test_edit_mode_fade());
    RUN_CASE(test_resize());
    RUN_CASE(test_box());
    RUN_CASE(test_placement());
    RUN_CASE(test_receivers());
    RUN_CASE(test_round_trip());
}

} // namespace

REGISTER_TEST_SUITE("decals", run_decals_suite)
