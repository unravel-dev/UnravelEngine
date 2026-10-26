#include "shadow_atlas.h"

#include <graphics/graphics.h>

#include <algorithm>

namespace unravel
{
namespace shadow
{

namespace
{
/// Atlas edges from the smallest up: every step quadruples the area.
constexpr std::uint16_t ATLAS_SIZES[] = {512, 1024, 2048, 4096};
/// Render frames without a light asking for the static copy before it is freed.
constexpr std::uint64_t STATIC_COPY_IDLE_FRAMES = 300;

auto get_area(std::uint16_t edge) -> std::uint64_t
{
    return std::uint64_t(edge) * std::uint64_t(edge);
}

/// @p size rounded down to a power of two inside the tile limits.
auto clamp_tile_size(std::uint16_t size) -> std::uint16_t
{
    std::uint16_t edge = shadow_atlas_allocator::min_tile_size;
    while(edge < shadow_atlas_allocator::max_atlas_size && std::uint32_t(edge) * 2u <= size)
    {
        edge = std::uint16_t(edge * 2u);
    }
    return edge;
}

/// Takes @p assigned down to its share @p target when it holds more, keeping @p total (the assigned area) current.
/// @return True when it stepped down.
auto step_down_to_share(std::uint16_t& assigned, std::uint16_t target, std::uint64_t& total) -> bool
{
    if(assigned <= target)
    {
        return false;
    }
    total -= get_area(assigned) - get_area(target);
    assigned = target;
    return true;
}
} // namespace

auto shadow_atlas_allocator::reserve(std::uint64_t frame, const std::vector<shadow_atlas_request>& requests) -> bool
{
    const std::uint64_t previous_generation = generation_;
    const bool is_first_view = frame != frame_;
    if(is_first_view)
    {
        begin_frame(frame);
    }
    for(const auto& request : requests)
    {
        auto& e = entries_[request.owner];
        const std::uint16_t requested = clamp_tile_size(request.size);
        if(e.requested != requested)
        {
            e.requested = requested;
            // An edited resolution applies at once, up or down: it is the user's change, not the atlas's.
            e.regain_votes = 0;
            e.assigned = 0;
        }
        e.frame_importance = std::max(e.frame_importance, request.importance);
        e.last_frame = frame;
    }
    const bool levels_changed = assign_sizes(is_first_view);
    const bool size_changed = choose_atlas_size(is_first_view);
    if(levels_changed || size_changed)
    {
        repack_all();
        return true;
    }
    std::vector<entry*> unplaced;
    for(auto& [owner, e] : entries_)
    {
        if(!e.is_placed)
        {
            unplaced.push_back(&e);
        }
    }
    std::sort(unplaced.begin(),
              unplaced.end(),
              [](const entry* lhs, const entry* rhs)
              {
                  return lhs->assigned > rhs->assigned;
              });
    for(auto* e : unplaced)
    {
        if(!place(*e))
        {
            // The free space is fragmented: pack everything again, which always fits what the atlas size admits.
            repack_all();
            break;
        }
    }
    return generation_ != previous_generation;
}

auto shadow_atlas_allocator::find(std::uintptr_t owner) const -> hpp::optional<shadow_atlas_tile>
{
    const auto it = entries_.find(owner);
    if(it == entries_.end() || !it->second.is_placed)
    {
        return {};
    }
    return it->second.tile;
}

auto shadow_atlas_allocator::find_placement(std::uintptr_t owner) const -> std::uint64_t
{
    const auto it = entries_.find(owner);
    return it != entries_.end() && it->second.is_placed ? it->second.placement : 0;
}

void shadow_atlas_allocator::release(std::uintptr_t owner)
{
    const auto it = entries_.find(owner);
    if(it == entries_.end())
    {
        return;
    }
    if(it->second.is_placed)
    {
        free_square(it->second.tile);
    }
    entries_.erase(it);
}

auto shadow_atlas_allocator::get_tile_count() const -> size_t
{
    return size_t(std::count_if(entries_.begin(),
                                entries_.end(),
                                [](const auto& item)
                                {
                                    return item.second.is_placed;
                                }));
}

void shadow_atlas_allocator::clear()
{
    entries_.clear();
    free_squares_.clear();
    size_ = 0;
    shrink_votes_ = 0;
    ++generation_;
}

void shadow_atlas_allocator::begin_frame(std::uint64_t frame)
{
    std::vector<std::uintptr_t> stale;
    for(auto& [owner, e] : entries_)
    {
        // Asked for in the previous frame: a view of this frame may still ask again.
        if(e.last_frame + 1u < frame)
        {
            stale.push_back(owner);
        }
        e.previous_importance = e.frame_importance;
        e.frame_importance = 0.0f;
    }
    for(const auto owner : stale)
    {
        release(owner);
    }
    frame_ = frame;
}

auto shadow_atlas_allocator::get_importance_order() const -> importance_order
{
    importance_order order;
    order.reserve(entries_.size());
    for(const auto& [owner, e] : entries_)
    {
        order.emplace_back(e.get_importance(), owner);
    }
    std::sort(order.begin(), order.end());
    return order;
}

auto shadow_atlas_allocator::compute_targets(const importance_order& order) const -> size_map
{
    size_map targets;
    std::uint64_t total = 0;
    for(const auto& [owner, e] : entries_)
    {
        targets[owner] = e.requested;
        total += get_area(e.requested);
    }
    const std::uint64_t capacity = get_area(max_atlas_size);
    if(total <= capacity)
    {
        return targets;
    }
    // Each pass takes one level from every light that still has one, the least important first, until all fit.
    bool has_stepped = true;
    while(total > capacity && has_stepped)
    {
        has_stepped = false;
        for(const auto& [importance, owner] : order)
        {
            auto& target = targets[owner];
            if(target <= min_tile_size)
            {
                continue;
            }
            const std::uint16_t lower = std::uint16_t(target / 2u);
            total -= get_area(target) - get_area(lower);
            target = lower;
            has_stepped = true;
            if(total <= capacity)
            {
                break;
            }
        }
    }
    return targets;
}

auto shadow_atlas_allocator::assign_sizes(bool is_first_view) -> bool
{
    const auto order = get_importance_order();
    const auto targets = compute_targets(order);
    const std::uint64_t capacity = get_area(max_atlas_size);
    bool has_changed = false;
    std::uint64_t total = 0;
    for(auto& [owner, e] : entries_)
    {
        if(e.assigned == 0)
        {
            // A new light, or an edited resolution: its share at once.
            const std::uint16_t target = targets.at(owner);
            has_changed |= e.is_placed && e.tile.size != target;
            e.assigned = target;
            e.regain_votes = 0;
        }
        total += get_area(e.assigned);
    }
    // Pressure: the lights holding more than their share give it back, the least important first, as far as needed.
    for(auto it = order.begin(); it != order.end() && total > capacity; ++it)
    {
        auto& e = entries_.at(it->second);
        if(step_down_to_share(e.assigned, targets.at(it->second), total))
        {
            e.regain_votes = 0;
            has_changed = true;
        }
    }
    if(!is_first_view)
    {
        return has_changed;
    }
    // More than the tile holds: only once the light has wanted it for regain_frames frames, so a ranking that wavers
    // does not toggle levels. The most important first; lights holding more than their share make room.
    for(auto it = order.rbegin(); it != order.rend(); ++it)
    {
        auto& e = entries_.at(it->second);
        const std::uint16_t target = targets.at(it->second);
        if(e.assigned >= target)
        {
            e.regain_votes = 0;
            continue;
        }
        if(++e.regain_votes < regain_frames)
        {
            continue;
        }
        const std::uint64_t growth = get_area(target) - get_area(e.assigned);
        for(auto low = order.begin(); low != order.end() && total + growth > capacity; ++low)
        {
            auto& holder = entries_.at(low->second);
            if(step_down_to_share(holder.assigned, targets.at(low->second), total))
            {
                holder.regain_votes = 0;
                has_changed = true;
            }
        }
        if(total + growth <= capacity)
        {
            total += growth;
            e.assigned = target;
            e.regain_votes = 0;
            has_changed = true;
        }
    }
    return has_changed;
}

auto shadow_atlas_allocator::choose_atlas_size(bool is_first_view) -> bool
{
    std::uint64_t needed = 0;
    for(const auto& [owner, e] : entries_)
    {
        needed += get_area(e.assigned);
    }
    std::uint16_t fit = 0;
    if(needed > 0)
    {
        fit = max_atlas_size;
        for(const auto edge : ATLAS_SIZES)
        {
            if(get_area(edge) >= needed)
            {
                fit = edge;
                break;
            }
        }
    }
    if(fit > size_)
    {
        size_ = fit;
        shrink_votes_ = 0;
        return true;
    }
    if(!is_first_view)
    {
        return false;
    }
    if(fit == size_)
    {
        shrink_votes_ = 0;
        return false;
    }
    if(++shrink_votes_ < shrink_frames)
    {
        return false;
    }
    size_ = fit;
    shrink_votes_ = 0;
    return true;
}

void shadow_atlas_allocator::repack_all()
{
    ++generation_;
    free_squares_.clear();
    std::vector<std::pair<std::uintptr_t, entry*>> order;
    order.reserve(entries_.size());
    for(auto& [owner, e] : entries_)
    {
        e.is_placed = false;
        order.emplace_back(owner, &e);
    }
    if(size_ == 0)
    {
        return;
    }
    free_squares_.push_back({0, 0, size_});
    // The largest first packs a quadtree exactly; equal sizes in a fixed order keep the layout reproducible.
    std::sort(order.begin(),
              order.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  if(lhs.second->assigned != rhs.second->assigned)
                  {
                      return lhs.second->assigned > rhs.second->assigned;
                  }
                  if(lhs.second->get_importance() != rhs.second->get_importance())
                  {
                      return lhs.second->get_importance() > rhs.second->get_importance();
                  }
                  return lhs.first < rhs.first;
              });
    for(auto& [owner, e] : order)
    {
        place(*e);
    }
}

auto shadow_atlas_allocator::place(entry& e) -> bool
{
    const auto square = take_square(e.assigned);
    if(!square)
    {
        return false;
    }
    e.tile = *square;
    e.is_placed = true;
    e.placement = ++placement_serial_;
    return true;
}

auto shadow_atlas_allocator::take_square(std::uint16_t size) -> hpp::optional<shadow_atlas_tile>
{
    auto best = free_squares_.end();
    for(auto it = free_squares_.begin(); it != free_squares_.end(); ++it)
    {
        if(it->size >= size && (best == free_squares_.end() || it->size < best->size))
        {
            best = it;
        }
    }
    if(best == free_squares_.end())
    {
        return {};
    }
    shadow_atlas_tile square = *best;
    free_squares_.erase(best);
    // Split down to the size: keep the first quadrant, free the other three.
    while(square.size > size)
    {
        const std::uint16_t half = std::uint16_t(square.size / 2u);
        free_squares_.push_back({std::uint16_t(square.x + half), square.y, half});
        free_squares_.push_back({square.x, std::uint16_t(square.y + half), half});
        free_squares_.push_back({std::uint16_t(square.x + half), std::uint16_t(square.y + half), half});
        square.size = half;
    }
    return square;
}

void shadow_atlas_allocator::free_square(shadow_atlas_tile square)
{
    // find_if, not find: with clang, the MSVC STL sends std::find on a trivially equality comparable type to its
    // vectorized path, which only supports 1, 2, 4 and 8 byte elements and fails to compile for this 6 byte tile.
    const auto find_free_square = [this](const shadow_atlas_tile& tile)
    {
        return std::find_if(free_squares_.begin(),
                            free_squares_.end(),
                            [&tile](const shadow_atlas_tile& free)
                            {
                                return free == tile;
                            });
    };
    // Merge with the three siblings while they are all free, so large tiles can be taken again.
    while(square.size < size_)
    {
        const std::uint16_t parent = std::uint16_t(square.size * 2u);
        const std::uint16_t parent_x = std::uint16_t(square.x - square.x % parent);
        const std::uint16_t parent_y = std::uint16_t(square.y - square.y % parent);
        const shadow_atlas_tile siblings[] = {
            {parent_x, parent_y, square.size},
            {std::uint16_t(parent_x + square.size), parent_y, square.size},
            {parent_x, std::uint16_t(parent_y + square.size), square.size},
            {std::uint16_t(parent_x + square.size), std::uint16_t(parent_y + square.size), square.size},
        };
        std::vector<std::vector<shadow_atlas_tile>::iterator> found;
        for(const auto& sibling : siblings)
        {
            if(sibling == square)
            {
                continue;
            }
            const auto it = find_free_square(sibling);
            if(it == free_squares_.end())
            {
                break;
            }
            found.push_back(it);
        }
        if(found.size() != 3)
        {
            break;
        }
        for(const auto& sibling : siblings)
        {
            if(!(sibling == square))
            {
                free_squares_.erase(find_free_square(sibling));
            }
        }
        square = {parent_x, parent_y, parent};
    }
    free_squares_.push_back(square);
}

auto shadow_atlas::get() -> shadow_atlas&
{
    static shadow_atlas instance;
    return instance;
}

auto shadow_atlas::reserve(const std::vector<shadow_atlas_request>& requests) -> bool
{
    const std::uint64_t frame = gfx::get_render_frame();
    const bool has_changed = allocator_.reserve(frame, requests);
    fit_textures();
    if(bgfx::isValid(static_frame_buffer_) && static_used_frame_ + STATIC_COPY_IDLE_FRAMES < frame)
    {
        destroy_static_copy();
    }
    return has_changed;
}

auto shadow_atlas::find(std::uintptr_t owner) const -> hpp::optional<shadow_atlas_tile>
{
    if(!bgfx::isValid(frame_buffer_))
    {
        return {};
    }
    return allocator_.find(owner);
}

auto shadow_atlas::find_placement(std::uintptr_t owner) const -> std::uint64_t
{
    return bgfx::isValid(frame_buffer_) ? allocator_.find_placement(owner) : 0;
}

void shadow_atlas::release(std::uintptr_t owner)
{
    allocator_.release(owner);
}

auto shadow_atlas::get_color_texture() const -> bgfx::TextureHandle
{
    return bgfx::isValid(frame_buffer_) ? bgfx::getTexture(frame_buffer_, 0) : bgfx::TextureHandle{bgfx::kInvalidHandle};
}

auto shadow_atlas::get_depth_texture() const -> bgfx::TextureHandle
{
    return bgfx::isValid(frame_buffer_) ? bgfx::getTexture(frame_buffer_, 1) : bgfx::TextureHandle{bgfx::kInvalidHandle};
}

auto shadow_atlas::get_static_frame_buffer() -> bgfx::FrameBufferHandle
{
    static_used_frame_ = gfx::get_render_frame();
    if(!bgfx::isValid(static_frame_buffer_) && texture_size_ != 0)
    {
        // Depth only: the restore rebuilds the atlas colour from it, so the copy costs one texture, not two.
        const bgfx::TextureHandle depth =
            bgfx::createTexture2D(texture_size_, texture_size_, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
        bgfx::setName(depth, "Shadow Atlas Static Copy");
        static_frame_buffer_ = bgfx::createFrameBuffer(1, &depth, true);
        ++static_generation_;
    }
    return static_frame_buffer_;
}

auto shadow_atlas::get_static_depth_texture() const -> bgfx::TextureHandle
{
    return bgfx::isValid(static_frame_buffer_) ? bgfx::getTexture(static_frame_buffer_, 0)
                                               : bgfx::TextureHandle{bgfx::kInvalidHandle};
}

void shadow_atlas::clear()
{
    destroy_textures();
    allocator_.clear();
}

void shadow_atlas::fit_textures()
{
    const std::uint16_t size = allocator_.get_size();
    if(size == texture_size_)
    {
        return;
    }
    destroy_textures();
    texture_size_ = size;
    if(size == 0)
    {
        return;
    }
    // The formats of a light's own maps: float depth (the colour the point taps read) and the depth attachment the
    // PCF comparisons sample.
    const bgfx::TextureHandle textures[] = {
        bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::R32F, BGFX_TEXTURE_RT),
        bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT),
    };
    bgfx::setName(textures[0], "Shadow Atlas");
    bgfx::setName(textures[1], "Shadow Atlas Depth");
    frame_buffer_ = bgfx::createFrameBuffer(2, textures, true);
}

void shadow_atlas::destroy_textures()
{
    if(bgfx::isValid(frame_buffer_))
    {
        bgfx::destroy(frame_buffer_);
        frame_buffer_ = {bgfx::kInvalidHandle};
    }
    destroy_static_copy();
    texture_size_ = 0;
}

void shadow_atlas::destroy_static_copy()
{
    if(bgfx::isValid(static_frame_buffer_))
    {
        bgfx::destroy(static_frame_buffer_);
        static_frame_buffer_ = {bgfx::kInvalidHandle};
    }
}

} // namespace shadow
} // namespace unravel
