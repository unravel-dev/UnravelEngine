#include "lumen_scene.h"

#include <engine/profiler/profiler.h>
#include <engine/rendering/gi/lumen_constants.h>

#include <concurrency/parallel.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace unravel
{
namespace
{

/// Lumen::MinCardResolution.
constexpr uint32_t k_min_card_resolution = 8;
/// An emissive light source's minimum visible card resolution (LumenSceneRendering.cpp:737) and the factor on its
/// minimum face area (LumenMeshCards.cpp:128-132).
constexpr uint32_t k_emissive_min_card_resolution = 1;
constexpr float k_emissive_min_card_area_scale = 0.2f;
/// Lumen::NumDistanceBuckets.
constexpr uint32_t k_distance_bins = 16;
/// The viewer distance floor of the resolution rule (100 cm).
constexpr float k_min_viewer_distance = 1.0f;
/// UE's floor of a primitive group's distance in the residency gate (1 cm) and the texels added to its projection.
constexpr float k_min_group_distance = 0.01f;
constexpr float k_group_resolution_bias = 0.01f;
/// GetMeshCardDistanceBin: bins start 10 m out, log2 of centimetres.
constexpr float k_distance_bin_offset = 10.0f;
/// A reallocation ranks as if this much farther away than a new card when its level moves by one.
constexpr float k_realloc_distance_penalty = 25.0f;
/// Slots per sub-allocated page are tracked in 64-bit words (at most 16 x 16 slots of 8 x 8 texels).
constexpr uint32_t k_slot_words = 4;
constexpr uint32_t k_slot_word_bits = 64;
/// The card lighting's tile edge (the lighting kernels' 8 x 8 groups).
constexpr uint32_t k_lighting_tile_size = 8;
/// Priority buckets of the lighting scheduler.
constexpr uint32_t k_lighting_buckets = 16;
/// The bucket of a page the direct lighting skips (lit, not dirty): past every real bucket, never admitted.
constexpr uint32_t k_skip_bucket = k_lighting_buckets;
/// The frustum planes the lighting priority tests: left, right, top, bottom and near (not far).
constexpr uint32_t k_priority_frustum_planes = 5;
/// The lighting update speed's range (R/LumenSceneLighting.cpp:565).
constexpr float k_min_lighting_update_speed = 0.5f;
constexpr float k_max_lighting_update_speed = 16.0f;
/// The surface cache resolution's range (UE FPostProcessSettings::LumenSurfaceCacheResolution).
constexpr float k_min_surface_cache_resolution = 0.5f;
constexpr float k_max_surface_cache_resolution = 1.0f;
/// The scene detail's range for the cards (R/LumenSceneRendering.cpp:2167).
constexpr float k_min_scene_detail = 0.125f;
constexpr float k_max_scene_detail = 8.0f;
/// The largest minimum card resolution (LumenScene::GetCardMinResolution).
constexpr uint32_t k_max_card_min_resolution = 1024;
/// Placements per resolution task (UE r.LumenScene.MeshCardsPerTask is 128 mesh cards).
constexpr uint32_t k_placements_per_task = 128;
/// Resident pages from which a per-page lighting pass runs on the pool: below them the dispatch costs more than the
/// loop (~0.05 ms). The direct-lighting invalidation tests every change per page, the page speeds walk the viewers and
/// their frusta, the per-frame buckets take an age and a log2.
constexpr uint32_t k_parallel_invalidation_pages = 1024;
constexpr uint32_t k_parallel_speed_pages = 4096;
constexpr uint32_t k_parallel_bucket_pages = 8192;
/// A hi-res request ranks this much farther than its card, plus as much again scaled by the share of the feedback
/// samples it did not get (LumenSurfaceCacheFeedback.cpp:362-364).
constexpr float k_hi_res_distance_bias = 25.0f;
/// Frames a hi-res page stays unasked for before a locked allocation, and before another hi-res page, may take its
/// place (LumenSceneRendering.cpp:947, 1063: two, and the 16 x 16 feedback tile's pixels).
constexpr uint64_t k_hi_res_idle_frames_for_locked = 2;
constexpr uint64_t k_hi_res_idle_frames_for_hi_res = 256;

/// The range of box (@p center, @p half) along @p axis: x = min, y = max.
auto project_box(const math::vec3& center, const math::vec3& half, const math::vec3& axis) -> math::vec2
{
    const float middle = math::dot(center, axis);
    const float reach = math::dot(half, math::abs(axis));
    return {middle - reach, middle + reach};
}

/// Whether @p occluder can lie between a point of @p page and @p light. A directional light: their ranges across the
/// light's direction overlap on both axes and the occluder reaches toward the light past the page's lowest point. A
/// local light: the page is in range and the occluder meets the box around the page and the light's position.
auto is_between(const math::bbox& occluder, const math::bbox& page, const lumen_scene::light_reach& light) -> bool
{
    if(light.is_directional)
    {
        const math::vec3 toward = math::normalize(light.direction);
        const math::vec3 helper = std::abs(toward.y) < 0.9f ? math::vec3(0.0f, 1.0f, 0.0f) : math::vec3(1.0f, 0.0f, 0.0f);
        const math::vec3 across_u = math::normalize(math::cross(toward, helper));
        const math::vec3 across_v = math::cross(toward, across_u);
        const math::vec3 occluder_center = occluder.get_center();
        const math::vec3 occluder_half = occluder.get_extents();
        const math::vec3 page_center = page.get_center();
        const math::vec3 page_half = page.get_extents();
        const auto overlaps = [&](const math::vec3& axis)
        {
            const math::vec2 a = project_box(occluder_center, occluder_half, axis);
            const math::vec2 b = project_box(page_center, page_half, axis);
            return a.y >= b.x && a.x <= b.y;
        };
        const math::vec2 occluder_height = project_box(occluder_center, occluder_half, toward);
        const math::vec2 page_height = project_box(page_center, page_half, toward);
        return overlaps(across_u) && overlaps(across_v) && occluder_height.y >= page_height.x;
    }
    const math::vec3 range(light.range);
    const math::bbox reach(light.position - range, light.position + range);
    if(!reach.intersect(page))
    {
        return false;
    }
    math::bbox hull = page;
    hull.add_point(light.position);
    return hull.intersect(occluder);
}

auto floor_log2(uint32_t value) -> uint32_t
{
    uint32_t result = 0;
    while(value > 1u)
    {
        value >>= 1u;
        ++result;
    }
    return result;
}

auto round_up_power_of_two(uint32_t value) -> uint32_t
{
    uint32_t result = 1;
    while(result < value)
    {
        result <<= 1u;
    }
    return result;
}

auto compute_distance_bin(float distance) -> uint32_t
{
    const int centimetres = int((distance - k_distance_bin_offset) * 100.0f);
    return std::min(floor_log2(uint32_t(std::max(1, centimetres))), k_distance_bins - 1u);
}

auto place_card(const lumen_card& card, const math::mat4& local_to_world) -> lumen_scene::placed_card
{
    const math::mat3 linear(local_to_world);
    lumen_scene::placed_card placed;
    placed.origin = math::vec3(local_to_world * math::vec4(card.origin, 1.0f));
    const math::vec3 axis_x = linear * card.axis_x;
    const math::vec3 axis_y = linear * card.axis_y;
    const math::vec3 axis_z = linear * card.axis_z;
    const math::vec3 scale(math::length(axis_x), math::length(axis_y), math::length(axis_z));
    placed.axis_x = axis_x / std::max(scale.x, 1e-12f);
    placed.axis_y = axis_y / std::max(scale.y, 1e-12f);
    placed.axis_z = axis_z / std::max(scale.z, 1e-12f);
    placed.extent = card.extent * scale;
    return placed;
}

auto distance_to_card(const lumen_scene::placed_card& card, const math::vec3& point) -> float
{
    const math::vec3 d = point - card.origin;
    const math::vec3 local(math::dot(d, card.axis_x), math::dot(d, card.axis_y), math::dot(d, card.axis_z));
    return math::length(math::max(math::abs(local) - card.extent, math::vec3(0.0f)));
}

/// Distance from @p point to the axis-aligned box (@p center, @p extent).
auto distance_to_box(const math::vec3& center, const math::vec3& extent, const math::vec3& point) -> float
{
    return math::length(math::max(math::abs(point - center) - extent, math::vec3(0.0f)));
}

/// Distance from the nearest of @p viewers to @p card.
auto nearest_distance_to_card(const lumen_scene::placed_card& card, const std::vector<lumen_scene::viewer>& viewers)
    -> float
{
    float nearest = std::numeric_limits<float>::max();
    for(const auto& viewer : viewers)
    {
        nearest = std::min(nearest, distance_to_card(card, viewer.origin));
    }
    return nearest;
}

/// Distance from the nearest of @p viewers to the axis-aligned box (@p center, @p extent).
auto nearest_distance_to_box(const math::vec3& center,
                             const math::vec3& extent,
                             const std::vector<lumen_scene::viewer>& viewers) -> float
{
    float nearest = std::numeric_limits<float>::max();
    for(const auto& viewer : viewers)
    {
        nearest = std::min(nearest, distance_to_box(center, extent, viewer.origin));
    }
    return nearest;
}

auto is_same_transform(const math::mat4& a, const math::mat4& b) -> bool
{
    return std::memcmp(&a, &b, sizeof(math::mat4)) == 0;
}

/// The farthest a corner of the local box @p bounds moved from @p from to @p to. The displacement is affine in the
/// point, so the corners bound it over the whole box.
auto compute_motion_bound(const math::bbox& bounds, const math::mat4& from, const math::mat4& to) -> float
{
    float motion = 0.0f;
    for(uint32_t corner = 0; corner < 8u; ++corner)
    {
        const math::vec4 point((corner & 1u) != 0u ? bounds.max.x : bounds.min.x,
                               (corner & 2u) != 0u ? bounds.max.y : bounds.min.y,
                               (corner & 4u) != 0u ? bounds.max.z : bounds.min.z,
                               1.0f);
        motion = std::max(motion, math::length(math::vec3(to * point) - math::vec3(from * point)));
    }
    return motion;
}

/// Appends a placed card's record (lumen_scene::get_card_table): its box, then @p mip_entry = (size in pages x, y,
/// res level x, y), then @p reflection_table = the reflections' page table (offset, size in pages x, y, hi-res flag).
void append_card_record(std::vector<math::vec4>& table,
                        const lumen_scene::placed_card& placed,
                        uint32_t page_offset,
                        const math::vec4& mip_entry,
                        const math::vec4& reflection_table)
{
    table.push_back(math::vec4(placed.origin, float(page_offset)));
    table.push_back(math::vec4(placed.axis_x, placed.extent.x));
    table.push_back(math::vec4(placed.axis_y, placed.extent.y));
    table.push_back(math::vec4(placed.axis_z, placed.extent.z));
    table.push_back(mip_entry);
    table.push_back(reflection_table);
}

/// The bin_lookup_ index of a sub-allocated element size (8 to 128 texels per axis).
auto get_bin_lookup_index(const math::uvec2& element_size) -> size_t
{
    constexpr uint32_t levels = lumen_scene::sub_allocation_res_level - lumen_scene::min_res_level + 1u;
    const uint32_t x = floor_log2(element_size.x) - lumen_scene::min_res_level;
    const uint32_t y = floor_log2(element_size.y) - lumen_scene::min_res_level;
    return size_t(x) * levels + y;
}

} // namespace

class lumen_scene::capture_packer
{
public:
    explicit capture_packer(uint32_t size)
        : size_(size)
    {
    }

    auto place(const math::uvec2& extent, math::uvec2& out) -> bool
    {
        for(auto& shelf : shelves_)
        {
            if(shelf.height == extent.y && shelf.cursor + extent.x <= size_)
            {
                out = math::uvec2(shelf.cursor, shelf.y);
                shelf.cursor += extent.x;
                return true;
            }
        }
        if(next_y_ + extent.y > size_ || extent.x > size_)
        {
            return false;
        }
        shelves_.push_back({next_y_, extent.y, extent.x});
        out = math::uvec2(0u, next_y_);
        next_y_ += extent.y;
        return true;
    }

private:
    struct shelf
    {
        uint32_t y = 0;
        uint32_t height = 0;
        uint32_t cursor = 0;
    };
    uint32_t size_ = 0;
    uint32_t next_y_ = 0;
    std::vector<shelf> shelves_;
};

void lumen_scene::init(const settings& s)
{
    settings_ = s;
    reset();
}

auto lumen_scene::apply_settings(const settings& s) -> bool
{
    if(s == settings_)
    {
        return false;
    }
    // Every page lives at an atlas position: other atlas sizes leave nothing to keep.
    if(s.atlas_size != settings_.atlas_size || s.capture_atlas_size != settings_.capture_atlas_size)
    {
        init(s);
        return true;
    }
    settings_ = s;
    return false;
}

auto lumen_scene::get_max_card_distance() const -> float
{
    // The reach of the global distance field whatever the view distance (UE LumenScene::GetCardMaxDistance): a ray
    // that hits a surface there must find its cards, or its bounce is black.
    return settings_.max_card_distance;
}

auto lumen_scene::get_lighting_update_factor(lighting_context context) const -> uint32_t
{
    const float speed =
        std::clamp(view_settings_.lighting_update_speed, k_min_lighting_update_speed, k_max_lighting_update_speed);
    const float factor = float(settings_.lighting_update_factor_scale) *
                         (context == lighting_direct ? float(gi::lumen::LUMEN_SCENE_DIRECT_UPDATE_FACTOR)
                                                     : float(gi::lumen::LUMEN_SCENE_RADIOSITY_UPDATE_FACTOR));
    return uint32_t(std::lround(factor / speed));
}

auto lumen_scene::get_resolution_rule() const -> resolution_rule
{
    const float scale = std::clamp(view_settings_.surface_cache_resolution,
                                   k_min_surface_cache_resolution,
                                   k_max_surface_cache_resolution);
    // UE LumenScene::GetCardMinResolution: the project's minimum over the scene detail, at the cache's resolution.
    const float detail = std::clamp(view_settings_.detail, k_min_scene_detail, k_max_scene_detail);
    resolution_rule rule;
    rule.texel_density_scale = settings_.texel_density_scale * scale;
    rule.max_resolution = uint32_t(std::max(1l, std::lround(float(settings_.card_max_resolution) * scale)));
    rule.min_resolution = uint32_t(std::clamp(std::lround(float(settings_.card_min_resolution) / detail * scale),
                                              1l,
                                              long(k_max_card_min_resolution)));
    return rule;
}

void lumen_scene::reset()
{
    pages_per_side_ = std::max(1u, settings_.atlas_size / physical_page_size);
    free_pages_.clear();
    const uint32_t page_count = pages_per_side_ * pages_per_side_;
    free_pages_.reserve(page_count);
    for(uint32_t page = page_count; page > 0; --page)
    {
        free_pages_.push_back(page - 1u);
    }
    bins_.clear();
    bin_lookup_.fill(-1);
    placements_.clear();
    source_placements_.clear();
    // The active list indexes source_placements_ and the resident pages pair with resident_slots_ by index: a reader
    // between this reset and the next update (the card visualizations) must find them empty too.
    active_.clear();
    first_card_.clear();
    active_keys_.clear();
    active_instance_count_ = 0;
    are_tables_dirty_ = true;
    resident_cards_ = 0;
    resident_page_owners_.clear();
    captures_.clear();
    card_table_.clear();
    page_table_.clear();
    instance_table_.clear();
    resample_cards_.clear();
    resample_pages_.clear();
    resample_table_.clear();
    resample_page_base_ = 0;
    resident_pages_.clear();
    resident_slots_.clear();
    page_entry_slots_.clear();
    card_owner_keys_.clear();
    hi_res_requests_.clear();
    hi_res_pages_.clear();
    is_source_active_.clear();
    ++card_index_revision_;
    for(auto& pages : lit_pages_)
    {
        pages.clear();
    }
    priority_tables_revision_ = ~0ull;
    stats_ = {};
}

auto lumen_scene::get_page_origin(uint32_t page) const -> math::uvec2
{
    return math::uvec2(page % pages_per_side_, page / pages_per_side_) * physical_page_size;
}

auto lumen_scene::compute_mip_desc(uint32_t res_level, const math::uvec2& bias) const -> mip_desc
{
    mip_desc desc;
    desc.res_level.x = uint32_t(std::clamp(int(res_level) - int(bias.x), int(min_res_level), int(max_res_level)));
    desc.res_level.y = uint32_t(std::clamp(int(res_level) - int(bias.y), int(min_res_level), int(max_res_level)));
    if(desc.res_level.x > sub_allocation_res_level || desc.res_level.y > sub_allocation_res_level)
    {
        desc.res_level = math::max(desc.res_level, math::uvec2(sub_allocation_res_level));
        desc.is_sub_allocation = false;
        desc.size_in_pages = math::uvec2(1u << (desc.res_level.x - sub_allocation_res_level),
                                         1u << (desc.res_level.y - sub_allocation_res_level));
        desc.page_resolution = math::uvec2(physical_page_size);
    }
    else
    {
        desc.is_sub_allocation = true;
        desc.size_in_pages = math::uvec2(1u);
        desc.page_resolution = math::uvec2(1u << desc.res_level.x, 1u << desc.res_level.y);
    }
    return desc;
}

auto lumen_scene::compute_page_uv_rect(const mip_desc& mip, uint32_t page) -> math::vec4
{
    const math::uvec2 pages = mip.size_in_pages;
    const math::uvec2 coord(page % pages.x, page / pages.x);
    math::vec4 rect(float(coord.x) / float(pages.x),
                    float(coord.y) / float(pages.y),
                    float(coord.x + 1u) / float(pages.x),
                    float(coord.y + 1u) / float(pages.y));
    if(!mip.is_sub_allocation)
    {
        // Half a texel of border on interior page edges, for seamless bilinear sampling.
        const float border_x = 0.5f * (rect.z - rect.x) / float(physical_page_size);
        const float border_y = 0.5f * (rect.w - rect.y) / float(physical_page_size);
        rect.x -= coord.x > 0u ? border_x : 0.0f;
        rect.y -= coord.y > 0u ? border_y : 0.0f;
        rect.z += coord.x + 1u < pages.x ? border_x : 0.0f;
        rect.w += coord.y + 1u < pages.y ? border_y : 0.0f;
    }
    return rect;
}

auto lumen_scene::find_bin(const math::uvec2& element_size) const -> const sub_allocation_bin*
{
    const int32_t index = bin_lookup_[get_bin_lookup_index(element_size)];
    return index < 0 ? nullptr : &bins_[size_t(index)];
}

auto lumen_scene::has_physical_space(const mip_desc& mip) const -> bool
{
    if(!mip.is_sub_allocation)
    {
        return free_pages_.size() >= size_t(mip.size_in_pages.x) * size_t(mip.size_in_pages.y);
    }
    if(!free_pages_.empty())
    {
        return true;
    }
    const sub_allocation_bin* bin = find_bin(mip.page_resolution);
    return bin != nullptr && bin->free_slots > 0;
}

auto lumen_scene::allocate_slot(const math::uvec2& element_size, physical_slot& out) -> bool
{
    int32_t& bin_index = bin_lookup_[get_bin_lookup_index(element_size)];
    const math::uvec2 slots(physical_page_size / element_size.x, physical_page_size / element_size.y);
    if(bin_index < 0)
    {
        sub_allocation_bin bin;
        bin.element_size = element_size;
        bin.slot_count = slots.x * slots.y;
        bins_.push_back(std::move(bin));
        bin_index = int32_t(bins_.size() - 1u);
    }
    sub_allocation_bin& bin = bins_[size_t(bin_index)];
    // The first free slot of the first page with room, as a scan of the pages in order would find it.
    for(size_t p = 0; bin.free_slots > 0 && p < bin.pages.size(); ++p)
    {
        if(bin.free_counts[p] == 0)
        {
            continue;
        }
        auto& used = bin.used[p];
        for(uint32_t word = 0; word < k_slot_words; ++word)
        {
            const uint64_t free_bits = ~used[word];
            if(free_bits == 0)
            {
                continue;
            }
            const uint32_t bit = uint32_t(std::countr_zero(free_bits));
            const uint32_t slot = word * k_slot_word_bits + bit;
            used[word] |= 1ull << bit;
            --bin.free_counts[p];
            --bin.free_slots;
            out.page = bin.pages[p];
            out.slot = slot;
            out.atlas_offset = get_page_origin(out.page) + math::uvec2(slot % slots.x, slot / slots.x) * element_size;
            return true;
        }
    }
    if(free_pages_.empty())
    {
        return false;
    }
    const uint32_t page = free_pages_.back();
    free_pages_.pop_back();
    // Bits past the page's slots stay set, so a free bit is always a slot.
    std::vector<uint64_t> used(k_slot_words, 0ull);
    for(uint32_t slot = bin.slot_count; slot < k_slot_words * k_slot_word_bits; ++slot)
    {
        used[slot / k_slot_word_bits] |= 1ull << (slot % k_slot_word_bits);
    }
    used[0] |= 1ull;
    bin.pages.push_back(page);
    bin.used.push_back(std::move(used));
    bin.free_counts.push_back(bin.slot_count - 1u);
    bin.free_slots += bin.slot_count - 1u;
    out.page = page;
    out.slot = 0;
    out.atlas_offset = get_page_origin(page);
    return true;
}

void lumen_scene::free_slot(const physical_slot& slot, const mip_desc& mip)
{
    if(!mip.is_sub_allocation)
    {
        free_pages_.push_back(slot.page);
        return;
    }
    const int32_t bin_index = bin_lookup_[get_bin_lookup_index(mip.page_resolution)];
    if(bin_index < 0)
    {
        return;
    }
    sub_allocation_bin& bin = bins_[size_t(bin_index)];
    for(size_t p = 0; p < bin.pages.size(); ++p)
    {
        if(bin.pages[p] != slot.page)
        {
            continue;
        }
        uint64_t& word = bin.used[p][slot.slot / k_slot_word_bits];
        const uint64_t bit = 1ull << (slot.slot % k_slot_word_bits);
        if((word & bit) != 0)
        {
            word &= ~bit;
            ++bin.free_counts[p];
            ++bin.free_slots;
        }
        if(bin.free_counts[p] == bin.slot_count)
        {
            free_pages_.push_back(bin.pages[p]);
            bin.free_slots -= bin.slot_count;
            bin.pages.erase(bin.pages.begin() + std::ptrdiff_t(p));
            bin.used.erase(bin.used.begin() + std::ptrdiff_t(p));
            bin.free_counts.erase(bin.free_counts.begin() + std::ptrdiff_t(p));
        }
        return;
    }
}

void lumen_scene::free_card(card_state& card)
{
    for(const auto& slot : card.slots)
    {
        free_slot(slot, card.mip);
    }
    card.slots.clear();
    card.res_level = 0;
    free_hi_res(card);
}

void lumen_scene::free_hi_res(card_state& card)
{
    hi_res_mip& hi = card.hi_res;
    for(size_t page = 0; page < hi.slots.size(); ++page)
    {
        if(hi.is_mapped[page] != 0)
        {
            free_slot(hi.slots[page], hi.mip);
        }
    }
    // Its keys in hi_res_pages_ go stale and are dropped when the eviction meets them.
    hi = hi_res_mip{};
}

auto lumen_scene::get_feedback_page(const mip_desc& mip, const math::uvec2& page, uint32_t res_level) -> uint32_t
{
    // The element's page grid at its level, without the aspect bias, and the centre of its page on the card.
    const float grid = float(1u << (std::max(res_level, sub_allocation_res_level) - sub_allocation_res_level));
    const math::vec2 center = (math::vec2(page) + 0.5f) / grid;
    const math::uvec2 pages = mip.size_in_pages;
    const uint32_t x = std::min(uint32_t(center.x * float(pages.x)), pages.x - 1u);
    const uint32_t y = std::min(uint32_t(center.y * float(pages.y)), pages.y - 1u);
    return x + y * pages.x;
}

auto lumen_scene::has_page_space(const mip_desc& mip) const -> bool
{
    return mip.is_sub_allocation ? has_physical_space(mip) : !free_pages_.empty();
}

auto lumen_scene::evict_oldest_hi_res_page(uint64_t min_idle_frames) -> bool
{
    const auto find_mip = [this](const hi_res_page_key& key) -> hi_res_mip*
    {
        const auto it = placements_.find(key.identity);
        if(it == placements_.end() || key.card >= it->second.card_states.size())
        {
            return nullptr;
        }
        hi_res_mip& hi = it->second.card_states[key.card].hi_res;
        return key.page < hi.is_mapped.size() && hi.is_mapped[key.page] != 0 ? &hi : nullptr;
    };
    hi_res_pages_.erase(std::remove_if(hi_res_pages_.begin(),
                                       hi_res_pages_.end(),
                                       [&](const hi_res_page_key& key)
                                       {
                                           return find_mip(key) == nullptr;
                                       }),
                        hi_res_pages_.end());
    size_t oldest = hi_res_pages_.size();
    uint64_t oldest_frame = std::numeric_limits<uint64_t>::max();
    for(size_t k = 0; k < hi_res_pages_.size(); ++k)
    {
        const uint64_t last_used = find_mip(hi_res_pages_[k])->last_used[hi_res_pages_[k].page];
        if(last_used < oldest_frame)
        {
            oldest_frame = last_used;
            oldest = k;
        }
    }
    if(oldest == hi_res_pages_.size() || oldest_frame + min_idle_frames > frame_)
    {
        return false;
    }
    const hi_res_page_key key = hi_res_pages_[oldest];
    hi_res_mip& hi = *find_mip(key);
    free_slot(hi.slots[key.page], hi.mip);
    hi.slots[key.page] = physical_slot{};
    hi.is_mapped[key.page] = 0;
    hi_res_pages_[oldest] = hi_res_pages_.back();
    hi_res_pages_.pop_back();
    are_tables_dirty_ = true;
    return true;
}

auto lumen_scene::append_resample_source(const card_state& card, const placed_card& placed) -> int32_t
{
    const int32_t index = int32_t(resample_cards_.size() / card_stride);
    // The card's box as placed this frame: the resample maps the new pages' card UV onto the previous mip, assuming
    // the card's extent has not changed (UE ResampleLightingHistoryToCardCaptureAtlasCS).
    append_card_record(resample_cards_,
                       placed,
                       uint32_t(resample_pages_.size()),
                       math::vec4(float(card.mip.size_in_pages.x),
                                  float(card.mip.size_in_pages.y),
                                  float(card.mip.res_level.x),
                                  float(card.mip.res_level.y)),
                       math::vec4(float(resample_pages_.size()),
                                  float(card.mip.size_in_pages.x),
                                  float(card.mip.size_in_pages.y),
                                  0.0f));
    for(const auto& slot : card.slots)
    {
        resample_pages_.push_back(math::vec4(float(slot.atlas_offset.x),
                                             float(slot.atlas_offset.y),
                                             float(card.mip.res_level.x),
                                             float(card.mip.res_level.y)));
    }
    return index;
}

auto lumen_scene::allocate(card_state& card, uint32_t res_level) -> bool
{
    const mip_desc mip = compute_mip_desc(res_level, card.res_level_bias);
    const uint32_t page_count = mip.size_in_pages.x * mip.size_in_pages.y;
    if(!mip.is_sub_allocation && free_pages_.size() < page_count)
    {
        return false;
    }
    std::vector<physical_slot> slots(page_count);
    if(mip.is_sub_allocation)
    {
        if(!allocate_slot(mip.page_resolution, slots[0]))
        {
            return false;
        }
    }
    else
    {
        for(auto& slot : slots)
        {
            slot.page = free_pages_.back();
            free_pages_.pop_back();
            slot.atlas_offset = get_page_origin(slot.page);
        }
    }
    card.mip = mip;
    card.slots = std::move(slots);
    card.res_level = res_level;
    return true;
}

void lumen_scene::update(const std::vector<source>& sources, uint32_t instance_count, const math::vec3& view_origin)
{
    viewer single;
    single.origin = view_origin;
    update(sources, instance_count, std::vector<viewer>{single});
}

void lumen_scene::update(const std::vector<source>& sources,
                         uint32_t instance_count,
                         const std::vector<viewer>& viewers)
{
    APP_SCOPE_PERF("GI/Scene Update");
    ++frame_;
    stats_ = {};
    const bool has_unique_placements = refresh_placements(sources);
    are_tables_dirty_ = collect_active(sources, instance_count) || are_tables_dirty_;
    // A placement repeated within the frame is shared by two sources, so its cards must be visited in order.
    choose_resolutions(sources, viewers, has_unique_placements);
    if(!are_hi_res_pages_enabled_ && hi_res_page_count_ != 0)
    {
        for(auto& [identity, entry] : placements_)
        {
            for(auto& state : entry.card_states)
            {
                free_hi_res(state);
            }
        }
        hi_res_pages_.clear();
        hi_res_requests_.clear();
        are_tables_dirty_ = true;
    }
    capture_packer packer(settings_.capture_atlas_size);
    captures_.clear();
    resample_cards_.clear();
    resample_pages_.clear();
    add_hi_res_requests(viewers);
    allocate_requests(sources, packer);
    build_tables(sources, instance_count);
    refresh_captures(sources, packer);
    resample_page_base_ = uint32_t(resample_cards_.size());
    resample_table_ = resample_cards_;
    resample_table_.insert(resample_table_.end(), resample_pages_.begin(), resample_pages_.end());
    stats_.resident_cards = resident_cards_;
    stats_.pages_total = pages_per_side_ * pages_per_side_;
    stats_.pages_used = stats_.pages_total - uint32_t(free_pages_.size());
    stats_.captures = uint32_t(captures_.size());
    stats_.hi_res_pages = hi_res_page_count_;
}

auto lumen_scene::refresh_placements(const std::vector<source>& sources) -> bool
{
    APP_SCOPE_PERF("GI/Scene Update/Placements");
    bool is_unique = true;
    source_placements_.resize(sources.size());
    for(size_t s = 0; s < sources.size(); ++s)
    {
        const source& src = sources[s];
        auto [it, is_new] = placements_.try_emplace(src.identity);
        placement& entry = it->second;
        are_tables_dirty_ = are_tables_dirty_ || is_new || entry.cards != src.cards;
        entry.source_index = uint32_t(s);
        if(entry.cards != src.cards)
        {
            // Another card set moves the card indices of every placement after this one.
            ++card_index_revision_;
            for(auto& card : entry.card_states)
            {
                free_card(card);
            }
            entry.cards = src.cards;
            entry.card_states.assign(src.cards ? src.cards->cards.size() : 0u, card_state{});
            entry.has_placed = false;
        }
        if(entry.material_key != src.material_key)
        {
            // The material changed: its resident pages read as never captured, so the refresh takes them first.
            entry.material_key = src.material_key;
            for(auto& card : entry.card_states)
            {
                for(auto& slot : card.slots)
                {
                    slot.captured_frame = 0;
                }
            }
        }
        is_unique = is_unique && entry.last_seen != frame_;
        entry.last_seen = frame_;
        source_placements_[s] = &entry;
    }
    // Erasing leaves the other entries in place, so source_placements_ stays valid.
    for(auto it = placements_.begin(); it != placements_.end();)
    {
        if(it->second.last_seen != frame_)
        {
            for(auto& card : it->second.card_states)
            {
                free_card(card);
            }
            it = placements_.erase(it);
            are_tables_dirty_ = true;
            continue;
        }
        ++it;
    }
    return is_unique;
}

auto lumen_scene::collect_active(const std::vector<source>& sources, uint32_t instance_count) -> bool
{
    // Every pass below walks this list, so card indices agree between the captures and the packed table.
    active_.clear();
    first_card_.assign(sources.size(), 0u);
    is_source_active_.assign(sources.size(), 0u);
    bool has_changed = instance_count != active_instance_count_;
    uint32_t card_total = 0;
    for(uint32_t s = 0; s < uint32_t(sources.size()); ++s)
    {
        if(sources[s].cards && sources[s].instance_index < instance_count)
        {
            const std::pair<uint64_t, uint32_t> key(sources[s].identity, sources[s].instance_index);
            has_changed = has_changed || active_.size() >= active_keys_.size() || active_keys_[active_.size()] != key;
            active_.push_back(s);
            is_source_active_[s] = 1u;
            first_card_[s] = card_total;
            card_total += uint32_t(source_placements_[s]->card_states.size());
        }
    }
    has_changed = has_changed || active_.size() != active_keys_.size();
    if(has_changed)
    {
        active_keys_.resize(active_.size());
        for(size_t a = 0; a < active_.size(); ++a)
        {
            active_keys_[a] = {sources[active_[a]].identity, sources[active_[a]].instance_index};
        }
        active_instance_count_ = instance_count;
        ++card_index_revision_;
    }
    return has_changed;
}

void lumen_scene::choose_resolutions(const std::vector<source>& sources,
                                     const std::vector<viewer>& viewers,
                                     bool parallel)
{
    APP_SCOPE_PERF("GI/Scene Update/Resolutions");
    const uint32_t active_count = uint32_t(active_.size());
    const uint32_t chunk_count =
        parallel ? std::max(1u, (active_count + k_placements_per_task - 1u) / k_placements_per_task) : 1u;
    resolution_chunks_.resize(chunk_count);
    poolstl::for_each_par_if(chunk_count > 1u,
                             poolstl::iota_iter<uint32_t>(0),
                             poolstl::iota_iter<uint32_t>(chunk_count),
                             [&](uint32_t chunk_index)
                             {
                                 const uint32_t begin = chunk_count > 1u ? chunk_index * k_placements_per_task : 0u;
                                 const uint32_t end =
                                     chunk_count > 1u ? std::min(begin + k_placements_per_task, active_count) : active_count;
                                 choose_chunk_resolutions(sources,
                                                          viewers,
                                                          begin,
                                                          end,
                                                          resolution_chunks_[chunk_index]);
                             });
    // In placement order, so the frees and the requests come out as one pass over the placements makes them. Nothing
    // in the pass reads the allocator's state, so the frees can follow it.
    requests_.clear();
    for(const auto& chunk : resolution_chunks_)
    {
        for(const math::uvec2& card : chunk.frees)
        {
            free_card(source_placements_[card.x]->card_states[card.y]);
        }
        requests_.insert(requests_.end(), chunk.requests.begin(), chunk.requests.end());
        stats_.cards += chunk.cards;
        stats_.texels_desired += chunk.texels_desired;
        are_tables_dirty_ = are_tables_dirty_ || chunk.has_moved || chunk.has_motion_change || !chunk.frees.empty();
    }
}

void lumen_scene::choose_chunk_resolutions(const std::vector<source>& sources,
                                           const std::vector<viewer>& viewers,
                                           uint32_t begin,
                                           uint32_t end,
                                           resolution_chunk& chunk)
{
    chunk.requests.clear();
    chunk.frees.clear();
    chunk.cards = 0;
    chunk.texels_desired = 0;
    chunk.has_moved = false;
    chunk.has_motion_change = false;
    const resolution_rule rule = get_resolution_rule();
    const float max_card_distance = get_max_card_distance();
    for(uint32_t a = begin; a < end; ++a)
    {
        const uint32_t s = active_[a];
        const source& src = sources[s];
        placement& entry = *source_placements_[s];
        const bool is_moving = entry.has_placed && !is_same_transform(entry.placed_transform, src.local_to_world);
        const float motion = is_moving ? compute_motion_bound(entry.cards->bounds, entry.placed_transform, src.local_to_world)
                                       : 0.0f;
        chunk.has_motion_change = chunk.has_motion_change || motion != entry.motion;
        entry.motion = motion;
        // Cards are placed once per transform: a static placement reuses its boxes every frame.
        if(!entry.has_placed || !is_same_transform(entry.placed_transform, src.local_to_world))
        {
            entry.placed.resize(entry.card_states.size());
            math::vec3 bounds_min(std::numeric_limits<float>::max());
            math::vec3 bounds_max(-std::numeric_limits<float>::max());
            for(size_t c = 0; c < entry.placed.size(); ++c)
            {
                const placed_card placed = place_card(entry.cards->cards[c], src.local_to_world);
                entry.placed[c] = placed;
                const math::vec3 half = math::abs(placed.axis_x) * placed.extent.x + math::abs(placed.axis_y) * placed.extent.y +
                                        math::abs(placed.axis_z) * placed.extent.z;
                bounds_min = math::min(bounds_min, placed.origin - half);
                bounds_max = math::max(bounds_max, placed.origin + half);
            }
            const bool has_cards = !entry.placed.empty();
            entry.bounds_center = has_cards ? 0.5f * (bounds_min + bounds_max) : math::vec3(0.0f);
            entry.bounds_extent = has_cards ? 0.5f * (bounds_max - bounds_min) : math::vec3(0.0f);
            entry.placed_transform = src.local_to_world;
            entry.has_placed = true;
            chunk.has_moved = true;
            // The cards moved with the placement: their pages' direct lighting is due.
            for(auto& state : entry.card_states)
            {
                for(auto& slot : state.slots)
                {
                    slot.is_direct_dirty = true;
                }
            }
        }
        // The placement's own gate (UE's primitive-group residency, LumenSceneRendering.cpp:501-514): its largest
        // extent must project to the minimum card resolution (one texel for an emissive light source) at its distance.
        const float group_distance =
            std::max(nearest_distance_to_box(entry.bounds_center, entry.bounds_extent, viewers), k_min_group_distance);
        const float group_extent = std::max(entry.bounds_extent.x, std::max(entry.bounds_extent.y, entry.bounds_extent.z));
        const float group_resolution = rule.texel_density_scale * group_extent / group_distance + k_group_resolution_bias;
        const bool is_group_resident =
            card_residency_without_group_gate_ ||
            group_resolution >= float(src.is_emissive_light_source ? k_emissive_min_card_resolution : rule.min_resolution);
        for(uint32_t c = 0; c < uint32_t(entry.card_states.size()); ++c)
        {
            card_state& state = entry.card_states[c];
            const placed_card& placed = entry.placed[c];
            const float distance = std::max(nearest_distance_to_card(placed, viewers), k_min_viewer_distance);
            const float max_extent = std::max(placed.extent.x, placed.extent.y);
            const float projected = std::min(rule.texel_density_scale * max_extent / distance,
                                             settings_.max_texel_density * max_extent);
            const uint32_t truncated = std::min(uint32_t(std::max(projected, 0.0f)), rule.max_resolution);
            // UE RoundUpToPowerOfTwo: 0 rounds up to 1, so a card below one texel stays at the minimum resolution
            // wherever its minimum is 1 (an emissive light source, while its placement is in range).
            const uint32_t snapped =
                truncated == 0 ? (card_residency_without_group_gate_ ? 0u : 1u) : round_up_power_of_two(truncated);
            const uint32_t min_resolution =
                src.is_emissive_light_source ? k_emissive_min_card_resolution : rule.min_resolution;
            const float min_area = settings_.mesh_cards_min_size * settings_.mesh_cards_min_size *
                                   (src.is_emissive_light_source ? k_emissive_min_card_area_scale : 1.0f);
            const bool is_large_enough = 4.0f * placed.extent.x * placed.extent.y > min_area;
            const bool visible =
                is_group_resident && is_large_enough && distance < max_card_distance && snapped >= min_resolution;
            const uint32_t res_level = floor_log2(std::max(snapped, k_min_card_resolution));
            // Texels stay roughly square: the shorter axis drops a level per doubling of the aspect.
            const float aspect = placed.extent.x / std::max(placed.extent.y, 1e-6f);
            state.res_level_bias = aspect >= 1.0f
                                       ? math::uvec2(0u, std::min(floor_log2(uint32_t(std::lround(aspect))), 8u))
                                       : math::uvec2(std::min(floor_log2(uint32_t(std::lround(1.0f / aspect))), 8u), 0u);
            ++chunk.cards;
            if(!visible)
            {
                if(state.res_level != 0)
                {
                    chunk.frees.push_back(math::uvec2(s, c));
                }
                state.desired_res_level = 0;
                state.res_level_on_last_alloc = 0;
                continue;
            }
            state.desired_res_level = res_level;
            const mip_desc desired = compute_mip_desc(res_level, state.res_level_bias);
            chunk.texels_desired += (1u << desired.res_level.x) * (1u << desired.res_level.y);
            if(res_level == state.res_level_on_last_alloc || (hold_resident_resolutions_ && state.res_level != 0))
            {
                continue;
            }
            float ranked = distance;
            if(state.res_level != 0)
            {
                // Reallocations rank behind new cards unless the level moves by two or more.
                const float delta = std::fabs(float(state.res_level_on_last_alloc) - float(res_level));
                ranked += (1.0f - std::clamp((delta + 1.0f) / 3.0f, 0.0f, 1.0f)) * k_realloc_distance_penalty;
            }
            chunk.requests.push_back({s, c, compute_distance_bin(ranked)});
        }
    }
}

void lumen_scene::add_hi_res_requests(const std::vector<viewer>& viewers)
{
    stats_.hi_res_requests = uint32_t(hi_res_requests_.size());
    for(const hi_res_request& asked : hi_res_requests_)
    {
        const auto it = placements_.find(asked.identity);
        if(it == placements_.end())
        {
            continue;
        }
        const placement& entry = it->second;
        const bool is_shown = entry.last_seen == frame_ && entry.has_placed &&
                              entry.source_index < is_source_active_.size() &&
                              is_source_active_[entry.source_index] != 0u && asked.card < entry.card_states.size();
        if(!is_shown)
        {
            continue;
        }
        const card_state& state = entry.card_states[asked.card];
        if(state.res_level == 0 || asked.res_level <= state.res_level)
        {
            continue;
        }
        const float distance = nearest_distance_to_card(entry.placed[asked.card], viewers) + asked.distance_bias;
        requests_.push_back({entry.source_index, asked.card, compute_distance_bin(distance), true, asked.res_level,
                             asked.page});
    }
    hi_res_requests_.clear();
}

void lumen_scene::set_feedback(const std::vector<feedback_element>& elements,
                               uint64_t card_index_revision,
                               uint32_t min_hits,
                               uint32_t sample_count)
{
    hi_res_requests_.clear();
    if(!are_hi_res_pages_enabled_ || card_index_revision != card_index_revision_)
    {
        return;
    }
    for(const feedback_element& element : elements)
    {
        if(element.hits <= min_hits || element.card_index >= card_owner_keys_.size())
        {
            continue;
        }
        const auto& owner = card_owner_keys_[element.card_index];
        const auto it = placements_.find(owner.first);
        if(it == placements_.end() || owner.second >= it->second.card_states.size())
        {
            continue;
        }
        card_state& state = it->second.card_states[owner.second];
        const uint32_t level = std::clamp(element.res_level, min_res_level, max_res_level);
        // Only a page above the locked level (UE: Request.ResLevel > Card.MinAllocatedResLevel).
        if(state.res_level == 0 || level <= state.res_level)
        {
            continue;
        }
        hi_res_mip& hi = state.hi_res;
        if(level <= hi.res_level)
        {
            const uint32_t page = get_feedback_page(hi.mip, element.page, level);
            if(hi.is_mapped[page] != 0)
            {
                // A mapped page the feedback still reads stays (UE UnlockedAllocationHeap.Update).
                hi.last_used[page] = frame_;
                continue;
            }
        }
        const float share = float(element.hits) / float(std::max(sample_count, 1u));
        hi_res_requests_.push_back({owner.first,
                                    owner.second,
                                    level,
                                    element.page,
                                    k_hi_res_distance_bias + k_hi_res_distance_bias * (1.0f - std::min(share, 1.0f))});
    }
}

auto lumen_scene::allocate_hi_res_page(const std::vector<source>& sources, const request& req, capture_packer& packer)
    -> uint32_t
{
    placement& entry = *source_placements_[req.source];
    card_state& state = entry.card_states[req.card];
    if(state.res_level == 0 || req.hi_res_level <= state.res_level)
    {
        return 0;
    }
    hi_res_mip& hi = state.hi_res;
    // One hi-res mip per card, the finest any request asked for.
    const uint32_t level = std::max(req.hi_res_level, hi.res_level);
    if(level != hi.res_level)
    {
        const mip_desc mip = compute_mip_desc(level, state.res_level_bias);
        if(mip.res_level == state.mip.res_level)
        {
            // Both axes stay at their locked levels: nothing finer to map.
            return 0;
        }
        free_hi_res(state);
        const size_t page_count = size_t(mip.size_in_pages.x) * size_t(mip.size_in_pages.y);
        hi.res_level = level;
        hi.mip = mip;
        hi.slots.assign(page_count, physical_slot{});
        hi.is_mapped.assign(page_count, 0u);
        hi.last_used.assign(page_count, 0u);
        are_tables_dirty_ = true;
    }
    const uint32_t page = get_feedback_page(hi.mip, req.hi_res_page, req.hi_res_level);
    if(hi.is_mapped[page] != 0)
    {
        hi.last_used[page] = frame_;
        return 0;
    }
    while(!has_page_space(hi.mip) && evict_oldest_hi_res_page(k_hi_res_idle_frames_for_hi_res))
    {
    }
    math::uvec2 capture_offset(0u);
    if(!has_page_space(hi.mip) || !packer.place(hi.mip.page_resolution, capture_offset))
    {
        return 0;
    }
    physical_slot& slot = hi.slots[page];
    slot = physical_slot{};
    if(hi.mip.is_sub_allocation)
    {
        allocate_slot(hi.mip.page_resolution, slot);
    }
    else
    {
        slot.page = free_pages_.back();
        free_pages_.pop_back();
        slot.atlas_offset = get_page_origin(slot.page);
    }
    slot.captured_frame = frame_;
    slot.is_direct_dirty = true;
    hi.is_mapped[page] = 1u;
    hi.last_used[page] = frame_;
    const source& src = sources[req.source];
    hi_res_pages_.push_back({src.identity, req.card, page});
    // The page starts from the locked mip's lighting (UE bResampleLastLighting), not dark.
    const int32_t resample_card =
        append_resample_source(state,
                               entry.has_placed && is_same_transform(entry.placed_transform, src.local_to_world)
                                   ? entry.placed[req.card]
                                   : place_card(entry.cards->cards[req.card], src.local_to_world));
    capture cap;
    cap.card_index = first_card_[req.source] + req.card;
    cap.source_index = req.source;
    cap.card_uv_rect = compute_page_uv_rect(hi.mip, page);
    cap.capture_offset = capture_offset;
    cap.atlas_offset = slot.atlas_offset;
    cap.size = hi.mip.page_resolution;
    cap.resample_card = resample_card;
    captures_.push_back(cap);
    are_tables_dirty_ = true;
    return 1;
}

void lumen_scene::allocate_requests(const std::vector<source>& sources, capture_packer& packer)
{
    APP_SCOPE_PERF("GI/Scene Update/Allocate");
    // Nearest bins first, then request order; the budget cuts inside the last bin.
    std::stable_sort(requests_.begin(),
                     requests_.end(),
                     [](const request& a, const request& b)
                     {
                         return a.bin < b.bin;
                     });
    uint32_t pages_requested = 0;
    std::vector<math::uvec2> capture_offsets;
    // The hi-res requests within the budget, mapped after every locked one (UE HiResPagesToMap).
    std::vector<const request*> hi_res;
    for(const auto& req : requests_)
    {
        if(pages_requested + uint32_t(hi_res.size()) >= settings_.max_captures_per_frame)
        {
            break;
        }
        if(req.is_hi_res)
        {
            hi_res.push_back(&req);
            continue;
        }
        placement& entry = *source_placements_[req.source];
        card_state& state = entry.card_states[req.card];
        // The level that fits the physical atlas beside everything resident, the card's own allocation included: a
        // locked mip never evicts another and drops levels instead, once the hi-res pages idle for two frames are gone.
        uint32_t level = state.desired_res_level;
        mip_desc mip = compute_mip_desc(level, state.res_level_bias);
        while(!has_physical_space(mip) && evict_oldest_hi_res_page(k_hi_res_idle_frames_for_locked))
        {
        }
        while(!has_physical_space(mip) && level > min_res_level)
        {
            --level;
            mip = compute_mip_desc(level, state.res_level_bias);
        }
        if(!has_physical_space(mip))
        {
            continue;
        }
        // The whole mip is captured this frame or the card waits, keeping its allocation, for a frame with room in the
        // capture atlas: a reallocated card's previous pages are read by the lighting resample this frame only.
        const uint32_t page_count = mip.size_in_pages.x * mip.size_in_pages.y;
        capture_offsets.assign(page_count, math::uvec2(0u));
        capture_packer trial = packer;
        bool fits = true;
        for(auto& offset : capture_offsets)
        {
            fits = fits && trial.place(mip.page_resolution, offset);
        }
        if(!fits)
        {
            continue;
        }
        packer = std::move(trial);
        stats_.downgraded += state.desired_res_level - level;
        int32_t resample_card = -1;
        if(state.res_level != 0)
        {
            ++stats_.reallocated;
            const source& src = sources[req.source];
            // The box as this source places it (a shared placement caches its last source's transform).
            resample_card = append_resample_source(state,
                                                   entry.has_placed && is_same_transform(entry.placed_transform, src.local_to_world)
                                                       ? entry.placed[req.card]
                                                       : place_card(entry.cards->cards[req.card], src.local_to_world));
        }
        // The locked mip alone: a hi-res mip above the new level keeps its pages.
        for(const auto& slot : state.slots)
        {
            free_slot(slot, state.mip);
        }
        state.slots.clear();
        allocate(state, level);
        if(state.hi_res.res_level != 0 && state.hi_res.res_level <= state.res_level)
        {
            free_hi_res(state);
        }
        for(auto& slot : state.slots)
        {
            slot.captured_frame = frame_;
            slot.is_direct_dirty = true;
        }
        // Recorded even when downgraded, so a card that only fits lower is not re-requested.
        state.res_level_on_last_alloc = state.desired_res_level;
        for(uint32_t page = 0; page < page_count; ++page)
        {
            capture cap;
            cap.card_index = first_card_[req.source] + req.card;
            cap.source_index = req.source;
            cap.card_uv_rect = compute_page_uv_rect(state.mip, page);
            cap.capture_offset = capture_offsets[page];
            cap.atlas_offset = state.slots[page].atlas_offset;
            cap.size = state.mip.page_resolution;
            cap.resample_card = resample_card;
            captures_.push_back(cap);
        }
        pages_requested += page_count;
    }
    for(const request* req : hi_res)
    {
        if(pages_requested >= settings_.max_captures_per_frame)
        {
            break;
        }
        pages_requested += allocate_hi_res_page(sources, *req, packer);
    }
    // Only allocations change the tables; the refresh after them captures pages in place.
    are_tables_dirty_ = are_tables_dirty_ || !captures_.empty();
}

void lumen_scene::build_tables(const std::vector<source>& sources, uint32_t instance_count)
{
    APP_SCOPE_PERF("GI/Scene Update/Tables");
    // Kept while nothing they hold changed: the placements, the active set and every allocation are as last frame.
    if(!are_tables_dirty_)
    {
        return;
    }
    are_tables_dirty_ = false;
    ++tables_revision_;
    resident_cards_ = 0;
    // Packed tables: cards, the page table (rebuilt from the physical allocations) and the per-instance card ranges.
    card_table_.clear();
    page_table_.clear();
    page_entry_slots_.clear();
    card_owner_keys_.clear();
    resident_pages_.clear();
    resident_slots_.clear();
    resident_page_owners_.clear();
    hi_res_page_count_ = 0;
    instance_table_.assign(size_t(instance_count) * instance_stride, math::vec4(0.0f));
    for(uint32_t a = 0; a < uint32_t(active_.size()); ++a)
    {
        const uint32_t s = active_[a];
        const auto& src = sources[s];
        placement& entry = *source_placements_[s];
        const bool is_placed = entry.has_placed && is_same_transform(entry.placed_transform, src.local_to_world);
        const uint32_t first_card = uint32_t(card_table_.size() / card_stride);
        instance_table_[src.instance_index] = math::vec4(float(first_card),
                                                         float(entry.card_states.size()),
                                                         entry.cards->is_mostly_two_sided ? 1.0f : 0.0f,
                                                         entry.motion);
        for(uint32_t c = 0; c < uint32_t(entry.card_states.size()); ++c)
        {
            card_state& state = entry.card_states[c];
            const bool resident = state.res_level != 0;
            const bool has_hi_res = resident && state.hi_res.res_level != 0;
            const auto locked_offset = uint32_t(page_table_.size());
            const auto locked_pages = uint32_t(state.slots.size());
            const math::uvec2 locked_size = resident ? state.mip.size_in_pages : math::uvec2(1u);
            // The reflections' table: the hi-res span after the locked one, or the locked span itself.
            const math::vec4 reflection_table =
                has_hi_res ? math::vec4(float(locked_offset + locked_pages),
                                        float(state.hi_res.mip.size_in_pages.x),
                                        float(state.hi_res.mip.size_in_pages.y),
                                        1.0f)
                           : math::vec4(float(locked_offset), float(locked_size.x), float(locked_size.y), 0.0f);
            append_card_record(card_table_,
                               is_placed ? entry.placed[c] : place_card(entry.cards->cards[c], src.local_to_world),
                               locked_offset,
                               resident ? math::vec4(float(state.mip.size_in_pages.x),
                                                     float(state.mip.size_in_pages.y),
                                                     float(state.mip.res_level.x),
                                                     float(state.mip.res_level.y))
                                        : math::vec4(1.0f, 1.0f, 0.0f, 0.0f),
                               reflection_table);
            card_owner_keys_.emplace_back(src.identity, c);
            if(!resident)
            {
                continue;
            }
            ++resident_cards_;
            const uint32_t card_index = uint32_t(card_table_.size() / card_stride) - 1u;
            for(uint32_t page = 0; page < locked_pages; ++page)
            {
                auto& slot = state.slots[page];
                resident_pages_.push_back({card_index,
                                           compute_page_uv_rect(state.mip, page),
                                           slot.atlas_offset,
                                           state.mip.page_resolution,
                                           locked_offset,
                                           state.mip.size_in_pages});
                resident_slots_.push_back(&slot);
                resident_page_owners_.push_back(math::uvec2(a, c));
                page_table_.push_back(math::vec4(float(slot.atlas_offset.x),
                                                 float(slot.atlas_offset.y),
                                                 float(state.mip.res_level.x),
                                                 float(state.mip.res_level.y)));
                page_entry_slots_.push_back(&slot);
            }
            if(!has_hi_res)
            {
                continue;
            }
            hi_res_mip& hi = state.hi_res;
            const auto hi_offset = uint32_t(page_table_.size());
            const math::uvec2 hi_size = hi.mip.size_in_pages;
            for(uint32_t page = 0; page < uint32_t(hi.slots.size()); ++page)
            {
                if(hi.is_mapped[page] != 0)
                {
                    auto& slot = hi.slots[page];
                    resident_pages_.push_back({card_index,
                                               compute_page_uv_rect(hi.mip, page),
                                               slot.atlas_offset,
                                               hi.mip.page_resolution,
                                               hi_offset,
                                               hi_size});
                    resident_slots_.push_back(&slot);
                    resident_page_owners_.push_back(math::uvec2(a, c));
                    page_table_.push_back(math::vec4(float(slot.atlas_offset.x),
                                                     float(slot.atlas_offset.y),
                                                     float(hi.mip.res_level.x),
                                                     float(hi.mip.res_level.y)));
                    page_entry_slots_.push_back(&slot);
                    ++hi_res_page_count_;
                    continue;
                }
                // An unmapped page reads the locked page covering it, in that page's own level (UE's page entries
                // may point at another mip; LumenSurfaceCacheSampling.ush recomputes the page from the entry).
                const math::uvec2 coord(page % hi_size.x, page / hi_size.x);
                const math::uvec2 locked_coord = coord * locked_size / hi_size;
                const auto& locked_slot = state.slots[locked_coord.x + locked_coord.y * locked_size.x];
                page_table_.push_back(math::vec4(float(locked_slot.atlas_offset.x),
                                                 float(locked_slot.atlas_offset.y),
                                                 float(state.mip.res_level.x),
                                                 float(state.mip.res_level.y)));
                page_entry_slots_.push_back(nullptr);
            }
        }
    }
}

void lumen_scene::get_page_lighting_ages(std::vector<math::vec4>& out) const
{
    out.resize(page_entry_slots_.size());
    for(size_t i = 0; i < page_entry_slots_.size(); ++i)
    {
        // A hi-res entry falling back to a locked page has no lighting of its own: it reads as never lit.
        const physical_slot* slot = page_entry_slots_[i];
        const uint64_t direct = slot != nullptr ? slot->lit_frame[lighting_direct] : 0u;
        const uint64_t indirect = slot != nullptr ? slot->lit_frame[lighting_radiosity] : 0u;
        out[i] = math::vec4(float(frame_ - direct), float(frame_ - indirect), 0.0f, 0.0f);
    }
}

void lumen_scene::get_page_radiosity_indices(std::vector<float>& out) const
{
    out.resize(page_entry_slots_.size());
    for(size_t i = 0; i < page_entry_slots_.size(); ++i)
    {
        // A slot counts its updates from its mapping; a fallback entry lies in another mip's probe grid.
        const physical_slot* slot = page_entry_slots_[i];
        out[i] = slot != nullptr ? float(slot->update_count[lighting_radiosity]) - 1.0f : -1.0f;
    }
}

void lumen_scene::get_visualized_pages(std::vector<math::vec4>& out) const
{
    out.clear();
    out.reserve(resident_pages_.size() * 3);
    for(size_t i = 0; i < resident_pages_.size(); ++i)
    {
        const resident_page& page = resident_pages_[i];
        const uint32_t updates = resident_slots_[i]->update_count[lighting_radiosity];
        out.emplace_back(float(page.atlas_offset.x), float(page.atlas_offset.y), float(page.size.x), float(page.size.y));
        out.push_back(page.card_uv_rect);
        out.emplace_back(float(page.card_index), float(updates > 0 ? updates - 1 : 0), 0.0f, 0.0f);
    }
}

auto lumen_scene::is_visualized(const math::bbox& local_bounds,
                                const math::mat4& local_to_world,
                                const math::vec3& view_origin,
                                float distance,
                                const math::frustum& view_frustum) -> bool
{
    math::bbox world_bounds;
    world_bounds.reset();
    for(uint32_t corner = 0; corner < 8; ++corner)
    {
        const math::vec3 point((corner & 1u) != 0u ? local_bounds.max.x : local_bounds.min.x,
                               (corner & 2u) != 0u ? local_bounds.max.y : local_bounds.min.y,
                               (corner & 4u) != 0u ? local_bounds.max.z : local_bounds.min.z);
        world_bounds.add_point(math::vec3(local_to_world * math::vec4(point, 1.0f)));
    }
    const math::vec3 offset = world_bounds.closest_point(view_origin) - view_origin;
    return math::dot(offset, offset) < distance * distance && view_frustum.test_aabb(world_bounds);
}

void lumen_scene::get_visualized_cards(const std::vector<source>& sources,
                                       const math::vec3& view_origin,
                                       float distance,
                                       const math::frustum& view_frustum,
                                       std::vector<visualized_card>& out) const
{
    out.clear();
    for(uint32_t s : active_)
    {
        if(s >= sources.size())
        {
            continue;
        }
        const source& src = sources[s];
        const placement& entry = *source_placements_[s];
        if(!entry.cards || !is_visualized(entry.cards->bounds, src.local_to_world, view_origin, distance, view_frustum))
        {
            continue;
        }
        const bool is_placed = entry.has_placed && is_same_transform(entry.placed_transform, src.local_to_world);
        for(uint32_t c = 0; c < uint32_t(entry.card_states.size()); ++c)
        {
            // UE draws the cards that hold an allocation (FLumenCardCullingInfo::bVisible).
            if(entry.card_states[c].res_level == 0)
            {
                continue;
            }
            const lumen_card& card = entry.cards->cards[c];
            visualized_card result;
            result.box = is_placed ? entry.placed[c] : place_card(card, src.local_to_world);
            result.index_in_mesh = c;
            result.source_index = s;
            result.direction = card.direction;
            const std::array<float, 9> key = {card.origin.x,
                                              card.origin.y,
                                              card.origin.z,
                                              card.extent.x,
                                              card.extent.y,
                                              card.extent.z,
                                              card.axis_z.x,
                                              card.axis_z.y,
                                              card.axis_z.z};
            uint32_t hash = 2166136261u;
            const auto* bytes = reinterpret_cast<const uint8_t*>(key.data());
            for(size_t i = 0; i < sizeof(key); ++i)
            {
                hash = (hash ^ bytes[i]) * 16777619u;
            }
            result.hash = (hash ^ (first_card_[s] + c)) * 16777619u;
            out.push_back(result);
        }
    }
}

void lumen_scene::refresh_captures(const std::vector<source>& sources, capture_packer& packer)
{
    APP_SCOPE_PERF("GI/Scene Update/Refresh");
    const float fraction = std::clamp(settings_.card_capture_refresh_fraction, 0.0f, 1.0f);
    const uint32_t captured = uint32_t(captures_.size());
    if(fraction <= 0.0f || captured >= settings_.max_captures_per_frame)
    {
        return;
    }
    // The refresh's share of the budget: at least one page, and at least one full page of texels
    // (UE GetCardCaptureRefreshNumPages / GetCardCaptureRefreshNumTexels).
    const uint32_t budget_pages =
        std::clamp(uint32_t(float(settings_.max_captures_per_frame) * fraction), 1u, settings_.max_captures_per_frame);
    const uint32_t pages_left = std::min(budget_pages, settings_.max_captures_per_frame - captured);
    const float capture_texels = float(settings_.capture_atlas_size) * float(settings_.capture_atlas_size);
    int64_t texels_left = int64_t(std::max(capture_texels * fraction, float(physical_page_size * physical_page_size)));
    // The pages captured longest ago first; none captured this frame.
    refresh_candidates_.clear();
    for(uint32_t i = 0; i < uint32_t(resident_slots_.size()); ++i)
    {
        if(resident_slots_[i]->captured_frame != frame_)
        {
            refresh_candidates_.push_back(i);
        }
    }
    const size_t count = std::min<size_t>(pages_left, refresh_candidates_.size());
    std::partial_sort(refresh_candidates_.begin(),
                      refresh_candidates_.begin() + std::ptrdiff_t(count),
                      refresh_candidates_.end(),
                      [this](uint32_t a, uint32_t b)
                      {
                          const uint64_t frame_a = resident_slots_[a]->captured_frame;
                          const uint64_t frame_b = resident_slots_[b]->captured_frame;
                          return frame_a != frame_b ? frame_a < frame_b : a < b;
                      });
    refresh_resamples_.clear();
    for(size_t k = 0; k < count; ++k)
    {
        const uint32_t i = refresh_candidates_[k];
        const resident_page& page = resident_pages_[i];
        texels_left -= int64_t(page.size.x) * int64_t(page.size.y);
        math::uvec2 capture_offset(0u);
        if(texels_left < 0 || !packer.place(page.size, capture_offset))
        {
            break;
        }
        // The page keeps its lighting: its card's own allocation is the resample source (UE bResampleLastLighting).
        auto resample = std::find_if(refresh_resamples_.begin(),
                                     refresh_resamples_.end(),
                                     [&](const std::pair<uint32_t, int32_t>& entry)
                                     {
                                         return entry.first == page.card_index;
                                     });
        if(resample == refresh_resamples_.end())
        {
            const math::uvec2 owner = resident_page_owners_[i];
            const uint32_t s = active_[owner.x];
            const source& src = sources[s];
            placement& entry = *source_placements_[s];
            const bool is_placed = entry.has_placed && is_same_transform(entry.placed_transform, src.local_to_world);
            const int32_t index = append_resample_source(entry.card_states[owner.y],
                                                         is_placed ? entry.placed[owner.y]
                                                                   : place_card(entry.cards->cards[owner.y],
                                                                                src.local_to_world));
            resample = refresh_resamples_.insert(refresh_resamples_.end(), {page.card_index, index});
        }
        capture cap;
        cap.card_index = page.card_index;
        cap.source_index = active_[resident_page_owners_[i].x];
        cap.card_uv_rect = page.card_uv_rect;
        cap.capture_offset = capture_offset;
        cap.atlas_offset = page.atlas_offset;
        cap.size = page.size;
        cap.resample_card = resample->second;
        cap.keeps_lighting = true;
        captures_.push_back(cap);
        // A periodic refresh captures the same geometry (its lighting is resampled in place) and the direct lighting
        // reads only the capture's normal and depth; a forced one (a material change, captured_frame 0) may change them.
        resident_slots_[i]->is_direct_dirty = resident_slots_[i]->is_direct_dirty || resident_slots_[i]->captured_frame == 0;
        resident_slots_[i]->captured_frame = frame_;
        ++stats_.refreshed;
    }
}

auto lumen_scene::compute_lighting_tile_budget(uint32_t atlas_size, uint32_t update_factor) -> uint32_t
{
    const uint32_t side_texels =
        uint32_t(float(atlas_size) / std::sqrt(float(std::max(update_factor, 1u))) + 0.5f);
    const uint32_t side =
        std::max((side_texels + k_lighting_tile_size - 1u) / k_lighting_tile_size * k_lighting_tile_size,
                 physical_page_size);
    return (side / k_lighting_tile_size) * (side / k_lighting_tile_size);
}

auto lumen_scene::compute_lighting_bucket(uint32_t frames_since_update, float speed) -> uint32_t
{
    const float urgency = std::max(4.0f * float(frames_since_update) * speed, 1.0f);
    return uint32_t(float(k_lighting_buckets - 1u) - std::clamp(std::log2(urgency), 0.0f, float(k_lighting_buckets - 1u)));
}

auto lumen_scene::compute_page_bounds(uint32_t page_index) const -> math::bbox
{
    const resident_page& page = resident_pages_[page_index];
    const size_t base = size_t(page.card_index) * card_stride;
    const math::vec3 origin(card_table_[base + 0]);
    const math::vec3 axis_x(card_table_[base + 1]);
    const math::vec3 axis_y(card_table_[base + 2]);
    const math::vec3 axis_z(card_table_[base + 3]);
    const math::vec3 extent(card_table_[base + 1].w, card_table_[base + 2].w, card_table_[base + 3].w);
    // The page's box in card space: its UV rectangle across the card's face (v runs down -axis_y), full depth.
    const math::vec2 corner_min((2.0f * page.card_uv_rect.x - 1.0f) * extent.x,
                                (1.0f - 2.0f * page.card_uv_rect.w) * extent.y);
    const math::vec2 corner_max((2.0f * page.card_uv_rect.z - 1.0f) * extent.x,
                                (1.0f - 2.0f * page.card_uv_rect.y) * extent.y);
    const math::vec2 center = 0.5f * (corner_min + corner_max);
    const math::vec3 half(0.5f * (corner_max - corner_min), extent.z);
    const math::vec3 world_center = origin + axis_x * center.x + axis_y * center.y;
    const math::vec3 world_half =
        math::abs(axis_x) * half.x + math::abs(axis_y) * half.y + math::abs(axis_z) * half.z;
    return math::bbox(world_center - world_half, world_center + world_half);
}

void lumen_scene::invalidate_direct_lighting(const direct_lighting_changes& changes)
{
    APP_SCOPE_PERF("GI/Lighting Invalidate");
    const uint32_t page_count = uint32_t(resident_pages_.size());
    const bool has_changes = changes.all || !changes.regions.empty() || !changes.occluders.empty();
    if(!has_changes)
    {
        return;
    }
    poolstl::for_each_par_if(
        page_count >= k_parallel_invalidation_pages,
        poolstl::iota_iter<uint32_t>(0),
        poolstl::iota_iter<uint32_t>(page_count),
        [&](uint32_t i)
        {
            physical_slot& slot = *resident_slots_[i];
            if(!slot.is_direct_dirty)
            {
                const math::bbox bounds = compute_page_bounds(i);
                bool is_changed = changes.all;
                for(size_t r = 0; r < changes.regions.size() && !is_changed; ++r)
                {
                    is_changed = changes.regions[r].intersect(bounds);
                }
                for(size_t o = 0; o < changes.occluders.size() && !is_changed; ++o)
                {
                    for(size_t l = 0; l < changes.lights.size() && !is_changed; ++l)
                    {
                        is_changed = is_between(changes.occluders[o], bounds, changes.lights[l]);
                    }
                }
                slot.is_direct_dirty = is_changed;
            }
        });
}

void lumen_scene::compute_page_priorities(const std::vector<viewer>& viewers)
{
    const uint32_t page_count = uint32_t(resident_pages_.size());
    for(auto& buckets : page_buckets_)
    {
        buckets.resize(page_count);
    }
    // A page's speed and nearest viewer are functions of its box (the tables) and the viewers alone: a still view of
    // still cards keeps them, and only the pages' ages move their buckets.
    const bool are_speeds_current = priority_tables_revision_ == tables_revision_ &&
                                    page_speeds_.size() == page_count && priority_viewers_.size() == viewers.size() &&
                                    std::equal(viewers.begin(),
                                               viewers.end(),
                                               priority_viewers_.begin(),
                                               [](const viewer& a, const viewer& b)
                                               {
                                                   return a.origin == b.origin && a.frustum == b.frustum;
                                               });
    if(!are_speeds_current)
    {
        compute_page_speeds(viewers);
    }
    // A page reads only its own slot: the pages spread over the pool. The buckets of both contexts are taken before
    // either admits a page, which stamps only its own context's frame.
    poolstl::for_each_par_if(
        page_count >= k_parallel_bucket_pages,
        poolstl::iota_iter<uint32_t>(0),
        poolstl::iota_iter<uint32_t>(page_count),
        [&](uint32_t i)
        {
            const float page_speed = page_speeds_[i];
            for(uint32_t context = 0; context < lighting_context_count; ++context)
            {
                const physical_slot& slot = *resident_slots_[i];
                const uint64_t lit_frame = slot.lit_frame[context];
                const uint32_t age = lit_frame == 0 ? uint32_t(gi::lumen::LUMEN_SCENE_LIGHTING_NEVER_LIT_FRAMES)
                                                    : uint32_t(std::min<uint64_t>(frame_ - lit_frame, UINT32_MAX));
                const bool is_skipped = context == lighting_direct && lit_frame != 0 && !slot.is_direct_dirty;
                page_buckets_[context][i] = is_skipped ? k_skip_bucket : compute_lighting_bucket(age, page_speed);
            }
        });
}

void lumen_scene::compute_page_speeds(const std::vector<viewer>& viewers)
{
    const uint32_t page_count = uint32_t(resident_pages_.size());
    page_tiles_.resize(page_count);
    page_viewers_.resize(page_count);
    page_speeds_.resize(page_count);
    // A page reads only its own card: the pages spread over the pool.
    poolstl::for_each_par_if(
        page_count >= k_parallel_speed_pages,
        poolstl::iota_iter<uint32_t>(0),
        poolstl::iota_iter<uint32_t>(page_count),
        [&](uint32_t i)
        {
            const resident_page& page = resident_pages_[i];
            const size_t base = size_t(page.card_index) * card_stride;
            const math::vec3 origin(card_table_[base + 0]);
            const math::vec3 axis_x(card_table_[base + 1]);
            const math::vec3 axis_y(card_table_[base + 2]);
            const math::vec3 axis_z(card_table_[base + 3]);
            const math::vec3 extent(card_table_[base + 1].w, card_table_[base + 2].w, card_table_[base + 3].w);
            // The page's box in card space: its UV rectangle across the card's face (v runs down -axis_y), full depth.
            const math::vec2 corner_min((2.0f * page.card_uv_rect.x - 1.0f) * extent.x,
                                        (1.0f - 2.0f * page.card_uv_rect.w) * extent.y);
            const math::vec2 corner_max((2.0f * page.card_uv_rect.z - 1.0f) * extent.x,
                                        (1.0f - 2.0f * page.card_uv_rect.y) * extent.y);
            const math::vec3 center(0.5f * (corner_min + corner_max), 0.0f);
            const math::vec3 half(0.5f * (corner_max - corner_min), extent.z);
            const math::vec3 world_center = origin + axis_x * center.x + axis_y * center.y;
            float distance = std::numeric_limits<float>::max();
            uint32_t nearest_viewer = 0;
            bool is_near_frustum = false;
            for(uint32_t v = 0; v < uint32_t(viewers.size()); ++v)
            {
                const viewer& viewer = viewers[v];
                const math::vec3 to_view = viewer.origin - origin;
                const math::vec3 local(math::dot(to_view, axis_x),
                                       math::dot(to_view, axis_y),
                                       math::dot(to_view, axis_z));
                const math::vec3 outside = math::max(math::abs(local - center) - half, math::vec3(0.0f));
                const float viewer_distance = math::length(outside);
                nearest_viewer = viewer_distance < distance ? v : nearest_viewer;
                distance = std::min(distance, viewer_distance);
                bool is_near_this = true;
                for(uint32_t p = 0; p < k_priority_frustum_planes; ++p)
                {
                    const math::plane& plane = viewer.frustum.planes[p];
                    const math::vec3 normal(plane.data);
                    const float radius = std::abs(math::dot(axis_x, normal)) * half.x +
                                         std::abs(math::dot(axis_y, normal)) * half.y +
                                         std::abs(math::dot(axis_z, normal)) * half.z;
                    is_near_this = is_near_this && math::plane::dot_coord(plane, world_center) <=
                                                       radius + gi::lumen::LUMEN_SCENE_LIGHTING_FRUSTUM_MARGIN;
                }
                is_near_frustum = is_near_frustum || is_near_this;
            }
            page_viewers_[i] = nearest_viewer;
            const float speed = 1.0f / (1.0f + distance / gi::lumen::LUMEN_SCENE_LIGHTING_PRIORITY_DISTANCE);
            page_speeds_[i] = is_near_frustum ? 2.0f * speed : speed;
            page_tiles_[i] = (page.size.x / k_lighting_tile_size) * (page.size.y / k_lighting_tile_size);
        });
    priority_tables_revision_ = tables_revision_;
    priority_viewers_ = viewers;
}

void lumen_scene::schedule_lighting(const math::vec3& view_origin, const math::frustum& view_frustum)
{
    viewer single;
    single.origin = view_origin;
    single.frustum = view_frustum;
    schedule_lighting(std::vector<viewer>{single});
}

void lumen_scene::revoke_lighting(uint32_t viewer)
{
    for(uint32_t context = 0; context < lighting_context_count; ++context)
    {
        for(const lit_page& lit : lit_pages_[context])
        {
            if(lit.viewer != viewer || lit.resident_page >= resident_slots_.size())
            {
                continue;
            }
            physical_slot& slot = *resident_slots_[lit.resident_page];
            slot.lit_frame[context] = lit.previous_lit_frame;
            slot.is_direct_dirty = slot.is_direct_dirty || context == lighting_direct;
        }
    }
}

void lumen_scene::schedule_lighting(const std::vector<viewer>& viewers)
{
    APP_SCOPE_PERF("GI/Lighting Schedule");
    compute_page_priorities(viewers);
    const uint32_t page_count = uint32_t(resident_pages_.size());
    const uint32_t atlas_size = settings_.atlas_size;
    const std::array<uint32_t, lighting_context_count> budgets{
        compute_lighting_tile_budget(atlas_size, get_lighting_update_factor(lighting_direct)),
        compute_lighting_tile_budget(atlas_size, get_lighting_update_factor(lighting_radiosity))};
    for(uint32_t context = 0; context < lighting_context_count; ++context)
    {
        const auto& buckets = page_buckets_[context];
        std::array<uint32_t, k_lighting_buckets> histogram{};
        uint32_t due_pages = 0;
        for(uint32_t i = 0; i < page_count; ++i)
        {
            if(buckets[i] != k_skip_bucket)
            {
                histogram[buckets[i]] += page_tiles_[i];
                ++due_pages;
            }
        }
        if(context == lighting_direct)
        {
            stats_.direct_dirty_pages = due_pages;
        }
        // The bucket the budget runs out in: every page in a more urgent one fits, this one only partly.
        const uint32_t budget = budgets[context];
        uint32_t cut_bucket = k_lighting_buckets;
        uint32_t cut_tiles = budget;
        uint32_t sum = 0;
        for(uint32_t b = 0; b < k_lighting_buckets; ++b)
        {
            if(sum + histogram[b] >= budget)
            {
                cut_bucket = b;
                cut_tiles = budget - sum;
                break;
            }
            sum += histogram[b];
        }
        auto& lit = lit_pages_[context];
        lit.clear();
        uint32_t cut_allocated = 0;
        uint32_t allocated = 0;
        for(uint32_t i = 0; i < page_count; ++i)
        {
            bool admit = buckets[i] != k_skip_bucket && buckets[i] <= cut_bucket;
            if(admit && buckets[i] == cut_bucket)
            {
                admit = cut_allocated + page_tiles_[i] <= cut_tiles;
                cut_allocated += page_tiles_[i];
            }
            if(admit)
            {
                admit = allocated + page_tiles_[i] <= budget;
                allocated += page_tiles_[i];
            }
            if(!admit)
            {
                continue;
            }
            physical_slot& slot = *resident_slots_[i];
            lit.push_back({i, slot.update_count[context], page_viewers_[i], slot.lit_frame[context]});
            slot.lit_frame[context] = frame_;
            slot.is_direct_dirty = slot.is_direct_dirty && context != lighting_direct;
            ++slot.update_count[context];
            stats_.lit_tiles[context] += page_tiles_[i];
        }
        stats_.cut_bucket[context] = cut_bucket;
    }
}

} // namespace unravel
