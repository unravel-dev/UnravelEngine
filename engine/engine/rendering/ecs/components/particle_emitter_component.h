#pragma once

#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>
#include <graphics/texture.h>
#include <engine/assets/asset_handle.h>
#include <engine/rendering/batch_collector.h>
#include <engine/rendering/instanced_mesh_source.h>
#include <engine/rendering/model.h>
#include <bx/easing.h>
#include <bx/rng.h>
#include <math/math.h>
#include <base/basetypes.hpp>
#include <chrono>

namespace unravel
{
class mesh;

/**
 * @brief A burst another emitter fires where this emitter's particles are born, die or hit a collision plane.
 */
struct particle_sub_emitter
{
    /// What fires it.
    ps_soa::particle_event_type trigger = ps_soa::particle_event_type::death;
    /// The entity whose Particle Emitter bursts.
    entt::handle target;
    /// Particles per event, a random whole number between min and max.
    frange_t count{5.0f, 5.0f};
    /// Chance (0..1) that an event fires it.
    float probability = 1.0f;
    /// Share of the particle's velocity the burst's particles add to their own launch.
    float inherit_velocity = 0.0f;
    /// The burst's particles take the particle's color / size.
    bool inherit_color = false;
    bool inherit_size = false;
};

/**
 * @class particle_emitter_component
 * @brief Component that wraps the soa particle system emitter.
 */
class particle_emitter_component : public component_crtp<particle_emitter_component, owned_component>,
                                   public instanced_mesh_source
{
public:
    enum class culling_mode : uint8_t
    {
        /// Simulate every frame, seen or not.
        always_simulate,
        /// Freeze while no camera draws the emitter, then catch up on the time it missed: it wakes as
        /// if it had kept running.
        renderer_based,
        /// Freeze while no camera draws the emitter and resume where it froze.
        pause,
    };

    static void on_create_component(entt::registry& r, entt::entity e);
    static void on_destroy_component(entt::registry& r, entt::entity e);

    void set_enabled(bool enabled);
    auto is_enabled() const -> bool;

    void set_culling_mode(culling_mode mode);
    auto get_culling_mode() const -> culling_mode;

    void set_last_render_frame(uint64_t frame);
    auto get_last_render_frame() const noexcept -> uint64_t;
    auto is_newly_created() const noexcept -> bool;
    auto was_used_last_frame() const noexcept -> bool;

    auto get_emitter_handle() const -> ps_soa::emitter_handle;

    void set_shape(ps_soa::emitter_shape shape);
    auto get_shape() const -> ps_soa::emitter_shape;

    void set_direction(ps_soa::emitter_direction direction);
    auto get_direction() const -> ps_soa::emitter_direction;

    void set_spawn_location(ps_soa::spawn_location spawn_location);
    auto get_spawn_location() const -> ps_soa::spawn_location;

    void set_max_particles(uint32_t max_particles);
    auto get_max_particles() const -> uint32_t;

    /// Length of one emission cycle: a looping emitter repeats it, a one-shot emitter emits for one
    /// cycle and stops once its last particle is gone.
    void set_duration(std::chrono::duration<float> duration);
    auto get_duration() const -> std::chrono::duration<float>;

    /// Multiples of standard gravity (9.81 m/s^2, down in the world); negative values lift.
    void set_gravity_scale(float scale);
    auto get_gravity_scale() const -> float;

    /// Particles per second (0 = none).
    void set_emission_rate(float emission_rate);
    auto get_emission_rate() const -> float;

    /// Where a moving emitter's particles appear: 1 = along the path it moved this frame, 0 = at its current place.
    void set_spawn_along_path(float share);
    auto get_spawn_along_path() const -> float;

    /// Linear drag, 1/s: a particle's velocity decays by exp(-drag t).
    void set_drag(float drag);
    auto get_drag() const -> float;

    /// Launch speed, metres per second, random between min and max.
    void set_start_speed(const frange_t& speed);
    auto get_start_speed() const -> const frange_t&;

    /// Multiplier of the launch velocity over a particle's life.
    void set_speed_over_life(const math::gradient<float>& curve);
    auto get_speed_over_life() const -> const math::gradient<float>&;

    /// Fraction of the emitter's own velocity new particles inherit (world simulation space).
    void set_inherit_velocity(float fraction);
    auto get_inherit_velocity() const -> float;

    /// Half angle, degrees, of the cone around the shape's direction particles launch in.
    void set_direction_spread(float degrees);
    auto get_direction_spread() const -> float;

    /// Replay the same particles every time (from random_seed) instead of a fresh sequence per emitter.
    void set_use_fixed_seed(bool use_fixed_seed);
    auto get_use_fixed_seed() const -> bool;
    void set_random_seed(uint32_t seed);
    auto get_random_seed() const -> uint32_t;

    /// Constant acceleration, m/s^2, in simulation space (wind, buoyancy), on top of gravity.
    void set_acceleration(const math::vec3& acceleration);
    auto get_acceleration() const -> math::vec3;

    /// Offset of the emission shape from the emitter, in shape units.
    void set_shape_position(const math::vec3& position);
    auto get_shape_position() const -> math::vec3;

    /// Size of the emission shape, metres per unit.
    void set_shape_scale(const math::vec3& scale);
    auto get_shape_scale() const -> math::vec3;

    /// Size multiplier at the slow and the fast end of the size-by-speed range.
    void set_size_by_speed_multiplier(const frange_t& multiplier);
    auto get_size_by_speed_multiplier() const -> const frange_t&;

    /// Speeds, m/s, the size multiplier maps over (min = slow end, max = fast end).
    void set_size_by_speed_speed_range(const frange_t& speed_range);
    auto get_size_by_speed_speed_range() const -> const frange_t&;

    /// Color multiplier over the color-by-speed range (start = slow end).
    void set_color_by_speed_gradient(const math::gradient<math::color>& gradient);
    auto get_color_by_speed_gradient() const -> const math::gradient<math::color>&;

    /// Speeds, m/s, the color-by-speed gradient maps over.
    void set_color_by_speed_speed_range(const frange_t& speed_range);
    auto get_color_by_speed_speed_range() const -> const frange_t&;

    /// Lifetime multiplier over the emitter's speed range (start = slow end).
    void set_lifetime_by_emitter_speed_curve(const math::gradient<float>& curve);
    auto get_lifetime_by_emitter_speed_curve() const -> const math::gradient<float>&;

    /// Emitter speeds, m/s, the lifetime curve maps over.
    void set_lifetime_by_emitter_speed_range(const frange_t& speed_range);
    auto get_lifetime_by_emitter_speed_range() const -> const frange_t&;

    /// Particle lifetime, seconds, random between min and max.
    void set_lifetime(const frange_t& lifetime);
    auto get_lifetime() const -> const frange_t&;

    /// Particle size over life, metres, random between the min and max curves.
    void set_size_over_life(const math::gradient<frange_t>& curve);
    auto get_size_over_life() const -> const math::gradient<frange_t>&;

    /// Multipliers of the size along each particle's own axes: x and y stretch quads and meshes, z the depth of meshes.
    void set_size_axes(const math::vec3& axes);
    auto get_size_axes() const -> math::vec3;

    void set_opacity(float opacity);
    auto get_opacity() const -> float;

    void set_color_intensity(float intensity);
    auto get_color_intensity() const -> float;

    /// Starts emitting (again from the start of the cycle when stopped).
    void play();
    /// Stops emitting; the particles alive live out their lives.
    void stop();
    /// Stops emitting and clears every particle.
    void stop_and_reset();
    /// Freezes the particles where they are; resume continues from there.
    void pause();
    void resume();
    /// The same on every emitter in the entity's children (the hierarchy below it, not the sub emitters).
    void play_child_emitters();
    void stop_child_emitters();
    void stop_and_reset_child_emitters();
    void pause_child_emitters();
    void resume_child_emitters();
    auto is_playing() const -> bool;
    auto is_paused() const -> bool;
    auto is_stopped() const -> bool;

    /**
     * @brief Queues a burst of @p count particles for the next update, whatever the emission rate,
     * start delay, loop or play state (ps_soa::emit). A queued burst wakes an emitter that culling
     * froze; a disabled emitter ignores it.
     */
    void emit(uint32_t count, const ps_soa::emit_params& params = {});

    void set_loop(bool loop);
    auto is_loop() const -> bool;

    /// A looping emitter starts full, as if it had been running for a whole particle lifetime, when it
    /// first plays after it is created or reset.
    void set_prewarm(bool prewarm);
    auto get_prewarm() const -> bool;

    /// Whether the emitter plays from the moment it is created (scene load, play start, instantiation);
    /// off leaves it stopped until it is played. Setting it after the emitter's first frame only
    /// changes what the next created copy does.
    void set_auto_play(bool auto_play);
    auto get_auto_play() const -> bool;

    void set_start_delay(std::chrono::duration<float> delay);
    auto get_start_delay() const -> std::chrono::duration<float>;

    void set_align_to_direction(bool align);
    auto get_align_to_direction() const -> bool;

    void set_pivot(const math::vec2& pivot);
    auto get_pivot() const -> math::vec2;

    void set_color_gradient(const math::gradient<math::color>& gradient);
    auto get_color_gradient() const -> const math::gradient<math::color>&;

    /// Remaps a particle's life before its motion is evaluated (Linear = physical timing).
    void set_motion_easing(bx::Easing::Enum easing);
    auto get_motion_easing() const -> bx::Easing::Enum;

    void set_simulation_space(ps_soa::simulation_space space);
    auto get_simulation_space() const -> ps_soa::simulation_space;

    /// Where the emitter's particles are culled and sorted for each view: a compute pass (GPU) or the main thread.
    void set_sort_backend(ps_soa::particle_sort_backend backend);
    auto get_sort_backend() const -> ps_soa::particle_sort_backend;

    auto get_num_particles() const -> uint32_t;
    auto get_world_bounds() const -> math::bbox;
    auto get_updated_world_bounds(const math::transform& world_transform) const -> math::bbox;

    /// The texture each particle draws; invalid when none is set.
    void set_texture(const asset_handle<gfx::texture>& texture);
    auto get_texture() const -> const asset_handle<gfx::texture>&;
    /// The texture the particles draw with: theirs, or the default color map when none is set.
    auto get_drawn_texture() const -> const asset_handle<gfx::texture>&;

    void set_texture_mode(ps_soa::texture_mode mode);
    auto get_texture_mode() const -> ps_soa::texture_mode;

    void set_render_mode(ps_soa::render_mode mode);
    auto get_render_mode() const -> ps_soa::render_mode;

    void set_blend_mode(ps_soa::blend_mode mode);
    auto get_blend_mode() const -> ps_soa::blend_mode;

    void set_texture_sheet_tiles(math::vec2 tiles);
    auto get_texture_sheet_tiles() const -> math::vec2;

    void set_texture_sheet_cycles(float cycles);
    auto get_texture_sheet_cycles() const -> float;

    void set_texture_sheet_randomize(bool randomize);
    auto get_texture_sheet_randomize() const -> bool;

    /// Crossfade from each texture sheet frame into the next.
    void set_frame_blending(bool blend);
    auto get_frame_blending() const -> bool;

    /// Start angle of each particle in its quad's plane, degrees, random between min and max.
    void set_start_rotation(const frange_t& degrees);
    auto get_start_rotation() const -> const frange_t&;

    /// Spin in degrees per second, random between min and max.
    void set_spin_speed(const frange_t& degrees_per_second);
    auto get_spin_speed() const -> const frange_t&;

    /// Half the particles spin the other way.
    void set_random_spin_direction(bool random);
    auto get_random_spin_direction() const -> bool;

    /// Metres over which particles fade where they meet the scene behind them; 0 = hard edge.
    void set_soft_fade_distance(float distance);
    auto get_soft_fade_distance() const -> float;

    /// Draw order against other emitters: lower draws first, higher composites on top. Equal
    /// priorities draw back to front by distance.
    void set_sort_priority(int32_t priority);
    auto get_sort_priority() const -> int32_t;

    /// Stretched mode: quad length as a multiple of the particle size.
    void set_stretch_length_scale(float value);
    auto get_stretch_length_scale() const -> float;

    /// Stretched mode: quad length added per metre per second of speed.
    void set_stretch_speed_scale(float value);
    auto get_stretch_speed_scale() const -> float;
    /// Stretched render mode: the share of the camera's velocity taken off the particles' before they stretch.
    void set_stretch_camera_velocity_scale(float value);
    auto get_stretch_camera_velocity_scale() const -> float;
    /// Fixed axis render mode: the axis the quads stand along, in the emitter's space (normalized; zero = +Y).
    void set_facing_axis(const math::vec3& value);
    auto get_facing_axis() const -> const math::vec3&;

    /// Metres each particle is pushed toward the camera.
    void set_camera_offset(float value);
    auto get_camera_offset() const -> float;

    /// Largest size on screen as a fraction of the view's height (0 = no limit).
    void set_max_screen_size(float value);
    auto get_max_screen_size() const -> float;

    /// How much the particles tell the temporal anti-aliasing to trust the current frame (0..1).
    void set_taa_response(float value);
    auto get_taa_response() const -> float;

    /// Distances from the camera over which particles fade in: invisible at min, whole at max (0, 0 = off).
    void set_camera_fade_range(const frange_t& range);
    auto get_camera_fade_range() const -> const frange_t&;

    /// Particles smaller than a pixel keep one pixel and fade by the area they lost.
    void set_subpixel_fade(bool fade);
    auto get_subpixel_fade() const -> bool;

    /// Alpha erosion threshold over life: texels below it vanish (0 throughout = none).
    void set_erosion_over_life(const math::gradient<float>& curve);
    auto get_erosion_over_life() const -> const math::gradient<float>&;

    /// Order of the emitter's own particles.
    void set_sort_mode(ps_soa::sort_mode mode);
    auto get_sort_mode() const -> ps_soa::sort_mode;

    /// Geometry each particle draws: the full quad, or an octagon that skips its transparent corners.
    void set_primitive(ps_soa::particle_primitive primitive);
    auto get_primitive() const -> ps_soa::particle_primitive;

    /// Draw at half the view's resolution (a quarter of the fill cost) and composite over the full-resolution
    /// particles. Ignored for multiply blend.
    void set_half_resolution(bool half_resolution);
    auto get_half_resolution() const -> bool;

    /// Write motion vectors where the particles are at least velocity_alpha_threshold opaque, so the TAA
    /// reprojects them like moving objects.
    void set_write_velocity(bool write_velocity);
    auto get_write_velocity() const -> bool;
    void set_velocity_alpha_threshold(float threshold);
    auto get_velocity_alpha_threshold() const -> float;

    /// Trail mode: seconds of the particle's path the trail covers.
    void set_trail_length(float value);
    auto get_trail_length() const -> float;

    /// Trail mode: the trail's width at the tail as a share of the particle's size.
    void set_trail_width_end(float value);
    auto get_trail_width_end() const -> float;

    /// Ribbon render mode: how the texture runs along the ribbon.
    void set_ribbon_texture_mode(ps_soa::ribbon_texture_mode mode);
    auto get_ribbon_texture_mode() const -> ps_soa::ribbon_texture_mode;
    /// Ribbon render mode, tile texture mode: seconds of the particles' age one texture repeat spans.
    void set_ribbon_tile_time(float value);
    auto get_ribbon_tile_time() const -> float;
    /// Ribbon render mode: which way the ribbon's width runs (the camera, or each particle's launch direction).
    void set_ribbon_facing(ps_soa::ribbon_facing facing);
    auto get_ribbon_facing() const -> ps_soa::ribbon_facing;

    /// Collision bounce: share of the speed into the plane a bounce keeps.
    void set_restitution(float value);
    auto get_restitution() const -> float;

    /// Collision bounce: share of the speed along the plane a bounce loses.
    void set_friction(float value);
    auto get_friction() const -> float;

    /// Collision bounce: bounces before the particle dies at its next contact.
    void set_max_bounces(uint32_t value);
    auto get_max_bounces() const -> uint32_t;
    /// Collision: a particle touches a plane at this many halves of its size from it (0 = its centre).
    void set_collision_radius_scale(float value);
    auto get_collision_radius_scale() const -> float;

    /// Collision (world-space emitters): kill or bounce where a particle's path first crosses a plane.
    void set_collision(ps_soa::collision_mode mode);
    auto get_collision() const -> ps_soa::collision_mode;
    /// Planes: world normal (xyz) and offset (w) from the emitter's origin along it; the normal's side is open.
    void set_collision_planes(const std::vector<math::vec4>& planes);
    auto get_collision_planes() const -> const std::vector<math::vec4>&;

    /// Bursts other emitters fire where this emitter's particles are born, die or collide.
    void set_sub_emitters(const std::vector<particle_sub_emitter>& sub_emitters);
    auto get_sub_emitters() const -> const std::vector<particle_sub_emitter>&;
    /// Fires the sub-emitters for the events of the last update. Call once per frame after every emitter
    /// updated, on one thread: it queues bursts on other emitters.
    void fire_sub_emitters();

    /// Particles that light the scene as point lights (the brightest; 0 = off).
    void set_light_count(uint32_t value);
    auto get_light_count() const -> uint32_t;

    /// Candela of a particle light at full particle brightness.
    void set_light_intensity(float value);
    auto get_light_intensity() const -> float;

    /// Metres a particle light reaches.
    void set_light_range(float value);
    auto get_light_range() const -> float;

    /// Particle lights take their particle's color (off: white).
    void set_light_color_from_particle(bool value);
    auto get_light_color_from_particle() const -> bool;

    /// Surface lighting: how far the sun wraps past the lit half (0 = hard terminator, 1 = lit all round).
    void set_lighting_wrap(float value);
    auto get_lighting_wrap() const -> float;

    /// Surface lighting: a tangent-space normal map (green up) laid out like the texture, sheet frames included.
    void set_normal_texture(const asset_handle<gfx::texture>& value);
    auto get_normal_texture() const -> const asset_handle<gfx::texture>&;

    /// How the particles are lit (multiply blend stays unlit); lit particles take their color as albedo.
    void set_lighting(ps_soa::particle_lighting lighting);
    auto get_lighting() const -> ps_soa::particle_lighting;

    /// Lit particles darken in the sun's shadow.
    void set_receive_shadows(bool value);
    auto get_receive_shadows() const -> bool;

    /// Metres from the camera beyond which the emitter is not drawn (0 = no limit).
    void set_max_draw_distance(float value);
    auto get_max_draw_distance() const -> float;

    /// Metres before the max draw distance over which particles fade out.
    void set_draw_distance_fade(float value);
    auto get_draw_distance_fade() const -> float;

    /// Metres a curl-noise field displaces the particles by (0 = off).
    void set_turbulence_strength(float value);
    auto get_turbulence_strength() const -> float;

    /// Noise features per metre: higher = smaller, busier swirls.
    void set_turbulence_frequency(float value);
    auto get_turbulence_frequency() const -> float;

    /// How fast the turbulence field evolves, in swirls per second.
    void set_turbulence_speed(float value);
    auto get_turbulence_speed() const -> float;

    /// Multiplier of the turbulence strength over life (1 throughout = constant).
    void set_turbulence_over_life(const math::gradient<float>& value);
    auto get_turbulence_over_life() const -> const math::gradient<float>&;

    /// Cone shape: angle, degrees, between the axis and the slant the rim launches along.
    void set_cone_angle(float value);
    auto get_cone_angle() const -> float;

    /// Torus shape: the tube's radius as a fraction of the ring's.
    void set_torus_thickness(float value);
    auto get_torus_thickness() const -> float;

    /// Mesh shape: the mesh particles spawn on, and where on it (surface by area, or vertices).
    void set_shape_mesh(const asset_handle<mesh>& value);
    auto get_shape_mesh() const -> const asset_handle<mesh>&;
    void set_shape_mesh_mode(ps_soa::mesh_spawn_mode mode);
    auto get_shape_mesh_mode() const -> ps_soa::mesh_spawn_mode;

    /// Particles per metre the emitter moves, spaced along its path (trails).
    void set_rate_over_distance(float value);
    auto get_rate_over_distance() const -> float;

    /// Bursts at set times of each emission cycle, on top of the rates.
    void set_bursts(const std::vector<ps_soa::emitter_burst>& bursts);
    auto get_bursts() const -> const std::vector<ps_soa::emitter_burst>&;

    /// Multipliers of the size axes over life (1 throughout = constant): x and y for quads and meshes, z for meshes.
    void set_size_axes_over_life(const math::gradient<math::vec3>& value);
    auto get_size_axes_over_life() const -> const math::gradient<math::vec3>&;

    /// Each particle's color is a random mix of the color gradient and color_gradient_b.
    void set_random_between_gradients(bool value);
    auto get_random_between_gradients() const -> bool;

    /// Second color over life, mixed with the color gradient by a random amount per particle.
    void set_color_gradient_b(const math::gradient<math::color>& value);
    auto get_color_gradient_b() const -> const math::gradient<math::color>&;

    /// Multiplier of the color's intensity over life (1 throughout = none).
    void set_intensity_over_life(const math::gradient<float>& value);
    auto get_intensity_over_life() const -> const math::gradient<float>&;

    /// Multiplier of the spin over life (1 throughout = constant spin).
    void set_spin_over_life(const math::gradient<float>& value);
    auto get_spin_over_life() const -> const math::gradient<float>&;

    /// The sheet frame follows the frame curve over life instead of the cycles.
    void set_texture_sheet_use_frame_curve(bool value);
    auto get_texture_sheet_use_frame_curve() const -> bool;

    /// Sheet frame over life, 0 = first frame, 1 = last (tex_sheet_use_frame_curve).
    void set_texture_sheet_frame_curve(const math::gradient<float>& value);
    auto get_texture_sheet_frame_curve() const -> const math::gradient<float>&;

    /// Each particle plays one random row of the sheet.
    void set_texture_sheet_random_row(bool value);
    auto get_texture_sheet_random_row() const -> bool;

    /// Culling bounds: worked out from the settings, or a fixed box in the emitter's local space.
    void set_bounds_mode(ps_soa::bounds_mode mode);
    auto get_bounds_mode() const -> ps_soa::bounds_mode;
    void set_fixed_bounds(const math::bbox& bounds);
    auto get_fixed_bounds() const -> const math::bbox&;

    /// Mesh render mode: the mesh each particle draws.
    void set_render_mesh(const asset_handle<mesh>& value);
    auto get_render_mesh() const -> const asset_handle<mesh>&;
    /// Mesh render mode: the material of every slot of the mesh; unset, the mesh's own materials.
    void set_render_material(const asset_handle<material>& value);
    auto get_render_material() const -> const asset_handle<material>&;
    /// Mesh render mode: the particles cast shadows.
    void set_mesh_cast_shadows(bool value);
    auto get_mesh_cast_shadows() const -> bool;

    /**
     * @brief Mesh render mode: the world matrix of every particle's mesh this frame. With node transforms in
     * the mesh there is one per particle and submesh: the instance of submesh s of particle p is
     * [p * stride + s] (stride = the submesh count); without them one per particle, shared by its submeshes.
     */
    auto get_mesh_transforms() const -> const std::vector<math::mat4>&;
    /// The world box the particles' meshes fill this frame (empty without particles).
    auto get_mesh_bounds() const -> const math::bbox& override;
    /// True in mesh render mode once the mesh has loaded.
    auto is_mesh_render() const -> bool;
    /// Mesh render mode: adds an instance per particle and submesh to @p collector (the scene's instanced
    /// geometry), with last frame's matrices when @p with_previous (the velocity pass draws those).
    void collect_mesh_instances(batch_collector& collector, bool with_previous) const override;
    /**
     * @brief Mesh render mode: adds the particles' shadow casting submeshes to the shadow views in @p view_mask
     * (bit i = collectors[i]).
     * @return True when anything was collected.
     */
    auto collect_mesh_shadow_instances(std::vector<shadow_batch_collector>& collectors, uint8_t view_mask) const
        -> bool override;
    /**
     * @brief What the particles' shadows draw, as a key: their matrices and opacities, the meshes and what the
     * shadows read of the materials. Unchanged while the emitter is paused or frozen, so a cached shadow map keeps it.
     */
    auto get_mesh_shadow_key() const -> uint64_t override;
    /// Builds the model the particles draw once the render mesh has loaded, and again when the mesh or the
    /// material changes. Main thread (it resolves the mesh's materials).
    void refresh_render_mesh();
    /// Mesh shape: builds the spawn data once its mesh has loaded (again when it is replaced or reloaded).
    /// Main thread (it reads the asset).
    void refresh_emission_mesh();

    /// Cutout primitive: texels whose alpha is above this stay inside each frame's polygon.
    void set_cutout_alpha_threshold(float value);
    auto get_cutout_alpha_threshold() const -> float;
    /// Points the emitter at the cutout table of its texture and sheet when it draws the cutout primitive (baked on
    /// a worker the first time). Main thread.
    void refresh_cutout();

    void update_emitter(const math::transform& world_transform, delta_t dt);

    /// True when the last update advanced the simulation; false when it was frozen by the
    /// renderer-based culling, or the emitter is disabled.
    auto is_simulated() const noexcept -> bool;

    auto get_desc() const -> const ps_soa::emitter_desc&;

    /// Clears every particle and starts the emitter's clock over.
    void reset_emitter();

private:
    /// Builds the simulation's emitter again (its capacity changed, or the component was created).
    void recreate_emitter();
    /// Mesh render mode: matrices per particle: 1, or the mesh's submesh count when its submeshes have node transforms.
    auto get_mesh_instance_stride() const -> uint32_t;

    bool enabled_ = true;
    culling_mode culling_mode_ = culling_mode::renderer_based;
    uint64_t last_render_frame_ = 0;
    /// The render frame of the last update; 0 before the first.
    uint64_t last_update_frame_ = 0;
    bool is_simulated_ = false;
    uint32_t max_particles_ = 1024;
    ps_soa::particle_sort_backend sort_backend_ = ps_soa::particle_sort_backend::gpu;
    ps_soa::emitter_handle emitter_handle_{};
    ps_soa::emitter_desc desc_{};
    ps_soa::emitter_transform_state transform_state_{};
    /// Mesh render mode: evaluates the particles' world matrices and the box they fill.
    void update_mesh_instances();

    asset_handle<gfx::texture> texture_;
    asset_handle<gfx::texture> normal_texture_;
    std::vector<particle_sub_emitter> sub_emitters_;
    std::vector<ps_soa::particle_event> sub_emitter_events_;
    bx::RngMwc sub_emitter_rng_{};
    bool is_sub_emitter_rng_seeded_ = false;
    /// The mesh shape's mesh, and the loaded mesh its spawn data was built from (compared, never read).
    asset_handle<mesh> shape_mesh_;
    const mesh* emission_mesh_source_ = nullptr;
    /// Mesh render mode: what the particles draw and the loaded mesh it was built from (compared, never read).
    asset_handle<mesh> render_mesh_;
    asset_handle<material> render_material_;
    bool mesh_cast_shadows_ = true;
    model mesh_model_;
    const mesh* render_mesh_source_ = nullptr;
    bool is_render_mesh_dirty_ = false;
    /// The mesh's submesh node transforms (empty when all are identity) and the farthest its vertices reach from
    /// its origin.
    std::vector<math::mat4> mesh_node_transforms_;
    float mesh_radius_ = 0.0f;
    std::vector<math::mat4> particle_transforms_;
    std::vector<math::mat4> prev_particle_transforms_;
    /// Each particle's opacity (its color's alpha): meshes fade out by dithering.
    std::vector<float> mesh_opacity_;
    std::vector<math::mat4> mesh_transforms_;
    std::vector<math::mat4> prev_mesh_transforms_;
    math::bbox mesh_bounds_;
};

} // namespace unravel
