#pragma once

#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>

namespace unravel
{

namespace shadow
{
class shadowmap_generator;
}

/**
 * @brief Lights the cascade's surface voxels with direct lighting and traced shadows
 *        (GI v2 plan 3.2).
 *
 * One thread per surface-list entry, a quarter of the list per frame
 * (GI_LIGHT_VOXEL_UPDATE_DENOM), radiance written straight to the light volume - no temporal
 * state, because direct lighting with traced shadows is deterministic and the stochastic
 * machinery belongs to the world probes. This replaces the unbudgeted whole-table sweep the
 * radiance-hash update pass performed: cost is proportional to resident SURFACE, not to table
 * capacity, and bounded by the rotation denominator.
 */
class gi_light_voxel_pass
{
public:
    struct run_params
    {
        surface_cache_system* surface_cache = nullptr;
        surface_cache_view* view_cache = nullptr;
        uint32_t frame = 0;
        /// The world-probe windows are centred here; the bounce term must agree with the trace.
        math::vec3 camera_position{0.0f};
        /// The cage-visibility variance gate (gi_resolve_pass::settings), carried in
        /// u_gi_world_probe_params.w for the bounce term's world-probe reads.
        float probe_visibility_variance_gate = gi::GI_WORLD_PROBE_CAGE_VIS_VARIANCE_GATE;
        /// The sun's CSM generator, when the scene has a shadow-casting directional light with
        /// maps rendered this frame. Cascade 0 then answers sun visibility for voxels it
        /// covers - the traced field cannot thread openings the bake fattened shut, and the
        /// map is the raster's own mesh-exact answer. Null keeps sun shadows fully traced.
        const shadow::shadowmap_generator* sun_shadows = nullptr;
        /// The sun's slot in the GPU light buffer, so the shader applies the map to exactly
        /// the light it was rendered for. Negative disables the tier.
        int sun_light_index = -1;
        /// The camera's TAA-unjittered view-projection - the frustum cascade 0 was fitted to
        /// this frame. The tier answers only for receivers inside that frustum slice
        /// ([near, cascade-0 far] in view depth, inside the FOV): the map's crop footprint
        /// (a bounding sphere of the slice) extends metres BEHIND and beside the camera, and
        /// receivers there project into the map but are outside its contract - the raster
        /// never samples them, and they measured LIT for sealed-room faces behind the camera
        /// (the first-look glow: the room lights up while the camera faces away, then decays
        /// when it turns). Outside the slice the traced field answers, as it does past the
        /// map's edge.
        math::mat4 camera_view_proj{1.0f};
        /// Diagnostic: dispatch the SUN-TIER debug PROGRAM (cs_gi_light_voxels_debug.sc),
        /// which writes tier-attribution colors into the light volume instead of radiance
        /// (see GiDebugSunTierColor in gi_light_voxels_kernel.sh), for the sun_tiers debug
        /// view. A compiled variant selected here on the CPU, not a shader flag: two hunts
        /// lost to a runtime flag that left the CPU but never steered the kernel. Downstream
        /// GI consumers ingest the colors while this is set; the volume relights within its
        /// usual rotation once cleared.
        bool sun_tier_debug = false;
        /// Diagnostic: dispatch the VIS-MEMO debug PROGRAM
        /// (cs_gi_light_voxels_vis_memo_debug.sc), which paints the live bounce
        /// visibility-memo transaction per face (green hit / red miss / blue generation-0)
        /// instead of radiance, for the vis_memo debug view. Same compiled-variant
        /// discipline as sun_tier_debug; takes precedence over it when both are set.
        bool vis_memo_debug = false;
    };

    auto init(rtti::context& ctx) -> bool;

    auto run(gfx::render_view& rview, const run_params& params) -> bool;

    auto is_valid() const -> bool
    {
        return program_.is_valid();
    }

private:
    /// One-time "the program never became valid" diagnostic; see run().
    bool invalid_warning_emitted_ = false;
    /// One-time "debug variant requested but its program never built" diagnostic; see run().
    bool debug_invalid_warning_emitted_ = false;
    /// Last logged state of run_params::sun_tier_debug; see the flip log in run().
    bool sun_tier_debug_logged_ = false;
    /// One-time "vis-memo variant requested but its program never built" diagnostic.
    bool vis_memo_invalid_warning_emitted_ = false;
    /// Last logged state of run_params::vis_memo_debug; see the flip log in run().
    bool vis_memo_debug_logged_ = false;
    /// Last logged bounce vis-memo generation. Changes are logged: they are legitimate on
    /// scene edits and window scrolls, but a STREAM of them with a parked camera in a
    /// static scene means an invalidation tracker is churning and the memo can never hit -
    /// the CPU-side discriminator for the measured miss-every-rotation cost signature.
    uint32_t vis_memo_generation_logged_ = uint32_t(-1);
    /// Relight-EMA change tracking (u_gi_vis_memo_params.y): what the blend was last
    /// computed against, and how many write-through frames remain so every voxel's first
    /// relight after a change snaps (one full rotation); see the blend block in run().
    bool ema_history_valid_ = false;
    uint64_t ema_light_hash_ = 0;
    uint32_t ema_generation_ = uint32_t(-1);
    uint32_t ema_snap_frames_ = 0;

private:
    struct light_voxel_program : uniforms_cache
    {
        gpu_program::ptr program;
        /// The GI_SUN_TIER_DEBUG_VARIANT sibling; shares every uniform name with `program`
        /// (bgfx uniforms are name-global), so the cached handles below serve both. Optional:
        /// when it failed to build, run() keeps the radiance program and warns once.
        gpu_program::ptr debug_program;
        /// The GI_VIS_MEMO_DEBUG_VARIANT sibling, same arrangement.
        gpu_program::ptr vis_memo_debug_program;
        gfx::program::uniform_ptr u_gi_light_voxel_params;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_gpu_light_params;
        gfx::program::uniform_ptr u_gi_shadow_params;
        gfx::program::uniform_ptr u_gi_shadow_params2;
        gfx::program::uniform_ptr u_gi_light_voxel_camera;
        gfx::program::uniform_ptr u_gi_vis_memo_params;
        gfx::program::uniform_ptr u_gi_world_probe_params;
        gfx::program::uniform_ptr u_gi_world_probe_atlas;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_attr_albedo;
        gfx::program::uniform_ptr s_attr_emissive;
        gfx::program::uniform_ptr s_world_probe_irradiance;
        gfx::program::uniform_ptr s_world_probe_depth;
        gfx::program::uniform_ptr u_gi_sun_shadowmap_mtx;
        gfx::program::uniform_ptr u_gi_sun_shadowmap_params;
        gfx::program::uniform_ptr u_gi_sun_shadowmap_camera_vp;
        gfx::program::uniform_ptr u_gi_sun_shadowmap_slice;
        gfx::program::uniform_ptr s_gi_sun_shadowmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_sun_shadowmap_mtx, "u_gi_sun_shadowmap_mtx", gfx::uniform_type::Mat4);
            cache_uniform(program.get(),
                          u_gi_sun_shadowmap_camera_vp,
                          "u_gi_sun_shadowmap_camera_vp",
                          gfx::uniform_type::Mat4);
            cache_uniform(program.get(),
                          u_gi_sun_shadowmap_slice,
                          "u_gi_sun_shadowmap_slice",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_gi_sun_shadowmap_params,
                          "u_gi_sun_shadowmap_params",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_gi_sun_shadowmap, "s_gi_sun_shadowmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_gi_light_voxel_camera, "u_gi_light_voxel_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_vis_memo_params, "u_gi_vis_memo_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_params, "u_gi_world_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_atlas, "u_gi_world_probe_atlas", gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          s_world_probe_irradiance,
                          "s_world_probe_irradiance",
                          gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_world_probe_depth, "s_world_probe_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(),
                          u_gi_light_voxel_params,
                          "u_gi_light_voxel_params",
                          gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(),
                          u_sdf_clipmap_levels,
                          "u_sdf_clipmap_levels",
                          gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_gpu_light_params, "u_gpu_light_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_shadow_params, "u_gi_shadow_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_shadow_params2, "u_gi_shadow_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_attr_albedo, "s_attr_albedo", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_attr_emissive, "s_attr_emissive", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } program_;
};

} // namespace unravel
