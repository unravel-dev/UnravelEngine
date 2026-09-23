#include "gi_light_voxel_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/shadow.h>

#include <graphics/graphics.h>

#include <algorithm>
#include <cstring>

static_assert(unravel::gi_light_voxel_pass::sun_cascade_count == unravel::shadow::ShadowMapRenderTargets::Count,
              "the sun tier's matrix array must hold one matrix per shadow cascade");

namespace unravel
{
namespace
{
/// Must match NUM_THREADS in gi_light_voxels_kernel.sh (both compiled variants share it).
constexpr uint32_t light_voxel_group_size = 64u;
} // namespace

auto gi_light_voxel_pass::get_dispatch_groups(const surface_cache_view& view_cache)
    -> gi_quiescence_gate_pass::dispatch_groups
{
    // One thread per entry due THIS frame: the 4-frame rotation is folded into the launch
    // (the kernel maps thread id -> entry = denom * id + phase), so the X extent covers a
    // quarter of the capacity, and the level rides Y so the kernel never divides. This is the
    // CPU's upper bound: on the indirect path the quiescence gate narrows X to the largest
    // level's GPU-side count (cs_gi_quiescence_gate.sc); the kernel's early-out beyond each
    // level's count remains for the smaller levels and for the direct fallback.
    const uint32_t attr_resolution = view_cache.get_clipmap_gpu().get_attr_resolution();
    const uint32_t capacity = attr_resolution * attr_resolution * attr_resolution;
    const uint32_t rotation_slice =
        (capacity + uint32_t(gi::GI_LIGHT_VOXEL_UPDATE_DENOM) - 1u) / uint32_t(gi::GI_LIGHT_VOXEL_UPDATE_DENOM);
    gi_quiescence_gate_pass::dispatch_groups groups;
    groups.x = (rotation_slice + light_voxel_group_size - 1u) / light_voxel_group_size;
    groups.y = global_sdf_clipmap::level_count;
    groups.z = 1u;
    return groups;
}

auto gi_light_voxel_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_light_voxels.sc");
    auto cs_debug = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_light_voxels_debug.sc");
    auto cs_vis_memo_debug =
        am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_light_voxels_vis_memo_debug.sc");
    program_.cache_uniforms();
    program_.program = std::make_unique<gpu_program>(cs);
    program_.debug_program = std::make_unique<gpu_program>(cs_debug);
    program_.vis_memo_debug_program = std::make_unique<gpu_program>(cs_vis_memo_debug);
    // Optional: without it the quiescence gate falls back to its fixed settle.
    auto cs_stats = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_light_voxel_stats.sc");
    stats_program_ = std::make_unique<gpu_program>(cs_stats);
    return program_.is_valid();
}

auto gi_light_voxel_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Light Voxels");
    if(!program_.is_valid())
    {
        // Loudly, once: a program that failed to create leaves the light volume unwritten -
        // every downstream view then paints allocation garbage with a perfectly clean log,
        // which is how a whole backend's GI stayed silently broken (measured: Linux GL).
        if(!invalid_warning_emitted_)
        {
            invalid_warning_emitted_ = true;
            APPLOG_WARNING("[SurfaceCache] Light voxel compute program is not valid on this "
                           "backend; the light volume will never be written.");
        }
        return false;
    }
    if(!params.surface_cache || !params.view_cache)
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
    const auto& light_buffer = surface_cache.get_light_buffer();
    if(!clipmap_gpu.is_valid() || !clipmap_gpu.get_light_voxel_texture())
    {
        return false;
    }
    auto& atlas = surface_cache.get_atlas();
    const auto& instances = surface_cache.get_instances();
    // The sun-tier debug write is a compiled VARIANT of the kernel, selected here - program
    // choice is the one switch no uniform stomp or stale constant buffer can undo (the
    // runtime-flag approach died twice with every CPU-side link verified). Falls back to the
    // radiance program, loudly, if the variant never built: the view then shows all-magenta
    // provenance, which is at least an honest "the write is not happening".
    const bool want_debug = params.sun_tier_debug;
    const bool debug_available = program_.debug_program && program_.debug_program->is_valid();
    if(want_debug && !debug_available && !debug_invalid_warning_emitted_)
    {
        debug_invalid_warning_emitted_ = true;
        APPLOG_WARNING("[SurfaceCache] Sun-tier debug program is not valid on this backend; "
                       "the sun_tiers view will keep showing radiance provenance (magenta).");
    }
    const bool want_vis_memo_debug = params.vis_memo_debug;
    const bool vis_memo_debug_available =
        program_.vis_memo_debug_program && program_.vis_memo_debug_program->is_valid();
    if(want_vis_memo_debug && !vis_memo_debug_available && !vis_memo_invalid_warning_emitted_)
    {
        vis_memo_invalid_warning_emitted_ = true;
        APPLOG_WARNING("[SurfaceCache] Vis-memo debug program is not valid on this backend; "
                       "the vis_memo view will keep showing radiance provenance (magenta).");
    }
    auto& active_program = (want_vis_memo_debug && vis_memo_debug_available)
                               ? *program_.vis_memo_debug_program
                               : ((want_debug && debug_available) ? *program_.debug_program : *program_.program);
    gfx::render_pass pass("GI/Light Voxels");
    active_program.begin();
    gfx::set_texture(program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
    gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
    gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
    gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
    gfx::set_texture(program_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    if(light_buffer.is_valid())
    {
        gfx::set_buffer(5, light_buffer.get_buffer(), gfx::access::Read);
    }
    // The surface list (header cursors + entries in one buffer) sits at stage 10 - a buffer
    // tolerates the high stages, which keeps stage 6 free as an IMAGE unit: OpenGL guarantees
    // only eight image units (bindings 0-7) and this pass binds two 3D images.
    gfx::set_buffer(10, clipmap_gpu.get_surface_list_buffer(), gfx::access::Read);
    gfx::set_texture(program_.s_attr_albedo, 8, clipmap_gpu.get_attr_albedo_texture());
    gfx::set_texture(program_.s_attr_emissive, 9, clipmap_gpu.get_attr_emissive_texture());
    // ReadWrite: the radiance store folds each relight into a per-voxel EMA, reading the
    // texel's own previous value (see the store in gi_light_voxels_kernel.sh).
    gfx::set_image_3d(7,
                      clipmap_gpu.get_light_voxel_texture()->native_handle(),
                      0,
                      gfx::access::ReadWrite,
                      gfx::texture_format::RGBA16F);
    gfx::set_buffer(12, surface_cache.get_grid_buffer(), gfx::access::Read);
    // Stage 13: the sparse world-probe index, read-write - the bounce requests the level-0
    // cages it reads (gi_world_probes.sh). Bound whenever it exists; the ready flag in
    // u_gi_world_probe_params gates the reads.
    if(clipmap_gpu.has_world_probes())
    {
        gfx::set_buffer(13, clipmap_gpu.get_world_probe_index(), gfx::access::ReadWrite);
    }
    const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                 float(atlas.get_atlas_voxel_dim()),
                                 float(instances.size()),
                                 float(surface_cache.get_emitters().size())};
    gfx::set_uniform(program_.u_sdf_params, sdf_params);
    gfx::set_uniform(program_.u_sdf_grid_params, surface_cache.get_grid_params(), gi::GI_SDF_GRID_PARAMS_VEC4);
    gfx::set_uniform(program_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    gfx::set_uniform(program_.u_sdf_clipmap_levels,
                     clipmap_gpu.get_level_params(),
                     global_sdf_clipmap::level_count);
    const float light_params[4] = {light_buffer.is_valid() ? float(light_buffer.get_light_count()) : 0.0f,
                                   0.0f,
                                   0.0f,
                                   0.0f};
    gfx::set_uniform(program_.u_gpu_light_params, light_params);
    // Shadow tracing wholly owned by gi_constants (Phase 8): no settings, one source.
    const float shadow_params[4] = {float(gi::GI_SHADOW_DISTANCE),
                                    float(gi::GI_SHADOW_NORMAL_BIAS_VOXELS),
                                    float(gi::GI_RELIGHT_SHADOW_NEAR_FIELD),
                                    float(gi::GI_TRACE_MAX_STEPS)};
    gfx::set_uniform(program_.u_gi_shadow_params, shadow_params);
    // z = the editor census (run_params::census; u_gi_stats_census in gi_lighting.sh).
    const float shadow_params2[4] = {float(gi::GI_SHADOW_SURFACE_BIAS),
                                     float(gi::GI_SHADOW_RELAXATION),
                                     params.census ? 1.0f : 0.0f,
                                     float(gi::GI_SHADOW_RAY_START_VOXELS)};
    gfx::set_uniform(program_.u_gi_shadow_params2, shadow_params2);
    // Sun shadow-map tier (see gi_lighting.sh): the sun's CSM cascades answer sun visibility
    // for the voxels their crops cover; the traced field remains the answer beyond them.
    // VSM packs moment pairs rather than a depth, so it falls back to tracing entirely.
    // The cascades reach the kernel as the layers of ONE texture array (its only free stage):
    // the generator's maps are blitted into it here, on the frames this gated pass runs -
    // nothing is re-rendered and the shadow pass's per-cascade caster sets are untouched.
    float sun_params[4] = {-1.0f, 0.0f, 0.0f, 0.0f};
    bool sun_maps_bound = false;
    if(params.sun_shadows != nullptr && params.sun_light_index >= 0 &&
       params.sun_shadows->get_depth_type() == shadow::PackDepth::RGBA)
    {
        const auto& shadows = *params.sun_shadows;
        const uint16_t size = shadows.get_shadow_map_size();
        uint8_t splits = std::min<uint8_t>(shadows.get_num_splits(), uint8_t(shadow::ShadowMapRenderTargets::Count));
        for(uint8_t split = 0; split < splits; ++split)
        {
            if(!bgfx::isValid(shadows.get_rt_texture(split)))
            {
                splits = split;
                break;
            }
        }
        if(splits > 0 && size > 0)
        {
            if(!sun_cascades_ || !sun_cascades_->is_valid() || sun_cascades_size_ != size)
            {
                sun_cascades_ = std::make_shared<gfx::texture>(size,
                                                               size,
                                                               false,
                                                               uint16_t(shadow::ShadowMapRenderTargets::Count),
                                                               gfx::texture_format::R32F,
                                                               BGFX_TEXTURE_BLIT_DST);
                sun_cascades_size_ = size;
            }
            float matrices[shadow::ShadowMapRenderTargets::Count * 16] = {};
            float slice_params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float bias_params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            // Cascade 0's constant receiver bias, scaled per split by its texel size - the
            // raster's own cascadeScale.
            const float bias0 = shadows.get_shadow_map_bias();
            const float texel0 = std::max(shadows.get_cascade_texel_world(0), 1e-6f);
            for(uint8_t split = 0; split < splits; ++split)
            {
                gfx::blit(pass.id,
                          sun_cascades_->native_handle(),
                          0,
                          0,
                          0,
                          split,
                          shadows.get_rt_texture(split),
                          0,
                          0,
                          0,
                          0,
                          size,
                          size,
                          1);
                std::memcpy(matrices + split * 16, shadows.get_shadow_map_matrix(split), sizeof(float) * 16);
                slice_params[split] = shadows.get_cascade_far_distance(split);
                bias_params[split] = bias0 * shadows.get_cascade_texel_world(split) / texel0;
            }
            gfx::set_uniform(program_.u_gi_sun_shadowmap_mtx, matrices, shadow::ShadowMapRenderTargets::Count);
            // The maps' CONTRACT: the cascades are fitted to the camera's frustum slices and
            // the raster samples them for nothing outside the frustum. A crop footprint (a
            // bounding sphere of its slice) reaches metres behind and beside the camera, and
            // receivers there project INTO a map while nothing about the fit is contracted
            // for them - measured as LIT verdicts for sealed-room faces behind the camera
            // (the room lights up while the camera faces away and decays when it turns: the
            // first-look glow). The kernel declines outside the frustum and the traced
            // field answers, exactly as it does past the maps' edges.
            gfx::set_uniform(program_.u_gi_sun_shadowmap_camera_vp, params.camera_view_proj);
            gfx::set_uniform(program_.u_gi_sun_shadowmap_slice, slice_params);
            gfx::set_uniform(program_.u_gi_sun_shadowmap_bias, bias_params);
            sun_params[0] = float(params.sun_light_index);
            sun_params[1] = float(splits);
            // One filter footprint inside the edge, mirroring the lighting shader's cascade
            // selection bounds, so a clamped tap never answers for a position outside a crop.
            sun_params[2] = 0.01f;
            // World -> stored depth, so the kernel can cover its slope allowance in depth.
            sun_params[3] = shadows.get_shadow_map_world_to_depth();
            // Raw float depth: point sampled and clamped, as the lighting pass binds it.
            gfx::set_texture(14,
                             program_.s_gi_sun_shadowmap->native_handle(),
                             sun_cascades_->native_handle(),
                             BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
            sun_maps_bound = true;
        }
    }
    if(!sun_maps_bound)
    {
        // The stage must hold an ARRAY on backends that validate bindings; the tier is
        // disabled by the negative index, so the content is never read.
        if(!sun_cascades_ || !sun_cascades_->is_valid())
        {
            sun_cascades_ = std::make_shared<gfx::texture>(1,
                                                           1,
                                                           false,
                                                           uint16_t(shadow::ShadowMapRenderTargets::Count),
                                                           gfx::texture_format::R32F,
                                                           BGFX_TEXTURE_BLIT_DST);
            sun_cascades_size_ = 1;
        }
        gfx::set_texture(14, program_.s_gi_sun_shadowmap->native_handle(), sun_cascades_->native_handle());
    }
    gfx::set_uniform(program_.u_gi_sun_shadowmap_params, sun_params);
    const uint32_t attr_resolution = clipmap_gpu.get_attr_resolution();
    // y and camera.w mirror the debug state as TELEMETRY only - the kernel decides by which
    // program was compiled in (see the variant note in gi_light_voxels_kernel.sh). The lanes
    // stay so a GPU-debugger capture can finally answer whether these uniforms ever arrive,
    // the question two hunts could not settle from the CPU side.
    // The frame lane carries only the rotation phase, never the raw frame count: past 2^24 a
    // float frame quantises to multiples of 2 and then 4, freezing `frame % 4` on one phase -
    // three quarters of the surface set would silently stop relighting after ~77 h at 60 fps.
    const float voxel_params[4] = {float(attr_resolution),
                                   params.sun_tier_debug ? 1.0f : 0.0f,
                                   float(params.frame % gi::GI_LIGHT_VOXEL_UPDATE_DENOM),
                                   1.0f};
    gfx::set_uniform(program_.u_gi_light_voxel_params, voxel_params);
    // Logged on every flip: the one question a screenshot cannot answer is whether the flag
    // reached the pass at all.
    if(params.sun_tier_debug != sun_tier_debug_logged_)
    {
        sun_tier_debug_logged_ = params.sun_tier_debug;
        APPLOG_INFO("[SurfaceCache] Sun-tier debug write {} (frame {}, program {}).",
                    params.sun_tier_debug ? "ENABLED" : "disabled",
                    params.frame,
                    params.sun_tier_debug ? (debug_available ? "debug variant" : "MISSING - radiance fallback")
                                          : "radiance");
    }
    const float camera[4] = {params.camera_position.x,
                             params.camera_position.y,
                             params.camera_position.z,
                             params.sun_tier_debug ? 1.0f : 0.0f};
    gfx::set_uniform(program_.u_gi_light_voxel_camera, camera);
    // Bounce inputs: LAST frame's world probes. Absent (wrong resolution, first frames), the
    // ready flag stays zero and the shader takes direct light alone.
    const auto& view_clipmap = view_cache.get_clipmap();
    const bool probes_ready = clipmap_gpu.has_world_probes();
    const float base_spacing =
        view_clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
    const float probe_params[4] = {base_spacing,
                                   float(params.frame),
                                   probes_ready ? 1.0f : 0.0f,
                                   params.probe_visibility_variance_gate};
    gfx::set_uniform(program_.u_gi_world_probe_params, probe_params);
    // The bounce's cage-visibility memo (stage 6, read+write - gfx::set_image_3d for the GL
    // layered-binding rule). The generation refresh compares the clipmap's COMPOSED content
    // epoch and the per-level probe-window cells against what the memo was stamped under;
    // 0 means the memo is not yet seeded and the kernel takes the plain gated-march path.
    // Composed, not target: the verdicts are marched against the composed field, and during
    // an edit drag the target epoch churns every frame while the field only changes when
    // the coalescing throttle lets a recompose land - keying on the target re-marched every
    // relight against an unchanged field.
    uint32_t vis_memo_generation = 0;
    const auto& vis_memo = clipmap_gpu.get_bounce_vis_memo();
    if(vis_memo && vis_memo->is_valid())
    {
        vis_memo_generation =
            view_cache.get_clipmap_gpu_mutable().refresh_bounce_vis_generation(
                view_clipmap.get_composed_content_epoch(), params.camera_position, base_spacing);
        gfx::set_image_3d(6, vis_memo->native_handle(), 0, gfx::access::ReadWrite, gfx::texture_format::R32U);
    }
    // SEGMENT-LOCAL KEEP AGE (u_gi_vis_memo_params.z; the kernel's GiSegmentTouchesBox note).
    // A stale word keeps the corners no changed region touched only while every field
    // change since its stamp is one the memo's regions describe. A placement change is; a
    // level scroll is not - the entering and leaving slabs change which level answers the
    // march's samples and no region names them - so a generation that lands after any
    // level's composed origin moved admits no keep, and the age a word may be counts up
    // from there. The origins are compared every frame because a scroll whose fingerprint
    // held still lands WITHOUT a bump; it is charged to the next one. A probe-window cell
    // crossing counts as a scroll as well: the crossing bumps the generation on the frame
    // the camera moved, while the composed origin follows one frame later, and on that one
    // frame every stale word would otherwise keep its verdicts against probe slots the
    // window has just re-assigned (measured 2026-09-10: 24k corners and 17k face verdicts
    // kept on the jump frame, a different one-rotation transient than stock's march, and
    // the quiescence gate froze its residual in the emissive cell).
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
    {
        const math::vec3& origin = view_clipmap.get_level(level).origin;
        if(origin != vis_memo_keep_origins_[level])
        {
            vis_memo_keep_origins_[level] = origin;
            vis_memo_scroll_pending_ = true;
        }
        const float spacing = base_spacing * float(1u << level);
        const std::array<int32_t, 3> cell = {int32_t(std::floor(params.camera_position.x / spacing)),
                                             int32_t(std::floor(params.camera_position.y / spacing)),
                                             int32_t(std::floor(params.camera_position.z / spacing))};
        if(cell != vis_memo_keep_cells_[level])
        {
            vis_memo_keep_cells_[level] = cell;
            vis_memo_scroll_pending_ = true;
        }
    }
    if(vis_memo_generation != vis_memo_keep_generation_)
    {
        vis_memo_keep_age_ = vis_memo_scroll_pending_
                                 ? 0u
                                 : std::min(vis_memo_keep_age_ + 1u, uint32_t(gi::GI_VIS_MEMO_KEEP_MAX_AGE));
        vis_memo_scroll_pending_ = false;
        vis_memo_keep_generation_ = vis_memo_generation;
    }
    // The memo's region count rides lane w; past the uniform budget the kernel cannot see
    // every change, so the lane goes negative and no stale word keeps anything this frame.
    const size_t memo_region_total = surface_cache.get_vis_memo_region_total();
    const float memo_region_lane = memo_region_total > size_t(gi::GI_TEMPORAL_DIRTY_MAX_BOUNDS)
                                       ? -1.0f
                                       : float(memo_region_total);
    // Every change is logged: bumps are legitimate on edits and window scrolls, but a stream
    // of these with a parked camera in a static scene means an invalidation tracker churns
    // and the memo can never hit - the CPU-side discriminator for a miss-shaped cost.
    if(vis_memo_generation != vis_memo_generation_logged_)
    {
        // APPLOG_INFO("[SurfaceCache] Bounce vis-memo generation {} -> {} (frame {}, epoch {}).",
        //             vis_memo_generation_logged_,
        //             vis_memo_generation,
        //             params.frame,
        //             view_clipmap.get_content_epoch());
        vis_memo_generation_logged_ = vis_memo_generation;
    }
    if(params.vis_memo_debug != vis_memo_debug_logged_)
    {
        vis_memo_debug_logged_ = params.vis_memo_debug;
        // APPLOG_INFO("[SurfaceCache] Vis-memo debug write {} (frame {}, program {}).",
        //             params.vis_memo_debug ? "ENABLED" : "disabled",
        //             params.frame,
        //             params.vis_memo_debug
        //                 ? (vis_memo_debug_available ? "vis-memo variant" : "MISSING - radiance fallback")
        //                 : "radiance");
    }
    // RELIGHT EMA blend (u_gi_vis_memo_params.y; the radiance store in the kernel). A GLOBAL
    // lighting change (gpu_light_buffer::get_global_revision: a directional light added,
    // removed or past the 4x brightness rule) or an EDIT of the field (the clipmap's edited
    // content epoch) holds the blend at write-through for one FULL rotation: only a quarter
    // of the surface set relights per frame, so every voxel's first relight after the change
    // has to snap. A LOCAL light change needs no snap: its influence region rides the dirty
    // regions, and inside one the kernel writes through per voxel (history_trusted).
    // CAMERA TRAVEL DOES NOT SNAP (plan item 1.3): the attribute pass clears a scrolled-in
    // slot to alpha 0 or seeds it at GI_LIGHT_VOXEL_SEED_ALPHA, and the kernel never blends a
    // history below alpha 0.5 - keyed on the vis-memo generation, the snap discarded the whole
    // volume's integration on every probe-cell crossing and scroll compose while moving.
    // Debug variants overwrite the volume with attribution colors, so the rotation after they
    // clear snaps too. Generation 0 means the change tracker is unavailable - the EMA stays
    // off rather than integrating over undetected changes.
    const bool radiance_write = !((want_vis_memo_debug && vis_memo_debug_available) ||
                                  (want_debug && debug_available));
    const uint64_t light_revision = light_buffer.is_valid() ? light_buffer.get_global_revision() : 0u;
    const uint64_t edited_epoch = view_clipmap.get_edited_content_epoch();
    // A placement-local edit (surface_cache_system::is_placement_local_edit) is written through per
    // voxel inside its dirty regions (history_trusted in the kernel), not scene-wide.
    const bool edited = edited_epoch != ema_edited_epoch_ && !surface_cache.is_placement_local_edit();
    if(!ema_history_valid_ || light_revision != ema_light_revision_ || edited || !radiance_write)
    {
        ema_snap_frames_ = uint32_t(gi::GI_LIGHT_VOXEL_UPDATE_DENOM);
    }
    ema_history_valid_ = radiance_write;
    ema_light_revision_ = light_revision;
    ema_edited_epoch_ = edited_epoch;
    float ema_blend = 1.0f;
    if(ema_snap_frames_ > 0u)
    {
        --ema_snap_frames_;
    }
    else if(vis_memo_generation != 0u)
    {
        ema_blend = float(gi::GI_LIGHT_VOXEL_EMA_BLEND);
    }
    const float vis_memo_params[4] = {float(vis_memo_generation),
                                      ema_blend,
                                      float(vis_memo_keep_age_),
                                      memo_region_lane};
    gfx::set_uniform(program_.u_gi_vis_memo_params, vis_memo_params);
    // DIRTY REGIONS (gi_dirty_regions.sh): inside one the radiance store writes through
    // instead of folding into the EMA - the bounce it integrates there is the light a moved
    // placement left, and blending it in at 1/8 per relight kept a moved emissive's pool in
    // the volume for a rotation window after the temporal's hold had already expired.
    {
        constexpr uint32_t max_regions = uint32_t(gi::GI_TEMPORAL_DIRTY_MAX_BOUNDS);
        float dirty_bounds[max_regions * 2u * 4u] = {};
        const uint32_t dirty_count = surface_cache.pack_dirty_regions(dirty_bounds, max_regions);
        const float dirty_margin =
            params.view_cache->get_clipmap().get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
        const float dirty_params[4] = {float(dirty_count), math::max(dirty_margin, 1e-3f), 0.0f, 0.0f};
        gfx::set_uniform(program_.u_gi_temporal_dirty, dirty_params);
        gfx::set_uniform(program_.u_gi_temporal_bounds, dirty_bounds, uint16_t(2u * max_regions));
        // The vis-memo's own list (the raw field bounds over its shorter hold): the segment
        // keep in the kernel reads these, count in u_gi_vis_memo_params.w.
        float memo_bounds[max_regions * 2u * 4u] = {};
        surface_cache.pack_vis_memo_regions(memo_bounds, max_regions);
        gfx::set_uniform(program_.u_gi_vis_memo_bounds, memo_bounds, uint16_t(2u * max_regions));
    }
    if(probes_ready)
    {
        gfx::set_texture(program_.s_world_probe_irradiance, 11, clipmap_gpu.get_world_probe_irradiance());
        gfx::set_texture(program_.s_world_probe_depth, 15, clipmap_gpu.get_world_probe_depth());
        gfx::set_uniform(program_.u_gi_world_probe_atlas, clipmap_gpu.get_world_probe_atlas_params());
    }
    else
    {
        // The probe samplers are ACTIVE regardless of the ready flag (a uniform branch
        // eliminates nothing), and OpenGL fails the whole dispatch when an unbound sampler's
        // unit-0 default collides with the 3D atlas bound there - which blacked out the light
        // voxels and with them the entire GI chain on that backend. The ready flag in
        // u_gi_world_probe_params gates what is actually read.
        const auto black = default_textures::get().black_texture();
        gfx::set_texture(program_.s_world_probe_irradiance, 11, black);
        gfx::set_texture(program_.s_world_probe_depth, 15, black);
    }
    if(bgfx::isValid(params.indirect))
    {
        // The GPU gate already decided: this entry holds either the counts
        // get_dispatch_groups derived or zeros. A zero-group dispatch is a no-op on every
        // backend, which is the whole point - the skip costs no CPU-GPU sync to discover.
        gfx::dispatch_indirect(pass.id, active_program.native_handle(), params.indirect, params.indirect_entry, 1);
    }
    else
    {
        const auto groups = get_dispatch_groups(view_cache);
        gfx::dispatch(pass.id, active_program.native_handle(), groups.x, groups.y, groups.z);
    }
    active_program.end();
    if(params.collect_stats)
    {
        collect_relight_stats(vis_memo, attr_resolution);
    }
    return true;
}

void gi_light_voxel_pass::collect_relight_stats(const gfx::texture::ptr& vis_memo, uint32_t attr_resolution)
{
    if(!stats_program_ || !stats_program_->is_valid() || !vis_memo || !vis_memo->is_valid())
    {
        return;
    }
    const auto width = static_cast<uint16_t>(global_sdf_clipmap::level_count);
    const auto height = static_cast<uint16_t>(gi_quiescence_gate_pass::stats_snapshot::quantity_count);
    if(!stats_texture_ || !stats_texture_->is_valid())
    {
        stats_texture_ = std::make_shared<gfx::texture>(width,
                                                        height,
                                                        false,
                                                        1,
                                                        gfx::texture_format::R32U,
                                                        BGFX_TEXTURE_COMPUTE_WRITE);
        for(auto& slot : stats_slots_)
        {
            slot.texture = std::make_shared<gfx::texture>(width,
                                                          height,
                                                          false,
                                                          1,
                                                          gfx::texture_format::R32U,
                                                          BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
            slot.pending = false;
        }
    }
    if(stats_source_ != vis_memo.get())
    {
        stats_source_ = vis_memo.get();
        stats_primed_ = false;
    }
    // Completed readbacks, oldest first (the cursor is the oldest slot in flight).
    const uint32_t frame = gfx::get_render_frame();
    for(uint32_t i = 0; i < stats_slots_.size(); ++i)
    {
        auto& slot = stats_slots_[(stats_slot_cursor_ + i) % stats_slots_.size()];
        if(!slot.pending || frame < slot.ready_frame)
        {
            continue;
        }
        slot.pending = false;
        if(!stats_primed_)
        {
            // The slice's first content is whatever the allocation held.
            stats_primed_ = true;
            continue;
        }
        // Rows GI_STATS_RELIGHT_CHANGE, _FACES and _RISE of the slice (gi_light_voxels.sh).
        constexpr uint32_t rise_row = 2u;
        float change = 0.0f;
        float faces = 0.0f;
        float rise = 0.0f;
        for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
        {
            change += float(slot.data[level]) / float(gi::GI_QUIESCENCE_STATS_SCALE);
            faces += float(slot.data[global_sdf_clipmap::level_count + level]);
            rise += float(slot.data[rise_row * global_sdf_clipmap::level_count + level]) /
                    float(gi::GI_QUIESCENCE_STATS_SCALE);
        }
        ++relight_sample_.index;
        relight_sample_.mean_change = faces > 0.0f ? change / faces : 0.0f;
        relight_sample_.mean_drift = faces > 0.0f ? (2.0f * rise - change) / faces : 0.0f;
    }
    // Copy and zero this frame's sums (a view of its own: a blit executes before the
    // dispatches of its view, so the staging copy needs the next one).
    gfx::render_pass copy_pass("GI/Light Voxel Stats");
    stats_program_->begin();
    gfx::set_image_3d(0, vis_memo->native_handle(), 0, gfx::access::ReadWrite, gfx::texture_format::R32U);
    gfx::set_image(1, stats_texture_->native_handle(), 0, gfx::access::Write, gfx::texture_format::R32U);
    // y = 1: on this path the copy is the drain (no GPU gate zeroes the slice).
    const float voxel_params[4] = {float(attr_resolution), 1.0f, 0.0f, 0.0f};
    gfx::set_uniform(program_.u_gi_light_voxel_params, voxel_params);
    gfx::dispatch(copy_pass.id, stats_program_->native_handle(), 1, 1, 1);
    stats_program_->end();
    auto& slot = stats_slots_[stats_slot_cursor_];
    if(slot.pending)
    {
        // Every staging texture in flight: this frame's sample is dropped, not awaited.
        return;
    }
    stats_slot_cursor_ = (stats_slot_cursor_ + 1) % uint32_t(stats_slots_.size());
    gfx::render_pass readback_pass("GI/Light Voxel Stats Readback");
    gfx::blit(readback_pass.id, slot.texture->native_handle(), 0, 0, stats_texture_->native_handle(), 0, 0, width, height);
    slot.ready_frame = gfx::read_texture(slot.texture->native_handle(), slot.data.data());
    slot.pending = true;
}

} // namespace unravel
