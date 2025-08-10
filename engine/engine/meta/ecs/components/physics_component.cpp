#include "physics_component.hpp"
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/layers/layer_mask.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/variant.hpp>

namespace unravel
{

REFLECT(physics_box_shape)
{
    rttr::registration::class_<physics_box_shape>("physics_box_shape")(rttr::metadata("pretty_name", "Box"))
        .constructor<>()()
        .property("center", &physics_box_shape::center)(rttr::metadata("pretty_name", "Center"),
                                                        rttr::metadata("tooltip", "The center of the collider."))
        .property("extends", &physics_box_shape::extends)(rttr::metadata("pretty_name", "Extends"),
                                                          rttr::metadata("tooltip", "The extends of the collider."));
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
    rttr::registration::class_<physics_sphere_shape>("physics_sphere_shape")(rttr::metadata("pretty_name", "Sphere"))
        .constructor<>()()
        .property("center", &physics_sphere_shape::center)(rttr::metadata("pretty_name", "Center"),
                                                           rttr::metadata("tooltip", "The center of the collider."))
        .property("radius", &physics_sphere_shape::radius)(rttr::metadata("pretty_name", "Radius"),
                                                           rttr::metadata("tooltip", "The radius of the collider."),
                                                           rttr::metadata("min", 0.0f),
                                                           rttr::metadata("step", 0.1f));
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
    rttr::registration::class_<physics_capsule_shape>("physics_capsule_shape")(rttr::metadata("pretty_name", "Capsule"))
        .constructor<>()()
        .property("center", &physics_capsule_shape::center)(rttr::metadata("pretty_name", "Center"),
                                                            rttr::metadata("tooltip", "The center of the collider."))
        .property("radius", &physics_capsule_shape::radius)(rttr::metadata("pretty_name", "Radius"),
                                                            rttr::metadata("tooltip", "The radius of the collider."),
                                                            rttr::metadata("min", 0.0f),
                                                            rttr::metadata("step", 0.1f))
        .property("length", &physics_capsule_shape::length)(rttr::metadata("pretty_name", "Length"),
                                                            rttr::metadata("tooltip", "The length of the collider."),
                                                            rttr::metadata("min", 0.0f),
                                                            rttr::metadata("step", 0.1f));
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
    rttr::registration::class_<physics_cylinder_shape>(
        "physics_cylinder_shape")(rttr::metadata("category", "PHYSICS"), rttr::metadata("pretty_name", "Cylinder"))
        .constructor<>()()
        .property("center", &physics_cylinder_shape::center)(rttr::metadata("pretty_name", "Center"),
                                                             rttr::metadata("tooltip", "The center of the collider."))
        .property("radius", &physics_cylinder_shape::radius)(rttr::metadata("pretty_name", "Radius"),
                                                             rttr::metadata("tooltip", "The radius of the collider."),
                                                             rttr::metadata("min", 0.0f),
                                                             rttr::metadata("step", 0.1f))
        .property("length", &physics_cylinder_shape::length)(rttr::metadata("pretty_name", "Length"),
                                                             rttr::metadata("tooltip", "The length of the collider."),
                                                             rttr::metadata("min", 0.0f),
                                                             rttr::metadata("step", 0.1f));
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

REFLECT(physics_compound_shape)
{
    static const auto& ps = rttr::type::get<physics_box_shape>();
    static const auto& ss = rttr::type::get<physics_sphere_shape>();
    static const auto& cs = rttr::type::get<physics_capsule_shape>();
    static const auto& cys = rttr::type::get<physics_cylinder_shape>();

    std::vector<const rttr::type*> variant_types{&ps, &ss, &cs, &cys};

    rttr::registration::class_<physics_compound_shape>("physics_compound_shape")(
        rttr::metadata("category", "PHYSICS"),
        rttr::metadata("pretty_name", "Shape"),
        rttr::metadata("variant_types", variant_types))
        .constructor<>()();
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
    static const auto& ps = rttr::type::get<physics_box_shape>();
    static const auto& ss = rttr::type::get<physics_sphere_shape>();

    std::vector<const rttr::type*> variant_types{&ps, &ss};

    rttr::registration::class_<physics_component>("physics_component")(rttr::metadata("category", "PHYSICS"),
                                                                       rttr::metadata("pretty_name", "Physics"))
        .constructor<>()()
        .method("component_exists", &component_exists<physics_component>)

        .property("is_using_gravity", &physics_component::is_using_gravity, &physics_component::set_is_using_gravity)(
            rttr::metadata("pretty_name", "Use Gravity"),
            rttr::metadata("tooltip", "Simulate gravity for this rigidbody."))
        .property("is_kinematic", &physics_component::is_kinematic, &physics_component::set_is_kinematic)(
            rttr::metadata("pretty_name", "Is Kinematic"),
            rttr::metadata(
                "tooltip",
                "Is the rigidbody kinematic(A rigid body that is not affected by others and can be moved directly.)"))
        .property("is_sensor", &physics_component::is_sensor, &physics_component::set_is_sensor)(
            rttr::metadata("pretty_name", "Is Sensor"),
            rttr::metadata("tooltip", "The rigidbody will not respond to collisions, i.e. it becomes a _sensor_."))
        .property("is_autoscaled", &physics_component::is_autoscaled, &physics_component::set_is_autoscaled)(
            rttr::metadata("pretty_name", "Is Auto Scaled"),
            rttr::metadata("tooltip", "Enables/Disables shape auto scale with transform."))
        .property("mass", &physics_component::get_mass, &physics_component::set_mass)(
            rttr::metadata("min", 0.0f),
            rttr::metadata("pretty_name", "Mass"),
            rttr::metadata("tooltip", "Mass for dynamic rigidbodies."))

        .property("include_layers",
                  &physics_component::get_collision_include_mask,
                  &physics_component::set_collision_include_mask)(
            rttr::metadata("pretty_name", "Include Layers"),
            rttr::metadata("tooltip", "Layers to include when producing collisions."))

        .property("exclude_layers",
                  &physics_component::get_collision_exclude_mask,
                  &physics_component::set_collision_exclude_mask)(
            rttr::metadata("pretty_name", "Exclude Layers"),
            rttr::metadata("tooltip", "Layers to exclude when producing collisions."))

        .property_readonly("collision_layers", &physics_component::get_collision_mask)(
            rttr::metadata("pretty_name", "Collision Layers"),
            rttr::metadata("tooltip", "Layers (Include - Exclude) used when producing collisions."))

        .property_readonly("velocity", &physics_component::get_velocity)(rttr::metadata("pretty_name", "Velocity"))

        .property_readonly("angular_velocity",
                           &physics_component::get_angular_velocity)(rttr::metadata("pretty_name", "Angular Velocity"))

        .property("mass", &physics_component::get_mass, &physics_component::set_mass)(
            rttr::metadata("min", 0.0f),
            rttr::metadata("pretty_name", "Mass"),
            rttr::metadata("tooltip", "Mass for dynamic rigidbodies."))
        .property("freeze_position", &physics_component::get_freeze_position, &physics_component::set_freeze_position)(
            rttr::metadata("pretty_name", "Freeze Position"),
            rttr::metadata("tooltip", "Freeze."))
        .property("freeze_rotation", &physics_component::get_freeze_rotation, &physics_component::set_freeze_rotation)(
            rttr::metadata("pretty_name", "Freeze Rotation"),
            rttr::metadata("tooltip", "Freeze."))
        .property("material", &physics_component::get_material, &physics_component::set_material)(
            rttr::metadata("pretty_name", "Material"),
            rttr::metadata("tooltip", "Physics material for the rigidbody."))
        .property("shapes", &physics_component::get_shapes, &physics_component::set_shapes)(
            rttr::metadata("pretty_name", "Shapes"),
            rttr::metadata("tooltip", "Shapes."));
}

SAVE(physics_component)
{
    try_save(ar, ser20::make_nvp("is_using_gravity", obj.is_using_gravity()));
    try_save(ar, ser20::make_nvp("is_kinematic", obj.is_kinematic()));
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

    bool is_kinematic{};
    if(try_load(ar, ser20::make_nvp("is_kinematic", is_kinematic)))
    {
        obj.set_is_kinematic(is_kinematic);
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
