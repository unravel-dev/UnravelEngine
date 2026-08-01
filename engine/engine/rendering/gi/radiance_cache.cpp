// Like the other GI translation units, this depends only on math and the standard library so
// the structure can be validated without a GPU. The shader mirrors it verbatim.
#include "radiance_cache.h"

#include <engine/profiler/profiler.h>

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace
{

/**
 * @brief PCG-style integer hash. Well distributed and cheap enough for a shader.
 *
 * Quality matters more here than it looks: the hash decides which entries collide, and a
 * clustered hash makes neighbouring cells fight over the same probe chain and evict each other
 * every frame, which shows up as flickering rather than as a cache miss.
 */
auto hash_uint(uint32_t value) -> uint32_t
{
    uint32_t state = value * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

auto hash_combine(uint32_t seed, uint32_t value) -> uint32_t
{
    return hash_uint(seed ^ (value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u)));
}

/// Round up to a power of two so the capacity modulo becomes a mask.
auto next_power_of_two(uint32_t value) -> uint32_t
{
    uint32_t result = 1u;
    while(result < value)
    {
        result <<= 1u;
    }
    return result;
}

} // namespace

namespace
{
/// Cube face of a given axis, sign included.
auto face_from_axis(const math::vec3& normal, uint32_t axis) -> uint32_t
{
    float axis_value = normal.z;
    if(axis == 0)
    {
        axis_value = normal.x;
    }
    else if(axis == 1)
    {
        axis_value = normal.y;
    }
    return axis * 2u + (axis_value < 0.0f ? 1u : 0u);
}

/// Index of the largest component. Ties resolve deterministically, x before y before z.
auto dominant_axis(const math::vec3& magnitude) -> uint32_t
{
    if(magnitude.y > magnitude.x && magnitude.y >= magnitude.z)
    {
        return 1;
    }
    if(magnitude.z > magnitude.x && magnitude.z >= magnitude.y)
    {
        return 2;
    }
    return 0;
}
} // namespace

auto quantize_normal(const math::vec3& normal) -> uint32_t
{
    return face_from_axis(normal, dominant_axis(math::abs(normal)));
}

auto quantize_normal_second(const math::vec3& normal) -> uint32_t
{
    math::vec3 magnitude = math::abs(normal);
    const uint32_t axis = dominant_axis(magnitude);
    // Mask the winner out so the same comparison yields the runner up.
    magnitude[int(axis)] = -1.0f;
    return face_from_axis(normal, dominant_axis(magnitude));
}

auto face_direction(uint32_t face) -> math::vec3
{
    math::vec3 direction(0.0f);
    direction[int(face >> 1u)] = (face & 1u) != 0u ? -1.0f : 1.0f;
    return direction;
}

void radiance_cache::init(const settings& settings)
{
    settings_ = settings;
    settings_.capacity = next_power_of_two(math::max(settings_.capacity, 64u));
    settings_.base_cell_size = math::max(settings_.base_cell_size, 1e-3f);
    settings_.base_distance = math::max(settings_.base_distance, 1e-3f);
    capacity_mask_ = settings_.capacity - 1u;
    entries_.assign(settings_.capacity, entry{});
}

void radiance_cache::clear()
{
    std::fill(entries_.begin(), entries_.end(), entry{});
}

auto radiance_cache::get_cell_size(uint32_t level) const -> float
{
    return settings_.base_cell_size * float(1u << level);
}

auto radiance_cache::compute_level(const math::vec3& position, const math::vec3& camera_position) const
    -> uint32_t
{
    const float distance = math::length(position - camera_position);
    if(distance <= settings_.base_distance)
    {
        return 0;
    }
    const float ratio = distance / settings_.base_distance;
    const int level = int(std::floor(std::log2(ratio))) + 1;
    return uint32_t(math::clamp(level, 0, int(settings_.max_level)));
}

auto radiance_cache::compute_key(const math::vec3& position, const math::vec3& normal, uint32_t level) const
    -> uint32_t
{
    return compute_key_for_face(position, quantize_normal(normal), level);
}

auto radiance_cache::compute_key_for_face(const math::vec3& position, uint32_t face, uint32_t level) const
    -> uint32_t
{
    const float cell_size = get_cell_size(level);
    // Lift half a cell along the face direction before snapping. A surface lying exactly on a
    // cell plane -- a ground plane at y = 0 with 0.25 m cells, say -- otherwise has the grid
    // boundary running through it, so the sign of a rounding error decides the cell and a writer
    // and reader that disagree by an epsilon address different entries. Half a cell is far
    // larger than any such epsilon, and the offset is derived from the QUANTISED face rather
    // than the raw normal so both sides shift identically.
    const math::vec3 snapped = position + face_direction(face) * (cell_size * 0.5f);
    // floor, not truncation: truncation folds the cells either side of zero into one, which
    // puts two surfaces a whole cell apart into the same entry across every axis plane.
    const math::vec3 cell = math::floor(snapped / cell_size);
    uint32_t key = hash_uint(uint32_t(int32_t(cell.x)));
    key = hash_combine(key, uint32_t(int32_t(cell.y)));
    key = hash_combine(key, uint32_t(int32_t(cell.z)));
    key = hash_combine(key, level);
    key = hash_combine(key, face);
    // Never produce the empty sentinel, or an occupied slot would read as free.
    return key == empty_key ? 1u : key;
}

auto radiance_cache::find_surface(const math::vec3& position, const math::vec3& normal, uint32_t level) const
    -> uint32_t
{
    const uint32_t slot = find(compute_key_for_face(position, quantize_normal(normal), level));
    if(slot != invalid_slot)
    {
        return slot;
    }
    return find(compute_key_for_face(position, quantize_normal_second(normal), level));
}

auto radiance_cache::find(uint32_t key) const -> uint32_t
{
    const uint32_t base = key & capacity_mask_;
    for(uint32_t i = 0; i < probe_length; ++i)
    {
        const uint32_t slot = (base + i) & capacity_mask_;
        if(entries_[slot].key == key)
        {
            return slot;
        }
    }
    return invalid_slot;
}

auto radiance_cache::insert(uint32_t key, uint32_t frame) -> uint32_t
{
    const uint32_t base = key & capacity_mask_;
    uint32_t oldest_slot = invalid_slot;
    uint32_t oldest_frame = 0xFFFFFFFFu;
    for(uint32_t i = 0; i < probe_length; ++i)
    {
        const uint32_t slot = (base + i) & capacity_mask_;
        auto& e = entries_[slot];
        if(e.key == key)
        {
            e.frame_touched = frame;
            return slot;
        }
        if(e.key == empty_key)
        {
            e = entry{};
            e.key = key;
            e.frame_touched = frame;
            return slot;
        }
        if(e.frame_touched < oldest_frame)
        {
            oldest_frame = e.frame_touched;
            oldest_slot = slot;
        }
    }
    // The chain is full. Evict the least recently touched entry rather than failing: a cell
    // being asked for now matters more than one nothing has looked at in a while. This is what
    // replaces a free list -- there is nothing to compact and no fragmentation to accumulate.
    //
    // Never evict something touched THIS frame, though: that would let two cells competing for
    // the same chain replace each other every frame, and neither would ever accumulate.
    if(oldest_slot == invalid_slot || oldest_frame == frame)
    {
        return invalid_slot;
    }
    auto& victim = entries_[oldest_slot];
    victim = entry{};
    victim.key = key;
    victim.frame_touched = frame;
    return oldest_slot;
}

void radiance_cache::accumulate(uint32_t slot,
                                const math::vec3& radiance,
                                uint32_t frame,
                                float min_alpha,
                                uint32_t max_samples)
{
    if(slot >= entries_.size())
    {
        return;
    }
    auto& e = entries_[slot];
    e.frame_touched = frame;
    e.sample_count = math::min(e.sample_count + 1u, math::max(max_samples, 1u));
    // 1/n while young so a fresh entry converges immediately, floored so a mature one keeps
    // responding to change. Without the floor the mean freezes and a light that switches off
    // stays visible forever.
    const float alpha = math::max(1.0f / float(e.sample_count), min_alpha);
    e.radiance += (radiance - e.radiance) * alpha;
}

auto radiance_cache::count_occupied() const -> uint32_t
{
    uint32_t count = 0;
    for(const auto& e : entries_)
    {
        count += e.key != empty_key ? 1u : 0u;
    }
    return count;
}

} // namespace unravel
