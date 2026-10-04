#pragma once

#include <engine/rendering/gpu_program.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <graphics/frame_buffer.h>
#include <graphics/texture.h>

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>

namespace unravel
{

/**
 * @brief UE 5.8's ShaderPrint for compute shaders (ShaderPrint.cpp, ShaderPrintCommon.ush): text a frame's compute
 *        shaders write into this view's print buffer (shader_print/shader_print.sh), drawn over the finished image
 *        with an 8x8 font and UE's drop shadow.
 *
 * A frame no shader printed in costs nothing: the buffer and the font are made on first use, and draw() draws only
 * after a bind() in the same frame.
 */
class shader_print
{
public:
    /// Symbols a frame can print (UE r.ShaderPrint.MaxCharacters).
    static constexpr uint32_t max_symbols = 4096;

    shader_print() = default;
    ~shader_print();
    shader_print(const shader_print&) = delete;
    auto operator=(const shader_print&) -> shader_print& = delete;

    auto init(rtti::context& ctx) -> bool;

    /// Binds the print buffer at @p stage for the compute dispatch that follows (shader_print.sh SHADER_PRINT_STAGE),
    /// printing over a view of @p view_size pixels; the frame's first bind empties the buffer.
    void bind(uint8_t stage, const usize32_t& view_size);

    /// Draws the frame's text over @p output.
    void draw(const gfx::frame_buffer::ptr& output);

private:
    struct draw_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_shader_print;
        gfx::program::uniform_ptr s_shader_print_font;
        std::unique_ptr<gpu_program> program;
    };

    draw_program draw_program_;
    bgfx::DynamicIndexBufferHandle buffer_{bgfx::kInvalidHandle};
    ///< The glyphs side by side (shader_print_font), made on the first draw.
    gfx::texture::ptr font_;
    ///< The render frame of the last bind and the view it printed over.
    uint32_t bound_frame_ = 0;
    bool has_bound_ = false;
    usize32_t view_size_{};
};

} // namespace unravel
