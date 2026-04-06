#include "defaults.h"

#include <engine/assets/asset_manager.h>

#include <engine/animation/ecs/components/animation_component.h>
#include <engine/audio/ecs/components/audio_listener_component.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/auto_exposure_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/taa_component.h>
#include <engine/rendering/ecs/components/bloom_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/ecs/components/volume_component.h>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/physics/ecs/components/physics_component.h>

#include <logging/logging.h>
#include <string_utils/utils.h>
#include <seq/seq.h>

#include <cmath>
#include <vector>

namespace unravel
{

namespace
{


// Add a shared helper to drive the timed camera focus transition
void run_camera_focus_transition(entt::handle camera,
                                 const math::vec3& target_center,
                                 float radius,
                                 bool keep_rotation,
                                 float duration)
{
    if(!camera.all_of<transform_component, camera_component>())
    {
        return;
    }

    auto& trans_comp = camera.get<transform_component>();
    auto& camera_comp = camera.get<camera_component>();
    const auto& cam = camera_comp.get_camera();

    float aspect = cam.get_aspect_ratio();
    float fov = cam.get_fov();
    float horizontal_fov = math::degrees(2.0f * math::atan(math::tan(math::radians(fov) / 2.0f) * aspect));
    float mfov = math::min(fov, horizontal_fov);
    float target_distance = radius / (math::sin(math::radians(mfov) / 2.0f));

    math::vec3 start_position = trans_comp.get_position_global();
    float start_ortho_size = camera_comp.get_ortho_size();

    // Calculate initial target position to check proximity
    math::vec3 initial_target_position{};
    if(keep_rotation)
    {
        // Keep camera rotation unchanged: move along current forward (Z axis) so center is on view axis
        math::vec3 forward = math::normalize(trans_comp.get_z_axis_global());
        if(math::length(forward) < 0.001f)
        {
            forward = math::vec3{0.0f, 0.0f, -1.0f};
        }
        initial_target_position = target_center - target_distance * forward;
    }
    else
    {
        // Point camera towards the center using current position direction
        math::vec3 dir = math::normalize(target_center - start_position);
        if(math::length(dir) < 0.001f)
        {
            dir = math::vec3{0.0f, 0.0f, -1.0f};
        }
        initial_target_position = target_center - target_distance * dir;
    }

    // Check if we're already very close to the calculated target position
    float distance_to_target = math::length(start_position - initial_target_position);
    float proximity_threshold = target_distance * 0.15f; // 15% of target distance
    
    // If we're very close, back away to provide a wider view before focusing
    float adjusted_target_distance = target_distance;
    if(distance_to_target < proximity_threshold)
    {
        // Increase distance by 50% to back away and provide wider view
        adjusted_target_distance = target_distance * 3.0f;
    }

    // Calculate final target position with adjusted distance
    math::vec3 target_position{};
    if(keep_rotation)
    {
        math::vec3 forward = math::normalize(trans_comp.get_z_axis_global());
        if(math::length(forward) < 0.001f)
        {
            forward = math::vec3{0.0f, 0.0f, -1.0f};
        }
        target_position = target_center - adjusted_target_distance * forward;
    }
    else
    {
        math::vec3 dir = math::normalize(target_center - start_position);
        if(math::length(dir) < 0.001f)
        {
            dir = math::vec3{0.0f, 0.0f, -1.0f};
        }
        target_position = target_center - adjusted_target_distance * dir;
    }

    auto ease = seq::ease::smooth_stop;
    auto seq_duration = std::chrono::duration_cast<seq::duration_t>(std::chrono::duration<float>(duration));

    struct camera_transition_state
    {
        math::vec3 current_position;
        float current_ortho_size;
    };

    auto state = std::make_shared<camera_transition_state>();
    state->current_position = start_position;
    state->current_ortho_size = start_ortho_size;

    auto position_action = seq::change_to(state->current_position, target_position, seq_duration, state, ease);
    auto ortho_action = seq::change_to(state->current_ortho_size, radius, seq_duration, state, ease);

    auto combined_action = seq::together(position_action, ortho_action);
    combined_action.on_step.connect([camera, state, keep_rotation, target_center]()
    {
        if(camera.valid())
        {
            auto& tc = camera.get<transform_component>();
            auto& cc = camera.get<camera_component>();
            tc.set_position_global(state->current_position);
            if(!keep_rotation)
            {
                tc.look_at(target_center);
            }
            cc.set_ortho_size(state->current_ortho_size);
            cc.update(tc.get_transform_global());
        }
    });

    seq::scope::stop_all("camera_focus");
    auto action_id = seq::start(combined_action, "camera_focus");
}


// Private recursive helper; never emits the 1-unit fallback
void calc_bounds_global_impl(math::bbox& bounds, entt::handle entity, int depth)
{
    auto& tc = entity.get<transform_component>();
    const auto world_xform = tc.get_transform_global();

    // include this entity's own model AABB if it has one
    if(auto* mc = entity.try_get<model_component>())
    {
        mc->update_world_bounds(world_xform);
        auto b = mc->get_world_bounds();
        for(const auto& corner : b.get_corners())
        {
            bounds.add_point(corner);
        }
    }

    if(auto* tc = entity.try_get<text_component>())
    {
        auto b = tc->get_render_bounds();
        for(const auto& corner : b.get_corners())
        {
            bounds.add_point(world_xform.transform_coord(corner));
        }
    }

    if(auto* pc = entity.try_get<particle_emitter_component>())
    {
        auto b = pc->get_updated_world_bounds(world_xform);
        for(const auto& corner : b.get_corners())
        {
            bounds.add_point(corner);
        }
    }

    // recurse into children
    if(depth != 0) // depth<0 means infinite
    {
        for(auto child : tc.get_children())
        {
            calc_bounds_global_impl(bounds, child, depth > 0 ? depth - 1 : -1);
        }
    }
}
} // namespace

auto defaults::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str<defaults>(), __func__);

    return init_assets(ctx);
}

auto defaults::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str<defaults>(), __func__);

    {
        // 100
        font::default_thin() = {};
        // 200
        font::default_extra_light() = {};
        // 300
        font::default_light() = {};
        // 400
        font::default_regular() = {};
        // 500
        font::default_medium() = {};
        // 600
        font::default_semi_bold() = {};
        // 700
        font::default_bold() = {};
        // 800
        font::default_heavy() = {};
        // 900
        font::default_black() = {};
    }
    material::default_color_map() = {};
    material::default_normal_map() = {};
    default_textures::get().clear();
    return true;
}

auto defaults::init_assets(rtti::context& ctx) -> bool
{
    auto& manager = ctx.get_cached<asset_manager>();
    {
        const auto id = "engine:/embedded/cube";
        auto instance = std::make_shared<mesh>();
        instance->create_cube(gfx::mesh_vertex::get_layout(), 1.0f, 1.0f, 1.0f, 1, 1, 1, mesh_create_origin::center);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/cube_rounded";
        auto instance = std::make_shared<mesh>();
        instance->create_rounded_cube(gfx::mesh_vertex::get_layout(), 1.0f, 1.0f, 1.0f, 1, 1, 1, mesh_create_origin::center);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/sphere";
        auto instance = std::make_shared<mesh>();
        instance->create_sphere(gfx::mesh_vertex::get_layout(), 0.5f, 20, 20, mesh_create_origin::center);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/plane";
        auto instance = std::make_shared<mesh>();
        instance->create_plane(gfx::mesh_vertex::get_layout(), 10.0f, 10.0f, 1, 1, mesh_create_origin::center);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/cylinder";
        auto instance = std::make_shared<mesh>();
        instance->create_cylinder(gfx::mesh_vertex::get_layout(), 0.5f, 2.0f, 20, 20, mesh_create_origin::center);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/capsule_2m";
        auto instance = std::make_shared<mesh>();
        instance->create_capsule(gfx::mesh_vertex::get_layout(), 0.5f, 2.0f, 20, 20, mesh_create_origin::center);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/capsule_1m";
        auto instance = std::make_shared<mesh>();
        instance->create_capsule(gfx::mesh_vertex::get_layout(), 0.5f, 1.0f, 20, 20, mesh_create_origin::center);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/cone";
        auto instance = std::make_shared<mesh>();
        instance->create_cone(gfx::mesh_vertex::get_layout(), 0.5f, 0.0f, 2, 20, 20, mesh_create_origin::bottom);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/torus";
        auto instance = std::make_shared<mesh>();
        instance->create_torus(gfx::mesh_vertex::get_layout(), 1.0f, 0.5f, 20, 20, mesh_create_origin::center);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/terrain_test";
        constexpr uint32_t sx = 64;
        constexpr uint32_t sz = 64;
        constexpr float k_pi = 3.14159265f;
        std::vector<float> heights(static_cast<size_t>(sx + 1) * static_cast<size_t>(sz + 1));
        for(uint32_t z = 0; z <= sz; ++z)
        {
            const float tz = static_cast<float>(z) / static_cast<float>(sz);
            for(uint32_t x = 0; x <= sx; ++x)
            {
                const float tx = static_cast<float>(x) / static_cast<float>(sx);
                const float nx = tx * 2.0f - 1.0f;
                const float nz = tz * 2.0f - 1.0f;
                heights[static_cast<size_t>(z) * static_cast<size_t>(sx + 1) + static_cast<size_t>(x)] =
                    std::sin(nx * k_pi * 2.0f) * std::cos(nz * k_pi * 2.0f) * 0.5f + 0.5f;
            }
        }
        auto instance = std::make_shared<mesh>();
        instance->create_heightfield(gfx::mesh_vertex::get_layout(),
                                     heights,
                                     sx,
                                     sz,
                                     25.0f,
                                     25.0f,
                                     4.0f,
                                     mesh_create_origin::center,
                                     true);
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/teapot";
        auto instance = std::make_shared<mesh>();
        instance->create_teapot(gfx::mesh_vertex::get_layout());
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/icosahedron";
        auto instance = std::make_shared<mesh>();
        instance->create_icosahedron(gfx::mesh_vertex::get_layout());
        manager.get_asset_from_instance(id, instance);
    }
    {
        const auto id = "engine:/embedded/dodecahedron";
        auto instance = std::make_shared<mesh>();
        instance->create_dodecahedron(gfx::mesh_vertex::get_layout());
        manager.get_asset_from_instance(id, instance);
    }

    for(int i = 0; i < 20; ++i)
    {
        const auto id = std::string("engine:/embedded/icosphere") + std::to_string(i);
        auto instance = std::make_shared<mesh>();
        instance->create_icosphere(gfx::mesh_vertex::get_layout(), i);
        manager.get_asset_from_instance(id, instance);
    }

    {
        // 100
        font::default_thin() = manager.get_asset<font>("engine:/data/fonts/Inter/static/Inter-Thin.ttf");
        // 200
        font::default_extra_light() = manager.get_asset<font>("engine:/data/fonts/Inter/static/Inter-ExtraLight.ttf");
        // 300
        font::default_light() = manager.get_asset<font>("engine:/data/fonts/Inter/static/Inter-Light.ttf");
        // 400
        font::default_regular() = manager.get_asset<font>("engine:/data/fonts/Inter/static/Inter-Regular.ttf");
        // 500
        font::default_medium() = manager.get_asset<font>("engine:/data/fonts/Inter/static/Inter-Medium.ttf");
        // 600
        font::default_semi_bold() = manager.get_asset<font>("engine:/data/fonts/Inter/static/Inter-SemiBold.ttf");
        // 700
        font::default_bold() = manager.get_asset<font>("engine:/data/fonts/Inter/static/Inter-Bold.ttf");
        // 800
        font::default_heavy() = manager.get_asset<font>("engine:/data/fonts/Inter/static/Inter-ExtraBold.ttf");
        // 900
        font::default_black() = manager.get_asset<font>("engine:/data/fonts/Inter/static/Inter-Black.ttf");

    }

    {
        material::default_color_map() = manager.get_asset<gfx::texture>("engine:/data/textures/default_color.dds");
        material::default_normal_map() = manager.get_asset<gfx::texture>("engine:/data/textures/default_normal.dds");
    }
    {
        const auto id = "engine:/embedded/standard";
        auto instance = std::make_shared<pbr_material>();
        auto asset = manager.get_asset_from_instance<material>(id, instance);

        model::default_material() = asset;
    }

    {
        const auto id = "engine:/embedded/fallback";
        auto instance = std::make_shared<pbr_material>();
        instance->set_emissive_color(math::color::purple());
        instance->set_base_color(math::color::purple());
        instance->set_roughness(1.0f);
        auto asset = manager.get_asset_from_instance<material>(id, instance);

        model::fallback_material() = asset;
    }

    default_textures::get().generate();

    return true;
}

void defaults::focus_camera_on_bounds(entt::handle camera, const math::bsphere& bounds, float duration)
{
    run_camera_focus_transition(camera, bounds.position, bounds.radius, /*keep_rotation=*/true, duration);
}

void defaults::focus_camera_on_bounds(entt::handle camera, const math::bbox& bounds, float duration)
{
    const math::vec3 center = bounds.get_center();
    const float radius = math::length(bounds.get_dimensions()) / 2.0f;
    run_camera_focus_transition(camera, center, radius, /*keep_rotation=*/true, duration);
}
auto defaults::create_embedded_mesh_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle
{
    auto& am = ctx.get_cached<asset_manager>();
    const auto id = "engine:/embedded/" + string_utils::replace(string_utils::to_lower(name), " ", "_");
    auto asset = am.get_asset<mesh>(id);
    auto mesh_asset = asset.get();
    auto bounds = mesh_asset->get_bounds();
    math::vec3 position = {0.0f, bounds.get_extents().y, 0.0f};

    return create_mesh_entity_at(ctx, scn, id, position);
}

auto defaults::create_prefab_at(rtti::context& ctx, scene& scn, const std::string& key) -> entt::handle
{
    auto& am = ctx.get_cached<asset_manager>();
    auto asset = am.get_asset<prefab>(key);

    auto object = scn.instantiate(asset);
    return object;
}

auto defaults::create_prefab_at(rtti::context& ctx, scene& scn, const std::string& key, math::vec3 pos) -> entt::handle
{
    auto& am = ctx.get_cached<asset_manager>();
    auto asset = am.get_asset<prefab>(key);

    auto object = scn.instantiate(asset);

    auto& trans_comp = object.get<transform_component>();
    trans_comp.set_position_global(pos);

    return object;
}

auto defaults::create_prefab_at(rtti::context& ctx,
                                scene& scn,
                                const std::string& key,
                                const camera& cam,
                                math::vec2 pos) -> entt::handle
{
    math::vec3 projected_pos{0.0f, 0.0f, 0.0f};
    cam.viewport_to_world(pos,
                          math::plane::from_point_normal(math::vec3{0.0f, 0.0f, 0.0f}, math::vec3{0.0f, 1.0f, 0.0f}),
                          projected_pos,
                          false);

    return create_prefab_at(ctx, scn, key, projected_pos);
}

auto defaults::create_mesh_entity_at(rtti::context& ctx, scene& scn, const std::string& key, math::vec3 pos)
    -> entt::handle
{
    auto& am = ctx.get_cached<asset_manager>();
    auto asset = am.get_asset<mesh>(key);

    model mdl;

    mdl.set_lod(asset, 0);

    auto mesh_ptr = asset.get();
    if(mesh_ptr)
    {
        const auto& material_uids = mesh_ptr->get_default_material_uids();
        for(size_t i = 0; i < material_uids.size(); ++i)
        {
            auto mat_asset = am.get_asset<material>(material_uids[i]);
            if(mat_asset)
            {
                mdl.set_material(mat_asset, static_cast<uint32_t>(i));
            }
        }
    }

    std::string name = fs::path(key).stem().string();
    auto object = scn.create_entity(name);

    auto& model_comp = object.emplace<model_component>();
    model_comp.set_casts_shadow(true);
    model_comp.set_casts_reflection(false);
    model_comp.set_model(mdl);

    auto& trans_comp = object.get<transform_component>();
    trans_comp.set_position_global(pos);

    if(model_comp.is_skinned())
    {
        object.emplace<animation_component>();
    }

    return object;
}

auto defaults::create_mesh_entity_at(rtti::context& ctx,
                                     scene& scn,
                                     const std::string& key,
                                     const camera& cam,
                                     math::vec2 pos) -> entt::handle
{
    math::vec3 projected_pos{0.0f, 0.0f, 0.0f};
    cam.viewport_to_world(pos,
                          math::plane::from_point_normal(math::vec3{0.0f, 0.0f, 0.0f}, math::vec3{0.0f, 1.0f, 0.0f}),
                          projected_pos,
                          false);

    return create_mesh_entity_at(ctx, scn, key, projected_pos);
}

auto defaults::create_terrain(rtti::context& ctx, scene& scn, math::vec3 pos) -> entt::handle
{
    static constexpr const char* terrain_mesh_id = "engine:/embedded/terrain_test";
    auto object = create_mesh_entity_at(ctx, scn, terrain_mesh_id, pos);
    if(object)
    {
        object.get<tag_component>().name = "Terrain";
    }
    return object;
}

auto defaults::create_light_entity(rtti::context& ctx, scene& scn, light_type type, const std::string& name)
    -> entt::handle
{
    auto& am = ctx.get_cached<asset_manager>();

    auto object = scn.create_entity(name + " Light");

    auto& transf_comp = object.get_or_emplace<transform_component>();
    transf_comp.set_position_local({0.0f, 1.0f, 0.0f});

    if(type != light_type::point)
    {
        transf_comp.rotate_by_euler_local({50.0f, -30.0f + 180.0f, 0.0f});
    }
  

    light light_data;
    light_data.color = math::color(255, 244, 214, 255);
    light_data.type = type;

    auto& light_comp = object.get_or_emplace<light_component>();
    light_comp.set_light(light_data);

    return object;
}

auto defaults::create_reflection_probe_entity(rtti::context& ctx, scene& scn, probe_type type, const std::string& name)
    -> entt::handle
{
    auto& am = ctx.get_cached<asset_manager>();

    auto object = scn.create_entity(name + " Probe");

    auto& transf_comp = object.get_or_emplace<transform_component>();
    transf_comp.set_position_local({0.0f, 0.1f, 0.0f});

    reflection_probe probe;
    probe.method = reflect_method::static_only;
    probe.type = type;

    auto& reflection_comp = object.get_or_emplace<reflection_probe_component>();
    reflection_comp.set_probe(probe);


    return object;
}

auto defaults::create_volume_entity(rtti::context& ctx, scene& scn, const std::string& name, volume_mode mode) -> entt::handle
{
    auto object = scn.create_entity(name);
    auto& volume_comp = object.emplace<volume_component>();
    volume_comp.mode = mode;

    object.emplace<assao_component>();
    object.emplace<auto_exposure_component>().enabled = false;
    object.emplace<bloom_component>();
    object.emplace<tonemapping_component>();
    object.emplace<fxaa_component>();
    object.emplace<taa_component>();
    object.emplace<ssr_component>();
    object.emplace<ssil_component>().enabled = false;
    return object;
}




auto defaults::create_camera_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle
{
    auto object = scn.create_entity(name);

    auto& transf_comp = object.get_or_emplace<transform_component>();
    transf_comp.set_position_local({0.0f, 1.0f, -10.0f});

    object.emplace<camera_component>();


    return object;
}

auto defaults::create_ui_document_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle
{
    auto object = scn.create_entity(name);
    object.emplace<ui_document_component>();
    return object;
}

auto defaults::create_text_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle
{
    auto object = scn.create_entity(name);

    auto& text = object.emplace<text_component>();
    text.set_text("Hello World!");

    return object;
}

auto defaults::create_particle_emitter_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle
{
    auto object = scn.create_entity(name);
    object.emplace<particle_emitter_component>();
    return object;
}

auto defaults::create_audio_source_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle
{
    auto object = scn.create_entity(name);
    object.emplace<audio_source_component>();
    return object;
}

void defaults::create_default_3d_scene(rtti::context& ctx, scene& scn)
{
    create_scene_from_preset(ctx, scn, scene_preset::medium);
}

void defaults::create_scene_from_preset(rtti::context& ctx, scene& scn, scene_preset preset)
{
    auto camera = create_camera_entity(ctx, scn, "Main Camera");
    camera.emplace<audio_listener_component>();

    {
        auto object = create_light_entity(ctx, scn, light_type::directional, "Sky & Directional");
        auto& skylight = object.emplace<skylight_component>();
        auto& light_comp = object.get<light_component>();
        auto light = light_comp.get_light();

        if(preset == scene_preset::low)
        {
            skylight.set_cloud_mode(skylight_component::cloud_mode::none);
            light.shadow_params.type = sm_impl::pcf;
        }
        else if(preset == scene_preset::medium)
        {
            skylight.set_cloud_mode(skylight_component::cloud_mode::flat);
            skylight.set_cloud_speed(10.0f);
            light.shadow_params.type = sm_impl::pcf;
        }
        else if(preset == scene_preset::high)
        {
            skylight.set_cloud_mode(skylight_component::cloud_mode::flat);
            skylight.set_cloud_speed(10.0f);
            light.shadow_params.type = sm_impl::pcss;
            light.contact_shadow.enabled = true;

        }
        else if(preset == scene_preset::showcase)
        {
            skylight.set_cloud_mode(skylight_component::cloud_mode::volumetric);
            skylight.set_cloud_speed(10.0f);
            light.shadow_params.type = sm_impl::pcss;
            light.contact_shadow.enabled = true;
            light.shadow_params.resolution = sm_resolution::very_high;
        }

        light_comp.set_light(light);
    }

    {
        auto object = create_reflection_probe_entity(ctx, scn, probe_type::sphere, "Envinroment");
        auto& reflection_comp = object.get_or_emplace<reflection_probe_component>();
        auto probe = reflection_comp.get_probe();
        probe.method = reflect_method::environment;
        probe.sphere_data.range = 1000.0f;
        reflection_comp.set_probe(probe);
        if(preset == scene_preset::low)
        {
            reflection_comp.set_faces_per_frame(0);
        }
    }

    {
        auto volume = create_volume_entity(ctx, scn, "Volume Global", volume_mode::global);

        if(preset == scene_preset::low)
        {
            if(auto* comp = volume.try_get<assao_component>())
                comp->enabled = false;
            if(auto* comp = volume.try_get<bloom_component>())
                comp->enabled = false;
            if(auto* comp = volume.try_get<ssr_component>())
                comp->enabled = false;
            if(auto* comp = volume.try_get<ssil_component>())
                comp->enabled = false;
            if(auto* comp = volume.try_get<auto_exposure_component>())
                comp->enabled = false;
            if(auto* comp = volume.try_get<bloom_component>())
                comp->enabled = false;
        }
        else if(preset == scene_preset::medium)
        {
            if(auto* comp = volume.try_get<ssr_component>())
            {
                comp->settings.fidelityfx.max_rays = 4;
                comp->settings.fidelityfx.enable_half_res = true;
            }
            if(auto* comp = volume.try_get<ssil_component>())
                comp->enabled = false;

        }
        else if(preset == scene_preset::high)
        {
            if(auto* comp = volume.try_get<auto_exposure_component>())
                comp->enabled = true;
            if(auto* comp = volume.try_get<bloom_component>())
                comp->enabled = true;
            if(auto* comp = volume.try_get<ssil_component>())
            {
                comp->enabled = true;
                comp->settings.enable_half_res = true;
            }
            if(auto* comp = volume.try_get<ssil_component>())
            {
                comp->enabled = true;
                comp->settings.enable_half_res = true;
            }
            if(auto* comp = volume.try_get<taa_component>())
                comp->enabled = true;
        }
        else if(preset == scene_preset::showcase)
        {
            if(auto* comp = volume.try_get<auto_exposure_component>())
                comp->enabled = true;
            if(auto* comp = volume.try_get<bloom_component>())
                comp->enabled = true;
            if(auto* comp = volume.try_get<ssil_component>())
                comp->enabled = true;
            if(auto* comp = volume.try_get<taa_component>())
                comp->enabled = true;
        }
    }
}

void defaults::create_default_3d_scene_for_editing(rtti::context& ctx, scene& scn)
{
    {
        auto object = create_volume_entity(ctx, scn, "Volume Global", volume_mode::global);
    }
    {
        auto object = create_light_entity(ctx, scn, light_type::directional, "Sky & Directional");
        auto& skylight = object.get_or_emplace<skylight_component>();
    }

    {
        auto object = create_reflection_probe_entity(ctx, scn, probe_type::sphere, "Envinroment");
        auto& reflection_comp = object.get_or_emplace<reflection_probe_component>();
        auto probe = reflection_comp.get_probe();
        probe.method = reflect_method::environment;
        probe.sphere_data.range = 1000.0f;
        reflection_comp.set_probe(probe);
    }

}

auto defaults::create_default_3d_scene_for_preview(rtti::context& ctx, scene& scn, const usize32_t& size)
    -> entt::handle
{
    auto camera = create_camera_entity(ctx, scn, "Main Camera");
    auto post_process_volume = create_volume_entity(ctx, scn, "Volume Global", volume_mode::global);
    
    {
        auto& transf_comp = camera.get<transform_component>();
        transf_comp.set_position_local({0.0f, 6.6f, 10.0f});
        transf_comp.set_rotation_euler_local({20.0f, 180.0f, 0.0f});
        auto& camera_comp = camera.get<camera_component>();
        camera_comp.set_viewport_size(size);
    }

    {
        auto object = create_light_entity(ctx, scn, light_type::directional, "Sky & Directional");

        
        auto& transf_comp = object.get<transform_component>();
        transf_comp.set_rotation_euler_local({110.0f,  -10.0f, -35.0f});

        auto& light_comp = object.get_or_emplace<light_component>();
        auto light = light_comp.get_light();
        light.casts_shadows = false;
        light_comp.set_light(light);

        auto& skylight = object.get_or_emplace<skylight_component>();
    }

    {
        auto object = create_reflection_probe_entity(ctx, scn, probe_type::sphere, "Envinroment");
        auto& reflection_comp = object.get_or_emplace<reflection_probe_component>();
        auto probe = reflection_comp.get_probe();
        probe.method = reflect_method::environment;
        probe.sphere_data.range = 1000.0f;
        reflection_comp.set_probe(probe);
    }

    return camera;
}

template<>
void defaults::focus_camera_on_3d_scene_for_asset_preview<material>(rtti::context& ctx, const asset_preview_result& result)
{
    auto bounds = calc_bounds_sphere_global(result.object, false);
    focus_camera_on_bounds(result.camera, bounds, 0.0f);
}

template<>
void defaults::focus_camera_on_3d_scene_for_asset_preview<mesh>(rtti::context& ctx, const asset_preview_result& result)
{
    auto bounds = calc_bounds_sphere_global(result.object);
    if(bounds.radius < 1.0f)
    {
        float scale = 1.0f / bounds.radius;
        result.object.get<transform_component>().scale_by_local(math::vec3(scale));
    }

    focus_camera_on_bounds(result.camera, calc_bounds_sphere_global(result.object), 0.0f);
}

template<>
void defaults::focus_camera_on_3d_scene_for_asset_preview<prefab>(rtti::context& ctx, const asset_preview_result& result)
{
    auto bounds = calc_bounds_sphere_global(result.object);
    if(bounds.radius < 1.0f)
    {
        float scale = 1.0f / bounds.radius;
        result.object.get<transform_component>().scale_by_local(math::vec3(scale));
    }

    focus_camera_on_bounds(result.camera, calc_bounds_sphere_global(result.object), 0.0f);
}

template<>
auto defaults::create_default_3d_scene_for_asset_preview(rtti::context& ctx,
                                                         scene& scn,
                                                         const asset_handle<material>& asset,
                                                         const usize32_t& size, bool focus_camera)
    -> asset_preview_result
{
    auto camera = create_default_3d_scene_for_preview(ctx, scn, size);

    
    auto object = create_embedded_mesh_entity(ctx, scn, "Sphere");
    auto& model_comp = object.get<model_component>();
    auto model = model_comp.get_model();
    model.set_material(asset, 0);
    model_comp.set_model(model);
    model_comp.set_casts_shadow(false);
    model_comp.set_casts_reflection(false);

    if(focus_camera)
    {
        focus_camera_on_bounds(camera, calc_bounds_sphere_global(object, false), 0.0f);
    }


    return asset_preview_result{object, camera};
}

template<>
auto defaults::create_default_3d_scene_for_asset_preview(rtti::context& ctx,
                                                         scene& scn,
                                                         const asset_handle<prefab>& asset,
                                                         const usize32_t& size, bool focus_camera)
    -> asset_preview_result
{
    auto camera = create_default_3d_scene_for_preview(ctx, scn, size);

    
    auto object = scn.instantiate(asset, false);

    if(object)
    {
        if(auto model_comp = object.try_get<model_component>())
        {
            model_comp->set_casts_shadow(false);
            model_comp->set_casts_reflection(false);
        }

        auto bounds = calc_bounds_sphere_global(object);
        if(bounds.radius < 1.0f)
        {
            float scale = 1.0f / bounds.radius;
            object.get<transform_component>().scale_by_local(math::vec3(scale));
        }

        if(focus_camera)
        {
            focus_camera_on_bounds(camera, calc_bounds_sphere_global(object), 0.0f);
        }
    }
    
    return asset_preview_result{object, camera};
}

template<>
auto defaults::create_default_3d_scene_for_asset_preview(rtti::context& ctx,
                                                         scene& scn,
                                                         const asset_handle<mesh>& asset,
                                                         const usize32_t& size, bool focus_camera)
    -> asset_preview_result
{
    auto camera = create_default_3d_scene_for_preview(ctx, scn, size);

    auto object = create_mesh_entity_at(ctx, scn, asset.id());

    if(auto model_comp = object.try_get<model_component>())
    {
        model_comp->set_casts_shadow(false);
        model_comp->set_casts_reflection(false);
    }

    auto bounds = calc_bounds_sphere_global(object);
    if(bounds.radius < 1.0f)
    {
        float scale = 1.0f / bounds.radius;
        object.get<transform_component>().scale_by_local(math::vec3(scale));
    }

    if(focus_camera)
    {
        focus_camera_on_bounds(camera, calc_bounds_sphere_global(object), 0.0f);
    }

    return asset_preview_result{object, camera};
}


void defaults::focus_camera_on_entities(entt::handle camera, 
                                        hpp::span<const entt::handle> entities,
                                        float duration)
{
    if(camera.all_of<transform_component, camera_component>())
    {
        math::bbox bounds;

        bool valid = false;
        for(const auto& entity : entities)
        {
            if(!entity.valid())
            {
                return;
            }
            auto ebounds = calc_bounds_global(entity);
            bounds.add_point(ebounds.min);
            bounds.add_point(ebounds.max);

            valid = true;
        }
        if(valid)
        {
            focus_camera_on_bounds(camera, bounds, duration);
        }
    }
}

auto defaults::calc_bounds_global(entt::handle entity, int depth) -> math::bbox
{
    // 1) Get the “true” union of all models (possibly empty)
    math::bbox bounds;
    calc_bounds_global_impl(bounds, entity, depth);

    // 2) If nothing was found, fall back *once* here to a unit cube
    if(!bounds.is_populated())
    {
        const math::vec3 one{1, 1, 1};
        const auto pos = entity.get<transform_component>().get_position_global();
        bounds = math::bbox{pos - one, pos + one};
    }

    return bounds;
}

auto defaults::calc_bounds_sphere_global(entt::handle entity, bool use_bbox_diagonal) -> math::bsphere
{
    auto box = calc_bounds_global(entity);
    math::bsphere result;
    result.position = box.get_center();

    // 2) radius is half the diagonal length
    math::vec3 diag = box.get_dimensions();
    float max_abs = math::max(math::abs(diag.x), math::max(math::abs(diag.y), math::abs(diag.z)));
    float     radius = 0.5f * (use_bbox_diagonal ? math::length(diag) : max_abs);
    result.radius = radius;

    return result;
}
} // namespace unravel
