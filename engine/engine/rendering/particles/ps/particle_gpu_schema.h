#pragma once

#include "particle_types.h"

#include <cstdint>

namespace unravel
{
namespace ps_soa
{

/// Where an emitter's particles are culled and sorted each view.
enum class particle_sort_backend : uint8_t
{
    cpu = 0,
    gpu = 1,
};

/// Threads per group of the scatter, cull / key and draw-argument kernels.
constexpr uint32_t gpu_group_threads = 64;
/// Elements one group of cs_particle_sort.sc sorts in groupshared memory (PARTICLE_SORT_BLOCK).
constexpr uint32_t gpu_sort_block = 1024;
/// Threads per group of cs_particle_sort.sc.
constexpr uint32_t gpu_sort_threads = 512;
/// uints per task of cs_particle_keygen.sc (PARTICLE_TASK_UINTS).
constexpr uint32_t gpu_task_uints = 8;
/// Indices of the particle quad and of the octagon after it in the particle index buffer.
constexpr uint32_t quad_index_count = 6;
constexpr uint32_t octagon_index_count = 18;
constexpr uint32_t octagon_first_index = quad_index_count;
/// The cutout fan after them: 8 vertices the vertex shader moves onto each frame's cutout polygon.
constexpr uint32_t cutout_index_count = 18;
constexpr uint32_t cutout_first_index = octagon_first_index + octagon_index_count;
/// The trail strip after them: trail_segments quads along the trail.
constexpr uint32_t trail_segments = 16;
constexpr uint32_t trail_index_count = trail_segments * 6;
constexpr uint32_t trail_first_index = cutout_first_index + cutout_index_count;
/// Stages of a particle draw. The vertex shaders' buffers stay below 8, the fewest buffer bindings a GL driver
/// may offer; the textures take the stages above.
/// The cutout tables the vertex shaders read (vec4: two polygon points each).
constexpr uint8_t gpu_cutout_stage = 6;
/// vec4 per sheet frame of a cutout table.
constexpr uint32_t cutout_frame_vec4s = 4;
/// The per-batch live counts the vertex shaders read (ribbons read their neighbours within it).
constexpr uint8_t gpu_batch_counts_stage = 5;
/// The turbulence noise volume (vertex shaders and the cull pass).
constexpr uint8_t gpu_noise_stage = 7;
/// The particle texture and the view's opaque depth (soft particles), read by the fragment shaders.
constexpr uint8_t gpu_color_texture_stage = 13;
constexpr uint8_t gpu_scene_depth_stage = 15;
/// uints per batch of the per-view batch table: index count, first index.
constexpr uint32_t gpu_batch_uints = 2;

/// cs_particle_keygen.sc PARTICLE_SORT_KIND_*.
enum class gpu_sort_kind : uint32_t
{
    append = 0,
    distance = 1,
    depth = 2,
    oldest_first = 3,
    youngest_first = 4,
    slot = 5,
    /// Oldest first, nothing culled: a ribbon connects every live particle in birth order.
    ribbon = 6,
};

} // namespace ps_soa
} // namespace unravel
