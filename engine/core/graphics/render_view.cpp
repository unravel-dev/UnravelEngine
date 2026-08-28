#include "render_view.h"
#include "graphics.h"
#include <algorithm>
#include <cassert>

namespace gfx
{

auto render_view::tex_get_or_emplace(const hpp::string_view& id, bool auto_collect) -> texture::ptr&
{
    auto it = textures_.find(id);
    if(it == textures_.end())
    {
        it = textures_.emplace(std::string(id), slot<texture::ptr>{}).first;
    }
    it->second.last_used_frame = get_render_frame();
    it->second.auto_collect = auto_collect;
    return it->second.ptr;
}

auto render_view::tex_get(const hpp::string_view& id) const -> const texture::ptr&
{
    const auto& tex = tex_safe_get(id);
    assert(tex != nullptr && "Trying to get non existent element");
    return tex;
}


auto render_view::tex_safe_get(const hpp::string_view& id) const -> const texture::ptr&
{
    auto it = textures_.find(id);
    if(it != textures_.end())
    {
        it->second.last_used_frame = get_render_frame();
        return it->second.ptr;
    }

    static const texture::ptr empty;
    return empty;
}


void render_view::tex_remove(const hpp::string_view& id)
{
    auto it = textures_.find(id);
    if(it != textures_.end())
    {
        textures_.erase(it);
    }
}

auto render_view::fbo_get_or_emplace(const hpp::string_view& id, bool auto_collect) -> frame_buffer::ptr&
{
    auto it = fbos_.find(id);
    if(it == fbos_.end())
    {
        it = fbos_.emplace(std::string(id), slot<frame_buffer::ptr>{}).first;
    }
    it->second.last_used_frame = get_render_frame();
    it->second.auto_collect = auto_collect;
    return it->second.ptr;
}

auto render_view::fbo_get(const hpp::string_view& id) const -> const frame_buffer::ptr&
{
    const auto& fbo = fbo_safe_get(id);
    assert(fbo != nullptr && "Trying to get non existent element");
    return fbo;
}

auto render_view::fbo_safe_get(const hpp::string_view& id) const -> const frame_buffer::ptr&
{
    auto it = fbos_.find(id);
    if(it != fbos_.end())
    {
        it->second.last_used_frame = get_render_frame();
        return it->second.ptr;
    }

    static const frame_buffer::ptr empty;
    return empty;
}

void render_view::fbo_remove(const hpp::string_view& id)
{
    auto it = fbos_.find(id);
    if(it != fbos_.end())
    {
        fbos_.erase(it);
    }
}

void render_view::release_unused(uint32_t current_frame, uint32_t max_idle_frames)
{
    // Framebuffers first, so an expired fbo+texture pair cascades in one call: the fbo
    // entry goes, its strong attachment references drop, and the texture entry below no
    // longer counts as referenced.
    for(auto it = fbos_.begin(); it != fbos_.end();)
    {
        if(it->second.auto_collect && current_frame - it->second.last_used_frame > max_idle_frames)
        {
            it = fbos_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    // Using an fbo is using its attachments (see the header for the desync this
    // prevents): a texture entry a surviving fbo references inherits the fbo's own
    // stamp, NOT current_frame. Stamping "now" would keep the texture fresh for as
    // long as the fbo entry exists, so the texture would outlive its fbo by one full
    // idle window after the fbo expires -- and a creation site re-entered in that gap
    // finds a valid texture next to a null fbo. Inheriting the stamp makes the pair
    // age in lockstep and expire in the same call. Permanent (auto_collect = false)
    // fbos keep the old guarantee: their attachments never age out.
    for(const auto& [fbo_name, fbo_slot] : fbos_)
    {
        if(!fbo_slot.ptr)
        {
            continue;
        }
        const uint32_t fbo_stamp = fbo_slot.auto_collect ? fbo_slot.last_used_frame : current_frame;
        for(const auto& att : fbo_slot.ptr->get_attachments())
        {
            if(!att.texture)
            {
                continue;
            }
            for(auto& [tex_name, tex_slot] : textures_)
            {
                if(tex_slot.ptr == att.texture)
                {
                    tex_slot.last_used_frame = std::max(tex_slot.last_used_frame, fbo_stamp);
                }
            }
        }
    }
    for(auto it = textures_.begin(); it != textures_.end();)
    {
        if(it->second.auto_collect && current_frame - it->second.last_used_frame > max_idle_frames)
        {
            it = textures_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

auto render_view::data_get_or_emplace(const hpp::string_view& id, uint32_t default_val) -> uint32_t&
{
    return data_.get_or_emplace<uint32_t>(id, default_val);
}

auto render_view::data_get(const hpp::string_view& id, uint32_t default_val) const -> uint32_t
{
    const auto* value = data_.try_get<uint32_t>(id);
    return value != nullptr ? *value : default_val;
}

} // namespace gfx
