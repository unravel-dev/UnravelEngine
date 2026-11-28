#pragma once

#include "emitter_desc.h"
#include "emitter_runtime.h"
#include "particle_cutout.h"
#include "particle_eval.h"
#include "particle_gpu_schema.h"
#include "particle_types.h"
#include <bgfx/bgfx.h>
#include <engine/rendering/pipeline/pre_exposure.h>
#include <graphics/format.h>
#include <context/context.hpp>
#include <math/bbox.h>
#include <math/math.h>

#include <functional>
#include <vector>

namespace unravel
{
class graph_material;

namespace ps_soa
{
/**
 * @brief How a batch that draws with a material is drawn (render_view_params::begin_material).
 */
struct material_draw
{
    bool is_lit{};
    /// A lit draw takes the GI's light around its particles in place of the sky's (render_view_params::bind_lighting
    /// bound the radiance cache).
    bool reads_gi{};
    bool is_velocity{};
    /// The address bits a ribbon replaces in the sampler state of the material's textures that read as their texture
    /// says (@ref address_mask), and what it puts there (@ref address_flags): a ribbon clamps across itself, and along
    /// itself unless its texture tiles, so its edges and its two ends do not filter into each other. 0: nothing.
    uint32_t address_mask{};
    uint32_t address_flags{};
};
} // namespace ps_soa

namespace ps_soa
{

/**
 * @brief Overrides for the particles of one burst (see emit). A field applies only when its flag
 * is set in mask; the others come from the emitter as for any spawned particle.
 */
struct emit_params
{
    static constexpr uint32_t position_flag = 1u << 0;
    static constexpr uint32_t velocity_flag = 1u << 1;
    static constexpr uint32_t color_flag = 1u << 2;
    static constexpr uint32_t size_flag = 1u << 3;
    static constexpr uint32_t lifetime_flag = 1u << 4;
    static constexpr uint32_t added_velocity_flag = 1u << 5;

    /// Spawn point in simulation space (world, or the emitter's local space); the shape's offset is
    /// replaced, the direction it gives is kept.
    math::vec3 position{};
    /// Initial velocity in simulation-space units per second; gravity, force and drag still apply.
    math::vec3 velocity{};
    /// Picker (sRGB) tint multiplied into the color over lifetime.
    math::color color{};
    /// Size at birth: the size curve is scaled to start at it, or to peak at it when the curve
    /// starts at 0.
    float size = 1.0f;
    /// Lifetime in seconds.
    float lifetime = 1.0f;
    /// Added to each particle's own launch velocity (simulation space).
    math::vec3 added_velocity{};
    uint32_t mask = 0;
};

/**
 * @brief Project-wide particle quality (settings::graphics_settings::particle_settings), applied every frame.
 */
struct particle_quality
{
    /// Multiplier of every emitter's rates and bursts.
    float spawn_scale = 1.0f;
    /// Sort where emitters ask for it (off: spawn order).
    bool sorting = true;
    /// The renderer draws every eligible emitter at half resolution.
    bool force_half_resolution = false;
    /// The renderer skips emitters smaller on screen than this fraction of the view's height (0 = off).
    float min_screen_size = 0.0f;
    /// The renderer draws at most this many particles per view, dropping the emitters smallest on screen (0 = no limit).
    uint32_t max_drawn_particles = 0;
    /// Multiplier of every emitter's capacity (0..1): spawns past the scaled capacity are dropped.
    float max_particles_scale = 1.0f;
    /// The most expensive lighting mode particles use: surface ones light as volumetric below surface, lit ones
    /// draw unlit at unlit.
    particle_lighting max_lighting = particle_lighting::surface;
    /// The most casts a frame spends on particles that collide with physics colliders; paths past it are swept in
    /// later frames.
    uint32_t max_collision_casts = 2048;
    /// What the particles that collide with scene surfaces are tested against.
    scene_collision scene_collision = scene_collision::depth_buffer;
};

void set_quality(const particle_quality& quality);
auto get_quality() -> const particle_quality&;

/// @p lighting as the quality allows it (particle_quality::max_lighting).
auto get_capped_lighting(particle_lighting lighting) -> particle_lighting;

/// Builds a cutout table's polygons (cutout_polygon per sheet frame); runs on a worker thread.
using cutout_baker = std::function<std::vector<cutout_polygon>()>;

/**
 * @brief Points the emitter at the cutout table of @p key (0: none), baked by @p bake on a worker thread the first
 * time any emitter asks for it and shared by every emitter of that key until the last one lets go (re-pointed or
 * destroyed). Until the table is in, the cutout primitive draws the whole quad. Main thread.
 */
void set_emitter_cutout(emitter_handle handle, uint64_t key, const cutout_baker& bake);
/// The cutout key the emitter points at (0: none).
auto get_emitter_cutout(emitter_handle handle) -> uint64_t;

/**
 * @brief Initialize the particle system.
 * @param max_emitters Maximum concurrent emitters.
 */
void init(uint16_t max_emitters = 64);

/**
 * @brief Load the particle programs and enable GPU culling and sorting when they are available.
 * @param ctx Runtime context with asset_manager.
 */
void init_gpu(rtti::context& ctx);

/**
 * @brief Shutdown and free all emitters / GPU resources.
 */
void shutdown();

/**
 * @brief Default sorting / culling backend for emitters without their own choice.
 */
void set_default_sort_backend(particle_sort_backend backend);
auto get_default_sort_backend() -> particle_sort_backend;

/**
 * @brief Where one emitter's particles are culled and sorted each view: the GPU (default) or the CPU.
 * Particles are always simulated the same way; only the per-view work moves.
 */
void set_emitter_sort_backend(emitter_handle handle, particle_sort_backend backend);
auto get_emitter_sort_backend(emitter_handle handle) -> particle_sort_backend;

/**
 * @brief True when the GPU cull / sort programs loaded.
 */
auto is_gpu_sort_available() -> bool;

/**
 * @brief Create an emitter holding up to @p max_particles live particles.
 */
auto create_emitter(uint32_t max_particles) -> emitter_handle;

/**
 * @brief Destroy an emitter and free its particle storage.
 */
void destroy_emitter(emitter_handle handle);

/**
 * @brief Clear particles and reset sim bookkeeping.
 */
void reset_emitter(emitter_handle handle);

/**
 * @brief Advance simulation for one emitter. Safe to call for different emitters in parallel.
 * @param handle Emitter handle.
 * @param dt Frame delta seconds.
 * @param desc Authoring description (gradients, rates, render flags). Playback fields in desc are ignored.
 * @param transform Current/previous world transform; previous is updated after the call.
 * @param playback Mutable playing/paused; cleared when a one-shot emitter finishes.
 */
void update_emitter(emitter_handle handle,
                    float dt,
                    const emitter_desc& desc,
                    emitter_transform_state& transform,
                    emitter_playback_desc& playback);

/**
 * @brief A frame the renderer-based culling froze the emitter for: its particles hold their pose and
 * its bounds follow the transform. With @p catch_up the skipped @p dt is simulated when it next updates,
 * so it wakes as if it had kept running; without it the emitter resumes where it froze.
 */
void update_emitter_frozen(emitter_handle handle,
                           float dt,
                           const emitter_desc& desc,
                           emitter_transform_state& transform,
                           bool catch_up);

/**
 * @brief Queues a burst of @p count particles, spawned at the emitter's next update whatever its
 * rate, start delay, loop or play state; capacity still caps it.
 */
void emit(emitter_handle handle, uint32_t count, const emit_params& params);
/**
 * @brief True while a burst waits for the emitter's next update.
 */
auto has_pending_bursts(emitter_handle handle) -> bool;

/**
 * @brief Uploads what the emitters changed since the last call (new particles, emitter tables, curve
 * sets) for the views drawn next.
 * @note Call on the main thread after updating a scene's emitters, before its views draw.
 */
void sync_gpu_simulation();

/**
 * @brief True after the first successful update.
 */
auto has_updated(emitter_handle handle) -> bool;

/**
 * @brief World-space AABB used for culling.
 */
void get_aabb(emitter_handle handle, math::bbox& out_aabb);
/// The world-space box @p handle's particles are lit in: their centres with the reach of the turbulence and the
/// camera offset, without the size of their quads, which the culling box holds too.
void get_lit_aabb(emitter_handle handle, math::bbox& out_aabb);

/**
 * @brief Live particle count.
 */
auto get_num_particles(emitter_handle handle) -> uint32_t;

/**
 * @brief How much further the emitter's stretched particles may reach, beyond its bounds, when the view's camera moves
 * at @p camera_speed (m/s): they stretch against the camera's motion. 0 for other render modes.
 */
auto get_camera_stretch_reach(emitter_handle handle, float camera_speed) -> float;

/**
 * @brief The live particles of an emitter at its current time (CPU evaluation, for tools and tests).
 */
void get_particle_states(emitter_handle handle, std::vector<particle_state>& out_states);

/// The entity of a particle_event or a particle_sweep_hit that has none.
constexpr uint32_t no_particle_entity = 0xFFFFFFFFu;

/// Something that happened to a particle, as a sub-emitter or a script reads it.
struct particle_event
{
    particle_event_type type = particle_event_type::death;
    /// World position and velocity then (a collision: the velocity the particle arrived with).
    math::vec3 position{0.0f};
    math::vec3 velocity{0.0f};
    /// Linear color without the emitter's intensity, and the size.
    math::vec4 color{1.0f};
    float size = 0.0f;
    /// A collision: the unit normal of the surface the particle touched, where on the surface it touched (the
    /// particle's position less its radius), and the entity of the collider it belongs to (no_particle_entity for a
    /// collision plane).
    math::vec3 normal{0.0f};
    math::vec3 surface_point{0.0f};
    uint32_t entity = no_particle_entity;
};

/**
 * @brief Moves the events the emitter recorded since the last call (emitter_desc::event_mask) to @p out_events.
 */
void take_particle_events(emitter_handle handle, std::vector<particle_event>& out_events);

/// What a sweep of a piece of a particle's path touched.
struct particle_sweep_hit
{
    /// How far along the sweep the sphere's centre had come when it touched, metres.
    float distance = 0.0f;
    /// Where it touched and the surface's unit normal there, world space.
    math::vec3 point{0.0f};
    math::vec3 normal{0.0f, 1.0f, 0.0f};
    /// The entity the collider belongs to.
    uint32_t entity = no_particle_entity;
    /// The sphere was in contact with a collider where the sweep started. Such a collider is no hit: the particle
    /// is born in it, or lies against it.
    bool starts_touching = false;
};

/**
 * @brief Sweeps a sphere of @p radius (0: a ray) from @p from to @p to through the colliders of the layer mask
 * @p layers. The colliders it is in contact with where it starts do not count (particle_sweep_hit::starts_touching
 * says there were some). False when it touches no other; else @p hit is the first it touches.
 */
using particle_sweep =
    std::function<bool(const math::vec3& from, const math::vec3& to, float radius, int layers, particle_sweep_hit& hit)>;

/// What one resolve_collisions call did.
struct collision_resolve_stats
{
    /// Sweeps made, and how many of them touched a surface.
    uint32_t casts = 0;
    uint32_t hits = 0;
    /// Contacts that happened within the call: the records of their particles changed after the emitter's update.
    uint32_t contacts = 0;
    /// Particles whose path still waits for a sweep because the cast limit was reached.
    uint32_t waiting = 0;
};

/**
 * @brief Sweeps the paths of the emitter's particles that wait for it (collision_source::physics_colliders) with
 * @p sweep, in at most @p max_casts sweeps (up to three more for a path that starts in contact with a collider), and
 * schedules the contacts it finds; a contact that lies behind the emitter's clock happens at once. With an empty
 * @p sweep (no physics world), or when the emitter no longer collides with the scene, the waiting paths are
 * forgotten; when sweeps start again, every live particle's path is swept from then on.
 * @note One thread, after the frame's update_emitter calls and before take_particle_events. Call it every frame for
 * an emitter that collides with colliders, with or without waiting paths.
 */
auto resolve_collisions(emitter_handle handle, const emitter_desc& desc, const particle_sweep& sweep, uint32_t max_casts)
    -> collision_resolve_stats;

/// How many of the emitter's particles wait for a sweep of their path (resolve_collisions).
auto get_waiting_sweeps(emitter_handle handle) -> uint32_t;

/// The scene a view's particles collide with (collide_with_scene).
struct scene_collision_view
{
    /// A view with the camera's matrices and view origin that runs before the views that draw the particles.
    bgfx::ViewId view = 0;
    /// The view's opaque depth and its G-buffer's normals.
    bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normals = BGFX_INVALID_HANDLE;
    /// The view's distance field with its level (one vec4 a level) and sampling uniforms: with all three the
    /// particles collide with the field instead of the depth.
    bgfx::TextureHandle distance_field = BGFX_INVALID_HANDLE;
    const float* distance_field_levels = nullptr;
    const float* distance_field_params = nullptr;
};

/// True where particles can collide with scene surfaces: the GPU culls and sorts them and the collision kernel loaded.
auto can_collide_with_scene() -> bool;

/// Whether the emitter's particles collide with scene surfaces, as it was last updated (collides_with_scene_surfaces).
auto collides_with_scene(emitter_handle handle) -> bool;

/**
 * @brief The particles of the @p emitters that collide with scene surfaces touch the scene of @p view: the GPU tests
 * each where it draws now and rewrites the record of one that is in a surface, or will be within the frame, as the
 * emitter's collision mode says. The contact happens at the frame's tick. The CPU is told nothing: no event, and what
 * it evaluates itself (particle lights, get_particle_states) follows the particle's path as it was born.
 * @note Main thread, after sync_gpu_simulation and before the view's render_view calls.
 * @return The particles tested (0 where particles cannot collide with scene surfaces).
 */
auto collide_with_scene(const scene_collision_view& view, const emitter_handle* emitters, uint32_t count) -> uint32_t;

/// A particle that lights the scene (emitter_render_desc::light_count).
struct particle_light
{
    math::vec3 position{0.0f};
    /// Linear color, including the particle's brightness.
    math::vec3 color{1.0f};
    /// Candela.
    float intensity = 0.0f;
    float range = 0.0f;
};

/**
 * @brief Appends the emitter's particle lights: its brightest particles (color luminance x alpha), up to its
 * light count, evaluated at its current time.
 */
void collect_particle_lights(emitter_handle handle, std::vector<particle_light>& out_lights);

/**
 * @brief A mesh emitter's particles (render_mode::mesh) as world matrices, at its current time and one frame
 * earlier, a frame back along each particle's motion (motion vectors). Each places the mesh at its particle,
 * scaled by the particle's size (x and y by the size axis curves) times the emitter's 3D scale, and turned by
 * the particle's angle about an axis of its own; aligned to direction, the mesh's +Y follows the motion and the
 * angle spins it about it.
 * @param out_current Replaced by this frame's matrices, one per live particle.
 * @param out_previous Replaced by last frame's, in the same order (this frame's for particles born since).
 * @param out_opacity Replaced by each particle's opacity (its color's alpha), in the same order.
 */
void collect_particle_mesh_transforms(emitter_handle handle,
                                      std::vector<math::mat4>& out_current,
                                      std::vector<math::mat4>& out_previous,
                                      std::vector<float>& out_opacity);

/**
 * @brief The particle's size, as drawn, for each state get_particle_states returned (same order).
 */
void get_particle_sizes(emitter_handle handle, std::vector<float>& out_sizes);

/**
 * @brief One draw: emitters whose materials bind the same shader, values and textures, in draw order. Their particles
 * sort together.
 */
struct render_batch
{
    const emitter_handle* emitters = nullptr;
    uint32_t count = 0;
    /// The material the batch's particles draw with: a Surface material with Particle Use. Its Blend Mode is
    /// @ref blend_mode, and @ref lighting is unlit for one whose Shading Model is Unlit.
    const graph_material* material = nullptr;
    blend_mode blend_mode = blend_mode::normal;
    particle_primitive primitive = particle_primitive::quad;
    /// Lit batches draw with the lit variants and bind_lighting before each draw.
    particle_lighting lighting = particle_lighting::unlit;
    /// Where the batch sorts among the view's translucent draws: its first emitter's sort priority and squared
    /// distance from the camera (the farthest of the batch).
    int32_t sort_priority = 0;
    float distance = 0.0f;
};

/// The reactive target's blend (the second render target) under BGFX_STATE_BLEND_INDEPENDENT, as bgfx::setState's
/// second argument (per extra target: source factor, destination factor << 4, equation << 8): ONE, ONE, MAX keeps
/// the largest coverage over the pixel.
constexpr uint32_t reactive_blend_rgba =
    uint32_t(BGFX_STATE_BLEND_ONE >> BGFX_STATE_BLEND_SHIFT) |
    (uint32_t(BGFX_STATE_BLEND_ONE >> BGFX_STATE_BLEND_SHIFT) << 4) |
    (uint32_t(BGFX_STATE_BLEND_EQUATION_MAX >> BGFX_STATE_BLEND_EQUATION_SHIFT) << 8);

struct render_view_params
{
    bgfx::ViewId view = 0;
    /// The overdraw debug view's program (overdraw_weight above 0): every other view draws each batch's material.
    bgfx::ProgramHandle overdraw_program = BGFX_INVALID_HANDLE;
    /// Binds the scene's light before each draw of a lit batch (render_batch::lighting); without it no batch is lit.
    /// True when it bound the GI's light around the batch's particles too (material_draw::reads_gi).
    std::function<bool(const render_batch&)> bind_lighting;
    /// The program of a batch's material (render_batch::material), called before its draw with what the material
    /// reads bound: the colour passes' program (lit when material_draw::is_lit), or the velocity pass's
    /// (material_draw::is_velocity). An invalid handle while the material cannot draw yet: the batch is skipped.
    std::function<bgfx::ProgramHandle(const render_batch&, const material_draw&)> begin_material;
    /// Sets the view's fog (fog.sh) before each draw: the color passes fog the particles with it. Called with whether
    /// the draw is lit (its programs hold the sun's cascades where the unlit ones read the froxel column).
    std::function<void(bool)> bind_fog;
    const float* view_matrix = nullptr;
    math::vec3 eye{0.0f};
    /// The camera's velocity (m/s): stretched particles may stretch against it.
    math::vec3 camera_velocity{0.0f};
    math::vec3 forward{0.0f, 0.0f, 1.0f};
    /// The view frustum's planes (outward normals, frustum::planes order).
    std::array<math::vec4, 6> frustum_planes{};
    /// How many pixels of the view a metre covers: one metre from the camera through a perspective, anywhere through
    /// an orthographic view. 0: unknown, and trails draw their middle count of quads.
    float pixels_per_metre = 0.0f;
    /// The view is orthographic: a metre covers as many pixels at every depth.
    bool is_orthographic = false;
    /// The view's opaque depth soft particles fade against; invalid turns the fade off.
    bgfx::TextureHandle scene_depth = BGFX_INVALID_HANDLE;
    /// The u_pre_exposure value the particle programs read.
    math::vec4 pre_exposure{1.0f, 1.0f, 1.0f, 1.0f};
    bgfx::UniformHandle pre_exposure_uniform = BGFX_INVALID_HANDLE;
    /// The view's environment SH, whose texel 9 is the GPU's share of the pre-exposure (pre_exposure_state::gpu_texture),
    /// bound for every draw at @ref environment_stage, where the particle programs read the share.
    bgfx::TextureHandle environment_texture = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environment_sampler = BGFX_INVALID_HANDLE;
    uint8_t environment_stage = 0;
    /// The view's second render target takes the particles' TAA reactive coverage (MAX blend; needs
    /// BGFX_CAPS_BLEND_INDEPENDENT). Without it the target, if any, takes the color target's blend.
    bool has_reactive_target = false;
    /// The color target starts at (0, 0, 0, 1) and composites over the scene later as color + scene x alpha
    /// (the half-resolution particles): the blends keep premultiplied light in rgb and the transmittance in
    /// alpha. Multiply-blend batches cannot be drawn into it and are skipped.
    bool is_transmittance_target = false;
    /// The velocity pass: last frame's unjittered view-projection, set per draw as @p prev_view_proj_uniform.
    /// The draws then replace the scene's motion where texels pass their emitter's opacity threshold: no
    /// blend, no reactive target, each material's velocity variant.
    const float* prev_view_proj = nullptr;
    bgfx::UniformHandle prev_view_proj_uniform = BGFX_INVALID_HANDLE;
    /// Above 0: the overdraw debug view. Every fragment the draws rasterize adds this weight (0..1) to the
    /// color target, whatever the batch's blend; no lighting, @ref overdraw_program for every batch.
    float overdraw_weight = 0.0f;
    /// Called before each batch's draw, drawn or not, in the order of the batches: the translucency pass draws there
    /// the meshes that sort before the batch. Empty: nothing is called.
    std::function<void(const render_batch&)> before_batch;
};

struct render_view_stats
{
    uint32_t drawn_emitters = 0;
    /// Live particles of the drawn emitters (before per-particle culling).
    uint32_t drawn_particles = 0;
    uint32_t draws = 0;
    uint32_t dispatches = 0;
    uint32_t sorted_keys = 0;
};

/**
 * @brief Culls, sorts and draws @p batches into one view, in order.
 */
auto render_view(const render_view_params& params, const render_batch* batches, uint32_t batch_count)
    -> render_view_stats;

} // namespace ps_soa
} // namespace unravel
