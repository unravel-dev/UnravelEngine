#pragma once

#include "particle_types.h"

#include <base/basetypes.hpp>
#include <bx/easing.h>
#include <math/bbox.h>
#include <math/gradient.h>
#include <math/math.h>

#include <memory>
#include <vector>

namespace unravel
{
namespace ps_soa
{

/// Resolution the emitter's curves are pre-sampled at. Every particle reads them per frame, so
/// they are baked once per edit rather than evaluated key by key.
constexpr size_t curve_bake_resolution = 256;
/// The lifetime curve is read against a speed rather than a 0..1 lifetime, so it is baked finer.
constexpr size_t lifetime_by_emitter_speed_bake_resolution = 1024;
/// Longest particle lifetime, seconds. Particle ages are counted in 32-bit ticks on the GPU; keeping
/// lifetimes far below the tick wrap keeps a dead particle from ever reading as alive again.
constexpr float max_particle_lifetime = 3600.0f;

/**
 * @brief What a mesh shape spawns on, in the mesh's space (its node transforms applied): the vertices, the
 * triangles and their running area, which picks a triangle by its share of the surface.
 */
struct emission_mesh
{
    std::vector<math::vec3> positions;
    /// Unit vertex normals (up where the mesh has none).
    std::vector<math::vec3> normals;
    /// Three per triangle.
    std::vector<uint32_t> indices;
    /// Per triangle: the area of it and every triangle before it.
    std::vector<float> cumulative_area;
    math::bbox bounds{math::vec3(0.0f), math::vec3(0.0f)};
};

/**
 * @brief A burst of particles at set times of the emission cycle.
 */
struct emitter_burst
{
    /// Seconds into the emission cycle of the first firing.
    float time = 0.0f;
    /// Particles per firing, a random whole number between min and max.
    frange_t count{10.0f, 10.0f};
    /// Firings per cycle: 1 = once, 0 = every interval until the cycle ends.
    uint32_t cycles = 1;
    /// Seconds between the firings of one burst.
    float interval = 0.1f;
    /// Chance (0..1) that each firing happens.
    float probability = 1.0f;
};

/**
 * @brief Emission rate, shape, cycle and randomness authoring.
 */
struct emitter_emission_desc
{
    float particles_per_second = 50.0f;
    /// Particles per metre the emitter moves, spaced along its path (trails).
    float particles_per_meter = 0.0f;
    /// Bursts at set times of each emission cycle, on top of the rates.
    std::vector<emitter_burst> bursts;
    /// Length of one emission cycle, seconds. A looping emitter repeats it; a one-shot emitter emits
    /// for one cycle and stops once its last particle is gone.
    float duration = 2.0f;
    float start_delay = 0.0f;
    bool loop = true;
    /// A looping emitter starts full, as if it had been running for a whole particle lifetime, when it
    /// first plays after it is created or reset.
    bool prewarm = false;
    emitter_shape shape = emitter_shape::sphere;
    emitter_direction direction = emitter_direction::up;
    /// Half angle, degrees, of the cone around the shape's direction the particles are launched in.
    float direction_spread = 0.0f;
    spawn_location spawn_location = spawn_location::inside;
    math::vec3 shape_position{0.0f, 0.0f, 0.0f};
    math::vec3 shape_scale{1.0f, 1.0f, 1.0f};
    /// Cone shape: angle, degrees, between the axis and the slant the rim launches along.
    float cone_angle = 25.0f;
    /// Torus shape: the tube's radius as a fraction of the ring's.
    float torus_thickness = 0.25f;
    /// Mesh shape: where on the mesh particles spawn, and the mesh (null: they spawn at the origin).
    mesh_spawn_mode mesh_mode = mesh_spawn_mode::surface;
    std::shared_ptr<const emission_mesh> mesh;
    /// Replay the same particles every time instead of a fresh random sequence per emitter.
    bool use_fixed_seed = false;
    uint32_t random_seed = 0;

    void reset();
};

/**
 * @brief Motion authoring: physical launch speed, gravity, constant force and drag, evaluated in closed
 * form at any age.
 */
struct emitter_motion_desc
{
    simulation_space space = simulation_space::world;
    /// Particle lifetime, seconds, random between min and max per particle.
    frange_t lifetime{1.0f, 1.0f};
    /// Launch speed, metres per second, random between min and max.
    frange_t start_speed{2.0f, 3.0f};
    /// Multiples of standard gravity (9.81 m/s^2 down, in world space).
    float gravity_scale = 0.0f;
    /// Linear drag, 1/s: velocity decays by exp(-drag t).
    float drag = 0.0f;
    /// Constant acceleration in simulation space, m/s^2.
    math::vec3 acceleration{0.0f, 0.0f, 0.0f};
    /// Fraction of the emitter's own velocity new particles inherit.
    float inherit_velocity = 0.0f;
    /// Multiplier of the launch velocity over a particle's life (1 throughout = constant).
    math::gradient<float> speed_over_life;
    /// Turbulence: metres a curl-noise field displaces the particles by (0 = off), its features per metre,
    /// how fast it evolves (features per second) and a multiplier of the strength over life.
    float turbulence_strength = 0.0f;
    float turbulence_frequency = 1.0f;
    float turbulence_speed = 0.5f;
    math::gradient<float> turbulence_over_life;
    /// Collision (world-space emitters): planes with a world normal and an offset from the emitter's origin along
    /// it (normal . (p - origin) = offset; the normal's side is open). A bounce keeps restitution of the speed into
    /// the plane and loses friction of the speed along it.
    collision_mode collision = collision_mode::none;
    std::vector<math::vec4> collision_planes;
    float restitution = 0.5f;
    float friction = 0.2f;
    uint32_t max_bounces = 3;
    /// A particle touches a plane at this many halves of its size from it (0 = its centre).
    float collision_radius_scale = 1.0f;
    float spawn_along_path = 1.0f;
    /// Remaps a particle's life before its motion is evaluated (Linear = physical timing).
    bx::Easing::Enum motion_easing = bx::Easing::Linear;
    math::gradient<float> lifetime_by_emitter_speed_curve;
    frange_t lifetime_by_emitter_speed_range{0.0f, 10.0f};

    void reset();
};

/**
 * @brief Color / scale / speed-based appearance authoring.
 */
struct emitter_appearance_desc
{
    math::gradient<math::color> color_gradient;
    /// Particle size over life, random between the min and max curves.
    math::gradient<frange_t> size_over_life;
    /// Multipliers of the size along each particle's own axes: x and y stretch quads and meshes, z the depth of meshes.
    math::vec3 size_axes{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    float color_intensity = 1.0f;
    frange_t size_by_speed_multiplier{1.0f, 1.0f};
    frange_t size_by_speed_speed_range{0.0f, 10.0f};
    math::gradient<math::color> color_by_speed_gradient;
    frange_t color_by_speed_speed_range{0.0f, 10.0f};
    /// Multipliers of size_axes over life (1 throughout = constant): x and y for quads and meshes, z for meshes.
    math::gradient<math::vec3> size_axes_over_life;
    /// Each particle's color is a random mix of color_gradient and color_gradient_b.
    bool random_between_gradients = false;
    math::gradient<math::color> color_gradient_b;
    /// Multiplier of the color's intensity over life (1 throughout = none).
    math::gradient<float> intensity_over_life;

    void reset();
};

/**
 * @brief Render / texture-sheet / ordering / culling authoring (constant per emitter).
 */
struct emitter_render_desc
{
    texture_mode texture_mode = texture_mode::multi_channel;
    render_mode render_mode = render_mode::billboard;
    blend_mode blend_mode = blend_mode::normal;
    /// Particles turn so the texture's top points along their motion.
    bool align_to_direction = false;
    math::vec2 pivot{0.5f, 0.5f};
    math::vec2 tex_sheet_tiles{1.0f, 1.0f};
    float tex_sheet_cycles = 0.0f;
    bool tex_sheet_randomize = false;
    /// Crossfade from each texture sheet frame into the next instead of stepping between them.
    bool frame_blending = false;
    /// Start angle of a particle in its quad's plane, degrees, random between min and max.
    frange_t start_rotation{0.0f, 0.0f};
    /// Spin in degrees per second, random between min and max.
    frange_t spin_speed{0.0f, 0.0f};
    /// Half the particles spin the other way.
    bool random_spin_direction = false;
    /// Multiplier of the spin over life (1 throughout = constant spin).
    math::gradient<float> spin_over_life;
    /// The sheet frame follows tex_sheet_frame_curve (0 = first frame, 1 = last) instead of tex_sheet_cycles.
    bool tex_sheet_use_frame_curve = false;
    math::gradient<float> tex_sheet_frame_curve;
    /// Each particle plays one random row of the sheet (its frames run along the row).
    bool tex_sheet_random_row = false;
    /// Metres over which a particle fades out where it meets the scene behind it (soft particles);
    /// 0 keeps the hard edge.
    float soft_fade_distance = 0.0f;
    /// Draw order against other emitters, whatever their blend mode: lower values draw first and
    /// higher ones composite on top. Emitters of equal priority draw back to front by distance.
    int32_t sort_priority = 0;
    /// Order of this emitter's own particles.
    sort_mode sort_mode = sort_mode::automatic;
    /// Geometry each particle draws.
    particle_primitive primitive = particle_primitive::quad;
    /// Draw at half the view's resolution and composite over the full-resolution particles (not for
    /// multiply blend, which always draws at full resolution).
    bool half_resolution = false;
    /// Write motion vectors where the particles are nearly opaque, so the TAA reprojects them like moving
    /// objects (debris, sparks, embers) instead of only trusting its history less.
    bool write_velocity = false;
    /// Opacity from which a particle texel writes its motion.
    float velocity_alpha_threshold = 0.5f;
    /// Trail mode: seconds of the particle's path the trail covers, and its width at the tail as a share of the
    /// particle's size (it fades out toward the tail too).
    /// Cutout primitive: texels whose alpha is above this stay inside each frame's polygon.
    float cutout_alpha_threshold = 0.05f;
    float trail_length = 0.5f;
    float trail_width_end = 0.0f;
    /// Ribbon render mode: how the texture runs along the ribbon, and the seconds of age one repeat spans (tile).
    ribbon_texture_mode ribbon_texture_mode = ribbon_texture_mode::age;
    float ribbon_tile_time = 0.25f;
    /// Ribbon render mode: which way the ribbon's width runs.
    ribbon_facing ribbon_facing = ribbon_facing::camera;
    /// Stretched mode: quad length = size x stretch_length_scale + speed x stretch_speed_scale.
    float stretch_length_scale = 1.0f;
    float stretch_speed_scale = 0.1f;
    /// Stretched mode: the share of the camera's velocity taken off the particle's before it stretches (1 =
    /// stretch with the motion as the camera sees it).
    float stretch_camera_velocity_scale = 0.0f;
    /// Fixed axis mode: the axis the quad stands along, in the emitter's space (unit).
    math::vec3 facing_axis{0.0f, 1.0f, 0.0f};
    /// Metres each particle is pushed toward the camera (keeps sprites from cutting into walls).
    float camera_offset = 0.0f;
    /// Distances from the camera over which particles fade in: invisible at min, whole at max (0, 0 = off).
    frange_t camera_fade_range{0.0f, 0.0f};
    /// Largest size on screen, as a fraction of the view's height (0 = no limit).
    float max_screen_size = 0.0f;
    /// How the particles are lit (multiply blend stays unlit). Lit particles take the color as their albedo.
    particle_lighting lighting = particle_lighting::unlit;
    /// Surface lighting: how far the sun wraps past the lit half (0 = a hard terminator, 1 = lit all round).
    float lighting_wrap = 0.5f;
    /// Lit particles darken in the sun's shadow.
    bool receive_shadows = true;
    /// Particle lights: the brightest light_count particles light the scene as point lights of light_intensity
    /// candela at full brightness (scaled by their color's luminance x alpha) reaching light_range metres, in
    /// their own color or white. 0 = off.
    uint32_t light_count = 0;
    float light_intensity = 20.0f;
    float light_range = 3.0f;
    bool light_color_from_particle = true;
    /// Metres from the camera beyond which the emitter is not drawn (0 = no limit); particles fade out over the
    /// last draw_distance_fade metres before it.
    float max_draw_distance = 0.0f;
    float draw_distance_fade = 2.0f;
    /// Particles smaller than a pixel keep one pixel and fade by the area they lost, instead of flickering.
    bool subpixel_fade = false;
    /// Alpha erosion threshold over life: texels below it vanish (0 throughout = no erosion).
    math::gradient<float> erosion_over_life;
    /// How much the particles tell the temporal anti-aliasing to trust the current frame (0..1).
    float taa_response = 0.5f;
    bounds_mode bounds_mode = bounds_mode::automatic;
    /// Culling box in the emitter's local space, used when bounds_mode is fixed.
    math::bbox fixed_bounds{math::vec3(-5.0f), math::vec3(5.0f)};

    void reset();
};

/**
 * @brief Playback gates (playing / paused).
 */
struct emitter_playback_desc
{
    bool playing = true;
    bool paused = false;
    /// Whether the emitter plays from the moment it is created (scene load, play start, instantiation);
    /// off leaves it stopped until it is played.
    bool auto_play = true;

    void reset();
};

/**
 * @brief Full authoring description for an emitter (no transforms / runtime).
 */
struct emitter_desc
{
    emitter_emission_desc emission;
    emitter_motion_desc motion;
    emitter_appearance_desc appearance;
    emitter_render_desc render;
    emitter_playback_desc playback;
    /// The particle events (particle_event_type bits) the emitter records for take_particle_events.
    uint32_t event_mask = 0;

    void reset();
    /**
     * @brief Derive feature mask used to specialize update/render kernels.
     */
    auto bake_features() const -> emitter_feature;
};

/**
 * @brief True when @p curve holds @p value everywhere.
 */
auto is_constant_curve(const math::gradient<float>& curve, float value) -> bool;

/**
 * @brief True when @p curve holds 1 everywhere (the speed curve's neutral value).
 */
auto is_unit_curve(const math::gradient<float>& curve) -> bool;

/**
 * @brief True when every channel of @p curve holds 1 everywhere.
 */
auto is_unit_curve(const math::gradient<math::vec3>& curve) -> bool;

/**
 * @brief A curve that holds @p value throughout, baked.
 */
auto make_flat_curve(float value) -> math::gradient<float>;

/**
 * @brief A curve that holds (1, 1, 1) throughout, baked.
 */
auto make_unit_axes_curve() -> math::gradient<math::vec3>;

} // namespace ps_soa
} // namespace unravel
