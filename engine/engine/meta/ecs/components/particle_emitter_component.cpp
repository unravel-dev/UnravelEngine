#include "particle_emitter_component.hpp"
#include <engine/meta/ecs/components/component_disables.h>
#include "serialization/serialization.h"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/array.hpp>
#include <serialization/types/chrono.hpp>
#include <serialization/types/vector.hpp>
#include <engine/meta/core/math/bbox.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/core/common/basetypes.hpp>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/ecs/entity_reference.hpp>
#include <engine/meta/layers/layer_mask.hpp>
#include <engine/meta/rendering/material_instance.hpp>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph_compiler.h>
#include <engine/rendering/mesh.h>

namespace unravel
{

REFLECT(ps_soa::emitter_burst)
{
    entt::meta_factory<ps_soa::emitter_burst>{}
        .type("emitter_burst"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emitter_burst"},
            entt::attribute{"pretty_name", "Burst"},
        })
        .data<&ps_soa::emitter_burst::time>("time"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "time"},
            entt::attribute{"pretty_name", "Time"},
            entt::attribute{"tooltip", "Seconds into the emission cycle of the first firing."},
            entt::attribute{"min", 0.0f},
        })
        .data<&ps_soa::emitter_burst::count>("count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "count"},
            entt::attribute{"pretty_name", "Count"},
            entt::attribute{"tooltip", "Particles per firing, a random whole number between min and max."},
            entt::attribute{"min", 0.0f},
        })
        .data<&ps_soa::emitter_burst::cycles>("cycles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cycles"},
            entt::attribute{"pretty_name", "Cycles"},
            entt::attribute{"tooltip", "Firings per emission cycle: 1 = once, 0 = every Interval until the cycle ends."},
        })
        .data<&ps_soa::emitter_burst::interval>("interval"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "interval"},
            entt::attribute{"pretty_name", "Interval"},
            entt::attribute{"tooltip", "Seconds between the firings of this burst (at least 0.01)."},
            entt::attribute{"min", 0.01f},
        })
        .data<&ps_soa::emitter_burst::probability>("probability"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probability"},
            entt::attribute{"pretty_name", "Probability"},
            entt::attribute{"tooltip", "Chance that each firing happens (0..1)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        });
}

namespace ps_soa
{
SAVE(emitter_burst)
{
    try_save(ar, ser20::make_nvp("time", obj.time));
    try_save(ar, ser20::make_nvp("count", obj.count));
    try_save(ar, ser20::make_nvp("cycles", obj.cycles));
    try_save(ar, ser20::make_nvp("interval", obj.interval));
    try_save(ar, ser20::make_nvp("probability", obj.probability));
}
SAVE_INSTANTIATE(emitter_burst, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(emitter_burst, ser20::oarchive_binary_t);

LOAD(emitter_burst)
{
    try_load(ar, ser20::make_nvp("time", obj.time));
    try_load(ar, ser20::make_nvp("count", obj.count));
    try_load(ar, ser20::make_nvp("cycles", obj.cycles));
    try_load(ar, ser20::make_nvp("interval", obj.interval));
    try_load(ar, ser20::make_nvp("probability", obj.probability));
}
LOAD_INSTANTIATE(emitter_burst, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(emitter_burst, ser20::iarchive_binary_t);
} // namespace ps_soa

REFLECT(particle_sub_emitter)
{
    entt::meta_factory<ps_soa::particle_event_type>{}
        .type("particle_event_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_event_type"},
            entt::attribute{"pretty_name", "Particle Event"},
        })
        .data<ps_soa::particle_event_type::birth>("birth"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "birth"},
            entt::attribute{"pretty_name", "Birth"},
        })
        .data<ps_soa::particle_event_type::death>("death"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "death"},
            entt::attribute{"pretty_name", "Death"},
        })
        .data<ps_soa::particle_event_type::collision>("collision"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision"},
            entt::attribute{"pretty_name", "Collision"},
        });

    entt::meta_factory<particle_sub_emitter>{}
        .type("particle_sub_emitter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_sub_emitter"},
            entt::attribute{"pretty_name", "Sub Emitter"},
        })
        .data<&particle_sub_emitter::trigger>("trigger"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "trigger"},
            entt::attribute{"pretty_name", "Trigger"},
            entt::attribute{"tooltip", "What fires the burst: a particle's birth, its death, or its contact with a collision plane."},
        })
        .data<&particle_sub_emitter::target>("target"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "target"},
            entt::attribute{"pretty_name", "Target"},
            entt::attribute{"tooltip", "The entity whose Particle Emitter bursts (usually a one-shot emitter with no rate)."},
        })
        .data<&particle_sub_emitter::count>("count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "count"},
            entt::attribute{"pretty_name", "Count"},
            entt::attribute{"tooltip", "Particles per event, a random whole number between min and max."},
            entt::attribute{"min", 0.0f},
        })
        .data<&particle_sub_emitter::probability>("probability"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probability"},
            entt::attribute{"pretty_name", "Probability"},
            entt::attribute{"tooltip", "Chance that an event fires the burst (0..1)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&particle_sub_emitter::inherit_velocity>("inherit_velocity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "inherit_velocity"},
            entt::attribute{"pretty_name", "Inherit Velocity"},
            entt::attribute{"tooltip", "Share of the particle's velocity the burst's particles add to their own launch."},
        })
        .data<&particle_sub_emitter::inherit_color>("inherit_color"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "inherit_color"},
            entt::attribute{"pretty_name", "Inherit Color"},
            entt::attribute{"tooltip", "The burst's particles take the particle's color."},
        })
        .data<&particle_sub_emitter::inherit_size>("inherit_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "inherit_size"},
            entt::attribute{"pretty_name", "Inherit Size"},
            entt::attribute{"tooltip", "The burst's particles start at the particle's size."},
        });
}

SAVE(particle_sub_emitter)
{
    try_save(ar, ser20::make_nvp("trigger", obj.trigger));
    try_save(ar, ser20::make_nvp("target", entt::const_handle(obj.target)));
    try_save(ar, ser20::make_nvp("count", obj.count));
    try_save(ar, ser20::make_nvp("probability", obj.probability));
    try_save(ar, ser20::make_nvp("inherit_velocity", obj.inherit_velocity));
    try_save(ar, ser20::make_nvp("inherit_color", obj.inherit_color));
    try_save(ar, ser20::make_nvp("inherit_size", obj.inherit_size));
}
SAVE_INSTANTIATE(particle_sub_emitter, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(particle_sub_emitter, ser20::oarchive_binary_t);

LOAD(particle_sub_emitter)
{
    try_load(ar, ser20::make_nvp("trigger", obj.trigger));
    try_load(ar, ser20::make_nvp("target", obj.target));
    try_load(ar, ser20::make_nvp("count", obj.count));
    try_load(ar, ser20::make_nvp("probability", obj.probability));
    try_load(ar, ser20::make_nvp("inherit_velocity", obj.inherit_velocity));
    try_load(ar, ser20::make_nvp("inherit_color", obj.inherit_color));
    try_load(ar, ser20::make_nvp("inherit_size", obj.inherit_size));
}
LOAD_INSTANTIATE(particle_sub_emitter, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(particle_sub_emitter, ser20::iarchive_binary_t);

REFLECT(particle_collision_decal)
{
    entt::meta_factory<particle_collision_decal>{}
        .type("particle_collision_decal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_collision_decal"},
            entt::attribute{"pretty_name", "Collision Decal"},
        })
        .data<&particle_collision_decal::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Each contact leaves a decal on the surface while the game plays: scorch marks, splats, bullet holes."},
        })
        .data<&particle_collision_decal::material>("material"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material"},
            entt::attribute{"pretty_name", "Material"},
            entt::attribute{"tooltip", "The Decal material the contacts leave. Empty uses the engine's Default Decal."},
        })
        .data<&particle_collision_decal::size>("size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size"},
            entt::attribute{"pretty_name", "Size (m)"},
            entt::attribute{"tooltip", "Side of the square decal in metres, random between min and max."},
            entt::attribute{"min", 0.001f},
        })
        .data<&particle_collision_decal::lifetime>("lifetime"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime"},
            entt::attribute{"pretty_name", "Lifetime (s)"},
            entt::attribute{"tooltip", "Seconds a decal stays before it fades out. 0 keeps it until newer decals take its place."},
            entt::attribute{"min", 0.0f},
        })
        .data<&particle_collision_decal::fade_out>("fade_out"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fade_out"},
            entt::attribute{"pretty_name", "Fade Out (s)"},
            entt::attribute{"tooltip", "Seconds a decal takes to fade out."},
            entt::attribute{"min", 0.0f},
        })
        .data<&particle_collision_decal::probability>("probability"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probability"},
            entt::attribute{"pretty_name", "Probability"},
            entt::attribute{"tooltip", "Chance that a contact leaves a decal."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&particle_collision_decal::attach_to_entity>("attach_to_entity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "attach_to_entity"},
            entt::attribute{"pretty_name", "Attach To Entity"},
            entt::attribute{"tooltip", "The decal follows the entity whose collider the particle touched, so it moves with a door or a crate."},
        });
}

SAVE(particle_collision_decal)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("material", obj.material));
    try_save(ar, ser20::make_nvp("size", obj.size));
    try_save(ar, ser20::make_nvp("lifetime", obj.lifetime));
    try_save(ar, ser20::make_nvp("fade_out", obj.fade_out));
    try_save(ar, ser20::make_nvp("probability", obj.probability));
    try_save(ar, ser20::make_nvp("attach_to_entity", obj.attach_to_entity));
}
SAVE_INSTANTIATE(particle_collision_decal, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(particle_collision_decal, ser20::oarchive_binary_t);

LOAD(particle_collision_decal)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("material", obj.material));
    try_load(ar, ser20::make_nvp("size", obj.size));
    try_load(ar, ser20::make_nvp("lifetime", obj.lifetime));
    try_load(ar, ser20::make_nvp("fade_out", obj.fade_out));
    try_load(ar, ser20::make_nvp("probability", obj.probability));
    try_load(ar, ser20::make_nvp("attach_to_entity", obj.attach_to_entity));
}
LOAD_INSTANTIATE(particle_collision_decal, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(particle_collision_decal, ser20::iarchive_binary_t);

REFLECT(particle_emitter_component)
{
    // Register EmitterShape enum

    entt::meta_factory<ps_soa::emitter_shape>{}
        .type("emitter_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emitter_shape"},
            entt::attribute{"pretty_name", "Emitter Shape"},
        })
        .data<ps_soa::emitter_shape::sphere>("sphere"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sphere"},
            entt::attribute{"pretty_name", "Sphere"},
        })
        .data<ps_soa::emitter_shape::hemisphere>("hemisphere"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hemisphere"},
            entt::attribute{"pretty_name", "Hemisphere"},
        })
        .data<ps_soa::emitter_shape::circle>("circle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "circle"},
            entt::attribute{"pretty_name", "Circle"},
        })
        .data<ps_soa::emitter_shape::box>("box"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "box"},
            entt::attribute{"pretty_name", "Box"},
        })
        .data<ps_soa::emitter_shape::rect>("rect"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rect"},
            entt::attribute{"pretty_name", "Rectangle"},
        })
        .data<ps_soa::emitter_shape::cone>("cone"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cone"},
            entt::attribute{"pretty_name", "Cone"},
        })
        .data<ps_soa::emitter_shape::line>("line"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "line"},
            entt::attribute{"pretty_name", "Line"},
        })
        .data<ps_soa::emitter_shape::torus>("torus"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "torus"},
            entt::attribute{"pretty_name", "Torus"},
        })
        .data<ps_soa::emitter_shape::mesh>("mesh"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh"},
            entt::attribute{"pretty_name", "Mesh"},
        });

    entt::meta_factory<ps_soa::collision_mode>{}
        .type("collision_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_mode"},
            entt::attribute{"pretty_name", "Collision Mode"},
        })
        .data<ps_soa::collision_mode::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
        })
        .data<ps_soa::collision_mode::kill>("kill"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "kill"},
            entt::attribute{"pretty_name", "Kill"},
        })
        .data<ps_soa::collision_mode::bounce>("bounce"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounce"},
            entt::attribute{"pretty_name", "Bounce"},
        })
        .data<ps_soa::collision_mode::stick>("stick"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stick"},
            entt::attribute{"pretty_name", "Stick"},
        });

    entt::meta_factory<ps_soa::collision_source>{}
        .type("collision_source"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_source"},
            entt::attribute{"pretty_name", "Collision Source"},
        })
        .data<ps_soa::collision_source::planes>("planes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "planes"},
            entt::attribute{"pretty_name", "Planes"},
        })
        .data<ps_soa::collision_source::physics_colliders>("physics_colliders"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "physics_colliders"},
            entt::attribute{"pretty_name", "Physics Colliders"},
        })
        .data<ps_soa::collision_source::scene_surfaces>("scene_surfaces"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scene_surfaces"},
            entt::attribute{"pretty_name", "Scene Surfaces"},
        });

    entt::meta_factory<ps_soa::scene_collision>{}
        .type("scene_collision"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scene_collision"},
            entt::attribute{"pretty_name", "Scene Collision"},
        })
        .data<ps_soa::scene_collision::off>("off"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "off"},
            entt::attribute{"pretty_name", "Off"},
        })
        .data<ps_soa::scene_collision::depth_buffer>("depth_buffer"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_buffer"},
            entt::attribute{"pretty_name", "Depth Buffer"},
        })
        .data<ps_soa::scene_collision::distance_field>("distance_field"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "distance_field"},
            entt::attribute{"pretty_name", "Distance Field"},
        });

    entt::meta_factory<ps_soa::collision_quality>{}
        .type("collision_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_quality"},
            entt::attribute{"pretty_name", "Collision Quality"},
        })
        .data<ps_soa::collision_quality::low>("low"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "low"},
            entt::attribute{"pretty_name", "Low"},
        })
        .data<ps_soa::collision_quality::medium>("medium"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "medium"},
            entt::attribute{"pretty_name", "Medium"},
        })
        .data<ps_soa::collision_quality::high>("high"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high"},
            entt::attribute{"pretty_name", "High"},
        });

    entt::meta_factory<ps_soa::particle_lighting>{}
        .type("particle_lighting"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_lighting"},
            entt::attribute{"pretty_name", "Particle Lighting"},
        })
        .data<ps_soa::particle_lighting::unlit>("unlit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "unlit"},
            entt::attribute{"pretty_name", "Unlit"},
        })
        .data<ps_soa::particle_lighting::volumetric>("volumetric"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric"},
            entt::attribute{"pretty_name", "Volumetric"},
        })
        .data<ps_soa::particle_lighting::surface>("surface"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface"},
            entt::attribute{"pretty_name", "Surface"},
        });

    entt::meta_factory<ps_soa::particle_ambient>{}
        .type("particle_ambient"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_ambient"},
            entt::attribute{"pretty_name", "Particle Ambient Light"},
        })
        .data<ps_soa::particle_ambient::sky>("sky"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sky"},
            entt::attribute{"pretty_name", "Sky"},
        })
        .data<ps_soa::particle_ambient::global_illumination>("global_illumination"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "global_illumination"},
            entt::attribute{"pretty_name", "Global Illumination"},
        });

    entt::meta_factory<ps_soa::ribbon_texture_mode>{}
        .type("ribbon_texture_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ribbon_texture_mode"},
            entt::attribute{"pretty_name", "Ribbon Texture Mode"},
        })
        .data<ps_soa::ribbon_texture_mode::age>("age"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "age"},
            entt::attribute{"pretty_name", "Age"},
        })
        .data<ps_soa::ribbon_texture_mode::stretch>("stretch"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stretch"},
            entt::attribute{"pretty_name", "Stretch"},
        })
        .data<ps_soa::ribbon_texture_mode::tile>("tile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tile"},
            entt::attribute{"pretty_name", "Tile"},
        });

    entt::meta_factory<ps_soa::ribbon_facing>{}
        .type("ribbon_facing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ribbon_facing"},
            entt::attribute{"pretty_name", "Ribbon Facing"},
        })
        .data<ps_soa::ribbon_facing::camera>("camera"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera"},
            entt::attribute{"pretty_name", "Camera"},
        })
        .data<ps_soa::ribbon_facing::launch_direction>("launch_direction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "launch_direction"},
            entt::attribute{"pretty_name", "Launch Direction"},
        });

    entt::meta_factory<ps_soa::ribbon_smoothing>{}
        .type("ribbon_smoothing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ribbon_smoothing"},
            entt::attribute{"pretty_name", "Ribbon Smoothing"},
        })
        .data<ps_soa::ribbon_smoothing::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
        })
        .data<ps_soa::ribbon_smoothing::low>("low"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "low"},
            entt::attribute{"pretty_name", "Low"},
        })
        .data<ps_soa::ribbon_smoothing::medium>("medium"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "medium"},
            entt::attribute{"pretty_name", "Medium"},
        })
        .data<ps_soa::ribbon_smoothing::high>("high"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high"},
            entt::attribute{"pretty_name", "High"},
        });

    entt::meta_factory<ps_soa::mesh_spawn_mode>{}
        .type("mesh_spawn_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh_spawn_mode"},
            entt::attribute{"pretty_name", "Mesh Spawn Mode"},
        })
        .data<ps_soa::mesh_spawn_mode::surface>("surface"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface"},
            entt::attribute{"pretty_name", "Surface"},
        })
        .data<ps_soa::mesh_spawn_mode::vertices>("vertices"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vertices"},
            entt::attribute{"pretty_name", "Vertices"},
        });


    entt::meta_factory<ps_soa::emitter_direction>{}
        .type("emitter_direction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emitter_direction"},
            entt::attribute{"pretty_name", "Emitter Direction"},
        })
        .data<ps_soa::emitter_direction::up>("up"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "up"},
            entt::attribute{"pretty_name", "Up"},
        })
        .data<ps_soa::emitter_direction::outward>("outward"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "outward"},
            entt::attribute{"pretty_name", "Outward"},
        })
        .data<ps_soa::emitter_direction::inward>("inward"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "inward"},
            entt::attribute{"pretty_name", "Inward"},
        });

    entt::meta_factory<ps_soa::spawn_location>{}
        .type("spawn_location"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spawn_location"},
            entt::attribute{"pretty_name", "Emitter Spawn Location"},
        })
        .data<ps_soa::spawn_location::inside>("inside"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "inside"},
            entt::attribute{"pretty_name", "Inside"},
        })
        .data<ps_soa::spawn_location::surface>("surface"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface"},
            entt::attribute{"pretty_name", "Surface"},
        });

    entt::meta_factory<bx::Easing::Enum>{}
        .type("easing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "easing"},
            entt::attribute{"pretty_name", "Easing Function"},
        })
        .data<bx::Easing::Linear>("linear"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "linear"},
            entt::attribute{"pretty_name", "Linear"},
        })
        .data<bx::Easing::Step>("step"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "step"},
            entt::attribute{"pretty_name", "Step"},
        })
        .data<bx::Easing::SmoothStep>("smooth_step"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "smooth_step"},
            entt::attribute{"pretty_name", "Smooth Step"},
        })
        .data<bx::Easing::InQuad>("in_quad"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_quad"},
            entt::attribute{"pretty_name", "In Quad"},
        })
        .data<bx::Easing::OutQuad>("out_quad"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "out_quad"},
            entt::attribute{"pretty_name", "Out Quad"},
        })
        .data<bx::Easing::InOutQuad>("in_out_quad"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_out_quad"},
            entt::attribute{"pretty_name", "In-Out Quad"},
        })
        .data<bx::Easing::InCubic>("in_cubic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_cubic"},
            entt::attribute{"pretty_name", "In Cubic"},
        })
        .data<bx::Easing::OutCubic>("out_cubic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "out_cubic"},
            entt::attribute{"pretty_name", "Out Cubic"},
        })
        .data<bx::Easing::InOutCubic>("in_out_cubic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_out_cubic"},
            entt::attribute{"pretty_name", "In-Out Cubic"},
        })
        .data<bx::Easing::InSine>("in_sine"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_sine"},
            entt::attribute{"pretty_name", "In Sine"},
        })
        .data<bx::Easing::OutSine>("out_sine"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "out_sine"},
            entt::attribute{"pretty_name", "Out Sine"},
        })
        .data<bx::Easing::InOutSine>("in_out_sine"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_out_sine"},
            entt::attribute{"pretty_name", "In-Out Sine"},
        })
        .data<bx::Easing::InExpo>("in_expo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_expo"},
            entt::attribute{"pretty_name", "In Expo"},
        })
        .data<bx::Easing::OutExpo>("out_expo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "out_expo"},
            entt::attribute{"pretty_name", "Out Expo"},
        })
        .data<bx::Easing::InOutExpo>("in_out_expo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_out_expo"},
            entt::attribute{"pretty_name", "In-Out Expo"},
        })
        .data<bx::Easing::InElastic>("in_elastic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_elastic"},
            entt::attribute{"pretty_name", "In Elastic"},
        })
        .data<bx::Easing::OutElastic>("out_elastic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "out_elastic"},
            entt::attribute{"pretty_name", "Out Elastic"},
        })
        .data<bx::Easing::InOutElastic>("in_out_elastic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_out_elastic"},
            entt::attribute{"pretty_name", "In-Out Elastic"},
        })
        .data<bx::Easing::InBack>("in_back"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_back"},
            entt::attribute{"pretty_name", "In Back"},
        })
        .data<bx::Easing::OutBack>("out_back"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "out_back"},
            entt::attribute{"pretty_name", "Out Back"},
        })
        .data<bx::Easing::InOutBack>("in_out_back"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_out_back"},
            entt::attribute{"pretty_name", "In-Out Back"},
        })
        .data<bx::Easing::InBounce>("in_bounce"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_bounce"},
            entt::attribute{"pretty_name", "In Bounce"},
        })
        .data<bx::Easing::OutBounce>("out_bounce"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "out_bounce"},
            entt::attribute{"pretty_name", "Out Bounce"},
        })
        .data<bx::Easing::InOutBounce>("in_out_bounce"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "in_out_bounce"},
            entt::attribute{"pretty_name", "In-Out Bounce"},
        });

    entt::meta_factory<ps_soa::simulation_space>{}
        .type("simulation_space"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "simulation_space"},
            entt::attribute{"pretty_name", "Simulation Space"},
        })
        .data<ps_soa::simulation_space::world>("world"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world"},
            entt::attribute{"pretty_name", "World"},
        })
        .data<ps_soa::simulation_space::local>("local"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local"},
            entt::attribute{"pretty_name", "Local"},
        });

    entt::meta_factory<ps_soa::texture_mode>{}
        .type("texture_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_mode"},
            entt::attribute{"pretty_name", "Texture Mode"},
        })
        .data<ps_soa::texture_mode::multi_channel>("multi_channel"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "multi_channel"},
            entt::attribute{"pretty_name", "Multi Channel"},
        })
        .data<ps_soa::texture_mode::mask>("mask"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mask"},
            entt::attribute{"pretty_name", "Mask"},
        });

    entt::meta_factory<ps_soa::render_mode>{}
        .type("render_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "render_mode"},
            entt::attribute{"pretty_name", "Render Mode"},
        })
        .data<ps_soa::render_mode::billboard>("billboard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "billboard"},
            entt::attribute{"pretty_name", "Billboard"},
        })
        .data<ps_soa::render_mode::horizontal_billboard>("horizontal_billboard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "horizontal_billboard"},
            entt::attribute{"pretty_name", "Horizontal Billboard"},
        })
        .data<ps_soa::render_mode::vertical_billboard>("vertical_billboard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vertical_billboard"},
            entt::attribute{"pretty_name", "Vertical Billboard"},
        })
        .data<ps_soa::render_mode::camera_facing>("camera_facing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera_facing"},
            entt::attribute{"pretty_name", "Camera Facing"},
        })
        .data<ps_soa::render_mode::stretched>("stretched"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stretched"},
            entt::attribute{"pretty_name", "Stretched"},
        })
        .data<ps_soa::render_mode::trail>("trail"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "trail"},
            entt::attribute{"pretty_name", "Trail"},
        })
        .data<ps_soa::render_mode::ribbon>("ribbon"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ribbon"},
            entt::attribute{"pretty_name", "Ribbon"},
        })
        .data<ps_soa::render_mode::mesh>("mesh"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh"},
            entt::attribute{"pretty_name", "Mesh"},
        })
        .data<ps_soa::render_mode::fixed_axis>("fixed_axis"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fixed_axis"},
            entt::attribute{"pretty_name", "Fixed Axis"},
        });

    entt::meta_factory<ps_soa::blend_mode>{}
        .type("blend_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_mode"},
            entt::attribute{"pretty_name", "Blend Mode"},
        })
        .data<ps_soa::blend_mode::normal>("normal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal"},
            entt::attribute{"pretty_name", "Normal"},
        })
        .data<ps_soa::blend_mode::additive>("additive"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "additive"},
            entt::attribute{"pretty_name", "Additive"},
        })
        .data<ps_soa::blend_mode::multiply>("multiply"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "multiply"},
            entt::attribute{"pretty_name", "Multiply"},
        })
        .data<ps_soa::blend_mode::premultiplied>("premultiplied"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "premultiplied"},
            entt::attribute{"pretty_name", "Premultiplied"},
        });

    entt::meta_factory<ps_soa::particle_sort_backend>{}
        .type("particle_sort_backend"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_sort_backend"},
            entt::attribute{"pretty_name", "Particle Sim Backend"},
        })
        .data<ps_soa::particle_sort_backend::cpu>("cpu"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cpu"},
            entt::attribute{"pretty_name", "CPU"},
        })
        .data<ps_soa::particle_sort_backend::gpu>("gpu"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gpu"},
            entt::attribute{"pretty_name", "GPU"},
        });

    // Not "culling_mode": the animation component registers its own enum under that id.
    entt::meta_factory<particle_emitter_component::culling_mode>{}
        .type("particle_culling_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "culling_mode"},
            entt::attribute{"pretty_name", "Particle Culling Mode"},
        })
        .data<particle_emitter_component::culling_mode::always_simulate>("always_simulate"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "always_simulate"},
            entt::attribute{"pretty_name", "Always Simulate"},
        })
        .data<particle_emitter_component::culling_mode::renderer_based>("renderer_based"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "renderer_based"},
            entt::attribute{"pretty_name", "Renderer Based"},
        })
        .data<particle_emitter_component::culling_mode::pause>("pause"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pause"},
            entt::attribute{"pretty_name", "Pause"},
        });

    entt::meta_factory<ps_soa::sort_mode>{}
        .type("sort_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sort_mode"},
            entt::attribute{"pretty_name", "Particle Sort Mode"},
        })
        .data<ps_soa::sort_mode::automatic>("automatic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "automatic"},
            entt::attribute{"pretty_name", "Auto"},
        })
        .data<ps_soa::sort_mode::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
        })
        .data<ps_soa::sort_mode::view_distance>("view_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "view_distance"},
            entt::attribute{"pretty_name", "View Distance"},
        })
        .data<ps_soa::sort_mode::view_depth>("view_depth"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "view_depth"},
            entt::attribute{"pretty_name", "View Depth"},
        })
        .data<ps_soa::sort_mode::oldest_first>("oldest_first"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "oldest_first"},
            entt::attribute{"pretty_name", "Oldest First"},
        })
        .data<ps_soa::sort_mode::youngest_first>("youngest_first"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "youngest_first"},
            entt::attribute{"pretty_name", "Youngest First"},
        });

    entt::meta_factory<ps_soa::bounds_mode>{}
        .type("bounds_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounds_mode"},
            entt::attribute{"pretty_name", "Particle Bounds Mode"},
        })
        .data<ps_soa::bounds_mode::automatic>("automatic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "automatic"},
            entt::attribute{"pretty_name", "Automatic"},
        })
        .data<ps_soa::bounds_mode::fixed>("fixed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fixed"},
            entt::attribute{"pretty_name", "Fixed"},
        });

    entt::meta_factory<ps_soa::particle_primitive>{}
        .type("particle_primitive"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_primitive"},
            entt::attribute{"pretty_name", "Particle Primitive"},
        })
        .data<ps_soa::particle_primitive::quad>("quad"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "quad"},
            entt::attribute{"pretty_name", "Quad"},
        })
        .data<ps_soa::particle_primitive::octagon>("octagon"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "octagon"},
            entt::attribute{"pretty_name", "Octagon"},
        })
        .data<ps_soa::particle_primitive::cutout>("cutout"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cutout"},
            entt::attribute{"pretty_name", "Cutout"},
        });

    // Inspector visibility: a setting shows only where it applies (its mode, shape or feature is on).
    const auto when = [](auto test)
    {
        return entt::property_predicate<bool>(
            [test](const entt::meta_any& object)
            {
                const auto* emitter = object.try_cast<particle_emitter_component>();
                return emitter != nullptr && test(*emitter);
            });
    };
    using emitter = particle_emitter_component;
    const auto is_sprite = [](const emitter& e)
    {
        return e.get_render_mode() != ps_soa::render_mode::mesh;
    };
    const auto has_texture_sheet = [is_sprite](const emitter& e)
    {
        const math::vec2 tiles = e.get_texture_sheet_tiles();
        return is_sprite(e) && tiles.x * tiles.y > 1.0f;
    };
    const auto is_shape_cone = when([](const emitter& e) { return e.get_shape() == ps_soa::emitter_shape::cone; });
    const auto is_shape_torus = when([](const emitter& e) { return e.get_shape() == ps_soa::emitter_shape::torus; });
    const auto is_shape_mesh = when([](const emitter& e) { return e.get_shape() == ps_soa::emitter_shape::mesh; });
    const auto is_not_shape_mesh = when([](const emitter& e) { return e.get_shape() != ps_soa::emitter_shape::mesh; });
    const auto has_fixed_seed = when([](const emitter& e) { return e.get_use_fixed_seed(); });
    const auto has_collision = when([](const emitter& e) { return e.get_collision() != ps_soa::collision_mode::none; });
    const auto has_bounce = when([](const emitter& e) { return e.get_collision() == ps_soa::collision_mode::bounce; });
    const auto has_collider_collision = when([](const emitter& e)
                                             {
                                                 return e.get_collision() != ps_soa::collision_mode::none &&
                                                        e.get_collide_with() == ps_soa::collision_source::physics_colliders;
                                             });
    // The contacts the CPU finds (planes, colliders): the ones a contact's decal and events come from.
    const auto has_known_contacts = when([](const emitter& e)
                                         {
                                             return e.get_collision() != ps_soa::collision_mode::none &&
                                                    e.get_collide_with() != ps_soa::collision_source::scene_surfaces;
                                         });
    const auto has_lasting_contact = when([](const emitter& e)
                                          {
                                              return e.get_collision() == ps_soa::collision_mode::bounce ||
                                                     e.get_collision() == ps_soa::collision_mode::stick;
                                          });
    const auto has_resting_particles = when([](const emitter& e)
                                            {
                                                return e.get_collision() == ps_soa::collision_mode::stick ||
                                                       (e.get_collision() == ps_soa::collision_mode::bounce && e.get_rest_speed() > 0.0f);
                                            });
    const auto has_turbulence = when([](const emitter& e) { return e.get_turbulence_strength() > 0.0f; });
    const auto has_orbit = when([](const emitter& e) { return e.get_orbit_speed() != 0.0f; });
    const auto has_radial = when([](const emitter& e) { return e.get_radial_speed() != 0.0f; });
    const auto has_axis_force = when([](const emitter& e) { return e.get_orbit_speed() != 0.0f || e.get_radial_speed() != 0.0f; });
    const auto has_converge = when([](const emitter& e) { return e.get_converge_to_point(); });
    const auto has_two_gradients = when([](const emitter& e) { return e.get_random_between_gradients(); });
    const auto is_sprite_mode = when(is_sprite);
    const auto is_sheet = when(has_texture_sheet);
    const auto is_sheet_by_cycles = when([has_texture_sheet](const emitter& e)
                                         { return has_texture_sheet(e) && !e.get_texture_sheet_use_frame_curve(); });
    const auto is_sheet_by_curve = when([has_texture_sheet](const emitter& e)
                                        { return has_texture_sheet(e) && e.get_texture_sheet_use_frame_curve(); });
    const auto is_trail = when([](const emitter& e) { return e.get_render_mode() == ps_soa::render_mode::trail; });
    const auto is_ribbon = when([](const emitter& e) { return e.get_render_mode() == ps_soa::render_mode::ribbon; });
    const auto is_ribbon_tile = when(
        [](const emitter& e)
        {
            return e.get_render_mode() == ps_soa::render_mode::ribbon &&
                   e.get_ribbon_texture_mode() == ps_soa::ribbon_texture_mode::tile;
        });
    const auto is_stretched = when([](const emitter& e) { return e.get_render_mode() == ps_soa::render_mode::stretched; });
    const auto is_fixed_axis = when([](const emitter& e) { return e.get_render_mode() == ps_soa::render_mode::fixed_axis; });
    const auto is_mesh_mode = when([](const emitter& e) { return e.get_render_mode() == ps_soa::render_mode::mesh; });
    const auto is_cutout = when([is_sprite](const emitter& e)
                                { return is_sprite(e) && e.get_primitive() == ps_soa::particle_primitive::cutout; });
    // The Lighting rows apply to a material that is lit: one whose Shading Model is Unlit shows as it is.
    const auto draws_lit_material = [is_sprite](const emitter& e)
    {
        return is_sprite(e) && e.draws_lit_material();
    };
    const auto is_lit_material = when(draws_lit_material);
    const auto is_lit = when([draws_lit_material](const emitter& e)
                             { return draws_lit_material(e) && e.get_lighting() != ps_soa::particle_lighting::unlit; });
    const auto is_surface_lit = when([draws_lit_material](const emitter& e)
                                     { return draws_lit_material(e) && e.get_lighting() == ps_soa::particle_lighting::surface; });
    static_assert(MATERIAL_GRAPH_MAX_PARTICLE_GI_TEXTURES == 2,
                  "The Ambient Light tooltip, the Inspector's note and the audit's catalog say two textures.");
    // The overrides show for a material the emitter draws, while it loads too.
    const auto has_material = when([](const emitter& e) { return e.draws_own_material(); });
    const auto has_particle_lights = when([](const emitter& e) { return e.get_light_count() > 0; });
    const auto writes_velocity = when([is_sprite](const emitter& e) { return is_sprite(e) && e.get_write_velocity(); });
    const auto has_draw_distance = when([](const emitter& e) { return e.get_max_draw_distance() > 0.0f; });
    const auto has_fixed_bounds = when([](const emitter& e) { return e.get_bounds_mode() == ps_soa::bounds_mode::fixed; });

    entt::meta_factory<particle_emitter_component>{}
        .type("particle_emitter_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_emitter_component"},
            entt::attribute{COMPONENT_RENDER_FEATURE_ATTRIBUTE, render_feature::particle_storage_buffers},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Particle Emitter"},
        })
        .func<&component_meta<particle_emitter_component>::exists>("component_exists"_hs)
        .func<&component_meta<particle_emitter_component>::add>("component_add"_hs)
        .func<&component_meta<particle_emitter_component>::remove>("component_remove"_hs)
        .func<&component_meta<particle_emitter_component>::save>("component_save"_hs)
        .func<&component_meta<particle_emitter_component>::load>("component_load"_hs)
        .data<&particle_emitter_component::set_enabled, &particle_emitter_component::is_enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Off hides the emitter and freezes it: nothing spawns, moves or draws until it is on again."},
        })
        .data<&particle_emitter_component::set_loop, &particle_emitter_component::is_loop>("loop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "loop"},
            entt::attribute{"pretty_name", "Loop"},
            entt::attribute{"tooltip", "Repeat the emission cycle for as long as the emitter plays. Off, it emits for one cycle (Duration) and stops once its last particle is gone."},
        })
        .data<&particle_emitter_component::set_duration, &particle_emitter_component::get_duration>("duration"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "duration"},
            entt::attribute{"pretty_name", "Duration (s)"},
            entt::attribute{"tooltip", "Length of one emission cycle: the time the rates emit for and the bursts are timed against."},
            entt::attribute{"min", 0.05f},
            entt::attribute{"step", 0.1f},
        })
        .data<&particle_emitter_component::set_start_delay, &particle_emitter_component::get_start_delay>("start_delay"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "start_delay"},
            entt::attribute{"pretty_name", "Start Delay (s)"},
            entt::attribute{"tooltip", "Time after the emitter starts playing before it emits."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&particle_emitter_component::set_prewarm, &particle_emitter_component::get_prewarm>("prewarm"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "prewarm"},
            entt::attribute{"pretty_name", "Prewarm"},
            entt::attribute{"tooltip", "A looping emitter starts full, as if it had been running for a whole particle lifetime, when it first plays: ambient fire, smoke and mist do not build up from nothing when the scene starts."},
        })
        .data<&particle_emitter_component::set_auto_play, &particle_emitter_component::get_auto_play>("auto_play"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "auto_play"},
            entt::attribute{"pretty_name", "Auto Play"},
            entt::attribute{"tooltip", "Play from the moment the emitter is created (scene load, play start, instantiation). Off, it waits for a script or the inspector to play it. Bursts (Emit) work either way."},
        })
        .data<&particle_emitter_component::set_lifetime, &particle_emitter_component::get_lifetime>("lifetime"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime"},
            entt::attribute{"pretty_name", "Lifetime (s)"},
            entt::attribute{"tooltip", "How long each particle lives, random between min and max."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&particle_emitter_component::set_max_particles, &particle_emitter_component::get_max_particles>("max_particles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_particles"},
            entt::attribute{"pretty_name", "Max Particles"},
            entt::attribute{"tooltip", "Most particles alive at once. Spawns beyond it are skipped."},
        })
        .data<&particle_emitter_component::set_simulation_space, &particle_emitter_component::get_simulation_space>("simulation_space"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "simulation_space"},
            entt::attribute{"pretty_name", "Simulation Space"},
            entt::attribute{"tooltip", "World: particles stay where they were born when the emitter moves. Local: they move with it. Gravity pulls down in the world either way."},
        })
        .data<&particle_emitter_component::set_use_fixed_seed, &particle_emitter_component::get_use_fixed_seed>("use_fixed_seed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "use_fixed_seed"},
            entt::attribute{"pretty_name", "Fixed Seed"},
            entt::attribute{"tooltip", "Replay the same particles every time the emitter resets. Off, every emitter and every reset gets a fresh random sequence, so copies never move in step."},
        })
        .data<&particle_emitter_component::set_random_seed, &particle_emitter_component::get_random_seed>("random_seed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "random_seed"},
            entt::attribute{"pretty_name", "Random Seed"},
            entt::attribute{"tooltip", "Seed of the replayed sequence."},
            entt::attribute{"predicate", has_fixed_seed},
        })
        .data<&particle_emitter_component::set_emission_rate, &particle_emitter_component::get_emission_rate>("emission_rate"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emission_rate"},
            entt::attribute{"pretty_name", "Rate Over Time (/s)"},
            entt::attribute{"tooltip", "Particles per second. Each is born at its exact time within the frame, so fast emitters leave even trails at any frame rate."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Emission"},
        })
        .data<&particle_emitter_component::set_rate_over_distance, &particle_emitter_component::get_rate_over_distance>("rate_over_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rate_over_distance"},
            entt::attribute{"pretty_name", "Rate Over Distance (/m)"},
            entt::attribute{"tooltip", "Particles per metre the emitter moves, spaced evenly along its path: trails and footprints that stay even at any speed or frame rate."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Emission"},
        })
        .data<&particle_emitter_component::set_bursts, &particle_emitter_component::get_bursts>("bursts"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bursts"},
            entt::attribute{"pretty_name", "Bursts"},
            entt::attribute{"tooltip", "Particles fired at set times of each cycle on top of the rates: an explosion's flash at 0 s, its debris at 0.05 s, a puff every 0.5 s. A one-shot emitter with no rate emits only its bursts."},
            entt::attribute{"group", "Emission"},
        })
        .data<&particle_emitter_component::set_spawn_along_path, &particle_emitter_component::get_spawn_along_path>("spawn_along_path"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spawn_along_path"},
            entt::attribute{"pretty_name", "Spawn Along Path"},
            entt::attribute{"tooltip", "Where a moving emitter's particles appear: 1 = spread along the path it moved this frame (smooth trails), 0 = all at its current place."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"group", "Emission"},
            entt::attribute{"former_name", "temporal_motion"},
        })
        .data<&particle_emitter_component::set_lifetime_by_emitter_speed_curve, &particle_emitter_component::get_lifetime_by_emitter_speed_curve>("lifetime_by_emitter_speed_curve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime_by_emitter_speed_curve"},
            entt::attribute{"pretty_name", "Lifetime By Emitter Speed"},
            entt::attribute{"tooltip", "Multiplier of the lifetime by how fast the emitter moves across Emitter Speed Range: a trail that grows longer when its emitter speeds up."},
            entt::attribute{"group", "Emission"},
            entt::attribute{"former_name", "lifetime_by_emitter_speed_gradient"},
        })
        .data<&particle_emitter_component::set_lifetime_by_emitter_speed_range, &particle_emitter_component::get_lifetime_by_emitter_speed_range>("lifetime_by_emitter_speed_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime_by_emitter_speed_range"},
            entt::attribute{"pretty_name", "Emitter Speed Range (m/s)"},
            entt::attribute{"tooltip", "Emitter speeds the lifetime curve maps over: the slow end takes its start, the fast end its end. Min = max turns it off."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Emission"},
        })
        .data<&particle_emitter_component::set_shape, &particle_emitter_component::get_shape>("shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape"},
            entt::attribute{"pretty_name", "Shape"},
            entt::attribute{"tooltip", "Where particles are born: Sphere, Hemisphere (the half above the emitter), Circle (a flat disc), Box, Rectangle (flat), Cone (its base disc, launching along its slant), Line (along X), Torus or Mesh."},
            entt::attribute{"group", "Shape"},
        })
        .data<&particle_emitter_component::set_shape_position, &particle_emitter_component::get_shape_position>("shape_position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape_position"},
            entt::attribute{"pretty_name", "Position"},
            entt::attribute{"tooltip", "Offset of the shape from the emitter, in shape units (scaled with it)."},
            entt::attribute{"group", "Shape"},
            entt::attribute{"former_name", "emission_shape_position"},
        })
        .data<&particle_emitter_component::set_shape_scale, &particle_emitter_component::get_shape_scale>("shape_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape_scale"},
            entt::attribute{"pretty_name", "Scale"},
            entt::attribute{"tooltip", "Size of the shape, metres per unit (a radius of 1 for the round shapes)."},
            entt::attribute{"group", "Shape"},
            entt::attribute{"former_name", "emission_shape_scale"},
        })
        .data<&particle_emitter_component::set_cone_angle, &particle_emitter_component::get_cone_angle>("cone_angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cone_angle"},
            entt::attribute{"pretty_name", "Cone Angle (deg)"},
            entt::attribute{"tooltip", "Angle between the cone's axis and its slant: particles at the centre of the base launch straight up, those on the rim at this angle."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 90.0f},
            entt::attribute{"group", "Shape"},
            entt::attribute{"predicate", is_shape_cone},
            entt::attribute{"former_name", "emission_cone_angle"},
        })
        .data<&particle_emitter_component::set_torus_thickness, &particle_emitter_component::get_torus_thickness>("torus_thickness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "torus_thickness"},
            entt::attribute{"pretty_name", "Torus Thickness"},
            entt::attribute{"tooltip", "The tube's radius as a share of the ring's."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"group", "Shape"},
            entt::attribute{"predicate", is_shape_torus},
            entt::attribute{"former_name", "emission_torus_thickness"},
        })
        .data<&particle_emitter_component::set_shape_mesh, &particle_emitter_component::get_shape_mesh>("shape_mesh"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape_mesh"},
            entt::attribute{"pretty_name", "Mesh"},
            entt::attribute{"tooltip", "The mesh particles are born on, in the emitter's space (Position and Scale apply). Outward launches along its normals."},
            entt::attribute{"group", "Shape"},
            entt::attribute{"predicate", is_shape_mesh},
            entt::attribute{"former_name", "emission_shape_mesh"},
        })
        .data<&particle_emitter_component::set_shape_mesh_mode, &particle_emitter_component::get_shape_mesh_mode>("shape_mesh_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape_mesh_mode"},
            entt::attribute{"pretty_name", "Mesh Spawn"},
            entt::attribute{"tooltip", "Surface: anywhere on the mesh, evenly by area. Vertices: on its vertices."},
            entt::attribute{"group", "Shape"},
            entt::attribute{"predicate", is_shape_mesh},
            entt::attribute{"former_name", "emission_mesh_mode"},
        })
        .data<&particle_emitter_component::set_spawn_location, &particle_emitter_component::get_spawn_location>("spawn_location"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spawn_location"},
            entt::attribute{"pretty_name", "Spawn Location"},
            entt::attribute{"tooltip", "Inside: anywhere in the shape's volume or area. Surface: only on its surface or rim."},
            entt::attribute{"group", "Shape"},
            entt::attribute{"predicate", is_not_shape_mesh},
        })
        .data<&particle_emitter_component::set_direction, &particle_emitter_component::get_direction>("direction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "direction"},
            entt::attribute{"pretty_name", "Direction"},
            entt::attribute{"tooltip", "Launch direction: Up (the emitter's +Y), Outward (away from the shape's centre) or Inward (toward it)."},
            entt::attribute{"group", "Shape"},
        })
        .data<&particle_emitter_component::set_direction_spread, &particle_emitter_component::get_direction_spread>("direction_spread"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "direction_spread"},
            entt::attribute{"pretty_name", "Direction Spread (deg)"},
            entt::attribute{"tooltip", "Half angle of the cone around the launch direction particles spread over: 0 = exactly along it, 180 = any direction."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 180.0f},
            entt::attribute{"group", "Shape"},
        })
        .data<&particle_emitter_component::set_start_speed, &particle_emitter_component::get_start_speed>("start_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "start_speed"},
            entt::attribute{"pretty_name", "Start Speed (m/s)"},
            entt::attribute{"tooltip", "Launch speed, random between min and max."},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Motion"},
        })
        .data<&particle_emitter_component::set_speed_over_life, &particle_emitter_component::get_speed_over_life>("speed_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "speed_over_life"},
            entt::attribute{"pretty_name", "Speed Over Life"},
            entt::attribute{"tooltip", "Multiplier of the launch velocity over the particle's life (1 throughout = constant). Gravity and acceleration are not scaled by it."},
            entt::attribute{"group", "Motion"},
        })
        .data<&particle_emitter_component::set_motion_easing, &particle_emitter_component::get_motion_easing>("motion_easing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "motion_easing"},
            entt::attribute{"pretty_name", "Motion Easing"},
            entt::attribute{"tooltip", "Remaps the particle's life before its motion is evaluated: Linear = physical timing. Out curves rush first and settle (explosions), In curves start slowly."},
            entt::attribute{"group", "Motion"},
            entt::attribute{"former_name", "position_easing"},
        })
        .data<&particle_emitter_component::set_drag, &particle_emitter_component::get_drag>("drag"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "drag"},
            entt::attribute{"pretty_name", "Drag (1/s)"},
            entt::attribute{"tooltip", "Air resistance: velocity decays by exp(-drag x time), so gravity and acceleration reach a terminal speed."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Motion"},
        })
        .data<&particle_emitter_component::set_gravity_scale, &particle_emitter_component::get_gravity_scale>("gravity_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gravity_scale"},
            entt::attribute{"pretty_name", "Gravity Scale"},
            entt::attribute{"tooltip", "Multiples of standard gravity (9.81 m/s^2, down in the world). Negative values lift."},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Motion"},
        })
        .data<&particle_emitter_component::set_acceleration, &particle_emitter_component::get_acceleration>("acceleration"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "acceleration"},
            entt::attribute{"pretty_name", "Acceleration (m/s^2)"},
            entt::attribute{"tooltip", "Constant acceleration in simulation space on top of gravity: wind, buoyancy, a pull."},
            entt::attribute{"group", "Motion"},
            entt::attribute{"former_name", "force_over_lifetime"},
        })
        .data<&particle_emitter_component::set_inherit_velocity, &particle_emitter_component::get_inherit_velocity>("inherit_velocity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "inherit_velocity"},
            entt::attribute{"pretty_name", "Inherit Velocity"},
            entt::attribute{"tooltip", "Share of the emitter's own velocity new particles carry (world simulation space)."},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Motion"},
        })
        .data<&particle_emitter_component::set_turbulence_strength, &particle_emitter_component::get_turbulence_strength>("turbulence_strength"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "turbulence_strength"},
            entt::attribute{"pretty_name", "Strength (m)"},
            entt::attribute{"tooltip", "How far a swirling noise field pushes the particles around their path: smoke that billows, embers that wander. Divergence-free, so particles swirl instead of clumping. 0 = off."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Turbulence"},
        })
        .data<&particle_emitter_component::set_turbulence_frequency, &particle_emitter_component::get_turbulence_frequency>("turbulence_frequency"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "turbulence_frequency"},
            entt::attribute{"pretty_name", "Frequency (/m)"},
            entt::attribute{"tooltip", "Swirls per metre: higher values give smaller, busier swirls."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Turbulence"},
            entt::attribute{"predicate", has_turbulence},
        })
        .data<&particle_emitter_component::set_turbulence_speed, &particle_emitter_component::get_turbulence_speed>("turbulence_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "turbulence_speed"},
            entt::attribute{"pretty_name", "Speed (/s)"},
            entt::attribute{"tooltip", "How fast the field changes over a particle's life, in swirls per second: 0 freezes it, so each particle keeps the offset of where it was born."},
            entt::attribute{"group", "Turbulence"},
            entt::attribute{"predicate", has_turbulence},
        })
        .data<&particle_emitter_component::set_turbulence_over_life, &particle_emitter_component::get_turbulence_over_life>("turbulence_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "turbulence_over_life"},
            entt::attribute{"pretty_name", "Strength Over Life"},
            entt::attribute{"tooltip", "Multiplier of the strength over the particle's life: calm at birth and churning as it rises, or the other way."},
            entt::attribute{"group", "Turbulence"},
            entt::attribute{"predicate", has_turbulence},
        })
        .data<&particle_emitter_component::set_orbit_speed, &particle_emitter_component::get_orbit_speed>("orbit_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "orbit_speed"},
            entt::attribute{"pretty_name", "Orbit Speed (deg/s)"},
            entt::attribute{"tooltip", "Turns the particles about the force axis by this many degrees per second: vortices, tornadoes, swirls around a pickup. The sign picks the way round, and 0 turns it off."},
            entt::attribute{"group", "Forces"},
        })
        .data<&particle_emitter_component::set_orbit_over_life, &particle_emitter_component::get_orbit_over_life>("orbit_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "orbit_over_life"},
            entt::attribute{"pretty_name", "Orbit Over Life"},
            entt::attribute{"tooltip", "Multiplier of the orbit speed over the particle's life: slow at birth and fast at the end, or the other way."},
            entt::attribute{"group", "Forces"},
            entt::attribute{"predicate", has_orbit},
        })
        .data<&particle_emitter_component::set_radial_speed, &particle_emitter_component::get_radial_speed>("radial_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radial_speed"},
            entt::attribute{"pretty_name", "Radial Speed (m/s)"},
            entt::attribute{"tooltip", "Moves each particle straight away from the force axis by this many metres per second, the way it lay from the axis at birth, or toward the axis when negative, where it stops. 0 turns it off."},
            entt::attribute{"group", "Forces"},
        })
        .data<&particle_emitter_component::set_radial_over_life, &particle_emitter_component::get_radial_over_life>("radial_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radial_over_life"},
            entt::attribute{"pretty_name", "Radial Over Life"},
            entt::attribute{"tooltip", "Multiplier of the radial speed over the particle's life."},
            entt::attribute{"group", "Forces"},
            entt::attribute{"predicate", has_radial},
        })
        .data<&particle_emitter_component::set_force_axis, &particle_emitter_component::get_force_axis>("force_axis"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "force_axis"},
            entt::attribute{"pretty_name", "Axis"},
            entt::attribute{"tooltip", "The line Orbit and Radial act about, as a direction in the emitter's space. It passes through the emitter's origin. Only the direction counts, not the length, and a zero axis is up. In World simulation space the live particles follow the axis when the emitter moves, so a vortex that travels reads best in Local."},
            entt::attribute{"group", "Forces"},
            entt::attribute{"predicate", has_axis_force},
        })
        .data<&particle_emitter_component::set_converge_to_point, &particle_emitter_component::get_converge_to_point>("converge_to_point"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "converge_to_point"},
            entt::attribute{"pretty_name", "Converge To Point"},
            entt::attribute{"tooltip", "The particles leave their own paths and go to a target over their life: pickups flying to the player, energy gathering before a blast."},
            entt::attribute{"group", "Forces"},
        })
        .data<&particle_emitter_component::set_converge_over_life, &particle_emitter_component::get_converge_over_life>("converge_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "converge_over_life"},
            entt::attribute{"pretty_name", "Converge Over Life"},
            entt::attribute{"tooltip", "How far a particle has gone to the target over its life: 0 is on its own path and 1 is at the target."},
            entt::attribute{"group", "Forces"},
            entt::attribute{"predicate", has_converge},
        })
        .data<&particle_emitter_component::set_converge_target, &particle_emitter_component::get_converge_target>("converge_target"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "converge_target"},
            entt::attribute{"pretty_name", "Target"},
            entt::attribute{"tooltip", "The entity the particles go to. Without one they go to the emitter itself. Once the entity is destroyed they go on to where it was last."},
            entt::attribute{"group", "Forces"},
            entt::attribute{"predicate", has_converge},
        })
        .data<&particle_emitter_component::set_converge_offset, &particle_emitter_component::get_converge_offset>("converge_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "converge_offset"},
            entt::attribute{"pretty_name", "Target Offset"},
            entt::attribute{"tooltip", "The point the particles go to, in the target's space, or in the emitter's without a target."},
            entt::attribute{"group", "Forces"},
            entt::attribute{"predicate", has_converge},
        })
        .data<&particle_emitter_component::set_collision, &particle_emitter_component::get_collision>("collision"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision"},
            entt::attribute{"pretty_name", "Mode"},
            entt::attribute{"tooltip", "What a particle does where its path first touches a surface: it dies there (Kill), bounces off (Bounce) or stays there (Stick). With planes and physics colliders the contact is exact at any frame rate, solved ahead of the particle. Particles collide in World simulation space only."},
            entt::attribute{"group", "Collision"},
        })
        .data<&particle_emitter_component::set_collide_with, &particle_emitter_component::get_collide_with>("collide_with"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collide_with"},
            entt::attribute{"pretty_name", "Collide With"},
            entt::attribute{"tooltip", "What the particles can touch. Planes: the planes listed here, in the editor and in the game. Physics Colliders: those planes and the colliders of the physics world, which exists only while the game plays. Scene Surfaces: the opaque surfaces a camera draws, found on the GPU by each camera that draws the particles, once a frame and at no cost to the CPU, in the editor and in the game. The game is told of no such contact: it sends no event, leaves no decal and fires no sub-emitter, and deaths, particle lights and the particle count follow the path a particle would take without it: a particle a surface ended keeps its place in Max Particles until its lifetime is over. The particles of an emitter that no camera draws touch nothing."},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_collision},
        })
        .data<&particle_emitter_component::set_collision_layers, &particle_emitter_component::get_collision_layers>("collision_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_layers"},
            entt::attribute{"pretty_name", "Layers"},
            entt::attribute{"tooltip", "The layers whose colliders the particles touch."},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_collider_collision},
        })
        .data<&particle_emitter_component::set_collision_quality, &particle_emitter_component::get_collision_quality>("collision_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_quality"},
            entt::attribute{"pretty_name", "Quality"},
            entt::attribute{"tooltip", "How closely a particle's path is followed against the colliders. A higher quality notices a collider that moves sooner and follows a curved path more closely, with more casts for each particle."},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_collider_collision},
        })
        .data<&particle_emitter_component::set_collision_planes, &particle_emitter_component::get_collision_planes>("collision_planes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_planes"},
            entt::attribute{"pretty_name", "Planes"},
            entt::attribute{"tooltip", "Planes the particles collide with: a world normal (x, y, z) and an offset (w) from the emitter's origin along it. Particles live on the normal's side. (0, 1, 0, 0) is the ground at the emitter's height."},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_known_contacts},
        })
        .data<&particle_emitter_component::set_collision_radius_scale, &particle_emitter_component::get_collision_radius_scale>("collision_radius_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_radius_scale"},
            entt::attribute{"pretty_name", "Radius Scale"},
            entt::attribute{"tooltip", "How far from a surface a particle touches it, in halves of its size: 1 = its edge (sprites and meshes rest on the floor), 0 = its centre."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_collision},
        })
        .data<&particle_emitter_component::set_restitution, &particle_emitter_component::get_restitution>("restitution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "restitution"},
            entt::attribute{"pretty_name", "Restitution"},
            entt::attribute{"tooltip", "Share of its speed into the surface a particle keeps when it bounces: 0 = it slides along, 1 = a perfect bounce."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_bounce},
        })
        .data<&particle_emitter_component::set_friction, &particle_emitter_component::get_friction>("friction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "friction"},
            entt::attribute{"pretty_name", "Friction"},
            entt::attribute{"tooltip", "Share of its speed along the surface a particle loses when it bounces."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_bounce},
        })
        .data<&particle_emitter_component::set_max_bounces, &particle_emitter_component::get_max_bounces>("max_bounces"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_bounces"},
            entt::attribute{"pretty_name", "Max Bounces"},
            entt::attribute{"tooltip", "Bounces before the particle dies at its next contact instead of sinking through. Particles that collide with Scene Surfaces count 63 bounces at most."},
            entt::attribute{"min", 0},
            entt::attribute{"max", 255},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_bounce},
        })
        .data<&particle_emitter_component::set_bounce_scatter, &particle_emitter_component::get_bounce_scatter>("bounce_scatter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounce_scatter"},
            entt::attribute{"pretty_name", "Bounce Scatter"},
            entt::attribute{"tooltip", "Half angle, in degrees, of the cone each bounce's direction is turned within at random, as a rough surface does."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 90.0f},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_bounce},
        })
        .data<&particle_emitter_component::set_rest_speed, &particle_emitter_component::get_rest_speed>("rest_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rest_speed"},
            entt::attribute{"pretty_name", "Rest Speed"},
            entt::attribute{"tooltip", "A particle that leaves a bounce slower than this, in metres per second, comes to rest on the surface for the rest of its life. 0 never rests: the particle bounces until Max Bounces."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_bounce},
        })
        .data<&particle_emitter_component::set_lifetime_loss, &particle_emitter_component::get_lifetime_loss>("lifetime_loss"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime_loss"},
            entt::attribute{"pretty_name", "Lifetime Loss"},
            entt::attribute{"tooltip", "Share of the life a particle has left that each contact takes away. 1 ends the particle at its first contact."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_lasting_contact},
        })
        .data<&particle_emitter_component::set_align_to_surface, &particle_emitter_component::get_align_to_surface>("align_to_surface"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "align_to_surface"},
            entt::attribute{"pretty_name", "Align To Surface"},
            entt::attribute{"tooltip", "Particles at rest lie flat on the surface they rest on, and meshes stand upright on it. A flat particle lies inside its Soft Fade Distance, so set that to 0 with this."},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_resting_particles},
        })
        .data<&particle_emitter_component::set_collision_decal, &particle_emitter_component::get_collision_decal>("collision_decal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_decal"},
            entt::attribute{"pretty_name", "Decal"},
            entt::attribute{"tooltip", "A decal each contact leaves on the surface it touched, while the game plays."},
            entt::attribute{"group", "Collision"},
            entt::attribute{"predicate", has_known_contacts},
        })
        .data<&particle_emitter_component::set_sub_emitters, &particle_emitter_component::get_sub_emitters>("sub_emitters"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sub_emitters"},
            entt::attribute{"pretty_name", "Sub Emitters"},
            entt::attribute{"tooltip", "Bursts other emitters fire where this emitter's particles are born, die or touch a surface: sparks where embers land, smoke where rockets die. A burst starts at the particle, optionally with its velocity, color and size."},
            entt::attribute{"group", "Sub Emitters"},
        })
        .data<&particle_emitter_component::set_send_birth_events, &particle_emitter_component::get_send_birth_events>("send_birth_events"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "send_birth_events"},
            entt::attribute{"pretty_name", "Send Birth Events"},
            entt::attribute{"tooltip", "The scripts on this entity, or on its nearest parent with scripts, are told of each particle that is born, once a frame in OnParticleEvents."},
            entt::attribute{"group", "Script Events"},
        })
        .data<&particle_emitter_component::set_send_death_events, &particle_emitter_component::get_send_death_events>("send_death_events"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "send_death_events"},
            entt::attribute{"pretty_name", "Send Death Events"},
            entt::attribute{"tooltip", "The scripts on this entity, or on its nearest parent with scripts, are told of each particle that dies, once a frame in OnParticleEvents."},
            entt::attribute{"group", "Script Events"},
        })
        .data<&particle_emitter_component::set_send_collision_events, &particle_emitter_component::get_send_collision_events>("send_collision_events"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "send_collision_events"},
            entt::attribute{"pretty_name", "Send Collision Events"},
            entt::attribute{"tooltip", "The scripts on this entity, or on its nearest parent with scripts, are told of each particle that touches a surface, once a frame in OnParticleEvents: where, how fast, the surface's normal and the entity it belongs to."},
            entt::attribute{"group", "Script Events"},
            entt::attribute{"predicate", has_known_contacts},
        })
        .data<&particle_emitter_component::set_notify_hit_entities, &particle_emitter_component::get_notify_hit_entities>("notify_hit_entities"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "notify_hit_entities"},
            entt::attribute{"pretty_name", "Notify Hit Entities"},
            entt::attribute{"tooltip", "The scripts on an entity whose collider a particle touches are told of it, once a frame in OnParticleHits: damage from sparks, flames or acid."},
            entt::attribute{"group", "Script Events"},
            entt::attribute{"predicate", has_collider_collision},
        })
        .data<&particle_emitter_component::set_size_over_life, &particle_emitter_component::get_size_over_life>("size_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_over_life"},
            entt::attribute{"pretty_name", "Size Over Life (m)"},
            entt::attribute{"tooltip", "Particle size over its life, random between the min and max curves: each particle keeps its place between them. Every key counts, so a size can pop in, or grow then shrink."},
            entt::attribute{"group", "Size"},
            entt::attribute{"former_name", "scale_gradient"},
        })
        .data<&particle_emitter_component::set_size_axes, &particle_emitter_component::get_size_axes>("size_axes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_axes"},
            entt::attribute{"pretty_name", "Size Axes"},
            entt::attribute{"tooltip", "Multipliers of the size along the particle's own axes: (2, 1, 1) is twice as wide, (1, 3, 1) a streak. Z is the depth of mesh particles."},
            entt::attribute{"group", "Size"},
            entt::attribute{"former_name", "initial_scale_3d"},
        })
        .data<&particle_emitter_component::set_size_axes_over_life, &particle_emitter_component::get_size_axes_over_life>("size_axes_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_axes_over_life"},
            entt::attribute{"pretty_name", "Size Axes Over Life"},
            entt::attribute{"tooltip", "Multipliers of the size axes over the particle's life: squash and stretch (raindrops, shockwaves, flattening smoke). Z applies to mesh particles. 1 throughout = constant."},
            entt::attribute{"group", "Size"},
        })
        .data<&particle_emitter_component::set_size_by_speed_multiplier, &particle_emitter_component::get_size_by_speed_multiplier>("size_by_speed_multiplier"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_by_speed_multiplier"},
            entt::attribute{"pretty_name", "Size By Speed"},
            entt::attribute{"tooltip", "Size multiplier at the slow end (min) and the fast end (max) of Speed Range: sparks that shrink as they slow. Min = max turns it off."},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"group", "Size"},
            entt::attribute{"former_name", "size_by_speed_range"},
        })
        .data<&particle_emitter_component::set_size_by_speed_speed_range, &particle_emitter_component::get_size_by_speed_speed_range>("size_by_speed_speed_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_by_speed_speed_range"},
            entt::attribute{"pretty_name", "Speed Range (m/s)"},
            entt::attribute{"tooltip", "Particle speeds Size By Speed maps over."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"group", "Size"},
            entt::attribute{"former_name", "size_by_speed_velocity_range"},
        })
        .data<&particle_emitter_component::set_color_gradient, &particle_emitter_component::get_color_gradient>("color_gradient"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color_gradient"},
            entt::attribute{"pretty_name", "Color Over Life"},
            entt::attribute{"tooltip", "The particle's color and opacity over its life."},
            entt::attribute{"group", "Color"},
        })
        .data<&particle_emitter_component::set_random_between_gradients, &particle_emitter_component::get_random_between_gradients>("random_between_gradients"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "random_between_gradients"},
            entt::attribute{"pretty_name", "Random Between Gradients"},
            entt::attribute{"tooltip", "Each particle takes its own random mix of Color Over Life and Color Over Life B: a fire's sparks that vary from yellow to red."},
            entt::attribute{"group", "Color"},
        })
        .data<&particle_emitter_component::set_color_gradient_b, &particle_emitter_component::get_color_gradient_b>("color_gradient_b"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color_gradient_b"},
            entt::attribute{"pretty_name", "Color Over Life B"},
            entt::attribute{"tooltip", "The second color over life. Each particle mixes the two by its own random amount."},
            entt::attribute{"group", "Color"},
            entt::attribute{"predicate", has_two_gradients},
        })
        .data<&particle_emitter_component::set_opacity, &particle_emitter_component::get_opacity>("opacity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "opacity"},
            entt::attribute{"pretty_name", "Opacity"},
            entt::attribute{"tooltip", "Opacity of every particle on top of the color: 0 = invisible, 1 = as the color gives."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Color"},
        })
        .data<&particle_emitter_component::set_color_intensity, &particle_emitter_component::get_color_intensity>("color_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color_intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"tooltip", "HDR multiplier of the color: above 1 the particles glow under bloom."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"group", "Color"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_intensity_over_life, &particle_emitter_component::get_intensity_over_life>("intensity_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "intensity_over_life"},
            entt::attribute{"pretty_name", "Intensity Over Life"},
            entt::attribute{"tooltip", "Multiplier of Intensity over the particle's life: embers that flare and cool, without touching the hues."},
            entt::attribute{"group", "Color"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_color_by_speed_gradient, &particle_emitter_component::get_color_by_speed_gradient>("color_by_speed_gradient"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color_by_speed_gradient"},
            entt::attribute{"pretty_name", "Color By Speed"},
            entt::attribute{"tooltip", "Color multiplier across Speed Range: the slow end takes its start, the fast end its end."},
            entt::attribute{"group", "Color"},
        })
        .data<&particle_emitter_component::set_color_by_speed_speed_range, &particle_emitter_component::get_color_by_speed_speed_range>("color_by_speed_speed_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color_by_speed_speed_range"},
            entt::attribute{"pretty_name", "Color Speed Range (m/s)"},
            entt::attribute{"tooltip", "Particle speeds Color By Speed maps over. Min = max turns it off."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Color"},
            entt::attribute{"former_name", "color_by_speed_velocity_range"},
        })
        .data<&particle_emitter_component::set_start_rotation, &particle_emitter_component::get_start_rotation>("start_rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "start_rotation"},
            entt::attribute{"pretty_name", "Start Rotation (deg)"},
            entt::attribute{"tooltip", "Angle of each particle when it is born, random between min and max: 0-360 keeps smoke and fire from repeating one orientation."},
            entt::attribute{"group", "Rotation"},
            entt::attribute{"former_name", "rotation"},
        })
        .data<&particle_emitter_component::set_spin_speed, &particle_emitter_component::get_spin_speed>("spin_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spin_speed"},
            entt::attribute{"pretty_name", "Spin Speed (deg/s)"},
            entt::attribute{"tooltip", "Spin, random between min and max."},
            entt::attribute{"group", "Rotation"},
            entt::attribute{"former_name", "rotation_speed"},
        })
        .data<&particle_emitter_component::set_spin_over_life, &particle_emitter_component::get_spin_over_life>("spin_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spin_over_life"},
            entt::attribute{"pretty_name", "Spin Over Life"},
            entt::attribute{"tooltip", "Multiplier of the spin over the particle's life: debris that spins fast and settles. The angle follows the curve's integral, so it never jumps."},
            entt::attribute{"group", "Rotation"},
        })
        .data<&particle_emitter_component::set_random_spin_direction, &particle_emitter_component::get_random_spin_direction>("random_spin_direction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "random_spin_direction"},
            entt::attribute{"pretty_name", "Random Spin Direction"},
            entt::attribute{"tooltip", "Half the particles spin the other way."},
            entt::attribute{"group", "Rotation"},
            entt::attribute{"former_name", "random_rotation_direction"},
        })
        .data<&particle_emitter_component::set_align_to_direction, &particle_emitter_component::get_align_to_direction>("align_to_direction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "align_to_direction"},
            entt::attribute{"pretty_name", "Align To Direction"},
            entt::attribute{"tooltip", "Turn each particle so the texture's top (a mesh's +Y) points along its motion: sparks, streaks, arrows. Start Rotation turns art drawn another way."},
            entt::attribute{"group", "Rotation"},
        })
        .data<&particle_emitter_component::set_render_material, &particle_emitter_component::get_render_material>("render_material"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "render_material"},
            entt::attribute{"pretty_name", "Material"},
            entt::attribute{"tooltip", "What the particles draw. Sprites, trails and ribbons draw a Surface material whose output has Particle Use on. The engine's own, under engine:/data/materials/particles, come one for each Blend Mode, for mask textures and for normal maps. Unset, they draw the plain one. In Mesh mode it is the material of every part of the mesh, which keeps its own for one of the engine's particle materials and when unset."},
            entt::attribute{"group", "Material"},
            entt::attribute{"material_domain", get_material_domain_key(material_domain::surface)},
            entt::attribute{"replaces", std::vector<std::string>{"blend_mode", "texture_mode", "normal_texture"}},
        })
        .data<&particle_emitter_component::set_material_instance,
              static_cast<const material_instance& (particle_emitter_component::*)() const>(&particle_emitter_component::get_material_instance)>(
            "material_overrides"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material_overrides"},
            entt::attribute{"pretty_name", "Material Overrides"},
            entt::attribute{"tooltip", "This emitter's own values of its material's parameters and textures, such as its Texture or a tint. A value it does not override follows the material."},
            entt::attribute{"group", "Material"},
            entt::attribute{"predicate", has_material},
            entt::attribute{"replaces", std::vector<std::string>{"texture", "normal_texture"}},
        })
        .data<&particle_emitter_component::set_custom_data_x, &particle_emitter_component::get_custom_data_x>("custom_data_x"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "custom_data_x"},
            entt::attribute{"pretty_name", "Custom Data X"},
            entt::attribute{"tooltip", "A value over each particle's life that the material reads through its Particle Custom Data node and gives a meaning, such as a dissolve amount or a glow."},
            entt::attribute{"group", "Material"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_custom_data_y, &particle_emitter_component::get_custom_data_y>("custom_data_y"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "custom_data_y"},
            entt::attribute{"pretty_name", "Custom Data Y"},
            entt::attribute{"tooltip", "A second value over each particle's life for the material's Particle Custom Data node."},
            entt::attribute{"group", "Material"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_texture_sheet_tiles, &particle_emitter_component::get_texture_sheet_tiles>("texture_sheet_tiles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_sheet_tiles"},
            entt::attribute{"pretty_name", "Sheet Tiles"},
            entt::attribute{"tooltip", "Columns (x) and rows (y) of a flipbook: a 4 x 4 sheet holds 16 frames. 1 x 1 = a single image."},
            entt::attribute{"min", 1.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"group", "Texture"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_texture_sheet_cycles, &particle_emitter_component::get_texture_sheet_cycles>("texture_sheet_cycles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_sheet_cycles"},
            entt::attribute{"pretty_name", "Sheet Cycles"},
            entt::attribute{"tooltip", "Times the sheet plays over a particle's life: 0 holds one frame (the first, or a random one), 1 plays it once and ends on its last frame."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"group", "Texture"},
            entt::attribute{"predicate", is_sheet_by_cycles},
        })
        .data<&particle_emitter_component::set_texture_sheet_randomize, &particle_emitter_component::get_texture_sheet_randomize>("texture_sheet_randomize"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_sheet_randomize"},
            entt::attribute{"pretty_name", "Random Start Frame"},
            entt::attribute{"tooltip", "Each particle starts the sheet at a random frame."},
            entt::attribute{"group", "Texture"},
            entt::attribute{"predicate", is_sheet},
        })
        .data<&particle_emitter_component::set_texture_sheet_random_row, &particle_emitter_component::get_texture_sheet_random_row>("texture_sheet_random_row"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_sheet_random_row"},
            entt::attribute{"pretty_name", "Random Row"},
            entt::attribute{"tooltip", "Each particle plays one random row, its frames running along the row: several animations in one texture."},
            entt::attribute{"group", "Texture"},
            entt::attribute{"predicate", is_sheet},
        })
        .data<&particle_emitter_component::set_texture_sheet_use_frame_curve, &particle_emitter_component::get_texture_sheet_use_frame_curve>("texture_sheet_use_frame_curve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_sheet_use_frame_curve"},
            entt::attribute{"pretty_name", "Use Frame Curve"},
            entt::attribute{"tooltip", "The frame follows Frame Curve over the particle's life instead of playing Sheet Cycles times: hold a frame, speed up, play backwards."},
            entt::attribute{"group", "Texture"},
            entt::attribute{"predicate", is_sheet},
        })
        .data<&particle_emitter_component::set_texture_sheet_frame_curve, &particle_emitter_component::get_texture_sheet_frame_curve>("texture_sheet_frame_curve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_sheet_frame_curve"},
            entt::attribute{"pretty_name", "Frame Curve"},
            entt::attribute{"tooltip", "Sheet frame over the particle's life: 0 = first frame, 1 = last."},
            entt::attribute{"group", "Texture"},
            entt::attribute{"predicate", is_sheet_by_curve},
        })
        .data<&particle_emitter_component::set_frame_blending, &particle_emitter_component::get_frame_blending>("frame_blending"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "frame_blending"},
            entt::attribute{"pretty_name", "Frame Blending"},
            entt::attribute{"tooltip", "Crossfade each frame into the next instead of stepping: smooths slow flipbooks."},
            entt::attribute{"group", "Texture"},
            entt::attribute{"predicate", is_sheet},
        })
        .data<&particle_emitter_component::set_erosion_over_life, &particle_emitter_component::get_erosion_over_life>("erosion_over_life"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "erosion_over_life"},
            entt::attribute{"pretty_name", "Erosion Over Life"},
            entt::attribute{"tooltip", "Alpha threshold over life (0..1): texels whose alpha is below it vanish, so smoke and fire dissolve along their shapes instead of fading flat. 0 throughout = off."},
            entt::attribute{"group", "Texture"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_render_mode, &particle_emitter_component::get_render_mode>("render_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "render_mode"},
            entt::attribute{"pretty_name", "Render Mode"},
            entt::attribute{"tooltip", "How each particle draws. Billboard: parallel to the view. Horizontal / Vertical Billboard: flat on the ground / upright, turning about the vertical. Camera Facing: turned toward the camera. Stretched: along its motion. Trail: a strip along its own recent path. Ribbon: one strip through all the emitter's particles. Mesh: a model with its material, lit and shadowed with the scene. Fixed Axis: upright along an axis of the emitter."},
            entt::attribute{"group", "Renderer"},
        })
        .data<&particle_emitter_component::set_trail_length, &particle_emitter_component::get_trail_length>("trail_length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "trail_length"},
            entt::attribute{"pretty_name", "Trail Length (s)"},
            entt::attribute{"tooltip", "Seconds of its own path each particle trails: its exact past positions, so the trail curves with gravity, drag and turbulence."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_trail},
        })
        .data<&particle_emitter_component::set_trail_width_end, &particle_emitter_component::get_trail_width_end>("trail_width_end"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "trail_width_end"},
            entt::attribute{"pretty_name", "Trail Width End"},
            entt::attribute{"tooltip", "The trail's width at its tail as a share of the particle's size (0 = a point). It fades out toward the tail too."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_trail},
        })
        .data<&particle_emitter_component::set_ribbon_texture_mode, &particle_emitter_component::get_ribbon_texture_mode>("ribbon_texture_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ribbon_texture_mode"},
            entt::attribute{"pretty_name", "Ribbon Texture"},
            entt::attribute{"tooltip", "How the texture runs along the ribbon (across it is the texture's width). Age: from where particles are born to where they die, flowing with them. Stretch: once over the whole ribbon. Tile: repeated every Ribbon Tile Time seconds of age."},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_ribbon},
        })
        .data<&particle_emitter_component::set_ribbon_tile_time, &particle_emitter_component::get_ribbon_tile_time>("ribbon_tile_time"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ribbon_tile_time"},
            entt::attribute{"pretty_name", "Ribbon Tile Time (s)"},
            entt::attribute{"tooltip", "Seconds of the particles' age one repeat of the texture spans."},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_ribbon_tile},
        })
        .data<&particle_emitter_component::set_ribbon_facing, &particle_emitter_component::get_ribbon_facing>("ribbon_facing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ribbon_facing"},
            entt::attribute{"pretty_name", "Ribbon Facing"},
            entt::attribute{"tooltip", "Which way the ribbon's width (each particle's size) runs. Camera: across the ribbon, facing the camera. Launch Direction: along each particle's launch direction as it was at birth - launch slowly along a blade and the ribbon sweeps its arc as one sheet, hilt to tip."},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_ribbon},
        })
        .data<&particle_emitter_component::set_ribbon_count, &particle_emitter_component::get_ribbon_count>("ribbon_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ribbon_count"},
            entt::attribute{"pretty_name", "Ribbons"},
            entt::attribute{"min", uint32_t(1)},
            entt::attribute{"max", ps_soa::k_max_ribbon_count},
            entt::attribute{"tooltip", "How many ribbons the particles form. Each particle joins the next ribbon in turn as it is born. Several ribbons are a stream each: its particles are born at one place of the shape and launched one way at one speed, so a moving emitter leaves that many strands side by side, and a still one that many jets."},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_ribbon},
        })
        .data<&particle_emitter_component::set_ribbon_smoothing, &particle_emitter_component::get_ribbon_smoothing>("ribbon_smoothing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ribbon_smoothing"},
            entt::attribute{"pretty_name", "Ribbon Smoothing"},
            entt::attribute{"tooltip", "How finely the ribbon bends between two particles, along a smooth curve through the particles around them. None: straight from particle to particle. Low, Medium and High draw each stretch in 2, 4 and 8 pieces, and cost as many times the vertices."},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_ribbon},
        })
        .data<&particle_emitter_component::set_stretch_length_scale, &particle_emitter_component::get_stretch_length_scale>("stretch_length_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stretch_length_scale"},
            entt::attribute{"pretty_name", "Stretch Length"},
            entt::attribute{"tooltip", "Quad length as a multiple of the particle size (1 = square at rest)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_stretched},
        })
        .data<&particle_emitter_component::set_stretch_speed_scale, &particle_emitter_component::get_stretch_speed_scale>("stretch_speed_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stretch_speed_scale"},
            entt::attribute{"pretty_name", "Stretch By Speed (s)"},
            entt::attribute{"tooltip", "Length added per metre per second of speed, so fast particles streak."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_stretched},
        })
        .data<&particle_emitter_component::set_stretch_camera_velocity_scale, &particle_emitter_component::get_stretch_camera_velocity_scale>("stretch_camera_velocity_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stretch_camera_velocity_scale"},
            entt::attribute{"pretty_name", "Stretch By Camera"},
            entt::attribute{"tooltip", "Share of the camera's velocity taken off the particles' before they stretch: 1 streaks rain and snow when the camera moves through them, 0 = their own motion only."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_stretched},
        })
        .data<&particle_emitter_component::set_facing_axis, &particle_emitter_component::get_facing_axis>("facing_axis"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "facing_axis"},
            entt::attribute{"pretty_name", "Facing Axis"},
            entt::attribute{"tooltip", "The axis each quad stands along, in the emitter's space. The quad turns about it toward the camera. (0, 1, 0) is the Vertical Billboard."},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_fixed_axis},
        })
        .data<&particle_emitter_component::set_render_mesh, &particle_emitter_component::get_render_mesh>("render_mesh"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "render_mesh"},
            entt::attribute{"pretty_name", "Mesh"},
            entt::attribute{"tooltip", "The mesh each particle draws, at its size (1 = the mesh's own size) times Size Axes, turned about an axis of its own (or +Y along the motion with Align To Direction)."},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_mesh_mode},
        })
        .data<&particle_emitter_component::set_mesh_cast_shadows, &particle_emitter_component::get_mesh_cast_shadows>("mesh_cast_shadows"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mesh_cast_shadows"},
            entt::attribute{"pretty_name", "Cast Shadows"},
            entt::attribute{"tooltip", "The meshes cast shadows (when their material does)."},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_mesh_mode},
        })
        .data<&particle_emitter_component::set_pivot, &particle_emitter_component::get_pivot>("pivot"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pivot"},
            entt::attribute{"pretty_name", "Pivot"},
            entt::attribute{"tooltip", "The point of the quad that sits at the particle's position and that it turns about: (0, 0) bottom left, (0.5, 0.5) centre, (1, 1) top right."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_soft_fade_distance, &particle_emitter_component::get_soft_fade_distance>("soft_fade_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "soft_fade_distance"},
            entt::attribute{"pretty_name", "Soft Fade Distance (m)"},
            entt::attribute{"tooltip", "Distance over which a particle fades out where it meets the scene behind it, so smoke and glows do not cut hard lines into the ground. 0 = hard edge."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_camera_offset, &particle_emitter_component::get_camera_offset>("camera_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera_offset"},
            entt::attribute{"pretty_name", "Camera Offset (m)"},
            entt::attribute{"tooltip", "Distance each particle is pushed toward the camera: keeps large sprites from cutting into the walls and floor they stand against."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Renderer"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_lighting, &particle_emitter_component::get_lighting>("lighting"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lighting"},
            entt::attribute{"pretty_name", "Lighting"},
            entt::attribute{"tooltip", "How the material's Base Color shows. Unlit: it is the light the particle gives off (fire, magic, sparks). Volumetric: lit once per particle like a small cloud by the sky, the sun (with its shadow) and up to 4 nearby lights, the same from every side (smoke, dust, steam). Surface: lit like a small sphere facing the camera, the sun per pixel, by the material's Normal where it gives one (debris, snow, leaves), with the sun's highlight where the material sets its Roughness or Metallic (droplets, ice, metal shavings). A material whose Shading Model is Unlit, as one that filters the scene, has no such choice."},
            entt::attribute{"group", "Lighting"},
            entt::attribute{"predicate", is_lit_material},
        })
        .data<&particle_emitter_component::set_lighting_wrap, &particle_emitter_component::get_lighting_wrap>("lighting_wrap"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lighting_wrap"},
            entt::attribute{"pretty_name", "Wrap"},
            entt::attribute{"tooltip", "How far the sun's light wraps past the lit half of each particle: 0 = a hard terminator, 1 = lit all round (thin, translucent particles)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"group", "Lighting"},
            entt::attribute{"predicate", is_surface_lit},
            entt::attribute{"former_name", "light_wrap"},
        })
        .data<&particle_emitter_component::set_ambient_light, &particle_emitter_component::get_ambient_light>("ambient_light"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ambient_light"},
            entt::attribute{"pretty_name", "Ambient Light"},
            entt::attribute{"tooltip", "Where the particles take the light around them from. Sky: the sky's light, the same wherever a particle is. Global Illumination: the scene's bounced light where the particle is, so smoke in a dark room is dark and takes the colour of what is lit beside it. That needs a view whose Global Illumination runs and a material with at most two textures that reads no Scene Color. The emitters of a view that are lit this way share the probes for a box of particles about eight metres a side near the camera, and for a larger one farther away. The nearest come first, and an emitter that does not fit keeps the sky's light, as the particles do wherever one of these is missing. While such an emitter is in view the Global Illumination keeps probes around its particles."},
            entt::attribute{"group", "Lighting"},
            entt::attribute{"predicate", is_lit},
        })
        .data<&particle_emitter_component::set_receive_shadows, &particle_emitter_component::get_receive_shadows>("receive_shadows"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "receive_shadows"},
            entt::attribute{"pretty_name", "Receive Shadows"},
            entt::attribute{"tooltip", "Darken where the sun's shadow falls on the particle (read once per particle)."},
            entt::attribute{"group", "Lighting"},
            entt::attribute{"predicate", is_lit},
        })
        .data<&particle_emitter_component::set_light_count, &particle_emitter_component::get_light_count>("light_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light_count"},
            entt::attribute{"pretty_name", "Count"},
            entt::attribute{"tooltip", "How many particles light the scene as point lights: the brightest (color luminance x alpha) each frame, without shadows. A view draws at most 64 particle lights. 0 = off."},
            entt::attribute{"group", "Particle Lights"},
        })
        .data<&particle_emitter_component::set_light_intensity, &particle_emitter_component::get_light_intensity>("light_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light_intensity"},
            entt::attribute{"pretty_name", "Intensity (cd)"},
            entt::attribute{"tooltip", "A light's intensity when its particle is at full brightness. It follows the particle's luminance x alpha, so fading particles dim their light."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Particle Lights"},
            entt::attribute{"predicate", has_particle_lights},
        })
        .data<&particle_emitter_component::set_light_range, &particle_emitter_component::get_light_range>("light_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light_range"},
            entt::attribute{"pretty_name", "Range (m)"},
            entt::attribute{"tooltip", "How far a particle light reaches. Its falloff ends smoothly there."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Particle Lights"},
            entt::attribute{"predicate", has_particle_lights},
        })
        .data<&particle_emitter_component::set_light_color_from_particle, &particle_emitter_component::get_light_color_from_particle>("light_color_from_particle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light_color_from_particle"},
            entt::attribute{"pretty_name", "Color From Particle"},
            entt::attribute{"tooltip", "The lights take their particle's color. Off, they are white."},
            entt::attribute{"group", "Particle Lights"},
            entt::attribute{"predicate", has_particle_lights},
        })
        .data<&particle_emitter_component::set_sort_mode, &particle_emitter_component::get_sort_mode>("sort_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sort_mode"},
            entt::attribute{"pretty_name", "Sort Mode"},
            entt::attribute{"tooltip", "Order of this emitter's own particles. Auto: back to front for Normal and Premultiplied blend, unsorted for Additive and Multiply (their result does not depend on order). None: a stable order. View Distance / View Depth: back to front. Oldest First: the youngest on top (trails, smoke). Youngest First: the oldest on top."},
            entt::attribute{"group", "Sorting"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_sort_priority, &particle_emitter_component::get_sort_priority>("sort_priority"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sort_priority"},
            entt::attribute{"pretty_name", "Sort Priority"},
            entt::attribute{"tooltip", "Draw order against other emitters: lower values draw first, higher ones on top. Equal priorities draw back to front by distance. Keeps a ground decal under the fire standing on it."},
            entt::attribute{"group", "Sorting"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_sort_backend, &particle_emitter_component::get_sort_backend>("sort_backend"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sort_backend"},
            entt::attribute{"pretty_name", "Sort Backend"},
            entt::attribute{"tooltip", "Where the particles are culled and sorted for each view. GPU: one compute pass for every emitter drawn. CPU: on the main thread. They look the same either way. An emitter that collides with Scene Surfaces is always culled and sorted on the GPU, which alone knows where its particles went."},
            entt::attribute{"group", "Sorting"},
            entt::attribute{"predicate", is_sprite_mode},
            entt::attribute{"former_name", "simulation_backend"},
        })
        .data<&particle_emitter_component::set_culling_mode, &particle_emitter_component::get_culling_mode>("culling_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "culling_mode"},
            entt::attribute{"pretty_name", "Culling Mode"},
            entt::attribute{"tooltip", "Always Simulate: update even while no camera draws the emitter. Renderer Based: freeze while unseen, then catch up on the time it missed, so it wakes as if it had kept running. Pause: freeze while unseen and resume where it froze."},
            entt::attribute{"group", "Visibility"},
        })
        .data<&particle_emitter_component::set_max_draw_distance, &particle_emitter_component::get_max_draw_distance>("max_draw_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_draw_distance"},
            entt::attribute{"pretty_name", "Max Draw Distance (m)"},
            entt::attribute{"tooltip", "Distance from the camera beyond which the emitter is not drawn (with Renderer Based culling it also stops simulating). 0 = no limit."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Visibility"},
        })
        .data<&particle_emitter_component::set_draw_distance_fade, &particle_emitter_component::get_draw_distance_fade>("draw_distance_fade"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "draw_distance_fade"},
            entt::attribute{"pretty_name", "Draw Distance Fade (m)"},
            entt::attribute{"tooltip", "Distance before Max Draw Distance over which particles fade out, so the cut never pops."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Visibility"},
            entt::attribute{"predicate", has_draw_distance},
        })
        .data<&particle_emitter_component::set_camera_fade_range, &particle_emitter_component::get_camera_fade_range>("camera_fade_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera_fade_range"},
            entt::attribute{"pretty_name", "Camera Fade Range (m)"},
            entt::attribute{"tooltip", "Distances from the camera over which particles fade in: invisible at min, whole at max, so smoke does not fill the screen when the camera walks into it. 0, 0 = off."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"group", "Visibility"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_max_screen_size, &particle_emitter_component::get_max_screen_size>("max_screen_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_screen_size"},
            entt::attribute{"pretty_name", "Max Screen Size"},
            entt::attribute{"tooltip", "Largest a particle grows on screen, as a share of the view's height: caps the fill cost of particles next to the camera. 0 = no limit."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Visibility"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_subpixel_fade, &particle_emitter_component::get_subpixel_fade>("subpixel_fade"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "subpixel_fade"},
            entt::attribute{"pretty_name", "Subpixel Fade"},
            entt::attribute{"tooltip", "Particles smaller than a pixel keep one pixel and fade by the area they lost, instead of flickering between frames."},
            entt::attribute{"group", "Visibility"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_bounds_mode, &particle_emitter_component::get_bounds_mode>("bounds_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounds_mode"},
            entt::attribute{"pretty_name", "Bounds Mode"},
            entt::attribute{"tooltip", "Automatic: the culling box follows from the settings every frame (where particles are born, how far they can travel, how large they grow). Fixed: the Fixed Bounds box."},
            entt::attribute{"group", "Visibility"},
        })
        .data<&particle_emitter_component::set_fixed_bounds, &particle_emitter_component::get_fixed_bounds>("fixed_bounds"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fixed_bounds"},
            entt::attribute{"pretty_name", "Fixed Bounds"},
            entt::attribute{"tooltip", "Culling box in the emitter's local space."},
            entt::attribute{"group", "Visibility"},
            entt::attribute{"predicate", has_fixed_bounds},
        })
        .data<&particle_emitter_component::set_primitive, &particle_emitter_component::get_primitive>("primitive"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "primitive"},
            entt::attribute{"pretty_name", "Primitive"},
            entt::attribute{"tooltip", "Geometry each particle draws. Quad: the whole texture. Octagon: the octagon around the texture's inscribed circle, 17% fewer pixels for round textures. The corners are cut off. Cutout: an 8-sided polygon around each frame's texels above Cutout Alpha Threshold, baked from the texture (the whole quad until the bake is in)."},
            entt::attribute{"group", "Performance"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_cutout_alpha_threshold, &particle_emitter_component::get_cutout_alpha_threshold>("cutout_alpha_threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cutout_alpha_threshold"},
            entt::attribute{"pretty_name", "Cutout Alpha Threshold"},
            entt::attribute{"tooltip", "Texels whose alpha is above this stay inside each frame's polygon: higher trims faint glows for less overdraw."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Performance"},
            entt::attribute{"predicate", is_cutout},
        })
        .data<&particle_emitter_component::set_half_resolution, &particle_emitter_component::get_half_resolution>("half_resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "half_resolution"},
            entt::attribute{"pretty_name", "Half Resolution"},
            entt::attribute{"tooltip", "Draw at half the view's resolution and blend over the scene: a quarter of the fill cost for large, soft effects (smoke, fog, dust). They draw over the full-resolution particles and fine detail softens. Multiply blend always draws at full resolution."},
            entt::attribute{"group", "Performance"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_taa_response, &particle_emitter_component::get_taa_response>("taa_response"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "taa_response"},
            entt::attribute{"pretty_name", "TAA Response"},
            entt::attribute{"tooltip", "How strongly the temporal anti-aliasing favours the current frame where the particles cover the screen: 1 keeps fast sparks from trailing, 0 keeps slow smoke smoothest."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Performance"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_write_velocity, &particle_emitter_component::get_write_velocity>("write_velocity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "write_velocity"},
            entt::attribute{"pretty_name", "Write Velocity"},
            entt::attribute{"tooltip", "Write motion vectors where the particles are at least Velocity Alpha Threshold opaque, so the temporal anti-aliasing reprojects them like moving objects: crisp debris and sparks. Costs one more draw."},
            entt::attribute{"group", "Performance"},
            entt::attribute{"predicate", is_sprite_mode},
        })
        .data<&particle_emitter_component::set_velocity_alpha_threshold, &particle_emitter_component::get_velocity_alpha_threshold>("velocity_alpha_threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "velocity_alpha_threshold"},
            entt::attribute{"pretty_name", "Velocity Alpha Threshold"},
            entt::attribute{"tooltip", "Opacity from which a texel writes its motion."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"group", "Performance"},
            entt::attribute{"predicate", writes_velocity},
        })
        .data<nullptr, &particle_emitter_component::get_num_particles>("num_particles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "num_particles"},
            entt::attribute{"pretty_name", "Live Particles"},
            entt::attribute{"tooltip", "Particles alive now."},
            entt::attribute{"group", "Statistics"},
        })
        .data<nullptr, &particle_emitter_component::get_world_bounds>("world_bounds"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_bounds"},
            entt::attribute{"pretty_name", "World Bounds"},
            entt::attribute{"tooltip", "World box every particle stays inside, used for culling."},
            entt::attribute{"group", "Statistics"},
        });
}

SAVE(particle_emitter_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.is_enabled()));
    try_save(ar, ser20::make_nvp("loop", obj.is_loop()));
    try_save(ar, ser20::make_nvp("duration", obj.get_duration()));
    try_save(ar, ser20::make_nvp("start_delay", obj.get_start_delay()));
    try_save(ar, ser20::make_nvp("prewarm", obj.get_prewarm()));
    try_save(ar, ser20::make_nvp("auto_play", obj.get_auto_play()));
    try_save(ar, ser20::make_nvp("lifetime", obj.get_lifetime()));
    try_save(ar, ser20::make_nvp("max_particles", obj.get_max_particles()));
    try_save(ar, ser20::make_nvp("simulation_space", obj.get_simulation_space()));
    try_save(ar, ser20::make_nvp("use_fixed_seed", obj.get_use_fixed_seed()));
    try_save(ar, ser20::make_nvp("random_seed", obj.get_random_seed()));
    try_save(ar, ser20::make_nvp("emission_rate", obj.get_emission_rate()));
    try_save(ar, ser20::make_nvp("rate_over_distance", obj.get_rate_over_distance()));
    try_save(ar, ser20::make_nvp("bursts", obj.get_bursts()));
    try_save(ar, ser20::make_nvp("spawn_along_path", obj.get_spawn_along_path()));
    try_save(ar, ser20::make_nvp("lifetime_by_emitter_speed_curve", obj.get_lifetime_by_emitter_speed_curve()));
    try_save(ar, ser20::make_nvp("lifetime_by_emitter_speed_range", obj.get_lifetime_by_emitter_speed_range()));
    try_save(ar, ser20::make_nvp("shape", obj.get_shape()));
    try_save(ar, ser20::make_nvp("shape_position", obj.get_shape_position()));
    try_save(ar, ser20::make_nvp("shape_scale", obj.get_shape_scale()));
    try_save(ar, ser20::make_nvp("cone_angle", obj.get_cone_angle()));
    try_save(ar, ser20::make_nvp("torus_thickness", obj.get_torus_thickness()));
    try_save(ar, ser20::make_nvp("shape_mesh", obj.get_shape_mesh()));
    try_save(ar, ser20::make_nvp("shape_mesh_mode", obj.get_shape_mesh_mode()));
    try_save(ar, ser20::make_nvp("spawn_location", obj.get_spawn_location()));
    try_save(ar, ser20::make_nvp("direction", obj.get_direction()));
    try_save(ar, ser20::make_nvp("direction_spread", obj.get_direction_spread()));
    try_save(ar, ser20::make_nvp("start_speed", obj.get_start_speed()));
    try_save(ar, ser20::make_nvp("speed_over_life", obj.get_speed_over_life()));
    try_save(ar, ser20::make_nvp("motion_easing", obj.get_motion_easing()));
    try_save(ar, ser20::make_nvp("drag", obj.get_drag()));
    try_save(ar, ser20::make_nvp("gravity_scale", obj.get_gravity_scale()));
    try_save(ar, ser20::make_nvp("acceleration", obj.get_acceleration()));
    try_save(ar, ser20::make_nvp("inherit_velocity", obj.get_inherit_velocity()));
    try_save(ar, ser20::make_nvp("turbulence_strength", obj.get_turbulence_strength()));
    try_save(ar, ser20::make_nvp("turbulence_frequency", obj.get_turbulence_frequency()));
    try_save(ar, ser20::make_nvp("turbulence_speed", obj.get_turbulence_speed()));
    try_save(ar, ser20::make_nvp("turbulence_over_life", obj.get_turbulence_over_life()));
    try_save(ar, ser20::make_nvp("orbit_speed", obj.get_orbit_speed()));
    try_save(ar, ser20::make_nvp("orbit_over_life", obj.get_orbit_over_life()));
    try_save(ar, ser20::make_nvp("radial_speed", obj.get_radial_speed()));
    try_save(ar, ser20::make_nvp("radial_over_life", obj.get_radial_over_life()));
    try_save(ar, ser20::make_nvp("force_axis", obj.get_force_axis()));
    try_save(ar, ser20::make_nvp("converge_to_point", obj.get_converge_to_point()));
    try_save(ar, ser20::make_nvp("converge_over_life", obj.get_converge_over_life()));
    save_entity_reference(ar, "converge_target", obj.get_converge_target());
    try_save(ar, ser20::make_nvp("converge_offset", obj.get_converge_offset()));
    try_save(ar, ser20::make_nvp("collision", obj.get_collision()));
    try_save(ar, ser20::make_nvp("collision_planes", obj.get_collision_planes()));
    try_save(ar, ser20::make_nvp("collision_radius_scale", obj.get_collision_radius_scale()));
    try_save(ar, ser20::make_nvp("restitution", obj.get_restitution()));
    try_save(ar, ser20::make_nvp("friction", obj.get_friction()));
    try_save(ar, ser20::make_nvp("max_bounces", obj.get_max_bounces()));
    try_save(ar, ser20::make_nvp("collide_with", obj.get_collide_with()));
    try_save(ar, ser20::make_nvp("collision_layers", obj.get_collision_layers()));
    try_save(ar, ser20::make_nvp("collision_quality", obj.get_collision_quality()));
    try_save(ar, ser20::make_nvp("rest_speed", obj.get_rest_speed()));
    try_save(ar, ser20::make_nvp("lifetime_loss", obj.get_lifetime_loss()));
    try_save(ar, ser20::make_nvp("bounce_scatter", obj.get_bounce_scatter()));
    try_save(ar, ser20::make_nvp("align_to_surface", obj.get_align_to_surface()));
    try_save(ar, ser20::make_nvp("collision_decal", obj.get_collision_decal()));
    try_save(ar, ser20::make_nvp("sub_emitters", obj.get_sub_emitters()));
    try_save(ar, ser20::make_nvp("send_birth_events", obj.get_send_birth_events()));
    try_save(ar, ser20::make_nvp("send_death_events", obj.get_send_death_events()));
    try_save(ar, ser20::make_nvp("send_collision_events", obj.get_send_collision_events()));
    try_save(ar, ser20::make_nvp("notify_hit_entities", obj.get_notify_hit_entities()));
    try_save(ar, ser20::make_nvp("size_over_life", obj.get_size_over_life()));
    try_save(ar, ser20::make_nvp("size_axes", obj.get_size_axes()));
    try_save(ar, ser20::make_nvp("size_axes_over_life", obj.get_size_axes_over_life()));
    try_save(ar, ser20::make_nvp("size_by_speed_multiplier", obj.get_size_by_speed_multiplier()));
    try_save(ar, ser20::make_nvp("size_by_speed_speed_range", obj.get_size_by_speed_speed_range()));
    try_save(ar, ser20::make_nvp("color_gradient", obj.get_color_gradient()));
    try_save(ar, ser20::make_nvp("random_between_gradients", obj.get_random_between_gradients()));
    try_save(ar, ser20::make_nvp("color_gradient_b", obj.get_color_gradient_b()));
    try_save(ar, ser20::make_nvp("opacity", obj.get_opacity()));
    try_save(ar, ser20::make_nvp("color_intensity", obj.get_color_intensity()));
    try_save(ar, ser20::make_nvp("intensity_over_life", obj.get_intensity_over_life()));
    try_save(ar, ser20::make_nvp("color_by_speed_gradient", obj.get_color_by_speed_gradient()));
    try_save(ar, ser20::make_nvp("color_by_speed_speed_range", obj.get_color_by_speed_speed_range()));
    try_save(ar, ser20::make_nvp("start_rotation", obj.get_start_rotation()));
    try_save(ar, ser20::make_nvp("spin_speed", obj.get_spin_speed()));
    try_save(ar, ser20::make_nvp("spin_over_life", obj.get_spin_over_life()));
    try_save(ar, ser20::make_nvp("random_spin_direction", obj.get_random_spin_direction()));
    try_save(ar, ser20::make_nvp("align_to_direction", obj.get_align_to_direction()));
    try_save(ar, ser20::make_nvp("texture_sheet_tiles", obj.get_texture_sheet_tiles()));
    try_save(ar, ser20::make_nvp("texture_sheet_cycles", obj.get_texture_sheet_cycles()));
    try_save(ar, ser20::make_nvp("texture_sheet_randomize", obj.get_texture_sheet_randomize()));
    try_save(ar, ser20::make_nvp("texture_sheet_random_row", obj.get_texture_sheet_random_row()));
    try_save(ar, ser20::make_nvp("texture_sheet_use_frame_curve", obj.get_texture_sheet_use_frame_curve()));
    try_save(ar, ser20::make_nvp("texture_sheet_frame_curve", obj.get_texture_sheet_frame_curve()));
    try_save(ar, ser20::make_nvp("frame_blending", obj.get_frame_blending()));
    try_save(ar, ser20::make_nvp("erosion_over_life", obj.get_erosion_over_life()));
    try_save(ar, ser20::make_nvp("render_mode", obj.get_render_mode()));
    try_save(ar, ser20::make_nvp("trail_length", obj.get_trail_length()));
    try_save(ar, ser20::make_nvp("trail_width_end", obj.get_trail_width_end()));
    try_save(ar, ser20::make_nvp("ribbon_texture_mode", obj.get_ribbon_texture_mode()));
    try_save(ar, ser20::make_nvp("ribbon_tile_time", obj.get_ribbon_tile_time()));
    try_save(ar, ser20::make_nvp("stretch_length_scale", obj.get_stretch_length_scale()));
    try_save(ar, ser20::make_nvp("stretch_speed_scale", obj.get_stretch_speed_scale()));
    try_save(ar, ser20::make_nvp("stretch_camera_velocity_scale", obj.get_stretch_camera_velocity_scale()));
    try_save(ar, ser20::make_nvp("facing_axis", obj.get_facing_axis()));
    try_save(ar, ser20::make_nvp("render_mesh", obj.get_render_mesh()));
    try_save(ar, ser20::make_nvp("render_material", obj.get_render_material()));
    try_save(ar, ser20::make_nvp("material_overrides", obj.get_material_instance()));
    try_save(ar, ser20::make_nvp("custom_data_x", obj.get_custom_data_x()));
    try_save(ar, ser20::make_nvp("custom_data_y", obj.get_custom_data_y()));
    try_save(ar, ser20::make_nvp("mesh_cast_shadows", obj.get_mesh_cast_shadows()));
    try_save(ar, ser20::make_nvp("pivot", obj.get_pivot()));
    try_save(ar, ser20::make_nvp("soft_fade_distance", obj.get_soft_fade_distance()));
    try_save(ar, ser20::make_nvp("camera_offset", obj.get_camera_offset()));
    try_save(ar, ser20::make_nvp("lighting", obj.get_lighting()));
    try_save(ar, ser20::make_nvp("ambient_light", obj.get_ambient_light()));
    try_save(ar, ser20::make_nvp("lighting_wrap", obj.get_lighting_wrap()));
    try_save(ar, ser20::make_nvp("receive_shadows", obj.get_receive_shadows()));
    try_save(ar, ser20::make_nvp("light_count", obj.get_light_count()));
    try_save(ar, ser20::make_nvp("light_intensity", obj.get_light_intensity()));
    try_save(ar, ser20::make_nvp("light_range", obj.get_light_range()));
    try_save(ar, ser20::make_nvp("light_color_from_particle", obj.get_light_color_from_particle()));
    try_save(ar, ser20::make_nvp("sort_mode", obj.get_sort_mode()));
    try_save(ar, ser20::make_nvp("sort_priority", obj.get_sort_priority()));
    try_save(ar, ser20::make_nvp("sort_backend", obj.get_sort_backend()));
    try_save(ar, ser20::make_nvp("culling_mode", obj.get_culling_mode()));
    try_save(ar, ser20::make_nvp("max_draw_distance", obj.get_max_draw_distance()));
    try_save(ar, ser20::make_nvp("draw_distance_fade", obj.get_draw_distance_fade()));
    try_save(ar, ser20::make_nvp("camera_fade_range", obj.get_camera_fade_range()));
    try_save(ar, ser20::make_nvp("max_screen_size", obj.get_max_screen_size()));
    try_save(ar, ser20::make_nvp("subpixel_fade", obj.get_subpixel_fade()));
    try_save(ar, ser20::make_nvp("bounds_mode", obj.get_bounds_mode()));
    try_save(ar, ser20::make_nvp("fixed_bounds", obj.get_fixed_bounds()));
    try_save(ar, ser20::make_nvp("primitive", obj.get_primitive()));
    try_save(ar, ser20::make_nvp("cutout_alpha_threshold", obj.get_cutout_alpha_threshold()));
    try_save(ar, ser20::make_nvp("half_resolution", obj.get_half_resolution()));
    try_save(ar, ser20::make_nvp("taa_response", obj.get_taa_response()));
    try_save(ar, ser20::make_nvp("write_velocity", obj.get_write_velocity()));
    try_save(ar, ser20::make_nvp("velocity_alpha_threshold", obj.get_velocity_alpha_threshold()));
    try_save(ar, ser20::make_nvp("ribbon_facing", obj.get_ribbon_facing()));
    try_save(ar, ser20::make_nvp("ribbon_count", obj.get_ribbon_count()));
    try_save(ar, ser20::make_nvp("ribbon_smoothing", obj.get_ribbon_smoothing()));
}
SAVE_INSTANTIATE(particle_emitter_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(particle_emitter_component, ser20::oarchive_binary_t);

namespace
{

/// The name the property called @p name was saved under before it was renamed (the reflection's former_name);
/// empty for a property that was never renamed.
auto get_former_particle_key(const char* name) -> std::string
{
    for(auto [id, prop] : entt::resolve<particle_emitter_component>().data())
    {
        if(entt::get_name(prop) == name)
        {
            return entt::get_former_name(prop);
        }
    }
    return {};
}

/// Loads @p name over @p value and hands it to @p apply when present; a scene saved before the property was
/// renamed holds it under its former name. @p value is the component's current one: a prefab override on part of
/// the property (a field, a key, a list element) skips that part, which then keeps the instance's own value.
template<typename Archive, typename T, typename Apply>
void load_property(Archive& ar, const char* name, T value, Apply&& apply)
{
    if(try_load(ar, ser20::make_nvp(name, value)))
    {
        apply(value);
        return;
    }
    const std::string former_name = get_former_particle_key(name);
    if(!former_name.empty() && try_load(ar, ser20::make_nvp(former_name.c_str(), value)))
    {
        apply(value);
    }
}

/// Scenes saved with one curve per quad axis: they become the x and y channels of the size axes curve (z, which
/// they never had, stays as it is).
template<typename Archive>
void load_legacy_size_axes_curves(Archive& ar, particle_emitter_component& obj)
{
    math::gradient<float> size_x;
    math::gradient<float> size_y;
    const bool has_x = try_load(ar, ser20::make_nvp("size_x_over_life", size_x));
    const bool has_y = try_load(ar, ser20::make_nvp("size_y_over_life", size_y));
    if(!has_x && !has_y)
    {
        return;
    }
    math::gradient<math::vec3> axes = obj.get_size_axes_over_life();
    auto channels = axes.get_channels();
    if(has_x)
    {
        channels[0] = size_x.get_channel(0);
    }
    if(has_y)
    {
        channels[1] = size_y.get_channel(0);
    }
    axes.set_channels(channels);
    axes.set_linked(false);
    obj.set_size_axes_over_life(axes);
}

/// Scenes saved before the closed-form motion. A particle then followed a curve that is exactly a
/// launch at twice velocity_gradient's last key (less velocity_damping) per lifetime, slowing linearly
/// to a stop at death, under twice the gravity and acceleration; and its speed-based color and size read
/// half its true speed. The same motion and mappings in the current model: that launch speed, a speed
/// curve from 1 to 0, gravity and acceleration doubled, and the speed ranges doubled.
template<typename Archive>
void load_legacy_motion(Archive& ar, particle_emitter_component& obj)
{
    std::chrono::duration<float> legacy_lifetime{1.0f};
    if(try_load(ar, ser20::make_nvp("lifetime", legacy_lifetime)))
    {
        obj.set_lifetime(frange_t(legacy_lifetime.count(), legacy_lifetime.count()));
    }
    std::chrono::duration<float> emission_lifetime{2.0f};
    if(try_load(ar, ser20::make_nvp("emission_lifetime", emission_lifetime)))
    {
        obj.set_duration(emission_lifetime);
    }
    // Every scene saved with the old motion holds its velocity curve; a partial load (a prefab
    // override) without it has no motion to convert.
    math::gradient<frange_t> velocity_gradient;
    if(!try_load(ar, ser20::make_nvp("velocity_gradient", velocity_gradient)))
    {
        return;
    }
    const frange_t distance = velocity_gradient.sample(1.0f);
    float velocity_damping{0.0f};
    try_load(ar, ser20::make_nvp("velocity_damping", velocity_damping));
    const float keep = 1.0f - math::clamp(velocity_damping, 0.0f, 1.0f);
    const float lifetime = math::max(obj.get_lifetime().max, 1.0e-3f);
    const float launch_per_distance = 2.0f * keep / lifetime;
    obj.set_start_speed(frange_t(distance.min * launch_per_distance, distance.max * launch_per_distance));
    math::gradient<float> slow_to_stop;
    slow_to_stop.add_point(1.0f, 0.0f);
    slow_to_stop.add_point(0.0f, 1.0f);
    obj.set_speed_over_life(slow_to_stop);
    obj.set_gravity_scale(obj.get_gravity_scale() * 2.0f);
    obj.set_acceleration(obj.get_acceleration() * 2.0f);
    const auto doubled = [](const frange_t& range)
    {
        return frange_t(range.min * 2.0f, range.max * 2.0f);
    };
    obj.set_size_by_speed_speed_range(doubled(obj.get_size_by_speed_speed_range()));
    obj.set_color_by_speed_speed_range(doubled(obj.get_color_by_speed_speed_range()));
}

/// Whether the load in progress reads member @p name of the component: every member without a list of what to read
/// (a prefab instance's overrides), else the ones the list names.
auto is_emitter_member_read(const char* name) -> bool
{
    const auto* path_ctx = serialization::get_path_context();
    if(path_ctx == nullptr || !path_ctx->is_recording())
    {
        return true;
    }
    const serialization::path_segment_guard member(name);
    return path_ctx->should_serialize_property(path_ctx->get_current_path());
}

/**
 * @brief What a scene saved before materials drew particles states of an emitter's look, under the keys the emitter
 * had for it then: its texture, how the texture gives the opacity, how the particles blend, and its normal map.
 */
struct particle_legacy_look
{
    asset_handle<gfx::texture> texture;
    asset_handle<gfx::texture> normal_texture;
    ps_soa::texture_mode texture_mode{ps_soa::texture_mode::multi_channel};
    ps_soa::blend_mode blend_mode{ps_soa::blend_mode::normal};
    bool has_texture{};
    bool has_normal_texture{};
    bool has_texture_mode{};
    bool has_blend_mode{};

    auto is_stated() const -> bool
    {
        return has_texture || has_normal_texture || has_texture_mode || has_blend_mode;
    }
};

/// The old keys of the look, as far as the load reads the members that took them over: the material for the blend and
/// the texture mode, the material's overrides for the texture, both for the normal map.
template<typename Archive>
auto load_legacy_look(Archive& ar) -> particle_legacy_look
{
    particle_legacy_look look;
    const bool reads_material = is_emitter_member_read("render_material");
    const bool reads_overrides = is_emitter_member_read("material_overrides");
    // Read whole: the keys stand for members with other names, which are what a list of members names.
    const serialization::path_context_pause unfiltered;
    if(reads_material)
    {
        look.has_blend_mode = try_load(ar, ser20::make_nvp("blend_mode", look.blend_mode));
        look.has_texture_mode = try_load(ar, ser20::make_nvp("texture_mode", look.texture_mode));
    }
    if(reads_overrides)
    {
        look.has_texture = try_load(ar, ser20::make_nvp("texture", look.texture));
    }
    if(reads_material || reads_overrides)
    {
        look.has_normal_texture = try_load(ar, ser20::make_nvp("normal_texture", look.normal_texture));
    }
    return look;
}

/// Gives @p obj the look @p look states, through the setters a script picks a look with: each takes the Standard
/// particle material that draws it and keeps the texture as the emitter's override.
void apply_legacy_look(const particle_legacy_look& look, particle_emitter_component& obj)
{
    if(look.has_blend_mode)
    {
        obj.set_blend_mode(look.blend_mode);
    }
    if(look.has_texture_mode)
    {
        obj.set_texture_mode(look.texture_mode);
    }
    if(look.has_texture)
    {
        obj.set_texture(look.texture);
    }
    if(look.has_normal_texture)
    {
        obj.set_normal_texture(look.normal_texture);
    }
}

} // namespace

LOAD(particle_emitter_component)
{
    bool has_start_speed = false;
    bool has_color_by_speed = false;
    bool has_size_axes = false;
    bool has_size_axes_over_life = false;
    bool has_render_mode = false;
    load_property(ar, "enabled", obj.is_enabled(), [&](const auto& v) { obj.set_enabled(v); });
    load_property(ar, "loop", obj.is_loop(), [&](const auto& v) { obj.set_loop(v); });
    load_property(ar, "duration", obj.get_duration(), [&](const auto& v) { obj.set_duration(v); });
    load_property(ar, "start_delay", obj.get_start_delay(), [&](const auto& v) { obj.set_start_delay(v); });
    load_property(ar, "prewarm", obj.get_prewarm(), [&](const auto& v) { obj.set_prewarm(v); });
    load_property(ar, "auto_play", obj.get_auto_play(), [&](const auto& v) { obj.set_auto_play(v); });
    // Older scenes hold a single number here: that load fails quietly and the legacy motion reads it.
    load_property(ar, "lifetime", obj.get_lifetime(), [&](const auto& v) { obj.set_lifetime(v); });
    load_property(ar, "max_particles", obj.get_max_particles(), [&](const auto& v) { obj.set_max_particles(v); });
    load_property(ar, "simulation_space", obj.get_simulation_space(), [&](const auto& v) { obj.set_simulation_space(v); });
    load_property(ar, "use_fixed_seed", obj.get_use_fixed_seed(), [&](const auto& v) { obj.set_use_fixed_seed(v); });
    load_property(ar, "random_seed", obj.get_random_seed(), [&](const auto& v) { obj.set_random_seed(v); });
    load_property(ar, "emission_rate", obj.get_emission_rate(), [&](const auto& v) { obj.set_emission_rate(v); });
    load_property(ar, "rate_over_distance", obj.get_rate_over_distance(), [&](const auto& v) { obj.set_rate_over_distance(v); });
    load_property(ar, "bursts", obj.get_bursts(), [&](const auto& v) { obj.set_bursts(v); });
    load_property(ar, "spawn_along_path", obj.get_spawn_along_path(), [&](const auto& v) { obj.set_spawn_along_path(v); });
    load_property(ar, "lifetime_by_emitter_speed_curve", obj.get_lifetime_by_emitter_speed_curve(), [&](const auto& v) { obj.set_lifetime_by_emitter_speed_curve(v); });
    load_property(ar, "lifetime_by_emitter_speed_range", obj.get_lifetime_by_emitter_speed_range(), [&](const auto& v) { obj.set_lifetime_by_emitter_speed_range(v); });
    load_property(ar, "shape", obj.get_shape(), [&](const auto& v) { obj.set_shape(v); });
    load_property(ar, "shape_position", obj.get_shape_position(), [&](const auto& v) { obj.set_shape_position(v); });
    load_property(ar, "shape_scale", obj.get_shape_scale(), [&](const auto& v) { obj.set_shape_scale(v); });
    load_property(ar, "cone_angle", obj.get_cone_angle(), [&](const auto& v) { obj.set_cone_angle(v); });
    load_property(ar, "torus_thickness", obj.get_torus_thickness(), [&](const auto& v) { obj.set_torus_thickness(v); });
    load_property(ar, "shape_mesh", obj.get_shape_mesh(), [&](const auto& v) { obj.set_shape_mesh(v); });
    load_property(ar, "shape_mesh_mode", obj.get_shape_mesh_mode(), [&](const auto& v) { obj.set_shape_mesh_mode(v); });
    load_property(ar, "spawn_location", obj.get_spawn_location(), [&](const auto& v) { obj.set_spawn_location(v); });
    load_property(ar, "direction", obj.get_direction(), [&](const auto& v) { obj.set_direction(v); });
    load_property(ar, "direction_spread", obj.get_direction_spread(), [&](const auto& v) { obj.set_direction_spread(v); });
    load_property(ar, "start_speed", obj.get_start_speed(), [&](const auto& v) { obj.set_start_speed(v); has_start_speed = true; });
    load_property(ar, "speed_over_life", obj.get_speed_over_life(), [&](const auto& v) { obj.set_speed_over_life(v); });
    load_property(ar, "motion_easing", obj.get_motion_easing(), [&](const auto& v) { obj.set_motion_easing(v); });
    load_property(ar, "drag", obj.get_drag(), [&](const auto& v) { obj.set_drag(v); });
    load_property(ar, "gravity_scale", obj.get_gravity_scale(), [&](const auto& v) { obj.set_gravity_scale(v); });
    load_property(ar, "acceleration", obj.get_acceleration(), [&](const auto& v) { obj.set_acceleration(v); });
    load_property(ar, "inherit_velocity", obj.get_inherit_velocity(), [&](const auto& v) { obj.set_inherit_velocity(v); });
    load_property(ar, "turbulence_strength", obj.get_turbulence_strength(), [&](const auto& v) { obj.set_turbulence_strength(v); });
    load_property(ar, "turbulence_frequency", obj.get_turbulence_frequency(), [&](const auto& v) { obj.set_turbulence_frequency(v); });
    load_property(ar, "turbulence_speed", obj.get_turbulence_speed(), [&](const auto& v) { obj.set_turbulence_speed(v); });
    load_property(ar, "turbulence_over_life", obj.get_turbulence_over_life(), [&](const auto& v) { obj.set_turbulence_over_life(v); });
    load_property(ar, "orbit_speed", obj.get_orbit_speed(), [&](const auto& v) { obj.set_orbit_speed(v); });
    load_property(ar, "orbit_over_life", obj.get_orbit_over_life(), [&](const auto& v) { obj.set_orbit_over_life(v); });
    load_property(ar, "radial_speed", obj.get_radial_speed(), [&](const auto& v) { obj.set_radial_speed(v); });
    load_property(ar, "radial_over_life", obj.get_radial_over_life(), [&](const auto& v) { obj.set_radial_over_life(v); });
    load_property(ar, "force_axis", obj.get_force_axis(), [&](const auto& v) { obj.set_force_axis(v); });
    load_property(ar, "converge_to_point", obj.get_converge_to_point(), [&](const auto& v) { obj.set_converge_to_point(v); });
    load_property(ar, "converge_over_life", obj.get_converge_over_life(), [&](const auto& v) { obj.set_converge_over_life(v); });
    entt::handle converge_target = obj.get_converge_target();
    load_entity_reference(ar, "converge_target", converge_target);
    obj.set_converge_target(converge_target);
    load_property(ar, "converge_offset", obj.get_converge_offset(), [&](const auto& v) { obj.set_converge_offset(v); });
    load_property(ar, "collision", obj.get_collision(), [&](const auto& v) { obj.set_collision(v); });
    load_property(ar, "collision_planes", obj.get_collision_planes(), [&](const auto& v) { obj.set_collision_planes(v); });
    load_property(ar, "collision_radius_scale", obj.get_collision_radius_scale(), [&](const auto& v) { obj.set_collision_radius_scale(v); });
    load_property(ar, "restitution", obj.get_restitution(), [&](const auto& v) { obj.set_restitution(v); });
    load_property(ar, "friction", obj.get_friction(), [&](const auto& v) { obj.set_friction(v); });
    load_property(ar, "max_bounces", obj.get_max_bounces(), [&](const auto& v) { obj.set_max_bounces(v); });
    load_property(ar, "collide_with", obj.get_collide_with(), [&](const auto& v) { obj.set_collide_with(v); });
    load_property(ar, "collision_layers", obj.get_collision_layers(), [&](const auto& v) { obj.set_collision_layers(v); });
    load_property(ar, "collision_quality", obj.get_collision_quality(), [&](const auto& v) { obj.set_collision_quality(v); });
    load_property(ar, "rest_speed", obj.get_rest_speed(), [&](const auto& v) { obj.set_rest_speed(v); });
    load_property(ar, "lifetime_loss", obj.get_lifetime_loss(), [&](const auto& v) { obj.set_lifetime_loss(v); });
    load_property(ar, "bounce_scatter", obj.get_bounce_scatter(), [&](const auto& v) { obj.set_bounce_scatter(v); });
    load_property(ar, "align_to_surface", obj.get_align_to_surface(), [&](const auto& v) { obj.set_align_to_surface(v); });
    load_property(ar, "collision_decal", obj.get_collision_decal(), [&](const auto& v) { obj.set_collision_decal(v); });
    load_property(ar, "sub_emitters", obj.get_sub_emitters(), [&](const auto& v) { obj.set_sub_emitters(v); });
    load_property(ar, "send_birth_events", obj.get_send_birth_events(), [&](const auto& v) { obj.set_send_birth_events(v); });
    load_property(ar, "send_death_events", obj.get_send_death_events(), [&](const auto& v) { obj.set_send_death_events(v); });
    load_property(ar, "send_collision_events", obj.get_send_collision_events(), [&](const auto& v) { obj.set_send_collision_events(v); });
    load_property(ar, "notify_hit_entities", obj.get_notify_hit_entities(), [&](const auto& v) { obj.set_notify_hit_entities(v); });
    load_property(ar, "size_over_life", obj.get_size_over_life(), [&](const auto& v) { obj.set_size_over_life(v); });
    load_property(ar, "size_axes", obj.get_size_axes(), [&](const auto& v) { obj.set_size_axes(v); has_size_axes = true; });
    load_property(ar, "size_axes_over_life", obj.get_size_axes_over_life(), [&](const auto& v) { obj.set_size_axes_over_life(v); has_size_axes_over_life = true; });
    load_property(ar, "size_by_speed_multiplier", obj.get_size_by_speed_multiplier(), [&](const auto& v) { obj.set_size_by_speed_multiplier(v); });
    load_property(ar, "size_by_speed_speed_range", obj.get_size_by_speed_speed_range(), [&](const auto& v) { obj.set_size_by_speed_speed_range(v); });
    load_property(ar, "color_gradient", obj.get_color_gradient(), [&](const auto& v) { obj.set_color_gradient(v); });
    load_property(ar, "random_between_gradients", obj.get_random_between_gradients(), [&](const auto& v) { obj.set_random_between_gradients(v); });
    load_property(ar, "color_gradient_b", obj.get_color_gradient_b(), [&](const auto& v) { obj.set_color_gradient_b(v); });
    load_property(ar, "opacity", obj.get_opacity(), [&](const auto& v) { obj.set_opacity(v); });
    load_property(ar, "color_intensity", obj.get_color_intensity(), [&](const auto& v) { obj.set_color_intensity(v); });
    load_property(ar, "intensity_over_life", obj.get_intensity_over_life(), [&](const auto& v) { obj.set_intensity_over_life(v); });
    load_property(ar, "color_by_speed_gradient", obj.get_color_by_speed_gradient(), [&](const auto& v) { obj.set_color_by_speed_gradient(v); has_color_by_speed = true; });
    load_property(ar, "color_by_speed_speed_range", obj.get_color_by_speed_speed_range(), [&](const auto& v) { obj.set_color_by_speed_speed_range(v); });
    load_property(ar, "start_rotation", obj.get_start_rotation(), [&](const auto& v) { obj.set_start_rotation(v); });
    load_property(ar, "spin_speed", obj.get_spin_speed(), [&](const auto& v) { obj.set_spin_speed(v); });
    load_property(ar, "spin_over_life", obj.get_spin_over_life(), [&](const auto& v) { obj.set_spin_over_life(v); });
    load_property(ar, "random_spin_direction", obj.get_random_spin_direction(), [&](const auto& v) { obj.set_random_spin_direction(v); });
    load_property(ar, "align_to_direction", obj.get_align_to_direction(), [&](const auto& v) { obj.set_align_to_direction(v); });
    load_property(ar, "texture_sheet_tiles", obj.get_texture_sheet_tiles(), [&](const auto& v) { obj.set_texture_sheet_tiles(v); });
    load_property(ar, "texture_sheet_cycles", obj.get_texture_sheet_cycles(), [&](const auto& v) { obj.set_texture_sheet_cycles(v); });
    load_property(ar, "texture_sheet_randomize", obj.get_texture_sheet_randomize(), [&](const auto& v) { obj.set_texture_sheet_randomize(v); });
    load_property(ar, "texture_sheet_random_row", obj.get_texture_sheet_random_row(), [&](const auto& v) { obj.set_texture_sheet_random_row(v); });
    load_property(ar, "texture_sheet_use_frame_curve", obj.get_texture_sheet_use_frame_curve(), [&](const auto& v) { obj.set_texture_sheet_use_frame_curve(v); });
    load_property(ar, "texture_sheet_frame_curve", obj.get_texture_sheet_frame_curve(), [&](const auto& v) { obj.set_texture_sheet_frame_curve(v); });
    load_property(ar, "frame_blending", obj.get_frame_blending(), [&](const auto& v) { obj.set_frame_blending(v); });
    load_property(ar, "erosion_over_life", obj.get_erosion_over_life(), [&](const auto& v) { obj.set_erosion_over_life(v); });
    load_property(ar, "render_mode", obj.get_render_mode(), [&](const auto& v) { obj.set_render_mode(v); has_render_mode = true; });
    load_property(ar, "trail_length", obj.get_trail_length(), [&](const auto& v) { obj.set_trail_length(v); });
    load_property(ar, "trail_width_end", obj.get_trail_width_end(), [&](const auto& v) { obj.set_trail_width_end(v); });
    load_property(ar, "ribbon_texture_mode", obj.get_ribbon_texture_mode(), [&](const auto& v) { obj.set_ribbon_texture_mode(v); });
    load_property(ar, "ribbon_tile_time", obj.get_ribbon_tile_time(), [&](const auto& v) { obj.set_ribbon_tile_time(v); });
    load_property(ar, "stretch_length_scale", obj.get_stretch_length_scale(), [&](const auto& v) { obj.set_stretch_length_scale(v); });
    load_property(ar, "stretch_speed_scale", obj.get_stretch_speed_scale(), [&](const auto& v) { obj.set_stretch_speed_scale(v); });
    load_property(ar, "stretch_camera_velocity_scale", obj.get_stretch_camera_velocity_scale(), [&](const auto& v) { obj.set_stretch_camera_velocity_scale(v); });
    load_property(ar, "facing_axis", obj.get_facing_axis(), [&](const auto& v) { obj.set_facing_axis(v); });
    load_property(ar, "render_mesh", obj.get_render_mesh(), [&](const auto& v) { obj.set_render_mesh(v); });
    // A scene from before materials drew particles names the look on the emitter itself: it loads as the Standard
    // particle material of that look, and the material it names is the one its mesh mode drew.
    const particle_legacy_look legacy_look = load_legacy_look(ar);
    const bool is_mesh_mode = obj.get_render_mode() == ps_soa::render_mode::mesh;
    if(!legacy_look.is_stated() || is_mesh_mode)
    {
        load_property(ar, "render_material", obj.get_render_material(), [&](const auto& v) { obj.set_render_material(v); });
    }
    if(!legacy_look.is_stated())
    {
        // The overrides load onto an instance of the material just read, which tells their types.
        load_property(ar, "material_overrides", obj.get_material_instance(), [&](const auto& v) { obj.set_material_instance(v); });
    }
    else if(!is_mesh_mode || (obj.get_render_material().uid().is_nil() && obj.get_render_material().id().empty()))
    {
        // In Mesh mode too while no material covers the mesh: the engine's particle materials leave a mesh its own,
        // and the look is there when the emitter draws sprites again.
        apply_legacy_look(legacy_look, obj);
    }
    load_property(ar, "custom_data_x", obj.get_custom_data_x(), [&](const auto& v) { obj.set_custom_data_x(v); });
    load_property(ar, "custom_data_y", obj.get_custom_data_y(), [&](const auto& v) { obj.set_custom_data_y(v); });
    load_property(ar, "mesh_cast_shadows", obj.get_mesh_cast_shadows(), [&](const auto& v) { obj.set_mesh_cast_shadows(v); });
    load_property(ar, "pivot", obj.get_pivot(), [&](const auto& v) { obj.set_pivot(v); });
    load_property(ar, "soft_fade_distance", obj.get_soft_fade_distance(), [&](const auto& v) { obj.set_soft_fade_distance(v); });
    load_property(ar, "camera_offset", obj.get_camera_offset(), [&](const auto& v) { obj.set_camera_offset(v); });
    load_property(ar, "lighting", obj.get_lighting(), [&](const auto& v) { obj.set_lighting(v); });
    load_property(ar, "ambient_light", obj.get_ambient_light(), [&](const auto& v) { obj.set_ambient_light(v); });
    load_property(ar, "lighting_wrap", obj.get_lighting_wrap(), [&](const auto& v) { obj.set_lighting_wrap(v); });
    load_property(ar, "receive_shadows", obj.get_receive_shadows(), [&](const auto& v) { obj.set_receive_shadows(v); });
    load_property(ar, "light_count", obj.get_light_count(), [&](const auto& v) { obj.set_light_count(v); });
    load_property(ar, "light_intensity", obj.get_light_intensity(), [&](const auto& v) { obj.set_light_intensity(v); });
    load_property(ar, "light_range", obj.get_light_range(), [&](const auto& v) { obj.set_light_range(v); });
    load_property(ar, "light_color_from_particle", obj.get_light_color_from_particle(), [&](const auto& v) { obj.set_light_color_from_particle(v); });
    load_property(ar, "sort_mode", obj.get_sort_mode(), [&](const auto& v) { obj.set_sort_mode(v); });
    load_property(ar, "sort_priority", obj.get_sort_priority(), [&](const auto& v) { obj.set_sort_priority(v); });
    load_property(ar, "sort_backend", obj.get_sort_backend(), [&](const auto& v) { obj.set_sort_backend(v); });
    load_property(ar, "culling_mode", obj.get_culling_mode(), [&](const auto& v) { obj.set_culling_mode(v); });
    load_property(ar, "max_draw_distance", obj.get_max_draw_distance(), [&](const auto& v) { obj.set_max_draw_distance(v); });
    load_property(ar, "draw_distance_fade", obj.get_draw_distance_fade(), [&](const auto& v) { obj.set_draw_distance_fade(v); });
    load_property(ar, "camera_fade_range", obj.get_camera_fade_range(), [&](const auto& v) { obj.set_camera_fade_range(v); });
    load_property(ar, "max_screen_size", obj.get_max_screen_size(), [&](const auto& v) { obj.set_max_screen_size(v); });
    load_property(ar, "subpixel_fade", obj.get_subpixel_fade(), [&](const auto& v) { obj.set_subpixel_fade(v); });
    load_property(ar, "bounds_mode", obj.get_bounds_mode(), [&](const auto& v) { obj.set_bounds_mode(v); });
    load_property(ar, "fixed_bounds", obj.get_fixed_bounds(), [&](const auto& v) { obj.set_fixed_bounds(v); });
    load_property(ar, "primitive", obj.get_primitive(), [&](const auto& v) { obj.set_primitive(v); });
    load_property(ar, "cutout_alpha_threshold", obj.get_cutout_alpha_threshold(), [&](const auto& v) { obj.set_cutout_alpha_threshold(v); });
    load_property(ar, "half_resolution", obj.get_half_resolution(), [&](const auto& v) { obj.set_half_resolution(v); });
    load_property(ar, "taa_response", obj.get_taa_response(), [&](const auto& v) { obj.set_taa_response(v); });
    load_property(ar, "write_velocity", obj.get_write_velocity(), [&](const auto& v) { obj.set_write_velocity(v); });
    load_property(ar, "velocity_alpha_threshold", obj.get_velocity_alpha_threshold(), [&](const auto& v) { obj.set_velocity_alpha_threshold(v); });
    load_property(ar, "ribbon_facing", obj.get_ribbon_facing(), [&](const auto& v) { obj.set_ribbon_facing(v); });
    load_property(ar, "ribbon_count", obj.get_ribbon_count(), [&](const auto& v) { obj.set_ribbon_count(v); });
    load_property(ar, "ribbon_smoothing", obj.get_ribbon_smoothing(), [&](const auto& v) { obj.set_ribbon_smoothing(v); });

    // Older scenes: keys that were replaced (a renamed key loads from its former name above).
    if(!has_start_speed)
    {
        load_legacy_motion(ar, obj);
    }
    if(!has_color_by_speed)
    {
        math::color color_by_speed_slow_color{0xffffffff};
        math::color color_by_speed_fast_color{0xffffffff};
        const bool has_slow = try_load(ar, ser20::make_nvp("color_by_speed_slow_color", color_by_speed_slow_color));
        const bool has_fast = try_load(ar, ser20::make_nvp("color_by_speed_fast_color", color_by_speed_fast_color));
        if(has_slow || has_fast)
        {
            math::gradient<math::color> legacy_gradient;
            legacy_gradient.add_point(color_by_speed_slow_color, 0.0f);
            legacy_gradient.add_point(color_by_speed_fast_color, 1.0f);
            obj.set_color_by_speed_gradient(legacy_gradient);
        }
    }
    if(!has_size_axes)
    {
        load_property(ar, "particle_scale_3d", obj.get_size_axes(), [&](const math::vec3& v) { obj.set_size_axes(v); });
    }
    if(!has_size_axes_over_life)
    {
        load_legacy_size_axes_curves(ar, obj);
    }
    if(!has_render_mode)
    {
        load_property(ar, "billboard_mode", obj.get_render_mode(), [&](auto v) { obj.set_render_mode(v); });
    }
}
LOAD_INSTANTIATE(particle_emitter_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(particle_emitter_component, ser20::iarchive_binary_t);

} // namespace unravel
