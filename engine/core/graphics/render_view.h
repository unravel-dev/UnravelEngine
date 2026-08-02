#pragma once

#include "frame_buffer.h"
#include <base/basetypes.hpp>
#include <base/hash.hpp>
#include <context/context.hpp>
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
    void fbo_remove(const hpp::string_view& id);

    auto tex_get_or_emplace(const hpp::string_view& id) -> texture::ptr&;
    auto tex_get(const hpp::string_view& id) const -> const texture::ptr&;
    auto tex_safe_get(const hpp::string_view& id) const -> const texture::ptr&;
    void tex_remove(const hpp::string_view& id);

    auto data_get_or_emplace(const hpp::string_view& id, uint32_t default_val = 0) -> uint32_t&;
    auto data_get(const hpp::string_view& id, uint32_t default_val = 0) const -> uint32_t;

    /**
     * @brief Arbitrary per-view state, keyed by name.
     *
     * Textures and framebuffers live per view because they are sized to the viewport. The same
     * argument applies to anything else a pass must not share between cameras -- a camera-centred
     * cascade, a history index, an accumulation counter -- but those are not textures, and several
     * cameras need instances of IDENTICAL type, which a type-keyed store cannot express.
     *
     * Untyped from this library's point of view, on purpose. A render view has no business knowing
     * what an engine feature parks in it, and moving engine types into the graphics library to give
     * them a home would invert the dependency. The name is the whole contract.
     */
    auto data() -> rtti::named_context&
    {
        return data_;
    }

    auto data() const -> const rtti::named_context&
    {
        return data_;
    }

private:
    std::map<std::string, texture::ptr, std::less<>> textures_;
    std::map<std::string, frame_buffer::ptr, std::less<>> fbos_;
    /// Backs both the uint32_t accessors above and @ref data. The scalar pair is kept because it
    /// reads better at its call sites than a templated get would.
    rtti::named_context data_;
};

} // namespace gfx
