#pragma once

#include "buffer_clear.h"

#include <engine/rendering/gi/lumen_scene.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/texture.h>

#include <context/context.hpp>

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief The reflections' surface cache feedback, from the GPU to lumen_scene::set_feedback. The reflection
 *        feedback pass (cs_lumen_reflection_feedback.sc) counts its elements in a hash table that lasts a window of
 *        frames; at the window's end the table is copied into an image and cleared, the image blitted into a
 *        read-back texture in a view of its own (bgfx runs a view's blits before its dispatches) and read, and once
 *        the GPU has delivered it the elements are decoded for the scene.
 *
 * bgfx reads a texture back by draining the GPU (every backend copies and maps at the read), so the feedback is read
 * once per window rather than every frame, and an element must exceed the per-frame minimum of hits times the
 * window's frames. A window starts over (its table cleared) when the card indices move: an element names a card by
 * its index in the card table.
 */
class lumen_surface_cache_feedback
{
public:
    /// The hits an element must exceed, per frame of feedback, to keep or ask for its card's hi-res page.
    static constexpr uint32_t min_page_hits_per_frame = 16;
    /// The render frames a window counts before its readback (every camera's feedback of a frame goes in it).
    static constexpr uint32_t window_frames = 16;
    /// Hash table slots: room for 1024 unique elements, twice over for the probing.
    static constexpr uint32_t hash_slots = 2048;

    lumen_surface_cache_feedback() = default;
    ~lumen_surface_cache_feedback();
    lumen_surface_cache_feedback(const lumen_surface_cache_feedback&) = delete;
    auto operator=(const lumen_surface_cache_feedback&) -> lumen_surface_cache_feedback& = delete;

    /// Loads the readback copy and the table clear; false, with a warning, when either failed to load.
    auto init(rtti::context& ctx) -> bool;
    auto is_ready() const -> bool;

    /// Starts this frame's feedback in view @p view_id, before the feedback pass: a new window, its table cleared, when
    /// the card indices are no longer the window's (@p card_index_revision, lumen_scene::get_card_index_revision).
    void begin_frame(uint16_t view_id, uint64_t card_index_revision);

    /// Binds the table's keys and counts for the feedback pass (read-write).
    void bind(uint8_t keys_stage, uint8_t counts_stage) const;

    /// Ends this frame's feedback of @p samples feedback texels in view @p view_id, after the feedback pass. At the end
    /// of a window, unless a readback is still on its way, copies the table out, reads it back and clears it.
    void end_frame(uint16_t view_id, uint32_t samples);

    /// The last readback once the GPU has delivered it (true, once): its elements, the window's card index revision,
    /// the hits an element must exceed (min_page_hits_per_frame per frame of the window) and the feedback texels it
    /// counted.
    auto take(std::vector<lumen_scene::feedback_element>& elements,
              uint64_t& card_index_revision,
              uint32_t& min_hits,
              uint32_t& sample_count) -> bool;

    /// Render frame @p frame's texel in every 16 x 16 feedback tile: a reversed-bits Morton walk over 256 frames.
    static auto get_tile_jitter(uint32_t frame) -> math::uvec2;

    /// For the surface cache's stats line: the frames counted, the windows started over and the readbacks taken since
    /// the last call, and the largest count of the last readback.
    struct stats
    {
        uint32_t frames = 0;
        uint32_t restarts = 0;
        uint32_t readbacks = 0;
        uint32_t elements = 0;
        uint32_t max_hits = 0;
    };
    auto take_stats() -> stats;

    /// Decodes the readback of one table slot (x = the element, y = its count); false for an empty slot.
    static auto decode(uint32_t key, uint32_t count, lumen_scene::feedback_element& out) -> bool;

private:
    struct copy_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_feedback_copy;
        std::unique_ptr<gpu_program> program;
    };

    void ensure_resources();
    void clear(uint16_t view_id) const;

    copy_program copy_;
    buffer_clear clear_;
    bgfx::DynamicIndexBufferHandle keys_{bgfx::kInvalidHandle};
    bgfx::DynamicIndexBufferHandle counts_{bgfx::kInvalidHandle};
    ///< The table as an image (the copy's target), and the CPU-side texture it is blitted into and read from.
    gfx::texture::ptr table_image_;
    gfx::texture::ptr readback_;
    ///< The readback's destination: x = element, y = count per slot.
    std::vector<uint32_t> readback_data_;
    ///< The frame bgfx delivers the pending readback in (0 = none pending), and the window it read.
    uint32_t ready_frame_ = 0;
    uint64_t pending_revision_ = 0;
    uint32_t pending_frames_ = 0;
    uint32_t pending_samples_ = 0;
    ///< The current window: its card index revision, frames and feedback texels.
    bool has_window_ = false;
    uint64_t window_revision_ = 0;
    uint32_t window_frame_count_ = 0;
    uint32_t window_samples_ = 0;
    ///< The render frame the window last counted: several cameras end one frame's feedback.
    uint32_t window_counted_frame_ = ~0u;
    stats stats_{};
};

} // namespace unravel
