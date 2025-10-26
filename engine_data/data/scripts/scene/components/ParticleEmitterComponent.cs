using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Emitter shape enumeration that matches the C++ EmitterShape::Enum
    /// </summary>
    public enum EmitterShape
    {
        Sphere = 0,
        Hemisphere = 1,
        Circle = 2,
        Box = 3,
        Rect = 4
    }

    /// <summary>
    /// Emitter direction enumeration that matches the C++ EmitterDirection::Enum
    /// </summary>
    public enum EmitterDirection
    {
        Up = 0,
        Outward = 1
    }

    /// <summary>
    /// Represents a component that provides particle emission capabilities for an entity.
    /// </summary>
    public class ParticleEmitterComponent : Component
    {
        /// <summary>
        /// Gets or sets whether the particle emitter is enabled.
        /// </summary>
        public bool enabled
        {
            get
            {
                return internal_m2n_particle_emitter_get_enabled(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_enabled(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the maximum number of particles.
        /// </summary>
        public uint maxParticles
        {
            get
            {
                return internal_m2n_particle_emitter_get_max_particles(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_max_particles(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the emitter shape.
        /// </summary>
        public EmitterShape shape
        {
            get
            {
                return (EmitterShape)internal_m2n_particle_emitter_get_shape(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_shape(owner, (int)value);
            }
        }

        /// <summary>
        /// Gets or sets the emitter direction.
        /// </summary>
        public EmitterDirection direction
        {
            get
            {
                return (EmitterDirection)internal_m2n_particle_emitter_get_direction(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_direction(owner, (int)value);
            }
        }

        /// <summary>
        /// Gets or sets the gravity scale applied to particles.
        /// </summary>
        public float gravityScale
        {
            get
            {
                return internal_m2n_particle_emitter_get_gravity_scale(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_gravity_scale(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the particle emission rate (particles per second).
        /// </summary>
        public float emissionRate
        {
            get
            {
                return internal_m2n_particle_emitter_get_emission_rate(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_emission_rate(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the temporal motion interpolation factor.
        /// </summary>
        public float temporalMotion
        {
            get
            {
                return internal_m2n_particle_emitter_get_temporal_motion(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_temporal_motion(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the velocity damping factor.
        /// </summary>
        public float velocityDamping
        {
            get
            {
                return internal_m2n_particle_emitter_get_velocity_damping(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_velocity_damping(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the blend multiplier for particle opacity.
        /// </summary>
        public float blendMultiplier
        {
            get
            {
                return internal_m2n_particle_emitter_get_blend_multiplier(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_blend_multiplier(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the force applied over particle lifetime.
        /// </summary>
        public Vector3 forceOverLifetime
        {
            get
            {
                return internal_m2n_particle_emitter_get_force_over_lifetime(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_force_over_lifetime(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the emission shape scale.
        /// </summary>
        public Vector3 emissionShapeScale
        {
            get
            {
                return internal_m2n_particle_emitter_get_emission_shape_scale(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_emission_shape_scale(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the emission lifetime in seconds.
        /// </summary>
        public float emissionLifetime
        {
            get
            {
                return internal_m2n_particle_emitter_get_emission_lifetime(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_emission_lifetime(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the particle lifetime in seconds.
        /// </summary>
        public float lifetime
        {
            get
            {
                return internal_m2n_particle_emitter_get_lifetime(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_lifetime(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets the position easing function.
        /// </summary>
        public int positionEasing
        {
            get
            {
                return internal_m2n_particle_emitter_get_position_easing(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_position_easing(owner, value);
            }
        }

        /// <summary>
        /// Gets the current number of active particles.
        /// </summary>
        public uint numParticles
        {
            get
            {
                return internal_m2n_particle_emitter_get_num_particles(owner);
            }
        }

        /// <summary>
        /// Gets whether the emitter is currently playing.
        /// </summary>
        public bool isPlaying
        {
            get
            {
                return internal_m2n_particle_emitter_is_playing(owner);
            }
        }

        /// <summary>
        /// Gets whether the emitter is currently paused.
        /// </summary>
        public bool isPaused
        {
            get
            {
                return internal_m2n_particle_emitter_is_paused(owner);
            }
        }

        /// <summary>
        /// Gets or sets the texture asset used for particles.
        /// </summary>
        public Guid texture
        {
            get
            {
                return internal_m2n_particle_emitter_get_texture(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_texture(owner, value);
            }
        }

        /// <summary>
        /// Gets or sets whether the emitter loops continuously (true) or emits only once (false).
        /// </summary>
        public bool loop
        {
            get
            {
                return internal_m2n_particle_emitter_get_loop(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_loop(owner, value);
            }
        }

        /// <summary>
        /// Starts particle emission.
        /// </summary>
        public void Play()
        {
            internal_m2n_particle_emitter_play(owner);
        }

        /// <summary>
        /// Stops particle emission.
        /// </summary>
        public void Stop()
        {
            internal_m2n_particle_emitter_stop(owner);
        }

        /// <summary>
        /// Stops particle emission and resets the emitter state.
        /// </summary>
        public void StopAndReset()
        {
            internal_m2n_particle_emitter_stop_and_reset(owner);
        }

        /// <summary>
        /// Pauses particle emission.
        /// </summary>
        public void Pause()
        {
            internal_m2n_particle_emitter_pause(owner);
        }

        /// <summary>
        /// Resumes particle emission from a paused state.
        /// </summary>
        public void Resume()
        {
            internal_m2n_particle_emitter_resume(owner);
        }

        /// <summary>
        /// Resets the emitter, clearing all particles and resetting internal state.
        /// </summary>
        public void ResetEmitter()
        {
            internal_m2n_particle_emitter_reset_emitter(owner);
        }

        // Internal call declarations
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_enabled(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_enabled(Entity eid, bool enabled);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_particle_emitter_get_max_particles(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_max_particles(Entity eid, uint maxParticles);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_shape(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_shape(Entity eid, int shape);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_direction(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_direction(Entity eid, int direction);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_gravity_scale(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_gravity_scale(Entity eid, float scale);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_emission_rate(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_emission_rate(Entity eid, float rate);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_temporal_motion(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_temporal_motion(Entity eid, float motion);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_velocity_damping(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_velocity_damping(Entity eid, float damping);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_blend_multiplier(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_blend_multiplier(Entity eid, float multiplier);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_particle_emitter_get_force_over_lifetime(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_force_over_lifetime(Entity eid, Vector3 force);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_particle_emitter_get_emission_shape_scale(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_emission_shape_scale(Entity eid, Vector3 scale);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_emission_lifetime(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_emission_lifetime(Entity eid, float lifetime);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_lifetime(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_lifetime(Entity eid, float lifetime);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_position_easing(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_position_easing(Entity eid, int easing);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_particle_emitter_get_num_particles(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_is_playing(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_is_paused(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_particle_emitter_get_texture(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_texture(Entity eid, Guid texture);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_play(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_stop(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_stop_and_reset(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_pause(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_resume(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_reset_emitter(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_loop(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_loop(Entity eid, bool loop);
    }
}
