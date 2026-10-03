#pragma once

#include <engine/engine_export.h>

#include <engine/rendering/gi/lumen_scene.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/tonemapping_pass.h>

#include <context/context.hpp>
#include <graphics/graphics.h>

#include <array>
#include <memory>
#include <vector>

namespace unravel
{

class camera;
class surface_cache_system;
class surface_cache_view;

/**
 * @brief Lumen's surface cache, GPU side: the physical atlases (albedo, normal, emissive, depth, direct,
 *        indirect, final lighting with the depth in alpha), the capture atlas, the packed scene table
 *        (cards | page table | instances), the object grid, the capture-to-atlas copy, the card lighting
 *        (direct + radiosity + final combine) and the debug views.
 *
 * Per frame: update() runs the CPU scene and uploads the table; the deferred pipeline then rasterizes this
 * frame's captures into get_capture_target() (it owns the G-buffer program and the material binding), each
 * with compute_capture_view(); copy_captures() resamples the lighting of reallocated cards' previous pages and
 * moves the captures into the physical atlases, and light() relights the pages the scene's lighting scheduler
 * picked (lumen_scene::schedule_lighting): direct lighting with the final combine, then radiosity.
 */
class lumen_surface_cache_pass
{
public:
    /// Experiment toggles (surface_cache_system::get_experiment_flags), above the gather's and the reflections'
    /// bits: each one changes one stage for an in-session A/B. Zero in production.
    enum experiment : uint32_t
    {
        ///< Reallocated cards start unlit, as new ones do, instead of inheriting their previous pages' lighting.
        experiment_no_lighting_resample = 1u << 10u,
        ///< Global-SDF hits sample the cards within UE's absolute tolerance: 3 voxel extents of UE's own layout
        ///< (50 m level 0) instead of this layout's.
        experiment_ue_card_tolerance = 1u << 20u,
        ///< The Lumen Scene views tint hits in uncovered global-SDF space magenta (diagnostic).
        experiment_show_sdf_coverage = 1u << 26u,
        ///< Resident cards keep their resolution while the viewer moves (diagnostic).
        experiment_hold_card_resolution = 1u << 28u,
        ///< The card lighting stops updating: no direct lighting or radiosity dispatches (diagnostic).
        experiment_freeze_card_lighting = 1u << 30u,
    };

    /// The capture's world -> clip transform (orthographic, depth 0 at the card front, 1 at its back).
    struct capture_view
    {
        math::mat4 view_proj{1.0f};
        ///< Sign of the mapping's linear determinant: two mappings of opposite sign wind triangles
        ///< oppositely, so the capture flips culling when it differs from the camera's.
        float orientation = 1.0f;
        ///< A point far in front of the card, for the G-buffer shader's camera-distance dither.
        math::vec3 far_eye{0.0f};
    };

    struct lighting_inputs
    {
        const surface_cache_system* gi_scene = nullptr;
        const surface_cache_view* view_cache = nullptr;
        ///< The environment's radiance SH: the sky radiosity rays see on a miss.
        gfx::texture::ptr environment_sh;
        ///< The view's exposure, which scales Lumen's MaxRayIntensity into cached units.
        float view_exposure = 1.0f;
    };

    struct debug_params
    {
        gfx::frame_buffer* output = nullptr;
        const camera* cam = nullptr;
        const surface_cache_system* gi_scene = nullptr;
        const surface_cache_view* view_cache = nullptr;
        ///< 0 = Lumen Scene (final lighting), 1 = card atlas, 2 = coverage, 3 = Lumen Scene albedo,
        ///< 4 = Surface Cache (missing coverage marked), 5 = the object grid card stages at global SDF hits,
        ///< 6 = Lumen Scene direct lighting, 7 = Lumen Scene indirect lighting.
        int mode = 0;
        ///< The view's exposure, applied to the lighting views before the tone map.
        float exposure = 1.0f;
        ///< The lit image's tone mapping operator, which the lighting views go through as UE's do.
        tonemapping_method tonemapping = tonemapping_method::none;
    };

    auto init(rtti::context& ctx) -> bool;

    /// CPU update from this frame's GI instances and the view the lighting is scheduled for; uploads the scene table.
    void update(const surface_cache_system& gi_scene, const math::vec3& view_origin, const math::frustum& view_frustum);

    auto get_scene() const -> const lumen_scene&
    {
        return scene_;
    }

    auto get_capture_target() const -> const gfx::frame_buffer::ptr&
    {
        return capture_target_;
    }

    auto compute_capture_view(const lumen_scene::capture& cap) const -> capture_view;

    /// Copies this frame's rasterized captures into the physical atlases, a reallocated card's pages with the
    /// lighting of its previous allocation.
    void copy_captures();

    /// The object grid for re-composed levels, then direct lighting with the final combine and radiosity for the
    /// pages the lighting scheduler picked.
    void light(const lighting_inputs& inputs);

    auto run_debug(const debug_params& params) -> bool;

    auto is_ready() const -> bool;

    /// The resources a surface cache sampler binds (lumen_surface_cache.sh) and its uniform values.
    auto get_scene_buffer() const -> bgfx::DynamicVertexBufferHandle
    {
        return scene_buffer_;
    }

    auto get_final_atlas() const -> const gfx::texture::ptr&
    {
        return final_atlas_;
    }

    auto get_object_grid() const -> const gfx::texture::ptr&
    {
        return object_grid_;
    }

    auto get_surface_cache_params() const -> math::vec4;
    /// u_lumen_object_grid_levels (4 vec4) and u_lumen_object_grid_params.
    auto get_object_grid_levels() const -> const std::array<math::vec4, 4>&
    {
        return object_grid_levels_;
    }
    auto get_object_grid_params() const -> math::vec4;

    /// True once the object grid exists and the cards have been lit.
    auto has_lighting() const -> bool;

    /// Binds the surface cache for a global-SDF hit sampler (lumen_surface_cache.sh with the object grid) at
    /// the given stages and sets u_lumen_hit_lighting: hits read the cards when @p enabled and the cache is
    /// lit, the light voxels otherwise.
    void bind_for_sampling(uint8_t scene_stage, uint8_t final_stage, uint8_t grid_stage, bool enabled) const;

private:
    struct uniforms : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_card_copy;
        gfx::program::uniform_ptr u_lumen_card_lighting;
        gfx::program::uniform_ptr u_lumen_radiosity;
        gfx::program::uniform_ptr u_lumen_surface_cache;
        gfx::program::uniform_ptr u_lumen_object_grid;
        gfx::program::uniform_ptr u_lumen_object_grid_origin;
        gfx::program::uniform_ptr u_lumen_object_grid_levels;
        gfx::program::uniform_ptr u_lumen_object_grid_params;
        gfx::program::uniform_ptr u_lumen_hit_lighting;
        gfx::program::uniform_ptr u_lumen_debug;
        gfx::program::uniform_ptr u_lumen_debug2;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_gpu_light_params;
        gfx::program::uniform_ptr s_lumen_capture_rt0;
        gfx::program::uniform_ptr s_lumen_capture_rt1;
        gfx::program::uniform_ptr s_lumen_capture_rt2;
        gfx::program::uniform_ptr s_lumen_capture_depth;
        gfx::program::uniform_ptr s_lumen_card_albedo;
        gfx::program::uniform_ptr s_lumen_card_normal;
        gfx::program::uniform_ptr s_lumen_card_emissive;
        gfx::program::uniform_ptr s_lumen_card_depth;
        gfx::program::uniform_ptr s_lumen_card_direct;
        gfx::program::uniform_ptr s_lumen_card_final;
        gfx::program::uniform_ptr s_lumen_object_grid;
        gfx::program::uniform_ptr s_lumen_env_sh;
        gfx::program::uniform_ptr s_lumen_radiosity_trace;
        gfx::program::uniform_ptr s_lumen_radiosity_sh_r;
        gfx::program::uniform_ptr s_lumen_radiosity_sh_g;
        gfx::program::uniform_ptr s_lumen_radiosity_sh_b;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_sdf_clipmap_coverage;
        gfx::program::uniform_ptr s_lumen_card_indirect;
        gfx::program::uniform_ptr s_lumen_radiosity_frames;
        gfx::program::uniform_ptr s_lumen_resample_direct;
        gfx::program::uniform_ptr s_lumen_resample_indirect;
        gfx::program::uniform_ptr s_lumen_resample_frames;
    };

    /// The object grid's state for one clipmap level: built for this origin and content.
    struct object_grid_level
    {
        math::vec3 origin{0.0f};
        float voxel_size = 0.0f;
        uint64_t content_fingerprint = 0;
        /// surface_cache_system::get_instance_order_hash when built: the cells store instance indices.
        uint64_t instance_order = 0;
        bool is_built = false;
    };

    void create_targets();
    void upload_scene_table();
    void build_copy_tiles();
    /// UE ResampleLightingHistoryToCardCaptureAtlasCS: the lighting of reallocated cards' previous pages into the
    /// capture-sized resample targets, before the copy overwrites the physical atlases.
    void resample_lighting();
    void build_light_tiles();
    void bind_sdf_instances(const surface_cache_system& gi_scene) const;
    void update_object_grid(const surface_cache_system& gi_scene, const surface_cache_view& view_cache);
    void dispatch_direct(const lighting_inputs& inputs, uint32_t first, uint32_t count);
    void dispatch_radiosity(const lighting_inputs& inputs, uint32_t first, uint32_t count);

    uniforms uniforms_;
    gpu_program::ptr copy_program_;
    gpu_program::ptr resample_program_;
    gpu_program::ptr lighting_program_;
    gpu_program::ptr object_grid_program_;
    gpu_program::ptr radiosity_trace_program_;
    gpu_program::ptr radiosity_sh_program_;
    gpu_program::ptr radiosity_integrate_program_;
    std::unique_ptr<gpu_program> debug_program_;
    lumen_scene scene_;
    std::vector<lumen_scene::source> sources_;
    gfx::frame_buffer::ptr capture_target_;
    gfx::texture::ptr albedo_atlas_;
    gfx::texture::ptr normal_atlas_;
    gfx::texture::ptr emissive_atlas_;
    gfx::texture::ptr depth_atlas_;
    gfx::texture::ptr direct_atlas_;
    gfx::texture::ptr indirect_atlas_;
    gfx::texture::ptr final_atlas_;
    gfx::texture::ptr radiosity_trace_atlas_;
    gfx::texture::ptr radiosity_sh_r_;
    gfx::texture::ptr radiosity_sh_g_;
    gfx::texture::ptr radiosity_sh_b_;
    gfx::texture::ptr radiosity_frames_;
    ///< Capture-atlas sized: the resampled direct and indirect lighting, and the update count per 8x8 tile.
    gfx::texture::ptr resample_direct_;
    gfx::texture::ptr resample_indirect_;
    gfx::texture::ptr resample_frames_;
    gfx::texture::ptr object_grid_;
    ///< A 1x1x1 stand-in bound while no object grid exists (a 3D stage must stay 3D).
    gfx::texture::ptr object_grid_dummy_;
    bool is_lit_ = false;
    uint32_t object_grid_resolution_ = 0;
    ///< Scale of the global-SDF hits' card sampling bias (u_lumen_object_grid_params.y; 1 = UE's rule on this layout).
    float card_bias_scale_ = 1.0f;
    /// This frame's experiment toggles (surface_cache_system::get_experiment_flags).
    uint32_t experiment_flags_ = 0;
    std::array<object_grid_level, 4> object_grid_built_{};
    std::array<math::vec4, 4> object_grid_levels_{};
    bgfx::DynamicVertexBufferHandle scene_buffer_{bgfx::kInvalidHandle};
    bgfx::DynamicVertexBufferHandle copy_tile_buffer_{bgfx::kInvalidHandle};
    ///< lumen_scene::get_resample_table, read as a scene table by the resample.
    bgfx::DynamicVertexBufferHandle resample_table_buffer_{bgfx::kInvalidHandle};
    bgfx::DynamicVertexBufferHandle light_tile_buffer_{bgfx::kInvalidHandle};
    uint32_t page_table_base_ = 0;
    uint32_t instance_table_base_ = 0;
    uint32_t copy_tile_count_ = 0;
    ///< This frame's captures include pages of a reallocated card.
    bool has_resample_ = false;
    ///< The light tile buffer holds the direct lighting's tiles, then the radiosity's from radiosity_tile_base_.
    uint32_t direct_tile_count_ = 0;
    uint32_t radiosity_tile_base_ = 0;
    uint32_t radiosity_tile_count_ = 0;
    uint32_t update_count_ = 0;
    /// Resident cards reallocated since the last stats line.
    uint32_t reallocated_since_log_ = 0;
};

} // namespace unravel
