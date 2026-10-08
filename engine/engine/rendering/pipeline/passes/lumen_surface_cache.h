#pragma once

#include <engine/engine_export.h>

#include <engine/rendering/gi/gi_project_settings.h>
#include <engine/rendering/gi/lumen_scene.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/lumen_pass_common.h>
#include <engine/rendering/pipeline/passes/lumen_surface_cache_feedback.h>
#include <engine/rendering/pipeline/passes/tonemapping_pass.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <graphics/graphics.h>

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace unravel
{

class camera;
class lumen_object_grid;
class surface_cache_system;
class surface_cache_view;

/**
 * @brief Lumen's surface cache, GPU side: the physical atlases (albedo, normal, emissive, depth, direct,
 *        indirect, final lighting with the depth in alpha), the capture atlas, the packed scene table
 *        (cards | page table | instances), the capture-to-atlas copy, the card lighting (direct + radiosity + final
 *        combine) and the debug views.
 *
 * One per process, shared by every camera (acquire): the cards describe the world, which surface_cache_system walks
 * once per frame for every camera. The cameras running the GI hold it (add_user / remove_user); the last one leaving
 * frees its atlases. Each frame the first camera to claim_update() updates it for every viewer seen this frame or the
 * last (register_viewer) and rasterizes the captures; the scheduled pages go to their nearest viewer, and each camera
 * lights its own with its global distance field and object grid (lumen_object_grid), so a page's shadows and bounces
 * are traced where a distance field covers it. Pages a camera left unlit (it stopped rendering) are due again.
 *
 * Per frame: update() runs the CPU scene and uploads the table; the deferred pipeline then rasterizes this
 * frame's captures into get_capture_target() (it owns the G-buffer program and the material binding), each
 * with compute_capture_view(); copy_captures() resamples the lighting of reallocated cards' previous pages and
 * moves the captures into the physical atlases, and light() relights the pages the scene's lighting scheduler
 * picked (lumen_scene::schedule_lighting): direct lighting with the final combine, then radiosity.
 */
class lumen_surface_cache
{
public:
    lumen_surface_cache() = default;
    ~lumen_surface_cache();
    lumen_surface_cache(const lumen_surface_cache&) = delete;
    auto operator=(const lumen_surface_cache&) -> lumen_surface_cache& = delete;

    /// The process's surface cache, created and initialized on first use; it lives while a caller holds it.
    static auto acquire(rtti::context& ctx) -> std::shared_ptr<lumen_surface_cache>;

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
        ///< lumen_scene::set_card_residency_without_group_gate.
        experiment_card_residency_without_group_gate = 1u << 19u,
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

    /// The sun's cloud shadow the card direct lighting applies to directional lights (cloud_shadow.sh): the map
    /// (null = none), u_cloudShadow, u_cloudShadow2 and the map's signature (atmospheric_pass_perez::cloud_shadow_result;
    /// a new one relights every page a directional light reaches).
    struct cloud_shadow
    {
        gfx::texture::ptr map;
        math::vec4 placement{0.0f};
        math::vec4 layer{0.0f};
        uint64_t signature = 0;
    };

    /// This frame's cloud shadow, before update().
    void set_cloud_shadow(const cloud_shadow& shadow)
    {
        cloud_shadow_ = shadow;
    }

    struct lighting_inputs
    {
        const surface_cache_system* gi_scene = nullptr;
        const surface_cache_view* view_cache = nullptr;
        ///< The lighting camera's object grid: the radiosity rays' hits read the cards through it.
        const lumen_object_grid* object_grid = nullptr;
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
        ///< The camera's object grid.
        const lumen_object_grid* object_grid = nullptr;
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

    /// A camera starts running the GI with this cache: the next update allocates the atlases if none exist.
    void add_user();
    /// A camera stops running the GI with it; the last one frees the atlases, the capture target and the cards.
    void remove_user();

    /// Records the viewer @p key (a camera) sees from this frame (@p frame), with its global distance field
    /// @p clipmap (null: none): update() serves every viewer recorded this frame or the last; older records go.
    void register_viewer(const void* key,
                         const lumen_scene::viewer& viewer,
                         const global_sdf_clipmap* clipmap,
                         uint64_t frame);
    /// Drops the viewer @p key (its camera stopped running the GI).
    void forget_viewer(const void* key);

    /// True for the first call of render frame @p frame: that camera updates, captures and lights the cache.
    auto claim_update(uint64_t frame) -> bool;

    /// CPU update from this frame's GI instances, the viewers of register_viewer (their distance fields moving change
    /// the cards' shadows), the updating camera's Lumen scene settings and the project's; uploads the scene table.
    /// Other atlas sizes in the project settings start the surface cache over at those sizes.
    void update(const surface_cache_system& gi_scene,
                uint64_t frame,
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

    /// Direct lighting with the final combine and radiosity for the pages the lighting scheduler gave viewer
    /// @p viewer_key, with its distance field and object grid (@p inputs).
    void light(const lighting_inputs& inputs, const void* viewer_key);

    auto run_debug(const debug_params& params) -> bool;

    auto is_ready() const -> bool;

    /// The atlases exist: created by the first update with a user.
    auto has_targets() const -> bool
    {
        return albedo_atlas_ != nullptr;
    }

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
    /// The radiosity probes' layout the SH atlases hold (the lighting camera's surface cache lighting quality).
    auto get_radiosity_layout() const -> const lumen_pass::radiosity_layout&
    {
        return radiosity_layout_;
    }
    /// lumen_scene::get_visualized_pages.
    void get_visualized_pages(std::vector<math::vec4>& out) const
    {
        scene_.get_visualized_pages(out);
    }

    auto get_surface_cache_params() const -> math::vec4;
    /// The scale of the global-SDF hits' card sampling bias (u_lumen_object_grid_params.y; 1 = UE's rule on this
    /// layout).
    auto get_card_bias_scale() const -> float
    {
        return card_bias_scale_;
    }

    /// True once the cards have been lit.
    auto has_lighting() const -> bool;

    /// The reflections' surface cache feedback, null while it cannot run (its programs) or the hi-res pages are off
    /// (gi_project_settings::hi_res_reflection_pages, experiment_no_hi_res_pages).
    auto get_feedback() -> lumen_surface_cache_feedback*;

    /// lumen_scene::get_card_index_revision of this frame's tables: the card indices the feedback names.
    auto get_card_index_revision() const -> uint64_t
    {
        return scene_.get_card_index_revision();
    }

    /// Binds the surface cache for a global-SDF hit sampler (lumen_surface_cache.sh with the object grid) at
    /// the given stages, the camera's @p object_grid at @p grid_stage, and sets u_lumen_hit_lighting: hits read the
    /// cards when @p enabled and the cache is lit, and are black otherwise.
    void bind_for_sampling(uint8_t scene_stage,
                           uint8_t final_stage,
                           uint8_t grid_stage,
                           bool enabled,
                           const lumen_object_grid& object_grid) const;

private:
    struct uniforms : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_card_copy;
        gfx::program::uniform_ptr u_lumen_card_lighting;
        gfx::program::uniform_ptr s_cloudShadow;
        gfx::program::uniform_ptr u_cloudShadow;
        gfx::program::uniform_ptr u_cloudShadow2;
        gfx::program::uniform_ptr u_lumen_radiosity;
        gfx::program::uniform_ptr u_lumen_surface_cache;
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
        gfx::program::uniform_ptr s_lumen_env_sh;
        gfx::program::uniform_ptr s_lumen_radiosity_trace;
        gfx::program::uniform_ptr s_lumen_radiosity_sh_r;
        gfx::program::uniform_ptr s_lumen_radiosity_sh_g;
        gfx::program::uniform_ptr s_lumen_radiosity_sh_b;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_sdf_clipmap_coverage;
        gfx::program::uniform_ptr s_sdf_clipmap_mip;
        gfx::program::uniform_ptr s_lumen_card_indirect;
        gfx::program::uniform_ptr s_lumen_radiosity_frames;
        gfx::program::uniform_ptr s_lumen_resample_direct;
        gfx::program::uniform_ptr s_lumen_resample_indirect;
        gfx::program::uniform_ptr s_lumen_capture_rt3;
    };

    /// A viewer's latest record (register_viewer).
    struct viewer_record
    {
        lumen_scene::viewer viewer;
        const global_sdf_clipmap* clipmap = nullptr;
        uint64_t frame = 0;
    };

    /// A viewer's share of the light tile words: tiles [first, first + count).
    struct tile_range
    {
        uint32_t first = 0;
        uint32_t count = 0;
    };

    /**
     * @brief What changed the card direct lighting since the last update (lumen_scene::invalidate_direct_lighting):
     *        the lights' records and the global distance field the shadow rays march - a level's partial recompose its
     *        boxes, a full one or a moved level everything, another camera's field everything. The field, not the
     *        instance list, decides: a level takes an instance's move on its own cadence, and a page relit before that
     *        would keep the old shadow.
     */
    auto collect_direct_lighting_changes(const surface_cache_system& gi_scene,
                                         const std::vector<const global_sdf_clipmap*>& clipmaps)
        -> lumen_scene::direct_lighting_changes;

    /// Starts the frame's schedule over the viewers recorded this frame (@p frame) or the last (older records go):
    /// their keys in scheduled_viewers_, their distance fields in @p clipmaps; returns the viewers.
    auto schedule_viewers(uint64_t frame, std::vector<const global_sdf_clipmap*>& clipmaps)
        -> std::vector<lumen_scene::viewer>;

    void create_targets();
    /// Frees the atlases and the capture target and starts the card scene over (its pages lived in the atlases).
    void release_targets();
    void upload_scene_table();
    void build_copy_tiles();
    /// UE ResampleLightingHistoryToCardCaptureAtlasCS: the lighting of reallocated cards' previous pages into the
    /// capture-sized resample targets, before the copy overwrites the physical atlases.
    void resample_lighting();
    void build_light_tiles();
    void bind_sdf_instances(const surface_cache_system& gi_scene) const;
    /// The texture a debug mode reads per card sample at stage 13 (fs_lumen_scene_debug.sc s_lumen_debug_values).
    auto get_debug_values(debug_mode mode) const -> const gfx::texture::ptr&;
    void dispatch_direct(const lighting_inputs& inputs, uint32_t first, uint32_t count);
    void dispatch_radiosity(const lighting_inputs& inputs, uint32_t first, uint32_t count);
    /// The radiosity's trace and SH atlases for @p layout: (atlas / spacing) x the rays per axis, atlas / spacing.
    void ensure_radiosity_targets(const lumen_pass::radiosity_layout& layout);

    uniforms uniforms_;
    gpu_program::ptr copy_program_;
    gpu_program::ptr resample_program_;
    gpu_program::ptr lighting_program_;
    gpu_program::ptr radiosity_trace_program_;
    gpu_program::ptr radiosity_sh_program_;
    gpu_program::ptr radiosity_integrate_program_;
    std::unique_ptr<gpu_program> debug_program_;
    lumen_scene scene_;
    lumen_surface_cache_feedback feedback_;
    ///< The last feedback readback's elements (scratch).
    std::vector<lumen_scene::feedback_element> feedback_elements_;
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
    bool is_lit_ = false;
    ///< Scale of the global-SDF hits' card sampling bias (u_lumen_object_grid_params.y; 1 = UE's rule on this layout).
    float card_bias_scale_ = 1.0f;
    /// This frame's experiment toggles (surface_cache_system::get_experiment_flags).
    uint64_t experiment_flags_ = 0;
    /// The last update's hi-res reflection pages switch: the project setting, unless an experiment turns them off.
    bool uses_hi_res_pages_ = false;
    /// The previous update's lights and each viewer's distance field's level recomposes, for
    /// collect_direct_lighting_changes; false before the first update.
    std::vector<float> previous_lights_;
    std::unordered_map<const global_sdf_clipmap*, std::array<uint64_t, global_sdf_clipmap::level_count>>
        previous_compose_serials_;
    uint64_t previous_cloud_signature_ = 0;
    bool has_previous_lighting_inputs_ = false;
    /// This frame's cloud shadow (set_cloud_shadow).
    cloud_shadow cloud_shadow_{};
    /// The cameras running the GI with this cache (add_user / remove_user).
    uint32_t user_count_ = 0;
    /// The frame the last claim_update() succeeded in.
    uint64_t claimed_frame_ = ~0ull;
    /// Every camera's latest viewer (register_viewer).
    std::unordered_map<const void*, viewer_record> viewers_;
    /// The current schedule's viewers in lumen_scene's order (their keys) and which of them lit their pages.
    std::vector<const void*> scheduled_viewers_;
    std::vector<uint8_t> lit_viewers_;
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
    ///< The copy tile buffer's page records come first, its tile words from this float4 (lumen_tile_records.sh).
    uint32_t copy_tile_words_base_ = 0;
    ///< This frame's captures include pages of a reallocated card.
    bool has_resample_ = false;
    ///< The light tile buffer holds the page records, then the tile words from light_tile_words_base_: per viewer,
    ///< the direct lighting's tiles, then per viewer the radiosity's.
    std::vector<tile_range> direct_ranges_;
    std::vector<tile_range> radiosity_ranges_;
    uint32_t light_tile_words_base_ = 0;
    ///< Then the radiosity's per-page update indices (lumen_scene::get_page_radiosity_indices), four per float4 from
    ///< this float4; -1 when the probes stop at their page's edge (experiment_page_bound_radiosity).
    int32_t radiosity_page_words_base_ = -1;
    uint32_t update_count_ = 0;
    /// Resident cards reallocated since the last stats line.
    uint32_t reallocated_since_log_ = 0;
};

} // namespace unravel
