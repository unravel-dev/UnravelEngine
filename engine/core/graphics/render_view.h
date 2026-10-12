#pragma once

#include "frame_buffer.h"
#include "texture_pool.h"
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
    /// Frames a free transient texture may go unacquired before release_unused() drops it.
    static constexpr uint32_t default_max_transient_idle_frames = texture_pool::default_max_idle_frames;

    /// Runs right after tex_acquire(id, desc, ...) (re)builds a texture.
    using texture_created_callback = std::function<void(const texture::ptr&)>;

    /**
     * @brief Returns the (possibly null) entry slot for @p id, creating the slot when absent.
     *
     * @param auto_collect Whether release_unused() may drop the entry once it goes
     * unaccessed for the idle window (the default). Pass false for resources whose
     * absence their consumers cannot detect or regenerate -- a reflection probe's
     * product cubemaps go unaccessed for as long as the probe is frustum-culled, and
     * the bake bookkeeping would not know to rebuild them. The flag is part of the
     * entry's description and is applied on every call: owning call sites for a key
     * must agree on it, the same way they must agree on size and format.
     */
    auto tex_acquire(const hpp::string_view& id, bool auto_collect = true) -> texture::ptr&;

    /**
     * @brief Returns the texture named @p id, built from @p desc when it is missing or was built
     * from a different desc.
     *
     * The desc is the whole description of the texture: owning call sites for a key must agree
     * on it, and a change rebuilds the texture (contents lost). A framebuffer holding the old
     * texture keeps rendering into it until rebuilt; compare its attachments with the returned
     * texture.
     *
     * @param on_create Runs only after a (re)build, with the new texture: seed its contents,
     * reset per-view state that describes them.
     * @param auto_collect See tex_acquire(id, auto_collect).
     */
    auto tex_acquire(const hpp::string_view& id,
                     const texture_desc& desc,
                     const texture_created_callback& on_create = {},
                     bool auto_collect = true) -> const texture::ptr&;

    /**
     * @brief Lends a texture equal to @p desc from the view's pool of transient textures.
     *
     * For textures that live within one pass or one frame. The texture is the caller's for as
     * long as it holds the returned pointer; once every copy is dropped a later acquire of an
     * equal desc may get it. Contents are undefined: write every texel that is read. See
     * texture_pool for the ordering rules.
     */
    auto tex_acquire_transient(const texture_desc& desc) -> texture::ptr;

    /// Returns the texture named @p id, or null when there is none. Counts as an access.
    auto tex_find(const hpp::string_view& id) const -> const texture::ptr&;

    /// Returns the texture named @p id, which must exist (asserted). Counts as an access.
    auto tex_get(const hpp::string_view& id) const -> const texture::ptr&;

    /**
     * @brief Returns the (possibly null) entry slot for @p id, creating the slot when absent.
     *
     * @param auto_collect See tex_acquire(id, auto_collect).
     */
    auto fbo_acquire(const hpp::string_view& id, bool auto_collect = true) -> frame_buffer::ptr&;

    /// Returns the framebuffer named @p id, or null when there is none. Counts as an access.
    auto fbo_find(const hpp::string_view& id) const -> const frame_buffer::ptr&;

    /// Returns the framebuffer named @p id, which must exist (asserted). Counts as an access.
    auto fbo_get(const hpp::string_view& id) const -> const frame_buffer::ptr&;

    /// Drops the framebuffer named @p id, if any.
    void fbo_remove(const hpp::string_view& id);

    /// Drops the texture named @p id, if any.
    void tex_remove(const hpp::string_view& id);

    /**
     * @brief Drops every texture/framebuffer entry that has not been accessed for
     * @p max_idle_frames, releasing the GPU resources with it.
     *
     * Every accessor above counts as an access, the const gets included, and using a
     * framebuffer counts as using its attachment textures: a texture entry referenced
     * by a live fbo inherits that fbo entry's stamp, so a texture reached only through
     * its fbo (created once, attached, then always bound via the fbo) ages IN LOCKSTEP
     * with the fbo and the pair expires in the same call. It can neither die before
     * the fbo -- which would not free it (the fbo's strong attachment reference keeps
     * it alive) but WOULD desync the name, handing the next tex_acquire a second
     * texture while the fbo still renders into the old one -- nor outlive it, which
     * would open a window where a creation site keyed on the texture's validity finds
     * a healthy texture next to a null fbo slot and hands its caller the null.
     *
     * Entries created with auto_collect = false are never dropped here, and their fbo
     * attachments stay permanently refreshed. The OWNER of the view decides whether
     * and when to call this (typically its component's per-frame update): lifetime is
     * a per-view concern.
     *
     * Transient textures (tex_acquire_transient()) that are free and have not been acquired for
     * @p max_transient_idle_frames are dropped as well.
     */
    void release_unused(uint32_t current_frame,
                        uint32_t max_idle_frames = default_max_idle_frames,
                        uint32_t max_transient_idle_frames = default_max_transient_idle_frames);

    /**
     * @brief Counts every texture and framebuffer entry whose id starts with @p prefix as
     * accessed this frame.
     *
     * For a feature that keeps its targets across frames without binding them (a held
     * result): without the stamp, release_unused() drops them once the hold outlasts the
     * idle window, and the feature resumes on fresh targets with no history.
     */
    void touch_prefixed(const hpp::string_view& prefix);

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
    texture_pool transient_textures_;
    rtti::named_context data_;
};

} // namespace gfx
