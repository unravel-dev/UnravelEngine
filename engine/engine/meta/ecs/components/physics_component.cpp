#include "physics_component.hpp"
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/layers/layer_mask.hpp>
#include <engine/meta/rendering/mesh.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/variant.hpp>

namespace unravel
{

REFLECT(physics_box_shape)
{
    entt::meta_factory<physics_box_shape>{}
        .type("physics_box_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_box_shape"},
            entt::attribute{"pretty_name", "Box"},
        })
        .data<&physics_box_shape::center>("center"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "center"},
            entt::attribute{"pretty_name", "Center"},
            entt::attribute{"tooltip", "The center of the collider."},
        })
        .data<&physics_box_shape::extends>("extends"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "extends"},
            entt::attribute{"pretty_name", "Extends"},
            entt::attribute{"tooltip", "The extends of the collider."},
        });
}

SAVE(physics_box_shape)
{
    try_save(ar, ser20::make_nvp("center", obj.center));
    try_save(ar, ser20::make_nvp("extends", obj.extends));
}
SAVE_INSTANTIATE(physics_box_shape, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(physics_box_shape, ser20::oarchive_binary_t);

LOAD(physics_box_shape)
{
    try_load(ar, ser20::make_nvp("center", obj.center));
    try_load(ar, ser20::make_nvp("extends", obj.extends));
}

LOAD_INSTANTIATE(physics_box_shape, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(physics_box_shape, ser20::iarchive_binary_t);

REFLECT(physics_sphere_shape)
{
    entt::meta_factory<physics_sphere_shape>{}
        .type("physics_sphere_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_sphere_shape"},
            entt::attribute{"pretty_name", "Sphere"},
        })
        .data<&physics_sphere_shape::center>("center"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "center"},
            entt::attribute{"pretty_name", "Center"},
            entt::attribute{"tooltip", "The center of the collider."},
        })
        .data<&physics_sphere_shape::radius>("radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radius"},
            entt::attribute{"pretty_name", "Radius"},
            entt::attribute{"tooltip", "The radius of the collider."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        });
}

SAVE(physics_sphere_shape)
{
    try_save(ar, ser20::make_nvp("center", obj.center));
    try_save(ar, ser20::make_nvp("radius", obj.radius));
}
SAVE_INSTANTIATE(physics_sphere_shape, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(physics_sphere_shape, ser20::oarchive_binary_t);

LOAD(physics_sphere_shape)
{
    try_load(ar, ser20::make_nvp("center", obj.center));
    try_load(ar, ser20::make_nvp("radius", obj.radius));
}

LOAD_INSTANTIATE(physics_sphere_shape, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(physics_sphere_shape, ser20::iarchive_binary_t);

REFLECT(physics_capsule_shape)
{
    entt::meta_factory<physics_capsule_shape>{}
        .type("physics_capsule_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_capsule_shape"},
            entt::attribute{"pretty_name", "Capsule"},
        })
        .data<&physics_capsule_shape::center>("center"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "center"},
            entt::attribute{"pretty_name", "Center"},
            entt::attribute{"tooltip", "The center of the collider."},
        })
        .data<&physics_capsule_shape::radius>("radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radius"},
            entt::attribute{"pretty_name", "Radius"},
            entt::attribute{"tooltip", "The radius of the collider."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&physics_capsule_shape::length>("length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "length"},
            entt::attribute{"pretty_name", "Length"},
            entt::attribute{"tooltip", "The length of the collider."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        });
}

SAVE(physics_capsule_shape)
{
    try_save(ar, ser20::make_nvp("center", obj.center));
    try_save(ar, ser20::make_nvp("radius", obj.radius));
    try_save(ar, ser20::make_nvp("length", obj.length));
}
SAVE_INSTANTIATE(physics_capsule_shape, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(physics_capsule_shape, ser20::oarchive_binary_t);

LOAD(physics_capsule_shape)
{
    try_load(ar, ser20::make_nvp("center", obj.center));
    try_load(ar, ser20::make_nvp("radius", obj.radius));
    try_load(ar, ser20::make_nvp("length", obj.length));
}

LOAD_INSTANTIATE(physics_capsule_shape, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(physics_capsule_shape, ser20::iarchive_binary_t);

REFLECT(physics_cylinder_shape)
{
    entt::meta_factory<physics_cylinder_shape>{}
        .type("physics_cylinder_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_cylinder_shape"},
            entt::attribute{"pretty_name", "Cylinder"},
        })
        .data<&physics_cylinder_shape::center>("center"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "center"},
            entt::attribute{"pretty_name", "Center"},
            entt::attribute{"tooltip", "The center of the collider."},
        })
        .data<&physics_cylinder_shape::radius>("radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radius"},
            entt::attribute{"pretty_name", "Radius"},
            entt::attribute{"tooltip", "The radius of the collider."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&physics_cylinder_shape::length>("length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "length"},
            entt::attribute{"pretty_name", "Length"},
            entt::attribute{"tooltip", "The length of the collider."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        });
}

SAVE(physics_cylinder_shape)
{
    try_save(ar, ser20::make_nvp("center", obj.center));
    try_save(ar, ser20::make_nvp("radius", obj.radius));
    try_save(ar, ser20::make_nvp("length", obj.length));
}
SAVE_INSTANTIATE(physics_cylinder_shape, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(physics_cylinder_shape, ser20::oarchive_binary_t);

LOAD(physics_cylinder_shape)
{
    try_load(ar, ser20::make_nvp("center", obj.center));
    try_load(ar, ser20::make_nvp("radius", obj.radius));
    try_load(ar, ser20::make_nvp("length", obj.length));
}

LOAD_INSTANTIATE(physics_cylinder_shape, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(physics_cylinder_shape, ser20::iarchive_binary_t);

REFLECT(rigidbody_type)
{
    entt::meta_factory<rigidbody_type>{}
        .type("rigidbody_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rigidbody_type"},
            entt::attribute{"pretty_name", "Body Type"},
        })
        .data<rigidbody_type::static_body>("static_body"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "static_body"},
            entt::attribute{"pretty_name", "Static"},
        })
        .data<rigidbody_type::kinematic>("kinematic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "kinematic"},
            entt::attribute{"pretty_name", "Kinematic"},
        })
        .data<rigidbody_type::dynamic>("dynamic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "dynamic"},
            entt::attribute{"pretty_name", "Dynamic"},
        });
}

REFLECT(mesh_collision_type)
{
    entt::meta_factory<mesh_collision_type>{}
        .type("mesh_collision_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh_collision_type"},
            entt::attribute{"pretty_name", "Mesh Collision Type"},
        })
        .data<mesh_collision_type::convex>("convex"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "convex"},
            entt::attribute{"pretty_name", "Convex"},
        })
        .data<mesh_collision_type::concave>("concave"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "concave"},
            entt::attribute{"pretty_name", "Concave"},
        });
}

REFLECT(physics_mesh_shape)
{
    entt::meta_factory<physics_mesh_shape>{}
        .type("physics_mesh_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_mesh_shape"},
            entt::attribute{"pretty_name", "Mesh"},
        })
        .data<&physics_mesh_shape::center>("center"_hs)
            .custom<entt::attributes>(entt::attributes{
                entt::attribute{"pretty_name", "Center"},
                entt::attribute{"tooltip", "Center offset of the mesh collision shape."},
            })
        .data<&physics_mesh_shape::mesh_asset>("mesh_asset"_hs)
            .custom<entt::attributes>(entt::attributes{
                entt::attribute{"pretty_name", "Mesh Asset"},
                entt::attribute{"tooltip", "The mesh asset to use for collision."},
            })
        .data<&physics_mesh_shape::collision_type>("collision_type"_hs)
            .custom<entt::attributes>(entt::attributes{
                entt::attribute{"pretty_name", "Collision Type"},
                entt::attribute{"tooltip", "Type of collision shape (convex for dynamic, concave for static)."},
            });
}

SAVE(physics_mesh_shape)
{
    try_save(ar, ser20::make_nvp("center", obj.center));
    try_save(ar, ser20::make_nvp("mesh_asset", obj.mesh_asset));
    try_save(ar, ser20::make_nvp("collision_type", obj.collision_type));
}

SAVE_INSTANTIATE(physics_mesh_shape, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(physics_mesh_shape, ser20::oarchive_binary_t);

LOAD(physics_mesh_shape)
{
    try_load(ar, ser20::make_nvp("center", obj.center));
    try_load(ar, ser20::make_nvp("mesh_asset", obj.mesh_asset));
    try_load(ar, ser20::make_nvp("collision_type", obj.collision_type));
}

LOAD_INSTANTIATE(physics_mesh_shape, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(physics_mesh_shape, ser20::iarchive_binary_t);

REFLECT(physics_compound_shape)
{
    static const auto& ps = entt::resolve<physics_box_shape>();
    static const auto& ss = entt::resolve<physics_sphere_shape>();
    static const auto& cs = entt::resolve<physics_capsule_shape>();
    static const auto& cys = entt::resolve<physics_cylinder_shape>();
    static const auto& ms = entt::resolve<physics_mesh_shape>();

    std::vector<entt::meta_type> variant_types{ps, ss, cs, cys, ms};

    // Register physics_compound_shape with entt
    entt::meta_factory<physics_compound_shape>{}
        .type("physics_compound_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_compound_shape"},
            entt::attribute{"pretty_name", "Shape"},
            entt::attribute{"variant_types", variant_types}
        });
    
}

SAVE(physics_compound_shape)
{
    try_save(ar, ser20::make_nvp("shape", obj.shape));
}
SAVE_INSTANTIATE(physics_compound_shape, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(physics_compound_shape, ser20::oarchive_binary_t);

LOAD(physics_compound_shape)
{
    try_load(ar, ser20::make_nvp("shape", obj.shape));
}

LOAD_INSTANTIATE(physics_compound_shape, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(physics_compound_shape, ser20::iarchive_binary_t);

REFLECT(physics_component)
{
    entt::meta_factory<physics_component>{}
        .type("physics_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_component"},
            entt::attribute{"category", "PHYSICS"},
            entt::attribute{"pretty_name", "Physics"},
        })
        .func<&component_meta<physics_component>::exists>("component_exists"_hs)
        .func<&component_meta<physics_component>::add>("component_add"_hs)
        .func<&component_meta<physics_component>::remove>("component_remove"_hs)
        .func<&component_meta<physics_component>::save>("component_save"_hs)
        .func<&component_meta<physics_component>::load>("component_load"_hs)
        .data<&physics_component::set_is_using_gravity, &physics_component::is_using_gravity>("is_using_gravity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "is_using_gravity"},
            entt::attribute{"pretty_name", "Use Gravity"},
            entt::attribute{"tooltip", "Simulate gravity for this rigidbody."},
        })
        .data<&physics_component::set_body_type, &physics_component::get_body_type>("body_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "body_type"},
            entt::attribute{"pretty_name", "Body Type"},
            entt::attribute{"tooltip",
                            "Static: ECS may teleport (AABB update). Kinematic: ECS-driven, pushes dynamics. Dynamic: "
                            "fully simulated."},
        })
        .data<&physics_component::set_is_sensor, &physics_component::is_sensor>("is_sensor"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "is_sensor"},
            entt::attribute{"pretty_name", "Is Sensor"},
            entt::attribute{"tooltip", "The rigidbody will not respond to collisions, i.e. it becomes a _sensor_."},
        })
        .data<&physics_component::set_is_autoscaled, &physics_component::is_autoscaled>("is_autoscaled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "is_autoscaled"},
            entt::attribute{"pretty_name", "Is Auto Scaled"},
            entt::attribute{"tooltip", "Enables/Disables shape auto scale with transform."},
        })
        .data<&physics_component::set_mass, &physics_component::get_mass>("mass"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mass"},
            entt::attribute{"pretty_name", "Mass"},
            entt::attribute{"tooltip", "Mass for dynamic rigidbodies."},
            entt::attribute{"min", 0.0f},
        })
        .data<&physics_component::set_collision_include_mask, &physics_component::get_collision_include_mask>("include_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "include_layers"},
            entt::attribute{"pretty_name", "Include Layers"},
            entt::attribute{"tooltip", "Layers to include when producing collisions."},
        })
        .data<&physics_component::set_collision_exclude_mask, &physics_component::get_collision_exclude_mask>("exclude_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "exclude_layers"},
            entt::attribute{"pretty_name", "Exclude Layers"},
            entt::attribute{"tooltip", "Layers to exclude when producing collisions."},
        })
        .data<nullptr, &physics_component::get_collision_mask>("collision_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_layers"},
            entt::attribute{"pretty_name", "Collision Layers"},
            entt::attribute{"tooltip", "Layers (Include - Exclude) used when producing collisions."},
        })
        .data<nullptr, &physics_component::get_velocity>("velocity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "velocity"},
            entt::attribute{"pretty_name", "Velocity"},
        })
        .data<nullptr, &physics_component::get_angular_velocity>("angular_velocity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "angular_velocity"},
            entt::attribute{"pretty_name", "Angular Velocity"},
        })
        .data<&physics_component::set_freeze_position, &physics_component::get_freeze_position>("freeze_position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "freeze_position"},
            entt::attribute{"pretty_name", "Freeze Position"},
            entt::attribute{"tooltip", "Freeze."},
        })
        .data<&physics_component::set_freeze_rotation, &physics_component::get_freeze_rotation>("freeze_rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "freeze_rotation"},
            entt::attribute{"pretty_name", "Freeze Rotation"},
            entt::attribute{"tooltip", "Freeze."},
        })
        .data<&physics_component::set_material, &physics_component::get_material>("material"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material"},
            entt::attribute{"pretty_name", "Material"},
            entt::attribute{"tooltip", "Physics material for the rigidbody."},
        })
        .data<&physics_component::set_shapes, &physics_component::get_shapes>("shapes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shapes"},
            entt::attribute{"pretty_name", "Shapes"},
            entt::attribute{"tooltip", "Shapes."},
        });
}

SAVE(physics_component)
{
    try_save(ar, ser20::make_nvp("is_using_gravity", obj.is_using_gravity()));
    try_save(ar, ser20::make_nvp("body_type", obj.get_body_type()));
    try_save(ar, ser20::make_nvp("is_sensor", obj.is_sensor()));
    try_save(ar, ser20::make_nvp("is_autoscaled", obj.is_autoscaled()));
    try_save(ar, ser20::make_nvp("mass", obj.get_mass()));
    try_save(ar, ser20::make_nvp("include_layers", obj.get_collision_include_mask()));
    try_save(ar, ser20::make_nvp("exclude_layers", obj.get_collision_exclude_mask()));
    try_save(ar, ser20::make_nvp("freeze_position", obj.get_freeze_position()));
    try_save(ar, ser20::make_nvp("freeze_rotation", obj.get_freeze_rotation()));

    try_save(ar, ser20::make_nvp("material", obj.get_material()));
    try_save(ar, ser20::make_nvp("shapes", obj.get_shapes()));
}
SAVE_INSTANTIATE(physics_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(physics_component, ser20::oarchive_binary_t);

LOAD(physics_component)
{
    bool is_using_gravity{};
    if(try_load(ar, ser20::make_nvp("is_using_gravity", is_using_gravity)))
    {
        obj.set_is_using_gravity(is_using_gravity);
    }

    rigidbody_type body_type{rigidbody_type::static_body};
    if(try_load(ar, ser20::make_nvp("body_type", body_type)))
    {
        obj.set_body_type(body_type);
    }
    else
    {
        // Legacy: bool is_kinematic -> kinematic/dynamic.
        bool is_kinematic{};
        if(try_load(ar, ser20::make_nvp("is_kinematic", is_kinematic)))
        {
            obj.set_body_type(is_kinematic ? rigidbody_type::kinematic : rigidbody_type::dynamic);
        }
    }

    bool is_sensor{};
    if(try_load(ar, ser20::make_nvp("is_sensor", is_sensor)))
    {
        obj.set_is_sensor(is_sensor);
    }

    bool is_autoscaled{true};
    if(try_load(ar, ser20::make_nvp("is_autoscaled", is_autoscaled)))
    {
        obj.set_is_autoscaled(is_autoscaled);
    }

    float mass{1};
    if(try_load(ar, ser20::make_nvp("mass", mass)))
    {
        obj.set_mass(mass);
    }

    layer_mask include_layers;
    if(try_load(ar, ser20::make_nvp("include_layers", include_layers)))
    {
        obj.set_collision_include_mask(include_layers);
    }
    layer_mask exclude_layers;
    if(try_load(ar, ser20::make_nvp("exclude_layers", exclude_layers)))
    {
        obj.set_collision_exclude_mask(exclude_layers);
    }

    math::bvec3 freeze_position{};
    if(try_load(ar, ser20::make_nvp("freeze_position", freeze_position)))
    {
        obj.set_freeze_position(freeze_position);
    }

    math::bvec3 freeze_rotation{};
    if(try_load(ar, ser20::make_nvp("freeze_rotation", freeze_rotation)))
    {
        obj.set_freeze_rotation(freeze_rotation);
    }

    asset_handle<physics_material> material;
    if(try_load(ar, ser20::make_nvp("material", material)))
    {
        obj.set_material(material);
    }

    std::vector<physics_compound_shape> shapes;
    if(try_load(ar, ser20::make_nvp("shapes", shapes)))
    {
        obj.set_shapes(shapes);
    }
}

LOAD_INSTANTIATE(physics_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(physics_component, ser20::iarchive_binary_t);

} // namespace unravel
