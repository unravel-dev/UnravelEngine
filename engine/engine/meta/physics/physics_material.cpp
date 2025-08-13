#include "physics_material.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(physics_material)
{
    rttr::registration::enumeration<combine_mode>("combine_mode")(rttr::value("Average", combine_mode::average),
                                                                  rttr::value("Minimum", combine_mode::minimum),
                                                                  rttr::value("Multiply", combine_mode::multiply),
                                                                  rttr::value("Maximum", combine_mode::maximum));

    rttr::registration::class_<physics_material>("physics_material")(rttr::metadata("pretty_name", "Physics Material"))
        .constructor<>()()
        .property("restitution", &physics_material::restitution)(
            rttr::metadata("pretty_name", "Restitution (Bounce)"),
            rttr::metadata(
                "tooltip",
                "Restitution represents the bounciness of the material. A value of 0.0 means no bounce (perfectly "
                "inelastic collision), while 1.0 means perfect bounce (perfectly elastic collision)."),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f))
        .property("friction", &physics_material::friction)(
            rttr::metadata("pretty_name", "Friction"),
            rttr::metadata(
                "tooltip",
                "Friction represents the resistance to sliding motion. A value of 0.0 means no friction (perfectly "
                "slippery), while values around 1.0 represent typical real-world friction. Values slightly above 1.0 "
                "can simulate very high friction surfaces but should be used cautiously."),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f))
        .property("stiffness", &physics_material::stiffness)(
            rttr::metadata("pretty_name", "Stiffness"),
            rttr::metadata("tooltip",
                           "Stiffness represents how much force is required to deform the material. A high value means "
                           "the material is very stiff (resists deformation)."),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f))
        .property("damping", &physics_material::damping)(
            rttr::metadata("pretty_name", "Damping"),
            rttr::metadata("tooltip",
                           "Damping represents energy loss in motion (e.g., through internal friction). A value of 0.0 "
                           "means no damping (energy is conserved), while 1.0 represents very high damping (rapid "
                           "energy loss). Typical values range from 0.01 to 0.3 for realistic simulations."),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f))
        .property("restitution_combine", &physics_material::restitution_combine)(
            rttr::metadata("pretty_name", "Restitution Combine"),
            rttr::metadata("tooltip",
                           "How to combine the restitution(bounce) values of both colliders in a collision pair to"
                           "calculate the total restitution(bounce) between them."))
        .property("friction_combine", &physics_material::friction_combine)(
            rttr::metadata("pretty_name", "Friction Combine"),
            rttr::metadata("tooltip",
                           "How to combine the friction values of both colliders in a collision pair to"
                           "calculate the total friction between them."));

    // Register combine_mode enum with entt
    entt::meta_factory<combine_mode>{}
        .type("combine_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "combine_mode"},
        })
        .data<combine_mode::average>("average"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "average"},
            entt::attribute{"pretty_name", "Average"},
        })
        .data<combine_mode::minimum>("minimum"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "minimum"},
            entt::attribute{"pretty_name", "Minimum"},
        })
        .data<combine_mode::multiply>("multiply"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "multiply"},
            entt::attribute{"pretty_name", "Multiply"},
        })
        .data<combine_mode::maximum>("maximum"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "maximum"},
            entt::attribute{"pretty_name", "Maximum"},
        });

    // Register physics_material with entt
    entt::meta_factory<physics_material>{}
        .type("physics_material"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_material"},
            entt::attribute{"pretty_name", "Physics Material"},
        })
        .data<&physics_material::restitution>("restitution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "restitution"},
            entt::attribute{"pretty_name", "Restitution (Bounce)"},
            entt::attribute{"tooltip", "Restitution represents the bounciness of the material. A value of 0.0 means no bounce (perfectly inelastic collision), while 1.0 means perfect bounce (perfectly elastic collision)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&physics_material::friction>("friction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "friction"},
            entt::attribute{"pretty_name", "Friction"},
            entt::attribute{"tooltip", "Friction represents the resistance to sliding motion. A value of 0.0 means no friction (perfectly slippery), while values around 1.0 represent typical real-world friction. Values slightly above 1.0 can simulate very high friction surfaces but should be used cautiously."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&physics_material::stiffness>("stiffness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stiffness"},
            entt::attribute{"pretty_name", "Stiffness"},
            entt::attribute{"tooltip", "Stiffness represents how much force is required to deform the material. A high value means the material is very stiff (resists deformation)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&physics_material::damping>("damping"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "damping"},
            entt::attribute{"pretty_name", "Damping"},
            entt::attribute{"tooltip", "Damping represents energy loss in motion (e.g., through internal friction). A value of 0.0 means no damping (energy is conserved), while 1.0 represents very high damping (rapid energy loss). Typical values range from 0.01 to 0.3 for realistic simulations."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&physics_material::restitution_combine>("restitution_combine"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "restitution_combine"},
            entt::attribute{"pretty_name", "Restitution Combine"},
            entt::attribute{"tooltip", "How to combine the restitution(bounce) values of both colliders in a collision pair tocalculate the total restitution(bounce) between them."},
        })
        .data<&physics_material::friction_combine>("friction_combine"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "friction_combine"},
            entt::attribute{"pretty_name", "Friction Combine"},
            entt::attribute{"tooltip", "How to combine the friction values of both colliders in a collision pair tocalculate the total friction between them."},
        });
}

SAVE(physics_material)
{
    try_save(ar, ser20::make_nvp("restitution", obj.restitution));
    try_save(ar, ser20::make_nvp("friction", obj.friction));
    try_save(ar, ser20::make_nvp("stiffness", obj.stiffness));
    try_save(ar, ser20::make_nvp("damping", obj.damping));
    try_save(ar, ser20::make_nvp("restitution_combine", obj.restitution_combine));
    try_save(ar, ser20::make_nvp("friction_combine", obj.friction_combine));
}
SAVE_INSTANTIATE(physics_material, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(physics_material, ser20::oarchive_binary_t);

LOAD(physics_material)
{
    try_load(ar, ser20::make_nvp("restitution", obj.restitution));
    try_load(ar, ser20::make_nvp("friction", obj.friction));
    try_load(ar, ser20::make_nvp("stiffness", obj.stiffness));
    try_load(ar, ser20::make_nvp("damping", obj.damping));
    try_load(ar, ser20::make_nvp("restitution_combine", obj.restitution_combine));
    try_load(ar, ser20::make_nvp("friction_combine", obj.friction_combine));
}
LOAD_INSTANTIATE(physics_material, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(physics_material, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const physics_material::sptr& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("physics_material", *obj));
    }
}

void save_to_file_bin(const std::string& absolute_path, const physics_material::sptr& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("physics_material", *obj));
    }
}

void load_from_file(const std::string& absolute_path, physics_material::sptr& obj)
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_iarchive_associative(stream);
        try_load(ar, ser20::make_nvp("physics_material", *obj));
    }
}

void load_from_file_bin(const std::string& absolute_path, physics_material::sptr& obj)
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::iarchive_binary_t ar(stream);
        try_load(ar, ser20::make_nvp("physics_material", *obj));
    }
}
} // namespace unravel
