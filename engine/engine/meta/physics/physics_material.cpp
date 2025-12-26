#include "physics_material.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(physics_material)
{
    
    entt::meta_factory<combine_mode>{}
        .type("combine_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "combine_mode"},
            entt::attribute{"pretty_name", "Combine Mode"},
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
    
    // Changes here should be reflected in ex::get_format_version<physics_material>() in asset_extensions.h
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
    
    // Changes here should be reflected in ex::get_format_version<physics_material>() in asset_extensions.h
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
