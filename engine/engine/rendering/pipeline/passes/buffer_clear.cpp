#include "buffer_clear.h"

#include <engine/assets/asset_manager.h>

#include <graphics/graphics.h>
#include <logging/logging.h>

namespace unravel
{
namespace
{
/// Threads per group of cs_clear_uints.sc (NUM_THREADS(64, 1, 1)), one uint each.
constexpr uint32_t CLEAR_GROUP_SIZE = 64;
/// The cleared buffer's stage (cs_clear_uints.sc).
constexpr uint8_t CLEAR_BUFFER_STAGE = 0;

/// The groups that clear @p count uints.
auto get_clear_groups(uint32_t count) -> uint32_t
{
    return (count + CLEAR_GROUP_SIZE - 1u) / CLEAR_GROUP_SIZE;
}
} // namespace

void buffer_clear::clear_program::cache_uniforms()
{
    cache_uniform(program.get(), u_clear_uints, "u_clear_uints", bgfx::UniformType::Vec4);
}

auto buffer_clear::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    program_.cache_uniforms();
    auto shader = am.get_asset<gfx::shader>("engine:/data/shaders/cs_clear_uints.sc");
    program_.program = std::make_unique<gpu_program>(shader);
    if(!is_ready())
    {
        APPLOG_WARNING("The buffer clear program failed to load; GPU-written counters cannot start over.");
    }
    return is_ready();
}

auto buffer_clear::is_ready() const -> bool
{
    return program_.program != nullptr && program_.program->is_valid();
}

void buffer_clear::dispatch(uint16_t view_id, bgfx::DynamicIndexBufferHandle buffer, uint32_t first, uint32_t count) const
{
    if(!is_ready() || !bgfx::isValid(buffer) || count == 0u)
    {
        return;
    }
    program_.program->begin();
    bgfx::setBuffer(CLEAR_BUFFER_STAGE, buffer, bgfx::Access::Write);
    // u_clear_uints: the range's first uint and its length; the shader skips the threads past the range's end.
    gfx::set_uniform(program_.u_clear_uints, math::vec4(float(first), float(count), 0.0f, 0.0f));
    bgfx::dispatch(view_id, program_.program->native_handle(), get_clear_groups(count), 1, 1);
    program_.program->end();
}

} // namespace unravel
