#pragma once

#include <engine/rendering/gi/lumen_mesh_cards.h>

#include <math/math.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
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
 * axis); a card whose resolution changes is reallocated and recaptured, nearest first, within the
 * capture budget. Mips above 128 texels are paged into 128x128 physical pages, each holding 127
 * virtual texels plus half-texel borders on interior edges; smaller mips share physical pages.
 *
 * A card is (re)allocated only when its whole mip has physical room beside everything resident and room in
 * this frame's capture atlas, so every page it maps is captured in the frame it is allocated; a reallocated
 * card's previous allocation is listed in the resample table, whose lighting the new pages inherit.
 *
 * The packed tables are rebuilt every frame (cards and page table are small); physical allocations
 * persist, so captured content survives until its card changes resolution or leaves.
 *
 * schedule_lighting() picks the pages the card lighting updates each frame as Lumen does: per page and per
 * context (direct lighting, radiosity) a priority bucket from the frames since its last update and its speed
 * (distance and frustum), spent bucket by bucket against a fixed tile budget per context.
 */
class lumen_scene
{
public:
    struct settings
    {
        ///< Physical atlas edge in texels (a multiple of the page size).
        uint32_t atlas_size = 2048;
        ///< Capture atlas edge in texels: the most texels captured per frame.
        uint32_t capture_atlas_size = 1024;
        ///< r.LumenScene.SurfaceCache.CardCapturesPerFrame.
        uint32_t max_captures_per_frame = 300;
        ///< Cards farther than this are not resident (the last global SDF clipmap's extent).
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
    };

    /// The two card lighting contexts, scheduled apart (UE's direct lighting and radiosity).
    enum lighting_context : uint32_t
    {
        lighting_direct = 0,
        lighting_radiosity = 1,
        lighting_context_count = 2,
    };

    /// A page the card lighting updates this frame.
    struct lit_page
    {
        ///< Index into get_resident_pages().
        uint32_t resident_page = 0;
        ///< The page's update count in the context (UE's per-page temporal index): the radiosity probes' jitter.
        uint32_t update_index = 0;
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
    };

    /// Lumen::PhysicalPageSize.
    static constexpr uint32_t physical_page_size = 128;
    /// Lumen::MinResLevel / MaxResLevel / SubAllocationResLevel.
    static constexpr uint32_t min_res_level = 3;
    static constexpr uint32_t max_res_level = 11;
    static constexpr uint32_t sub_allocation_res_level = 7;
    /// float4s per card record, page-table entry and instance record in the packed tables.
    static constexpr uint32_t card_stride = 5;
    static constexpr uint32_t page_stride = 1;
    static constexpr uint32_t instance_stride = 1;

    void init(const settings& s);
    void reset();

    /**
     * @brief Places this frame's cards, chooses resolutions, (re)allocates and queues captures.
     *
     * @param sources        Placements with built cards (any order; identity keys the history).
     * @param instance_count This frame's GI instance count (the instance table's size).
     * @param view_origin    The viewer the resolutions are chosen for.
     */
    void update(const std::vector<source>& sources, uint32_t instance_count, const math::vec3& view_origin);

    /**
     * @brief Picks this frame's pages for each lighting context (UE LumenSceneLighting.usf:105-366), after update().
     *
     * A page's speed is 1 / (1 + its distance to @p view_origin / LUMEN_SCENE_LIGHTING_PRIORITY_DISTANCE), doubled
     * within LUMEN_SCENE_LIGHTING_FRUSTUM_MARGIN of @p view_frustum; its bucket 15 - ceil(log2(4 x frames since its
     * last update x speed)), a never-lit page ranking as LUMEN_SCENE_LIGHTING_NEVER_LIT_FRAMES old. Buckets are
     * admitted most urgent first, whole pages, until the context's tile budget (compute_lighting_tile_budget) is
     * spent; the pages taken are stamped with this frame.
     */
    void schedule_lighting(const math::vec3& view_origin, const math::frustum& view_frustum);

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
    void set_hold_resident_resolutions(bool hold)
    {
        hold_resident_resolutions_ = hold;
    }

    auto get_settings() const -> const settings&
    {
        return settings_;
    }

    auto get_captures() const -> const std::vector<capture>&
    {
        return captures_;
    }

    /// Per card: origin + page-table offset, axis_x + extent x, axis_y + extent y, axis_z + extent z,
    /// (size in pages x, y, res level x, y) - res level 0 = not resident.
    auto get_card_table() const -> const std::vector<math::vec4>&
    {
        return card_table_;
    }

    /// Per virtual page: (atlas bias x, y in texels, res level x, y); res level 0 = unmapped.
    auto get_page_table() const -> const std::vector<math::vec4>&
    {
        return page_table_;
    }

    /// Every captured page of every resident card, this frame (captured this frame included).
    auto get_resident_pages() const -> const std::vector<resident_page>&
    {
        return resident_pages_;
    }

    /// Per GI instance: (first card, card count, two-sided, 0); count 0 = no cards.
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
        ///< Card tiles each lighting context updates this frame, and the bucket its budget ran out in (16 = none).
        std::array<uint32_t, lighting_context_count> lit_tiles{};
        std::array<uint32_t, lighting_context_count> cut_bucket{};
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
    };

    struct placement
    {
        std::shared_ptr<const lumen_mesh_cards> cards;
        std::vector<card_state> card_states;
        uint64_t last_seen = 0;
    };

    struct sub_allocation_bin
    {
        math::uvec2 element_size{0u};
        ///< Pages owned by this bin and their used-slot masks (bit per slot, at most 256 slots).
        std::vector<uint32_t> pages;
        std::vector<std::vector<uint64_t>> used;
    };

    auto compute_mip_desc(uint32_t res_level, const math::uvec2& bias) const -> mip_desc;
    /// The card UV rectangle of one page of a mip, with half a texel of border on interior edges.
    static auto compute_page_uv_rect(const mip_desc& mip, uint32_t page) -> math::vec4;
    auto allocate(card_state& card, uint32_t res_level) -> bool;
    void free_card(card_state& card);
    /// Whether @p mip fits the physical atlas as it is now (UE IsPhysicalSpaceAvailable).
    auto has_physical_space(const mip_desc& mip) const -> bool;
    auto allocate_slot(const math::uvec2& element_size, physical_slot& out) -> bool;
    void free_slot(const physical_slot& slot, const mip_desc& mip);
    auto get_page_origin(uint32_t page) const -> math::uvec2;
    /// Lists a resident card's allocation in the resample table; returns its card index there.
    auto append_resample_source(const card_state& card, const lumen_card& card_desc, const math::mat4& local_to_world)
        -> int32_t;

    settings settings_{};
    uint32_t pages_per_side_ = 0;
    std::vector<uint32_t> free_pages_;
    std::vector<sub_allocation_bin> bins_;
    std::unordered_map<uint64_t, placement> placements_;
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
};

} // namespace unravel
