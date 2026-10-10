#include "lumen_surface_cache_feedback.h"

#include "lumen_pass_common.h"

#include <engine/assets/asset_manager.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

#include <algorithm>

namespace unravel
{
namespace
{

/// The readback image's rows of table slots, and the copy's threads per group (cs_lumen_feedback_copy.sc).
constexpr uint32_t readback_width = 64;
constexpr uint32_t copy_group_size = 64;
/// An element's fields (cs_lumen_reflection_feedback.sc): card | level << 20 | page x << 24 | page y << 28.
constexpr uint32_t card_bits = 0xFFFFFu;
constexpr uint32_t level_shift = 20u;
constexpr uint32_t u_shift = 24u;
constexpr uint32_t v_shift = 28u;
constexpr uint32_t nibble = 0xFu;
/// log2 of the feedback tile's side (16 texels): the jitter covers its 2^(2 x this) texels.
constexpr uint32_t tile_size_log2 = 4u;
constexpr uint32_t bits_per_uint = 32u;

/// Decodes one axis of a 2D Morton code: the even bits of @p x packed together.
auto reverse_morton_code2(uint32_t x) -> uint32_t
{
    x &= 0x55555555u;
    x = (x ^ (x >> 1u)) & 0x33333333u;
    x = (x ^ (x >> 2u)) & 0x0F0F0F0Fu;
    x = (x ^ (x >> 4u)) & 0x00FF00FFu;
    x = (x ^ (x >> 8u)) & 0x0000FFFFu;
    return x;
}

auto reverse_bits(uint32_t x) -> uint32_t
{
    x = ((x >> 1u) & 0x55555555u) | ((x & 0x55555555u) << 1u);
    x = ((x >> 2u) & 0x33333333u) | ((x & 0x33333333u) << 2u);
    x = ((x >> 4u) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4u);
    x = ((x >> 8u) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8u);
    return (x >> 16u) | (x << 16u);
}

} // namespace

void lumen_surface_cache_feedback::copy_program::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_feedback_copy, "u_lumen_feedback_copy", bgfx::UniformType::Vec4);
}

lumen_surface_cache_feedback::~lumen_surface_cache_feedback()
{
    lumen_pass::destroy_handle(keys_);
    lumen_pass::destroy_handle(counts_);
}

auto lumen_surface_cache_feedback::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before the program: the OpenGL renderer wires a program's uniforms at link time.
    copy_.cache_uniforms();
    copy_.program = std::make_unique<gpu_program>(am.get_asset<gfx::shader>("engine:/data/shaders/lumen/cs_lumen_feedback_copy.sc"));
    clear_.init(ctx);
    if(!is_ready())
    {
        APPLOG_WARNING("[GI] Surface cache feedback programs failed to load; reflections read the locked pages only.");
    }
    return is_ready();
}

auto lumen_surface_cache_feedback::is_ready() const -> bool
{
    return copy_.program && copy_.program->is_valid() && clear_.is_ready();
}

void lumen_surface_cache_feedback::ensure_resources()
{
    if(bgfx::isValid(keys_))
    {
        return;
    }
    keys_ = lumen_pass::make_uint_buffer(hash_slots);
    counts_ = lumen_pass::make_uint_buffer(hash_slots);
    table_image_ = std::make_shared<gfx::texture>(uint16_t(readback_width),
                                                  uint16_t(hash_slots / readback_width),
                                                  false,
                                                  1,
                                                  bgfx::TextureFormat::RG32U,
                                                  BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
    readback_ = std::make_shared<gfx::texture>(uint16_t(readback_width),
                                               uint16_t(hash_slots / readback_width),
                                               false,
                                               1,
                                               bgfx::TextureFormat::RG32U,
                                               BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    readback_data_.assign(size_t(hash_slots) * 2u, 0u);
    has_window_ = false;
}

void lumen_surface_cache_feedback::clear(uint16_t view_id) const
{
    clear_.dispatch(view_id, keys_, 0, hash_slots);
    clear_.dispatch(view_id, counts_, 0, hash_slots);
}

void lumen_surface_cache_feedback::begin_frame(uint16_t view_id, uint64_t card_index_revision)
{
    ensure_resources();
    if(has_window_ && card_index_revision == window_revision_)
    {
        return;
    }
    clear(view_id);
    ++stats_.restarts;
    has_window_ = true;
    window_revision_ = card_index_revision;
    window_frame_count_ = 0;
    window_samples_ = 0;
    window_counted_frame_ = ~0u;
}

void lumen_surface_cache_feedback::bind(uint8_t keys_stage, uint8_t counts_stage) const
{
    bgfx::setBuffer(keys_stage, keys_, bgfx::Access::ReadWrite);
    bgfx::setBuffer(counts_stage, counts_, bgfx::Access::ReadWrite);
}

void lumen_surface_cache_feedback::end_frame(uint16_t view_id, uint32_t samples)
{
    window_samples_ += samples;
    const uint32_t frame = gfx::get_render_frame();
    if(frame != window_counted_frame_)
    {
        window_counted_frame_ = frame;
        ++window_frame_count_;
        ++stats_.frames;
    }
    // The bgfx readback drains the GPU, so only once per window, and never over one still on its way.
    if(window_frame_count_ < window_frames || ready_frame_ != 0)
    {
        return;
    }
    copy_.program->begin();
    bgfx::setBuffer(0, keys_, bgfx::Access::Read);
    bgfx::setBuffer(1, counts_, bgfx::Access::Read);
    bgfx::setImage(2, table_image_->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RG32U);
    const math::vec4 params(float(hash_slots), float(readback_width), 0.0f, 0.0f);
    gfx::set_uniform(copy_.u_lumen_feedback_copy, params);
    bgfx::dispatch(view_id, copy_.program->native_handle(), lumen_pass::divide_round_up(hash_slots, copy_group_size), 1, 1);
    copy_.program->end();
    clear(view_id);
    // A view after the copy's: its blit runs once the copy is done. bgfx reads the texture after every view.
    gfx::render_pass readback_pass("GI/Reflections Feedback Readback");
    bgfx::blit(readback_pass.id,
               bgfx::TextureRegion{.handle = readback_->native_handle()},
               bgfx::TextureRegion{.handle = table_image_->native_handle()});
    ready_frame_ = bgfx::read(bgfx::TextureRegion{.handle = readback_->native_handle()}, readback_data_.data());
    pending_revision_ = window_revision_;
    pending_frames_ = window_frame_count_;
    pending_samples_ = window_samples_;
    window_frame_count_ = 0;
    window_samples_ = 0;
}

auto lumen_surface_cache_feedback::decode(uint32_t key, uint32_t count, lumen_scene::feedback_element& out) -> bool
{
    if(key == 0u || count == 0u)
    {
        return false;
    }
    out.card_index = key & card_bits;
    out.res_level = (key >> level_shift) & nibble;
    out.page = math::uvec2((key >> u_shift) & nibble, (key >> v_shift) & nibble);
    out.hits = count;
    return true;
}

auto lumen_surface_cache_feedback::take(std::vector<lumen_scene::feedback_element>& elements,
                                        uint64_t& card_index_revision,
                                        uint32_t& min_hits,
                                        uint32_t& sample_count) -> bool
{
    if(ready_frame_ == 0 || gfx::get_render_frame() < ready_frame_)
    {
        return false;
    }
    ready_frame_ = 0;
    elements.clear();
    stats_.max_hits = 0;
    for(uint32_t slot = 0; slot < hash_slots; ++slot)
    {
        lumen_scene::feedback_element element;
        if(decode(readback_data_[size_t(slot) * 2u], readback_data_[size_t(slot) * 2u + 1u], element))
        {
            elements.push_back(element);
            stats_.max_hits = std::max(stats_.max_hits, element.hits);
        }
    }
    ++stats_.readbacks;
    stats_.elements = uint32_t(elements.size());
    card_index_revision = pending_revision_;
    min_hits = min_page_hits_per_frame * pending_frames_;
    sample_count = pending_samples_;
    return true;
}

auto lumen_surface_cache_feedback::take_stats() -> stats
{
    const stats taken = stats_;
    stats_.frames = 0;
    stats_.restarts = 0;
    stats_.readbacks = 0;
    return taken;
}

auto lumen_surface_cache_feedback::get_tile_jitter(uint32_t frame) -> math::uvec2
{
    const uint32_t sequence = 1u << (2u * tile_size_log2);
    const uint32_t pixel = frame % sequence;
    const uint32_t address = reverse_bits(pixel) >> (bits_per_uint - 2u * tile_size_log2);
    return {reverse_morton_code2(address), reverse_morton_code2(address >> 1u)};
}

} // namespace unravel
