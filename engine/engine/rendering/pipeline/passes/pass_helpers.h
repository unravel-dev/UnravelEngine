#pragma once

#include <base/basetypes.hpp>
#include <graphics/frame_buffer.h>
#include <graphics/graphics.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <hpp/string_view.hpp>
#include <math/math.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace unravel
{

/// The output grain uniform takes a quarter of the settings' amount: 0.1-0.3 is a filmic range, 1 heavy stylized grain.
constexpr float OUTPUT_GRAIN_SCALE = 0.25f;
/// The output grain's per-frame seed cycles through this many frames.
constexpr std::uint32_t OUTPUT_GRAIN_SEED_PERIOD = 1024u;

/// Whether @p program can run this frame (gpu_program::begin), for a pointer that init may have left null.
template<typename program_pointer>
inline auto begin_program(const program_pointer& program) -> bool
{
    return program && program->begin();
}

/// The view's frame buffer @p name over @p attachments, rebuilt whenever one of them was replaced.
inline auto acquire_frame_buffer(gfx::render_view& rview,
                                 const hpp::string_view& name,
                                 const std::vector<gfx::texture::ptr>& attachments) -> gfx::frame_buffer::ptr
{
    auto& frame_buffer = rview.fbo_acquire(name);
    if(gfx::needs_recreate(frame_buffer, attachments))
    {
        frame_buffer.reset();
        frame_buffer = std::make_shared<gfx::frame_buffer>();
        frame_buffer->populate(attachments);
    }
    return frame_buffer;
}

/// @p output when the caller gave one, otherwise the view's target @p name in @p format, the size of @p input.
inline auto get_output_frame_buffer(gfx::render_view& rview,
                                    const hpp::string_view& name,
                                    const gfx::frame_buffer::ptr& input,
                                    const gfx::frame_buffer::ptr& output,
                                    bgfx::TextureFormat::Enum format) -> gfx::frame_buffer::ptr
{
    if(output)
    {
        return output;
    }
    const auto& texture = rview.tex_acquire(name, {.size = input->get_size(), .format = format, .flags = BGFX_TEXTURE_RT});
    return acquire_frame_buffer(rview, name, {texture});
}

/// The compute groups of @p group_size threads that cover @p items.
constexpr auto compute_group_count(std::uint32_t items, std::uint32_t group_size) -> std::uint32_t
{
    return (items + group_size - 1u) / group_size;
}

/// The frame's seed of the output grain.
inline auto get_output_grain_seed() -> float
{
    return static_cast<float>(gfx::get_render_frame() % OUTPUT_GRAIN_SEED_PERIOD);
}

/// u_output_noise of the last pass that writes the display image: grain amount, grain seed, dither on.
inline auto make_output_noise(float grain_intensity, bool dithering) -> std::array<float, 4>
{
    return {grain_intensity * OUTPUT_GRAIN_SCALE, get_output_grain_seed(), dithering ? 1.0f : 0.0f, 0.0f};
}

/**
 * The full-resolution size over a trace target's size. The scale is per axis because the two ratios disagree at an odd
 * full-resolution size with an even other: (1233, 900) -> half (616, 450) gives x = 2.00162 and y = 2.0. One scalar on
 * both axes would shift the bottom row's G-buffer fetch about 0.7 full-resolution pixels off, which makes a ray origin
 * outside the frustum and a noise band along the bottom of the viewport.
 */
inline auto compute_trace_scale(const usize32_t& full_size, const usize32_t& trace_size) -> math::vec2
{
    return {float(full_size.width) / float(trace_size.width), float(full_size.height) / float(trace_size.height)};
}

} // namespace unravel
