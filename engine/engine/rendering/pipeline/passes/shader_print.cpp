#include "shader_print.h"
#include "shader_print_font.h"

#include <engine/assets/asset_manager.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <vector>

namespace unravel
{
namespace
{

/// The buffer's layout (shader_print.sh): a header ([0] = the symbols printed), then four words per symbol.
constexpr uint32_t header_words = 4;
constexpr uint32_t symbol_words = 4;
/// Per symbol, two quads of six vertices: its drop shadow, then its glyph (vs_shader_print.sc).
constexpr uint32_t vertices_per_symbol = 12;
/// The draw's stages (vs_shader_print.sc, fs_shader_print.sc).
constexpr uint8_t draw_buffer_stage = 0;
constexpr uint8_t draw_font_stage = 1;
/// A set bit of a glyph row.
constexpr uint8_t glyph_coverage = 255;

/// The glyphs side by side: an R8 texture of glyph_count x 8 by 8 texels.
auto make_font_texture() -> gfx::texture::ptr
{
    using namespace shader_print_font;
    const uint32_t width = glyph_count * glyph_size;
    std::vector<uint8_t> texels(size_t(width) * glyph_size, 0);
    for(uint32_t glyph = 0; glyph < glyph_count; ++glyph)
    {
        for(uint32_t row = 0; row < glyph_size; ++row)
        {
            const uint8_t bits = glyphs[glyph * glyph_size + row];
            for(uint32_t column = 0; column < glyph_size; ++column)
            {
                const bool is_set = ((bits >> (glyph_size - 1u - column)) & 1u) != 0u;
                texels[size_t(row) * width + glyph * glyph_size + column] = is_set ? glyph_coverage : 0;
            }
        }
    }
    return std::make_shared<gfx::texture>(uint16_t(width),
                                          uint16_t(glyph_size),
                                          false,
                                          1,
                                          bgfx::TextureFormat::R8,
                                          BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
                                          bgfx::copy(texels.data(), uint32_t(texels.size())));
}

} // namespace

void shader_print::draw_program::cache_uniforms()
{
    cache_uniform(program.get(), u_shader_print, "u_shader_print", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), s_shader_print_font, "s_shader_print_font", bgfx::UniformType::Sampler);
}

shader_print::~shader_print()
{
    if(bgfx::isValid(buffer_))
    {
        bgfx::destroy(buffer_);
    }
}

auto shader_print::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    draw_program_.cache_uniforms();
    auto vs = am.get_asset<gfx::shader>("engine:/data/shaders/shader_print/vs_shader_print.sc");
    auto fs = am.get_asset<gfx::shader>("engine:/data/shaders/shader_print/fs_shader_print.sc");
    draw_program_.program = std::make_unique<gpu_program>(vs, fs);
    const bool has_clear = header_clear_.init(ctx);
    return draw_program_.program->is_valid() && has_clear;
}

void shader_print::ensure_buffer()
{
    if(!bgfx::isValid(buffer_))
    {
        buffer_ = bgfx::createDynamicIndexBuffer(header_words + max_symbols * symbol_words,
                                                 BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
    }
}

void shader_print::begin_frame()
{
    ensure_buffer();
    // Its own view, ahead of the printers' views: every print of the frame lands in the emptied buffer.
    gfx::render_pass pass("Shader Print Clear");
    header_clear_.dispatch(pass.id, buffer_, 0, header_words);
    cleared_frame_ = gfx::get_render_frame();
    has_cleared_ = header_clear_.is_ready();
}

void shader_print::bind(uint8_t stage, const usize32_t& view_size)
{
    ensure_buffer();
    bound_frame_ = gfx::get_render_frame();
    has_bound_ = true;
    view_size_ = view_size;
    bgfx::setBuffer(stage, buffer_, bgfx::Access::ReadWrite);
    const math::vec4 print(float(view_size.width), float(view_size.height), float(max_symbols), 0.0f);
    gfx::set_uniform(draw_program_.u_shader_print, print);
}

void shader_print::draw(const gfx::frame_buffer::ptr& output)
{
    auto& program = draw_program_;
    const uint32_t frame = gfx::get_render_frame();
    const bool has_prints = has_bound_ && bound_frame_ == frame && has_cleared_ && cleared_frame_ == frame;
    if(!has_prints || !output || !program.program || !program.program->is_valid())
    {
        return;
    }
    if(!font_)
    {
        font_ = make_font_texture();
    }
    gfx::render_pass pass("Shader Print");
    pass.bind(output.get());
    program.program->begin();
    bgfx::setBuffer(draw_buffer_stage, buffer_, bgfx::Access::Read);
    gfx::set_texture(program.s_shader_print_font, draw_font_stage, font_);
    const math::vec4 print(float(view_size_.width), float(view_size_.height), float(max_symbols), 0.0f);
    gfx::set_uniform(program.u_shader_print, print);
    bgfx::setVertexCount(max_symbols * vertices_per_symbol);
    // UE's premultiplied composition; the image's alpha stays.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(pass.id, program.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    program.program->end();
}

} // namespace unravel
