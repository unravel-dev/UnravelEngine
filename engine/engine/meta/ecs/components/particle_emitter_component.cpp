#include "particle_emitter_component.hpp"
#include "serialization/serialization.h"

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
        .data<EmitterShape::Box>("Box"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Box"},
            entt::attribute{"pretty_name", "Box"},
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
        .data<&particle_emitter_component::set_lifetime, &particle_emitter_component::get_lifetime>("lifetime"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime"},
            entt::attribute{"pretty_name", "Lifetime"},
            entt::attribute{"tooltip", "How long each particle lives in seconds. Longer lifetimes create more persistent effects."},
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
        .data<&particle_emitter_component::set_emission_shape_scale, &particle_emitter_component::get_emission_shape_scale>("emission_shape_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emission_shape_scale"},
            entt::attribute{"pretty_name", "Emitter Shape Scale"},
            entt::attribute{"tooltip", "Scale of the emission shape. 1.0 = no scaling, 2.0 = double size, 0.5 = half size."},
        })
        .data<&particle_emitter_component::set_direction, &particle_emitter_component::get_direction>("direction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "direction"},
            entt::attribute{"pretty_name", "Emitter Direction"},
            entt::attribute{"tooltip", "Initial direction particles move when spawned. Up = particles move upward, Outward = particles move away from spawn position."},
        })
         .data<&particle_emitter_component::set_velocity_gradient, &particle_emitter_component::get_velocity_gradient>("velocity_gradient"_hs)
         .custom<entt::attributes>(entt::attributes{
             entt::attribute{"name", "velocity_gradient"},
             entt::attribute{"pretty_name", "Velocity Gradient"},
             entt::attribute{"tooltip", "Velocity range gradient over particle lifetime. Controls how particle speed changes from spawn to death."},
             entt::attribute{"group", "Velocity over lifetime"},
             entt::attribute{"step", 0.05f},

         })
        .data<&particle_emitter_component::set_velocity_damping, &particle_emitter_component::get_velocity_damping>("velocity_damping"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "velocity_damping"},
            entt::attribute{"pretty_name", "Velocity Damping"},
            entt::attribute{"tooltip", "Reduces particle velocity over time. 0.0 = no damping (particles maintain speed), 1.0 = full damping (particles stop immediately)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Velocity over lifetime"},

        })
        .data<&particle_emitter_component::set_position_easing, &particle_emitter_component::get_position_easing>("position_easing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "position_easing"},
            entt::attribute{"pretty_name", "Position Easing"},
            entt::attribute{"tooltip", "Curve controlling how particles move from start to end position. Linear = constant speed, EaseIn = slow start, EaseOut = slow end."},
            entt::attribute{"group", "Position over lifetime"},

        })

        .data<&particle_emitter_component::set_force_over_lifetime, &particle_emitter_component::get_force_over_lifetime>("force_over_lifetime"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "force_over_lifetime"},
            entt::attribute{"pretty_name", "Force Over Lifetime"},
            entt::attribute{"tooltip", "Additional force applied to particles throughout their lifetime. Use for wind, magnetism, or other environmental effects. Values are in world units."},
            entt::attribute{"group", "Force over lifetime"},

        })
         .data<&particle_emitter_component::set_scale_gradient, &particle_emitter_component::get_scale_gradient>("scale_gradient"_hs)
         .custom<entt::attributes>(entt::attributes{
             entt::attribute{"name", "scale_gradient"},
             entt::attribute{"pretty_name", "Scale Gradient"},
             entt::attribute{"tooltip", "Scale range gradient over particle lifetime. Controls how particle size changes from spawn to death."},
             entt::attribute{"group", "Size over lifetime"},

         })
        .data<&particle_emitter_component::set_size_by_speed_range, &particle_emitter_component::get_size_by_speed_range>("size_by_speed_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_by_speed_range"},
            entt::attribute{"pretty_name", "Size by Speed Range"},
            entt::attribute{"tooltip", "Size multiplier range based on particle speed. Min = size at slow speed, Max = size at fast speed. Use values like 0.5-2.0 for dramatic effects."},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"group", "Size by Speed"},

        })
        .data<&particle_emitter_component::set_size_by_speed_velocity_range, &particle_emitter_component::get_size_by_speed_velocity_range>("size_by_speed_velocity_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size_by_speed_velocity_range"},
            entt::attribute{"pretty_name", "Size by Speed Velocity Range"},
            entt::attribute{"tooltip", "Velocity range for size mapping. Particles moving at min speed get min size, particles at max speed get max size."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"group", "Size by Speed"},

        })
         .data<&particle_emitter_component::set_blend_gradient, &particle_emitter_component::get_blend_gradient>("blend_gradient"_hs)
         .custom<entt::attributes>(entt::attributes{
             entt::attribute{"name", "blend_gradient"},
             entt::attribute{"pretty_name", "Blend Gradient"},
             entt::attribute{"tooltip", "Opacity range gradient over particle lifetime. Controls how particle transparency changes from spawn to death."},
             entt::attribute{"group", "Opacity over lifetime"},

         })
        .data<&particle_emitter_component::set_blend_multiplier, &particle_emitter_component::get_blend_multiplier>("blend_multiplier"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_multiplier"},
            entt::attribute{"pretty_name", "Blend Multiplier"},
            entt::attribute{"tooltip", "Global blend multiplier for all particles regardless of lifetime. 0.0 = fully transparent, 1.0 = no change, values > 1.0 = enhanced opacity."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Opacity over lifetime"},
        })
         .data<&particle_emitter_component::set_color_gradient, &particle_emitter_component::get_color_gradient>("color_gradient"_hs)
         .custom<entt::attributes>(entt::attributes{
             entt::attribute{"name", "color_gradient"},
             entt::attribute{"pretty_name", "Color Gradient"},
             entt::attribute{"tooltip", "Color gradient defining particle color over lifetime. Colors are interpolated smoothly based on gradient keyframes."},
             entt::attribute{"group", "Color over lifetime"},

         })
        .data<&particle_emitter_component::set_color_by_speed_gradient, &particle_emitter_component::get_color_by_speed_gradient>("color_by_speed_gradient"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color_by_speed_gradient"},
            entt::attribute{"pretty_name", "Color by Speed Gradient"},
            entt::attribute{"tooltip", "Color gradient applied based on particle speed. Slow particles use colors from the start of the gradient, fast particles use colors from the end."},
            entt::attribute{"group", "Color by Speed"},

        })
        .data<&particle_emitter_component::set_color_by_speed_velocity_range, &particle_emitter_component::get_color_by_speed_velocity_range>("color_by_speed_velocity_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color_by_speed_velocity_range"},
            entt::attribute{"pretty_name", "Color by Speed Velocity Range"},
            entt::attribute{"tooltip", "Velocity range for color mapping. Particles moving at min speed get slow color, particles at max speed get fast color."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"group", "Color by Speed"},

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
    try_save(ar, ser20::make_nvp("size_by_speed_range", obj.get_size_by_speed_range()));
    try_save(ar, ser20::make_nvp("size_by_speed_velocity_range", obj.get_size_by_speed_velocity_range()));
    try_save(ar, ser20::make_nvp("color_by_speed_gradient", obj.get_color_by_speed_gradient()));
    try_save(ar, ser20::make_nvp("color_by_speed_velocity_range", obj.get_color_by_speed_velocity_range()));
    
    // Gradient properties
    try_save(ar, ser20::make_nvp("lifetime", obj.get_lifetime()));
    try_save(ar, ser20::make_nvp("velocity_gradient", obj.get_velocity_gradient()));
    try_save(ar, ser20::make_nvp("scale_gradient", obj.get_scale_gradient()));
    try_save(ar, ser20::make_nvp("blend_gradient", obj.get_blend_gradient()));
    try_save(ar, ser20::make_nvp("blend_multiplier", obj.get_blend_multiplier()));
    
    // Colors
    try_save(ar, ser20::make_nvp("color_gradient", obj.get_color_gradient()));

    // Easing functions (only position easing remains)
    try_save(ar, ser20::make_nvp("position_easing", obj.get_position_easing()));
    
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
    
    frange_t size_by_speed_range{1.0f, 1.0f};
    if(try_load(ar, ser20::make_nvp("size_by_speed_range", size_by_speed_range)))
    {
        obj.set_size_by_speed_range(size_by_speed_range);
    }
    
    frange_t size_by_speed_velocity_range{0.0f, 10.0f};
    if(try_load(ar, ser20::make_nvp("size_by_speed_velocity_range", size_by_speed_velocity_range)))
    {
        obj.set_size_by_speed_velocity_range(size_by_speed_velocity_range);
    }
    
    math::gradient<math::color> color_by_speed_gradient;
    if(try_load(ar, ser20::make_nvp("color_by_speed_gradient", color_by_speed_gradient)))
    {
        obj.set_color_by_speed_gradient(color_by_speed_gradient);
    }
    else
    {
        // Legacy loading: try to load old slow/fast colors and convert to gradient
        math::color color_by_speed_slow_color{0xffffffff};
        math::color color_by_speed_fast_color{0xffffffff};
        bool has_slow = try_load(ar, ser20::make_nvp("color_by_speed_slow_color", color_by_speed_slow_color));
        bool has_fast = try_load(ar, ser20::make_nvp("color_by_speed_fast_color", color_by_speed_fast_color));
        
        if(has_slow || has_fast)
        {
            math::gradient<math::color> legacy_gradient;
            legacy_gradient.add_point(color_by_speed_slow_color, 0.0f);
            legacy_gradient.add_point(color_by_speed_fast_color, 1.0f);
            obj.set_color_by_speed_gradient(legacy_gradient);
        }
    }
    
    frange_t color_by_speed_velocity_range{0.0f, 10.0f};
    if(try_load(ar, ser20::make_nvp("color_by_speed_velocity_range", color_by_speed_velocity_range)))
    {
        obj.set_color_by_speed_velocity_range(color_by_speed_velocity_range);
    }
    
    // Gradient properties
    std::chrono::duration<float> lifetime{1.0f};
    if(try_load(ar, ser20::make_nvp("lifetime", lifetime)))
    {
        obj.set_lifetime(lifetime);
    }
    
    math::gradient<frange_t> velocity_gradient;
    if(try_load(ar, ser20::make_nvp("velocity_gradient", velocity_gradient)))
    {
        obj.set_velocity_gradient(velocity_gradient);
    }
    
    math::gradient<frange_t> scale_gradient;
    if(try_load(ar, ser20::make_nvp("scale_gradient", scale_gradient)))
    {
        obj.set_scale_gradient(scale_gradient);
    }
    
    math::gradient<frange_t> blend_gradient;
    if(try_load(ar, ser20::make_nvp("blend_gradient", blend_gradient)))
    {
        obj.set_blend_gradient(blend_gradient);
    }
    
    float blend_multiplier{1.0f};
    if(try_load(ar, ser20::make_nvp("blend_multiplier", blend_multiplier)))
    {
        obj.set_blend_multiplier(blend_multiplier);
    }
    
    // Colors
    // Try to load gradient first (new format)
    math::gradient<math::color> color_gradient;
    if(try_load(ar, ser20::make_nvp("color_gradient", color_gradient)))
    {
        obj.set_color_gradient(color_gradient);
    }
    
    // Easing functions (only position easing remains)
    bx::Easing::Enum position_easing{};
    if(try_load(ar, ser20::make_nvp("position_easing", position_easing)))
    {
        obj.set_position_easing(position_easing);
    }
    
    // Load old easing values for backward compatibility but ignore them
    bx::Easing::Enum blend_easing{}, scale_easing{};
    try_load(ar, ser20::make_nvp("blend_easing", blend_easing));
    try_load(ar, ser20::make_nvp("scale_easing", scale_easing));
    
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