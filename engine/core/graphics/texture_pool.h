#pragma once

#include "texture.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gfx
{

/**
 * @brief Textures lent out by desc and taken back once the borrower lets go.
 *
 * An entry is free when the pool holds the only reference to it: a borrower keeps the
 * texture::ptr it got for as long as it records uses of the texture, and the next acquire of an
 * equal desc gets the texture back once every copy outside the pool is gone. Holding a texture
 * too long only costs memory (the pool creates another one); it never hands one texture to two
 * borrowers at once.
 *
 * Reuse within a frame is safe because passes record in view order and every view runs
 * sequentially: a texture dropped after its last recorded use is only lent to work recorded,
 * and therefore executed, after that use. A blit into a borrowed texture must not share a view
 * with the previous borrower's reads, since bgfx runs a view's blits before its draws.
 *
 * Contents are undefined on acquire: a borrower writes every texel it reads. Not thread safe;
 * a pool belongs to one render view.
 */
class texture_pool
{
public:
    /// Frames a free entry may go unacquired before release_unused() destroys it. Short on
    /// purpose: a resize creates a new size every frame, and each stale size lingers this long.
    static constexpr uint32_t default_max_idle_frames = 8;

    /**
     * @brief Lends a free texture equal to @p desc, creating one when none is free.
     *
     * A texture whose creation failed is returned without being pooled, so a later acquire
     * tries again.
     */
    auto acquire(const texture_desc& desc) -> texture::ptr;

    /// Destroys every free entry not acquired for more than @p max_idle_frames.
    void release_unused(uint32_t current_frame, uint32_t max_idle_frames = default_max_idle_frames);

    /// Entries the pool owns, lent out or free.
    [[nodiscard]] auto get_entry_count() const -> std::size_t;

private:
    struct entry
    {
        texture::ptr tex;
        uint32_t last_used_frame{0};
    };

    std::vector<entry> entries_;
};

} // namespace gfx
