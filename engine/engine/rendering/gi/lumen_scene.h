#pragma once

#include <engine/rendering/gi/gi_settings.h>
#include <engine/rendering/gi/lumen_mesh_cards.h>

#include <math/math.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace unravel
{

class mesh;
class material;

/**
 * @brief Lumen's surface cache, CPU side: card placements, per-card resolution, physical pages and
 *        the pages to capture (UE 5.8 FLumenSceneData, LumenSceneRendering.cpp, LumenMeshCards.cpp).
 *
 * Every GI instance with cards gets its cards placed in the world (oriented boxes). Each frame a card's
 * resolution follows its distance to the viewer exactly as Lumen chooses it (texel density 100 x half
 * extent / distance, at most 20 texels per metre and 512 texels, a power of two, aspect-biased per
 * axis; the density and the resolution limits scale with the view's surface cache resolution); a card
 * whose resolution changes is reallocated and recaptured, nearest first, within the capture budget.
 * Mips above 128 texels are paged into 128x128 physical pages, each holding 127 virtual texels plus
 * half-texel borders on interior edges; smaller mips share physical pages.
 *
 * A card is (re)allocated only when its whole mip has physical room beside everything resident and room in
 * this frame's capture atlas, so every page it maps is captured in the frame it is allocated; a reallocated
 * card's previous allocation is listed in the resample table, whose lighting the new pages inherit.
 *
 * The packed tables are rebuilt every frame; physical allocations persist, so captured content survives until its
 * card changes resolution or leaves. A placement's cards are placed once per transform, the per-card resolution
 * pass runs across the pool (UE r.LumenScene.ParallelUpdate), and the allocator answers space queries in constant
 * time from free counts (UE FLumenSurfaceCacheAllocator), so a full atlas costs nothing per request.
 *
 * schedule_lighting() picks the pages the card lighting updates each frame as Lumen does: per page and per
 * context (direct lighting, radiosity) a priority bucket from the frames since its last update and its speed
 * (distance and frustum), spent bucket by bucket against a tile budget per context that grows with the view's
 * lighting update speed. Direct lighting keeps no history and is a function of the page's capture, placement, the
 * lights and the occluders between them: a page lit once is skipped until one of those changes
 * (invalidate_direct_lighting), so the direct budget goes to the pages whose lighting changed.
 *
 * Beside its locked mip a resident card may map pages of one finer mip on demand (UE's unlocked hi-res pages,
 * LumenSceneRendering.cpp:1050-1135): the reflections' surface cache feedback (set_feedback) asks for the pages their
 * rays hit, ranked behind every locked request; a new hi-res page inherits the locked mip's lighting and is lit like
 * any page. Hi-res pages give way to locked allocations once idle for two frames and to other hi-res pages once idle
 * for 256 (UE EvictOldestAllocation). The reflections read a card through its row 5 page table, whose unmapped hi-res
 * pages fall back to the locked pages covering them; every other reader uses the locked mip.
 */
class lumen_scene
{
public:
    struct settings
    {
        ///< Physical atlas edge in texels (a multiple of the page size).
        uint32_t atlas_size = 4096;
        ///< Capture atlas edge in texels: the most texels captured per frame.
        uint32_t capture_atlas_size = 512;
        ///< r.LumenScene.SurfaceCache.CardCapturesPerFrame.
        uint32_t max_captures_per_frame = 300;
        ///< Cards farther than this are never resident, whatever the view distance: the reach of the global distance
        ///< field (half the extent of its last level), beyond which no ray hits them.
        float max_card_distance = 200.0f;
        ///< r.LumenScene.SurfaceCache.CardTexelDensityScale (texels per unit half-extent / distance).
        float texel_density_scale = 100.0f;
        ///< r.LumenScene.SurfaceCache.CardMaxTexelDensity, in texels per metre (0.2 per cm).
        float max_texel_density = 20.0f;
        ///< r.LumenScene.SurfaceCache.CardMaxResolution.
        uint32_t card_max_resolution = 512;
        ///< r.LumenScene.SurfaceCache.CardMinResolution (Epic).
        uint32_t card_min_resolution = 2;
        ///< r.LumenScene.SurfaceCache.MeshCardsMinSize, in metres: a card whose placed face is smaller than this
        ///< squared is never resident.
        float mesh_cards_min_size = 0.1f;
        ///< r.LumenScene.SurfaceCache.CardCaptureRefreshFraction: the share of the capture budget spent capturing
        ///< resident pages again, oldest first, so material changes reach the surface cache. 0 disables.
        float card_capture_refresh_fraction = 0.125f;
        ///< The scalability tier's scale on both card lighting update factors (UE High doubles Epic's).
        uint32_t lighting_update_factor_scale = 1;

        friend auto operator==(const settings& lhs, const settings& rhs) -> bool = default;
    };

    /// One placement's input: its cards (mesh space) and transform.
    struct source
    {
        ///< Stable across frames (entity, submesh, drawn instance).
        uint64_t identity = 0;
        ///< The placement's GI instance index this frame.
        uint32_t instance_index = 0;
        std::shared_ptr<const lumen_mesh_cards> cards;
        math::mat4 local_to_world{1.0f};
        ///< UE's Emissive Light Source (surface_cache_system::instance::is_emissive_light_source): its cards stay
        ///< resident down to one texel and down to a fifth of the minimum face area.
        bool is_emissive_light_source = false;
        ///< The material as captured (its identity and material::get_revision): a change queues the placement's pages
        ///< ahead of the refresh (UE recaptures a primitive whose render state changed).
        uint64_t material_key = 0;
    };

    /// One page to rasterize this frame.
    struct capture
    {
        ///< Index into this frame's card table.
        uint32_t card_index = 0;
        ///< Source placement (index into the update's sources).
        uint32_t source_index = 0;
        ///< The card UV rectangle the page covers, borders included (min.xy, max.xy).
        math::vec4 card_uv_rect{0.0f};
        ///< Texel rectangle in the capture atlas and in the physical atlas (same size).
        math::uvec2 capture_offset{0u};
        math::uvec2 atlas_offset{0u};
        math::uvec2 size{0u};
        ///< The card's previous allocation in the resample table (get_resample_table), whose lighting the page
        ///< inherits (UE bResampleLastLighting), or -1 when the card was not resident.
        int32_t resample_card = -1;
        ///< A refresh of the page in place: it keeps its lighting texel for texel rather than resampling it, so the
        ///< direct lighting, which a clean page does not relight, stays exact.
        bool keeps_lighting = false;
    };

    /// The two card lighting contexts, scheduled apart (UE's direct lighting and radiosity).
    enum lighting_context : uint32_t
    {
        lighting_direct = 0,
        lighting_radiosity = 1,
        lighting_context_count = 2,
    };

    /// A light the occluder changes of invalidate_direct_lighting() are tested against.
    struct light_reach
    {
        ///< A directional light: @ref direction points toward it and it reaches everywhere.
        bool is_directional = false;
        math::vec3 direction{0.0f, 1.0f, 0.0f};
        ///< A local light: its position and range.
        math::vec3 position{0.0f};
        float range = 0.0f;
    };

    /// What changed the card direct lighting since the last frame (lumen_surface_cache_pass collects it).
    struct direct_lighting_changes
    {
        ///< Every page's direct lighting may have changed (a directional light, the sky or sun, the global distance
        ///< field's levels moved, more changes than are worth placing).
        bool all = false;
        ///< World boxes in which the lighting changed (a local light's reach before and after it changed).
        std::vector<math::bbox> regions;
        ///< Occluders that moved, appeared or left: a page sees one that lies between it and one of @ref lights.
        std::vector<math::bbox> occluders;
        std::vector<light_reach> lights;
    };

    /// A page the card lighting updates this frame.
    struct lit_page
    {
        ///< Index into get_resident_pages().
        uint32_t resident_page = 0;
        ///< The page's update count in the context (UE's per-page temporal index): the radiosity probes' jitter.
        uint32_t update_index = 0;
        ///< The nearest viewer's index in schedule_lighting()'s list: the camera whose distance field lights the page.
        uint32_t viewer = 0;
        ///< The frame the page was lit in before this schedule (revoke_lighting restores it).
        uint64_t previous_lit_frame = 0;
    };

    /// A captured page of a resident card: what the lighting passes need to light its texels.
    struct resident_page
    {
        ///< Index into this frame's card table.
        uint32_t card_index = 0;
        ///< The card UV rectangle the page covers, borders included (min.xy, max.xy).
        math::vec4 card_uv_rect{0.0f};
        math::uvec2 atlas_offset{0u};
        math::uvec2 size{0u};
        ///< The page's mip in the page table (the locked mip's or the hi-res mip's): its span's first entry and the
        ///< mip's size in pages, through which the radiosity finds neighbouring pages.
        uint32_t mip_page_table_offset = 0;
        math::uvec2 mip_size_in_pages{1u};
    };

    /// One element of the reflections' surface cache feedback (UE FLumenSurfaceCacheFeedback, decoded): a card of the
    /// card table at the feedback's card index revision, the res level its hits want, the page they land on at that
    /// level (on the card's page grid without its aspect bias: 2^(level - 7) pages across from level 8, one below),
    /// and how many feedback samples landed there.
    struct feedback_element
    {
        uint32_t card_index = 0;
        uint32_t res_level = 0;
        math::uvec2 page{0u};
        uint32_t hits = 0;
    };

    /// A card's box placed in the world: unit axes and world half extents.
    struct placed_card
    {
        math::vec3 origin{0.0f};
        math::vec3 axis_x{1.0f, 0.0f, 0.0f};
        math::vec3 axis_y{0.0f, 1.0f, 0.0f};
        math::vec3 axis_z{0.0f, 0.0f, 1.0f};
        math::vec3 extent{0.0f};
    };

    /// A resident card as UE's card placement view draws it (LumenVisualize.cpp VisualizeCardPlacement).
    struct visualized_card
    {
        placed_card box;
        ///< The card's index among its mesh's cards (UE IndexInMeshCards).
        uint32_t index_in_mesh = 0;
        ///< UE's colour key: a hash of the card's mesh-space box and its index in the card table.
        uint32_t hash = 0;
        ///< The placement it belongs to (index into the update's sources).
        uint32_t source_index = 0;
        ///< The side of the mesh it faces (lumen_card::direction, UE AxisAlignedDirectionIndex).
        uint32_t direction = 0;
    };

    /// Whether a placement's cards (@p local_bounds at @p local_to_world) come within @p distance of @p view_origin and
    /// touch @p view_frustum: UE's visualize filters over a primitive group's world bounds.
    static auto is_visualized(const math::bbox& local_bounds,
                              const math::mat4& local_to_world,
                              const math::vec3& view_origin,
                              float distance,
                              const math::frustum& view_frustum) -> bool;

    /// Lumen::PhysicalPageSize.
    static constexpr uint32_t physical_page_size = 128;
    /// Lumen::MinResLevel / MaxResLevel / SubAllocationResLevel.
    static constexpr uint32_t min_res_level = 3;
    static constexpr uint32_t max_res_level = 11;
    static constexpr uint32_t sub_allocation_res_level = 7;
    /// float4s per card record, page-table entry and instance record in the packed tables.
    static constexpr uint32_t card_stride = 6;
    static constexpr uint32_t page_stride = 1;
    static constexpr uint32_t instance_stride = 1;

    void init(const settings& s);
    void reset();

    /**
     * @brief Applies new settings: in place when the atlases keep their sizes, otherwise by starting over.
     * @return Whether the scene started over, so every atlas has to be created again at the new sizes.
     */
    auto apply_settings(const settings& s) -> bool;

    /// A camera the cards serve: a card's distance is to the nearest viewer, a page near any viewer's frustum lights
    /// faster.
    struct viewer
    {
        math::vec3 origin{0.0f};
        math::frustum frustum{};
    };

    /**
     * @brief Places this frame's cards, chooses resolutions, (re)allocates and queues captures.
     *
     * @param sources        Placements with built cards (any order; identity keys the history).
     * @param instance_count This frame's GI instance count (the instance table's size).
     * @param viewers        The viewers the resolutions are chosen for (at least one).
     */
    void update(const std::vector<source>& sources, uint32_t instance_count, const std::vector<viewer>& viewers);
    /// update() for one viewer at @p view_origin.
    void update(const std::vector<source>& sources, uint32_t instance_count, const math::vec3& view_origin);

    /**
     * @brief Picks this frame's pages for each lighting context (UE LumenSceneLighting.usf:105-366), after update().
     *
     * A page's speed is 1 / (1 + its distance to the nearest of @p viewers / LUMEN_SCENE_LIGHTING_PRIORITY_DISTANCE),
     * doubled within LUMEN_SCENE_LIGHTING_FRUSTUM_MARGIN of any viewer's frustum; its bucket 15 - ceil(log2(4 x frames
     * since its last update x speed)), a never-lit page ranking as LUMEN_SCENE_LIGHTING_NEVER_LIT_FRAMES old. Buckets
     * are admitted most urgent first, whole pages, until the context's tile budget (compute_lighting_tile_budget) is
     * spent; the pages taken are stamped with this frame.
     */
    void schedule_lighting(const std::vector<viewer>& viewers);
    /// schedule_lighting() for one viewer.
    void schedule_lighting(const math::vec3& view_origin, const math::frustum& view_frustum);

    /// The last schedule_lighting()'s pages for @p viewer were not lit (its camera stopped rendering): they take back
    /// their previous lighting frame, and their direct lighting is due again. Before the next update().
    void revoke_lighting(uint32_t viewer);

    /**
     * @brief Marks the resident pages whose direct lighting @p changes reach for relighting, after update() and before
     *        schedule_lighting(). A page is also marked when it is captured or its placement moves; a page lit since
     *        it was last marked is skipped by the direct lighting, which keeps no history.
     */
    void invalidate_direct_lighting(const direct_lighting_changes& changes);

    /**
     * @brief Takes the reflections' surface cache feedback gathered while the card indices were at
     *        @p card_index_revision (UE FLumenSceneData::UpdateSurfaceCacheFeedback); feedback from other card indices
     *        is dropped. An element hit more than @p min_hits times keeps its card's hi-res page in use, or asks for it
     *        when the level it wants is above the card's locked level: the next update() maps it, ranked behind every
     *        locked request by 25 m plus 25 m x (1 - hits / @p sample_count).
     */
    void set_feedback(const std::vector<feedback_element>& elements,
                      uint64_t card_index_revision,
                      uint32_t min_hits,
                      uint32_t sample_count);

    /// Whether the cards map hi-res pages (on by default, UE r.LumenScene.SurfaceCache.Feedback); turned off, the next
    /// update() frees every hi-res page and set_feedback() is ignored.
    void set_hi_res_pages_enabled(bool enabled)
    {
        are_hi_res_pages_enabled_ = enabled;
    }

    /// Bumped whenever a card's index in the card table may change (the active placements or their card sets): the
    /// card indices of feedback gathered at one revision hold until the next.
    auto get_card_index_revision() const -> uint64_t
    {
        return card_index_revision_;
    }

    /// The pages schedule_lighting() picked for @p context this frame.
    auto get_lit_pages(lighting_context context) const -> const std::vector<lit_page>&
    {
        return lit_pages_[context];
    }

    /// UE's per-frame tile budget: the 8 x 8 tiles of a square of atlas / sqrt(@p update_factor) texels rounded up to
    /// whole tiles, at least one full page (R/LumenSceneLighting.cpp:98-126).
    static auto compute_lighting_tile_budget(uint32_t atlas_size, uint32_t update_factor) -> uint32_t;

    /// UE's priority bucket, 0 = most urgent: 15 - ceil(log2(4 x @p frames_since_update x @p speed)) in [0, 15].
    static auto compute_lighting_bucket(uint32_t frames_since_update, float speed) -> uint32_t;

    /// Diagnostic: resident cards keep their resolution instead of following the viewer's distance.
    /// Experiment: a card below one texel drops out and no placement-level gate applies (the residency before UE's
    /// RoundUpToPowerOfTwo(0) = 1 and primitive-group rules).
    void set_card_residency_without_group_gate(bool enabled)
    {
        card_residency_without_group_gate_ = enabled;
    }

    void set_hold_resident_resolutions(bool hold)
    {
        hold_resident_resolutions_ = hold;
    }

    /// The view's Lumen scene settings (UE LumenSceneViewDistance, LumenSurfaceCacheResolution,
    /// LumenSceneLightingUpdateSpeed), applied by the update() and schedule_lighting() calls that follow.
    void set_view_settings(const gi_settings::scene_settings& view_settings)
    {
        view_settings_ = view_settings;
    }

    /// The distance from the viewer within which cards are resident: settings::max_card_distance, the reach of the
    /// global distance field.
    auto get_max_card_distance() const -> float;

    /// A lighting context's update factor at the view's lighting update speed (R/LumenSceneLighting.cpp:561-584): the
    /// context's factor (LUMEN_SCENE_DIRECT_UPDATE_FACTOR, LUMEN_SCENE_RADIOSITY_UPDATE_FACTOR) over the speed clamped
    /// to [0.5, 16], rounded.
    auto get_lighting_update_factor(lighting_context context) const -> uint32_t;

    auto get_settings() const -> const settings&
    {
        return settings_;
    }

    auto get_captures() const -> const std::vector<capture>&
    {
        return captures_;
    }

    /// Per card: origin + page-table offset, axis_x + extent x, axis_y + extent y, axis_z + extent z,
    /// (size in pages x, y, res level x, y) - res level 0 = not resident, and the reflections' page table (offset, size
    /// in pages x, y, 1 for the hi-res mip's or 0 for the locked mip's own).
    auto get_card_table() const -> const std::vector<math::vec4>&
    {
        return card_table_;
    }

    /// Per virtual page: (atlas bias x, y in texels, res level x, y); res level 0 = unmapped.
    /**
     * @brief Per page-table entry, in get_page_table's order: x = the frames since its direct lighting was last
     *        updated, y = since its indirect lighting was (UE FLumenCardPageData Last*LightingUpdateFrameIndex against
     *        the surface cache's update frame), for the lighting updates views. A page never lit counts every frame.
     */
    void get_page_lighting_ages(std::vector<math::vec4>& out) const;

    /**
     * @brief Per page-table entry, in get_page_table's order: the update index of the page's last radiosity update
     *        (lit_page::update_index, this frame's for a page scheduled this frame), -1 while the radiosity has not
     *        updated the page since it was mapped. The radiosity probes of a neighbouring page read its traces and SH
     *        with that update's jitter (UE FLumenCardPageData IndirectLightingTemporalIndex).
     */
    void get_page_radiosity_indices(std::vector<float>& out) const;

    /**
     * @brief Per resident page, 3 vec4: (atlas origin xy, size xy), the card UV rectangle it covers, (card index, the
     *        index of its last radiosity update, 0, 0) - the radiosity probe visualization's input.
     */
    void get_visualized_pages(std::vector<math::vec4>& out) const;

    /**
     * @brief The resident cards of the placements whose world bounds come within @p distance of @p view_origin and
     *        touch @p view_frustum (UE VisualizeCardPlacement's culling).
     * @param sources The sources of the last update().
     */
    void get_visualized_cards(const std::vector<source>& sources,
                              const math::vec3& view_origin,
                              float distance,
                              const math::frustum& view_frustum,
                              std::vector<visualized_card>& out) const;

    auto get_page_table() const -> const std::vector<math::vec4>&
    {
        return page_table_;
    }

    /// Every captured page of every resident card, this frame (captured this frame included).
    auto get_resident_pages() const -> const std::vector<resident_page>&
    {
        return resident_pages_;
    }

    /// Per GI instance: (first card, card count, two-sided, motion since the last update in world units); count 0 = no
    /// cards.
    auto get_instance_table() const -> const std::vector<math::vec4>&
    {
        return instance_table_;
    }

    /// The previous allocations of this frame's reallocated cards, a scene table of their own: card records
    /// (get_card_table's layout, page-table offsets counted from get_resample_page_base) then their page entries
    /// (get_page_table's layout). The lighting resample reads them before the copy overwrites the atlas.
    auto get_resample_table() const -> const std::vector<math::vec4>&
    {
        return resample_table_;
    }

    auto get_resample_page_base() const -> uint32_t
    {
        return resample_page_base_;
    }

    auto get_card_count() const -> uint32_t
    {
        return uint32_t(card_table_.size() / card_stride);
    }

    /// Bumped whenever the card, page and instance tables and the resident page list change: they are rebuilt only
    /// when a placement, the active set or an allocation changed, so a still scene keeps them (and their upload).
    auto get_tables_revision() const -> uint64_t
    {
        return tables_revision_;
    }

    struct stats
    {
        uint32_t cards = 0;
        uint32_t resident_cards = 0;
        uint32_t pages_used = 0;
        uint32_t pages_total = 0;
        uint32_t texels_desired = 0;
        uint32_t captures = 0;
        uint32_t downgraded = 0;
        ///< Resident cards given a new resolution this frame.
        uint32_t reallocated = 0;
        ///< Resident pages captured again this frame (the refresh), included in captures.
        uint32_t refreshed = 0;
        ///< Card tiles each lighting context updates this frame, and the bucket its budget ran out in (16 = none).
        std::array<uint32_t, lighting_context_count> lit_tiles{};
        std::array<uint32_t, lighting_context_count> cut_bucket{};
        ///< Resident pages whose direct lighting is due (never lit, or marked since it was).
        uint32_t direct_dirty_pages = 0;
        ///< Hi-res pages mapped, and the hi-res pages the feedback asked for this frame.
        uint32_t hi_res_pages = 0;
        uint32_t hi_res_requests = 0;
    };

    auto get_stats() const -> const stats&
    {
        return stats_;
    }

private:
    struct mip_desc
    {
        math::uvec2 res_level{0u};
        math::uvec2 size_in_pages{1u};
        ///< Texels of one page (128 for paged mips, the whole mip when sub-allocated).
        math::uvec2 page_resolution{0u};
        bool is_sub_allocation = true;
    };

    struct physical_slot
    {
        ///< Physical page index, and the slot inside it for sub-allocations.
        uint32_t page = 0;
        uint32_t slot = 0;
        math::uvec2 atlas_offset{0u};
        ///< Per lighting context: the frame of the page's last update (0 = never lit) and its update count.
        std::array<uint64_t, lighting_context_count> lit_frame{};
        std::array<uint32_t, lighting_context_count> update_count{};
        ///< The frame the page was last captured, by its allocation or by the refresh.
        uint64_t captured_frame = 0;
        ///< The page's direct lighting changed since it was last lit (its capture, its placement's transform, a
        ///< light reaching it, an occluder between it and a light): the direct lighting does not skip it.
        bool is_direct_dirty = true;
    };

    /// A card's on-demand mip above its locked one: any of its pages mapped as the feedback asks for them.
    struct hi_res_mip
    {
        ///< The mip's res level, 0 when the card has none.
        uint32_t res_level = 0;
        mip_desc mip{};
        ///< One per virtual page; is_mapped says which hold a physical page.
        std::vector<physical_slot> slots;
        std::vector<uint8_t> is_mapped;
        ///< Per virtual page: the frame the feedback last asked for it (UE UnlockedAllocationHeap's key).
        std::vector<uint64_t> last_used;
    };

    struct card_state
    {
        ///< The resident (locked) res level, 0 when not resident.
        uint32_t res_level = 0;
        uint32_t desired_res_level = 0;
        uint32_t res_level_on_last_alloc = 0;
        mip_desc mip{};
        ///< One per virtual page of the resident mip (one for a sub-allocation), all captured.
        std::vector<physical_slot> slots;
        math::uvec2 res_level_bias{0u};
        hi_res_mip hi_res;
    };

    struct placement
    {
        std::shared_ptr<const lumen_mesh_cards> cards;
        std::vector<card_state> card_states;
        ///< The cards placed at placed_transform, reused while the placement's transform holds still.
        std::vector<placed_card> placed;
        ///< The world box around the placed cards (centre, half size): the residency gate of the whole placement.
        math::vec3 bounds_center{0.0f};
        math::vec3 bounds_extent{0.0f};
        math::mat4 placed_transform{0.0f};
        bool has_placed = false;
        ///< How far a point of the cards' local bounds moved since the last update at most, in world units: the
        ///< instance table carries it to the probe traces, which mark distance-field hits on it moving.
        float motion = 0.0f;
        uint64_t last_seen = 0;
        ///< source::material_key at the last capture request.
        uint64_t material_key = 0;
        ///< The source that last showed this placement (index into the update's sources).
        uint32_t source_index = 0;
    };

    struct sub_allocation_bin
    {
        math::uvec2 element_size{0u};
        ///< Slots per page (at most 256).
        uint32_t slot_count = 0;
        ///< Free slots over every page of the bin.
        uint32_t free_slots = 0;
        ///< Pages owned by this bin, their used-slot masks (bit per slot; bits past slot_count stay set) and free slots.
        std::vector<uint32_t> pages;
        std::vector<std::vector<uint64_t>> used;
        std::vector<uint32_t> free_counts;
    };

    /// A card whose resolution moved asks for an allocation of its locked mip; the feedback asks for a hi-res page.
    struct request
    {
        ///< Index into the update's sources.
        uint32_t source = 0;
        uint32_t card = 0;
        ///< Distance bucket, nearest first.
        uint32_t bin = 0;
        ///< A hi-res request: the level asked for and its page there (feedback_element::page).
        bool is_hi_res = false;
        uint32_t hi_res_level = 0;
        math::uvec2 hi_res_page{0u};
    };

    /// A hi-res page set_feedback() asked for, ranked and allocated by the next update().
    struct hi_res_request
    {
        uint64_t identity = 0;
        uint32_t card = 0;
        uint32_t res_level = 0;
        math::uvec2 page{0u};
        ///< Added to the card's distance when the request is ranked.
        float distance_bias = 0.0f;
    };

    /// A mapped hi-res page (placement identity, card, virtual page): the eviction's candidates.
    struct hi_res_page_key
    {
        uint64_t identity = 0;
        uint32_t card = 0;
        uint32_t page = 0;
    };

    /// One parallel task's share of the resolution pass (choose_resolutions).
    struct resolution_chunk
    {
        std::vector<request> requests;
        ///< Resident cards that left the view: freed after the pass, in this order (source, card).
        std::vector<math::uvec2> frees;
        uint32_t cards = 0;
        uint32_t texels_desired = 0;
        ///< A placement of the chunk moved (its card boxes were placed again).
        bool has_moved = false;
        ///< A placement's motion changed (stopping included): the instance table carries it.
        bool has_motion_change = false;
    };

    /// Sub-allocated element sizes: 8 to 128 texels per axis.
    static constexpr uint32_t sub_allocation_levels = sub_allocation_res_level - min_res_level + 1u;

    /// A card's resolution rule at the view's surface cache resolution (UE GetCardTexelDensity, GetCardMaxResolution,
    /// GetCardMinResolution at a Lumen scene detail of 1).
    struct resolution_rule
    {
        float texel_density_scale = 0.0f;
        uint32_t max_resolution = 0;
        uint32_t min_resolution = 0;
    };

    auto get_resolution_rule() const -> resolution_rule;
    auto compute_mip_desc(uint32_t res_level, const math::uvec2& bias) const -> mip_desc;
    /// The card UV rectangle of one page of a mip, with half a texel of border on interior edges.
    static auto compute_page_uv_rect(const mip_desc& mip, uint32_t page) -> math::vec4;
    auto allocate(card_state& card, uint32_t res_level) -> bool;
    void free_card(card_state& card);
    /// Frees @p card's hi-res mip and its mapped pages.
    void free_hi_res(card_state& card);
    /// The virtual page of @p mip under the centre of a feedback element's page @p page at @p res_level
    /// (feedback_element::page).
    static auto get_feedback_page(const mip_desc& mip, const math::uvec2& page, uint32_t res_level) -> uint32_t;
    /// Whether one page of @p mip fits the physical atlas as it is now.
    auto has_page_space(const mip_desc& mip) const -> bool;
    /// Frees the hi-res page whose feedback is oldest when it has been idle for @p min_idle_frames or more (UE
    /// EvictOldestAllocation); returns whether one was freed.
    auto evict_oldest_hi_res_page(uint64_t min_idle_frames) -> bool;
    /// update(): the requests set_feedback() left, ranked by the card's distance to @p view_origin.
    void add_hi_res_requests(const std::vector<viewer>& viewers);
    /// Whether @p mip fits the physical atlas as it is now (UE IsPhysicalSpaceAvailable), in constant time.
    auto has_physical_space(const mip_desc& mip) const -> bool;
    /// The bin of a sub-allocated element size, or null when none was created yet.
    auto find_bin(const math::uvec2& element_size) const -> const sub_allocation_bin*;
    auto allocate_slot(const math::uvec2& element_size, physical_slot& out) -> bool;
    void free_slot(const physical_slot& slot, const mip_desc& mip);
    auto get_page_origin(uint32_t page) const -> math::uvec2;
    /// Lists a resident card's allocation in the resample table; returns its card index there.
    auto append_resample_source(const card_state& card, const placed_card& placed) -> int32_t;

    /// update(): the placement per source (created, refreshed when its card set changed), and the unseen ones dropped.
    /// @return Whether every source has its own placement (no identity repeated this frame).
    auto refresh_placements(const std::vector<source>& sources) -> bool;
    /// update(): the sources served this frame (built cards, valid instance) and their first card in the card table.
    /// @return Whether the active set (placements, their order and instances) differs from the previous frame's.
    auto collect_active(const std::vector<source>& sources, uint32_t instance_count) -> bool;
    /// update(): every active card's resolution from its distance to @p view_origin, and a request for every card whose
    /// resolution moved; @p parallel spreads the placements over the pool.
    void choose_resolutions(const std::vector<source>& sources, const std::vector<viewer>& viewers, bool parallel);
    /// choose_resolutions() for the active placements [@p begin, @p end).
    void choose_chunk_resolutions(const std::vector<source>& sources,
                                  const std::vector<viewer>& viewers,
                                  uint32_t begin,
                                  uint32_t end,
                                  resolution_chunk& chunk);
    /// Places this frame's captures in the capture atlas (shelves of equal page heights).
    class capture_packer;
    /// update(): the requests, nearest first, allocated and queued for capture within the frame's budgets.
    void allocate_requests(const std::vector<source>& sources, capture_packer& packer);
    /// allocate_requests(): maps the hi-res page @p req asks for and queues its capture; returns the pages captured.
    auto allocate_hi_res_page(const std::vector<source>& sources, const request& req, capture_packer& packer)
        -> uint32_t;
    /// update(): the packed card, page and instance tables and the resident page list.
    void build_tables(const std::vector<source>& sources, uint32_t instance_count);
    /// update(): resident pages captured again, oldest first, within the refresh's share of the capture budget (UE
    /// SceneCardCaptureRefresh). A refreshed page keeps its lighting: its card is its own resample source.
    void refresh_captures(const std::vector<source>& sources, capture_packer& packer);
    /// schedule_lighting(): every resident page's tiles, speed and bucket per lighting context, across the pool; a
    /// direct-lit page that is not dirty takes k_skip_bucket.
    void compute_page_priorities(const std::vector<viewer>& viewers);
    /// Per resident page: its lighting speed (UE's 1 / (1 + distance / priority distance), doubled near a frustum),
    /// its nearest viewer's index and its 8 x 8 tile count.
    void compute_page_speeds(const std::vector<viewer>& viewers);
    /// Per resident page, the index of its nearest viewer (compute_page_speeds).
    std::vector<uint32_t> page_viewers_;
    /// Per resident page, its lighting speed (compute_page_speeds), kept while the tables and the viewers are the
    /// ones it was computed for.
    std::vector<float> page_speeds_;
    std::vector<viewer> priority_viewers_;
    uint64_t priority_tables_revision_ = ~0ull;
    /// The world box of resident page @p page (its card-space box across the card's depth).
    auto compute_page_bounds(uint32_t page) const -> math::bbox;

    settings settings_{};
    gi_settings::scene_settings view_settings_{};
    uint32_t pages_per_side_ = 0;
    std::vector<uint32_t> free_pages_;
    std::vector<sub_allocation_bin> bins_;
    ///< bins_ index per sub-allocated element size (x level, y level from min_res_level), -1 for none.
    std::array<int32_t, sub_allocation_levels * sub_allocation_levels> bin_lookup_ = []()
    {
        std::array<int32_t, sub_allocation_levels * sub_allocation_levels> lookup{};
        lookup.fill(-1);
        return lookup;
    }();
    std::unordered_map<uint64_t, placement> placements_;
    ///< This frame's placement per source (refresh_placements).
    std::vector<placement*> source_placements_;
    ///< This frame's active sources and each one's first card in the card table (collect_active).
    std::vector<uint32_t> active_;
    std::vector<uint32_t> first_card_;
    ///< The previous frame's active set: (identity, instance index) per active source, and the instance count.
    std::vector<std::pair<uint64_t, uint32_t>> active_keys_;
    uint32_t active_instance_count_ = 0;
    std::vector<resolution_chunk> resolution_chunks_;
    std::vector<request> requests_;
    ///< The tables no longer describe the scene: rebuilt by this frame's build_tables().
    bool are_tables_dirty_ = true;
    uint64_t tables_revision_ = 0;
    ///< Resident cards in the current tables.
    uint32_t resident_cards_ = 0;
    ///< Per resident page: its placement (index into active_) and its card there, for the refresh.
    std::vector<math::uvec2> resident_page_owners_;
    ///< Per page-table entry: the physical slot it maps, null for a hi-res entry falling back to a locked page.
    std::vector<const physical_slot*> page_entry_slots_;
    ///< Per card of the card table: its placement's identity and its card there (the feedback's card indices).
    std::vector<std::pair<uint64_t, uint32_t>> card_owner_keys_;
    uint64_t card_index_revision_ = 0;
    ///< The feedback's requests for the next update(), and the mapped hi-res pages (stale keys dropped as met).
    std::vector<hi_res_request> hi_res_requests_;
    std::vector<hi_res_page_key> hi_res_pages_;
    ///< Hi-res pages mapped in the current tables.
    uint32_t hi_res_page_count_ = 0;
    bool are_hi_res_pages_enabled_ = true;
    ///< This frame's active sources (collect_active), per source.
    std::vector<uint8_t> is_source_active_;
    ///< refresh_captures() scratch: the resident pages it may capture, and each refreshed card's resample entry.
    std::vector<uint32_t> refresh_candidates_;
    std::vector<std::pair<uint32_t, int32_t>> refresh_resamples_;
    ///< schedule_lighting() scratch, per resident page: tiles, and bucket per lighting context.
    std::vector<uint32_t> page_tiles_;
    std::array<std::vector<uint32_t>, lighting_context_count> page_buckets_;
    uint64_t frame_ = 0;
    std::vector<capture> captures_;
    std::vector<math::vec4> card_table_;
    std::vector<math::vec4> page_table_;
    std::vector<math::vec4> instance_table_;
    std::vector<resident_page> resident_pages_;
    ///< The physical slot behind each of resident_pages_, for the lighting stamps.
    std::vector<physical_slot*> resident_slots_;
    std::array<std::vector<lit_page>, lighting_context_count> lit_pages_;
    std::vector<math::vec4> resample_cards_;
    std::vector<math::vec4> resample_pages_;
    std::vector<math::vec4> resample_table_;
    uint32_t resample_page_base_ = 0;
    stats stats_{};
    bool hold_resident_resolutions_ = false;
    bool card_residency_without_group_gate_ = false;
};

} // namespace unravel
