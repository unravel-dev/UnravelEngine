#include "texture_pool.h"
#include "graphics.h"
#include <algorithm>

namespace gfx
{
namespace
{

/// Whether nothing outside the pool references the texture.
auto is_free(const texture::ptr& tex) -> bool
{
    return tex.use_count() == 1;
}

} // namespace

auto texture_pool::acquire(const texture_desc& desc) -> texture::ptr
{
    const uint32_t frame = get_render_frame();
    const auto it = std::find_if(entries_.begin(),
                                 entries_.end(),
                                 [&](const entry& candidate)
                                 {
                                     return is_free(candidate.tex) && candidate.tex->get_desc() == desc;
                                 });
    if(it != entries_.end())
    {
        it->last_used_frame = frame;
        return it->tex;
    }
    auto tex = std::make_shared<texture>(desc);
    if(tex->is_valid())
    {
        entries_.push_back({tex, frame});
    }
    return tex;
}

void texture_pool::release_unused(uint32_t current_frame, uint32_t max_idle_frames)
{
    const auto is_expired = [&](const entry& candidate)
    {
        return is_free(candidate.tex) && current_frame - candidate.last_used_frame > max_idle_frames;
    };
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), is_expired), entries_.end());
}

auto texture_pool::get_entry_count() const -> std::size_t
{
    return entries_.size();
}

} // namespace gfx
