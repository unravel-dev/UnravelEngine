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
/// Elements one group of cs_particle_sort.eshader sorts in groupshared memory (PARTICLE_SORT_BLOCK).
constexpr uint32_t gpu_sort_block = 1024;
/// uints per task of cs_particle_keygen.eshader (PARTICLE_TASK_UINTS).
constexpr uint32_t gpu_task_uints = 8;
/// Indices of the particle quad and of the octagon after it in the particle index buffer.
constexpr uint32_t quad_index_count = 6;
constexpr uint32_t octagon_index_count = 18;
constexpr uint32_t octagon_first_index = quad_index_count;
/// The cutout fan after them: 8 vertices the vertex shader moves onto each frame's cutout polygon.
constexpr uint32_t cutout_index_count = 18;
constexpr uint32_t cutout_first_index = octagon_first_index + octagon_index_count;
/// The strip after them, which trails and ribbons draw: one set of vertices, a pair every 1 / strip_max_segments of
/// its length, and index ranges of 1, 2, 4 ... strip_max_segments quads over it, one after the other. A draw picks
/// the range that bends as finely as it needs.
constexpr uint32_t strip_max_segments = 32;
constexpr uint32_t strip_first_index = cutout_first_index + cutout_index_count;

/// Whether a strip can be drawn in @p segments quads: a power of two up to strip_max_segments.
constexpr auto is_strip_segment_count(uint32_t segments) -> bool
{
    return segments > 0 && segments <= strip_max_segments && (segments & (segments - 1)) == 0;
}

/// How many indices a strip of @p segments quads (is_strip_segment_count) takes.
constexpr auto get_strip_index_count(uint32_t segments) -> uint32_t
{
    return segments * 6;
}

/// Where the indices of a strip of @p segments quads start in the particle index buffer: after the ranges of fewer
/// quads, which hold segments - 1 quads together.
constexpr auto get_strip_first_index(uint32_t segments) -> uint32_t
{
    return strip_first_index + (segments - 1) * 6;
}

/// The quads a trail is drawn in: one when its path is a straight line, else by how many pixels the longest trail of
/// its batch can cover in the view.
constexpr uint32_t trail_segments_straight = 1;
constexpr uint32_t trail_segments_small = 8;
constexpr uint32_t trail_segments_medium = 16;
constexpr uint32_t trail_segments_large = 32;
/// A trail shorter than trail_small_pixels draws trail_segments_small quads (12 pixels a quad at that length), and
/// one longer than trail_large_pixels trail_segments_large (the middle count's quads cover 24 pixels there).
constexpr float trail_small_pixels = 96.0f;
constexpr float trail_large_pixels = 384.0f;
/// Stages of a particle draw. The vertex shaders' buffers stay below 8, the fewest buffer bindings a GL driver
/// may offer; the textures take the stages above.
/// The particle data the vertex shaders read: the record pool, the emitter tables, the curve-set atlas and the view's
/// sorted list (shader buffers: storage buffers, or textures on devices without storage buffers in vertex shaders).
/// The atlas also holds the cutout tables, in blocks of their own, and the sorted list each GPU batch's live count,
/// after its entries.
constexpr uint8_t gpu_records_stage = 0;
constexpr uint8_t gpu_static_stage = 1;
constexpr uint8_t gpu_dynamic_stage = 2;
constexpr uint8_t gpu_luts_stage = 3;
constexpr uint8_t gpu_sorted_stage = 4;
/// vec4 per sheet frame of a cutout table (two polygon points each).
constexpr uint32_t cutout_frame_vec4s = 4;
/// The turbulence noise volume (vertex shaders and the cull pass).
constexpr uint8_t gpu_noise_stage = 7;
/// The view's opaque depth (soft particles), read by the fragment shaders. A material's own textures take the stages
/// between (get_material_graph_particle_texture_stage).
constexpr uint8_t gpu_scene_depth_stage = 15;
/// uints per batch of the per-view batch table: index count, first index.
constexpr uint32_t gpu_batch_uints = 2;
/// uints per task of cs_particle_collide.eshader (PARTICLE_COLLIDE_TASK_UINTS).
constexpr uint32_t gpu_collide_task_uints = 10;
/// What each uint of a collide task holds (PARTICLE_COLLIDE_TASK_*); the ones from the restitution on are floats, the
/// bounce scatter in radians.
enum gpu_collide_task_slot : uint32_t
{
    gpu_collide_task_pool_base = 0,
    gpu_collide_task_high_water = 1,
    gpu_collide_task_thread_offset = 2,
    gpu_collide_task_answer = 3,
    gpu_collide_task_restitution = 4,
    gpu_collide_task_friction = 5,
    gpu_collide_task_radius_scale = 6,
    gpu_collide_task_rest_speed = 7,
    gpu_collide_task_lifetime_loss = 8,
    gpu_collide_task_bounce_scatter = 9,
};
static_assert(gpu_collide_task_bounce_scatter + 1 == gpu_collide_task_uints, "a collide task is its slots and no more");
/// A collide task's answer: the collision mode in its low byte, the max bounces from gpu_collide_bounces_shift, and
/// gpu_collide_flat_bit where a particle at rest lies flat on its surface (PARTICLE_COLLIDE_BOUNCES_SHIFT,
/// PARTICLE_COLLIDE_FLAT_BIT).
constexpr uint32_t gpu_collide_bounces_shift = 8;
constexpr uint32_t gpu_collide_flat_bit = 1u << 16u;
/// Stages of a collide dispatch beside the particle data: its task table, and the scene it reads, the view's depth
/// and G-buffer normals or the distance field (gi/sdf_clipmap.sh fixes its stage). All below 8, the compute bindings
/// the particle kernels count on.
constexpr uint8_t gpu_collide_tasks_stage = 5;
constexpr uint8_t gpu_collide_depth_stage = 4;
constexpr uint8_t gpu_collide_normals_stage = 6;
constexpr uint8_t gpu_collide_distance_field_stage = 4;

/// cs_particle_keygen.eshader PARTICLE_SORT_KIND_*.
enum class gpu_sort_kind : uint32_t
{
    append = 0,
    distance = 1,
    depth = 2,
    oldest_first = 3,
    youngest_first = 4,
    slot = 5,
    /// Ribbon after ribbon, each oldest first, nothing culled: a ribbon connects every live particle of it in birth
    /// order.
    ribbon = 6,
};

/// How many bits hold a number below @p count.
constexpr auto get_bits_for(uint64_t count) -> uint32_t
{
    uint32_t bits = 0;
    while(bits < 63u && (uint64_t(1) << bits) < count)
    {
        ++bits;
    }
    return bits;
}

/**
 * @brief How a ribbon task's sort key shares the @p key_bits bits below its batch's (PARTICLE_SORT_KIND_RIBBON in
 * cs_particle_keygen.eshader): in the low byte, how many of them hold the ribbon; above it, how far an age is
 * shifted down so the longest age of the task's emitter fits the bits the ribbon leaves. An emitter of one ribbon
 * keeps all the bits for the age.
 *
 * The key orders a ribbon's particles after the ones of the ribbon before it, each ribbon oldest first. Ages that
 * differ only in what is shifted out can swap places in their ribbon: a ribbon of very long lives beside many
 * ribbons and many sorted batches.
 */
constexpr auto pack_ribbon_key_bits(uint32_t ribbon_count, uint32_t longest_age_ticks, uint32_t key_bits) -> uint32_t
{
    const uint32_t ribbon_bits = ribbon_count > 1 ? get_bits_for(ribbon_count) : 0u;
    const uint32_t age_bits = key_bits > ribbon_bits ? key_bits - ribbon_bits : 0u;
    // One past the longest age and one more: the largest key of the bits marks the dead.
    const uint32_t needed_bits = get_bits_for(uint64_t(longest_age_ticks) + 2u);
    const uint32_t age_shift = needed_bits > age_bits ? needed_bits - age_bits : 0u;
    return ribbon_bits | (age_shift << 8u);
}

} // namespace ps_soa
} // namespace unravel
