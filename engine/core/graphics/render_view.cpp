#include "render_view.h"
#include <cassert>

namespace gfx
{

auto render_view::tex_get_or_emplace(const hpp::string_view& id) -> texture::ptr&
{
    auto it = textures_.find(id);
    if(it != textures_.end())
    {
        return it->second;
    }

    return textures_[std::string(id)];
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
        return it->second;
    }

    static const texture::ptr empty;
    return empty;
}


auto render_view::fbo_get_or_emplace(const hpp::string_view& id) -> frame_buffer::ptr&
{
    auto it = fbos_.find(id);
    if(it != fbos_.end())
    {
        return it->second;
    }

    return fbos_[std::string(id)];
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
        return it->second;
    }

    static const frame_buffer::ptr empty;
    return empty;
}

auto render_view::data_get_or_emplace(const hpp::string_view& id, uint32_t default_val) -> uint32_t&
{
    auto it = data_.find(id);
    if(it != data_.end())
    {
        return it->second;
    }

    return data_.emplace(std::string(id), default_val).first->second;
}

auto render_view::data_get(const hpp::string_view& id, uint32_t default_val) const -> uint32_t
{
    auto it = data_.find(id);
    if(it != data_.end())
    {
        return it->second;
    }

    return default_val;
}

} // namespace gfx
