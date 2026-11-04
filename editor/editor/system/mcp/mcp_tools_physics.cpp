#include "mcp_protocol.h"
#include "mcp_tools_common.h"

#include <editor/hub/panels/inspector_panel/inspectors/inspector_physics_components.h>
#include <editor/system/mcp_manager.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
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

/// Where each body is in the physics world against where its bone is drawn, for checking a pose with numbers.
auto ragdoll_bodies_to_json(const ragdoll_component& ragdoll) -> std::string
{
    const auto& runtime = ragdoll.get_runtime();
    const auto& definition = ragdoll.get_bodies();
    std::string out = "[";
    for(size_t i = 0; i < runtime.bodies.size(); ++i)
    {
        const auto& body = runtime.bodies[i];
        const bool has_bone = body.bone && body.bone.all_of<transform_component>();
        std::string drawn = "null";
        float turn = 0.0f;
        if(has_bone)
        {
            const auto& transform = body.bone.get<transform_component>();
            drawn = vec3_to_json(transform.get_position_global());
            const math::quat delta =
                math::normalize(glm::inverse(transform.get_rotation_global()) * body.world.get_rotation());
            turn = math::degrees(2.0f * std::acos(math::clamp(std::abs(delta.w), 0.0f, 1.0f)));
        }
        out += fmt::format(
            R"({}{{"bone":{},"simulated":{},"weight":{:.2f},"physics":{},"drawn":{},"turn":{:.1f},"speed":{:.3f}}})",
            i > 0 ? "," : "",
            make_json_string(i < definition.size() ? definition[i].bone : std::string{}),
            bool_to_json(body.simulated),
            body.weight,
            vec3_to_json(body.world.get_position()),
            drawn,
            turn,
            math::length(body.linear_velocity));
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
        return {.text = "entity_id is required: the entity with the skinned model and the Ragdoll component",
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
                                         "{\"component\":\"Ragdoll\"} first",
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
    const auto result = auto_setup_ragdoll(ctx, entity, options);
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
    return {.text = fmt::format(R"({{"ok":true,"bodies":{},"skin_fitted":{},"disabled_pairs":{},"total_mass":{:.2f}}})",
                                bodies,
                                result.skin_fitted_bodies,
                                result.disabled_collisions.size(),
                                mass)};
}

auto handle_world(rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
{
    scene* scn = nullptr;
    std::string error;
    if(!require_active_scene(ctx, scn, error))
    {
        return {.text = error, .is_error = true};
    }
    auto& physics = ctx.get_cached<physics_system>();
    const auto stats = physics.get_world_stats();
    const bool playing = ctx.get_cached<play_mode>().is_active();
    int sub_steps = 4;
    float fixed_timestep = 0.02f;
    if(ctx.has<settings>())
    {
        sub_steps = ctx.get<settings>().physics.sub_steps;
        fixed_timestep = ctx.get<settings>().physics.fixed_timestep;
    }
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
                                R"("bodies":{},"awake_bodies":{},"shapes":{},"contacts":{},"constraints":{},)"
                                R"("islands":{},"step_ms":{:.3f},"joint_components":{},"active_joints":{},)"
                                R"("broken_joints":{},"ragdolls":{}}})",
                                bool_to_json(playing),
                                vec3_to_json(physics.get_gravity()),
                                fixed_timestep,
                                sub_steps,
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
         .description =
             "Ragdoll Auto Setup (the Ragdoll inspector's button, one undo step): builds the bodies of the humanoid "
             "skinned model on entity_id from its skeleton - capsules fitted to the skin, human joint ranges (hinges "
             "at elbows and knees), masses shared out of total_mass (default 70 kg) by body segment, and the body "
             "pairs that overlap at rest left apart. The entity needs a Model and a Ragdoll component (add it with "
             "scene_add_components_batch first); bones are matched by humanoid role, so any naming works. Replaces "
             "the bodies there are. Tune the result with scene_set_component_properties_batch on the Ragdoll "
             "(start_state, joint_friction, drive, bodies). Edit mode only.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"total_mass":{"type":"number"},)json"
             R"json("fit_to_skin":{"type":"boolean"},"include_hands":{"type":"boolean"},"include_feet":{"type":"boolean"},)json"
             R"json("include_neck":{"type":"boolean"}},"required":["entity_id"]})json",
         .handler = handle_ragdoll,
         .mutates_scene = true});

    registry.add(
        {.name = "physics_world",
         .description =
             "The physics world of the active scene: whether it runs (play mode), gravity, fixed step and solver "
             "sub-steps, and the running world's bodies, awake bodies, shapes, contacts, constraints, islands and "
             "last step time; then every joint component (how many are active, which broke) and every ragdoll "
             "(state, bodies simulating, average speed, seconds at rest, face up, root body position). With "
             "ragdoll_bodies true, each ragdoll also lists its bodies: bone, whether it simulates, its blend weight, "
             "where the body is in the physics world against where its bone is drawn, how many degrees the drawn "
             "bone is turned from its body, and its speed. Body and "
             "joint details are component properties: read them with scene_get_component_properties_batch "
             "(Physics: velocity, is_sleeping; Joint: state).",
         .input_schema_json = R"json({"type":"object","properties":{"ragdoll_bodies":{"type":"boolean"}}})json",
         .handler = handle_world,
         .effect = tool_effect::read_only});
}

} // namespace unravel::mcp
