#include "render_pass.h"
#include "graphics/graphics.h"
#include <bitset>
#include <limits>

namespace gfx
{
namespace
{
auto get_last_frame_counter() -> gfx::view_id&
{
    static gfx::view_id id = 0;
    return id;
}

auto get_counter() -> gfx::view_id&
{
    static gfx::view_id id = 0;
    return id;
}

auto generate_id() -> gfx::view_id
{
    const auto& limits = gfx::get_caps()->limits;
    auto& counter = get_counter();
    if(counter >= limits.maxViews - 1)
    {
        frame();
        counter = 0;
    }
    gfx::view_id idx = counter++;

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

render_pass::render_pass(view_id i, const char* name) : id(i)
{
    reset_view(id);

    const auto& scopes = get_scopes();
    if(scopes.empty())
    {
        set_view_name(id, name);
    }
    else
    {
        std::string scoped_name{};
        for(const auto& scope : scopes)
        {
            scoped_name.append(scope).append("/");
        }

        scoped_name.append(name);

        set_view_name(id, scoped_name.c_str());

    }
}

void render_pass::bind(const frame_buffer* fb) const
{
    set_view_mode(id, gfx::view_mode::Sequential);
    if(fb != nullptr)
    {
        const auto size = fb->get_size();
        const auto width = size.width;
        const auto height = size.height;
        set_view_frame_buffer(id, fb->native_handle());
        set_view_rect(id, uint16_t(0), uint16_t(0), uint16_t(width), uint16_t(height));
        set_view_scissor(id, uint16_t(0), uint16_t(0), uint16_t(width), uint16_t(height));
    }
    else
    {
        set_view_frame_buffer(id, frame_buffer::invalid_handle());
        set_view_rect(id, uint16_t(0), uint16_t(0), backbuffer_ratio::Equal);
    }
    touch();
}

void render_pass::touch() const
{
    gfx::touch(id);
}

void render_pass::clear(uint16_t _flags,
                        uint32_t _rgba /*= 0x000000ff */,
                        float _depth /*= 1.0f */,
                        uint8_t _stencil /*= 0*/) const
{
    set_view_clear(id, _flags, _rgba, _depth, _stencil);
    touch();
}

void render_pass::clear() const
{
    clear(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, 0x000000FF, 1.0f, 0);
}

void render_pass::set_view_proj(const float* v, const float* p)
{
    set_view_transform(id, v, p);
}

void render_pass::reset()
{
    auto& count = get_counter();
    get_last_frame_counter() = count;
    count = 0;
}

auto render_pass::get_max_pass_id() -> gfx::view_id
{
    const auto& limits = gfx::get_caps()->limits;
    return limits.maxViews - 1;
}

auto render_pass::get_last_frame_max_pass_id() -> gfx::view_id
{
    return get_last_frame_counter();
}
} // namespace gfx
