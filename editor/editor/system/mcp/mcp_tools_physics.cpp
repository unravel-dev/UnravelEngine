#include "mcp_protocol.h"
#include "mcp_tools_common.h"

#include <editor/editing/editor_actions.h>
#include <editor/system/mcp_manager.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/physics/ecs/components/joint_component.h>
#include <engine/physics/ecs/components/ragdoll_component.h>
#include <engine/physics/ecs/systems/physics_system.h>
#include <engine/settings/settings.h>

#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{
auto shape_to_json(const physics_compound_shape& shape) -> std::string
{
    if(const auto* capsule = hpp::get_if<physics_capsule_shape>(&shape.shape))
    {
        return fmt::format(R"({{"type":"capsule","radius":{:.4f},"length":{:.4f}}})", capsule->radius, capsule->length);
    }
    if(const auto* box = hpp::get_if<physics_box_shape>(&shape.shape))
    {
        return fmt::format(R"({{"type":"box","extends":{}}})", vec3_to_json(box->extends));
    }
    if(const auto* sphere = hpp::get_if<physics_sphere_shape>(&shape.shape))
    {
        return fmt::format(R"({{"type":"sphere","radius":{:.4f}}})", sphere->radius);
    }
    return R"({"type":"other"})";
}

auto joint_type_name(ragdoll_joint_type type) -> const char*
{
    switch(type)
    {
        case ragdoll_joint_type::hinge:
            return "hinge";
        case ragdoll_joint_type::fixed:
            return "fixed";
        case ragdoll_joint_type::ball:
        default:
            return "ball";
    }
}

auto state_name(ragdoll_state state) -> const char*
{
    switch(state)
    {
        case ragdoll_state::simulated:
            return "simulated";
        case ragdoll_state::powered:
            return "powered";
        case ragdoll_state::animated:
        default:
            return "animated";
    }
}

auto body_to_json(const ragdoll_body& body, bool is_root) -> std::string
{
    return fmt::format(R"({{"bone":{},"shape":{},"mass":{:.3f},"joint":{},"swing":{:.1f},"twist":[{:.1f},{:.1f}]}})",
                       make_json_string(body.bone),
                       shape_to_json(body.shape),
                       body.mass,
                       is_root ? "null" : make_json_string(joint_type_name(body.joint)),
                       body.swing_limit,
                       body.twist_lower,
                       body.twist_upper);
}

/// A body's physics pose between its last two fixed steps.
struct interpolated_pose
{
    math::vec3 position{};
    math::quat rotation{};
};

/// Where the drawing puts a simulating body: between its last two fixed steps by @p step_fraction, as show_ragdolls
/// places it, so it trails the last step by up to one step by design.
auto get_interpolated_pose(const ragdoll_body_runtime& body, float step_fraction) -> interpolated_pose
{
    return {math::mix(body.world_previous.get_position(), body.world.get_position(), step_fraction),
            math::slerp(body.world_previous.get_rotation(), body.world.get_rotation(), step_fraction)};
}

/// Degrees between two rotations.
auto get_turn_degrees(const math::quat& from, const math::quat& to) -> float
{
    const math::quat delta = math::normalize(glm::inverse(from) * to);
    return math::degrees(2.0f * std::acos(math::clamp(std::abs(delta.w), 0.0f, 1.0f)));
}

/// One body: its last step's position, the pose the drawing interpolates to, and its bone as drawn.
auto ragdoll_body_pose_to_json(const ragdoll_body_runtime& body, const std::string& bone_name, float step_fraction)
    -> std::string
{
    const bool has_bone = body.bone && body.bone.all_of<transform_component>();
    const auto interpolated = get_interpolated_pose(body, step_fraction);
    const auto* transform = has_bone ? &body.bone.get<transform_component>() : nullptr;
    const bool can_compare = transform != nullptr && body.has_physics_pose;
    return fmt::format(
        R"({{"bone":{},"simulated":{},"weight":{:.2f},"physics":{},"interpolated":{},"drawn":{},"turn":{},)"
        R"("speed":{:.3f}}})",
        make_json_string(bone_name),
        bool_to_json(body.simulated),
        body.weight,
        body.has_physics_pose ? vec3_to_json(body.world.get_position()) : "null",
        body.has_physics_pose ? vec3_to_json(interpolated.position) : "null",
        transform != nullptr ? vec3_to_json(transform->get_position_global()) : "null",
        can_compare ? fmt::format("{:.1f}", get_turn_degrees(transform->get_rotation_global(), interpolated.rotation))
                    : "null",
        math::length(body.linear_velocity));
}

/// Where each body is in the physics world against where its bone is drawn, for checking a pose with numbers. The
/// interpolated pose uses the step fraction the ragdoll was last shown at, which a late show after LateUpdate keeps.
auto ragdoll_bodies_to_json(const ragdoll_component& ragdoll) -> std::string
{
    const auto& runtime = ragdoll.get_runtime();
    const auto& definition = ragdoll.get_bodies();
    std::string out = "[";
    for(size_t i = 0; i < runtime.bodies.size(); ++i)
    {
        out += (i > 0 ? "," : "") +
               ragdoll_body_pose_to_json(runtime.bodies[i],
                                         i < definition.size() ? definition[i].bone : std::string{},
                                         runtime.shown_step_fraction);
    }
    return out + "]";
}

auto ragdoll_to_json(entt::handle entity, const ragdoll_component& ragdoll, bool with_bodies) -> std::string
{
    const auto& runtime = ragdoll.get_runtime();
    int simulated = 0;
    for(const auto& body : runtime.bodies)
    {
        simulated += body.simulated ? 1 : 0;
    }
    const auto* tag = entity.try_get<tag_component>();
    return fmt::format(R"({{"entity_id":{},"name":{},"state":"{}","built":{},"bodies":{},"simulated_bodies":{},)"
                       R"("average_speed":{:.3f},"rest_time":{:.2f},"face_up":{},"root":{}{}}})",
                       make_json_string(entity_id_string(entity)),
                       make_json_string(tag ? tag->name : std::string{}),
                       state_name(ragdoll.get_state()),
                       bool_to_json(runtime.is_built),
                       ragdoll.get_bodies().size(),
                       simulated,
                       ragdoll.get_average_speed(),
                       ragdoll.get_rest_time(),
                       bool_to_json(ragdoll.is_face_up()),
                       vec3_to_json(ragdoll.get_root_pose().get_position()),
                       with_bodies ? R"(,"body_poses":)" + ragdoll_bodies_to_json(ragdoll) : std::string{});
}

auto handle_ragdoll(rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
{
    scene* scn = nullptr;
    std::string error;
    if(!require_edit_scene(ctx, scn, error))
    {
        return {.text = error, .is_error = true};
    }
    std::string id;
    if(!read_string(args, "entity_id", id))
    {
        return {.text = "entity_id is required: the entity with the skinned model and the Ragdoll component.",
                .is_error = true};
    }
    auto entity = resolve_entity(*scn, id, error);
    if(!entity)
    {
        return {.text = error, .is_error = true};
    }
    if(!entity.all_of<ragdoll_component>())
    {
        return {.text = "Entity " + id + " has no Ragdoll component: add it with scene_add_components_batch "
                                         "{\"component\":\"Ragdoll\"} first.",
                .is_error = true};
    }
    ragdoll_build_options options;
    double total_mass = options.total_mass;
    if(read_double(args, "total_mass", total_mass))
    {
        options.total_mass = float(total_mass);
    }
    read_bool(args, "fit_to_skin", options.fit_to_skin);
    read_bool(args, "include_hands", options.include_hands);
    read_bool(args, "include_feet", options.include_feet);
    read_bool(args, "include_neck", options.include_neck);
    const auto setup = editor_actions::auto_setup_ragdoll(ctx, entity, options);
    const auto& result = setup.build;
    if(!result.ok)
    {
        return {.text = result.error, .is_error = true};
    }
    std::string bodies = "[";
    for(size_t i = 0; i < result.bodies.size(); ++i)
    {
        bodies += (i > 0 ? "," : "") + body_to_json(result.bodies[i], i == 0);
    }
    bodies += "]";
    float mass = 0.0f;
    for(const auto& body : result.bodies)
    {
        mass += body.mass;
    }
    return {.text = fmt::format(
                R"({{"ok":true,"changed":{},"bodies":{},"skin_fitted":{},"disabled_pairs":{},"total_mass":{:.2f}}})",
                bool_to_json(setup.is_changed),
                bodies,
                result.skin_fitted_bodies,
                result.disabled_collisions.size(),
                mass)};
}

auto handle_world(rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
{
    // The physics world simulates the main scene, also while a prefab is open in prefab mode: the stats, the joints
    // and the ragdolls all describe that one scene.
    scene* scn = &ctx.get_cached<ecs>().get_scene();
    if(!scn->registry)
    {
        return {.text = "No scene is open: open one with scene_open, or make one with scene_new_from_preset.",
                .is_error = true};
    }
    auto& physics = ctx.get_cached<physics_system>();
    const auto stats = physics.get_world_stats();
    // The same test get_gravity makes: the world runs from the end of the splash screen to play stop, paused or not.
    const bool is_running = ctx.get_cached<play_mode>().is_simulation_running();
    const auto physics_settings = ctx.has<settings>() ? ctx.get<settings>().physics : settings::physics_settings{};
    bool with_bodies = false;
    read_bool(args, "ragdoll_bodies", with_bodies);
    std::string ragdolls = "[";
    scn->registry->view<ragdoll_component>().each(
        [&](auto e, auto& ragdoll)
        {
            ragdolls +=
                (ragdolls.size() > 1 ? "," : "") + ragdoll_to_json(scn->create_handle(e), ragdoll, with_bodies);
        });
    ragdolls += "]";
    int joints = 0;
    int active_joints = 0;
    std::vector<std::string> broken;
    scn->registry->view<joint_component>().each(
        [&](auto e, auto& joint)
        {
            ++joints;
            active_joints += joint.get_state().is_active ? 1 : 0;
            if(joint.get_state().is_broken)
            {
                broken.push_back(entity_id_string(scn->create_handle(e)));
            }
        });
    return {.text = fmt::format(R"({{"playing":{},"gravity":{},"fixed_timestep":{:.4f},"sub_steps":{},)"
                                R"("step_fraction":{:.3f},)"
                                R"("bodies":{},"awake_bodies":{},"shapes":{},"contacts":{},"constraints":{},)"
                                R"("islands":{},"step_ms":{:.3f},"joint_components":{},"active_joints":{},)"
                                R"("broken_joints":{},"ragdolls":{}}})",
                                bool_to_json(is_running),
                                vec3_to_json(physics.get_gravity()),
                                physics_settings.fixed_timestep,
                                physics_settings.sub_steps,
                                physics.get_step_fraction(),
                                stats.body_count,
                                stats.awake_body_count,
                                stats.shape_count,
                                stats.contact_count,
                                stats.joint_count,
                                stats.island_count,
                                stats.step_ms,
                                joints,
                                active_joints,
                                strings_to_json_array(broken),
                                ragdolls)};
}
} // namespace

void register_physics_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "physics_ragdoll",
         .description = fmt::format(
             "Ragdoll Auto Setup (the Ragdoll inspector's button, one undo step): builds the bodies of the humanoid "
             "skinned model on entity_id from its skeleton - capsules fitted to the skin, boxes on the soles of the "
             "feet, human joint ranges (hinges at elbows and knees, swing cones of at most 90 degrees), masses shared "
             "out of total_mass (kilograms above 0, default {:g}) by body segment, and the body pairs that overlap at "
             "rest left apart. The entity needs a Model and a Ragdoll component (add it with "
             "scene_add_components_batch first); bones are matched by humanoid role, so any naming works. Replaces "
             "the bodies there are; when the result matches them, \"changed\" is false and no undo step is made. "
             "Tune the result with scene_set_component_properties_batch on the Ragdoll (start_state, joint_friction, "
             "drive, bodies). Edit mode only.",
             ragdoll_build_options{}.total_mass),
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"total_mass":{"type":"number"},)json"
             R"json("fit_to_skin":{"type":"boolean"},"include_hands":{"type":"boolean"},"include_feet":{"type":"boolean"},)json"
             R"json("include_neck":{"type":"boolean"}},"required":["entity_id"]})json",
         .handler = handle_ragdoll,
         .mutates_scene = true});

    registry.add(
        {.name = "physics_world",
         .description =
             "The physics world of the main scene, also while a prefab is open: whether it runs (play mode after the "
             "splash screen, paused or not), gravity, fixed step, solver sub-steps, step_fraction (how far between "
             "the last two fixed steps the frame is drawn: interpolated bodies, characters and ragdolls show there), "
             "and the running world's bodies, awake bodies, shapes, contacts, constraints, islands and last step "
             "time; then every joint component (how many are active, which broke) and every ragdoll (state, bodies "
             "simulating, average speed, seconds at rest, face up, root body position). With ragdoll_bodies true, "
             "each ragdoll also lists its bodies: bone, whether it simulates, its blend weight, physics (the last "
             "step's position), interpolated (the position the drawing uses, which trails physics by up to one step "
             "by design), drawn (its bone's position), turn (degrees between the drawn bone and the interpolated "
             "body) and speed. Compare drawn with interpolated, not with physics: a simulating body at weight 1 is "
             "drawn turned as interpolated (turn near 0), and only the root body's bone also stands at that "
             "position, since the others keep their bone lengths; a lower weight is drawn partly from the animation. "
             "Bullet simulates no joints or ragdolls. Body and joint details are component properties: read them "
             "with scene_get_component_properties_batch (Physics: velocity, is_sleeping; Joint: state).",
         .input_schema_json = R"json({"type":"object","properties":{"ragdoll_bodies":{"type":"boolean"}}})json",
         .handler = handle_world,
         .effect = tool_effect::read_only});
}

} // namespace unravel::mcp
