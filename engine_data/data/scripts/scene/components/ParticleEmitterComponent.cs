using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{
    /// <summary>
    /// A burst of particles at set times of the emission cycle. Matches C++ ps_soa::emitter_burst.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ParticleBurst
    {
        /// <summary>Seconds into the emission cycle of the first firing.</summary>
        public float time;
        /// <summary>Fewest particles per firing.</summary>
        public float countMin;
        /// <summary>Most particles per firing.</summary>
        public float countMax;
        /// <summary>Firings per cycle: 1 = once, 0 = every interval until the cycle ends.</summary>
        public uint cycles;
        /// <summary>Seconds between the firings of this burst (at least 0.01).</summary>
        public float interval;
        /// <summary>Chance (0..1) that each firing happens.</summary>
        public float probability;

        /// <summary>
        /// A burst of @p count particles at @p time, once per cycle, always (probability 1). Build bursts with it: a
        /// struct left at its defaults has probability 0 and never fires.
        /// </summary>
        public ParticleBurst(float time, float count)
            : this(time, count, 1, 0.1f)
        {
        }

        /// <summary>
        /// A burst of @p count particles first at @p time, then @p cycles firings in all (0 = until the cycle ends)
        /// every @p interval seconds.
        /// </summary>
        public ParticleBurst(float time, float count, uint cycles, float interval)
        {
            this.time = time;
            countMin = count;
            countMax = count;
            this.cycles = cycles;
            this.interval = interval;
            probability = 1.0f;
        }
    }

    /// <summary>
    /// Emitter shape enumeration that matches the C++ EmitterShape::Enum
    /// </summary>
    public enum EmitterShape
    {
        Sphere = 0,
        Hemisphere = 1,
        Circle = 2,
        Box = 3,
        Rect = 4,
        /// <summary>Base disc of a cone; particles launch along its slant (coneAngle).</summary>
        Cone = 5,
        /// <summary>A segment along X.</summary>
        Line = 6,
        /// <summary>A ring in the XZ plane with a round tube (torusThickness).</summary>
        Torus = 7,
        /// <summary>The surface or the vertices of shapeMesh.</summary>
        Mesh = 8
    }

    /// <summary>
    /// What a particle does where its path first crosses a collision plane. Matches C++ ps_soa::collision_mode.
    /// </summary>
    public enum ParticleCollisionMode
    {
        /// <summary>No collision.</summary>
        None = 0,
        /// <summary>It dies there.</summary>
        Kill = 1,
        /// <summary>It bounces off (restitution, friction), up to maxBounces times.</summary>
        Bounce = 2
    }

    /// <summary>
    /// What fires a sub-emitter. Matches C++ ps_soa::particle_event_type.
    /// </summary>
    public enum ParticleEventType
    {
        /// <summary>A particle is born.</summary>
        Birth = 1,
        /// <summary>A particle dies.</summary>
        Death = 2,
        /// <summary>A particle reaches a collision plane.</summary>
        Collision = 4
    }

    /// <summary>
    /// A burst another emitter fires where this emitter's particles are born, die or collide.
    /// Matches C++ particle_sub_emitter.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ParticleSubEmitter
    {
        /// <summary>What fires the burst.</summary>
        public ParticleEventType trigger;
        /// <summary>The entity whose Particle Emitter bursts.</summary>
        public Entity target;
        /// <summary>Fewest particles per event.</summary>
        public float countMin;
        /// <summary>Most particles per event.</summary>
        public float countMax;
        /// <summary>Chance (0..1) that an event fires the burst.</summary>
        public float probability;
        /// <summary>Share of the particle's velocity the burst's particles add to their own launch.</summary>
        public float inheritVelocity;
        /// <summary>1: the burst's particles take the particle's color.</summary>
        public int inheritColor;
        /// <summary>1: the burst's particles start at the particle's size.</summary>
        public int inheritSize;

        /// <summary>
        /// A sub-emitter firing @p count particles on @p target at every @p trigger, always (probability 1).
        /// Build sub-emitters with it: a struct left at its defaults has probability 0 and never fires.
        /// </summary>
        public ParticleSubEmitter(ParticleEventType trigger, Entity target, float count)
        {
            this.trigger = trigger;
            this.target = target;
            countMin = count;
            countMax = count;
            probability = 1.0f;
            inheritVelocity = 0.0f;
            inheritColor = 0;
            inheritSize = 0;
        }
    }

    /// <summary>
    /// How particles are lit. Matches C++ ps_soa::particle_lighting.
    /// </summary>
    public enum ParticleLighting
    {
        /// <summary>The color is the light the particle gives off (fire, magic, sparks).</summary>
        Unlit = 0,
        /// <summary>Lit once per particle like a small cloud: sky, sun (with its shadow), nearby lights.</summary>
        Volumetric = 1,
        /// <summary>Lit like a small sphere facing the camera: the sun per pixel with lightingWrap.</summary>
        Surface = 2
    }

    /// <summary>
    /// How a ribbon's texture runs along it. Matches C++ ps_soa::ribbon_texture_mode.
    /// </summary>
    public enum ParticleRibbonTextureMode
    {
        /// <summary>With each particle's age, flowing with the particles.</summary>
        Age = 0,
        /// <summary>Once over the whole ribbon, head to tail.</summary>
        Stretch = 1,
        /// <summary>Repeated every ribbonTileTime seconds of the particles' age.</summary>
        Tile = 2
    }

    /// <summary>
    /// Where a mesh shape spawns. Matches C++ ps_soa::mesh_spawn_mode.
    /// </summary>
    public enum ParticleMeshSpawnMode
    {
        /// <summary>Anywhere on the surface, evenly by area.</summary>
        Surface = 0,
        /// <summary>On the vertices.</summary>
        Vertices = 1
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
    /// Where an emitter's particles are culled and sorted. Matches C++ ps_soa::particle_sort_backend.
    /// </summary>
    public enum ParticleSortBackend
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
        /// <summary>Freezes while no camera sees the emitter's bounds, then catches up on the time it
        /// missed, so it wakes as if it had kept running.</summary>
        RendererBased = 1,
        /// <summary>Freezes while no camera sees the emitter's bounds and resumes where it froze.</summary>
        Pause = 2
    }

    /// <summary>
    /// Order an emitter's own particles draw in. Matches C++ ps_soa::sort_mode.
    /// </summary>
    public enum ParticleSortMode
    {
        /// <summary>Back to front for Normal and Premultiplied blend; unsorted for Additive and Multiply.</summary>
        Auto = 0,
        /// <summary>A stable order, no sort.</summary>
        None = 1,
        /// <summary>Back to front by distance to the camera.</summary>
        ViewDistance = 2,
        /// <summary>Back to front along the camera's forward axis.</summary>
        ViewDepth = 3,
        /// <summary>The oldest particles draw first, the youngest on top.</summary>
        OldestFirst = 4,
        /// <summary>The youngest particles draw first, the oldest on top.</summary>
        YoungestFirst = 5
    }

    /// <summary>
    /// Geometry each particle draws. Matches C++ ps_soa::particle_primitive.
    /// </summary>
    public enum ParticlePrimitive
    {
        /// <summary>The whole texture.</summary>
        Quad = 0,
        /// <summary>The regular octagon around the texture's inscribed circle; the corners are cut off.</summary>
        Octagon = 1,
        /// <summary>An 8-sided polygon around each sheet frame's texels above cutoutAlphaThreshold.</summary>
        Cutout = 4
    }

    /// <summary>
    /// How an emitter's culling bounds are found. Matches C++ ps_soa::bounds_mode.
    /// </summary>
    public enum ParticleBoundsMode
    {
        /// <summary>Worked out every frame from the emitter's settings.</summary>
        Automatic = 0,
        /// <summary>The fixedBounds box, in the emitter's local space.</summary>
        Fixed = 1
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
        VerticalBillboard = 2,
        /// <summary>Turned toward the camera's position: no stretch at the screen edges.</summary>
        CameraFacing = 3,
        /// <summary>Stretched along its motion (stretchLengthScale, stretchSpeedScale).</summary>
        Stretched = 4,
        /// <summary>A ribbon along the particle's own path over the last trailLength seconds.</summary>
        Trail = 5,
        /// <summary>One ribbon through all of the emitter's particles in birth order.</summary>
        Ribbon = 6,
        /// <summary>renderMesh with its material, drawn, lit and shadowed with the scene's models.</summary>
        Mesh = 7,
        /// <summary>Upright along facingAxis (in the emitter's space), turning about it toward the camera.</summary>
        FixedAxis = 8
    }

    /// <summary>
    /// How particles blend over the scene. Matches C++ ps_soa::blend_mode.
    /// </summary>
    public enum ParticleBlendMode
    {
        Normal = 0,
        Additive = 1,
        Multiply = 2,
        /// <summary>Normal blending of a texture whose color is already multiplied by its alpha.</summary>
        Premultiplied = 3
    }

    /// <summary>
    /// How the particle texture is read. Matches C++ ps_soa::texture_mode.
    /// </summary>
    public enum ParticleTextureMode
    {
        /// <summary>The texture's color and alpha.</summary>
        MultiChannel = 0,
        /// <summary>The texture's luminance is the opacity; the particle's color tints it.</summary>
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
        /// Launch velocity, m/s, in simulation space. Gravity, acceleration and drag still apply.
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
        /// Size at birth: sizeOverLife is scaled to start at it, or to peak at it when the curve
        /// starts at 0.
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
        /// Where the particles are culled and sorted for each view: GPU (one compute pass for every emitter) or CPU
        /// (the main thread). They look the same either way.
        /// </summary>
        public ParticleSortBackend sortBackend
        {
            get
            {
                return (ParticleSortBackend)internal_m2n_particle_emitter_get_sort_backend(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_sort_backend(owner, (int)value);
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
        /// Where a moving emitter's particles appear: 1 = spread along the path it moved this frame, 0 = all at its
        /// current place.
        /// </summary>
        public float spawnAlongPath
        {
            get
            {
                return internal_m2n_particle_emitter_get_spawn_along_path(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_spawn_along_path(owner, value);
            }
        }

        /// <summary>
        /// Linear drag, per second: velocity decays by exp(-drag t), so gravity and acceleration reach a
        /// terminal speed.
        /// </summary>
        public float drag
        {
            get => internal_m2n_particle_emitter_get_drag(owner);
            set => internal_m2n_particle_emitter_set_drag(owner, value);
        }

        /// <summary>
        /// Fraction of the emitter's own velocity new particles inherit (world simulation space).
        /// </summary>
        public float inheritVelocity
        {
            get => internal_m2n_particle_emitter_get_inherit_velocity(owner);
            set => internal_m2n_particle_emitter_set_inherit_velocity(owner, value);
        }

        /// <summary>
        /// Half angle, degrees, of the cone around the launch direction particles spread over.
        /// </summary>
        public float directionSpread
        {
            get => internal_m2n_particle_emitter_get_direction_spread(owner);
            set => internal_m2n_particle_emitter_set_direction_spread(owner, value);
        }

        /// <summary>
        /// Replay the same particles every time from randomSeed instead of a fresh sequence per emitter.
        /// </summary>
        public bool useFixedSeed
        {
            get => internal_m2n_particle_emitter_get_use_fixed_seed(owner);
            set => internal_m2n_particle_emitter_set_use_fixed_seed(owner, value);
        }

        /// <summary>
        /// Seed of the replayed sequence when useFixedSeed is on.
        /// </summary>
        public uint randomSeed
        {
            get => internal_m2n_particle_emitter_get_random_seed(owner);
            set => internal_m2n_particle_emitter_set_random_seed(owner, value);
        }

        /// <summary>
        /// Order of this emitter's own particles.
        /// </summary>
        public ParticleSortMode sortMode
        {
            get => (ParticleSortMode)internal_m2n_particle_emitter_get_sort_mode(owner);
            set => internal_m2n_particle_emitter_set_sort_mode(owner, (int)value);
        }

        /// <summary>
        /// Geometry each particle draws: the full quad, or an octagon that skips the texture's corners
        /// (fewer pixels for round textures).
        /// </summary>
        public ParticlePrimitive primitive
        {
            get => (ParticlePrimitive)internal_m2n_particle_emitter_get_primitive(owner);
            set => internal_m2n_particle_emitter_set_primitive(owner, (int)value);
        }

        /// <summary>
        /// Draw at half the view's resolution (a quarter of the fill cost) and blend over the
        /// full-resolution particles. Suits large, soft effects; ignored for Multiply blend.
        /// </summary>
        public bool halfResolution
        {
            get => internal_m2n_particle_emitter_get_half_resolution(owner);
            set => internal_m2n_particle_emitter_set_half_resolution(owner, value);
        }

        /// <summary>
        /// Write motion vectors where the particles are at least velocityAlphaThreshold opaque, so the
        /// temporal anti-aliasing reprojects them like moving objects (debris, sparks, embers).
        /// </summary>
        public bool writeVelocity
        {
            get => internal_m2n_particle_emitter_get_write_velocity(owner);
            set => internal_m2n_particle_emitter_set_write_velocity(owner, value);
        }

        /// <summary>
        /// Opacity (0..1) from which a particle texel writes its motion (writeVelocity).
        /// </summary>
        public float velocityAlphaThreshold
        {
            get => internal_m2n_particle_emitter_get_velocity_alpha_threshold(owner);
            set => internal_m2n_particle_emitter_set_velocity_alpha_threshold(owner, value);
        }

        /// <summary>
        /// Multipliers of sizeAxes over the particle's life (1 throughout = constant): x and y squash and stretch
        /// quads and meshes, z is the depth of mesh particles.
        /// </summary>
        public Vector3Curve sizeAxesOverLife
        {
            get => new Vector3Curve(internal_m2n_particle_emitter_get_size_axes_over_life(owner).ToStructArray<Vector3Key>());
            set => internal_m2n_particle_emitter_set_size_axes_over_life(owner, RequireKeys(value?.keys, nameof(sizeAxesOverLife)).ToByteArray());
        }

        /// <summary>
        /// Each particle takes its own random mix of colorGradient and colorGradientB.
        /// </summary>
        public bool randomBetweenGradients
        {
            get => internal_m2n_particle_emitter_get_random_between_gradients(owner);
            set => internal_m2n_particle_emitter_set_random_between_gradients(owner, value);
        }

        /// <summary>
        /// The second color over life, used with randomBetweenGradients.
        /// </summary>
        public Gradient colorGradientB
        {
            get => new Gradient(internal_m2n_particle_emitter_get_color_gradient_b(owner).ToStructArray<GradientKey>());
            set => internal_m2n_particle_emitter_set_color_gradient_b(owner, RequireKeys(value?.keys, nameof(colorGradientB)).ToByteArray());
        }

        /// <summary>
        /// Multiplier of the color's intensity over life (1 throughout = off).
        /// </summary>
        public Curve intensityOverLife
        {
            get => new Curve(internal_m2n_particle_emitter_get_intensity_over_life(owner).ToStructArray<CurveKey>());
            set => internal_m2n_particle_emitter_set_intensity_over_life(owner, RequireKeys(value?.keys, nameof(intensityOverLife)).ToByteArray());
        }

        /// <summary>
        /// Multiplier of spinSpeed over life (1 throughout = constant spin).
        /// </summary>
        public Curve spinOverLife
        {
            get => new Curve(internal_m2n_particle_emitter_get_spin_over_life(owner).ToStructArray<CurveKey>());
            set => internal_m2n_particle_emitter_set_spin_over_life(owner, RequireKeys(value?.keys, nameof(spinOverLife)).ToByteArray());
        }

        /// <summary>
        /// The texture sheet frame follows textureSheetFrameCurve instead of textureSheetCycles.
        /// </summary>
        public bool textureSheetUseFrameCurve
        {
            get => internal_m2n_particle_emitter_get_texture_sheet_use_frame_curve(owner);
            set => internal_m2n_particle_emitter_set_texture_sheet_use_frame_curve(owner, value);
        }

        /// <summary>
        /// Texture sheet frame over life, 0 = first frame, 1 = last (textureSheetUseFrameCurve).
        /// </summary>
        public Curve textureSheetFrameCurve
        {
            get => new Curve(internal_m2n_particle_emitter_get_texture_sheet_frame_curve(owner).ToStructArray<CurveKey>());
            set => internal_m2n_particle_emitter_set_texture_sheet_frame_curve(owner, RequireKeys(value?.keys, nameof(textureSheetFrameCurve)).ToByteArray());
        }

        /// <summary>
        /// Each particle plays one random row of the texture sheet.
        /// </summary>
        public bool textureSheetRandomRow
        {
            get => internal_m2n_particle_emitter_get_texture_sheet_random_row(owner);
            set => internal_m2n_particle_emitter_set_texture_sheet_random_row(owner, value);
        }

        /// <summary>
        /// Particles per metre the emitter moves, spaced along its path (trails). 0 = off.
        /// </summary>
        public float rateOverDistance
        {
            get => internal_m2n_particle_emitter_get_rate_over_distance(owner);
            set => internal_m2n_particle_emitter_set_rate_over_distance(owner, value);
        }

        /// <summary>
        /// Bursts at set times of each emission cycle, on top of the rates. A one-shot emitter with an
        /// emission rate of 0 emits only its bursts.
        /// </summary>
        public ParticleBurst[] bursts
        {
            get => internal_m2n_particle_emitter_get_bursts(owner).ToStructArray<ParticleBurst>();
            set => internal_m2n_particle_emitter_set_bursts(owner, (value ?? Array.Empty<ParticleBurst>()).ToByteArray());
        }

        /// <summary>
        /// Cone shape: angle, degrees, between the axis and the slant the rim launches along.
        /// </summary>
        public float coneAngle
        {
            get => internal_m2n_particle_emitter_get_cone_angle(owner);
            set => internal_m2n_particle_emitter_set_cone_angle(owner, value);
        }

        /// <summary>
        /// Torus shape: the tube's radius as a fraction of the ring's.
        /// </summary>
        public float torusThickness
        {
            get => internal_m2n_particle_emitter_get_torus_thickness(owner);
            set => internal_m2n_particle_emitter_set_torus_thickness(owner, value);
        }

        /// <summary>
        /// Mesh render mode: the mesh each particle draws, at its size times sizeAxes (null: nothing draws).
        /// </summary>
        public Mesh renderMesh
        {
            get
            {
                var uid = internal_m2n_particle_emitter_get_render_mesh(owner);
                return uid == Guid.Empty ? null : new Mesh { uid = uid };
            }
            set => internal_m2n_particle_emitter_set_render_mesh(owner, value?.uid ?? Guid.Empty);
        }

        /// <summary>
        /// Mesh render mode: the material of every part of the mesh (null: the mesh keeps its own materials).
        /// </summary>
        public Material renderMaterial
        {
            get
            {
                var uid = internal_m2n_particle_emitter_get_render_material(owner);
                return uid == Guid.Empty ? null : new Material { uid = uid };
            }
            set => internal_m2n_particle_emitter_set_render_material(owner, value?.uid ?? Guid.Empty);
        }

        /// <summary>
        /// Mesh render mode: the particles cast shadows (when their material does).
        /// </summary>
        public bool meshCastShadows
        {
            get => internal_m2n_particle_emitter_get_mesh_cast_shadows(owner);
            set => internal_m2n_particle_emitter_set_mesh_cast_shadows(owner, value);
        }

        /// <summary>
        /// Mesh shape: the mesh particles spawn on (null: they spawn at the shape's origin).
        /// </summary>
        public Mesh shapeMesh
        {
            get
            {
                var uid = internal_m2n_particle_emitter_get_shape_mesh(owner);
                return uid == Guid.Empty ? null : new Mesh { uid = uid };
            }
            set => internal_m2n_particle_emitter_set_shape_mesh(owner, value?.uid ?? Guid.Empty);
        }

        /// <summary>
        /// Mesh shape: anywhere on the surface by area, or on the vertices.
        /// </summary>
        public ParticleMeshSpawnMode shapeMeshMode
        {
            get => (ParticleMeshSpawnMode)internal_m2n_particle_emitter_get_mesh_spawn_mode(owner);
            set => internal_m2n_particle_emitter_set_mesh_spawn_mode(owner, (int)value);
        }

        /// <summary>
        /// Metres a swirling curl-noise field displaces the particles by (0 = off).
        /// </summary>
        public float turbulenceStrength
        {
            get => internal_m2n_particle_emitter_get_turbulence_strength(owner);
            set => internal_m2n_particle_emitter_set_turbulence_strength(owner, value);
        }

        /// <summary>
        /// Turbulence swirls per metre: higher = smaller, busier swirls.
        /// </summary>
        public float turbulenceFrequency
        {
            get => internal_m2n_particle_emitter_get_turbulence_frequency(owner);
            set => internal_m2n_particle_emitter_set_turbulence_frequency(owner, value);
        }

        /// <summary>
        /// How fast the turbulence field evolves, in swirls per second.
        /// </summary>
        public float turbulenceSpeed
        {
            get => internal_m2n_particle_emitter_get_turbulence_speed(owner);
            set => internal_m2n_particle_emitter_set_turbulence_speed(owner, value);
        }

        /// <summary>
        /// Multiplier of the turbulence strength over life (1 throughout = constant).
        /// </summary>
        public Curve turbulenceOverLife
        {
            get => new Curve(internal_m2n_particle_emitter_get_turbulence_over_life(owner).ToStructArray<CurveKey>());
            set => internal_m2n_particle_emitter_set_turbulence_over_life(owner, RequireKeys(value?.keys, nameof(turbulenceOverLife)).ToByteArray());
        }

        /// <summary>
        /// Metres from the camera beyond which the emitter is not drawn (0 = no limit).
        /// </summary>
        public float maxDrawDistance
        {
            get => internal_m2n_particle_emitter_get_max_draw_distance(owner);
            set => internal_m2n_particle_emitter_set_max_draw_distance(owner, value);
        }

        /// <summary>
        /// Metres before maxDrawDistance over which particles fade out.
        /// </summary>
        public float drawDistanceFade
        {
            get => internal_m2n_particle_emitter_get_draw_distance_fade(owner);
            set => internal_m2n_particle_emitter_set_draw_distance_fade(owner, value);
        }

        /// <summary>
        /// Cutout primitive: texels whose alpha is above this stay inside each sheet frame's polygon.
        /// </summary>
        public float cutoutAlphaThreshold
        {
            get => internal_m2n_particle_emitter_get_cutout_alpha_threshold(owner);
            set => internal_m2n_particle_emitter_set_cutout_alpha_threshold(owner, value);
        }

        /// <summary>
        /// Surface lighting: how far the sun wraps past the lit half (0 = hard terminator, 1 = lit all round).
        /// </summary>
        public float lightingWrap
        {
            get => internal_m2n_particle_emitter_get_lighting_wrap(owner);
            set => internal_m2n_particle_emitter_set_lighting_wrap(owner, value);
        }

        /// <summary>
        /// Surface lighting: a tangent-space normal map (green up) laid out like the texture (null: spheres).
        /// </summary>
        public Texture normalTexture
        {
            get
            {
                var uid = internal_m2n_particle_emitter_get_normal_texture(owner);
                return uid == Guid.Empty ? null : new Texture { uid = uid };
            }
            set => internal_m2n_particle_emitter_set_normal_texture(owner, value?.uid ?? Guid.Empty);
        }

        /// <summary>
        /// Lit particles darken in the sun's shadow.
        /// </summary>
        public bool receiveShadows
        {
            get => internal_m2n_particle_emitter_get_receive_shadows(owner);
            set => internal_m2n_particle_emitter_set_receive_shadows(owner, value);
        }

        /// <summary>
        /// How the particles are lit; lit particles take their color as albedo (multiply blend stays unlit).
        /// </summary>
        public ParticleLighting lighting
        {
            get => (ParticleLighting)internal_m2n_particle_emitter_get_lighting(owner);
            set => internal_m2n_particle_emitter_set_lighting(owner, (int)value);
        }

        /// <summary>
        /// How many of the brightest particles light the scene as point lights (0 = off).
        /// </summary>
        public uint lightCount
        {
            get => internal_m2n_particle_emitter_get_light_count(owner);
            set => internal_m2n_particle_emitter_set_light_count(owner, value);
        }

        /// <summary>
        /// Candela of a particle light at full particle brightness.
        /// </summary>
        public float lightIntensity
        {
            get => internal_m2n_particle_emitter_get_light_intensity(owner);
            set => internal_m2n_particle_emitter_set_light_intensity(owner, value);
        }

        /// <summary>
        /// Metres a particle light reaches.
        /// </summary>
        public float lightRange
        {
            get => internal_m2n_particle_emitter_get_light_range(owner);
            set => internal_m2n_particle_emitter_set_light_range(owner, value);
        }

        /// <summary>
        /// Particle lights take their particle's color (off: white).
        /// </summary>
        public bool lightColorFromParticle
        {
            get => internal_m2n_particle_emitter_get_light_color_from_particle(owner);
            set => internal_m2n_particle_emitter_set_light_color_from_particle(owner, value);
        }

        /// <summary>
        /// Bounce collision: share of the speed into the plane a bounce keeps.
        /// </summary>
        public float restitution
        {
            get => internal_m2n_particle_emitter_get_restitution(owner);
            set => internal_m2n_particle_emitter_set_restitution(owner, value);
        }

        /// <summary>
        /// Bounce collision: share of the speed along the plane a bounce loses.
        /// </summary>
        public float friction
        {
            get => internal_m2n_particle_emitter_get_friction(owner);
            set => internal_m2n_particle_emitter_set_friction(owner, value);
        }

        /// <summary>
        /// Bounce collision: bounces before the particle dies at its next contact.
        /// </summary>
        public uint maxBounces
        {
            get => internal_m2n_particle_emitter_get_max_bounces(owner);
            set => internal_m2n_particle_emitter_set_max_bounces(owner, value);
        }

        /// <summary>
        /// Collision: how far from a plane a particle touches it, in halves of its size (1 = its edge, 0 = its centre).
        /// </summary>
        public float collisionRadiusScale
        {
            get => internal_m2n_particle_emitter_get_collision_radius_scale(owner);
            set => internal_m2n_particle_emitter_set_collision_radius_scale(owner, value);
        }

        /// <summary>
        /// What a particle does where its path first crosses one of collisionPlanes (world-space emitters).
        /// </summary>
        public ParticleCollisionMode collision
        {
            get => (ParticleCollisionMode)internal_m2n_particle_emitter_get_collision(owner);
            set => internal_m2n_particle_emitter_set_collision(owner, (int)value);
        }

        /// <summary>
        /// Collision planes: a world normal (x, y, z) and an offset (w) from the emitter's origin along it.
        /// </summary>
        public Vector4[] collisionPlanes
        {
            get => internal_m2n_particle_emitter_get_collision_planes(owner).ToStructArray<Vector4>();
            set => internal_m2n_particle_emitter_set_collision_planes(owner, (value ?? Array.Empty<Vector4>()).ToByteArray());
        }

        /// <summary>
        /// Bursts other emitters fire where this emitter's particles are born, die or collide.
        /// </summary>
        public ParticleSubEmitter[] subEmitters
        {
            get => internal_m2n_particle_emitter_get_sub_emitters(owner).ToStructArray<ParticleSubEmitter>();
            set => internal_m2n_particle_emitter_set_sub_emitters(owner, (value ?? Array.Empty<ParticleSubEmitter>()).ToByteArray());
        }

        /// <summary>
        /// Trail render mode: seconds of the particle's path the trail covers.
        /// </summary>
        public float trailLength
        {
            get => internal_m2n_particle_emitter_get_trail_length(owner);
            set => internal_m2n_particle_emitter_set_trail_length(owner, value);
        }

        /// <summary>
        /// Trail render mode: the trail's width at the tail as a share of the particle's size.
        /// </summary>
        public float trailWidthEnd
        {
            get => internal_m2n_particle_emitter_get_trail_width_end(owner);
            set => internal_m2n_particle_emitter_set_trail_width_end(owner, value);
        }

        /// <summary>
        /// Ribbon render mode: how the texture runs along the ribbon.
        /// </summary>
        public ParticleRibbonTextureMode ribbonTextureMode
        {
            get => (ParticleRibbonTextureMode)internal_m2n_particle_emitter_get_ribbon_texture_mode(owner);
            set => internal_m2n_particle_emitter_set_ribbon_texture_mode(owner, (int)value);
        }

        /// <summary>
        /// Ribbon render mode, tile texture mode: seconds of the particles' age one texture repeat spans.
        /// </summary>
        public float ribbonTileTime
        {
            get => internal_m2n_particle_emitter_get_ribbon_tile_time(owner);
            set => internal_m2n_particle_emitter_set_ribbon_tile_time(owner, value);
        }

        /// <summary>
        /// Whether the culling bounds are worked out from the settings or set by hand (fixedBounds).
        /// </summary>
        public ParticleBoundsMode boundsMode
        {
            get => (ParticleBoundsMode)internal_m2n_particle_emitter_get_bounds_mode(owner);
            set => internal_m2n_particle_emitter_set_bounds_mode(owner, (int)value);
        }

        /// <summary>
        /// Culling box in the emitter's local space when boundsMode is Fixed.
        /// </summary>
        public Bounds fixedBounds
        {
            get => internal_m2n_particle_emitter_get_fixed_bounds(owner);
            set => internal_m2n_particle_emitter_set_fixed_bounds(owner, value);
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
        /// Constant acceleration, m/s^2, in simulation space, on top of gravity: wind, buoyancy, a pull.
        /// </summary>
        public Vector3 acceleration
        {
            get
            {
                return internal_m2n_particle_emitter_get_acceleration(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_acceleration(owner, value);
            }
        }

        /// <summary>
        /// Size of the emission shape, metres per unit.
        /// </summary>
        public Vector3 shapeScale
        {
            get
            {
                return internal_m2n_particle_emitter_get_shape_scale(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_shape_scale(owner, value);
            }
        }

        /// <summary>
        /// Length of one emission cycle, seconds: a looping emitter repeats it, a one-shot emitter emits
        /// for one cycle and stops once its last particle is gone.
        /// </summary>
        public float duration
        {
            get => internal_m2n_particle_emitter_get_duration(owner);
            set => internal_m2n_particle_emitter_set_duration(owner, value);
        }

        /// <summary>
        /// Particle lifetime, seconds, random between min and max.
        /// </summary>
        public Range<float> lifetime
        {
            get => internal_m2n_particle_emitter_get_lifetime_range(owner);
            set => internal_m2n_particle_emitter_set_lifetime_range(owner, value);
        }

        /// <summary>
        /// Launch speed, metres per second, random between min and max.
        /// </summary>
        public Range<float> startSpeed
        {
            get => internal_m2n_particle_emitter_get_start_speed(owner);
            set => internal_m2n_particle_emitter_set_start_speed(owner, value);
        }

        /// <summary>
        /// Remaps a particle's life before its motion is evaluated (Linear = physical timing).
        /// </summary>
        public ParticleEasing motionEasing
        {
            get
            {
                return (ParticleEasing)internal_m2n_particle_emitter_get_motion_easing(owner);
            }
            set
            {
                internal_m2n_particle_emitter_set_motion_easing(owner, (int)value);
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
        public Vector3 shapePosition
        {
            get => internal_m2n_particle_emitter_get_shape_position(owner);
            set => internal_m2n_particle_emitter_set_shape_position(owner, value);
        }

        /// <summary>
        /// Multipliers of the size along each particle's own axes: x and y stretch quads and meshes, z is the depth
        /// of mesh particles.
        /// </summary>
        public Vector3 sizeAxes
        {
            get => internal_m2n_particle_emitter_get_size_axes(owner);
            set => internal_m2n_particle_emitter_set_size_axes(owner, value);
        }

        /// <summary>
        /// Start angle of each particle in its quad's plane, degrees, random between min and max.
        /// </summary>
        public Range<float> startRotation
        {
            get => internal_m2n_particle_emitter_get_start_rotation(owner);
            set => internal_m2n_particle_emitter_set_start_rotation(owner, value);
        }

        /// <summary>
        /// Spin in degrees per second, random between min and max.
        /// </summary>
        public Range<float> spinSpeed
        {
            get => internal_m2n_particle_emitter_get_spin_speed(owner);
            set => internal_m2n_particle_emitter_set_spin_speed(owner, value);
        }

        /// <summary>
        /// Whether half the particles spin the other way.
        /// </summary>
        public bool randomSpinDirection
        {
            get => internal_m2n_particle_emitter_get_random_spin_direction(owner);
            set => internal_m2n_particle_emitter_set_random_spin_direction(owner, value);
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
        /// Size multiplier at the slow (min) and fast (max) ends of sizeBySpeedSpeedRange; min = max turns it off.
        /// </summary>
        public Range<float> sizeBySpeedMultiplier
        {
            get => internal_m2n_particle_emitter_get_size_by_speed_multiplier(owner);
            set => internal_m2n_particle_emitter_set_size_by_speed_multiplier(owner, value);
        }

        /// <summary>
        /// Particle speeds, m/s, sizeBySpeedMultiplier maps over.
        /// </summary>
        public Range<float> sizeBySpeedSpeedRange
        {
            get => internal_m2n_particle_emitter_get_size_by_speed_speed_range(owner);
            set => internal_m2n_particle_emitter_set_size_by_speed_speed_range(owner, value);
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
        /// Particle speeds, m/s, colorBySpeedGradient maps over; min = max turns it off.
        /// </summary>
        public Range<float> colorBySpeedSpeedRange
        {
            get => internal_m2n_particle_emitter_get_color_by_speed_speed_range(owner);
            set => internal_m2n_particle_emitter_set_color_by_speed_speed_range(owner, value);
        }

        /// <summary>
        /// Emitter speeds (units per second) that map to lifetimeByEmitterSpeedCurve.
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
        /// Color multiplier over colorBySpeedSpeedRange (time 0 = slow end, 1 = fast end).
        /// </summary>
        public Gradient colorBySpeedGradient
        {
            get => new Gradient(internal_m2n_particle_emitter_get_color_by_speed_gradient(owner).ToStructArray<GradientKey>());
            set => internal_m2n_particle_emitter_set_color_by_speed_gradient(owner, RequireKeys(value?.keys, nameof(colorBySpeedGradient)).ToByteArray());
        }

        /// <summary>
        /// Stretched mode: quad length as a multiple of the particle size.
        /// </summary>
        public float stretchLengthScale
        {
            get => internal_m2n_particle_emitter_get_stretch_length_scale(owner);
            set => internal_m2n_particle_emitter_set_stretch_length_scale(owner, value);
        }

        /// <summary>
        /// Stretched mode: quad length added per metre per second of speed.
        /// </summary>
        public float stretchSpeedScale
        {
            get => internal_m2n_particle_emitter_get_stretch_speed_scale(owner);
            set => internal_m2n_particle_emitter_set_stretch_speed_scale(owner, value);
        }

        /// <summary>
        /// Stretched mode: the share of the camera's velocity taken off the particles' before they stretch.
        /// </summary>
        public float stretchCameraVelocityScale
        {
            get => internal_m2n_particle_emitter_get_stretch_camera_velocity_scale(owner);
            set => internal_m2n_particle_emitter_set_stretch_camera_velocity_scale(owner, value);
        }

        /// <summary>
        /// Fixed axis mode: the axis the quads stand along, in the emitter's space.
        /// </summary>
        public Vector3 facingAxis
        {
            get => internal_m2n_particle_emitter_get_facing_axis(owner);
            set => internal_m2n_particle_emitter_set_facing_axis(owner, value);
        }

        /// <summary>
        /// Metres each particle is pushed toward the camera.
        /// </summary>
        public float cameraOffset
        {
            get => internal_m2n_particle_emitter_get_camera_offset(owner);
            set => internal_m2n_particle_emitter_set_camera_offset(owner, value);
        }

        /// <summary>
        /// Largest size on screen as a fraction of the view's height (0 = no limit).
        /// </summary>
        public float maxScreenSize
        {
            get => internal_m2n_particle_emitter_get_max_screen_size(owner);
            set => internal_m2n_particle_emitter_set_max_screen_size(owner, value);
        }

        /// <summary>
        /// How much the particles tell the temporal anti-aliasing to trust the current frame (0..1).
        /// </summary>
        public float taaResponse
        {
            get => internal_m2n_particle_emitter_get_taa_response(owner);
            set => internal_m2n_particle_emitter_set_taa_response(owner, value);
        }

        /// <summary>
        /// Distances from the camera over which particles fade in: invisible at min, whole at max (0, 0 = off).
        /// </summary>
        public Range<float> cameraFadeRange
        {
            get => internal_m2n_particle_emitter_get_camera_fade_range(owner);
            set => internal_m2n_particle_emitter_set_camera_fade_range(owner, value);
        }

        /// <summary>
        /// Particles smaller than a pixel keep one pixel and fade by the area they lost.
        /// </summary>
        public bool subpixelFade
        {
            get => internal_m2n_particle_emitter_get_subpixel_fade(owner);
            set => internal_m2n_particle_emitter_set_subpixel_fade(owner, value);
        }

        /// <summary>
        /// Alpha erosion threshold over life (0..1): texels below it vanish. 0 throughout = off.
        /// </summary>
        public Curve erosionOverLife
        {
            get => new Curve(internal_m2n_particle_emitter_get_erosion_over_life(owner).ToStructArray<CurveKey>());
            set => internal_m2n_particle_emitter_set_erosion_over_life(owner, RequireKeys(value?.keys, nameof(erosionOverLife)).ToByteArray());
        }

        /// <summary>
        /// Multiplier of the launch velocity over a particle's life (1 throughout = constant speed).
        /// </summary>
        public Curve speedOverLife
        {
            get => new Curve(internal_m2n_particle_emitter_get_speed_over_life(owner).ToStructArray<CurveKey>());
            set => internal_m2n_particle_emitter_set_speed_over_life(owner, RequireKeys(value?.keys, nameof(speedOverLife)).ToByteArray());
        }

        /// <summary>
        /// Particle size over its lifetime, random between the min and max curves; every key counts,
        /// so a size can pop in, or grow then shrink. A particle picks its place between min and max
        /// at birth and at death and drifts from one to the other.
        /// </summary>
        public MinMaxCurve sizeOverLife
        {
            get => new MinMaxCurve(internal_m2n_particle_emitter_get_size_over_life(owner).ToStructArray<MinMaxKey>());
            set => internal_m2n_particle_emitter_set_size_over_life(owner, RequireKeys(value?.keys, nameof(sizeOverLife)).ToByteArray());
        }

        /// <summary>
        /// Lifetime multiplier over lifetimeByEmitterSpeedRange (time 0 = slow end, 1 = fast end).
        /// </summary>
        public Curve lifetimeByEmitterSpeedCurve
        {
            get => new Curve(internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_curve(owner).ToStructArray<CurveKey>());
            set => internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_curve(owner, RequireKeys(value?.keys, nameof(lifetimeByEmitterSpeedCurve)).ToByteArray());
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
                internal_m2n_particle_emitter_play_child_emitters(owner);
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
                internal_m2n_particle_emitter_stop_child_emitters(owner);
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
                internal_m2n_particle_emitter_stop_and_reset_child_emitters(owner);
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
                internal_m2n_particle_emitter_pause_child_emitters(owner);
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
                internal_m2n_particle_emitter_resume_child_emitters(owner);
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
        private static extern Vector3 internal_m2n_particle_emitter_get_shape_position(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_shape_position(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_particle_emitter_get_size_axes(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_size_axes(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_start_rotation(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_start_rotation(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_spin_speed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_spin_speed(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_random_spin_direction(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_random_spin_direction(Entity eid, bool value);

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
        private static extern Range<float> internal_m2n_particle_emitter_get_size_by_speed_multiplier(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_size_by_speed_multiplier(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_size_by_speed_speed_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_size_by_speed_speed_range(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_color_intensity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_color_intensity(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_color_by_speed_speed_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_color_by_speed_speed_range(Entity eid, Range<float> value);

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
        private static extern float internal_m2n_particle_emitter_get_stretch_length_scale(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_stretch_length_scale(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_stretch_camera_velocity_scale(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_stretch_camera_velocity_scale(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_particle_emitter_get_facing_axis(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_facing_axis(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_stretch_speed_scale(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_stretch_speed_scale(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_camera_offset(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_camera_offset(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_max_screen_size(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_max_screen_size(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_taa_response(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_taa_response(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_camera_fade_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_camera_fade_range(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_subpixel_fade(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_subpixel_fade(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_erosion_over_life(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_erosion_over_life(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_speed_over_life(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_speed_over_life(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_size_over_life(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_size_over_life(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_curve(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_curve(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Bounds internal_m2n_particle_emitter_get_world_bounds(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_is_stopped(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_is_simulated(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_play_child_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_stop_child_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_stop_and_reset_child_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_pause_child_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_resume_child_emitters(Entity eid);

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
        private static extern float internal_m2n_particle_emitter_get_spawn_along_path(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_spawn_along_path(Entity eid, float motion);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_drag(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_drag(Entity eid, float drag);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_inherit_velocity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_inherit_velocity(Entity eid, float fraction);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_direction_spread(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_direction_spread(Entity eid, float degrees);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_use_fixed_seed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_use_fixed_seed(Entity eid, bool useFixedSeed);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_particle_emitter_get_random_seed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_random_seed(Entity eid, uint seed);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_sort_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_sort_mode(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_primitive(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_primitive(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_half_resolution(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_half_resolution(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_write_velocity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_write_velocity(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_velocity_alpha_threshold(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_velocity_alpha_threshold(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_size_axes_over_life(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_size_axes_over_life(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_random_between_gradients(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_random_between_gradients(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_color_gradient_b(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_color_gradient_b(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_intensity_over_life(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_intensity_over_life(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_spin_over_life(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_spin_over_life(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_texture_sheet_use_frame_curve(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_texture_sheet_use_frame_curve(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_texture_sheet_frame_curve(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_texture_sheet_frame_curve(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_texture_sheet_random_row(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_texture_sheet_random_row(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_rate_over_distance(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_rate_over_distance(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_bursts(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_bursts(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_cone_angle(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_cone_angle(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_torus_thickness(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_torus_thickness(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_particle_emitter_get_render_mesh(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_render_mesh(Entity eid, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_particle_emitter_get_render_material(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_render_material(Entity eid, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_mesh_cast_shadows(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_mesh_cast_shadows(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_particle_emitter_get_shape_mesh(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_shape_mesh(Entity eid, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_mesh_spawn_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_mesh_spawn_mode(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_turbulence_strength(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_turbulence_strength(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_turbulence_frequency(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_turbulence_frequency(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_turbulence_speed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_turbulence_speed(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_turbulence_over_life(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_turbulence_over_life(Entity eid, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_max_draw_distance(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_max_draw_distance(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_draw_distance_fade(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_draw_distance_fade(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_particle_emitter_get_normal_texture(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_normal_texture(Entity eid, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_cutout_alpha_threshold(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_cutout_alpha_threshold(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_lighting_wrap(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_lighting_wrap(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_receive_shadows(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_receive_shadows(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_lighting(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_lighting(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_particle_emitter_get_light_count(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_light_count(Entity eid, uint value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_light_intensity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_light_intensity(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_light_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_light_range(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_particle_emitter_get_light_color_from_particle(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_light_color_from_particle(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_restitution(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_restitution(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_friction(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_friction(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_collision_radius_scale(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_collision_radius_scale(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_particle_emitter_get_max_bounces(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_max_bounces(Entity eid, uint value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_collision(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_collision(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_collision_planes(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_collision_planes(Entity eid, byte[] planes);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_particle_emitter_get_sub_emitters(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_sub_emitters(Entity eid, byte[] subEmitters);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_trail_length(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_trail_length(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_ribbon_texture_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_ribbon_texture_mode(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_ribbon_tile_time(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_ribbon_tile_time(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_trail_width_end(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_trail_width_end(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_bounds_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_bounds_mode(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Bounds internal_m2n_particle_emitter_get_fixed_bounds(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_fixed_bounds(Entity eid, Bounds bounds);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_opacity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_opacity(Entity eid, float opacity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_particle_emitter_get_acceleration(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_acceleration(Entity eid, Vector3 acceleration);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_particle_emitter_get_shape_scale(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_shape_scale(Entity eid, Vector3 scale);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_particle_emitter_get_duration(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_duration(Entity eid, float duration);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_lifetime_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_lifetime_range(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<float> internal_m2n_particle_emitter_get_start_speed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_start_speed(Entity eid, Range<float> value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_particle_emitter_get_motion_easing(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_motion_easing(Entity eid, int easing);

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
        private static extern int internal_m2n_particle_emitter_get_sort_backend(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_particle_emitter_set_sort_backend(Entity eid, int backend);
    }
}
