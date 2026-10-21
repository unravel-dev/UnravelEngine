#pragma once

#include "emitter_desc.h"
#include "emitter_runtime.h"
#include "particle_cutout.h"
#include "particle_eval.h"
#include "particle_gpu_schema.h"
#include "particle_types.h"
#include <bgfx/bgfx.h>
#include <engine/rendering/pipeline/pre_exposure.h>
#include <context/context.hpp>
#include <math/bbox.h>
#include <math/math.h>

#include <functional>
#include <vector>

namespace unravel
{
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

/// Something that happened to a particle, as a sub-emitter reads it.
struct particle_event
{
    particle_event_type type = particle_event_type::death;
    /// World position and velocity then.
    math::vec3 position{0.0f};
    math::vec3 velocity{0.0f};
    /// Linear color without the emitter's intensity, and the size.
    math::vec4 color{1.0f};
    float size = 0.0f;
};

/**
 * @brief Moves the events the emitter recorded since the last call (emitter_desc::event_mask) to @p out_events.
 */
void take_particle_events(emitter_handle handle, std::vector<particle_event>& out_events);

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
 * @brief One draw: emitters sharing a texture, texture mode and blend mode, in draw order. Their
 * particles sort together.
 */
struct render_batch
{
    const emitter_handle* emitters = nullptr;
    uint32_t count = 0;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    texture_mode texture_mode = texture_mode::multi_channel;
    blend_mode blend_mode = blend_mode::normal;
    particle_primitive primitive = particle_primitive::quad;
    /// Lit batches draw with the lit programs and bind_lighting before each draw.
    particle_lighting lighting = particle_lighting::unlit;
    /// Surface-lit batches: the normal map their pixels light with (invalid: a sphere's normal), and whether its
    /// format stores x and y only (z is rebuilt).
    bgfx::TextureHandle normal_texture = BGFX_INVALID_HANDLE;
    bool is_normal_texture_two_channel = false;
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
    /// Programs for the multi-channel and mask texture modes.
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle mask_program = BGFX_INVALID_HANDLE;
    /// The same for lit batches (render_batch::lighting), and what binds their light before each of their draws.
    bgfx::ProgramHandle lit_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle lit_mask_program = BGFX_INVALID_HANDLE;
    std::function<void(const render_batch&)> bind_lighting;
    const float* view_matrix = nullptr;
    math::vec3 eye{0.0f};
    /// The camera's velocity (m/s): stretched particles may stretch against it.
    math::vec3 camera_velocity{0.0f};
    math::vec3 forward{0.0f, 0.0f, 1.0f};
    /// The view frustum's planes (outward normals, frustum::planes order).
    std::array<math::vec4, 6> frustum_planes{};
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
    /// blend, no reactive target, one program for both texture modes.
    const float* prev_view_proj = nullptr;
    bgfx::UniformHandle prev_view_proj_uniform = BGFX_INVALID_HANDLE;
    /// Above 0: the overdraw debug view. Every fragment the draws rasterize adds this weight (0..1) to the
    /// color target, whatever the batch's blend; no lighting, one program for both texture modes.
    float overdraw_weight = 0.0f;
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
