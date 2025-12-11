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
        })
        .data<EmitterDirection::Inward>("Inward"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Inward"},
            entt::attribute{"pretty_name", "Inward"},
        });

    entt::meta_factory<EmitterSpawnLocation::Enum>{}
        .type("EmitterSpawnLocation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "EmitterSpawnLocation"},
            entt::attribute{"pretty_name", "Emitter Spawn Location"},
        })
        .data<EmitterSpawnLocation::Inside>("Inside"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Inside"},
            entt::attribute{"pretty_name", "Inside"},
        })
        .data<EmitterSpawnLocation::Surface>("Surface"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Surface"},
            entt::attribute{"pretty_name", "Surface"},
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

    entt::meta_factory<SimulationSpace::Enum>{}
        .type("SimulationSpace"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "SimulationSpace"},
            entt::attribute{"pretty_name", "Simulation Space"},
        })
        .data<SimulationSpace::World>("World"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "World"},
            entt::attribute{"pretty_name", "World"},
        })
        .data<SimulationSpace::Local>("Local"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Local"},
            entt::attribute{"pretty_name", "Local"},
        });

    entt::meta_factory<TextureMode::Enum>{}
        .type("TextureMode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "TextureMode"},
            entt::attribute{"pretty_name", "Texture Mode"},
        })
        .data<TextureMode::MultiChannel>("MultiChannel"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "MultiChannel"},
            entt::attribute{"pretty_name", "Multi Channel"},
        })
        .data<TextureMode::Mask>("Mask"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Mask"},
            entt::attribute{"pretty_name", "Mask"},
        });

    entt::meta_factory<RenderMode::Enum>{}
        .type("RenderMode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "RenderMode"},
            entt::attribute{"pretty_name", "Render Mode"},
        })
        .data<RenderMode::Billboard>("Billboard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "Billboard"},
            entt::attribute{"pretty_name", "Billboard"},
        })
        .data<RenderMode::HorizontalBillboard>("HorizontalBillboard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "HorizontalBillboard"},
            entt::attribute{"pretty_name", "Horizontal Billboard"},
        })
        .data<RenderMode::VerticalBillboard>("VerticalBillboard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "VerticalBillboard"},
            entt::attribute{"pretty_name", "Vertical Billboard"},
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
        .data<&particle_emitter_component::set_loop, &particle_emitter_component::is_loop>("loop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "loop"},
            entt::attribute{"pretty_name", "Loop"},
            entt::attribute{"tooltip", "Controls whether the emitter loops continuously (true) or emits only once up to max particles (false). Non-looping emitters stop emitting after reaching max particles."},
        })
        .data<&particle_emitter_component::set_start_delay, &particle_emitter_component::get_start_delay>("start_delay"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "start_delay"},
            entt::attribute{"pretty_name", "Start Delay"},
            entt::attribute{"tooltip", "Delay in seconds before particle emission starts. Particles will begin spawning after this delay period."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&particle_emitter_component::set_align_to_direction, &particle_emitter_component::get_align_to_direction>("align_to_direction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "align_to_direction"},
            entt::attribute{"pretty_name", "Align To Direction"},
            entt::attribute{"tooltip", "If enabled, particles rotate to align with their velocity direction. Useful for directional particles like arrows or sparks."},
        })
        .data<&particle_emitter_component::set_pivot, &particle_emitter_component::get_pivot>("pivot"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pivot"},
            entt::attribute{"pretty_name", "Pivot"},
            entt::attribute{"tooltip", "Pivot point for particle rotation and positioning. (0,0) = bottom-left, (0.5,0.5) = center (default), (1,1) = top-right. Affects both rotation and where the particle is anchored."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
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
            entt::attribute{"step", 0.01f},

        })

        .data<nullptr, &particle_emitter_component::get_world_bounds>("world_bounds"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_bounds"},
            entt::attribute{"pretty_name", "World Bounds"},
            entt::attribute{"tooltip", "Bounding box containing all particles in world space. Used for culling and optimization (read-only)."},
        })
        .data<&particle_emitter_component::set_emission_shape_position, &particle_emitter_component::get_emission_shape_position>("emission_shape_position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emission_shape_position"},
            entt::attribute{"pretty_name", "Emitter Shape Position"},
            entt::attribute{"tooltip", "Position offset of the emission shape relative to the emitter transform. Allows offsetting where particles spawn."},
        })
        .data<&particle_emitter_component::set_emission_shape_scale, &particle_emitter_component::get_emission_shape_scale>("emission_shape_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emission_shape_scale"},
            entt::attribute{"pretty_name", "Emitter Shape Scale"},
            entt::attribute{"tooltip", "Scale of the emission shape. 1.0 = no scaling, 2.0 = double size, 0.5 = half size."},
        })
        .data<&particle_emitter_component::set_shape, &particle_emitter_component::get_shape>("shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape"},
            entt::attribute{"pretty_name", "Emitter Shape"},
            entt::attribute{"tooltip", "Geometric shape from which particles are spawned. Sphere = 3D ball, Hemisphere = half sphere, Circle = 2D ring, Disc = filled circle, Rect = rectangle."},
        })
        .data<&particle_emitter_component::set_spawn_location, &particle_emitter_component::get_spawn_location>("spawn_location"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spawn_location"},
            entt::attribute{"pretty_name", "Spawn Location"},
            entt::attribute{"tooltip", "Controls where particles spawn within the emission shape. Inside = particles spawn anywhere inside the shape volume/area, Surface = particles spawn only on the surface/perimeter of the shape."},
        })
        .data<&particle_emitter_component::set_direction, &particle_emitter_component::get_direction>("direction"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "direction"},
            entt::attribute{"pretty_name", "Emitter Direction"},
            entt::attribute{"tooltip", "Initial direction particles move when spawned. Up = particles move upward, Outward = particles move away from spawn position, Inward = particles move towards spawn position (only visible in 3D space)."},
        })
        .data<&particle_emitter_component::set_simulation_space, &particle_emitter_component::get_simulation_space>("simulation_space"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "simulation_space"},
            entt::attribute{"pretty_name", "Simulation Space"},
            entt::attribute{"tooltip", "Controls whether particles are simulated in world space or local space."},
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
        .data<&particle_emitter_component::set_initial_scale_3d, &particle_emitter_component::get_initial_scale_3d>("initial_scale_3d"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "initial_scale_3d"},
            entt::attribute{"pretty_name", "Initial 3D Scale"},
            entt::attribute{"tooltip", "3D scale for particles. Allows creating rectangular particles (e.g., 2,1,1 for wide particles, 1,2,1 for tall particles). Default: 1,1,1 (square)."},
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
        .data<&particle_emitter_component::set_color_gradient, &particle_emitter_component::get_color_gradient>("color_gradient"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color_gradient"},
            entt::attribute{"pretty_name", "Color Gradient"},
            entt::attribute{"tooltip", "Color gradient defining particle color over lifetime. Colors are interpolated smoothly based on gradient keyframes."},
            entt::attribute{"group", "Color over lifetime"},

        })
        .data<&particle_emitter_component::set_opacity, &particle_emitter_component::get_opacity>("opacity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "opacity"},
            entt::attribute{"pretty_name", "Opacity"},
            entt::attribute{"tooltip", "Global opacity for all particles regardless of lifetime. 0.0 = fully transparent, 1.0 = no change, values > 1.0 = enhanced opacity."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
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
            entt::attribute{"group", "Color by Speed"},

        })
        .data<&particle_emitter_component::set_lifetime_by_emitter_speed_gradient, &particle_emitter_component::get_lifetime_by_emitter_speed_gradient>("lifetime_by_emitter_speed_gradient"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime_by_emitter_speed_gradient"},
            entt::attribute{"pretty_name", "Lifetime by Emitter Speed Gradient"},
            entt::attribute{"tooltip", "Lifetime multiplier gradient based on emitter movement speed. Allows particles to live longer/shorter based on how fast the emitter is moving."},
            entt::attribute{"group", "Lifetime by Emitter Speed"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.01f},
        })
        .data<&particle_emitter_component::set_lifetime_by_emitter_speed_range, &particle_emitter_component::get_lifetime_by_emitter_speed_range>("lifetime_by_emitter_speed_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime_by_emitter_speed_range"},
            entt::attribute{"pretty_name", "Lifetime by Emitter Speed Range"},
            entt::attribute{"tooltip", "Emitter speed range for lifetime mapping. Emitters moving at min speed get slow gradient value, emitters at max speed get fast gradient value."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Lifetime by Emitter Speed"},
        })
        .data<&particle_emitter_component::set_texture, &particle_emitter_component::get_texture>("texture"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture"},
            entt::attribute{"pretty_name", "Texture"},
            entt::attribute{"tooltip", "Texture asset used to render each particle. Should be a square texture with alpha channel for best results. Common formats: smoke, fire, sparkle, etc."},
            entt::attribute{"group", "Texture"},
        })
        .data<&particle_emitter_component::set_texture_mode, &particle_emitter_component::get_texture_mode>("texture_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_mode"},
            entt::attribute{"pretty_name", "Texture Mode"},
            entt::attribute{"tooltip", "Texture mode determines how the texture is interpreted. MultiChannel = standard RGBA texture, Mask = black/white mask where black = transparent, white = opaque (particle color used)."},
            entt::attribute{"group", "Texture"},
        })
        .data<&particle_emitter_component::set_render_mode, &particle_emitter_component::get_render_mode>("render_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "render_mode"},
            entt::attribute{"pretty_name", "Render Mode"},
            entt::attribute{"tooltip", "Render orientation mode. Billboard = always face camera, Horizontal = rotate around Y axis only (stay horizontal), Vertical = stay vertical (perpendicular to ground)."},
            entt::attribute{"group", "Rendering"},
        })
        .data<&particle_emitter_component::set_texture_sheet_tiles, &particle_emitter_component::get_texture_sheet_tiles>("texture_sheet_tiles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_sheet_tiles"},
            entt::attribute{"pretty_name", "Texture Sheet Tiles"},
            entt::attribute{"tooltip", "Number of tiles in the texture sheet grid (X columns, Y rows). For example, a 4x4 grid contains 16 animation frames. Set to 1x1 to disable."},
            entt::attribute{"group", "Texture"},
            entt::attribute{"step", 1.0f},
            entt::attribute{"min", 1.0f},

        })
        .data<&particle_emitter_component::set_texture_sheet_cycles, &particle_emitter_component::get_texture_sheet_cycles>("texture_sheet_cycles"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_sheet_cycles"},
            entt::attribute{"pretty_name", "Animation Cycles"},
            entt::attribute{"tooltip", "Number of times the animation loops over particle lifetime. 0 = disabled, 1 = play once, 2 = play twice, etc. Higher values make the animation play faster."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"group", "Texture"},
        })
        .data<&particle_emitter_component::set_texture_sheet_randomize, &particle_emitter_component::get_texture_sheet_randomize>("texture_sheet_randomize"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_sheet_randomize"},
            entt::attribute{"pretty_name", "Randomize Start Frame"},
            entt::attribute{"tooltip", "When enabled, each particle starts at a random frame in the texture sheet animation instead of frame 0. Creates visual variety."},
            entt::attribute{"group", "Texture"},
        });

}

SAVE(particle_emitter_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.is_enabled()));
    try_save(ar, ser20::make_nvp("shape", obj.get_shape()));
    try_save(ar, ser20::make_nvp("direction", obj.get_direction()));
    try_save(ar, ser20::make_nvp("spawn_location", obj.get_spawn_location()));
    try_save(ar, ser20::make_nvp("simulation_space", obj.get_simulation_space()));
    try_save(ar, ser20::make_nvp("max_particles", obj.get_max_particles()));
    
    
    // Emission properties
    try_save(ar, ser20::make_nvp("emission_lifetime", obj.get_emission_lifetime()));
    try_save(ar, ser20::make_nvp("emission_shape_position", obj.get_emission_shape_position()));
    try_save(ar, ser20::make_nvp("emission_shape_scale", obj.get_emission_shape_scale()));
    try_save(ar, ser20::make_nvp("gravity_scale", obj.get_gravity_scale()));
    try_save(ar, ser20::make_nvp("emission_rate", obj.get_emission_rate()));
    try_save(ar, ser20::make_nvp("temporal_motion", obj.get_temporal_motion()));
    try_save(ar, ser20::make_nvp("velocity_damping", obj.get_velocity_damping()));
    try_save(ar, ser20::make_nvp("force_over_lifetime", obj.get_force_over_lifetime()));
    try_save(ar, ser20::make_nvp("size_by_speed_range", obj.get_size_by_speed_range()));
    try_save(ar, ser20::make_nvp("size_by_speed_velocity_range", obj.get_size_by_speed_velocity_range()));
    try_save(ar, ser20::make_nvp("color_by_speed_gradient", obj.get_color_by_speed_gradient()));
    try_save(ar, ser20::make_nvp("color_by_speed_velocity_range", obj.get_color_by_speed_velocity_range()));
    try_save(ar, ser20::make_nvp("lifetime_by_emitter_speed_gradient", obj.get_lifetime_by_emitter_speed_gradient()));
    try_save(ar, ser20::make_nvp("lifetime_by_emitter_speed_range", obj.get_lifetime_by_emitter_speed_range()));
    
    // Gradient properties
    try_save(ar, ser20::make_nvp("lifetime", obj.get_lifetime()));
    try_save(ar, ser20::make_nvp("velocity_gradient", obj.get_velocity_gradient()));
    try_save(ar, ser20::make_nvp("scale_gradient", obj.get_scale_gradient()));
    try_save(ar, ser20::make_nvp("initial_scale_3d", obj.get_initial_scale_3d()));
    try_save(ar, ser20::make_nvp("opacity", obj.get_opacity()));
    
    // Colors
    try_save(ar, ser20::make_nvp("color_gradient", obj.get_color_gradient()));

    // Easing functions (only position easing remains)
    try_save(ar, ser20::make_nvp("position_easing", obj.get_position_easing()));
    
    // Texture handle
    try_save(ar, ser20::make_nvp("texture", obj.get_texture()));
    try_save(ar, ser20::make_nvp("texture_mode", obj.get_texture_mode()));
    try_save(ar, ser20::make_nvp("render_mode", obj.get_render_mode()));
    
    // Texture sheet animation
    try_save(ar, ser20::make_nvp("texture_sheet_tiles", obj.get_texture_sheet_tiles()));
    try_save(ar, ser20::make_nvp("texture_sheet_cycles", obj.get_texture_sheet_cycles()));
    try_save(ar, ser20::make_nvp("texture_sheet_randomize", obj.get_texture_sheet_randomize()));
    
    // Loop control
    try_save(ar, ser20::make_nvp("loop", obj.is_loop()));
    try_save(ar, ser20::make_nvp("start_delay", obj.get_start_delay()));
    try_save(ar, ser20::make_nvp("align_to_direction", obj.get_align_to_direction()));
    
    // Pivot
    try_save(ar, ser20::make_nvp("pivot", obj.get_pivot()));
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
    
    EmitterShape::Enum shape{EmitterShape::Sphere};
    EmitterDirection::Enum direction{EmitterDirection::Up};
    if(try_load(ar, ser20::make_nvp("shape", shape)))
    {
        obj.set_shape(shape);
    }
    if(try_load(ar, ser20::make_nvp("direction", direction)))
    {
        obj.set_direction(direction);
    }

    EmitterSpawnLocation::Enum spawn_location{EmitterSpawnLocation::Inside};
    if(try_load(ar, ser20::make_nvp("spawn_location", spawn_location)))
    {
        obj.set_spawn_location(spawn_location);
    }

    SimulationSpace::Enum simulation_space{SimulationSpace::World};
    if(try_load(ar, ser20::make_nvp("simulation_space", simulation_space)))
    {
        obj.set_simulation_space(simulation_space);
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
    
    math::vec3 emission_shape_position{0.0f, 0.0f, 0.0f};
    if(try_load(ar, ser20::make_nvp("emission_shape_position", emission_shape_position)))
    {
        obj.set_emission_shape_position(emission_shape_position);
    }
    
    math::vec3 emission_shape_scale{1.0f, 1.0f, 1.0f};
    if(try_load(ar, ser20::make_nvp("emission_shape_scale", emission_shape_scale)))
    {
        obj.set_emission_shape_scale(emission_shape_scale);
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
    
    math::gradient<float> lifetime_by_emitter_speed_gradient;
    if(try_load(ar, ser20::make_nvp("lifetime_by_emitter_speed_gradient", lifetime_by_emitter_speed_gradient)))
    {
        obj.set_lifetime_by_emitter_speed_gradient(lifetime_by_emitter_speed_gradient);
    }
    
    frange_t lifetime_by_emitter_speed_range{0.0f, 10.0f};
    if(try_load(ar, ser20::make_nvp("lifetime_by_emitter_speed_range", lifetime_by_emitter_speed_range)))
    {
        obj.set_lifetime_by_emitter_speed_range(lifetime_by_emitter_speed_range);
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
    
    math::vec3 initial_scale_3d{1.0f, 1.0f, 1.0f};
    if(try_load(ar, ser20::make_nvp("initial_scale_3d", initial_scale_3d)))
    {
        obj.set_initial_scale_3d(initial_scale_3d);
    }
    // Backward compatibility: try loading old name
    else if(try_load(ar, ser20::make_nvp("particle_scale_3d", initial_scale_3d)))
    {
        obj.set_initial_scale_3d(initial_scale_3d);
    }

    
    float opacity{1.0f};
    if(try_load(ar, ser20::make_nvp("opacity", opacity)))
    {
        obj.set_opacity(opacity);
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
    
    TextureMode::Enum texture_mode{TextureMode::MultiChannel};
    if(try_load(ar, ser20::make_nvp("texture_mode", texture_mode)))
    {
        obj.set_texture_mode(texture_mode);
    }
    
    RenderMode::Enum render_mode{RenderMode::Billboard};
    if(try_load(ar, ser20::make_nvp("render_mode", render_mode)))
    {
        obj.set_render_mode(render_mode);
    }
    // Backward compatibility: also check for old "billboard_mode" name
    RenderMode::Enum billboard_mode{RenderMode::Billboard};
    if(try_load(ar, ser20::make_nvp("billboard_mode", billboard_mode)))
    {
        obj.set_render_mode(billboard_mode);
    }
    
    // Texture sheet animation
    math::vec2 texture_sheet_tiles{1.0f, 1.0f};
    if(try_load(ar, ser20::make_nvp("texture_sheet_tiles", texture_sheet_tiles)))
    {
        obj.set_texture_sheet_tiles(texture_sheet_tiles);
    }
    
    float texture_sheet_cycles{0.0f};
    if(try_load(ar, ser20::make_nvp("texture_sheet_cycles", texture_sheet_cycles)))
    {
        obj.set_texture_sheet_cycles(texture_sheet_cycles);
    }
    
    bool texture_sheet_randomize{false};
    if(try_load(ar, ser20::make_nvp("texture_sheet_randomize", texture_sheet_randomize)))
    {
        obj.set_texture_sheet_randomize(texture_sheet_randomize);
    }
    
    // Loop control
    bool loop{true}; // Default to true for backward compatibility
    if(try_load(ar, ser20::make_nvp("loop", loop)))
    {
        obj.set_loop(loop);
    }
    
    std::chrono::duration<float> start_delay{0.0f}; // Default to 0 for backward compatibility
    if(try_load(ar, ser20::make_nvp("start_delay", start_delay)))
    {
        obj.set_start_delay(start_delay);
    }
    
    bool align_to_direction{false}; // Default to false for backward compatibility
    if(try_load(ar, ser20::make_nvp("align_to_direction", align_to_direction)))
    {
        obj.set_align_to_direction(align_to_direction);
    }
    
    math::vec2 pivot{0.5f, 0.5f}; // Default to center for backward compatibility
    if(try_load(ar, ser20::make_nvp("pivot", pivot)))
    {
        obj.set_pivot(pivot);
    }
}
LOAD_INSTANTIATE(particle_emitter_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(particle_emitter_component, ser20::iarchive_binary_t);

} // namespace unravel