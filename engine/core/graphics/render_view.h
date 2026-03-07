#pragma once

#include "frame_buffer.h"
#include <base/basetypes.hpp>
#include <base/hash.hpp>
#include <functional>
#include <hpp/string_view.hpp>
#include <map>

namespace gfx
{


class render_view
{
public:
    auto fbo_get_or_emplace(const hpp::string_view& id) -> frame_buffer::ptr&;
    auto fbo_get(const hpp::string_view& id) const -> const frame_buffer::ptr&;
    auto fbo_safe_get(const hpp::string_view& id) const -> const frame_buffer::ptr&;

    auto tex_get_or_emplace(const hpp::string_view& id) -> texture::ptr&;
    auto tex_get(const hpp::string_view& id) const -> const texture::ptr&;
    auto tex_safe_get(const hpp::string_view& id) const -> const texture::ptr&;

    auto data_get_or_emplace(const hpp::string_view& id, uint32_t default_val = 0) -> uint32_t&;
    auto data_get(const hpp::string_view& id, uint32_t default_val = 0) const -> uint32_t;

private:
    std::map<std::string, texture::ptr, std::less<>> textures_;
    std::map<std::string, frame_buffer::ptr, std::less<>> fbos_;
    std::map<std::string, uint32_t, std::less<>> data_;
};

} // namespace gfx
