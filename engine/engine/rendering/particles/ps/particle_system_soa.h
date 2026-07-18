#pragma once

#include "emitter_desc.h"
#include "emitter_runtime.h"
#include "particle_gpu_schema.h"
#include "particle_types.h"

#include <bgfx/bgfx.h>
#include <context/context.hpp>
#include <math/bbox.h>
#include <math/math.h>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief Initialize the soa particle system.
 * @param max_emitters Maximum concurrent emitters.
 */
void init(uint16_t max_emitters = 64);

/**
 * @brief Load compute pack program and enable GPU backend when available.
 * @param ctx Runtime context with asset_manager.
 */
void init_gpu(rtti::context& ctx);

/**
 * @brief Shutdown and free all emitters / GPU resources.
 */
void shutdown();

/**
 * @brief Set default sim backend for new / unset emitters (cpu remains fallback).
 */
void set_default_sim_backend(particle_sim_backend backend);

/**
 * @brief Current default sim backend.
 */
auto get_default_sim_backend() -> particle_sim_backend;

/**
 * @brief Override backend for one emitter.
 */
void set_emitter_sim_backend(emitter_handle handle, particle_sim_backend backend);

/**
 * @brief Effective backend for an emitter (after availability checks).
 */
auto get_emitter_sim_backend(emitter_handle handle) -> particle_sim_backend;

/**
 * @brief True when compute pack program loaded successfully.
 */
auto is_gpu_sim_available() -> bool;

/**
 * @brief Create an emitter with shape/direction and capacity.
 */
auto create_emitter(emitter_shape shape, emitter_direction direction, uint32_t max_particles) -> emitter_handle;

/**
 * @brief Destroy an emitter and free its particle storage.
 */
void destroy_emitter(emitter_handle handle);

/**
 * @brief Clear particles and reset sim bookkeeping.
 */
void reset_emitter(emitter_handle handle);

/**
 * @brief Advance simulation for one emitter.
 * @param handle Emitter handle.
 * @param dt Frame delta seconds.
 * @param desc Authoring description (gradients, rates, render flags). Playback fields in desc are ignored.
 * @param transform Current/previous world transform; previous is updated after the call.
 * @param playback Mutable playing/paused; may be cleared when a one-shot emitter finishes.
 */
void update_emitter(emitter_handle handle,
                    float dt,
                    const emitter_desc& desc,
                    emitter_transform_state& transform,
                    emitter_playback_desc& playback);

/**
 * @brief Refresh transform-driven world bounds without advancing sim (renderer-based freeze).
 */
void update_emitter_bounds_only(emitter_handle handle,
                                const emitter_desc& desc,
                                emitter_transform_state& transform);

/**
 * @brief Flush staged GPU spawns and advance resident GPU sim for awake emitters.
 * @note Call once per frame on the main/render thread after parallel update_emitter.
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
 * @brief Submit a homogeneous material batch (same texture / texture_mode / blend).
 * @param sort_by_depth When true, back-to-front sort (Normal blend). When false, emission order.
 * @return Number of instances submitted.
 */
auto render_emitter_batch(const emitter_handle* handles,
                          uint32_t count,
                          uint8_t view,
                          bgfx::ProgramHandle program,
                          const float* mtx_view,
                          const math::vec3& eye,
                          bgfx::TextureHandle texture,
                          uint64_t blend_state,
                          bool sort_by_depth = true) -> uint32_t;

} // namespace ps_soa
} // namespace unravel
