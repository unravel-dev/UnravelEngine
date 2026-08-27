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
    /// Frames an entry may go unaccessed before release_unused() drops it. Generous on
    /// purpose: a feature in use touches its entries every frame, so only targets of a
    /// disabled or idle feature ever reach the window.
    static constexpr uint32_t default_max_idle_frames = 240;

    /**
     * @brief Returns the (possibly null) entry slot for @p id, creating it when absent.
     *
     * @param auto_collect Whether release_unused() may drop the entry once it goes
     * unaccessed for the idle window (the default). Pass false for resources whose
     * absence their consumers cannot detect or regenerate -- a reflection probe's
     * product cubemaps go unaccessed for as long as the probe is frustum-culled, and
     * the bake bookkeeping would not know to rebuild them. The flag is part of the
     * entry's description and is applied on every call: owning call sites for a key
     * must agree on it, the same way they must agree on size and format.
     */
    auto fbo_get_or_emplace(const hpp::string_view& id, bool auto_collect = true) -> frame_buffer::ptr&;
    auto fbo_get(const hpp::string_view& id) const -> const frame_buffer::ptr&;
    auto fbo_safe_get(const hpp::string_view& id) const -> const frame_buffer::ptr&;
    void fbo_remove(const hpp::string_view& id);

    /// @copydoc fbo_get_or_emplace
    auto tex_get_or_emplace(const hpp::string_view& id, bool auto_collect = true) -> texture::ptr&;
    auto tex_get(const hpp::string_view& id) const -> const texture::ptr&;
    auto tex_safe_get(const hpp::string_view& id) const -> const texture::ptr&;
    void tex_remove(const hpp::string_view& id);

    /**
     * @brief Drops every texture/framebuffer entry that has not been accessed for
     * @p max_idle_frames, releasing the GPU resources with it.
     *
     * Every accessor above counts as an access, the const gets included, and using a
     * framebuffer counts as using its attachment textures: a texture reached only
     * through its fbo (created once, attached, then always bound via the fbo) ages
     * with the fbo, never alone. Dropping such a texture entry on its own would not
     * free the texture -- the fbo's strong attachment reference keeps it alive -- but
     * WOULD desync the name, handing the next get_or_emplace a second texture while
     * the fbo still renders into the old one.
     *
     * Entries created with auto_collect = false are never dropped here (and, being
     * permanent, their fbo attachments stay refreshed). The OWNER of the view decides
     * whether and when to call this (typically its component's per-frame update):
     * lifetime is a per-view concern.
     */
    void release_unused(uint32_t current_frame, uint32_t max_idle_frames = default_max_idle_frames);

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
    template<typename T>
    struct slot
    {
        T ptr{};
        /// Mutable: the const gets are logically reads of the resource and must still
        /// count as an access for release_unused().
        mutable uint32_t last_used_frame{0};
        /// Whether release_unused() may drop this entry once it idles past the window.
        bool auto_collect{true};
    };

    std::map<std::string, slot<texture::ptr>, std::less<>> textures_;
    std::map<std::string, slot<frame_buffer::ptr>, std::less<>> fbos_;
    /// Backs both the uint32_t accessors above and @ref data. The scalar pair is kept because it
    /// reads better at its call sites than a templated get would.
    rtti::named_context data_;
};

} // namespace gfx
