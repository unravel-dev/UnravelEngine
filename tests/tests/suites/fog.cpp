/*
 * Validation suite for the height fog: its component's document round trip, its volume blending, the shader
 * parameters the fog pass packs from its settings and the volumetric fog's froxel grids.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "fog"
 *
 * What it pins:
 *   - every fog setting and the enabled flag survive a scene document save and load;
 *   - post-process volumes blend the fog like the other effects: a local volume the camera is inside replaces a
 *     global one, half its blend distance away it lerps halfway, and a dominant disabled volume turns fog off;
 *   - fog_pass::make_shader_params: the extinction is the inverse attenuation distance, the density reaches 0.1%
 *     at the maximum height, the transmittance floor is 1 - max opacity, the sun and sky scale by the linear
 *     albedo, and a default-constructed value is fog off;
 *   - fog_pass::get_volumetric_grid: the quality presets' grids at 16:9, a view without an aspect, and the column
 *     limits of extreme aspects;
 *   - fog_pass::pick_local_lights: lights out of the fog's reach are dropped, the brightest at the camera come first,
 *     and no more than max_local_lights are kept;
 *   - fog_pass::find_changed_lights: lights are matched by id, and a moved, dimmed or new light is marked changed;
 *   - a light's volumetric intensity round-trips and an older light without it loads at 1;
 *   - fog volumes: the nearest are kept and packed farthest first, the packed matrix maps a world point to its place
 *     in the unit shape, the fade band and shape codes are what fog.sh reads, and the component round-trips.
 */

#include "../tests.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/ecs/components/fog_component.h>
#include <engine/rendering/ecs/components/fog_volume_component.h>
#include <engine/rendering/ecs/components/volume_component.h>
#include <engine/rendering/pipeline/volume_resolver.h>
#include <engine/meta/rendering/light.hpp>
#include <serialization/associative_archive.h>

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

using namespace unravel;

namespace
{

int g_fog_checks = 0;
int g_fog_failures = 0;

/// Tolerance of the float comparisons.
constexpr float FOG_TOLERANCE = 1e-4f;

void fog_check(bool condition, const std::string& what)
{
    ++g_fog_checks;
    if(!condition)
    {
        ++g_fog_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto is_close(float actual, float expected) -> bool
{
    return std::abs(actual - expected) <= FOG_TOLERANCE * math::max(1.0f, std::abs(expected));
}

/// Settings that differ from the defaults in every field.
auto make_custom_settings() -> fog_pass::settings
{
    fog_pass::settings settings;
    settings.attenuation_distance = 120.0f;
    settings.base_height = -3.0f;
    settings.maximum_height = 80.0f;
    settings.albedo = math::color(0.5f, 0.6f, 0.7f, 1.0f);
    settings.anisotropy = 0.6f;
    settings.max_opacity = 0.8f;
    settings.start_distance = 12.0f;
    settings.sun_scattering = 2.0f;
    settings.ambient_scattering = 0.5f;
    settings.sky_affect = 0.25f;
    settings.sky_distance = 9000.0f;
    settings.volumetric = true;
    settings.volumetric_distance = 48.0f;
    settings.volumetric_quality = volumetric_fog_quality::ultra;
    settings.volumetric_slice_uniformity = 0.4f;
    settings.volumetric_history = 0.7f;
    return settings;
}

auto add_fog_volume(scene& scn, const std::string& name, volume_mode mode, const fog_pass::settings& settings)
    -> entt::handle
{
    auto entity = scn.create_entity(name);
    auto& active = entity.get_or_emplace<active_component>();
    (void)active;
    auto& volume = entity.emplace<volume_component>();
    volume.mode = mode;
    volume.extents = {5.0f, 5.0f, 5.0f};
    volume.blend_distance = 10.0f;
    entity.emplace<fog_component>().settings = settings;
    return entity;
}

void test_settings_round_trip()
{
    std::printf("test_settings_round_trip\n");
    const auto expected = make_custom_settings();
    scene source("source");
    auto entity = source.create_entity("fog");
    auto& fog = entity.emplace<fog_component>();
    fog.enabled = false;
    fog.settings = expected;
    std::stringstream stream;
    save_to_stream(stream, source);
    scene loaded_scene("loaded");
    load_from_stream(stream, loaded_scene);
    bool found = false;
    loaded_scene.registry->view<fog_component>().each(
        [&](auto, const fog_component& loaded)
        {
            found = true;
            const auto& actual = loaded.settings;
            fog_check(!loaded.enabled, "enabled round-trips");
            fog_check(is_close(actual.attenuation_distance, expected.attenuation_distance), "attenuation distance");
            fog_check(is_close(actual.base_height, expected.base_height), "base height");
            fog_check(is_close(actual.maximum_height, expected.maximum_height), "maximum height");
            fog_check(is_close(actual.albedo.value.x, expected.albedo.value.x) &&
                          is_close(actual.albedo.value.y, expected.albedo.value.y) &&
                          is_close(actual.albedo.value.z, expected.albedo.value.z),
                      "albedo");
            fog_check(is_close(actual.anisotropy, expected.anisotropy), "anisotropy");
            fog_check(is_close(actual.max_opacity, expected.max_opacity), "max opacity");
            fog_check(is_close(actual.start_distance, expected.start_distance), "start distance");
            fog_check(is_close(actual.sun_scattering, expected.sun_scattering), "sun scattering");
            fog_check(is_close(actual.ambient_scattering, expected.ambient_scattering), "ambient scattering");
            fog_check(is_close(actual.sky_affect, expected.sky_affect), "sky affect");
            fog_check(is_close(actual.sky_distance, expected.sky_distance), "sky distance");
            fog_check(actual.volumetric == expected.volumetric, "volumetric");
            fog_check(is_close(actual.volumetric_distance, expected.volumetric_distance), "volumetric distance");
            fog_check(actual.volumetric_quality == expected.volumetric_quality, "volumetric quality");
            fog_check(is_close(actual.volumetric_slice_uniformity, expected.volumetric_slice_uniformity),
                      "volumetric slice uniformity");
            fog_check(is_close(actual.volumetric_history, expected.volumetric_history), "volumetric history");
        });
    fog_check(found, "the loaded scene has the fog component");
}

void test_volume_blending()
{
    std::printf("test_volume_blending\n");
    scene scn("volumes");
    fog_pass::settings global_settings;
    global_settings.attenuation_distance = 400.0f;
    fog_pass::settings local_settings;
    local_settings.attenuation_distance = 100.0f;
    add_fog_volume(scn, "global", volume_mode::global, global_settings);
    auto local = add_fog_volume(scn, "local", volume_mode::local, local_settings);
    const entt::handle no_camera{};

    const auto inside = resolve_post_process_volumes(scn, math::vec3(0.0f, 0.0f, 0.0f), no_camera);
    fog_check(inside.has_fog, "fog resolves inside the local volume");
    fog_check(is_close(inside.fog.attenuation_distance, 100.0f), "inside the local volume its settings win");

    // The local box ends at x = 5 and blends over 10 m: at x = 10 it contributes half.
    const auto halfway = resolve_post_process_volumes(scn, math::vec3(10.0f, 0.0f, 0.0f), no_camera);
    fog_check(halfway.has_fog, "fog resolves in the blend region");
    fog_check(is_close(halfway.fog.attenuation_distance, 250.0f), "half the blend distance away it lerps halfway");

    const auto outside = resolve_post_process_volumes(scn, math::vec3(100.0f, 0.0f, 0.0f), no_camera);
    fog_check(outside.has_fog, "fog resolves outside the local volume");
    fog_check(is_close(outside.fog.attenuation_distance, 400.0f), "outside the local volume the global one applies");

    local.get<fog_component>().settings.volumetric = true;
    const auto volumetric_inside = resolve_post_process_volumes(scn, math::vec3(0.0f, 0.0f, 0.0f), no_camera);
    const auto volumetric_far = resolve_post_process_volumes(scn, math::vec3(12.0f, 0.0f, 0.0f), no_camera);
    fog_check(volumetric_inside.fog.volumetric, "a dominant local volume turns the volumetric fog on");
    fog_check(!volumetric_far.fog.volumetric, "a minor local volume leaves the global one's volumetric switch");

    local.get<fog_component>().enabled = false;
    const auto disabled = resolve_post_process_volumes(scn, math::vec3(0.0f, 0.0f, 0.0f), no_camera);
    fog_check(!disabled.has_fog, "a dominant disabled local volume turns the fog off");
}

void test_shader_params()
{
    std::printf("test_shader_params\n");
    fog_check(!fog_pass::shader_params{}.is_enabled(), "a default-constructed value is fog off");

    const auto settings = make_custom_settings();
    fog_pass::sun_light sun;
    sun.direction = {0.0f, 1.0f, 0.0f};
    sun.illuminance = {1000.0f, 2000.0f, 4000.0f};
    const math::vec3 camera_position(1.0f, 2.0f, 3.0f);
    const auto params = fog_pass::make_shader_params(settings, sun, camera_position);
    fog_check(params.is_enabled(), "settings with a finite attenuation distance fog");
    fog_check(is_close(params.density.x, 1.0f / settings.attenuation_distance), "extinction = 1 / attenuation distance");
    fog_check(is_close(params.density.y, settings.base_height), "the base height passes through");
    const float density_at_maximum =
        std::exp(-params.density.z * (settings.maximum_height - settings.base_height));
    fog_check(is_close(density_at_maximum, 0.001f), "the density at the maximum height is 0.1% of the base density");
    fog_check(is_close(params.density.w, 1.0f - settings.max_opacity), "the transmittance floor is 1 - max opacity");
    fog_check(is_close(params.range.x, settings.start_distance) && is_close(params.range.y, settings.sky_distance) &&
                  is_close(params.range.z, settings.sky_affect) && is_close(params.range.w, settings.anisotropy),
              "start distance, sky distance, sky affect and anisotropy pass through");
    const auto albedo = settings.albedo.to_linear().value;
    fog_check(is_close(params.light.x, sun.illuminance.x * settings.sun_scattering) &&
                  is_close(params.light.z, sun.illuminance.z * settings.sun_scattering),
              "the light is the sun's illuminance x sun scattering, before any albedo");
    fog_check(is_close(params.light.w, settings.ambient_scattering), "the light's w is the ambient scattering");
    fog_check(is_close(params.albedo.x, albedo.x) && is_close(params.albedo.y, albedo.y), "the albedo is linear");
    fog_check(is_close(params.camera.x, camera_position.x) && is_close(params.camera.y, camera_position.y),
              "the camera position passes through");

    auto inverted = settings;
    inverted.maximum_height = inverted.base_height - 10.0f;
    inverted.anisotropy = 2.0f;
    inverted.max_opacity = 3.0f;
    const auto clamped = fog_pass::make_shader_params(inverted, sun, camera_position);
    fog_check(std::isfinite(clamped.density.z) && clamped.density.z > 0.0f,
              "a maximum height below the base keeps a finite falloff");
    fog_check(clamped.range.w < 1.0f, "the anisotropy is clamped below 1");
    fog_check(is_close(clamped.density.w, 0.0f), "max opacity is clamped to 1");
}

void test_volumetric_grids()
{
    std::printf("test_volumetric_grids\n");
    constexpr float wide = 16.0f / 9.0f;
    const auto low = fog_pass::get_volumetric_grid(volumetric_fog_quality::low, wide);
    const auto medium = fog_pass::get_volumetric_grid(volumetric_fog_quality::medium, wide);
    const auto high = fog_pass::get_volumetric_grid(volumetric_fog_quality::high, wide);
    const auto ultra = fog_pass::get_volumetric_grid(volumetric_fog_quality::ultra, wide);
    fog_check(low == fog_pass::volumetric_grid{128, 72, 64}, "low is 128x72x64 at 16:9");
    fog_check(medium == fog_pass::volumetric_grid{160, 90, 64}, "medium is 160x90x64 at 16:9");
    fog_check(high == fog_pass::volumetric_grid{240, 135, 64}, "high is 240x135x64 at 16:9");
    fog_check(ultra == fog_pass::volumetric_grid{240, 135, 128}, "ultra is 240x135x128 at 16:9");
    fog_check(fog_pass::get_volumetric_grid(volumetric_fog_quality::medium, 0.0f) == medium,
              "a view without an aspect gets the 16:9 grid");
    fog_check(fog_pass::get_volumetric_grid(volumetric_fog_quality::medium, std::nanf("")) == medium,
              "an aspect that is not a number gets the 16:9 grid");
    const auto narrow = fog_pass::get_volumetric_grid(volumetric_fog_quality::medium, 0.01f);
    const auto panorama = fog_pass::get_volumetric_grid(volumetric_fog_quality::ultra, 100.0f);
    fog_check(narrow.width >= 16 && narrow.height == 90, "a very narrow view keeps a few columns");
    fog_check(panorama.width <= 512 && panorama.depth == 128, "a very wide view is capped in columns");
    const auto off = fog_pass::shader_params{};
    fog_check(!off.is_volumetric(), "a default-constructed value has no volumetric fog");
}

auto make_point_light(const math::vec3& position, float range, float candela) -> fog_pass::local_light
{
    fog_pass::local_light light;
    light.position = position;
    light.range = range;
    light.intensity = math::vec3(candela);
    return light;
}

void test_local_light_picking()
{
    std::printf("test_local_light_picking\n");
    const math::vec3 camera(0.0f, 0.0f, 0.0f);
    constexpr float reach = 64.0f;
    std::vector<fog_pass::local_light> lights;
    lights.push_back(make_point_light({100.0f, 0.0f, 0.0f}, 10.0f, 1000.0f));
    lights.push_back(make_point_light({70.0f, 0.0f, 0.0f}, 10.0f, 1.0f));
    lights.push_back(make_point_light({5.0f, 0.0f, 0.0f}, 10.0f, 1.0f));
    lights.push_back(make_point_light({2.0f, 0.0f, 0.0f}, 10.0f, 50.0f));
    const auto picked = fog_pass::pick_local_lights(lights, camera, reach);
    fog_check(picked.size() == 3, "a light whose reach ends past the fog's is dropped");
    fog_check(picked.size() == 3 && picked[0].position.x == 2.0f, "the brightest at the camera comes first");
    fog_check(picked.size() == 3 && picked[2].position.x == 70.0f, "a light just reaching the fog comes last");
    std::vector<fog_pass::local_light> many;
    for(std::uint32_t i = 0; i < fog_pass::max_local_lights + 8; ++i)
    {
        many.push_back(make_point_light({float(i), 0.0f, 0.0f}, 5.0f, 10.0f));
    }
    const auto capped = fog_pass::pick_local_lights(many, camera, reach);
    fog_check(capped.size() == fog_pass::max_local_lights, "no more than max_local_lights are kept");
    fog_check(!capped.empty() && capped.back().position.x < float(fog_pass::max_local_lights + 8),
              "the dimmest at the camera are the ones dropped");
}

void test_changed_lights()
{
    std::printf("test_changed_lights\n");
    std::vector<fog_pass::local_light> previous;
    for(std::uint32_t i = 0; i < 4; ++i)
    {
        previous.push_back(make_point_light({float(i), 0.0f, 0.0f}, 5.0f, 10.0f));
        previous.back().id = 100 + i;
    }
    fog_check(fog_pass::find_changed_lights(previous, previous) == 0, "lights that stay as they were did not change");
    // The same lights in another order, one moved, one dimmed, and one new in place of the last.
    std::vector<fog_pass::local_light> current = {previous[1], previous[0], previous[2], previous[3]};
    current[2].position.y = 0.5f;
    current[1].intensity = math::vec3(2.0f);
    current[3].id = 200;
    const std::uint32_t changed = fog_pass::find_changed_lights(previous, current);
    fog_check((changed & 0b0001u) == 0, "a light is matched by id, wherever it is in the list");
    fog_check((changed & 0b0010u) != 0, "a dimmed light changed");
    fog_check((changed & 0b0100u) != 0, "a moved light changed");
    fog_check((changed & 0b1000u) != 0, "a light new since the previous frame changed");
    std::vector<fog_pass::local_light> many(fog_pass::max_local_lights + 4);
    fog_check(fog_pass::find_changed_lights({}, many) == 0xFFFFFFFFu, "only max_local_lights lights have bits");
}

void test_light_volumetric_intensity_round_trip()
{
    std::printf("test_light_volumetric_intensity_round_trip\n");
    light saved;
    saved.type = light_type::spot;
    saved.volumetric_intensity = 2.5f;
    std::stringstream stream;
    {
        ser20::oarchive_associative_t archive(stream);
        try_save(archive, ser20::make_nvp("light", saved));
    }
    light loaded;
    {
        ser20::iarchive_associative_t archive(stream);
        try_load(archive, ser20::make_nvp("light", loaded));
    }
    fog_check(is_close(loaded.volumetric_intensity, 2.5f), "the volumetric intensity round-trips");
    std::stringstream old_stream(R"({"light": {"type": 1, "intensity": 8.0, "point_data": {"range": 20.0}}})");
    light old_light;
    {
        ser20::iarchive_associative_t archive(old_stream);
        fog_check(try_load(archive, ser20::make_nvp("light", old_light)), "a light saved before it loads");
    }
    fog_check(is_close(old_light.volumetric_intensity, 1.0f), "and scatters in the fog at full strength");
}

auto make_fog_volume(const math::vec3& position, fog_volume_shape shape) -> fog_pass::fog_volume
{
    fog_pass::fog_volume volume;
    volume.world.set_position(position);
    volume.settings.shape = shape;
    return volume;
}

/// Where packed volume @p index puts the world point @p world_point in its unit shape (fog.sh fog_volume_unit_position).
auto unpack_unit_position(const fog_pass::packed_volumes& packed, size_t index, const math::vec3& world_point, const math::vec3& camera)
    -> math::vec3
{
    const math::vec4 offset(world_point - camera, 1.0f);
    const size_t base = index * fog_pass::fog_volume_vec4s;
    return {math::dot(packed[base], offset), math::dot(packed[base + 1], offset), math::dot(packed[base + 2], offset)};
}

void test_fog_volumes()
{
    std::printf("test_fog_volumes\n");
    const math::vec3 camera(1.0f, 2.0f, 3.0f);
    std::vector<fog_pass::fog_volume> volumes;
    for(std::uint32_t i = 0; i < fog_pass::max_fog_volumes + 4; ++i)
    {
        volumes.push_back(make_fog_volume(camera + math::vec3(float(i) * 10.0f, 0.0f, 0.0f), fog_volume_shape::box));
    }
    const auto picked = fog_pass::pick_fog_volumes(volumes, camera);
    fog_check(picked.size() == fog_pass::max_fog_volumes, "no more than max_fog_volumes are kept");
    fog_check(!picked.empty() && is_close(picked.front().world.get_position().x, camera.x + 150.0f) &&
                  is_close(picked.back().world.get_position().x, camera.x),
              "the nearest are kept, the farthest first");

    auto stretched = make_fog_volume({10.0f, 0.0f, 0.0f}, fog_volume_shape::ellipsoid);
    stretched.world.set_scale({2.0f, 1.0f, 1.0f});
    stretched.settings.extents = {3.0f, 4.0f, 5.0f};
    stretched.settings.edge_fade = 0.5f;
    stretched.settings.mode = fog_volume_mode::subtract;
    stretched.settings.attenuation_distance = 4.0f;
    const auto packed = fog_pass::pack_fog_volumes({stretched}, camera);
    const auto centre = unpack_unit_position(packed, 0, {10.0f, 0.0f, 0.0f}, camera);
    const auto x_surface = unpack_unit_position(packed, 0, {16.0f, 0.0f, 0.0f}, camera);
    const auto z_surface = unpack_unit_position(packed, 0, {10.0f, 0.0f, -5.0f}, camera);
    fog_check(is_close(math::length(centre), 0.0f), "the volume's position is the unit shape's centre");
    fog_check(is_close(x_surface.x, 1.0f), "the scaled extent along x reaches the unit surface");
    fog_check(is_close(z_surface.z, -1.0f), "the extent along z reaches the unit surface");
    const auto params = packed[3];
    fog_check(is_close(params.x, 0.25f), "the extinction is the inverse attenuation distance");
    fog_check(is_close(params.y, 1.0f / 0.75f), "an ellipsoid's fade band is 1 - (1 - fade)^2");
    fog_check(is_close(params.z, 3.0f), "a subtracting ellipsoid is shape 1 + 2");

    scene source("source");
    auto entity = source.create_entity("fog volume");
    auto& component = entity.emplace<fog_volume_component>();
    component.enabled = false;
    component.settings = stretched.settings;
    component.settings.noise_strength = 0.6f;
    component.settings.noise_size = 3.0f;
    std::stringstream stream;
    save_to_stream(stream, source);
    scene loaded_scene("loaded");
    load_from_stream(stream, loaded_scene);
    bool found = false;
    loaded_scene.registry->view<fog_volume_component>().each(
        [&](auto, const fog_volume_component& loaded)
        {
            found = true;
            const auto& actual = loaded.settings;
            fog_check(!loaded.enabled && actual.shape == fog_volume_shape::ellipsoid && actual.mode == fog_volume_mode::subtract,
                      "enabled, shape and mode round-trip");
            fog_check(is_close(actual.extents.z, 5.0f) && is_close(actual.attenuation_distance, 4.0f) &&
                          is_close(actual.edge_fade, 0.5f) && is_close(actual.noise_strength, 0.6f) &&
                          is_close(actual.noise_size, 3.0f),
                      "the extents, density, fade and noise round-trip");
        });
    fog_check(found, "the loaded scene has the fog volume");
}

auto run_fog_suite(rtti::context& /*ctx*/) -> int
{
    g_fog_checks = 0;
    g_fog_failures = 0;
    test_settings_round_trip();
    test_volume_blending();
    test_shader_params();
    test_volumetric_grids();
    test_local_light_picking();
    test_changed_lights();
    test_light_volumetric_intensity_round_trip();
    test_fog_volumes();
    std::printf("fog: %d checks, %d failures\n", g_fog_checks, g_fog_failures);
    return g_fog_failures;
}

} // namespace

REGISTER_TEST_SUITE("fog", run_fog_suite)
