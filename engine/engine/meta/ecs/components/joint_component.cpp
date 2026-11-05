#include "joint_component.hpp"
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/ecs/entity_reference.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
// Named, not anonymous: the unity build joins sources and their anonymous namespaces.
namespace joint_meta_detail
{
auto is_type(std::initializer_list<joint_type> types)
{
    std::vector<joint_type> list(types);
    return entt::property_predicate<bool>(
        [list](const entt::meta_any& object)
        {
            const auto* joint = object.try_cast<joint_component>();
            return joint != nullptr && std::find(list.begin(), list.end(), joint->get_type()) != list.end();
        });
}
} // namespace joint_meta_detail

REFLECT(joint_type)
{
    entt::meta_factory<joint_type>{}
        .type("joint_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint_type"},
            entt::attribute{"pretty_name", "Joint Type"},
        })
        .data<joint_type::fixed>("fixed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fixed"},
            entt::attribute{"pretty_name", "Fixed"},
        })
        .data<joint_type::hinge>("hinge"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hinge"},
            entt::attribute{"pretty_name", "Hinge"},
        })
        .data<joint_type::slider>("slider"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "slider"},
            entt::attribute{"pretty_name", "Slider"},
        })
        .data<joint_type::ball>("ball"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ball"},
            entt::attribute{"pretty_name", "Ball"},
        })
        .data<joint_type::distance>("distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "distance"},
            entt::attribute{"pretty_name", "Distance"},
        })
        .data<joint_type::wheel>("wheel"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "wheel"},
            entt::attribute{"pretty_name", "Wheel"},
        });
}

REFLECT(joint_limits)
{
    entt::meta_factory<joint_limits>{}
        .type("joint_limits"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint_limits"},
            entt::attribute{"pretty_name", "Joint Limits"},
        })
        .data<&joint_limits::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Keeps the joint inside its range."},
        })
        .data<&joint_limits::lower>("lower"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lower"},
            entt::attribute{"pretty_name", "Lower"},
            entt::attribute{"tooltip",
                            "Hinge and ball twist: the lowest angle in degrees. Slider and wheel suspension: the "
                            "lowest position in meters. Distance: the shortest length in meters."},
        })
        .data<&joint_limits::upper>("upper"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "upper"},
            entt::attribute{"pretty_name", "Upper"},
            entt::attribute{"tooltip",
                            "Hinge and ball twist: the highest angle in degrees. Slider and wheel suspension: the "
                            "highest position in meters. Distance: the longest length in meters."},
        })
        .data<&joint_limits::cone_angle>("cone_angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cone_angle"},
            entt::attribute{"pretty_name", "Cone Angle"},
            entt::attribute{"tooltip", "Ball: the half angle of the cone the axis may swing in, in degrees, at most 90."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", MAX_CONE_ANGLE},
        });
}

SAVE(joint_limits)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("lower", obj.lower));
    try_save(ar, ser20::make_nvp("upper", obj.upper));
    try_save(ar, ser20::make_nvp("cone_angle", obj.cone_angle));
}
SAVE_INSTANTIATE(joint_limits, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(joint_limits, ser20::oarchive_binary_t);

LOAD(joint_limits)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("lower", obj.lower));
    try_load(ar, ser20::make_nvp("upper", obj.upper));
    try_load(ar, ser20::make_nvp("cone_angle", obj.cone_angle));
}
LOAD_INSTANTIATE(joint_limits, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(joint_limits, ser20::iarchive_binary_t);

REFLECT(joint_spring)
{
    entt::meta_factory<joint_spring>{}
        .type("joint_spring"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint_spring"},
            entt::attribute{"pretty_name", "Joint Spring"},
        })
        .data<&joint_spring::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Pulls the joint towards its target. A fixed joint with a spring is soft."},
        })
        .data<&joint_spring::frequency>("frequency"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "frequency"},
            entt::attribute{"pretty_name", "Frequency"},
            entt::attribute{"tooltip", "Oscillations per second. Higher is stiffer; the mass does not matter."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&joint_spring::damping_ratio>("damping_ratio"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "damping_ratio"},
            entt::attribute{"pretty_name", "Damping Ratio"},
            entt::attribute{"tooltip", "0 bounces forever, 1 settles without overshoot."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
        })
        .data<&joint_spring::target>("target"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "target"},
            entt::attribute{"pretty_name", "Target"},
            entt::attribute{"tooltip", "Hinge: the angle in degrees the spring pulls to. Slider: the position in meters."},
        })
        .data<&joint_spring::target_rotation>("target_rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "target_rotation"},
            entt::attribute{"pretty_name", "Target Rotation"},
            entt::attribute{"tooltip",
                            "Ball: the turn of body B against body A the spring pulls to, Euler angles in degrees."},
        });
}

SAVE(joint_spring)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("frequency", obj.frequency));
    try_save(ar, ser20::make_nvp("damping_ratio", obj.damping_ratio));
    try_save(ar, ser20::make_nvp("target", obj.target));
    try_save(ar, ser20::make_nvp("target_rotation", obj.target_rotation));
}
SAVE_INSTANTIATE(joint_spring, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(joint_spring, ser20::oarchive_binary_t);

LOAD(joint_spring)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("frequency", obj.frequency));
    try_load(ar, ser20::make_nvp("damping_ratio", obj.damping_ratio));
    try_load(ar, ser20::make_nvp("target", obj.target));
    try_load(ar, ser20::make_nvp("target_rotation", obj.target_rotation));
}
LOAD_INSTANTIATE(joint_spring, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(joint_spring, ser20::iarchive_binary_t);

REFLECT(joint_motor)
{
    entt::meta_factory<joint_motor>{}
        .type("joint_motor"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint_motor"},
            entt::attribute{"pretty_name", "Joint Motor"},
        })
        .data<&joint_motor::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Drives the joint at its speed."},
        })
        .data<&joint_motor::speed>("speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "speed"},
            entt::attribute{"pretty_name", "Speed"},
            entt::attribute{"tooltip", "Hinge and wheel: degrees per second. Slider and distance: meters per second."},
        })
        .data<&joint_motor::max_force>("max_force"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_force"},
            entt::attribute{"pretty_name", "Max Force"},
            entt::attribute{"tooltip", "Hinge and wheel: newton meters. Slider and distance: newtons."},
            entt::attribute{"min", 0.0f},
        });
}

SAVE(joint_motor)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("speed", obj.speed));
    try_save(ar, ser20::make_nvp("max_force", obj.max_force));
}
SAVE_INSTANTIATE(joint_motor, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(joint_motor, ser20::oarchive_binary_t);

LOAD(joint_motor)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("speed", obj.speed));
    try_load(ar, ser20::make_nvp("max_force", obj.max_force));
}
LOAD_INSTANTIATE(joint_motor, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(joint_motor, ser20::iarchive_binary_t);

REFLECT(joint_steering)
{
    entt::meta_factory<joint_steering>{}
        .type("joint_steering"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint_steering"},
            entt::attribute{"pretty_name", "Joint Steering"},
        })
        .data<&joint_steering::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Lets the wheel steer; otherwise it points straight ahead."},
        })
        .data<&joint_steering::angle>("angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "angle"},
            entt::attribute{"pretty_name", "Angle"},
            entt::attribute{"tooltip", "The angle the wheel steers to, in degrees."},
        })
        .data<&joint_steering::limit>("limit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "limit"},
            entt::attribute{"pretty_name", "Limit"},
            entt::attribute{"tooltip", "The largest steering angle either way, in degrees."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 89.0f},
        })
        .data<&joint_steering::max_torque>("max_torque"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_torque"},
            entt::attribute{"pretty_name", "Max Torque"},
            entt::attribute{"tooltip", "Newton meters available to steer with."},
            entt::attribute{"min", 0.0f},
        });
}

SAVE(joint_steering)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("angle", obj.angle));
    try_save(ar, ser20::make_nvp("limit", obj.limit));
    try_save(ar, ser20::make_nvp("max_torque", obj.max_torque));
}
SAVE_INSTANTIATE(joint_steering, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(joint_steering, ser20::oarchive_binary_t);

LOAD(joint_steering)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("angle", obj.angle));
    try_load(ar, ser20::make_nvp("limit", obj.limit));
    try_load(ar, ser20::make_nvp("max_torque", obj.max_torque));
}
LOAD_INSTANTIATE(joint_steering, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(joint_steering, ser20::iarchive_binary_t);

REFLECT(joint_state)
{
    entt::meta_factory<joint_state>{}
        .type("joint_state"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint_state"},
            entt::attribute{"pretty_name", "Joint State"},
        })
        .data<&joint_state::is_active>("is_active"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "is_active"},
            entt::attribute{"pretty_name", "Active"},
            entt::attribute{"tooltip", "The joint exists in the running simulation."},
        })
        .data<&joint_state::is_broken>("is_broken"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "is_broken"},
            entt::attribute{"pretty_name", "Broken"},
        })
        .data<&joint_state::angle>("angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "angle"},
            entt::attribute{"pretty_name", "Angle"},
            entt::attribute{"tooltip", "Hinge angle, ball twist or wheel steering, in degrees."},
        })
        .data<&joint_state::position>("position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "position"},
            entt::attribute{"pretty_name", "Position"},
            entt::attribute{"tooltip", "Slider position or distance length, in meters."},
        })
        .data<&joint_state::speed>("speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "speed"},
            entt::attribute{"pretty_name", "Speed"},
            entt::attribute{"tooltip", "Degrees per second for hinges and wheels, meters per second for sliders."},
        })
        .data<&joint_state::force>("force"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "force"},
            entt::attribute{"pretty_name", "Force"},
            entt::attribute{"tooltip", "Force holding the bodies, in newtons, world space."},
        })
        .data<&joint_state::torque>("torque"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "torque"},
            entt::attribute{"pretty_name", "Torque"},
            entt::attribute{"tooltip", "Torque holding the bodies, in newton meters, world space."},
        });
}

REFLECT(joint_component)
{
    using joint_meta_detail::is_type;
    entt::meta_factory<joint_component>{}
        .type("joint_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint_component"},
            entt::attribute{"category", "Physics"},
            entt::attribute{"pretty_name", "Joint"},
            entt::attribute{"tooltip",
                            "Connects two bodies. The joint's own transform, moved by the anchor and turned by "
                            "the axis rotation, is the joint frame; its X axis is the hinge, slider, twist or wheel "
                            "axis. Body A empty hangs the joint on the world; body B empty takes this entity."},
        })
        .func<&component_meta<joint_component>::exists>("component_exists"_hs)
        .func<&component_meta<joint_component>::add>("component_add"_hs)
        .func<&component_meta<joint_component>::remove>("component_remove"_hs)
        .func<&component_meta<joint_component>::save>("component_save"_hs)
        .func<&component_meta<joint_component>::load>("component_load"_hs)
        .data<&joint_component::set_type, &joint_component::get_type>("type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "type"},
            entt::attribute{"pretty_name", "Type"},
            entt::attribute{"tooltip",
                            "Fixed holds rigidly. Hinge turns about the axis. Slider moves along it. Ball turns any "
                            "way in a cone. Distance keeps two points apart (ropes, springs). Wheel spins about the "
                            "axis on a suspension along Y."},
        })
        .data<&joint_component::set_body_a, &joint_component::get_body_a>("body_a"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "body_a"},
            entt::attribute{"pretty_name", "Body A"},
            entt::attribute{"tooltip", "The body the joint hangs on. Empty hangs it on the world."},
        })
        .data<&joint_component::set_body_b, &joint_component::get_body_b>("body_b"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "body_b"},
            entt::attribute{"pretty_name", "Body B"},
            entt::attribute{"tooltip", "The body the joint holds. Empty takes this entity, which then needs a physics component."},
        })
        .data<&joint_component::set_anchor, &joint_component::get_anchor>("anchor"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "anchor"},
            entt::attribute{"pretty_name", "Anchor"},
            entt::attribute{"tooltip", "Offset of the joint frame from this entity, in its space."},
        })
        .data<&joint_component::set_axis_rotation, &joint_component::get_axis_rotation>("axis_rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "axis_rotation"},
            entt::attribute{"pretty_name", "Axis Rotation"},
            entt::attribute{"tooltip", "Turn of the joint frame against this entity, Euler angles in degrees."},
        })
        .data<&joint_component::set_collide_connected, &joint_component::get_collide_connected>("collide_connected"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collide_connected"},
            entt::attribute{"pretty_name", "Collide Connected"},
            entt::attribute{"tooltip", "Whether the two jointed bodies still collide with each other."},
        })
        .data<&joint_component::set_limits, &joint_component::get_limits>("limits"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "limits"},
            entt::attribute{"pretty_name", "Limits"},
            entt::attribute{"predicate",
                            is_type({joint_type::hinge, joint_type::slider, joint_type::ball, joint_type::distance,
                                     joint_type::wheel})},
        })
        .data<&joint_component::set_spring, &joint_component::get_spring>("spring"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spring"},
            entt::attribute{"pretty_name", "Spring"},
            entt::attribute{"tooltip", "For a wheel, the suspension."},
        })
        .data<&joint_component::set_motor, &joint_component::get_motor>("motor"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "motor"},
            entt::attribute{"pretty_name", "Motor"},
            entt::attribute{"predicate",
                            is_type({joint_type::hinge, joint_type::slider, joint_type::distance, joint_type::wheel})},
        })
        .data<&joint_component::set_steering, &joint_component::get_steering>("steering"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "steering"},
            entt::attribute{"pretty_name", "Steering"},
            entt::attribute{"predicate", is_type({joint_type::wheel})},
        })
        .data<&joint_component::set_friction, &joint_component::get_friction>("friction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "friction"},
            entt::attribute{"pretty_name", "Friction"},
            entt::attribute{"tooltip",
                            "Resistance to motion while the motor is off, in newton meters (newtons for sliders): "
                            "stiff hinges, heavy doors."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"predicate",
                            is_type({joint_type::hinge, joint_type::slider, joint_type::ball, joint_type::distance,
                                     joint_type::wheel})},
        })
        .data<&joint_component::set_distance, &joint_component::get_distance>("distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "distance"},
            entt::attribute{"pretty_name", "Distance"},
            entt::attribute{"tooltip", "The length kept, in meters. 0 takes the distance between the anchors at the start."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"predicate", is_type({joint_type::distance})},
        })
        .data<&joint_component::set_connected_anchor, &joint_component::get_connected_anchor>("connected_anchor"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "connected_anchor"},
            entt::attribute{"pretty_name", "Connected Anchor"},
            entt::attribute{"tooltip", "The point held on body B, in body B's space."},
            entt::attribute{"predicate", is_type({joint_type::distance})},
        })
        .data<&joint_component::set_break_force, &joint_component::get_break_force>("break_force"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "break_force"},
            entt::attribute{"pretty_name", "Break Force"},
            entt::attribute{"tooltip", "Newtons above which the joint breaks. 0 never breaks."},
            entt::attribute{"min", 0.0f},
        })
        .data<&joint_component::set_break_torque, &joint_component::get_break_torque>("break_torque"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "break_torque"},
            entt::attribute{"pretty_name", "Break Torque"},
            entt::attribute{"tooltip", "Newton meters above which the joint breaks. 0 never breaks."},
            entt::attribute{"min", 0.0f},
        })
        .data<nullptr, static_cast<const joint_state& (joint_component::*)() const noexcept>(&joint_component::get_state)>(
            "state"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "state"},
            entt::attribute{"pretty_name", "State"},
            entt::attribute{"tooltip", "What the running simulation reports about the joint."},
        });
}

SAVE(joint_component)
{
    try_save(ar, ser20::make_nvp("type", obj.get_type()));
    save_entity_reference(ar, "body_a", obj.get_body_a());
    save_entity_reference(ar, "body_b", obj.get_body_b());
    try_save(ar, ser20::make_nvp("anchor", obj.get_anchor()));
    try_save(ar, ser20::make_nvp("axis_rotation", obj.get_axis_rotation()));
    try_save(ar, ser20::make_nvp("collide_connected", obj.get_collide_connected()));
    try_save(ar, ser20::make_nvp("limits", obj.get_limits()));
    try_save(ar, ser20::make_nvp("spring", obj.get_spring()));
    try_save(ar, ser20::make_nvp("motor", obj.get_motor()));
    try_save(ar, ser20::make_nvp("steering", obj.get_steering()));
    try_save(ar, ser20::make_nvp("friction", obj.get_friction()));
    try_save(ar, ser20::make_nvp("distance", obj.get_distance()));
    try_save(ar, ser20::make_nvp("connected_anchor", obj.get_connected_anchor()));
    try_save(ar, ser20::make_nvp("break_force", obj.get_break_force()));
    try_save(ar, ser20::make_nvp("break_torque", obj.get_break_torque()));
}
SAVE_INSTANTIATE(joint_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(joint_component, ser20::oarchive_binary_t);

LOAD(joint_component)
{
    joint_type type{joint_type::ball};
    if(try_load(ar, ser20::make_nvp("type", type)))
    {
        obj.set_type(type);
    }
    entt::handle body_a;
    load_entity_reference(ar, "body_a", body_a);
    obj.set_body_a(body_a);
    entt::handle body_b;
    load_entity_reference(ar, "body_b", body_b);
    obj.set_body_b(body_b);
    math::vec3 anchor{0.0f};
    if(try_load(ar, ser20::make_nvp("anchor", anchor)))
    {
        obj.set_anchor(anchor);
    }
    math::vec3 axis_rotation{0.0f};
    if(try_load(ar, ser20::make_nvp("axis_rotation", axis_rotation)))
    {
        obj.set_axis_rotation(axis_rotation);
    }
    bool collide_connected{false};
    if(try_load(ar, ser20::make_nvp("collide_connected", collide_connected)))
    {
        obj.set_collide_connected(collide_connected);
    }
    joint_limits limits;
    if(try_load(ar, ser20::make_nvp("limits", limits)))
    {
        obj.set_limits(limits);
    }
    joint_spring spring;
    if(try_load(ar, ser20::make_nvp("spring", spring)))
    {
        obj.set_spring(spring);
    }
    joint_motor motor;
    if(try_load(ar, ser20::make_nvp("motor", motor)))
    {
        obj.set_motor(motor);
    }
    joint_steering steering;
    if(try_load(ar, ser20::make_nvp("steering", steering)))
    {
        obj.set_steering(steering);
    }
    float friction{0.0f};
    if(try_load(ar, ser20::make_nvp("friction", friction)))
    {
        obj.set_friction(friction);
    }
    float distance{0.0f};
    if(try_load(ar, ser20::make_nvp("distance", distance)))
    {
        obj.set_distance(distance);
    }
    math::vec3 connected_anchor{0.0f};
    if(try_load(ar, ser20::make_nvp("connected_anchor", connected_anchor)))
    {
        obj.set_connected_anchor(connected_anchor);
    }
    float break_force{0.0f};
    if(try_load(ar, ser20::make_nvp("break_force", break_force)))
    {
        obj.set_break_force(break_force);
    }
    float break_torque{0.0f};
    if(try_load(ar, ser20::make_nvp("break_torque", break_torque)))
    {
        obj.set_break_torque(break_torque);
    }
}
LOAD_INSTANTIATE(joint_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(joint_component, ser20::iarchive_binary_t);

} // namespace unravel
