#include "lumen_scene.h"

#include <engine/profiler/profiler.h>
#include <engine/rendering/gi/lumen_constants.h>

#include <algorithm>
#include <cmath>
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
/// GetMeshCardDistanceBin: bins start 10 m out, log2 of centimetres.
constexpr float k_distance_bin_offset = 10.0f;
/// A reallocation ranks as if this much farther away than a new card when its level moves by one.
constexpr float k_realloc_distance_penalty = 25.0f;
/// Slots per sub-allocated page are tracked in 64-bit words (at most 16 x 16 slots of 8 x 8 texels).
constexpr uint32_t k_slot_words = 4;
/// The card lighting's tile edge (the lighting kernels' 8 x 8 groups).
constexpr uint32_t k_lighting_tile_size = 8;
/// Priority buckets of the lighting scheduler.
constexpr uint32_t k_lighting_buckets = 16;
/// The frustum planes the lighting priority tests: left, right, top, bottom and near (not far).
constexpr uint32_t k_priority_frustum_planes = 5;
/// The lighting update speed's range (R/LumenSceneLighting.cpp:565).
constexpr float k_min_lighting_update_speed = 0.5f;
constexpr float k_max_lighting_update_speed = 16.0f;
/// The surface cache resolution's range (UE FPostProcessSettings::LumenSurfaceCacheResolution).
constexpr float k_min_surface_cache_resolution = 0.5f;
constexpr float k_max_surface_cache_resolution = 1.0f;
/// The largest minimum card resolution (LumenScene::GetCardMinResolution).
constexpr uint32_t k_max_card_min_resolution = 1024;

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

/// A card's box placed in the world: unit axes and world half extents.
struct world_card
{
    math::vec3 origin{0.0f};
    math::vec3 axis_x{1.0f, 0.0f, 0.0f};
    math::vec3 axis_y{0.0f, 1.0f, 0.0f};
    math::vec3 axis_z{0.0f, 0.0f, 1.0f};
    math::vec3 extent{0.0f};
};

auto place_card(const lumen_card& card, const math::mat4& local_to_world) -> world_card
{
    const math::mat3 linear(local_to_world);
    world_card placed;
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

auto distance_to_card(const world_card& card, const math::vec3& point) -> float
{
    const math::vec3 d = point - card.origin;
    const math::vec3 local(math::dot(d, card.axis_x), math::dot(d, card.axis_y), math::dot(d, card.axis_z));
    return math::length(math::max(math::abs(local) - card.extent, math::vec3(0.0f)));
}

/// Appends a placed card's record (lumen_scene::get_card_table): its box, then @p mip_entry = (size in pages x, y,
/// res level x, y).
void append_card_record(std::vector<math::vec4>& table,
                        const world_card& placed,
                        uint32_t page_offset,
                        const math::vec4& mip_entry)
{
    table.push_back(math::vec4(placed.origin, float(page_offset)));
    table.push_back(math::vec4(placed.axis_x, placed.extent.x));
    table.push_back(math::vec4(placed.axis_y, placed.extent.y));
    table.push_back(math::vec4(placed.axis_z, placed.extent.z));
    table.push_back(mip_entry);
}

/// Places pages on shelves in the capture atlas for one frame.
class shelf_packer
{
public:
    explicit shelf_packer(uint32_t size)
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

struct request
{
    uint64_t identity = 0;
    ///< Index into the update's sources.
    uint32_t source = 0;
    uint32_t card = 0;
    uint32_t bin = 0;
    uint32_t order = 0;
};

auto has_free_slot(const std::vector<uint64_t>& used, uint32_t slot_count) -> bool
{
    for(uint32_t slot = 0; slot < slot_count; ++slot)
    {
        if((used[slot / 64u] & (1ull << (slot % 64u))) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

void lumen_scene::init(const settings& s)
{
    settings_ = s;
    reset();
}

auto lumen_scene::get_max_card_distance() const -> float
{
    return std::clamp(view_settings_.view_distance, 0.0f, settings_.max_card_distance);
}

auto lumen_scene::get_lighting_update_factor(lighting_context context) const -> uint32_t
{
    const float speed =
        std::clamp(view_settings_.lighting_update_speed, k_min_lighting_update_speed, k_max_lighting_update_speed);
    const float factor = context == lighting_direct ? float(gi::lumen::LUMEN_SCENE_DIRECT_UPDATE_FACTOR)
                                                    : float(gi::lumen::LUMEN_SCENE_RADIOSITY_UPDATE_FACTOR);
    return uint32_t(std::lround(factor / speed));
}

auto lumen_scene::get_resolution_rule() const -> resolution_rule
{
    const float scale = std::clamp(view_settings_.surface_cache_resolution,
                                   k_min_surface_cache_resolution,
                                   k_max_surface_cache_resolution);
    resolution_rule rule;
    rule.texel_density_scale = settings_.texel_density_scale * scale;
    rule.max_resolution = uint32_t(std::max(1l, std::lround(float(settings_.card_max_resolution) * scale)));
    rule.min_resolution = uint32_t(
        std::clamp(std::lround(float(settings_.card_min_resolution) * scale), 1l, long(k_max_card_min_resolution)));
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
    placements_.clear();
    captures_.clear();
    card_table_.clear();
    page_table_.clear();
    instance_table_.clear();
    resample_cards_.clear();
    resample_pages_.clear();
    resample_table_.clear();
    resample_page_base_ = 0;
    resident_slots_.clear();
    for(auto& pages : lit_pages_)
    {
        pages.clear();
    }
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
    const math::uvec2 slots(physical_page_size / mip.page_resolution.x, physical_page_size / mip.page_resolution.y);
    for(const auto& bin : bins_)
    {
        if(bin.element_size != mip.page_resolution)
        {
            continue;
        }
        for(const auto& used : bin.used)
        {
            if(has_free_slot(used, slots.x * slots.y))
            {
                return true;
            }
        }
    }
    return false;
}

auto lumen_scene::allocate_slot(const math::uvec2& element_size, physical_slot& out) -> bool
{
    auto bin_it = std::find_if(bins_.begin(),
                               bins_.end(),
                               [&](const sub_allocation_bin& bin)
                               {
                                   return bin.element_size == element_size;
                               });
    if(bin_it == bins_.end())
    {
        bins_.push_back({element_size, {}, {}});
        bin_it = bins_.end() - 1;
    }
    const math::uvec2 slots(physical_page_size / element_size.x, physical_page_size / element_size.y);
    const uint32_t slot_count = slots.x * slots.y;
    for(size_t p = 0; p < bin_it->pages.size(); ++p)
    {
        auto& used = bin_it->used[p];
        for(uint32_t slot = 0; slot < slot_count; ++slot)
        {
            uint64_t& word = used[slot / 64u];
            const uint64_t bit = 1ull << (slot % 64u);
            if((word & bit) == 0)
            {
                word |= bit;
                out.page = bin_it->pages[p];
                out.slot = slot;
                out.atlas_offset = get_page_origin(out.page) + math::uvec2(slot % slots.x, slot / slots.x) * element_size;
                return true;
            }
        }
    }
    if(free_pages_.empty())
    {
        return false;
    }
    const uint32_t page = free_pages_.back();
    free_pages_.pop_back();
    bin_it->pages.push_back(page);
    bin_it->used.push_back(std::vector<uint64_t>(k_slot_words, 0ull));
    bin_it->used.back()[0] = 1ull;
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
    for(auto& bin : bins_)
    {
        if(bin.element_size != mip.page_resolution)
        {
            continue;
        }
        for(size_t p = 0; p < bin.pages.size(); ++p)
        {
            if(bin.pages[p] != slot.page)
            {
                continue;
            }
            bin.used[p][slot.slot / 64u] &= ~(1ull << (slot.slot % 64u));
            const bool empty = std::all_of(bin.used[p].begin(),
                                           bin.used[p].end(),
                                           [](uint64_t word)
                                           {
                                               return word == 0;
                                           });
            if(empty)
            {
                free_pages_.push_back(bin.pages[p]);
                bin.pages.erase(bin.pages.begin() + std::ptrdiff_t(p));
                bin.used.erase(bin.used.begin() + std::ptrdiff_t(p));
            }
            return;
        }
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
}

auto lumen_scene::append_resample_source(const card_state& card,
                                         const lumen_card& card_desc,
                                         const math::mat4& local_to_world) -> int32_t
{
    const int32_t index = int32_t(resample_cards_.size() / card_stride);
    // The card's box as placed this frame: the resample maps the new pages' card UV onto the previous mip, assuming
    // the card's extent has not changed (UE ResampleLightingHistoryToCardCaptureAtlasCS).
    append_card_record(resample_cards_,
                       place_card(card_desc, local_to_world),
                       uint32_t(resample_pages_.size()),
                       math::vec4(float(card.mip.size_in_pages.x),
                                  float(card.mip.size_in_pages.y),
                                  float(card.mip.res_level.x),
                                  float(card.mip.res_level.y)));
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
    APP_SCOPE_PERF("GI/Lumen/Scene Update");
    ++frame_;
    stats_ = {};
    // Placements: create, refresh when their card set changed, drop the ones not seen this frame.
    for(const auto& src : sources)
    {
        auto& entry = placements_[src.identity];
        if(entry.cards != src.cards)
        {
            for(auto& card : entry.card_states)
            {
                free_card(card);
            }
            entry.cards = src.cards;
            entry.card_states.assign(src.cards ? src.cards->cards.size() : 0u, card_state{});
        }
        entry.last_seen = frame_;
    }
    for(auto it = placements_.begin(); it != placements_.end();)
    {
        if(it->second.last_seen != frame_)
        {
            for(auto& card : it->second.card_states)
            {
                free_card(card);
            }
            it = placements_.erase(it);
            continue;
        }
        ++it;
    }
    // The placements this frame serves: built cards and a valid instance. Every loop below walks
    // this list, so card indices agree between the captures and the packed table.
    std::vector<uint32_t> active;
    // Each active placement's first card in this frame's packed card table.
    std::vector<uint32_t> first_card(sources.size(), 0u);
    uint32_t card_total = 0;
    for(uint32_t s = 0; s < uint32_t(sources.size()); ++s)
    {
        if(sources[s].cards && sources[s].instance_index < instance_count)
        {
            active.push_back(s);
            first_card[s] = card_total;
            card_total += uint32_t(placements_[sources[s].identity].card_states.size());
        }
    }
    // Resolution per card and the requests for every card whose resolution moved.
    std::vector<request> requests;
    const resolution_rule rule = get_resolution_rule();
    const float max_card_distance = get_max_card_distance();
    for(uint32_t s : active)
    {
        const auto& src = sources[s];
        auto& entry = placements_[src.identity];
        for(uint32_t c = 0; c < uint32_t(entry.card_states.size()); ++c)
        {
            card_state& state = entry.card_states[c];
            const world_card placed = place_card(entry.cards->cards[c], src.local_to_world);
            const float distance = std::max(distance_to_card(placed, view_origin), k_min_viewer_distance);
            const float max_extent = std::max(placed.extent.x, placed.extent.y);
            const float projected = std::min(rule.texel_density_scale * max_extent / distance,
                                             settings_.max_texel_density * max_extent);
            const uint32_t truncated = std::min(uint32_t(std::max(projected, 0.0f)), rule.max_resolution);
            const uint32_t snapped = truncated == 0 ? 0u : round_up_power_of_two(truncated);
            const uint32_t min_resolution =
                src.is_emissive_light_source ? k_emissive_min_card_resolution : rule.min_resolution;
            const float min_area = settings_.mesh_cards_min_size * settings_.mesh_cards_min_size *
                                   (src.is_emissive_light_source ? k_emissive_min_card_area_scale : 1.0f);
            const bool is_large_enough = 4.0f * placed.extent.x * placed.extent.y > min_area;
            const bool visible = is_large_enough && distance < max_card_distance && snapped >= min_resolution;
            const uint32_t res_level = floor_log2(std::max(snapped, k_min_card_resolution));
            // Texels stay roughly square: the shorter axis drops a level per doubling of the aspect.
            const float aspect = placed.extent.x / std::max(placed.extent.y, 1e-6f);
            state.res_level_bias = aspect >= 1.0f
                                       ? math::uvec2(0u, std::min(floor_log2(uint32_t(std::lround(aspect))), 8u))
                                       : math::uvec2(std::min(floor_log2(uint32_t(std::lround(1.0f / aspect))), 8u), 0u);
            ++stats_.cards;
            if(!visible)
            {
                if(state.res_level != 0)
                {
                    free_card(state);
                }
                state.desired_res_level = 0;
                state.res_level_on_last_alloc = 0;
                continue;
            }
            state.desired_res_level = res_level;
            const mip_desc desired = compute_mip_desc(res_level, state.res_level_bias);
            stats_.texels_desired += (1u << desired.res_level.x) * (1u << desired.res_level.y);
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
            requests.push_back({src.identity, s, c, compute_distance_bin(ranked), uint32_t(requests.size())});
        }
    }
    // Nearest bins first, then request order; the budget cuts inside the last bin.
    std::stable_sort(requests.begin(),
                     requests.end(),
                     [](const request& a, const request& b)
                     {
                         return a.bin < b.bin;
                     });
    captures_.clear();
    resample_cards_.clear();
    resample_pages_.clear();
    shelf_packer packer(settings_.capture_atlas_size);
    uint32_t pages_requested = 0;
    for(const auto& req : requests)
    {
        if(pages_requested >= settings_.max_captures_per_frame)
        {
            break;
        }
        card_state& state = placements_[req.identity].card_states[req.card];
        // The level that fits the physical atlas beside everything resident, the card's own allocation included: a
        // locked mip never evicts another and drops levels instead.
        uint32_t level = state.desired_res_level;
        mip_desc mip = compute_mip_desc(level, state.res_level_bias);
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
        std::vector<math::uvec2> capture_offsets(page_count);
        shelf_packer trial = packer;
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
        const auto& src = sources[req.source];
        int32_t resample_card = -1;
        if(state.res_level != 0)
        {
            ++stats_.reallocated;
            resample_card = append_resample_source(state, placements_[req.identity].cards->cards[req.card], src.local_to_world);
        }
        free_card(state);
        allocate(state, level);
        // Recorded even when downgraded, so a card that only fits lower is not re-requested.
        state.res_level_on_last_alloc = state.desired_res_level;
        for(uint32_t page = 0; page < page_count; ++page)
        {
            capture cap;
            cap.card_index = first_card[req.source] + req.card;
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
    resample_page_base_ = uint32_t(resample_cards_.size());
    resample_table_ = resample_cards_;
    resample_table_.insert(resample_table_.end(), resample_pages_.begin(), resample_pages_.end());
    // Packed tables: cards, the page table (rebuilt from the physical allocations) and the
    // per-instance card ranges.
    card_table_.clear();
    page_table_.clear();
    resident_pages_.clear();
    resident_slots_.clear();
    instance_table_.assign(size_t(instance_count) * instance_stride, math::vec4(0.0f));
    for(uint32_t s : active)
    {
        const auto& src = sources[s];
        auto& entry = placements_[src.identity];
        const uint32_t first_card = uint32_t(card_table_.size() / card_stride);
        instance_table_[src.instance_index] = math::vec4(float(first_card),
                                                         float(entry.card_states.size()),
                                                         entry.cards->is_mostly_two_sided ? 1.0f : 0.0f,
                                                         0.0f);
        for(uint32_t c = 0; c < uint32_t(entry.card_states.size()); ++c)
        {
            card_state& state = entry.card_states[c];
            const bool resident = state.res_level != 0;
            append_card_record(card_table_,
                               place_card(entry.cards->cards[c], src.local_to_world),
                               uint32_t(page_table_.size()),
                               resident ? math::vec4(float(state.mip.size_in_pages.x),
                                                     float(state.mip.size_in_pages.y),
                                                     float(state.mip.res_level.x),
                                                     float(state.mip.res_level.y))
                                        : math::vec4(1.0f, 1.0f, 0.0f, 0.0f));
            if(!resident)
            {
                continue;
            }
            ++stats_.resident_cards;
            const uint32_t card_index = uint32_t(card_table_.size() / card_stride) - 1u;
            for(uint32_t page = 0; page < uint32_t(state.slots.size()); ++page)
            {
                auto& slot = state.slots[page];
                resident_pages_.push_back(
                    {card_index, compute_page_uv_rect(state.mip, page), slot.atlas_offset, state.mip.page_resolution});
                resident_slots_.push_back(&slot);
                page_table_.push_back(math::vec4(float(slot.atlas_offset.x),
                                                 float(slot.atlas_offset.y),
                                                 float(state.mip.res_level.x),
                                                 float(state.mip.res_level.y)));
            }
        }
    }
    stats_.pages_total = pages_per_side_ * pages_per_side_;
    stats_.pages_used = stats_.pages_total - uint32_t(free_pages_.size());
    stats_.captures = uint32_t(captures_.size());
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

void lumen_scene::schedule_lighting(const math::vec3& view_origin, const math::frustum& view_frustum)
{
    APP_SCOPE_PERF("GI/Lumen/Lighting Schedule");
    const uint32_t page_count = uint32_t(resident_pages_.size());
    std::vector<float> speeds(page_count);
    std::vector<uint32_t> tiles(page_count);
    for(uint32_t i = 0; i < page_count; ++i)
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
        const math::vec3 to_view = view_origin - origin;
        const math::vec3 local(math::dot(to_view, axis_x), math::dot(to_view, axis_y), math::dot(to_view, axis_z));
        const float distance = math::length(math::max(math::abs(local - center) - half, math::vec3(0.0f)));
        float speed = 1.0f / (1.0f + distance / gi::lumen::LUMEN_SCENE_LIGHTING_PRIORITY_DISTANCE);
        const math::vec3 world_center = origin + axis_x * center.x + axis_y * center.y;
        bool is_near_frustum = true;
        for(uint32_t p = 0; p < k_priority_frustum_planes; ++p)
        {
            const math::plane& plane = view_frustum.planes[p];
            const math::vec3 normal(plane.data);
            const float radius = std::abs(math::dot(axis_x, normal)) * half.x +
                                 std::abs(math::dot(axis_y, normal)) * half.y +
                                 std::abs(math::dot(axis_z, normal)) * half.z;
            is_near_frustum = is_near_frustum && math::plane::dot_coord(plane, world_center) <=
                                                     radius + gi::lumen::LUMEN_SCENE_LIGHTING_FRUSTUM_MARGIN;
        }
        speeds[i] = is_near_frustum ? 2.0f * speed : speed;
        tiles[i] = (page.size.x / k_lighting_tile_size) * (page.size.y / k_lighting_tile_size);
    }
    const uint32_t atlas_size = settings_.atlas_size;
    const std::array<uint32_t, lighting_context_count> budgets{
        compute_lighting_tile_budget(atlas_size, get_lighting_update_factor(lighting_direct)),
        compute_lighting_tile_budget(atlas_size, get_lighting_update_factor(lighting_radiosity))};
    std::vector<uint32_t> buckets(page_count);
    for(uint32_t context = 0; context < lighting_context_count; ++context)
    {
        std::array<uint32_t, k_lighting_buckets> histogram{};
        for(uint32_t i = 0; i < page_count; ++i)
        {
            const uint64_t lit_frame = resident_slots_[i]->lit_frame[context];
            const uint32_t age = lit_frame == 0 ? uint32_t(gi::lumen::LUMEN_SCENE_LIGHTING_NEVER_LIT_FRAMES)
                                                : uint32_t(std::min<uint64_t>(frame_ - lit_frame, UINT32_MAX));
            buckets[i] = compute_lighting_bucket(age, speeds[i]);
            histogram[buckets[i]] += tiles[i];
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
            bool admit = buckets[i] <= cut_bucket;
            if(admit && buckets[i] == cut_bucket)
            {
                admit = cut_allocated + tiles[i] <= cut_tiles;
                cut_allocated += tiles[i];
            }
            if(admit)
            {
                admit = allocated + tiles[i] <= budget;
                allocated += tiles[i];
            }
            if(!admit)
            {
                continue;
            }
            physical_slot& slot = *resident_slots_[i];
            lit.push_back({i, slot.update_count[context]});
            slot.lit_frame[context] = frame_;
            ++slot.update_count[context];
            stats_.lit_tiles[context] += tiles[i];
        }
        stats_.cut_bucket[context] = cut_bucket;
    }
}

} // namespace unravel
