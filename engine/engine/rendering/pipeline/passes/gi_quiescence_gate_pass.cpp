#include "gi_quiescence_gate_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>

#include <logging/logging.h>

namespace unravel
{

auto gi_quiescence_gate_pass::init(rtti::context& ctx) -> bool
{
    if(!gfx::is_supported(BGFX_CAPS_DRAW_INDIRECT))
    {
        // Not an error: the readback path covers this backend. Said once, because the
        // difference between "the gate is on the GPU" and "the gate costs a sync per frame"
        // is exactly the kind of thing a profile capture cannot explain on its own.
        APPLOG_INFO("[SurfaceCache] Indirect dispatch is unavailable on this backend; the GI "
                    "quiescence gate falls back to a per-frame convergence readback.");
        return false;
    }
    auto& am = ctx.get_cached<asset_manager>();
    auto cs = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_quiescence_gate.sc");
    program_.cache_uniforms();
    program_.program = std::make_unique<gpu_program>(cs);
    return program_.is_valid();
}

gi_quiescence_gate_pass::~gi_quiescence_gate_pass()
{
    if(bgfx::isValid(indirect_))
    {
        gfx::destroy(indirect_);
    }
    if(bgfx::isValid(ring_))
    {
        gfx::destroy(ring_);
    }
}

auto gi_quiescence_gate_pass::is_available() const -> bool
{
    return program_.is_valid() && gfx::is_supported(BGFX_CAPS_DRAW_INDIRECT);
}

auto gi_quiescence_gate_pass::ensure_buffers() -> bool
{
    if(bgfx::isValid(indirect_) && bgfx::isValid(ring_))
    {
        return true;
    }
    if(!bgfx::isValid(indirect_))
    {
        indirect_ = gfx::create_indirect_buffer(entry_count);
    }
    if(!bgfx::isValid(ring_))
    {
        // INDEX32 so the shader's BUFFER_RW(uint) view matches, COMPUTE_READ_WRITE because
        // the kernel both accumulates into it and reads its own history back.
        ring_ = gfx::create_dynamic_index_buffer(ring_header_slots + ring_sample_slots,
                                                 BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
    }
    return bgfx::isValid(indirect_) && bgfx::isValid(ring_);
}

auto gi_quiescence_gate_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Quiescence Gate");
    if(!is_available() || !params.view_cache)
    {
        return false;
    }
    auto& view_cache = *params.view_cache;
    const auto& clipmap_gpu = view_cache.get_clipmap_gpu();
    if(!clipmap_gpu.is_valid())
    {
        return false;
    }
    // The statistic lives in the bounce vis-memo's trailing slice. Without that texture the
    // relight publishes nothing to measure, and the CPU path's fixed settle is the only
    // honest answer - so decline rather than gate on a statistic that is always zero.
    const auto& vis_memo = clipmap_gpu.get_bounce_vis_memo();
    if(!vis_memo || !vis_memo->is_valid())
    {
        return false;
    }
    if(!ensure_buffers())
    {
        if(!unavailable_warning_emitted_)
        {
            unavailable_warning_emitted_ = true;
            APPLOG_WARNING("[SurfaceCache] GI quiescence gate buffers could not be created; "
                           "falling back to the convergence readback.");
        }
        return false;
    }
    // A re-created memo (a resolution change) carries an unwritten statistics slice, so the
    // first frame against it must not fold allocation garbage into the ring.
    bool reset = params.reset;
    if(stats_source_ != vis_memo.get())
    {
        stats_source_ = vis_memo.get();
        reset = true;
    }
    gfx::render_pass pass("GI/Quiescence Gate");
    program_.program->begin();
    gfx::set_image_3d(0, vis_memo->native_handle(), 0, gfx::access::ReadWrite, gfx::texture_format::R32U);
    gfx::set_buffer(1, ring_, gfx::access::ReadWrite);
    gfx::set_buffer(2, indirect_, gfx::access::Write);
    // The resolution lane alone: GiLightVoxelStatsTexel needs it to address the slice.
    const float voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(program_.u_gi_light_voxel_params, voxel_params);
    const float gate_params[4] = {float(uint32_t(params.mode)), reset ? 1.0f : 0.0f, 0.0f, 0.0f};
    gfx::set_uniform(program_.u_gi_gate_params, gate_params);
    float groups[entry_count * 4] = {};
    for(uint16_t i = 0; i < entry_count; ++i)
    {
        groups[i * 4 + 0] = float(params.groups[i].x);
        groups[i * 4 + 1] = float(params.groups[i].y);
        groups[i * 4 + 2] = float(params.groups[i].z);
    }
    gfx::set_uniform(program_.u_gi_gate_groups, groups, entry_count);
    gfx::dispatch(pass.id, program_.program->native_handle(), 1, 1, 1);
    program_.program->end();
    return true;
}

} // namespace unravel
