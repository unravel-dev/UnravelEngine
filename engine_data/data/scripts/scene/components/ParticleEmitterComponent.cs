using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

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
        Outward = 1,
        Inward = 2
    }

    /// <summary>
    /// Particle simulation/pack backend. Matches C++ ps_soa::particle_sim_backend.
    /// </summary>
    public enum ParticleSimBackend
    {
        CPU = 0,
        GPU = 1
    }

    /// <summary>
    /// Whether an emitter keeps simulating while no camera sees it. Matches C++ particle_emitter_component::culling_mode.
    /// </summary>
    public enum ParticleCullingMode
    {
        /// <summary>Simulates every frame.</summary>
        AlwaysSimulate = 0,
        /// <summary>Freezes while no camera sees the emitter's bounds.</summary>
        RendererBased = 1
    }

    /// <summary>
    /// Space particles live in. Matches C++ ps_soa::simulation_space.
    /// </summary>
    public enum ParticleSimulationSpace
    {
        /// <summary>Particles stay where they spawned when the emitter moves.</summary>
        World = 0,
        /// <summary>Particles move with the emitter.</summary>
        Local = 1
    }

    /// <summary>
    /// Where in the emission shape particles spawn. Matches C++ ps_soa::spawn_location.
    /// </summary>
    public enum ParticleSpawnLocation
    {
        Inside = 0,
        Surface = 1
    }

    /// <summary>
    /// How particle quads face the camera. Matches C++ ps_soa::render_mode.
    /// </summary>
    public enum ParticleRenderMode
    {
        /// <summary>Faces the camera.</summary>
        Billboard = 0,
        /// <summary>Lies flat, facing up.</summary>
        HorizontalBillboard = 1,
        /// <summary>Stands upright, turning around the vertical axis.</summary>
        VerticalBillboard = 2
    }

    /// <summary>
    /// How particles blend over the scene. Matches C++ ps_soa::blend_mode.
    /// </summary>
    public enum ParticleBlendMode
    {
        Normal = 0,
        Additive = 1,
        Multiply = 2
    }

    /// <summary>
    /// How the particle texture is read. Matches C++ ps_soa::texture_mode.
    /// </summary>
    public enum ParticleTextureMode
    {
        /// <summary>The texture's color and alpha.</summary>
        MultiChannel = 0,
        /// <summary>The texture's red channel as alpha.</summary>
        Mask = 1
    }

    /// <summary>
    /// Easing of particle positions over their lifetime. The values match the engine's easing table.
    /// </summary>
    public enum ParticleEasing
    {
        Linear = 0,
        Step = 1,
        SmoothStep = 2,
        InQuad = 3,
        OutQuad = 4,
        InOutQuad = 5,
        InCubic = 7,
        OutCubic = 8,
        InOutCubic = 9,
        InSine = 19,
        OutSine = 20,
        InOutSine = 21,
        InExpo = 23,
        OutExpo = 24,
        InOutExpo = 25,
        InElastic = 31,
        OutElastic = 32,
        InOutElastic = 33,
        InBack = 35,
        OutBack = 36,
        InOutBack = 37,
        InBounce = 39,
        OutBounce = 40,
        InOutBounce = 41
    }

    /// <summary>
    /// Overrides for the particles of one <see cref="ParticleEmitterComponent.Emit(int, EmitParams)"/>
    /// burst. Only the fields that were set apply; the others come from the emitter.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct EmitParams
    {
        private const uint PositionFlag = 1u << 0;
        private const uint VelocityFlag = 1u << 1;
        private const uint ColorFlag = 1u << 2;
        private const uint SizeFlag = 1u << 3;
        private const uint LifetimeFlag = 1u << 4;

        private Vector3 position_;
        private Vector3 velocity_;
        private Color color_;
        private float size_;
        private float lifetime_;
        private uint mask_;

        /// <summary>
        /// Spawn point in simulation space: world space, or the emitter's local space when
        /// simulationSpace is Local.
        /// </summary>
        public Vector3 position
        {
            get => position_;
            set { position_ = value; mask_ |= PositionFlag; }
        }

        /// <summary>
        /// Initial velocity in units per second, in simulation space. Gravity, force and damping still apply.
        /// </summary>
        public Vector3 velocity
        {
            get => velocity_;
            set { velocity_ = value; mask_ |= VelocityFlag; }
        }

        /// <summary>
        /// Tint multiplied into the color over lifetime.
        /// </summary>
        public Color color
        {
            get => color_;
            set { color_ = value; mask_ |= ColorFlag; }
        }

        /// <summary>
        /// Start size; the end size keeps the scale gradient's end to start ratio.
        /// </summary>
        public float size
        {
            get => size_;
            set { size_ = value; mask_ |= SizeFlag; }
        }

        /// <summary>
        /// Lifetime in seconds.
        /// </summary>
        public float lifetime
        {
            get => lifetime_;
            set { lifetime_ = value; mask_ |= LifetimeFlag; }
        }
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
        /// Gets or sets the particle simulation backend (CPU batched gather vs GPU compute pack).
        /// </summary>
        public ParticleSimBackend simulationBackend
        {
            get
            {
                return (ParticleSimBackend)internal_m2n_particle_emitter_get_simulation_backend(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_simulation_backend(owner, (int)value);
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
        /// Gets or sets the opacity for particle opacity.
        /// </summary>
        public float opacity
        {
            get
            {
                return internal_m2n_particle_emitter_get_opacity(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_opacity(owner, value);
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
        /// Gets or sets the easing of particle positions over their lifetime.
        /// </summary>
        public ParticleEasing positionEasing
        {
            get
            {
                return (ParticleEasing)internal_m2n_particle_emitter_get_position_easing(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_position_easing(owner, (int)value);
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
        /// Whether the emitter keeps simulating while no camera sees it.
        /// </summary>
        public ParticleCullingMode cullingMode
        {
            get => (ParticleCullingMode)internal_m2n_particle_emitter_get_culling_mode(owner);
            set => internal_m2n_particle_emitter_set_culling_mode(owner, (int)value);
        }

        /// <summary>
        /// Whether particles live in world space or move with the emitter. Changing it clears the live particles.
        /// </summary>
        public ParticleSimulationSpace simulationSpace
        {
            get => (ParticleSimulationSpace)internal_m2n_particle_emitter_get_simulation_space(owner);
            set => internal_m2n_particle_emitter_set_simulation_space(owner, (int)value);
        }

        /// <summary>
        /// Seconds between Play and the first spawn. Changing it clears the live particles.
        /// </summary>
        public float startDelay
        {
            get => internal_m2n_particle_emitter_get_start_delay(owner);
            set => internal_m2n_particle_emitter_set_start_delay(owner, value);
        }

        /// <summary>
        /// Whether particles spawn inside the shape or on its surface.
        /// </summary>
        public ParticleSpawnLocation spawnLocation
        {
            get => (ParticleSpawnLocation)internal_m2n_particle_emitter_get_spawn_location(owner);
            set => internal_m2n_particle_emitter_set_spawn_location(owner, (int)value);
        }

        /// <summary>
        /// Offset of the emission shape from the emitter, before the shape scale.
        /// </summary>
        public Vector3 emissionShapePosition
        {
            get => internal_m2n_particle_emitter_get_emission_shape_position(owner);
            set => internal_m2n_particle_emitter_set_emission_shape_position(owner, value);
        }

        /// <summary>
        /// Per-axis multiplier of the particle size.
        /// </summary>
        public Vector3 initialScale3D
        {
            get => internal_m2n_particle_emitter_get_initial_scale_3d(owner);
            set => internal_m2n_particle_emitter_set_initial_scale_3d(owner, value);
        }

        /// <summary>
        /// Start angle of each particle in its quad's plane, degrees, random between min and max.
        /// </summary>
        public Range<float> rotation
        {
            get => internal_m2n_particle_emitter_get_rotation(owner);
            set => internal_m2n_particle_emitter_set_rotation(owner, value);
        }

        /// <summary>
        /// Spin in degrees per second, random between min and max.
        /// </summary>
        public Range<float> rotationSpeed
        {
            get => internal_m2n_particle_emitter_get_rotation_speed(owner);
            set => internal_m2n_particle_emitter_set_rotation_speed(owner, value);
        }

        /// <summary>
        /// Whether half the particles spin the other way.
        /// </summary>
        public bool randomRotationDirection
        {
            get => internal_m2n_particle_emitter_get_random_rotation_direction(owner);
            set => internal_m2n_particle_emitter_set_random_rotation_direction(owner, value);
        }

        /// <summary>
        /// Soft particles: metres over which a particle fades where it meets the scene behind it.
        /// 0 keeps the hard edge.
        /// </summary>
        public float softFadeDistance
        {
            get => internal_m2n_particle_emitter_get_soft_fade_distance(owner);
            set => internal_m2n_particle_emitter_set_soft_fade_distance(owner, value);
        }

        /// <summary>
        /// A looping emitter starts full, as if it had been running for a whole particle lifetime,
        /// when it first plays after it is created or reset.
        /// </summary>
        public bool prewarm
        {
            get => internal_m2n_particle_emitter_get_prewarm(owner);
            set => internal_m2n_particle_emitter_set_prewarm(owner, value);
        }

        /// <summary>
        /// Whether the emitter plays from the moment it is created (scene load, play start,
        /// instantiation); off leaves it stopped until Play(). Setting it on a running emitter only
        /// changes what the next created copy does.
        /// </summary>
        public bool autoPlay
        {
            get => internal_m2n_particle_emitter_get_auto_play(owner);
            set => internal_m2n_particle_emitter_set_auto_play(owner, value);
        }

        /// <summary>
        /// Draw order against other emitters, whatever their blend mode: lower values draw first and
        /// higher ones composite on top. Emitters of equal priority draw back to front by distance.
        /// </summary>
        public int sortPriority
        {
            get => internal_m2n_particle_emitter_get_sort_priority(owner);
            set => internal_m2n_particle_emitter_set_sort_priority(owner, value);
        }

        /// <summary>
        /// Whether texture sheet frames crossfade into each other instead of stepping.
        /// </summary>
        public bool frameBlending
        {
            get => internal_m2n_particle_emitter_get_frame_blending(owner);
            set => internal_m2n_particle_emitter_set_frame_blending(owner, value);
        }

        /// <summary>
        /// Size multiplier at the slow and fast ends of sizeBySpeedVelocityRange.
        /// </summary>
        public Range<float> sizeBySpeedRange
        {
            get => internal_m2n_particle_emitter_get_size_by_speed_range(owner);
            set => internal_m2n_particle_emitter_set_size_by_speed_range(owner, value);
        }

        /// <summary>
        /// Particle speeds (units per second) that map to sizeBySpeedRange.
        /// </summary>
        public Range<float> sizeBySpeedVelocityRange
        {
            get => internal_m2n_particle_emitter_get_size_by_speed_velocity_range(owner);
            set => internal_m2n_particle_emitter_set_size_by_speed_velocity_range(owner, value);
        }

        /// <summary>
        /// HDR multiplier of the particle color.
        /// </summary>
        public float colorIntensity
        {
            get => internal_m2n_particle_emitter_get_color_intensity(owner);
            set => internal_m2n_particle_emitter_set_color_intensity(owner, value);
        }

        /// <summary>
        /// Particle speeds (units per second) that map to colorBySpeedGradient.
        /// </summary>
        public Range<float> colorBySpeedVelocityRange
        {
            get => internal_m2n_particle_emitter_get_color_by_speed_velocity_range(owner);
            set => internal_m2n_particle_emitter_set_color_by_speed_velocity_range(owner, value);
        }

        /// <summary>
        /// Emitter speeds (units per second) that map to lifetimeByEmitterSpeedGradient.
        /// </summary>
        public Range<float> lifetimeByEmitterSpeedRange
        {
            get => internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_range(owner);
            set => internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_range(owner, value);
        }

        /// <summary>
        /// How particle quads face the camera.
        /// </summary>
        public ParticleRenderMode renderMode
        {
            get => (ParticleRenderMode)internal_m2n_particle_emitter_get_render_mode(owner);
            set => internal_m2n_particle_emitter_set_render_mode(owner, (int)value);
        }

        /// <summary>
        /// Whether particles turn so the texture's top points along their motion:
        /// draw directional art pointing up.
        /// </summary>
        public bool alignToDirection
        {
            get => internal_m2n_particle_emitter_get_align_to_direction(owner);
            set => internal_m2n_particle_emitter_set_align_to_direction(owner, value);
        }

        /// <summary>
        /// Point of the particle quad that sits on the particle position (0.5, 0.5 is the center).
        /// </summary>
        public Vector2 pivot
        {
            get => internal_m2n_particle_emitter_get_pivot(owner);
            set => internal_m2n_particle_emitter_set_pivot(owner, value);
        }

        /// <summary>
        /// Whether the texture is used as color or as an alpha mask.
        /// </summary>
        public ParticleTextureMode textureMode
        {
            get => (ParticleTextureMode)internal_m2n_particle_emitter_get_texture_mode(owner);
            set => internal_m2n_particle_emitter_set_texture_mode(owner, (int)value);
        }

        /// <summary>
        /// Columns and rows of the texture sheet.
        /// </summary>
        public Vector2 textureSheetTiles
        {
            get => internal_m2n_particle_emitter_get_texture_sheet_tiles(owner);
            set => internal_m2n_particle_emitter_set_texture_sheet_tiles(owner, value);
        }

        /// <summary>
        /// How many times a particle plays the texture sheet over its lifetime.
        /// </summary>
        public float textureSheetCycles
        {
            get => internal_m2n_particle_emitter_get_texture_sheet_cycles(owner);
            set => internal_m2n_particle_emitter_set_texture_sheet_cycles(owner, value);
        }

        /// <summary>
        /// Whether each particle starts on a random frame of the texture sheet.
        /// </summary>
        public bool textureSheetRandomize
        {
            get => internal_m2n_particle_emitter_get_texture_sheet_randomize(owner);
            set => internal_m2n_particle_emitter_set_texture_sheet_randomize(owner, value);
        }

        /// <summary>
        /// How particles blend over the scene.
        /// </summary>
        public ParticleBlendMode blendMode
        {
            get => (ParticleBlendMode)internal_m2n_particle_emitter_get_blend_mode(owner);
            set => internal_m2n_particle_emitter_set_blend_mode(owner, (int)value);
        }

        /// <summary>
        /// Color over the particle lifetime (time 0 to 1). Colors are picker (sRGB) values.
        /// </summary>
        public Gradient colorGradient
        {
            get => new Gradient(internal_m2n_particle_emitter_get_color_gradient(owner).ToStructArray<GradientKey>());
            set => internal_m2n_particle_emitter_set_color_gradient(owner, RequireKeys(value?.keys, nameof(colorGradient)).ToByteArray());
        }

        /// <summary>
        /// Color multiplier over colorBySpeedVelocityRange (time 0 = slow end, 1 = fast end).
        /// </summary>
        public Gradient colorBySpeedGradient
        {
            get => new Gradient(internal_m2n_particle_emitter_get_color_by_speed_gradient(owner).ToStructArray<GradientKey>());
            set => internal_m2n_particle_emitter_set_color_by_speed_gradient(owner, RequireKeys(value?.keys, nameof(colorBySpeedGradient)).ToByteArray());
        }

        /// <summary>
        /// Distance a particle travels along its direction over its lifetime, random between min and
        /// max. Only the value at time 1 is used.
        /// </summary>
        public MinMaxCurve velocityGradient
        {
            get => new MinMaxCurve(internal_m2n_particle_emitter_get_velocity_gradient(owner).ToStructArray<MinMaxKey>());
            set => internal_m2n_particle_emitter_set_velocity_gradient(owner, RequireKeys(value?.keys, nameof(velocityGradient)).ToByteArray());
        }

        /// <summary>
        /// Particle size, random between min and max at time 0 (birth) and at time 1 (death), with
        /// the size interpolated in between.
        /// </summary>
        public MinMaxCurve scaleGradient
        {
            get => new MinMaxCurve(internal_m2n_particle_emitter_get_scale_gradient(owner).ToStructArray<MinMaxKey>());
            set => internal_m2n_particle_emitter_set_scale_gradient(owner, RequireKeys(value?.keys, nameof(scaleGradient)).ToByteArray());
        }

        /// <summary>
        /// Lifetime multiplier over lifetimeByEmitterSpeedRange (time 0 = slow end, 1 = fast end).
        /// </summary>
        public Curve lifetimeByEmitterSpeedGradient
        {
            get => new Curve(internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_gradient(owner).ToStructArray<CurveKey>());
            set => internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_gradient(owner, RequireKeys(value?.keys, nameof(lifetimeByEmitterSpeedGradient)).ToByteArray());
        }

        /// <summary>
        /// World-space bounds of the live particles and the region new ones appear in.
        /// </summary>
        public Bounds worldBounds => internal_m2n_particle_emitter_get_world_bounds(owner);

        /// <summary>
        /// Whether the emitter is neither playing nor paused. Its last particles may still be alive.
        /// </summary>
        public bool isStopped => internal_m2n_particle_emitter_is_stopped(owner);

        /// <summary>
        /// Whether the emitter advanced this frame (false while culling froze it).
        /// </summary>
        public bool isSimulated => internal_m2n_particle_emitter_is_simulated(owner);

        /// <summary>
        /// Whether the emitter is playing or still has live particles.
        /// </summary>
        public bool isAlive => isPlaying || numParticles > 0;

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

        /// <summary>
        /// Starts emission here and, when @p withChildren, on every emitter below this entity.
        /// </summary>
        public void Play(bool withChildren)
        {
            Play();
            if (withChildren)
            {
                internal_m2n_particle_emitter_play_sub_emitters(owner);
            }
        }

        /// <summary>
        /// Stops emission here and, when @p withChildren, on every emitter below this entity.
        /// </summary>
        public void Stop(bool withChildren)
        {
            Stop();
            if (withChildren)
            {
                internal_m2n_particle_emitter_stop_sub_emitters(owner);
            }
        }

        /// <summary>
        /// Stops and clears this emitter and, when @p withChildren, every emitter below this entity.
        /// </summary>
        public void StopAndReset(bool withChildren)
        {
            StopAndReset();
            if (withChildren)
            {
                internal_m2n_particle_emitter_stop_and_reset_sub_emitters(owner);
            }
        }

        /// <summary>
        /// Pauses this emitter and, when @p withChildren, every emitter below this entity.
        /// </summary>
        public void Pause(bool withChildren)
        {
            Pause();
            if (withChildren)
            {
                internal_m2n_particle_emitter_pause_sub_emitters(owner);
            }
        }

        /// <summary>
        /// Resumes this emitter and, when @p withChildren, every emitter below this entity.
        /// </summary>
        public void Resume(bool withChildren)
        {
            Resume();
            if (withChildren)
            {
                internal_m2n_particle_emitter_resume_sub_emitters(owner);
            }
        }

        /// <summary>
        /// Spawns @p count particles at the next update, whatever the emission rate, start delay,
        /// loop or play state; the capacity (maxParticles) still caps them. A burst wakes an emitter
        /// that culling froze; a disabled emitter ignores it.
        /// </summary>
        public void Emit(int count)
        {
            Emit(count, new EmitParams());
        }

        /// <summary>
        /// Spawns @p count particles like <see cref="Emit(int)"/>, with the overrides set in @p parameters.
        /// </summary>
        public void Emit(int count, EmitParams parameters)
        {
            if (count <= 0)
            {
                return;
            }
            internal_m2n_particle_emitter_emit(owner, (uint)count, parameters);
        }

        private static T[] RequireKeys<T>(T[] keys, string property)
        {
            if (keys == null || keys.Length == 0)
            {
                throw new ArgumentException($"{property} needs at least one key", property);
            }
            return keys;
        }

        // Internal call declarations
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_culling_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_culling_mode(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_simulation_space(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_simulation_space(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_start_delay(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_start_delay(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_spawn_location(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_spawn_location(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_particle_emitter_get_emission_shape_position(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_emission_shape_position(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_particle_emitter_get_initial_scale_3d(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_initial_scale_3d(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_rotation(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_rotation(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_rotation_speed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_rotation_speed(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_random_rotation_direction(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_random_rotation_direction(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_soft_fade_distance(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_soft_fade_distance(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_prewarm(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_prewarm(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_auto_play(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_auto_play(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_sort_priority(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_sort_priority(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_frame_blending(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_frame_blending(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_size_by_speed_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_size_by_speed_range(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_size_by_speed_velocity_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_size_by_speed_velocity_range(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_color_intensity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_color_intensity(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_color_by_speed_velocity_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_color_by_speed_velocity_range(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_range(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_render_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_render_mode(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_align_to_direction(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_align_to_direction(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_particle_emitter_get_pivot(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_pivot(Entity eid, Vector2 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_texture_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_texture_mode(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_particle_emitter_get_texture_sheet_tiles(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_texture_sheet_tiles(Entity eid, Vector2 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_texture_sheet_cycles(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_texture_sheet_cycles(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_texture_sheet_randomize(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_texture_sheet_randomize(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_blend_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_blend_mode(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_color_gradient(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_color_gradient(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_color_by_speed_gradient(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_color_by_speed_gradient(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_velocity_gradient(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_velocity_gradient(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_scale_gradient(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_scale_gradient(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_gradient(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_gradient(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Bounds internal_m2n_particle_emitter_get_world_bounds(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_is_stopped(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_is_simulated(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_play_sub_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_stop_sub_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_stop_and_reset_sub_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_pause_sub_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_resume_sub_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_emit(Entity eid, uint count, EmitParams parameters);

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
        private static extern float internal_m2n_particle_emitter_get_opacity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_opacity(Entity eid, float opacity);

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

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_simulation_backend(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_simulation_backend(Entity eid, int backend);
    }
}
