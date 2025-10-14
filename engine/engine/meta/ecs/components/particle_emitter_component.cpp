#include "particle_emitter_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/array.hpp>
#include <serialization/types/chrono.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/core/common/basetypes.hpp>
#include <engine/meta/assets/asset_handle.hpp>

namespace unravel
{


REFLECT(particle_emitter_component)
{
    // Register EmitterShape enum

    entt::meta_factory<EmitterShape::Enum>{}
        .type("EmitterShape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "EmitterShape"},
            entt::attribute{"pretty_name", "Emitter Shape"},
        })
        .data<EmitterShape::Sphere>("Sphere"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Sphere"},
            entt::attribute{"pretty_name", "Sphere"},
        })
        .data<EmitterShape::Hemisphere>("Hemisphere"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Hemisphere"},
            entt::attribute{"pretty_name", "Hemisphere"},
        })
        .data<EmitterShape::Circle>("Circle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Circle"},
            entt::attribute{"pretty_name", "Circle"},
        })
        .data<EmitterShape::Disc>("Disc"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Disc"},
            entt::attribute{"pretty_name", "Disc"},
        })
        .data<EmitterShape::Rect>("Rect"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Rect"},
            entt::attribute{"pretty_name", "Rectangle"},
        });


    entt::meta_factory<EmitterDirection::Enum>{}
        .type("EmitterDirection"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "EmitterDirection"},
            entt::attribute{"pretty_name", "Emitter Direction"},
        })
        .data<EmitterDirection::Up>("Up"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Up"},
            entt::attribute{"pretty_name", "Up"},
        })
        .data<EmitterDirection::Outward>("Outward"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Outward"},
            entt::attribute{"pretty_name", "Outward"},
        });

    entt::meta_factory<bx::Easing::Enum>{}
        .type("Easing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Easing"},
            entt::attribute{"pretty_name", "Easing Function"},
        })
        .data<bx::Easing::Linear>("Linear"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Linear"},
            entt::attribute{"pretty_name", "Linear"},
        })
        .data<bx::Easing::Step>("Step"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Step"},
            entt::attribute{"pretty_name", "Step"},
        })
        .data<bx::Easing::SmoothStep>("SmoothStep"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "SmoothStep"},
            entt::attribute{"pretty_name", "Smooth Step"},
        })
        .data<bx::Easing::InQuad>("InQuad"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InQuad"},
            entt::attribute{"pretty_name", "In Quad"},
        })
        .data<bx::Easing::OutQuad>("OutQuad"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "OutQuad"},
            entt::attribute{"pretty_name", "Out Quad"},
        })
        .data<bx::Easing::InOutQuad>("InOutQuad"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InOutQuad"},
            entt::attribute{"pretty_name", "In-Out Quad"},
        })
        .data<bx::Easing::InCubic>("InCubic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InCubic"},
            entt::attribute{"pretty_name", "In Cubic"},
        })
        .data<bx::Easing::OutCubic>("OutCubic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "OutCubic"},
            entt::attribute{"pretty_name", "Out Cubic"},
        })
        .data<bx::Easing::InOutCubic>("InOutCubic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InOutCubic"},
            entt::attribute{"pretty_name", "In-Out Cubic"},
        })
        .data<bx::Easing::InSine>("InSine"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InSine"},
            entt::attribute{"pretty_name", "In Sine"},
        })
        .data<bx::Easing::OutSine>("OutSine"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "OutSine"},
            entt::attribute{"pretty_name", "Out Sine"},
        })
        .data<bx::Easing::InOutSine>("InOutSine"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InOutSine"},
            entt::attribute{"pretty_name", "In-Out Sine"},
        })
        .data<bx::Easing::InExpo>("InExpo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InExpo"},
            entt::attribute{"pretty_name", "In Expo"},
        })
        .data<bx::Easing::OutExpo>("OutExpo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "OutExpo"},
            entt::attribute{"pretty_name", "Out Expo"},
        })
        .data<bx::Easing::InOutExpo>("InOutExpo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InOutExpo"},
            entt::attribute{"pretty_name", "In-Out Expo"},
        })
        .data<bx::Easing::InElastic>("InElastic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InElastic"},
            entt::attribute{"pretty_name", "In Elastic"},
        })
        .data<bx::Easing::OutElastic>("OutElastic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "OutElastic"},
            entt::attribute{"pretty_name", "Out Elastic"},
        })
        .data<bx::Easing::InOutElastic>("InOutElastic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InOutElastic"},
            entt::attribute{"pretty_name", "In-Out Elastic"},
        })
        .data<bx::Easing::InBack>("InBack"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InBack"},
            entt::attribute{"pretty_name", "In Back"},
        })
        .data<bx::Easing::OutBack>("OutBack"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "OutBack"},
            entt::attribute{"pretty_name", "Out Back"},
        })
        .data<bx::Easing::InOutBack>("InOutBack"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InOutBack"},
            entt::attribute{"pretty_name", "In-Out Back"},
        })
        .data<bx::Easing::InBounce>("InBounce"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InBounce"},
            entt::attribute{"pretty_name", "In Bounce"},
        })
        .data<bx::Easing::OutBounce>("OutBounce"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "OutBounce"},
            entt::attribute{"pretty_name", "Out Bounce"},
        })
        .data<bx::Easing::InOutBounce>("InOutBounce"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "InOutBounce"},
            entt::attribute{"pretty_name", "In-Out Bounce"},
        });


    entt::meta_factory<particle_emitter_component>{}
        .type("particle_emitter_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "particle_emitter_component"},
            entt::attribute{"category", "RENDERING"},
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
            entt::attribute{"tooltip", "Controls whether the particle emitter actively spawns and updates particles. Disabled emitters stop emission but existing particles continue to animate."},
        })
        .data<&particle_emitter_component::set_max_particles, &particle_emitter_component::get_max_particles>("max_particles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_particles"},
            entt::attribute{"pretty_name", "Max Particles"},
            entt::attribute{"tooltip", "Maximum number of particles that can exist simultaneously. Higher values allow more dense effects but impact performance."},
        })
        .data<nullptr, &particle_emitter_component::get_num_particles>("num_particles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "num_particles"},
            entt::attribute{"pretty_name", "Num Particles"},
            entt::attribute{"tooltip", "Current number of active particles in the system (read-only). Updates in real-time as particles spawn and die."},
        })
        .data<&particle_emitter_component::set_emission_lifetime, &particle_emitter_component::get_emission_lifetime>("emission_lifetime"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emission_lifetime"},
            entt::attribute{"pretty_name", "Emission Lifetime"},
            entt::attribute{"tooltip", "Duration of one complete emission cycle in seconds. Controls how long the emitter takes to spawn all particles before restarting the cycle."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&particle_emitter_component::set_emission_rate, &particle_emitter_component::get_emission_rate>("emission_rate"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emission_rate"},
            entt::attribute{"pretty_name", "Emission Rate"},
            entt::attribute{"tooltip", "Emission rate in particles per second. Higher values create denser particle effects. 0 = no emission."},
            entt::attribute{"min", 0.0f},
        })
        .data<&particle_emitter_component::set_temporal_motion, &particle_emitter_component::get_temporal_motion>("temporal_motion"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "temporal_motion"},
            entt::attribute{"pretty_name", "Temporal Motion"},
            entt::attribute{"tooltip", "Controls temporal interpolation for moving emitters. 1.0 = full interpolation (smooth trails), 0.0 = no interpolation (discrete emission)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
        })
        .data<&particle_emitter_component::set_velocity_damping, &particle_emitter_component::get_velocity_damping>("velocity_damping"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "velocity_damping"},
            entt::attribute{"pretty_name", "Velocity Damping"},
            entt::attribute{"tooltip", "Reduces particle velocity over time. 0.0 = no damping (particles maintain speed), 1.0 = full damping (particles stop immediately)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
        })
        .data<&particle_emitter_component::set_force_over_lifetime, &particle_emitter_component::get_force_over_lifetime>("force_over_lifetime"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "force_over_lifetime"},
            entt::attribute{"pretty_name", "Force Over Lifetime"},
            entt::attribute{"tooltip", "Additional force applied to particles throughout their lifetime. Use for wind, magnetism, or other environmental effects. Values are in world units."},
        })
        .data<&particle_emitter_component::set_gravity_scale, &particle_emitter_component::get_gravity_scale>("gravity_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gravity_scale"},
            entt::attribute{"pretty_name", "Gravity Scale"},
            entt::attribute{"tooltip", "Multiplier for gravity effect on particles. 0.0 = no gravity, 1.0 = Earth-like gravity, negative values = upward force."},
        })

        .data<nullptr, &particle_emitter_component::get_world_bounds>("world_bounds"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_bounds"},
            entt::attribute{"pretty_name", "World Bounds"},
            entt::attribute{"tooltip", "Bounding box containing all particles in world space. Used for culling and optimization (read-only)."},
        })
        .data<&particle_emitter_component::set_shape, &particle_emitter_component::get_shape>("shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape"},
            entt::attribute{"pretty_name", "Emitter Shape"},
            entt::attribute{"tooltip", "Geometric shape from which particles are spawned. Sphere = 3D ball, Hemisphere = half sphere, Circle = 2D ring, Disc = filled circle, Rect = rectangle."},
        })
        .data<&particle_emitter_component::set_direction, &particle_emitter_component::get_direction>("direction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "direction"},
            entt::attribute{"pretty_name", "Emitter Direction"},
            entt::attribute{"tooltip", "Initial direction particles move when spawned. Up = particles move upward, Outward = particles move away from spawn position."},
        })
        .data<&particle_emitter_component::set_lifetime, &particle_emitter_component::get_lifetime>("lifetime"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime"},
            entt::attribute{"pretty_name", "Lifetime"},
            entt::attribute{"tooltip", "How long each particle lives in seconds. Longer lifetimes create more persistent effects."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&particle_emitter_component::set_velocity_start_range, &particle_emitter_component::get_velocity_start_range>("velocity_start_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "velocity_start_range"},
            entt::attribute{"pretty_name", "Velocity Start Range"},
            entt::attribute{"tooltip", "Minimum and maximum initial speed when particles are spawned. Higher values make particles move faster from their spawn position."},
        })
        .data<&particle_emitter_component::set_velocity_end_range, &particle_emitter_component::get_velocity_end_range>("velocity_end_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "velocity_end_range"},
            entt::attribute{"pretty_name", "Velocity End Range"},
            entt::attribute{"tooltip", "Minimum and maximum target speed particles move toward over their lifetime. Creates acceleration/deceleration effects."},
        })
        .data<&particle_emitter_component::set_scale_start_range, &particle_emitter_component::get_scale_start_range>("scale_start_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scale_start_range"},
            entt::attribute{"pretty_name", "Scale Start Range"},
            entt::attribute{"tooltip", "Minimum and maximum size multiplier when particles spawn. 1.0 = normal size, 0.5 = half size, 2.0 = double size."},
        })
        .data<&particle_emitter_component::set_scale_end_range, &particle_emitter_component::get_scale_end_range>("scale_end_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scale_end_range"},
            entt::attribute{"pretty_name", "Scale End Range"},
            entt::attribute{"tooltip", "Minimum and maximum size multiplier when particles die. Creates growing/shrinking effects as particles animate."},
        })
        .data<&particle_emitter_component::set_blend_start_range, &particle_emitter_component::get_blend_start_range>("blend_start_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_start_range"},
            entt::attribute{"pretty_name", "Blend Start Range"},
            entt::attribute{"tooltip", "Minimum and maximum opacity/transparency when particles spawn. 0.0 = fully transparent, 1.0 = fully opaque."},
        })
        .data<&particle_emitter_component::set_blend_end_range, &particle_emitter_component::get_blend_end_range>("blend_end_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_end_range"},
            entt::attribute{"pretty_name", "Blend End Range"},
            entt::attribute{"tooltip", "Minimum and maximum opacity/transparency when particles die. Creates fade-in/fade-out effects over particle lifetime."},
        })
        .data<&particle_emitter_component::set_rgba_colors, &particle_emitter_component::get_rgba_colors>("rgba_colors"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rgba_colors"},
            entt::attribute{"pretty_name", "RGBA Colors"},
            entt::attribute{"tooltip", "5-point color gradient defining particle color over lifetime. Colors are interpolated smoothly from spawn (color 0) to death (color 4)."},
        })
        .data<&particle_emitter_component::set_position_easing, &particle_emitter_component::get_position_easing>("position_easing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "position_easing"},
            entt::attribute{"pretty_name", "Position Easing"},
            entt::attribute{"tooltip", "Curve controlling how particles move from start to end position. Linear = constant speed, EaseIn = slow start, EaseOut = slow end."},
        })
        .data<&particle_emitter_component::set_rgba_easing, &particle_emitter_component::get_rgba_easing>("rgba_easing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rgba_easing"},
            entt::attribute{"pretty_name", "RGBA Easing"},
            entt::attribute{"tooltip", "Curve controlling how particle colors change over lifetime. Affects the speed of color transitions through the 5-color gradient."},
        })
        .data<&particle_emitter_component::set_blend_easing, &particle_emitter_component::get_blend_easing>("blend_easing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_easing"},
            entt::attribute{"pretty_name", "Blend Easing"},
            entt::attribute{"tooltip", "Curve controlling how particle opacity changes over lifetime. Affects fade-in/fade-out timing and smoothness."},
        })
        .data<&particle_emitter_component::set_scale_easing, &particle_emitter_component::get_scale_easing>("scale_easing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scale_easing"},
            entt::attribute{"pretty_name", "Scale Easing"},
            entt::attribute{"tooltip", "Curve controlling how particle size changes over lifetime. Affects growth/shrink timing and smoothness."},
        })
        .data<&particle_emitter_component::set_texture, &particle_emitter_component::get_texture>("texture"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture"},
            entt::attribute{"pretty_name", "Texture"},
            entt::attribute{"tooltip", "Texture asset used to render each particle. Should be a square texture with alpha channel for best results. Common formats: smoke, fire, sparkle, etc."},
        });
}

SAVE(particle_emitter_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.is_enabled()));
    try_save(ar, ser20::make_nvp("shape", static_cast<int>(obj.get_shape())));
    try_save(ar, ser20::make_nvp("direction", static_cast<int>(obj.get_direction())));
    try_save(ar, ser20::make_nvp("max_particles", obj.get_max_particles()));
    
    
    // Emission properties
    try_save(ar, ser20::make_nvp("emission_lifetime", obj.get_emission_lifetime()));
    try_save(ar, ser20::make_nvp("gravity_scale", obj.get_gravity_scale()));
    try_save(ar, ser20::make_nvp("emission_rate", obj.get_emission_rate()));
    try_save(ar, ser20::make_nvp("temporal_motion", obj.get_temporal_motion()));
    try_save(ar, ser20::make_nvp("velocity_damping", obj.get_velocity_damping()));
    try_save(ar, ser20::make_nvp("force_over_lifetime", obj.get_force_over_lifetime()));
    
    // Range properties
    try_save(ar, ser20::make_nvp("lifetime", obj.get_lifetime()));
    try_save(ar, ser20::make_nvp("velocity_start_range", obj.get_velocity_start_range()));
    try_save(ar, ser20::make_nvp("velocity_end_range", obj.get_velocity_end_range()));
    try_save(ar, ser20::make_nvp("scale_start_range", obj.get_scale_start_range()));
    try_save(ar, ser20::make_nvp("scale_end_range", obj.get_scale_end_range()));
    try_save(ar, ser20::make_nvp("blend_start_range", obj.get_blend_start_range()));
    try_save(ar, ser20::make_nvp("blend_end_range", obj.get_blend_end_range()));
    
    // Colors
    try_save(ar, ser20::make_nvp("rgba_colors", obj.get_rgba_colors()));
    
    // Easing functions
    try_save(ar, ser20::make_nvp("position_easing", static_cast<int>(obj.get_position_easing())));
    try_save(ar, ser20::make_nvp("rgba_easing", static_cast<int>(obj.get_rgba_easing())));
    try_save(ar, ser20::make_nvp("blend_easing", static_cast<int>(obj.get_blend_easing())));
    try_save(ar, ser20::make_nvp("scale_easing", static_cast<int>(obj.get_scale_easing())));
    
    // Texture handle
    try_save(ar, ser20::make_nvp("texture", obj.get_texture()));
}
SAVE_INSTANTIATE(particle_emitter_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(particle_emitter_component, ser20::oarchive_binary_t);

LOAD(particle_emitter_component)
{
    bool enabled{true};
    if(try_load(ar, ser20::make_nvp("enabled", enabled)))
    {
        obj.set_enabled(enabled);
    }
    
    int shape{0}, direction{0};
    if(try_load(ar, ser20::make_nvp("shape", shape)))
    {
        obj.set_shape(static_cast<EmitterShape::Enum>(shape));
    }
    if(try_load(ar, ser20::make_nvp("direction", direction)))
    {
        obj.set_direction(static_cast<EmitterDirection::Enum>(direction));
    }
    
    uint32_t max_particles{1024};
    if(try_load(ar, ser20::make_nvp("max_particles", max_particles)))
    {
        obj.set_max_particles(max_particles);
    }
    
    // Emission properties
    std::chrono::duration<float> emission_lifetime{2.0f};
    if(try_load(ar, ser20::make_nvp("emission_lifetime", emission_lifetime)))
    {
        obj.set_emission_lifetime(emission_lifetime);
    }
    
    float gravity_scale{0.0f};
    if(try_load(ar, ser20::make_nvp("gravity_scale", gravity_scale)))
    {
        obj.set_gravity_scale(gravity_scale);
    }
    
    float emission_rate{50.0f};
    if(try_load(ar, ser20::make_nvp("emission_rate", emission_rate)))
    {
        obj.set_emission_rate(emission_rate);
    }
    
    float temporal_motion{1.0f};
    if(try_load(ar, ser20::make_nvp("temporal_motion", temporal_motion)))
    {
        obj.set_temporal_motion(temporal_motion);
    }
    
    float velocity_damping{0.0f};
    if(try_load(ar, ser20::make_nvp("velocity_damping", velocity_damping)))
    {
        obj.set_velocity_damping(velocity_damping);
    }
    
    math::vec3 force_over_lifetime{0.0f, 0.0f, 0.0f};
    if(try_load(ar, ser20::make_nvp("force_over_lifetime", force_over_lifetime)))
    {
        obj.set_force_over_lifetime(force_over_lifetime);
    }
    
    // Range properties
    std::chrono::duration<float> lifetime{1.0f};
    if(try_load(ar, ser20::make_nvp("lifetime", lifetime)))
    {
        obj.set_lifetime(lifetime);
    }
    
    frange_t velocity_start_range{0.0f, 1.0f};
    if(try_load(ar, ser20::make_nvp("velocity_start_range", velocity_start_range)))
    {
        obj.set_velocity_start_range(velocity_start_range);
    }
    
    frange_t velocity_end_range{2.0f, 3.0f};
    if(try_load(ar, ser20::make_nvp("velocity_end_range", velocity_end_range)))
    {
        obj.set_velocity_end_range(velocity_end_range);
    }
    
    frange_t scale_start_range{0.1f, 0.2f};
    if(try_load(ar, ser20::make_nvp("scale_start_range", scale_start_range)))
    {
        obj.set_scale_start_range(scale_start_range);
    }
    
    frange_t scale_end_range{0.3f, 0.4f};
    if(try_load(ar, ser20::make_nvp("scale_end_range", scale_end_range)))
    {
        obj.set_scale_end_range(scale_end_range);
    }
    
    frange_t blend_start_range{0.8f, 1.0f};
    if(try_load(ar, ser20::make_nvp("blend_start_range", blend_start_range)))
    {
        obj.set_blend_start_range(blend_start_range);
    }
    
    frange_t blend_end_range{0.0f, 0.2f};
    if(try_load(ar, ser20::make_nvp("blend_end_range", blend_end_range)))
    {
        obj.set_blend_end_range(blend_end_range);
    }
    
    // Colors
    std::array<math::color, 5> rgba_colors{
        math::color(0x00ffffff),
        math::color(0xffffffff),
        math::color(0xffffffff),
        math::color(0xffffffff),
        math::color(0x00ffffff)
    };
    if(try_load(ar, ser20::make_nvp("rgba_colors", rgba_colors)))
    {
        obj.set_rgba_colors(rgba_colors);
    }
    
    // Easing functions
    int pos_easing{0}, rgba_easing{0}, blend_easing{0}, scale_easing{0};
    try_load(ar, ser20::make_nvp("position_easing", pos_easing));
    try_load(ar, ser20::make_nvp("rgba_easing", rgba_easing));
    try_load(ar, ser20::make_nvp("blend_easing", blend_easing));
    try_load(ar, ser20::make_nvp("scale_easing", scale_easing));
    obj.set_position_easing(static_cast<bx::Easing::Enum>(pos_easing));
    obj.set_rgba_easing(static_cast<bx::Easing::Enum>(rgba_easing));
    obj.set_blend_easing(static_cast<bx::Easing::Enum>(blend_easing));
    obj.set_scale_easing(static_cast<bx::Easing::Enum>(scale_easing));
    
    // Texture handle
    asset_handle<gfx::texture> texture;
    if(try_load(ar, ser20::make_nvp("texture", texture)))
    {
        obj.set_texture(texture);
    }
}
LOAD_INSTANTIATE(particle_emitter_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(particle_emitter_component, ser20::iarchive_binary_t);

} // namespace unravel