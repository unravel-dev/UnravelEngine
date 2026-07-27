#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/camera.h>
#include <engine/ecs/scene.h>
#include <engine/rendering/pipeline/passes/surface_cache/card_capture.h>
#include <engine/rendering/pipeline/passes/surface_cache/card_lighting.h>
#include <engine/rendering/pipeline/passes/surface_cache/gi_probe_volume.h>
#include <engine/rendering/pipeline/passes/surface_cache/gi_ray_query.h>
#include <engine/rendering/pipeline/passes/surface_cache/opacity_clipmap.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <graphics/render_pass.h>
#include <math/math.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace unravel
{

/**
 * @brief Lumen-lite software surface-cache GI.
 *
 * Cards: world/volume resident (unioned with a camera working set).
 * Final-gather probes: camera-centered cascades (world-snapped), like Lumen's
 * radiance cache — walking never falls off a volume-center grid.
 */
class surface_cache_gi_pass
{
public:
    static constexpr uint16_t ATLAS_SIZE = 2048;
    static constexpr uint16_t PAGE_SIZE = 64;
    static constexpr uint16_t PAGES_PER_AXIS = ATLAS_SIZE / PAGE_SIZE;
    static constexpr uint32_t MAX_PAGES = PAGES_PER_AXIS * PAGES_PER_AXIS;
    static constexpr uint32_t MAX_CARDS = MAX_PAGES;
    static constexpr uint32_t TEXELS_PER_CARD = 4;
    static constexpr uint32_t GATHER_UPLOAD_CAP = 512;
    /// Match gather/bounce/probe GPU caps so textured floors are not stuck outside refine.
    static constexpr uint32_t PROJECT_UPLOAD_CAP = 512;
    static constexpr uint8_t MAX_TILES_PER_AXIS = 16;
    static constexpr int NEW_CARDS_PER_FRAME = 160;
    /// One PageFill view per card — keep small to avoid pass spam on discovery.
    /// Seed enough pages/frame so large red floors get albedo before lighting.
    static constexpr int MESH_FILLS_PER_FRAME = 32;

    struct surface_cache_gi_settings
    {
        float cache_blend = 1.0f;
        /// Near-field SSIL only. Keep low so world probes (and red bounce) remain visible.
        float ssil_near_field_weight = 0.30f;
        float max_card_distance = 200.0f;
        float card_thickness = 0.35f;
        /// Low enough that G-buffer textured albedo overwrites mesh tint seed.
        float project_history = 0.40f;
        int stale_frames = 600;
        /// Amortized card light/bounce budget (batched into 2 GPU views).
        int pages_per_frame = 24;
        float min_face_area = 0.05f;
        /// Legacy flag — card pages no longer stamp sky×albedo (that looked like ambient).
        /// Skylight remains the compose miss path when cache confidence is 0.
        bool seed_with_skylight = false;
        float max_gather_distance = 160.0f;
        /// Scales final-gather RGB only (not confidence). ~1.0 = balanced after exposure.
        float gather_intensity = 1.0f;
        float max_card_extent = 3.0f;
        float sticky_distance = 50.0f;
        float probe_near_extent = 40.0f;
        float probe_far_extent = 120.0f;
        int probes_per_frame = 160;
        float probe_history = 0.75f;
        /// Local card bounce — high enough for visible floor color bleed.
        float bounce_strength = 1.25f;
        /// Material/emissive G-buffer refine only — never write screen radiance (view-unstable).
        bool enable_screen_project = false;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr g_buffer;
        gfx::texture::ptr direct_lighting;
        gfx::texture::ptr irradiance_sh;
        const camera* cam{};
        scene* scn{};
        surface_cache_gi_settings settings{};
        /// Winning GI volume world AABB (required for world-anchored residency).
        math::bbox volume_bounds{};
        bool has_volume_bounds = false;
    };

    auto init(rtti::context& ctx) -> bool;
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr;
    void update_world_cache(gfx::render_view& rview, const run_params& params);
    auto sample_frame(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr;
    void release_resources(gfx::render_view& rview);

private:
    struct surface_card
    {
        math::vec3 origin{0.0f, 0.0f, 0.0f};
        math::vec3 normal{0.0f, 1.0f, 0.0f};
        math::vec3 tangent{1.0f, 0.0f, 0.0f};
        math::vec3 bitangent{0.0f, 0.0f, 1.0f};
        math::vec2 half_extents{1.0f, 1.0f};
        uint16_t page_x = 0;
        uint16_t page_y = 0;
        uint32_t entity_id = 0;
        uint16_t submesh_index = 0;
        uint8_t instance_index = 0;
        uint8_t face_index = 0;
        uint8_t tile_u = 0;
        uint8_t tile_v = 0;
        uint8_t tiles_u = 1;
        uint8_t tiles_v = 1;
        uint32_t last_project_frame = 0;
        uint32_t alloc_frame = 0;
        bool dirty = true;
        bool needs_mesh_seed = true;
        bool seen_this_frame = false;
        float last_sun_ndotl = -1.0f;
    };

    struct page_slot
    {
        bool allocated = false;
        uint32_t card_index = UINT32_MAX;
        uint32_t last_use_frame = 0;
    };

    using card_key = uint64_t;
    using surface_key = uint64_t;

    static auto make_card_key(uint32_t entity_id,
                              uint16_t submesh_index,
                              uint8_t instance_index,
                              uint8_t face,
                              uint8_t tile_u,
                              uint8_t tile_v) -> card_key
    {
        return (uint64_t(entity_id & 0x00FFFFFFu) << 40) | (uint64_t(submesh_index) << 24) |
               (uint64_t(instance_index) << 16) | (uint64_t(face) << 8) | (uint64_t(tile_u) << 4) |
               uint64_t(tile_v);
    }

    static auto make_card_key(const surface_card& card) -> card_key
    {
        return make_card_key(card.entity_id,
                             card.submesh_index,
                             card.instance_index,
                             card.face_index,
                             card.tile_u,
                             card.tile_v);
    }

    static auto make_surface_key(uint32_t entity_id, uint16_t submesh_index, uint8_t instance_index)
        -> surface_key
    {
        return (uint64_t(entity_id & 0x00FFFFFFu) << 24) | (uint64_t(submesh_index) << 8) |
               uint64_t(instance_index);
    }

    static auto make_surface_key(const surface_card& card) -> surface_key
    {
        return make_surface_key(card.entity_id, card.submesh_index, card.instance_index);
    }

    auto resolve_volume(const run_params& params) const -> math::bbox;
    auto is_in_volume(const math::vec3& point, const math::bbox& volume) const -> bool;
    auto bounds_intersect_volume(const math::bbox& bounds, const math::bbox& volume) const -> bool;

    void sync_cards_from_scene(scene& scn,
                               const camera& cam,
                               const surface_cache_gi_settings& settings,
                               const math::bbox& volume);
    /**
     * @brief Recompute card frames from live model world bounds so IL tracks mesh moves.
     */
    void refresh_card_world_frames(scene& scn, const surface_cache_gi_settings& settings);
    auto lookup_surface_world_bounds(scene& scn,
                                     uint32_t entity_id,
                                     uint16_t submesh_index,
                                     uint8_t instance_index) const -> math::bbox;
    void discover_new_surfaces(scene& scn,
                               const camera& cam,
                               const surface_cache_gi_settings& settings,
                               const math::bbox& volume,
                               int new_card_budget);
    void ensure_atlas();
    void ensure_atlas_srv();
    void resolve_atlas_for_sample();
    void ensure_card_texture();
    void upload_card_texture(const camera& cam,
                             const surface_cache_gi_settings& settings,
                             const math::bbox& volume);
    auto allocate_page(uint32_t frame, float protect_closer_than) -> int32_t;
    void free_page(uint32_t page_index);
    /// Zero radiance/material/emissive for a recycled page (prevents stale cyan knife-cuts).
    void clear_atlas_page(uint16_t page_x, uint16_t page_y);
    void remove_card_at(uint32_t card_index);
    auto try_evict_farthest_page(const math::vec3& volume_center, float protect_closer_than) -> bool;
    auto spawn_cards_for_bounds(uint32_t entity_id,
                                uint16_t submesh_index,
                                uint8_t instance_index,
                                const math::bbox& world_bounds,
                                const surface_cache_gi_settings& settings,
                                uint32_t frame,
                                int& new_card_budget) -> bool;
    void seed_mesh_materials(scene& scn, int budget);
    void amortized_age_pages(const surface_cache_gi_settings& settings);
    auto gather_lights(scene& scn, const math::bbox& volume) const -> card_lighting::light_env;
    void run_material_emissive_capture(const run_params& params, uint32_t project_count, bool write_radiance);

    struct project_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_gbuffer0, "s_gbuffer0", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gbuffer1, "s_gbuffer1", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gbuffer4, "s_gbuffer4", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_direct, "s_direct", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_cards, "s_cards", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gbuffer2, "s_gbuffer2", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_scache_params, "u_scache_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_scache_params2, "u_scache_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_camera_position, "u_camera_position", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr s_gbuffer0;
        gfx::program::uniform_ptr s_gbuffer1;
        gfx::program::uniform_ptr s_gbuffer4;
        gfx::program::uniform_ptr s_direct;
        gfx::program::uniform_ptr s_cards;
        gfx::program::uniform_ptr s_gbuffer2;
        gfx::program::uniform_ptr u_scache_params;
        gfx::program::uniform_ptr u_scache_params2;
        gfx::program::uniform_ptr u_camera_position;
        std::unique_ptr<gpu_program> program;
    } project_program_{};

    struct age_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_age_params0, "u_age_params0", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_age_params1, "u_age_params1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_age_params2, "u_age_params2", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr u_age_params0;
        gfx::program::uniform_ptr u_age_params1;
        gfx::program::uniform_ptr u_age_params2;
        std::unique_ptr<gpu_program> program;
    } age_program_{};

    struct clear_page_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_fill_params0, "u_fill_params0", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr u_fill_params0;
        std::unique_ptr<gpu_program> program;
    } clear_page_program_{};

    gi_probe_volume probes_{};
    card_lighting card_lighting_{};
    surface_cache::card_capture capture_{};
    surface_cache::gi_ray_query ray_query_{};
    surface_cache::opacity_clipmap opacity_{};

    gfx::texture::ptr atlas_{};
    /// Blit target for FS sampling — D3D often reads black from UAV-written atlas via texture2D.
    gfx::texture::ptr atlas_srv_{};
    gfx::texture::ptr card_tex_{};
    std::vector<surface_card> cards_{};
    std::vector<page_slot> pages_{};
    std::vector<uint32_t> free_pages_{};
    std::unordered_map<card_key, uint32_t> card_lookup_{};
    std::unordered_map<surface_key, uint32_t> surface_card_counts_{};
    std::unordered_set<surface_key> fully_spawned_surfaces_{};
    std::vector<float> card_upload_{};
    std::unordered_set<card_key> sticky_upload_keys_{};
    std::vector<uint32_t> sticky_upload_indices_{};
    math::vec3 last_camera_position_{0.0f, 0.0f, 0.0f};
    math::vec3 last_sun_direction_{0.0f, -1.0f, 0.0f};
    bool has_sun_direction_ = false;
    math::vec3 volume_center_{0.0f, 0.0f, 0.0f};
    math::bbox volume_bounds_{};
    uint32_t frame_index_ = 0;
    uint32_t age_cursor_ = 0;
    uint32_t mesh_seed_cursor_ = 0;
    uint32_t uploaded_card_count_ = 0;
};

} // namespace unravel
