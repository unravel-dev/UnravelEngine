#include "defaults.h"

#include <engine/assets/asset_manager.h>

#include <engine/animation/ecs/components/animation_component.h>
#include <engine/audio/ecs/components/audio_listener_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/ecs/components/text_component.h>

#include <engine/physics/ecs/components/physics_component.h>

#include <logging/logging.h>
#include <string_utils/utils.h>
#include <seq/seq.h>

namespace unravel
{

namespace
{

void focus_camera_on_bounds(entt::handle camera, const math::bsphere& bounds)
{
    auto& trans_comp = camera.get<transform_component>();
    auto& camera_comp = camera.get<camera_component>();
    const auto& cam = camera_comp.get_camera();

    math::vec3 cen = bounds.position;

    float aspect = cam.get_aspect_ratio();
    float fov = cam.get_fov();
    // Get the radius of a sphere circumscribing the bounds
    float radius = bounds.radius;
    // Get the horizontal FOV, since it may be the limiting of the two FOVs to properly
    // encapsulate the objects
    float hfov = math::degrees(2.0f * math::atan(math::tan(math::radians(fov) / 2.0f) * aspect));
    // Use the smaller FOV as it limits what would get cut off by the frustum
    float mfov = math::min(fov, hfov);
    float dist = radius / (math::sin(math::radians(mfov) / 2.0f));

    trans_comp.look_at(cen);
    trans_comp.set_position_global(cen - dist * trans_comp.get_z_axis_global());
    camera_comp.set_ortho_size(radius);
    camera_comp.update(trans_comp.get_transform_global());
}

void focus_camera_on_bounds(entt::handle camera, const math::bbox& bounds)
{
    auto& trans_comp = camera.get<transform_component>();
    auto& camera_comp = camera.get<camera_component>();
    const auto& cam = camera_comp.get_camera();

    math::vec3 cen = bounds.get_center();
    math::vec3 size = bounds.get_dimensions();

    float aspect = cam.get_aspect_ratio();
    float fov = cam.get_fov();
    // Get the radius of a sphere circumscribing the bounds
    float radius = math::length(size) / 2.0f;
    // Get the horizontal FOV, since it may be the limiting of the two FOVs to properly
    // encapsulate the objects
    float horizontal_fov = math::degrees(2.0f * math::atan(math::tan(math::radians(fov) / 2.0f) * aspect));
    // Use the smaller FOV as it limits what would get cut off by the frustum
    float mfov = math::min(fov, horizontal_fov);
    float dist = radius / (math::sin(math::radians(mfov) / 2.0f));

    trans_comp.look_at(cen);
    trans_comp.set_position_global(cen - dist * trans_comp.get_z_axis_global());
    camera_comp.set_ortho_size(radius);
    camera_comp.update(trans_comp.get_transform_global());
}

// Add a shared helper to drive the timed camera focus transition
void run_camera_focus_transition(entt::handle camera,
                                 const math::vec3& target_center,
                                 float radius,
                                 bool keep_rotation,
                                 float duration)
{
    if(duration <= 0.0f || !camera.all_of<transform_component, camera_component>())
    {
        math::bsphere bs{};
        bs.position = target_center;
        bs.radius = radius;
        ::unravel::focus_camera_on_bounds(camera, bs);
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

    math::vec3 target_position{};
    if(keep_rotation)
    {
        // Keep camera rotation unchanged: move along current forward (Z axis) so center is on view axis
        math::vec3 forward = math::normalize(trans_comp.get_z_axis_global());
        if(math::length(forward) < 0.001f)
        {
            forward = math::vec3{0.0f, 0.0f, -1.0f};
        }
        target_position = target_center - target_distance * forward;
    }
    else
    {
        // Point camera towards the center using current position direction
        math::vec3 dir = math::normalize(target_center - start_position);
        if(math::length(dir) < 0.001f)
        {
            dir = math::vec3{0.0f, 0.0f, -1.0f};
        }
        target_position = target_center - target_distance * dir;
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
    combined_action.on_update.connect([camera, state, keep_rotation, target_center]()
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
    seq::start(combined_action, "camera_focus");
}

void focus_camera_on_bounds(entt::handle camera, const math::bsphere& bounds, float duration)
{
    run_camera_focus_transition(camera, bounds.position, bounds.radius, /*keep_rotation=*/true, duration);
}

void focus_camera_on_bounds(entt::handle camera, const math::bbox& bounds, float duration)
{
    const math::vec3 center = bounds.get_center();
    const float radius = math::length(bounds.get_dimensions()) / 2.0f;
    run_camera_focus_transition(camera, center, radius, /*keep_rotation=*/true, duration);
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

    return true;
}

auto defaults::create_embedded_mesh_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle
{
    auto& am = ctx.get_cached<asset_manager>();
    const auto id = "engine:/embedded/" + string_utils::replace(string_utils::to_lower(name), " ", "_");

    auto lod = am.get_asset<mesh>(id);
    model model;
    model.set_lod(lod, 0);
    model.set_material(am.get_asset<material>("engine:/embedded/standard"), 0);
    auto object = scn.create_entity(name);

    auto& transf_comp = object.get_or_emplace<transform_component>();

    auto bounds = lod.get()->get_bounds();
    transf_comp.set_position_local({0.0f, bounds.get_extents().y, 0.0f});

    auto& model_comp = object.get_or_emplace<model_component>();
    model_comp.set_casts_shadow(true);
    model_comp.set_casts_reflection(false);
    model_comp.set_model(model);

    return object;
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

    std::string name = fs::path(key).stem().string();
    auto object = scn.create_entity(name);

    // Add component and configure it.
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

    if(type == light_type::directional)
    {
        light_data.ambient_intensity = 0.05f;
    }


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

    object.emplace<tonemapping_component>();

    return object;
}

auto defaults::create_camera_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle
{
    auto object = scn.create_entity(name);

    auto& transf_comp = object.get_or_emplace<transform_component>();
    transf_comp.set_position_local({0.0f, 1.0f, -10.0f});

    object.emplace<camera_component>();
    object.emplace<assao_component>();
    object.emplace<tonemapping_component>();
    object.emplace<fxaa_component>();
    object.emplace<ssr_component>();

    return object;
}

auto defaults::create_text_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle
{
    auto object = scn.create_entity(name);

    auto& text = object.emplace<text_component>();
    text.set_text("Hello World!");

    return object;
}


void defaults::create_default_3d_scene(rtti::context& ctx, scene& scn)
{
    auto camera = create_camera_entity(ctx, scn, "Main Camera");
    camera.emplace<audio_listener_component>();

    {
        auto object = create_light_entity(ctx, scn, light_type::directional, "Sky & Directional");
        object.emplace<skylight_component>();
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

void defaults::create_default_3d_scene_for_editing(rtti::context& ctx, scene& scn)
{
    {
        auto object = create_light_entity(ctx, scn, light_type::directional, "Sky & Directional");
        object.emplace<skylight_component>();
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
    {
        auto assao_comp = camera.try_get<assao_component>();
        if(assao_comp)
        {
            assao_comp->enabled = false;
        }

        auto ssr_comp = camera.try_get<ssr_component>();
        if(ssr_comp)
        {
            ssr_comp->enabled = false;
        }
    }
    
    {
        auto& transf_comp = camera.get<transform_component>();
        transf_comp.set_position_local({10.0f, 6.6f, 10.0f});
        transf_comp.rotate_by_euler_local({0.0f, 180.0f, 0.0f});
        auto& camera_comp = camera.get<camera_component>();
        camera_comp.set_viewport_size(size);
    }

    {
        auto object = create_light_entity(ctx, scn, light_type::directional, "Sky & Directional");

        auto& light_comp = object.get_or_emplace<light_component>();
        auto light = light_comp.get_light();
        light.casts_shadows = false;
        light_comp.set_light(light);

        object.emplace<skylight_component>();
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
void defaults::create_default_3d_scene_for_asset_preview(rtti::context& ctx,
                                                         scene& scn,
                                                         const asset_handle<material>& asset,
                                                         const usize32_t& size)
{
    auto camera = create_default_3d_scene_for_preview(ctx, scn, size);

    {
        auto object = create_embedded_mesh_entity(ctx, scn, "Sphere");
        auto& model_comp = object.get<model_component>();
        auto model = model_comp.get_model();
        model.set_material(asset, 0);
        model_comp.set_model(model);
        model_comp.set_casts_shadow(false);
        model_comp.set_casts_reflection(false);

        ::unravel::focus_camera_on_bounds(camera, calc_bounds_sphere_global(object, false));
    }
}

template<>
void defaults::create_default_3d_scene_for_asset_preview(rtti::context& ctx,
                                                         scene& scn,
                                                         const asset_handle<prefab>& asset,
                                                         const usize32_t& size)
{
    auto camera = create_default_3d_scene_for_preview(ctx, scn, size);

    {
        auto object = scn.instantiate(asset);

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

            ::unravel::focus_camera_on_bounds(camera, calc_bounds_sphere_global(object));
        }
    }
}

template<>
void defaults::create_default_3d_scene_for_asset_preview(rtti::context& ctx,
                                                         scene& scn,
                                                         const asset_handle<mesh>& asset,
                                                         const usize32_t& size)
{
    auto camera = create_default_3d_scene_for_preview(ctx, scn, size);

    {
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

        ::unravel::focus_camera_on_bounds(camera, calc_bounds_sphere_global(object));
    }
}


void defaults::focus_camera_on_entities(entt::handle camera, const std::vector<entt::handle>& entities)
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
            ::unravel::focus_camera_on_bounds(camera, bounds);
        }
    }
}

void defaults::focus_camera_on_entities(entt::handle camera, 
                                        const std::vector<entt::handle>& entities,
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
            ::unravel::focus_camera_on_bounds(camera, bounds, duration);
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
    math::vec3 diag = box.max - box.min;
    float max_abs = math::max(math::abs(diag.x), math::max(math::abs(diag.y), math::abs(diag.z)));
    float     radius = 0.5f * (use_bbox_diagonal ? math::length(diag) : max_abs);
    result.radius = radius;

    return result;
}
} // namespace unravel
