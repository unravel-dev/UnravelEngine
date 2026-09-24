#include "render_pass.h"
#include "graphics/graphics.h"
#include <bitset>
#include <limits>

namespace gfx
{
namespace
{
auto get_last_frame_counter() -> bgfx::ViewId&
{
    static bgfx::ViewId id = 0;
    return id;
}

auto get_counter() -> bgfx::ViewId&
{
    static bgfx::ViewId id = 0;
    return id;
}

auto generate_id() -> bgfx::ViewId
{
    const auto& limits = bgfx::getCaps()->limits;
    auto& counter = get_counter();
    if(counter >= limits.maxViews - 1)
    {
        gfx::log("warning", "Render pass ID overflow", __FILE__, __LINE__);
        frame();
        counter = 0;
    }
    bgfx::ViewId idx = counter++;

    return idx;
}

auto get_scopes() -> std::vector<std::string>&
{
    static std::vector<std::string> scopes;
    return scopes;
}
}
void render_pass::push_scope(const char* name)
{
    get_scopes().emplace_back(name);
}

void render_pass::pop_scope()
{
    get_scopes().pop_back();
}

render_pass::render_pass(const char* name) : render_pass(generate_id(), name)
{
}

render_pass::render_pass(bgfx::ViewId i, const char* name) : id(i)
{
    bgfx::resetView(id);

    const auto& scopes = get_scopes();
    if(scopes.empty())
    {
        bgfx::setViewName(id, name);
    }
    else
    {
        std::string scoped_name{};
        for(const auto& scope : scopes)
        {
            scoped_name.append(scope).append("/");
        }

        scoped_name.append(name);

        bgfx::setViewName(id, scoped_name.c_str());

    }
}

void render_pass::bind(const frame_buffer* fb) const
{
    bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
    if(fb != nullptr)
    {
        const auto size = fb->get_size();
        const auto width = size.width;
        const auto height = size.height;
        bgfx::setViewFrameBuffer(id, fb->native_handle());
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewScissor(id, uint16_t(0), uint16_t(0), uint16_t(width), uint16_t(height));
    }
    else
    {
        bgfx::setViewFrameBuffer(id, frame_buffer::invalid_handle());
        bgfx::setViewRect(id, uint16_t(0), uint16_t(0), bgfx::BackbufferRatio::Equal);
    }
    touch();
}

void render_pass::touch() const
{
    bgfx::touch(id);
}

void render_pass::clear(uint16_t _flags,
                        uint32_t _rgba /*= 0x000000ff */,
                        float _depth /*= 1.0f */,
                        uint8_t _stencil /*= 0*/) const
{
    bgfx::setViewClear(id, _flags, _rgba, _depth, _stencil);
    touch();
}

void render_pass::clear() const
{
    clear(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, 0x000000FF, 1.0f, 0);
}

void render_pass::set_view_proj(const float* v, const float* p)
{
    bgfx::setViewTransform(id, v, p);
}

void render_pass::set_view_scissor(uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height)
{
    bgfx::setViewScissor(id, _x, _y, _width, _height);
}

void render_pass::set_view_rect(uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height)
{
    bgfx::setViewRect(id, _x, _y, _width, _height);
}

void render_pass::reset()
{
    auto& count = get_counter();
    get_last_frame_counter() = count;
    count = 0;
}

auto render_pass::get_max_pass_id() -> bgfx::ViewId
{
    const auto& limits = bgfx::getCaps()->limits;
    return limits.maxViews - 1;
}

auto render_pass::get_last_frame_max_pass_id() -> bgfx::ViewId
{
    return get_last_frame_counter();
}
} // namespace gfx
