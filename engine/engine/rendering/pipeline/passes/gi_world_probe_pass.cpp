#include "gi_world_probe_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>

#include <graphics/graphics.h>

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace
{
/// A camera move of more than this many level-0 probe cells in one frame is a JUMP (a teleport,
/// a focus jump, a scene open) and re-arms the fast window: the probes of the new region
/// re-measure within four frames, and the relight loop around them converges before the
/// quiescence gate's scene-wide change mean reads rest. Continuous flight moves a fraction of a
/// 2 m cell per frame and stays at one stratum.
constexpr float fast_window_jump_cells = 2.0f;
/// Windows of fast refresh a jump arms. The scroll composes that used to arm the window after a
/// jump landed over several frames, keeping it on for about a window and a quarter; three
/// windows (48 frames) keep a margin for the budgeted recomposes of a long jump.
constexpr uint32_t fast_window_jump_windows = 3u;
/// Four probes per 64-lane group (PROBE_TRACE_SLOTS in cs_gi_world_probe_trace.sc): a 16-lane
/// group left half or three quarters of every wave idle.
constexpr uint32_t probes_per_trace_group = 4u;
/// One thread per pool slot or index cell (NUM_THREADS of cs_gi_world_probe_alloc.sc).
constexpr uint32_t alloc_threads_per_group = 64u;
/// The allocation kernel's phases (ALLOC_PHASE_* in cs_gi_world_probe_alloc.sc).
constexpr float alloc_phase_init = 0.0f;
constexpr float alloc_phase_evict = 1.0f;
constexpr float alloc_phase_allocate = 2.0f;
/// The trace scheduler's phases (SELECT_PHASE_* in cs_gi_world_probe_select.sc).
constexpr float select_phase_histogram = 0.0f;
constexpr float select_phase_threshold = 1.0f;
constexpr float select_phase_emit = 2.0f;
/// One thread per slot (NUM_THREADS of cs_gi_world_probe_select.sc).
constexpr uint32_t select_threads_per_group = 64u;
/// The schedule word's frame field (GI_WORLD_PROBE_COUNT_FRAME_MASK in gi_world_probes.sh).
constexpr uint32_t schedule_frame_mask = 0xFFFFFu;
} // namespace

auto gi_world_probe_pass::get_trace_dispatch_groups() const -> gi_quiescence_gate_pass::dispatch_groups
{
    gi_quiescence_gate_pass::dispatch_groups groups;
    groups.x = (frame_budget_ + probes_per_trace_group - 1u) / probes_per_trace_group;
    groups.y = 1u;
    groups.z = 1u;
    return groups;
}

auto gi_world_probe_pass::get_convolve_dispatch_groups() const -> gi_quiescence_gate_pass::dispatch_groups
{
    gi_quiescence_gate_pass::dispatch_groups groups;
    groups.x = frame_budget_;
    groups.y = 1u;
    groups.z = 1u;
    return groups;
}

auto gi_world_probe_pass::get_select_dispatch_groups(uint16_t entry) -> gi_quiescence_gate_pass::dispatch_groups
{
    gi_quiescence_gate_pass::dispatch_groups groups;
    groups.x = entry == gi_quiescence_gate_pass::entry_probe_select_threshold
                   ? 1u
                   : (probe_count + select_threads_per_group - 1u) / select_threads_per_group;
    groups.y = 1u;
    groups.z = 1u;
    return groups;
}

auto gi_world_probe_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs_trace = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_world_probe_trace.sc");
    trace_program_.cache_uniforms();
    trace_program_.program = std::make_unique<gpu_program>(cs_trace);
    auto cs_convolve = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_world_probe_convolve.sc");
    convolve_program_.cache_uniforms();
    convolve_program_.program = std::make_unique<gpu_program>(cs_convolve);
    auto cs_alloc = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_world_probe_alloc.sc");
    alloc_program_.cache_uniforms();
    alloc_program_.program = std::make_unique<gpu_program>(cs_alloc);
    auto cs_relocate = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_world_probe_relocate.sc");
    relocate_program_.cache_uniforms();
    relocate_program_.program = std::make_unique<gpu_program>(cs_relocate);
    auto cs_select = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_world_probe_select.sc");
    select_program_.cache_uniforms();
    select_program_.program = std::make_unique<gpu_program>(cs_select);
    return is_valid();
}

auto gi_world_probe_pass::run_alloc(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/World Probe Alloc");
    if(!alloc_program_.is_valid() || !relocate_program_.is_valid() || !params.surface_cache ||
       !params.view_cache)
    {
        return false;
    }
    if(!params.surface_cache->is_enabled())
    {
        return false;
    }
    auto& surface_cache = *params.surface_cache;
    auto& view_cache = *params.view_cache;
    auto& clipmap_gpu = view_cache.get_clipmap_gpu_mutable();
    const auto& clipmap = view_cache.get_clipmap();
    if(!clipmap_gpu.is_valid() || !clipmap_gpu.has_world_probes())
    {
        return false;
    }
    // The census rows this pass reports through (the gate's hold) live in the vis-memo's
    // statistics slice, and the evict phase reads the cell buffer's sentinels: both are seeded
    // by the compose pass earlier this frame, so a seed still pending means a helper shader is
    // missing - no allocation until the bookkeeping it depends on exists.
    const auto& vis_memo = clipmap_gpu.get_bounce_vis_memo();
    if(!vis_memo || !vis_memo->is_valid() || clipmap_gpu.needs_bounce_vis_memo_seed() ||
       clipmap_gpu.needs_buffer_seed())
    {
        return false;
    }
    // The level-0 window centre, in whole cells, exactly as the trace derives it.
    const float spacing = clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
    const float safe_spacing = spacing > 0.0f ? spacing : 1.0f;
    const float center[3] = {std::floor(params.camera_position.x / safe_spacing + 0.5f),
                             std::floor(params.camera_position.y / safe_spacing + 0.5f),
                             std::floor(params.camera_position.z / safe_spacing + 0.5f)};
    gfx::render_pass pass("GI/World Probe Alloc");
    const auto dispatch_phase = [&](float phase, uint32_t threads)
    {
        alloc_program_.program->begin();
        gfx::set_buffer(13, clipmap_gpu.get_world_probe_index(), gfx::access::ReadWrite);
        gfx::set_buffer(8, clipmap_gpu.get_world_probe_cells(), gfx::access::ReadWrite);
        gfx::set_buffer(7, clipmap_gpu.get_world_probe_counts(), gfx::access::ReadWrite);
        gfx::set_image_3d(6, vis_memo->native_handle(), 0, gfx::access::ReadWrite, gfx::texture_format::R32U);
        const float alloc_params[4] = {phase, center[0], center[1], center[2]};
        gfx::set_uniform(alloc_program_.u_gi_world_probe_alloc, alloc_params);
        // x = the resolution GiLightVoxelStatsTexel needs to address the slice; y = the editor
        // census (the eviction count is instrument work).
        const float voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), params.census ? 1.0f : 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(alloc_program_.u_gi_light_voxel_params, voxel_params);
        gfx::dispatch(pass.id,
                      alloc_program_.program->native_handle(),
                      (threads + alloc_threads_per_group - 1u) / alloc_threads_per_group,
                      1,
                      1);
        alloc_program_.program->end();
    };
    const uint32_t index_cells = global_sdf_clipmap_gpu::get_world_probe_index_cell_count();
    const uint32_t pool = global_sdf_clipmap_gpu::world_probe_pool_l0;
    if(clipmap_gpu.needs_world_probe_index_seed())
    {
        dispatch_phase(alloc_phase_init, std::max(index_cells, pool));
        clipmap_gpu.mark_world_probe_index_seeded();
    }
    // Pushes (evict) and pops (allocate) in separate dispatches: the free stack then needs
    // only its counter's atomics.
    dispatch_phase(alloc_phase_evict, pool);
    dispatch_phase(alloc_phase_allocate, index_cells);
    // The relocation pass over the frame's fresh claims: the mesh fields (sdf_common.sh's
    // fixed stages, as every tracer binds them) plus the same index, cell, count and census
    // bindings; a buried claim is pushed back onto the free stack here.
    {
        relocate_program_.program->begin();
        gfx::set_buffer(13, clipmap_gpu.get_world_probe_index(), gfx::access::ReadWrite);
        gfx::set_buffer(8, clipmap_gpu.get_world_probe_cells(), gfx::access::ReadWrite);
        gfx::set_buffer(7, clipmap_gpu.get_world_probe_counts(), gfx::access::ReadWrite);
        gfx::set_image_3d(6, vis_memo->native_handle(), 0, gfx::access::ReadWrite, gfx::texture_format::R32U);
        auto& atlas = surface_cache.get_atlas();
        gfx::set_texture(relocate_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
        gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
        gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
        gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
        gfx::set_buffer(12, surface_cache.get_grid_buffer(), gfx::access::Read);
        const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                     float(atlas.get_atlas_voxel_dim()),
                                     float(surface_cache.get_instances().size()),
                                     float(surface_cache.get_emitters().size())};
        gfx::set_uniform(relocate_program_.u_sdf_params, sdf_params);
        gfx::set_uniform(relocate_program_.u_sdf_grid_params, surface_cache.get_grid_params(), gi::GI_SDF_GRID_PARAMS_VEC4);
        // The spacing lane alone drives GiWorldProbeRelocate; the rest as the trace sets it.
        const float probe_params[4] = {safe_spacing, 0.0f, 1.0f, 0.0f};
        gfx::set_uniform(relocate_program_.u_gi_world_probe_params, probe_params);
        const float voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), 0.0f, 0.0f, 0.0f};
        gfx::set_uniform(relocate_program_.u_gi_light_voxel_params, voxel_params);
        gfx::dispatch(pass.id,
                      relocate_program_.program->native_handle(),
                      (pool + alloc_threads_per_group - 1u) / alloc_threads_per_group,
                      1,
                      1);
        relocate_program_.program->end();
    }
    return true;
}

auto gi_world_probe_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/World Probes");
    if(!is_valid() || !params.surface_cache || !params.view_cache)
    {
        return false;
    }
    auto& surface_cache = *params.surface_cache;
    if(!surface_cache.is_enabled())
    {
        return false;
    }
    auto& view_cache = *params.view_cache;
    const auto& clipmap_gpu = view_cache.get_clipmap_gpu();
    const auto& clipmap = view_cache.get_clipmap();
    if(!clipmap_gpu.is_valid() || !clipmap_gpu.has_world_probes())
    {
        return false;
    }
    auto& atlas = surface_cache.get_atlas();
    const auto& instances = surface_cache.get_instances();
    // Rays reach the whole traceable world: the outermost cascade's half extent, the same
    // derivation the gather's max distance uses (a longer promise would be fiction).
    const float trace_reach = clipmap.get_level_extent(global_sdf_clipmap::level_count - 1u) * 0.5f;
    // Window centres: the camera's probe cell per level, in whole cells (the snap is what makes
    // rotation and sub-cell translation no-ops).
    float window[global_sdf_clipmap::level_count * 4] = {};
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
    {
        const float spacing =
            clipmap.get_level(level).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
        const float safe_spacing = spacing > 0.0f ? spacing : 1.0f;
        window[level * 4 + 0] = std::floor(params.camera_position.x / safe_spacing + 0.5f);
        window[level * 4 + 1] = std::floor(params.camera_position.y / safe_spacing + 0.5f);
        window[level * 4 + 2] = std::floor(params.camera_position.z / safe_spacing + 0.5f);
        window[level * 4 + 3] = trace_reach;
    }
    const float base_spacing =
        clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
    // Change fast window (plan section 8): a changed light set OR changed scene content
    // quadruples the strata per frame for one full window, so the whole atlas re-measures
    // within 4 frames exactly while something is changing and costs nothing while the scene
    // is still. x4 rather than the original x2 because the world probes sit mid-chain in the
    // reactivity path (recompose -> relight -> HERE -> screen probes -> resolve temporal)
    // and every stage's latency SERIALIZES - an 8-frame refresh here was a third of the
    // measured emissive-drag trail on its own. The stratum count must divide
    // GI_WORLD_PROBE_WINDOW so the per-frame coverage stays exhaustive. The content epoch
    // fires on geometry/material changes (a door closing) and is scroll-suppressed, so
    // camera motion alone never pins the fast path.
    if(params.light_hash != last_light_hash_)
    {
        last_light_hash_ = params.light_hash;
        fast_frames_ = gi::GI_WORLD_PROBE_WINDOW;
    }
    // The environment SH is the other global light source these probes read (every sky miss
    // integrates it), and nothing else in this chain moves when only the sky is edited.
    if(params.environment_hash != last_environment_hash_)
    {
        last_environment_hash_ = params.environment_hash;
        fast_frames_ = gi::GI_WORLD_PROBE_WINDOW;
    }
    // EDITED epoch: the probes trace the composed field and read the light voxels, so the
    // fast window keys on content actually landing - during an edit drag the target epoch
    // churns every frame while recomposes coalesce, and each landing re-arms the window. Not
    // the composed epoch itself: that one also moves on every window scroll that brings new
    // instances into a level, and keyed on it camera motion held the whole atlas at four
    // strata per frame (1.5-2.2 ms of a 6 ms frame, gi_perf_investigation_2026-09-13.md).
    if(clipmap.get_edited_content_epoch() != last_content_epoch_)
    {
        last_content_epoch_ = clipmap.get_edited_content_epoch();
        fast_frames_ = gi::GI_WORLD_PROBE_WINDOW;
    }
    // CAMERA JUMP (fast_window_jump_cells): the scroll composes no longer arm the window, so
    // a teleport into a new region re-measured its probes over a whole window while the gate's
    // scene-wide change mean already read rest - the GI test suite's thin-walled sealed cell
    // then froze at 2.7x its converged indirect (0.0431 against 0.0157; the trace at four
    // strata every frame restored 0.0157, the convolve without its rotation did not).
    const float jump_cells = std::max(std::abs(window[0] - last_level0_cell_[0]),
                                      std::max(std::abs(window[1] - last_level0_cell_[1]),
                                               std::abs(window[2] - last_level0_cell_[2])));
    if(has_last_level0_cell_ && jump_cells > fast_window_jump_cells)
    {
        fast_frames_ = std::max<uint32_t>(fast_frames_,
                                          uint32_t(gi::GI_WORLD_PROBE_WINDOW) * fast_window_jump_windows);
    }
    last_level0_cell_[0] = window[0];
    last_level0_cell_[1] = window[1];
    last_level0_cell_[2] = window[2];
    has_last_level0_cell_ = true;
    const uint32_t strata_per_frame = fast_frames_ > 0 ? 4u : 1u;
    static_assert(gi::GI_WORLD_PROBE_WINDOW % 4 == 0,
                  "fast-window strata must divide the probe window (exhaustive coverage)");
    if(fast_frames_ > 0)
    {
        --fast_frames_;
    }
    // w carries the cage-visibility variance gate, its documented meaning for every reading
    // consumer. The trace's strata-per-frame rides the trace-only seed-atlas uniform's z lane
    // instead (see cs_gi_world_probe_trace.sc) - the old aliasing was one #define away from
    // the gate silently becoming the stratum count.
    const float probe_params[4] = {base_spacing,
                                   float(params.frame),
                                   1.0f,
                                   gi::GI_WORLD_PROBE_CAGE_VIS_VARIANCE_GATE};
    const auto env_sh =
        params.irradiance_sh ? params.irradiance_sh : default_textures::get().black_texture();
    // TRACE SCHEDULER (plan item 2.1, Lumen's radiance-cache update budget): the probes this
    // frame's trace and convolve process, listed by cs_gi_world_probe_select.sc - claims and
    // scrolled-in slots first, then first-window probes, then the stalest by level-weighted age -
    // up to GI_WORLD_PROBE_TRACE_BUDGET, or every live probe while a fast window is armed. Each
    // phase rides its own gate entry, so a closed gate stops it with the passes it feeds.
    const uint32_t budget = strata_per_frame > 1u ? probe_count : uint32_t(gi::GI_WORLD_PROBE_TRACE_BUDGET);
    frame_budget_ = budget;
    {
        gfx::render_pass pass("GI/World Probe Select");
        const auto dispatch_select = [&](float phase, uint16_t entry)
        {
            select_program_.program->begin();
            gfx::set_buffer(7, clipmap_gpu.get_world_probe_counts(), gfx::access::Read);
            gfx::set_buffer(8, clipmap_gpu.get_world_probe_cells(), gfx::access::Read);
            gfx::set_buffer(9, clipmap_gpu.get_world_probe_select(), gfx::access::ReadWrite);
            gfx::set_buffer(10, clipmap_gpu.get_world_probe_list(), gfx::access::ReadWrite);
            gfx::set_buffer(13, clipmap_gpu.get_world_probe_index(), gfx::access::ReadWrite);
            const float select_params[4] = {phase, float(budget), float(params.frame & schedule_frame_mask), 0.0f};
            gfx::set_uniform(select_program_.u_gi_world_probe_select, select_params);
            gfx::set_uniform(select_program_.u_gi_world_probe_window, window, global_sdf_clipmap::level_count);
            if(bgfx::isValid(params.indirect))
            {
                gfx::dispatch_indirect(pass.id, select_program_.program->native_handle(), params.indirect, entry, 1);
            }
            else
            {
                const auto groups = get_select_dispatch_groups(entry);
                gfx::dispatch(pass.id, select_program_.program->native_handle(), groups.x, groups.y, groups.z);
            }
            select_program_.program->end();
        };
        dispatch_select(select_phase_histogram, gi_quiescence_gate_pass::entry_probe_select_histogram);
        dispatch_select(select_phase_threshold, gi_quiescence_gate_pass::entry_probe_select_threshold);
        dispatch_select(select_phase_emit, gi_quiescence_gate_pass::entry_probe_select_emit);
    }
    {
        gfx::render_pass pass("GI/World Probe Trace");
        trace_program_.program->begin();
        gfx::set_texture(trace_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
        gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
        gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
        gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
        gfx::set_texture(trace_program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
        // ReadWrite: the trace folds each window's sample into the texel's running mean.
        gfx::set_image(5,
                       clipmap_gpu.get_world_probe_radiance()->native_handle(),
                       0,
                       gfx::access::ReadWrite,
                       gfx::texture_format::RGBA16F);
        // Stage 8 for the cells buffer and 6 for the vis-memo image: OpenGL has eight image
        // units (0-7), buffers may bind past them (cs_gi_world_probe_trace.sc).
        gfx::set_buffer(8, clipmap_gpu.get_world_probe_cells(), gfx::access::ReadWrite);
        gfx::set_buffer(7, clipmap_gpu.get_world_probe_counts(), gfx::access::ReadWrite);
        // The bounce vis-memo for its statistics slice alone: the probe census the waste
        // ledger reads back on demand (GI_STATS_PROBES_*). Stage 8 is free in this kernel.
        const auto& vis_memo = clipmap_gpu.get_bounce_vis_memo();
        if(vis_memo && vis_memo->is_valid())
        {
            gfx::set_image_3d(6, vis_memo->native_handle(), 0, gfx::access::ReadWrite, gfx::texture_format::R32U);
        }
        // The window index's R2 offset in double (a float(frame) product loses the jitter
        // over a long session); the fast window advances windows four times faster.
        const double window_index =
            std::floor(double(params.frame) * double(strata_per_frame) / double(gi::GI_WORLD_PROBE_WINDOW));
        // z = the jitter/mean enable (the settings knob); 0 keeps texel centres at write-through.
        // w = the editor census (run_params::census).
        const float jitter[4] = {float(std::fmod(0.754877666 * window_index, 1.0)),
                                 float(std::fmod(0.569840291 * window_index, 1.0)),
                                 params.jitter_directions ? 1.0f : 0.0f,
                                 params.census ? 1.0f : 0.0f};
        gfx::set_uniform(trace_program_.u_gi_world_probe_jitter, jitter);
        gfx::set_texture(trace_program_.s_light_voxels, 10, clipmap_gpu.get_light_voxel_texture());
        gfx::set_texture(trace_program_.s_world_probe_irradiance_seed,
                         11,
                         clipmap_gpu.get_world_probe_irradiance());
        float seed_atlas[4] = {};
        std::memcpy(seed_atlas, clipmap_gpu.get_world_probe_atlas_params(), sizeof(seed_atlas));
        seed_atlas[2] = float(strata_per_frame);
        gfx::set_uniform(trace_program_.u_gi_world_probe_seed_atlas, seed_atlas);
        gfx::set_buffer(12, surface_cache.get_grid_buffer(), gfx::access::Read);
        // The sparse index: the trace refreshes the relocation lane once per probe window.
        gfx::set_buffer(13, clipmap_gpu.get_world_probe_index(), gfx::access::ReadWrite);
        // The scheduler's list and state (plan item 2.1).
        gfx::set_buffer(9, clipmap_gpu.get_world_probe_list(), gfx::access::Read);
        gfx::set_buffer(15, clipmap_gpu.get_world_probe_select(), gfx::access::Read);
        gfx::set_texture(trace_program_.s_gi_env_sh, 14, env_sh);
        const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                     float(atlas.get_atlas_voxel_dim()),
                                     float(instances.size()),
                                     float(surface_cache.get_emitters().size())};
        gfx::set_uniform(trace_program_.u_sdf_params, sdf_params);
        gfx::set_uniform(trace_program_.u_sdf_grid_params, surface_cache.get_grid_params(), gi::GI_SDF_GRID_PARAMS_VEC4);
        gfx::set_uniform(trace_program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
        gfx::set_uniform(trace_program_.u_sdf_clipmap_levels,
                         clipmap_gpu.get_level_params(),
                         global_sdf_clipmap::level_count);
        const float light_voxel_params[4] = {float(clipmap_gpu.get_attr_resolution()), 0.0f, 0.0f, 1.0f};
        gfx::set_uniform(trace_program_.u_gi_light_voxel_params, light_voxel_params);
        gfx::set_uniform(trace_program_.u_gi_world_probe_params, probe_params);
        gfx::set_uniform(trace_program_.u_gi_world_probe_window, window, global_sdf_clipmap::level_count);
        if(bgfx::isValid(params.indirect))
        {
            gfx::dispatch_indirect(pass.id,
                                   trace_program_.program->native_handle(),
                                   params.indirect,
                                   params.indirect_entry_trace,
                                   1);
        }
        else
        {
            const auto groups = get_trace_dispatch_groups();
            gfx::dispatch(pass.id, trace_program_.program->native_handle(), groups.x, groups.y, groups.z);
        }
        trace_program_.program->end();
    }
    {
        gfx::render_pass pass("GI/World Probe Convolve");
        convolve_program_.program->begin();
        gfx::set_texture(convolve_program_.s_world_probe_radiance,
                         11,
                         clipmap_gpu.get_world_probe_radiance());
        // The cell ids, for the free-slot skip (most of the sparse pool is free).
        gfx::set_buffer(7, clipmap_gpu.get_world_probe_cells(), gfx::access::Read);
        // The scheduler's list and state: exactly the probes the trace refreshed (plan item 2.1).
        gfx::set_buffer(9, clipmap_gpu.get_world_probe_list(), gfx::access::Read);
        gfx::set_buffer(10, clipmap_gpu.get_world_probe_select(), gfx::access::Read);
        gfx::set_image(5,
                       clipmap_gpu.get_world_probe_irradiance()->native_handle(),
                       0,
                       gfx::access::Write,
                       gfx::texture_format::RGBA16F);
        gfx::set_image(6,
                       clipmap_gpu.get_world_probe_depth()->native_handle(),
                       0,
                       gfx::access::Write,
                       gfx::texture_format::RG16F);
        gfx::set_uniform(convolve_program_.u_gi_world_probe_params, probe_params);
        if(bgfx::isValid(params.indirect))
        {
            gfx::dispatch_indirect(pass.id,
                                   convolve_program_.program->native_handle(),
                                   params.indirect,
                                   params.indirect_entry_convolve,
                                   1);
        }
        else
        {
            const auto groups = get_convolve_dispatch_groups();
            gfx::dispatch(pass.id, convolve_program_.program->native_handle(), groups.x, groups.y, groups.z);
        }
        convolve_program_.program->end();
    }
    return true;
}

} // namespace unravel
