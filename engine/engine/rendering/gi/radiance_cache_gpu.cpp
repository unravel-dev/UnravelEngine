#include "radiance_cache_gpu.h"

#include <engine/profiler/profiler.h>

#include <logging/logging.h>

#include <vector>

namespace unravel
{
namespace
{

namespace ANONYMOUS
{
/// Layout of the payload buffer: a flat array of vec4, matching BUFFER_RW(_, vec4, _).
auto get_vec4_buffer_layout() -> const gfx::vertex_layout&
{
    static const gfx::vertex_layout layout = []()
    {
        gfx::vertex_layout decl;
        decl.begin().add(gfx::attribute::TexCoord0, 4, gfx::attribute_type::Float).end();
        return decl;
    }();
    return layout;
}

} // namespace ANONYMOUS
} // namespace

auto radiance_cache_gpu::init(uint32_t capacity) -> bool
{
    shutdown();
    capacity_ = capacity;
    settings_.capacity = capacity;
    // Both buffers are compute read AND write: the update pass compare-exchanges keys and
    // accumulates payload in place.
    keys_ = gfx::create_dynamic_index_buffer(capacity_,
                                             BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
    data_ = gfx::create_dynamic_vertex_buffer(capacity_ * data_vec4_stride,
                                              ANONYMOUS::get_vec4_buffer_layout(),
                                              BGFX_BUFFER_COMPUTE_READ_WRITE);
    if(!is_valid())
    {
        APPLOG_ERROR("[SurfaceCache] Failed to allocate a {}-entry radiance cache.", capacity_);
        shutdown();
        return false;
    }
    needs_clear_ = true;
    const size_t bytes = size_t(capacity_) * (sizeof(uint32_t) + size_t(data_vec4_stride) * 16u);
    APPLOG_INFO("[SurfaceCache] Radiance cache ready: {} entries ({} KB).", capacity_, bytes / 1024);
    return true;
}

void radiance_cache_gpu::shutdown()
{
    if(bgfx::isValid(keys_))
    {
        gfx::destroy(keys_);
        keys_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(data_))
    {
        gfx::destroy(data_);
        data_ = {bgfx::kInvalidHandle};
    }
    capacity_ = 0;
    needs_clear_ = true;
}

void radiance_cache_gpu::clear(gfx::view_id view)
{
    if(!is_valid() || !needs_clear_)
    {
        return;
    }
    // A CPU-side upload of zeros, once. A compute clear would need its own shader and dispatch
    // to save an upload that happens exactly at startup.
    const std::vector<uint32_t> zeros(capacity_, 0u);
    gfx::update(keys_, 0, gfx::copy(zeros.data(), uint32_t(zeros.size() * sizeof(uint32_t))));
    needs_clear_ = false;
}

} // namespace unravel
