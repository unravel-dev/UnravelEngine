#include "ragdoll_component.hpp"
#include "physics_component.hpp"
#include <engine/meta/core/math/vector.hpp>
#include <engine/physics/ecs/components/joint_component.h>
#include <engine/meta/layers/layer_mask.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>
#include <serialization/types/variant.hpp>
#include <serialization/types/vector.hpp>

namespace unravel
{

REFLECT(ragdoll_state)
{
    entt::meta_factory<ragdoll_state>{}
        .type("ragdoll_state"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ragdoll_state"},
            entt::attribute{"pretty_name", "Ragdoll State"},
        })
        .data<ragdoll_state::animated>("animated"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "animated"},
            entt::attribute{"pretty_name", "Animated"},
        })
        .data<ragdoll_state::simulated>("simulated"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "simulated"},
            entt::attribute{"pretty_name", "Simulated"},
        })
        .data<ragdoll_state::powered>("powered"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "powered"},
            entt::attribute{"pretty_name", "Powered"},
        });
}

REFLECT(ragdoll_joint_type)
{
    entt::meta_factory<ragdoll_joint_type>{}
        .type("ragdoll_joint_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ragdoll_joint_type"},
            entt::attribute{"pretty_name", "Ragdoll Joint Type"},
        })
        .data<ragdoll_joint_type::ball>("ball"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ball"},
            entt::attribute{"pretty_name", "Ball"},
        })
        .data<ragdoll_joint_type::hinge>("hinge"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hinge"},
            entt::attribute{"pretty_name", "Hinge"},
        })
        .data<ragdoll_joint_type::fixed>("fixed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fixed"},
            entt::attribute{"pretty_name", "Fixed"},
        });
}

REFLECT(ragdoll_body)
{
    entt::meta_factory<ragdoll_body>{}
        .type("ragdoll_body"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ragdoll_body"},
            entt::attribute{"pretty_name", "Ragdoll Body"},
        })
        .data<&ragdoll_body::bone>("bone"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone"},
            entt::attribute{"pretty_name", "Bone"},
            entt::attribute{"tooltip", "Name of the bone the body moves."},
        })
        .data<&ragdoll_body::shape>("shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape"},
            entt::attribute{"pretty_name", "Shape"},
            entt::attribute{"tooltip", "Collision shape in the bone's space."},
        })
        .data<&ragdoll_body::mass>("mass"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mass"},
            entt::attribute{"pretty_name", "Mass"},
            entt::attribute{"tooltip", "Kilograms."},
            entt::attribute{"min", 0.01f},
        })
        .data<&ragdoll_body::joint>("joint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint"},
            entt::attribute{"pretty_name", "Joint"},
            entt::attribute{"tooltip",
                            "How the body turns against its parent body: a cone with a twist, a hinge, or not at "
                            "all. The root body has no joint."},
        })
        .data<&ragdoll_body::joint_rotation>("joint_rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint_rotation"},
            entt::attribute{"pretty_name", "Joint Rotation"},
            entt::attribute{"tooltip",
                            "Turn of the joint frame against the bone, Euler angles in degrees. Its X axis is the "
                            "twist or hinge axis."},
        })
        .data<&ragdoll_body::swing_offset>("swing_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "swing_offset"},
            entt::attribute{"pretty_name", "Swing Offset"},
            entt::attribute{"tooltip",
                            "Ball: turn of the swing cone's centre away from the bone's rest direction, Euler angles "
                            "in degrees in the joint frame. Lets a limb rest away from its bind pose, like arms "
                            "hanging from a T-pose."},
        })
        .data<&ragdoll_body::swing_limit>("swing_limit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "swing_limit"},
            entt::attribute{"pretty_name", "Swing Limit"},
            entt::attribute{"tooltip", "Ball: half angle of the cone the joint axis swings in, degrees, at most 90."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", MAX_CONE_ANGLE},
        })
        .data<&ragdoll_body::twist_lower>("twist_lower"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "twist_lower"},
            entt::attribute{"pretty_name", "Twist Lower"},
            entt::attribute{"tooltip", "Ball: lowest twist. Hinge: lowest angle. Degrees."},
            entt::attribute{"min", -178.0f},
            entt::attribute{"max", 178.0f},
        })
        .data<&ragdoll_body::twist_upper>("twist_upper"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "twist_upper"},
            entt::attribute{"pretty_name", "Twist Upper"},
            entt::attribute{"tooltip", "Ball: highest twist. Hinge: highest angle. Degrees."},
            entt::attribute{"min", -178.0f},
            entt::attribute{"max", 178.0f},
        })
        .data<&ragdoll_body::friction_scale>("friction_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "friction_scale"},
            entt::attribute{"pretty_name", "Friction Scale"},
            entt::attribute{"tooltip", "Multiplies the ragdoll's joint friction for this joint."},
            entt::attribute{"min", 0.0f},
        })
        .data<&ragdoll_body::drive_scale>("drive_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "drive_scale"},
            entt::attribute{"pretty_name", "Drive Scale"},
            entt::attribute{"tooltip", "Multiplies the ragdoll's drive frequency for this body."},
            entt::attribute{"min", 0.0f},
        });
}

SAVE(ragdoll_body)
{
    try_save(ar, ser20::make_nvp("bone", obj.bone));
    try_save(ar, ser20::make_nvp("shape", obj.shape));
    try_save(ar, ser20::make_nvp("mass", obj.mass));
    try_save(ar, ser20::make_nvp("joint", obj.joint));
    try_save(ar, ser20::make_nvp("joint_rotation", obj.joint_rotation));
    try_save(ar, ser20::make_nvp("swing_offset", obj.swing_offset));
    try_save(ar, ser20::make_nvp("swing_limit", obj.swing_limit));
    try_save(ar, ser20::make_nvp("twist_lower", obj.twist_lower));
    try_save(ar, ser20::make_nvp("twist_upper", obj.twist_upper));
    try_save(ar, ser20::make_nvp("friction_scale", obj.friction_scale));
    try_save(ar, ser20::make_nvp("drive_scale", obj.drive_scale));
}
SAVE_INSTANTIATE(ragdoll_body, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ragdoll_body, ser20::oarchive_binary_t);

LOAD(ragdoll_body)
{
    try_load(ar, ser20::make_nvp("bone", obj.bone));
    try_load(ar, ser20::make_nvp("shape", obj.shape));
    try_load(ar, ser20::make_nvp("mass", obj.mass));
    try_load(ar, ser20::make_nvp("joint", obj.joint));
    try_load(ar, ser20::make_nvp("joint_rotation", obj.joint_rotation));
    try_load(ar, ser20::make_nvp("swing_offset", obj.swing_offset));
    try_load(ar, ser20::make_nvp("swing_limit", obj.swing_limit));
    try_load(ar, ser20::make_nvp("twist_lower", obj.twist_lower));
    try_load(ar, ser20::make_nvp("twist_upper", obj.twist_upper));
    try_load(ar, ser20::make_nvp("friction_scale", obj.friction_scale));
    try_load(ar, ser20::make_nvp("drive_scale", obj.drive_scale));
}
LOAD_INSTANTIATE(ragdoll_body, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ragdoll_body, ser20::iarchive_binary_t);

REFLECT(ragdoll_bone_pair)
{
    entt::meta_factory<ragdoll_bone_pair>{}
        .type("ragdoll_bone_pair"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ragdoll_bone_pair"},
            entt::attribute{"pretty_name", "Bone Pair"},
        })
        .data<&ragdoll_bone_pair::bone_a>("bone_a"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone_a"},
            entt::attribute{"pretty_name", "Bone A"},
        })
        .data<&ragdoll_bone_pair::bone_b>("bone_b"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone_b"},
            entt::attribute{"pretty_name", "Bone B"},
        });
}

SAVE(ragdoll_bone_pair)
{
    try_save(ar, ser20::make_nvp("bone_a", obj.bone_a));
    try_save(ar, ser20::make_nvp("bone_b", obj.bone_b));
}
SAVE_INSTANTIATE(ragdoll_bone_pair, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ragdoll_bone_pair, ser20::oarchive_binary_t);

LOAD(ragdoll_bone_pair)
{
    try_load(ar, ser20::make_nvp("bone_a", obj.bone_a));
    try_load(ar, ser20::make_nvp("bone_b", obj.bone_b));
}
LOAD_INSTANTIATE(ragdoll_bone_pair, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ragdoll_bone_pair, ser20::iarchive_binary_t);

REFLECT(ragdoll_drive)
{
    entt::meta_factory<ragdoll_drive>{}
        .type("ragdoll_drive"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ragdoll_drive"},
            entt::attribute{"pretty_name", "Ragdoll Drive"},
        })
        .data<&ragdoll_drive::local_frequency>("local_frequency"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_frequency"},
            entt::attribute{"pretty_name", "Local Frequency"},
            entt::attribute{"tooltip",
                            "Joint springs towards the animated rotation against the parent body, oscillations per "
                            "second. 0 turns them off."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&ragdoll_drive::local_damping_ratio>("local_damping_ratio"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_damping_ratio"},
            entt::attribute{"pretty_name", "Local Damping Ratio"},
            entt::attribute{"tooltip", "1 settles on the animation without overshoot."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
        })
        .data<&ragdoll_drive::world_frequency>("world_frequency"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_frequency"},
            entt::attribute{"pretty_name", "World Frequency"},
            entt::attribute{"tooltip",
                            "Pull of every body of a powered ragdoll towards its animated place in the world, "
                            "oscillations per second. Keeps it standing. 0 turns it off."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&ragdoll_drive::world_damping_ratio>("world_damping_ratio"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_damping_ratio"},
            entt::attribute{"pretty_name", "World Damping Ratio"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
        })
        .data<&ragdoll_drive::world_max_force>("world_max_force"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_max_force"},
            entt::attribute{"pretty_name", "World Max Force"},
            entt::attribute{"tooltip", "Newtons the world pull uses at most, per body."},
            entt::attribute{"min", 0.0f},
        })
        .data<&ragdoll_drive::world_max_torque>("world_max_torque"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_max_torque"},
            entt::attribute{"pretty_name", "World Max Torque"},
            entt::attribute{"tooltip", "Newton meters the world pull uses at most, per body."},
            entt::attribute{"min", 0.0f},
        });
}

SAVE(ragdoll_drive)
{
    try_save(ar, ser20::make_nvp("local_frequency", obj.local_frequency));
    try_save(ar, ser20::make_nvp("local_damping_ratio", obj.local_damping_ratio));
    try_save(ar, ser20::make_nvp("world_frequency", obj.world_frequency));
    try_save(ar, ser20::make_nvp("world_damping_ratio", obj.world_damping_ratio));
    try_save(ar, ser20::make_nvp("world_max_force", obj.world_max_force));
    try_save(ar, ser20::make_nvp("world_max_torque", obj.world_max_torque));
}
SAVE_INSTANTIATE(ragdoll_drive, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ragdoll_drive, ser20::oarchive_binary_t);

LOAD(ragdoll_drive)
{
    try_load(ar, ser20::make_nvp("local_frequency", obj.local_frequency));
    try_load(ar, ser20::make_nvp("local_damping_ratio", obj.local_damping_ratio));
    try_load(ar, ser20::make_nvp("world_frequency", obj.world_frequency));
    try_load(ar, ser20::make_nvp("world_damping_ratio", obj.world_damping_ratio));
    try_load(ar, ser20::make_nvp("world_max_force", obj.world_max_force));
    try_load(ar, ser20::make_nvp("world_max_torque", obj.world_max_torque));
}
LOAD_INSTANTIATE(ragdoll_drive, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ragdoll_drive, ser20::iarchive_binary_t);

REFLECT(ragdoll_component)
{
    entt::meta_factory<ragdoll_component>{}
        .type("ragdoll_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ragdoll_component"},
            entt::attribute{"category", "Physics"},
            entt::attribute{"pretty_name", "Ragdoll"},
            entt::attribute{"tooltip",
                            "Physics bodies on the bones of a skinned model, held by joints. Animated: the bodies "
                            "follow the animation as hitboxes. Simulated: physics moves the bones. Powered: physics "
                            "moves them while drives pull them to the animation. Use Auto Setup on a humanoid."},
        })
        .func<&component_meta<ragdoll_component>::exists>("component_exists"_hs)
        .func<&component_meta<ragdoll_component>::add>("component_add"_hs)
        .func<&component_meta<ragdoll_component>::remove>("component_remove"_hs)
        .func<&component_meta<ragdoll_component>::save>("component_save"_hs)
        .func<&component_meta<ragdoll_component>::load>("component_load"_hs)
        .data<&ragdoll_component::set_start_state, &ragdoll_component::get_start_state>("start_state"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "start_state"},
            entt::attribute{"pretty_name", "Start State"},
            entt::attribute{"tooltip", "The state play starts in."},
        })
        .data<nullptr, &ragdoll_component::get_state>("state"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "state"},
            entt::attribute{"pretty_name", "State"},
            entt::attribute{"tooltip", "Who moves the bones now."},
        })
        .data<&ragdoll_component::set_joint_friction, &ragdoll_component::get_joint_friction>("joint_friction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "joint_friction"},
            entt::attribute{"pretty_name", "Joint Friction"},
            entt::attribute{"tooltip", "Newton meters a joint resists turning with: low values flop, high values look stiff."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&ragdoll_component::set_linear_damping, &ragdoll_component::get_linear_damping>("linear_damping"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "linear_damping"},
            entt::attribute{"pretty_name", "Linear Damping"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.01f},
        })
        .data<&ragdoll_component::set_angular_damping, &ragdoll_component::get_angular_damping>("angular_damping"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "angular_damping"},
            entt::attribute{"pretty_name", "Angular Damping"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.01f},
        })
        .data<&ragdoll_component::set_self_collision, &ragdoll_component::get_self_collision>("self_collision"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "self_collision"},
            entt::attribute{"pretty_name", "Self Collision"},
            entt::attribute{"tooltip",
                            "Bodies that are not parent and child, and not in the disabled list, collide with each "
                            "other, so arms do not pass through the chest."},
        })
        .data<&ragdoll_component::set_is_continuous, &ragdoll_component::is_continuous>("is_continuous"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "is_continuous"},
            entt::attribute{"pretty_name", "Continuous"},
            entt::attribute{"tooltip",
                            "Continuous collision against other moving bodies too. Static geometry is always swept."},
        })
        .data<&ragdoll_component::set_include_layers, &ragdoll_component::get_include_layers>("include_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "include_layers"},
            entt::attribute{"pretty_name", "Include Layers"},
        })
        .data<&ragdoll_component::set_exclude_layers, &ragdoll_component::get_exclude_layers>("exclude_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "exclude_layers"},
            entt::attribute{"pretty_name", "Exclude Layers"},
        })
        .data<&ragdoll_component::set_drive, &ragdoll_component::get_drive>("drive"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "drive"},
            entt::attribute{"pretty_name", "Drive"},
            entt::attribute{"tooltip", "How hard powered bodies follow the animation."},
        })
        .data<&ragdoll_component::set_blend_time, &ragdoll_component::get_blend_time>("blend_time"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_time"},
            entt::attribute{"pretty_name", "Blend Time"},
            entt::attribute{"tooltip", "Seconds the bones take to blend back to the animation."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
        })
        .data<&ragdoll_component::set_bodies, &ragdoll_component::get_bodies>("bodies"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bodies"},
            entt::attribute{"pretty_name", "Bodies"},
        })
        .data<&ragdoll_component::set_disabled_collisions, &ragdoll_component::get_disabled_collisions>(
            "disabled_collisions"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "disabled_collisions"},
            entt::attribute{"pretty_name", "Disabled Collisions"},
            entt::attribute{"tooltip", "Body pairs that never collide, for bones that overlap in the rest pose."},
        });
}

SAVE(ragdoll_component)
{
    try_save(ar, ser20::make_nvp("start_state", obj.get_start_state()));
    try_save(ar, ser20::make_nvp("joint_friction", obj.get_joint_friction()));
    try_save(ar, ser20::make_nvp("linear_damping", obj.get_linear_damping()));
    try_save(ar, ser20::make_nvp("angular_damping", obj.get_angular_damping()));
    try_save(ar, ser20::make_nvp("self_collision", obj.get_self_collision()));
    try_save(ar, ser20::make_nvp("is_continuous", obj.is_continuous()));
    try_save(ar, ser20::make_nvp("include_layers", obj.get_include_layers()));
    try_save(ar, ser20::make_nvp("exclude_layers", obj.get_exclude_layers()));
    try_save(ar, ser20::make_nvp("drive", obj.get_drive()));
    try_save(ar, ser20::make_nvp("blend_time", obj.get_blend_time()));
    try_save(ar, ser20::make_nvp("bodies", obj.get_bodies()));
    try_save(ar, ser20::make_nvp("disabled_collisions", obj.get_disabled_collisions()));
}
SAVE_INSTANTIATE(ragdoll_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ragdoll_component, ser20::oarchive_binary_t);

LOAD(ragdoll_component)
{
    ragdoll_state start_state{ragdoll_state::animated};
    if(try_load(ar, ser20::make_nvp("start_state", start_state)))
    {
        obj.set_start_state(start_state);
    }
    float joint_friction{0.0f};
    if(try_load(ar, ser20::make_nvp("joint_friction", joint_friction)))
    {
        obj.set_joint_friction(joint_friction);
    }
    float linear_damping{0.0f};
    if(try_load(ar, ser20::make_nvp("linear_damping", linear_damping)))
    {
        obj.set_linear_damping(linear_damping);
    }
    float angular_damping{0.0f};
    if(try_load(ar, ser20::make_nvp("angular_damping", angular_damping)))
    {
        obj.set_angular_damping(angular_damping);
    }
    bool self_collision{true};
    if(try_load(ar, ser20::make_nvp("self_collision", self_collision)))
    {
        obj.set_self_collision(self_collision);
    }
    bool is_continuous{false};
    if(try_load(ar, ser20::make_nvp("is_continuous", is_continuous)))
    {
        obj.set_is_continuous(is_continuous);
    }
    layer_mask include_layers;
    if(try_load(ar, ser20::make_nvp("include_layers", include_layers)))
    {
        obj.set_include_layers(include_layers);
    }
    layer_mask exclude_layers;
    if(try_load(ar, ser20::make_nvp("exclude_layers", exclude_layers)))
    {
        obj.set_exclude_layers(exclude_layers);
    }
    ragdoll_drive drive;
    if(try_load(ar, ser20::make_nvp("drive", drive)))
    {
        obj.set_drive(drive);
    }
    float blend_time{0.3f};
    if(try_load(ar, ser20::make_nvp("blend_time", blend_time)))
    {
        obj.set_blend_time(blend_time);
    }
    std::vector<ragdoll_body> bodies;
    if(try_load(ar, ser20::make_nvp("bodies", bodies)))
    {
        obj.set_bodies(bodies);
    }
    std::vector<ragdoll_bone_pair> disabled_collisions;
    if(try_load(ar, ser20::make_nvp("disabled_collisions", disabled_collisions)))
    {
        obj.set_disabled_collisions(disabled_collisions);
    }
}
LOAD_INSTANTIATE(ragdoll_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ragdoll_component, ser20::iarchive_binary_t);

} // namespace unravel
