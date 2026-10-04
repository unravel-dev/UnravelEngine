#pragma once

#include <engine/engine_export.h>

#include <engine/rendering/gi/gi_project_settings.h>
#include <engine/rendering/gi/lumen_scene.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/lumen_pass_common.h>
#include <engine/rendering/pipeline/passes/tonemapping_pass.h>

#include <base/basetypes.hpp>
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

    /// The capture's world -> clip transform (orthographic, depth 0 at the card front, 1 at its back), placed in its
    /// tile of the capture atlas, which one pass draws every capture into.
    struct capture_view
    {
        math::mat4 view_proj{1.0f};
        ///< Sign of the mapping's linear determinant: two mappings of opposite sign wind triangles
        ///< oppositely, so the capture flips culling when it differs from the camera's.
        float orientation = 1.0f;
        ///< A point far in front of the card, for the G-buffer shader's camera-distance dither.
        math::vec3 far_eye{0.0f};
        ///< The draw's scissor in bgfx's terms: the capture's tile.
        irect32_t scissor{};
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

    /// The debug views of fs_lumen_scene_debug.sc: UE's r.Lumen.Visualize scene modes (its value in brackets) and the
    /// card atlas, coverage and object grid views.
    enum class debug_mode : int
    {
        ///< [3] the cards' final lighting at the global distance field's hits.
        lumen_scene = 0,
        ///< The physical albedo atlas, fitted to the viewport.
        card_atlas = 1,
        ///< The placements' mesh distance fields, coloured by how their cards cover each hit.
        card_coverage = 2,
        ///< [8]
        albedo = 3,
        ///< [5] pink / yellow where the cards miss a hit.
        surface_cache = 4,
        ///< The object grid's card stages at the global distance field's hits.
        object_grid = 5,
        ///< [12]
        direct_lighting = 6,
        ///< [13]
        indirect_lighting = 7,
        ///< [4] the Lumen Scene traced as far as the reflections trace.
        reflection_view = 9,
        ///< [6]
        geometry_normals = 10,
        ///< [9]
        normals = 11,
        ///< [10]
        emissive = 12,
        ///< [11]
        card_weights = 13,
        ///< [16]
        direct_lighting_updates = 14,
        ///< [17]
        indirect_lighting_updates = 15,
        ///< [24]
        radiosity_frames = 16,
    };

    struct debug_params
    {
        gfx::frame_buffer* output = nullptr;
        const camera* cam = nullptr;
        const surface_cache_system* gi_scene = nullptr;
        const surface_cache_view* view_cache = nullptr;
        debug_mode mode = debug_mode::lumen_scene;
        ///< How far the camera rays trace, in metres.
        float max_trace_distance = 200.0f;
        ///< The output pixels the view fills (UE's overview tiles); empty = all of it.
        irect32_t tile{};
        ///< The environment's radiance SH: what a ray that hits nothing shows (black while null).
        gfx::texture::ptr environment_sh;
        ///< The view's exposure, applied to the lighting views before the tone map.
        float exposure = 1.0f;
        ///< The lit image's tone mapping operator, which the views go through as UE's do.
        tonemapping_method tonemapping = tonemapping_method::none;
    };

    auto init(rtti::context& ctx) -> bool;

    /// CPU update from this frame's GI instances, the view the lighting is scheduled for, its Lumen scene settings
    /// and the project's; uploads the scene table. Other atlas sizes in the project settings start the surface cache
    /// over at those sizes.
    void update(const surface_cache_system& gi_scene,
                const math::vec3& view_origin,
                const math::frustum& view_frustum,
                const gi_settings::scene_settings& view_settings,
                const gi_project_settings& project_settings);

    auto get_scene() const -> const lumen_scene&
    {
        return scene_;
    }

    /// lumen_scene::get_visualized_cards over this frame's placements.
    void get_visualized_cards(const math::vec3& view_origin,
                              float distance,
                              const math::frustum& view_frustum,
                              std::vector<lumen_scene::visualized_card>& out) const
    {
        scene_.get_visualized_cards(sources_, view_origin, distance, view_frustum, out);
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

    /// The card depth atlas (0 at a card's front, 1 = no surface) and the radiosity probes' SH atlases (R, G, B).
    auto get_depth_atlas() const -> const gfx::texture::ptr&
    {
        return depth_atlas_;
    }
    auto get_radiosity_sh(uint32_t channel) const -> const gfx::texture::ptr&
    {
        return channel == 0 ? radiosity_sh_r_ : channel == 1 ? radiosity_sh_g_ : radiosity_sh_b_;
    }
    /// The radiosity probes' layout the SH atlases hold (the view's surface cache lighting quality).
    auto get_radiosity_layout() const -> const lumen_pass::radiosity_layout&
    {
        return radiosity_layout_;
    }
    /// lumen_scene::get_visualized_pages.
    void get_visualized_pages(std::vector<math::vec4>& out) const
    {
        scene_.get_visualized_pages(out);
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
    /// lit, and are black otherwise.
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
        gfx::program::uniform_ptr u_lumen_debug3;
        gfx::program::uniform_ptr s_lumen_debug_values;
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
    /// The texture a debug mode reads per card sample at stage 13 (fs_lumen_scene_debug.sc s_lumen_debug_values).
    auto get_debug_values(debug_mode mode) const -> const gfx::texture::ptr&;
    void update_object_grid(const surface_cache_system& gi_scene, const surface_cache_view& view_cache);
    void dispatch_direct(const lighting_inputs& inputs, uint32_t first, uint32_t count);
    void dispatch_radiosity(const lighting_inputs& inputs, uint32_t first, uint32_t count);
    /// The radiosity's trace and SH atlases for @p layout: (atlas / spacing) x the rays per axis, atlas / spacing.
    void ensure_radiosity_targets(const lumen_pass::radiosity_layout& layout);

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
    /// The probes the radiosity atlases are laid out for (ensure_radiosity_targets).
    lumen_pass::radiosity_layout radiosity_layout_{};
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
    ///< lumen_scene::get_page_lighting_ages for the lighting updates views, and its scratch.
    bgfx::DynamicVertexBufferHandle page_ages_buffer_{bgfx::kInvalidHandle};
    std::vector<math::vec4> page_ages_;
    bgfx::DynamicVertexBufferHandle light_tile_buffer_{bgfx::kInvalidHandle};
    uint32_t page_table_base_ = 0;
    ///< The scene tables' revision (lumen_scene::get_tables_revision) scene_buffer_ holds.
    uint64_t uploaded_tables_revision_ = 0;
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
