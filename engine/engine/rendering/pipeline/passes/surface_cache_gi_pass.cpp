#include "surface_cache_gi_pass.h"

#include "surface_cache/card_placement.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/basic_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

#include <algorithm>
#include <cmath>

namespace unravel
{

namespace
{
auto spatial_cell_key(const math::vec3& pos, float cell) -> uint64_t
{
    const float c = std::max(cell, 1.0f);
    const int32_t ix = int32_t(std::floor(pos.x / c));
    const int32_t iy = int32_t(std::floor(pos.y / c));
    const int32_t iz = int32_t(std::floor(pos.z / c));
    return (uint64_t(uint32_t(ix)) << 42) ^ (uint64_t(uint32_t(iy)) << 21) ^ uint64_t(uint32_t(iz));
}
} // namespace

auto surface_cache_gi_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs_project = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_surface_cache_project.sc");
    auto cs_age = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_surface_cache_age.sc");
    auto cs_clear = am.get_asset<gfx::shader>("engine:/data/shaders/surface_cache_gi/cs_card_page_clear.sc");
    project_program_.program = std::make_unique<gpu_program>(cs_project);
    project_program_.cache_uniforms();
    age_program_.program = std::make_unique<gpu_program>(cs_age);
    age_program_.cache_uniforms();
    clear_page_program_.program = std::make_unique<gpu_program>(cs_clear);
    clear_page_program_.cache_uniforms();
    pages_.assign(MAX_PAGES, page_slot{});
    free_pages_.clear();
    free_pages_.reserve(MAX_PAGES);
    for(uint32_t i = 0; i < MAX_PAGES; ++i)
    {
        free_pages_.push_back(MAX_PAGES - 1u - i);
    }
    const bool capture_ok = capture_.init(ctx);
    const bool probes_ok = probes_.init(ctx);
    const bool lighting_ok = card_lighting_.init(ctx);
    const bool opacity_ok = opacity_.init(ctx);
    ray_query_.select_backend(false);
    return project_program_.program && project_program_.program->is_valid() && capture_ok && probes_ok &&
           lighting_ok && opacity_ok;
}

auto surface_cache_gi_pass::resolve_volume(const run_params& params) const -> math::bbox
{
    math::bbox volume{};
    if(params.has_volume_bounds && params.volume_bounds.is_populated())
    {
        volume = params.volume_bounds;
    }
    else
    {
        const math::vec3 center = params.cam ? params.cam->get_position() : math::vec3{0.0f, 0.0f, 0.0f};
        const float he = std::max(params.settings.probe_far_extent, params.settings.max_card_distance);
        const math::vec3 half(he, he * 0.55f, he);
        volume = math::bbox(center - half, center + half);
    }
    // Always union a camera working set. Volume-only residency culled Bistro outskirts
    // and made local-volume GI feel glued to a single mesh AABB.
    if(params.cam)
    {
        const float cam_he =
            std::max(params.settings.max_card_distance, std::max(params.settings.probe_far_extent, 80.0f));
        const math::vec3 cam = params.cam->get_position();
        const math::vec3 half(cam_he, cam_he * 0.55f, cam_he);
        volume.add_point(cam - half);
        volume.add_point(cam + half);
    }
    return volume;
}

auto surface_cache_gi_pass::is_in_volume(const math::vec3& point, const math::bbox& volume) const -> bool
{
    if(volume.contains_point(point))
    {
        return true;
    }
    // Small margin so cards on the shell of buildings stay resident.
    const math::vec3 closest = volume.closest_point(point);
    const math::vec3 d = point - closest;
    return math::dot(d, d) <= 4.0f; // 2m slack
}

auto surface_cache_gi_pass::bounds_intersect_volume(const math::bbox& bounds, const math::bbox& volume) const
    -> bool
{
    if(!bounds.is_populated() || !volume.is_populated())
    {
        return false;
    }
    return bounds.intersect(volume);
}

void surface_cache_gi_pass::ensure_atlas()
{
    if(atlas_)
    {
        return;
    }
    atlas_ = std::make_shared<gfx::texture>(ATLAS_SIZE,
                                            ATLAS_SIZE,
                                            false,
                                            1,
                                            gfx::texture_format::RGBA16F,
                                            BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                BGFX_SAMPLER_V_CLAMP);
}

void surface_cache_gi_pass::ensure_atlas_srv()
{
    if(atlas_srv_)
    {
        return;
    }
    atlas_srv_ = std::make_shared<gfx::texture>(ATLAS_SIZE,
                                                ATLAS_SIZE,
                                                false,
                                                1,
                                                gfx::texture_format::RGBA16F,
                                                BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                                    BGFX_SAMPLER_V_CLAMP);
}

void surface_cache_gi_pass::resolve_atlas_for_sample()
{
    if(!atlas_)
    {
        return;
    }
    ensure_atlas_srv();
    gfx::render_pass pass("SurfaceCacheGI/AtlasResolve");
    pass.bind();
    gfx::blit(pass.id, atlas_srv_->native_handle(), 0, 0, atlas_->native_handle());
}

void surface_cache_gi_pass::ensure_card_texture()
{
    if(card_tex_)
    {
        return;
    }
    card_tex_ = std::make_shared<gfx::texture>(TEXELS_PER_CARD,
                                               MAX_CARDS,
                                               false,
                                               1,
                                               gfx::texture_format::RGBA32F,
                                               BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                                   BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    card_upload_.assign(TEXELS_PER_CARD * MAX_CARDS * 4, 0.0f);
    const gfx::memory_view* mem =
        gfx::copy(card_upload_.data(), static_cast<uint32_t>(card_upload_.size() * sizeof(float)));
    gfx::update_texture_2d(card_tex_->native_handle(), 0, 0, 0, 0, TEXELS_PER_CARD, MAX_CARDS, mem);
}

void surface_cache_gi_pass::upload_card_texture(const camera& cam,
                                                const surface_cache_gi_settings& settings,
                                                const math::bbox& volume)
{
    APP_SCOPE_PERF("Rendering/Surface Cache GI/Upload");
    ensure_card_texture();
    const math::vec3 volume_center = volume.get_center();
    const float cell = std::max(settings.max_card_extent * 2.0f, 4.0f);
    std::vector<uint32_t> kept;
    kept.reserve(std::min<size_t>(sticky_upload_indices_.size(), GATHER_UPLOAD_CAP));
    std::unordered_set<card_key> kept_keys;
    kept_keys.reserve(GATHER_UPLOAD_CAP);
    // 1) Preserve previous GPU row order for cards still inside the volume.
    for(uint32_t prev_index : sticky_upload_indices_)
    {
        if(kept.size() >= GATHER_UPLOAD_CAP || prev_index >= cards_.size())
        {
            continue;
        }
        const surface_card& card = cards_[prev_index];
        if(!is_in_volume(card.origin, volume))
        {
            continue;
        }
        const card_key key = make_card_key(card);
        if(kept_keys.count(key) != 0)
        {
            continue;
        }
        kept.push_back(prev_index);
        kept_keys.insert(key);
    }
    // 1b) Append nearby +Y floors into free sticky slots only.
    // Never mid-list swap or re-partition — that reshuffles GPU rows and pops lighting.
    {
        std::vector<uint32_t> floors;
        floors.reserve(64);
        for(uint32_t i = 0; i < cards_.size(); ++i)
        {
            const surface_card& card = cards_[i];
            if(card.face_index != 2 || !is_in_volume(card.origin, volume))
            {
                continue;
            }
            if(kept_keys.count(make_card_key(card)) != 0)
            {
                continue;
            }
            floors.push_back(i);
        }
        const math::vec3 cam_pos_early = cam.get_position();
        std::sort(floors.begin(),
                  floors.end(),
                  [&](uint32_t a, uint32_t b)
                  {
                      const math::vec3 da = cards_[a].origin - cam_pos_early;
                      const math::vec3 db = cards_[b].origin - cam_pos_early;
                      return math::dot(da, da) < math::dot(db, db);
                  });
        for(uint32_t index : floors)
        {
            if(kept.size() >= GATHER_UPLOAD_CAP)
            {
                break;
            }
            kept.push_back(index);
            kept_keys.insert(make_card_key(cards_[index]));
        }
    }
    // 2) Fill remaining slots: prefer camera neighborhood (final gather is view-centered),
    //    with spatial-hash diversity so one building does not monopolize the GPU set.
    std::vector<uint32_t> candidates;
    candidates.reserve(cards_.size());
    std::unordered_set<uint64_t> seen_cells;
    seen_cells.reserve(GATHER_UPLOAD_CAP);
    for(uint32_t i = 0; i < kept.size(); ++i)
    {
        seen_cells.insert(spatial_cell_key(cards_[kept[i]].origin, cell));
    }
    for(uint32_t i = 0; i < cards_.size(); ++i)
    {
        const surface_card& card = cards_[i];
        if(kept_keys.count(make_card_key(card)) != 0 || !is_in_volume(card.origin, volume))
        {
            continue;
        }
        candidates.push_back(i);
    }
    const math::vec3 cam_pos = cam.get_position();
    auto by_camera_coverage = [&](uint32_t a, uint32_t b)
    {
        // Prefer +Y floor emitters so red floors enter the GPU set before walls.
        const bool a_floor = cards_[a].face_index == 2;
        const bool b_floor = cards_[b].face_index == 2;
        if(a_floor != b_floor)
        {
            return a_floor;
        }
        const uint64_t ca = spatial_cell_key(cards_[a].origin, cell);
        const uint64_t cb = spatial_cell_key(cards_[b].origin, cell);
        const bool a_new = seen_cells.count(ca) == 0;
        const bool b_new = seen_cells.count(cb) == 0;
        if(a_new != b_new)
        {
            return a_new && !b_new;
        }
        const math::vec3 da = cards_[a].origin - cam_pos;
        const math::vec3 db = cards_[b].origin - cam_pos;
        return math::dot(da, da) < math::dot(db, db);
    };
    const size_t slots_left = GATHER_UPLOAD_CAP > kept.size() ? (GATHER_UPLOAD_CAP - kept.size()) : 0;
    if(candidates.size() > slots_left)
    {
        std::nth_element(candidates.begin(),
                         candidates.begin() + static_cast<std::ptrdiff_t>(slots_left),
                         candidates.end(),
                         by_camera_coverage);
        candidates.resize(slots_left);
    }
    std::sort(candidates.begin(), candidates.end(), by_camera_coverage);
    for(uint32_t index : candidates)
    {
        if(kept.size() >= GATHER_UPLOAD_CAP)
        {
            break;
        }
        kept.push_back(index);
        kept_keys.insert(make_card_key(cards_[index]));
        seen_cells.insert(spatial_cell_key(cards_[index].origin, cell));
    }
    (void)volume_center;
    (void)settings;
    // Keep sticky GPU row order stable (no partition). Row reshuffles caused bright pops.
    const uint32_t count = static_cast<uint32_t>(kept.size());
    sticky_upload_keys_ = std::move(kept_keys);
    sticky_upload_indices_ = std::move(kept);
    for(uint32_t i = 0; i < count; ++i)
    {
        const uint32_t card_index = sticky_upload_indices_[i];
        const surface_card& card = cards_[card_index];
        const float page_u0 = float(card.page_x * PAGE_SIZE) / float(ATLAS_SIZE);
        const float page_v0 = float(card.page_y * PAGE_SIZE) / float(ATLAS_SIZE);
        float* dst = card_upload_.data() + i * TEXELS_PER_CARD * 4;
        dst[0] = card.origin.x;
        dst[1] = card.origin.y;
        dst[2] = card.origin.z;
        dst[3] = card.half_extents.x;
        dst[4] = card.normal.x;
        dst[5] = card.normal.y;
        dst[6] = card.normal.z;
        dst[7] = card.half_extents.y;
        dst[8] = card.tangent.x;
        dst[9] = card.tangent.y;
        dst[10] = card.tangent.z;
        dst[11] = page_u0;
        dst[12] = card.bitangent.x;
        dst[13] = card.bitangent.y;
        dst[14] = card.bitangent.z;
        dst[15] = page_v0;
    }
    uploaded_card_count_ = count;
    if(count == 0)
    {
        return;
    }
    const gfx::memory_view* mem =
        gfx::copy(card_upload_.data(), static_cast<uint32_t>(count * TEXELS_PER_CARD * 4 * sizeof(float)));
    gfx::update_texture_2d(card_tex_->native_handle(), 0, 0, 0, 0, TEXELS_PER_CARD, uint16_t(count), mem);
}

auto surface_cache_gi_pass::allocate_page(uint32_t frame, float protect_closer_than) -> int32_t
{
    if(free_pages_.empty())
    {
        if(!try_evict_farthest_page(volume_center_, protect_closer_than))
        {
            return -1;
        }
    }
    if(free_pages_.empty())
    {
        return -1;
    }
    const uint32_t page = free_pages_.back();
    free_pages_.pop_back();
    pages_[page].allocated = true;
    pages_[page].last_use_frame = frame;
    pages_[page].card_index = UINT32_MAX;
    // Recycled pages keep old radiance until overwritten — that looked like knife-cut cyan.
    clear_atlas_page(uint16_t(page % PAGES_PER_AXIS), uint16_t(page / PAGES_PER_AXIS));
    return static_cast<int32_t>(page);
}

void surface_cache_gi_pass::clear_atlas_page(uint16_t page_x, uint16_t page_y)
{
    ensure_atlas();
    capture_.ensure_atlases();
    if(!clear_page_program_.program || !clear_page_program_.program->is_valid() || !atlas_ ||
       !capture_.material_atlas() || !capture_.emissive_atlas())
    {
        return;
    }
    float p0[4] = {float(page_x * PAGE_SIZE), float(page_y * PAGE_SIZE), float(PAGE_SIZE), 0.0f};
    gfx::render_pass pass("SurfaceCacheGI/PageClear");
    pass.bind();
    clear_page_program_.program->begin();
    gfx::set_uniform(clear_page_program_.u_fill_params0, p0);
    gfx::set_image(0, atlas_->native_handle(), 0, bgfx::Access::Write);
    gfx::set_image(1, capture_.material_atlas()->native_handle(), 0, bgfx::Access::Write);
    gfx::set_image(2, capture_.emissive_atlas()->native_handle(), 0, bgfx::Access::Write);
    const uint16_t groups = uint16_t((PAGE_SIZE + 7) / 8);
    bgfx::dispatch(pass.id, clear_page_program_.program->native_handle(), groups, groups, 1);
    clear_page_program_.program->end();
    gfx::discard();
}

void surface_cache_gi_pass::free_page(uint32_t page_index)
{
    if(page_index >= MAX_PAGES || !pages_[page_index].allocated)
    {
        return;
    }
    clear_atlas_page(uint16_t(page_index % PAGES_PER_AXIS), uint16_t(page_index / PAGES_PER_AXIS));
    pages_[page_index] = page_slot{};
    free_pages_.push_back(page_index);
}

void surface_cache_gi_pass::remove_card_at(uint32_t card_index)
{
    if(card_index >= cards_.size())
    {
        return;
    }
    const surface_key sk = make_surface_key(cards_[card_index]);
    sticky_upload_keys_.erase(make_card_key(cards_[card_index]));
    card_lookup_.erase(make_card_key(cards_[card_index]));
    const uint32_t page_index =
        uint32_t(cards_[card_index].page_y) * PAGES_PER_AXIS + uint32_t(cards_[card_index].page_x);
    free_page(page_index);
    auto count_it = surface_card_counts_.find(sk);
    if(count_it != surface_card_counts_.end())
    {
        if(count_it->second <= 1)
        {
            surface_card_counts_.erase(count_it);
            fully_spawned_surfaces_.erase(sk);
        }
        else
        {
            --count_it->second;
        }
    }
    const uint32_t last = uint32_t(cards_.size() - 1);
    // Drop removed index from sticky GPU rows; remap swapped-from-back indices.
    for(int i = int(sticky_upload_indices_.size()) - 1; i >= 0; --i)
    {
        if(sticky_upload_indices_[uint32_t(i)] == card_index)
        {
            sticky_upload_indices_.erase(sticky_upload_indices_.begin() + i);
        }
        else if(sticky_upload_indices_[uint32_t(i)] == last && card_index != last)
        {
            sticky_upload_indices_[uint32_t(i)] = card_index;
        }
    }
    if(card_index != last)
    {
        cards_[card_index] = cards_.back();
        card_lookup_[make_card_key(cards_[card_index])] = card_index;
        const uint32_t moved_page =
            uint32_t(cards_[card_index].page_y) * PAGES_PER_AXIS + uint32_t(cards_[card_index].page_x);
        if(moved_page < MAX_PAGES && pages_[moved_page].allocated)
        {
            pages_[moved_page].card_index = card_index;
        }
    }
    cards_.pop_back();
}

auto surface_cache_gi_pass::try_evict_farthest_page(const math::vec3& volume_center, float protect_closer_than)
    -> bool
{
    const float protect_sq = std::max(0.0f, protect_closer_than) * std::max(0.0f, protect_closer_than);
    uint32_t best_card = UINT32_MAX;
    float best_dist_sq = -1.0f;
    for(uint32_t i = 0; i < cards_.size(); ++i)
    {
        const math::vec3 d = cards_[i].origin - volume_center;
        const float dist_sq = math::dot(d, d);
        if(dist_sq + 1.0f < protect_sq)
        {
            continue;
        }
        // Prefer evicting cards already outside the volume.
        const float outside_boost = is_in_volume(cards_[i].origin, volume_bounds_) ? 1.0f : 4.0f;
        const float score = dist_sq * outside_boost;
        if(score > best_dist_sq)
        {
            best_dist_sq = score;
            best_card = i;
        }
    }
    if(best_card == UINT32_MAX)
    {
        return false;
    }
    remove_card_at(best_card);
    return true;
}

auto surface_cache_gi_pass::spawn_cards_for_bounds(uint32_t entity_id,
                                                   uint16_t submesh_index,
                                                   uint8_t instance_index,
                                                   const math::bbox& world_bounds,
                                                   const surface_cache_gi_settings& settings,
                                                   uint32_t frame,
                                                   int& new_card_budget) -> bool
{
    if(!world_bounds.is_populated() || new_card_budget <= 0)
    {
        return false;
    }
    const math::vec3 extents = world_bounds.get_extents();
    const math::vec3 bounds_closest = world_bounds.closest_point(volume_center_);
    const math::vec3 to_closest = bounds_closest - volume_center_;
    const float protect_closer_than = std::sqrt(std::max(0.0f, math::dot(to_closest, to_closest)));
    const surface_key sk = make_surface_key(entity_id, submesh_index, instance_index);
    bool completed = true;
    for(uint8_t face = 0; face < 6; ++face)
    {
        const math::vec2 face_half = surface_cache::face_half_extents(face, extents);
        const float face_area = 4.0f * face_half.x * face_half.y;
        if(face_area < settings.min_face_area)
        {
            continue;
        }
        uint8_t tiles_u = 1;
        uint8_t tiles_v = 1;
        surface_cache::compute_face_tile_counts(face,
                                               extents,
                                               settings.max_card_extent,
                                               MAX_TILES_PER_AXIS,
                                               tiles_u,
                                               tiles_v);
        for(uint8_t tu = 0; tu < tiles_u; ++tu)
        {
            for(uint8_t tv = 0; tv < tiles_v; ++tv)
            {
                const card_key key =
                    make_card_key(entity_id, submesh_index, instance_index, face, tu, tv);
                if(card_lookup_.find(key) != card_lookup_.end())
                {
                    continue;
                }
                if(new_card_budget <= 0)
                {
                    completed = false;
                    return completed;
                }
                if(cards_.size() >= MAX_CARDS)
                {
                    if(!try_evict_farthest_page(volume_center_, protect_closer_than) ||
                       cards_.size() >= MAX_CARDS)
                    {
                        completed = false;
                        return completed;
                    }
                }
                const int32_t page = allocate_page(frame, protect_closer_than);
                if(page < 0)
                {
                    completed = false;
                    return completed;
                }
                const auto frame_card =
                    surface_cache::make_face_tile_card(world_bounds, face, tu, tv, tiles_u, tiles_v);
                if(!is_in_volume(frame_card.origin, volume_bounds_))
                {
                    free_page(uint32_t(page));
                    continue;
                }
                surface_card card{};
                card.origin = frame_card.origin;
                card.normal = frame_card.normal;
                card.tangent = frame_card.tangent;
                card.bitangent = frame_card.bitangent;
                card.half_extents = frame_card.half_extents;
                card.page_x = uint16_t(page % PAGES_PER_AXIS);
                card.page_y = uint16_t(page / PAGES_PER_AXIS);
                card.entity_id = entity_id;
                card.submesh_index = submesh_index;
                card.instance_index = instance_index;
                card.face_index = face;
                card.tile_u = tu;
                card.tile_v = tv;
                card.tiles_u = tiles_u;
                card.tiles_v = tiles_v;
                card.alloc_frame = frame;
                card.last_project_frame = 0;
                card.dirty = true;
                card.needs_mesh_seed = true;
                card.seen_this_frame = true;
                pages_[uint32_t(page)].card_index = static_cast<uint32_t>(cards_.size());
                card_lookup_[key] = static_cast<uint32_t>(cards_.size());
                cards_.push_back(card);
                ++surface_card_counts_[sk];
                --new_card_budget;
            }
        }
    }
    if(completed && surface_card_counts_.count(sk) != 0 && surface_card_counts_[sk] > 0)
    {
        fully_spawned_surfaces_.insert(sk);
    }
    return completed;
}

auto surface_cache_gi_pass::lookup_surface_world_bounds(scene& scn,
                                                        uint32_t entity_id,
                                                        uint16_t submesh_index,
                                                        uint8_t instance_index) const -> math::bbox
{
    auto handle = scn.create_handle(entt::entity(entity_id));
    if(!handle)
    {
        return {};
    }
    auto* model_comp = handle.try_get<model_component>();
    if(!model_comp)
    {
        return {};
    }
    const math::bbox live_bounds = model_comp->get_world_bounds();
    const auto& proxies = model_comp->get_render_proxies();
    if(const auto* bounds = proxies.get_instance_bounds(submesh_index, instance_index))
    {
        if(proxies.has_instance_bounds() && live_bounds.is_populated())
        {
            const math::vec3 delta = live_bounds.get_center() - proxies.instance_bounds_union.get_center();
            if(math::dot(delta, delta) > 1e-6f)
            {
                return math::bbox(bounds->min + delta, bounds->max + delta);
            }
        }
        return *bounds;
    }
    return live_bounds;
}

void surface_cache_gi_pass::refresh_card_world_frames(scene& scn, const surface_cache_gi_settings& settings)
{
    APP_SCOPE_PERF("Rendering/Surface Cache GI/Refresh Frames");
    for(int i = static_cast<int>(cards_.size()) - 1; i >= 0; --i)
    {
        surface_card& card = cards_[uint32_t(i)];
        const math::bbox bounds =
            lookup_surface_world_bounds(scn, card.entity_id, card.submesh_index, card.instance_index);
        if(!bounds.is_populated())
        {
            remove_card_at(uint32_t(i));
            continue;
        }
        uint8_t tiles_u = card.tiles_u;
        uint8_t tiles_v = card.tiles_v;
        if(tiles_u == 0 || tiles_v == 0)
        {
            surface_cache::compute_face_tile_counts(card.face_index,
                                                   bounds.get_extents(),
                                                   settings.max_card_extent,
                                                   MAX_TILES_PER_AXIS,
                                                   tiles_u,
                                                   tiles_v);
            card.tiles_u = tiles_u;
            card.tiles_v = tiles_v;
        }
        if(card.tile_u >= tiles_u || card.tile_v >= tiles_v)
        {
            // Tile layout no longer valid for this bounds — respawn via discovery.
            const surface_key sk = make_surface_key(card);
            fully_spawned_surfaces_.erase(sk);
            remove_card_at(uint32_t(i));
            continue;
        }
        const auto frame = surface_cache::make_face_tile_card(bounds,
                                                              card.face_index,
                                                              card.tile_u,
                                                              card.tile_v,
                                                              tiles_u,
                                                              tiles_v);
        const math::vec3 delta = frame.origin - card.origin;
        const float moved_sq = math::dot(delta, delta);
        card.origin = frame.origin;
        card.normal = frame.normal;
        card.tangent = frame.tangent;
        card.bitangent = frame.bitangent;
        card.half_extents = frame.half_extents;
        if(moved_sq > 0.0001f)
        {
            card.dirty = true;
            // Bounds moved: allow discovery to refill any missing edge tiles.
            fully_spawned_surfaces_.erase(make_surface_key(card));
        }
    }
}

void surface_cache_gi_pass::discover_new_surfaces(scene& scn,
                                                  const camera& cam,
                                                  const surface_cache_gi_settings& settings,
                                                  const math::bbox& volume,
                                                  int new_card_budget)
{
    if(new_card_budget <= 0)
    {
        return;
    }
    const math::vec3 cam_pos = cam.get_position();
    struct candidate
    {
        uint32_t entity_id = 0;
        uint16_t submesh_index = 0;
        uint8_t instance_index = 0;
        math::bbox bounds{};
        float priority = 0.0f;
    };
    std::vector<candidate> candidates;
    candidates.reserve(256);
    auto try_add_candidate = [&](uint32_t entity_id,
                                 uint16_t submesh_index,
                                 uint8_t instance_index,
                                 const math::bbox& bounds) -> bool
    {
        if(!bounds.is_populated() || !bounds_intersect_volume(bounds, volume))
        {
            return false;
        }
        const surface_key sk = make_surface_key(entity_id, submesh_index, instance_index);
        if(fully_spawned_surfaces_.count(sk) != 0)
        {
            return false;
        }
        // Prefer surfaces with no cards yet (edge fill), then camera distance for update order.
        const float existing = float(surface_card_counts_.count(sk) ? surface_card_counts_.at(sk) : 0u);
        const math::vec3 closest = bounds.closest_point(cam_pos);
        const math::vec3 delta = closest - cam_pos;
        const float priority = existing * 1.0e6f + math::dot(delta, delta);
        candidates.push_back(
            candidate{entity_id, submesh_index, instance_index, bounds, priority});
        return true;
    };
    auto view = scn.registry->view<model_component, transform_component, active_component>();
    for(auto e : view)
    {
        const auto& model_comp = view.get<model_component>(e);
        if(model_comp.is_skinned())
        {
            continue;
        }
        const uint32_t entity_id = static_cast<uint32_t>(e);
        const math::bbox live_bounds = model_comp.get_world_bounds();
        if(!live_bounds.is_populated())
        {
            continue;
        }
        const auto& proxies = model_comp.get_render_proxies();
        // Per-submesh cards are required for Bistro (whole-model AABB only covers the outer shell).
        // Re-anchor stale proxy boxes to the live world-bounds center so IL tracks mesh moves.
        if(proxies.has_instance_bounds())
        {
            const math::vec3 live_center = live_bounds.get_center();
            const math::vec3 proxy_center = proxies.instance_bounds_union.get_center();
            const math::vec3 delta = live_center - proxy_center;
            const size_t submesh_count = proxies.instance_bounds.size();
            for(size_t sm = 0; sm < submesh_count; ++sm)
            {
                const auto& instances = proxies.instance_bounds[sm];
                const size_t instance_count = std::min(instances.size(), size_t(255));
                for(size_t inst = 0; inst < instance_count; ++inst)
                {
                    if(!instances[inst].is_populated())
                    {
                        continue;
                    }
                    math::bbox world_b = instances[inst];
                    if(math::dot(delta, delta) > 1e-6f)
                    {
                        world_b = math::bbox(world_b.min + delta, world_b.max + delta);
                    }
                    try_add_candidate(entity_id, uint16_t(sm), uint8_t(inst), world_b);
                }
            }
        }
        else
        {
            try_add_candidate(entity_id, 0, 0, live_bounds);
        }
    }
    std::sort(candidates.begin(),
              candidates.end(),
              [](const candidate& a, const candidate& b) { return a.priority < b.priority; });
    for(const candidate& c : candidates)
    {
        if(new_card_budget <= 0)
        {
            break;
        }
        spawn_cards_for_bounds(c.entity_id,
                               c.submesh_index,
                               c.instance_index,
                               c.bounds,
                               settings,
                               frame_index_,
                               new_card_budget);
    }
}

void surface_cache_gi_pass::sync_cards_from_scene(scene& scn,
                                                  const camera& cam,
                                                  const surface_cache_gi_settings& settings,
                                                  const math::bbox& volume)
{
    APP_SCOPE_PERF("Rendering/Surface Cache GI/Sync");
    volume_bounds_ = volume;
    volume_center_ = volume.get_center();
    // Track mesh transforms first — otherwise IL stays at the spawn-time world position.
    refresh_card_world_frames(scn, settings);
    // Never kill cards because the camera walked away — only leave the GI volume.
    for(int i = static_cast<int>(cards_.size()) - 1; i >= 0; --i)
    {
        if(is_in_volume(cards_[uint32_t(i)].origin, volume))
        {
            cards_[uint32_t(i)].seen_this_frame = true;
            const uint32_t page_index =
                uint32_t(cards_[uint32_t(i)].page_y) * PAGES_PER_AXIS + uint32_t(cards_[uint32_t(i)].page_x);
            if(page_index < MAX_PAGES)
            {
                pages_[page_index].last_use_frame = frame_index_;
            }
            continue;
        }
        remove_card_at(uint32_t(i));
    }
    const bool pool_empty = cards_.empty();
    const bool pool_thin = cards_.size() < (GATHER_UPLOAD_CAP * 3u) / 2u;
    const bool periodic = (frame_index_ % 2u) == 0u;
    if(pool_empty || pool_thin || periodic)
    {
        const int budget = pool_empty ? NEW_CARDS_PER_FRAME * 3
                                      : (pool_thin ? NEW_CARDS_PER_FRAME : NEW_CARDS_PER_FRAME / 2);
        discover_new_surfaces(scn, cam, settings, volume, std::max(32, budget));
    }
}

void surface_cache_gi_pass::seed_mesh_materials(scene& scn, int budget)
{
    APP_SCOPE_PERF("Rendering/Surface Cache GI/Mesh Seed");
    if(cards_.empty() || budget <= 0)
    {
        return;
    }
    capture_.ensure_atlases();
    int filled = 0;
    const uint32_t n = uint32_t(cards_.size());
    for(int i = 0; i < int(n) && filled < budget; ++i)
    {
        const uint32_t idx = (mesh_seed_cursor_ + uint32_t(i)) % n;
        surface_card& card = cards_[idx];
        if(!card.needs_mesh_seed)
        {
            continue;
        }
        math::vec3 albedo(0.35f, 0.35f, 0.35f);
        math::vec3 emissive(0.0f, 0.0f, 0.0f);
        gfx::texture::ptr color_map{};
        auto handle = scn.create_handle(entt::entity(card.entity_id));
        if(auto* model_comp = handle.try_get<model_component>())
        {
            material::sptr mat;
            const auto& overrides = model_comp->get_submesh_material_overrides();
            if(card.submesh_index < overrides.size() && overrides[card.submesh_index])
            {
                mat = overrides[card.submesh_index];
            }
            else
            {
                mat = model_comp->get_model().get_material_instance(card.submesh_index);
            }
            if(mat)
            {
                if(auto* pbr = dynamic_cast<pbr_material*>(mat.get()))
                {
                    const auto& bc = pbr->get_base_color();
                    albedo = math::vec3(bc.value.r, bc.value.g, bc.value.b);
                    const auto& ec = pbr->get_emissive_color();
                    const float ei = pbr->get_emissive_intensity();
                    emissive = math::vec3(ec.value.r, ec.value.g, ec.value.b) * ei;
                    const auto& cmap = pbr->get_color_map();
                    if(cmap)
                    {
                        color_map = cmap.get();
                    }
                }
            }
        }
        // Textured maps (red awnings with white tint) must seed chroma — tint-only was gray.
        if(color_map)
        {
            capture_.fill_page_textured(card.page_x, card.page_y, albedo, 0.55f, emissive, color_map);
        }
        else
        {
            capture_.fill_page(card.page_x, card.page_y, albedo, 0.35f, emissive);
        }
        card.needs_mesh_seed = false;
        card.dirty = true;
        ++filled;
    }
    mesh_seed_cursor_ = (mesh_seed_cursor_ + uint32_t(std::max(filled, 1))) % std::max(n, 1u);
}

void surface_cache_gi_pass::amortized_age_pages(const surface_cache_gi_settings& settings)
{
    if(!atlas_ || !age_program_.program || !age_program_.program->is_valid())
    {
        return;
    }
    constexpr int MAX_BATCH = 4;
    const int budget = std::max(1, settings.pages_per_frame);
    float page_origins[MAX_BATCH * 2];
    for(int i = 0; i < MAX_BATCH * 2; ++i)
    {
        page_origins[i] = -1.0f;
    }
    int batch_count = 0;
    const int scan_limit = std::max(budget * 4, MAX_BATCH);
    for(int n = 0; n < scan_limit && batch_count < MAX_BATCH && batch_count < budget; ++n)
    {
        const uint32_t page_index = age_cursor_ % MAX_PAGES;
        age_cursor_++;
        if(!pages_[page_index].allocated)
        {
            continue;
        }
        const uint32_t card_index = pages_[page_index].card_index;
        if(card_index >= cards_.size())
        {
            continue;
        }
        const surface_card& card = cards_[card_index];
        const uint32_t age = frame_index_ - card.last_project_frame;
        if(card.last_project_frame == 0 || age < uint32_t(std::max(1, settings.stale_frames)))
        {
            continue;
        }
        page_origins[batch_count * 2 + 0] = float(card.page_x * PAGE_SIZE);
        page_origins[batch_count * 2 + 1] = float(card.page_y * PAGE_SIZE);
        ++batch_count;
    }
    if(batch_count <= 0)
    {
        return;
    }
    float age_params0[4] = {page_origins[0], page_origins[1], page_origins[2], page_origins[3]};
    float age_params1[4] = {page_origins[4], page_origins[5], page_origins[6], page_origins[7]};
    float age_params2[4] = {float(PAGE_SIZE), 0.98f, float(batch_count), 0.0f};
    gfx::render_pass pass("SurfaceCacheGI/Age");
    pass.bind();
    age_program_.program->begin();
    gfx::set_uniform(age_program_.u_age_params0, age_params0);
    gfx::set_uniform(age_program_.u_age_params1, age_params1);
    gfx::set_uniform(age_program_.u_age_params2, age_params2);
    gfx::set_image(0, atlas_->native_handle(), 0, bgfx::Access::ReadWrite);
    const uint16_t groups = uint16_t((PAGE_SIZE + 7) / 8);
    bgfx::dispatch(pass.id, age_program_.program->native_handle(), groups, groups, 1);
    age_program_.program->end();
    gfx::discard();
}

auto surface_cache_gi_pass::gather_lights(scene& scn, const math::bbox& volume) const
    -> card_lighting::light_env
{
    card_lighting::light_env env{};
    float best_sun = 0.0f;
    struct scored_point
    {
        card_lighting::point_light light{};
        float score = 0.0f;
    };
    std::vector<scored_point> points;
    points.reserve(32);
    const math::vec3 vol_center = volume.get_center();
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto /*e*/, transform_component& transform, light_component& light_comp, auto&& /*active*/)
        {
            const light& L = light_comp.get_light();
            const math::vec3 color(L.color.value.r, L.color.value.g, L.color.value.b);
            if(L.type == light_type::directional)
            {
                if(L.intensity <= best_sun)
                {
                    return;
                }
                best_sun = L.intensity;
                env.has_sun = true;
                env.sun_intensity = L.intensity;
                env.sun_color = color;
                env.sun_direction = transform.get_transform_global().z_unit_axis();
                // Only accept sun when CSM is available — unshadowed N·L paints floors under arches.
                env.sun_shadows = (L.casts_shadows) ? &light_comp.get_shadowmap_generator() : nullptr;
                return;
            }
            if(L.type != light_type::point && L.type != light_type::spot)
            {
                return;
            }
            if(L.intensity <= 1e-4f)
            {
                return;
            }
            const math::vec3 pos = transform.get_position_global();
            float range = (L.type == light_type::point) ? L.point_data.range : L.spot_data.get_range();
            range = std::max(range, 0.5f);
            const math::vec3 closest = volume.closest_point(pos);
            const float dist_to_vol = math::length(pos - closest);
            if(dist_to_vol > range)
            {
                return;
            }
            const float lum = L.intensity * (color.x * 0.3f + color.y * 0.6f + color.z * 0.1f);
            const float center_dist = math::length(pos - vol_center);
            const float score = lum * range / (1.0f + center_dist * 0.05f);
            scored_point sp{};
            sp.light.position = pos;
            sp.light.range = range;
            sp.light.color = color;
            sp.light.intensity = L.intensity;
            sp.score = score;
            points.push_back(sp);
        });
    std::sort(points.begin(),
              points.end(),
              [](const scored_point& a, const scored_point& b) { return a.score > b.score; });
    env.point_count = int(std::min<size_t>(points.size(), card_lighting::MAX_POINT_LIGHTS));
    for(int i = 0; i < env.point_count; ++i)
    {
        env.points[size_t(i)] = points[size_t(i)].light;
    }
    return env;
}

void surface_cache_gi_pass::run_material_emissive_capture(const run_params& params,
                                                          uint32_t project_count,
                                                          bool write_radiance)
{
    if(project_count == 0 || !params.g_buffer || !params.direct_lighting || !project_program_.program ||
       !project_program_.program->is_valid() || !capture_.material_atlas() || !capture_.emissive_atlas())
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Surface Cache GI/Material Capture");
    const auto gsize = params.g_buffer->get_size();
    const math::vec3 cam_pos = params.cam->get_position();
    // Material refine must prefer G-buffer albedo over mesh tint; keep history soft.
    const float hist = write_radiance ? 0.30f : std::min(params.settings.project_history, 0.40f);
    float project_params[4] = {float(project_count),
                               float(PAGE_SIZE) / float(ATLAS_SIZE),
                               params.settings.card_thickness,
                               hist};
    float scache_params2[4] = {float(ATLAS_SIZE),
                               write_radiance ? 1.0f : 0.0f,
                               0.0f,
                               params.settings.max_gather_distance};
    float cam_pos4[4] = {cam_pos.x, cam_pos.y, cam_pos.z, write_radiance ? params.settings.gather_intensity : 0.0f};
    gfx::render_pass pass(write_radiance ? "SurfaceCacheGI/Project" : "SurfaceCacheGI/MaterialRefine");
    pass.bind();
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    project_program_.program->begin();
    gfx::set_uniform(project_program_.u_scache_params, project_params);
    gfx::set_uniform(project_program_.u_scache_params2, scache_params2);
    gfx::set_uniform(project_program_.u_camera_position, cam_pos4);
    gfx::set_texture(project_program_.s_gbuffer0, 0, params.g_buffer->get_texture(0));
    gfx::set_texture(project_program_.s_gbuffer1, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(project_program_.s_gbuffer4, 2, params.g_buffer->get_texture(4));
    gfx::set_texture(project_program_.s_direct, 3, params.direct_lighting);
    gfx::set_texture(project_program_.s_cards, 4, card_tex_);
    gfx::set_texture(project_program_.s_gbuffer2, 5, params.g_buffer->get_texture(2));
    gfx::set_image(6, atlas_->native_handle(), 0, bgfx::Access::ReadWrite);
    gfx::set_image(7, capture_.material_atlas()->native_handle(), 0, bgfx::Access::ReadWrite);
    gfx::set_image(8, capture_.emissive_atlas()->native_handle(), 0, bgfx::Access::ReadWrite);
    const uint16_t gx = uint16_t((gsize.width + 7) / 8);
    const uint16_t gy = uint16_t((gsize.height + 7) / 8);
    bgfx::dispatch(pass.id, project_program_.program->native_handle(), gx, gy, 1);
    project_program_.program->end();
    gfx::discard();
}

void surface_cache_gi_pass::update_world_cache(gfx::render_view& rview, const run_params& params)
{
    APP_SCOPE_PERF("Rendering/Surface Cache GI/World Update");
    (void)rview;
    if(!params.cam || !params.scn)
    {
        return;
    }
    ++frame_index_;
    ensure_atlas();
    capture_.ensure_atlases();
    last_camera_position_ = params.cam->get_position();
    const math::bbox volume = resolve_volume(params);
    volume_bounds_ = volume;
    volume_center_ = volume.get_center();
    sync_cards_from_scene(*params.scn, *params.cam, params.settings, volume);
    seed_mesh_materials(*params.scn, MESH_FILLS_PER_FRAME);
    upload_card_texture(*params.cam, params.settings, volume);
    const uint32_t project_count = std::min(uploaded_card_count_, PROJECT_UPLOAD_CAP);
    // Material/emissive refine only — never write screen radiance (that made GI view-unstable).
    if(params.g_buffer && params.direct_lighting && project_count > 0)
    {
        run_material_emissive_capture(params, project_count, false);
    }
    card_lighting::light_env lights = gather_lights(*params.scn, volume);
    int sun_soft_reset = 0;
    if(lights.has_sun)
    {
        const math::vec3 sun_dir = math::normalize(lights.sun_direction);
        if(has_sun_direction_)
        {
            const float sun_dot = math::dot(sun_dir, last_sun_direction_);
            if(sun_dot < 0.985f) // ~10 degrees
            {
                sun_soft_reset = 5;
                probes_.request_soft_reset(5);
                for(auto& card : cards_)
                {
                    card.dirty = true;
                }
            }
        }
        last_sun_direction_ = sun_dir;
        has_sun_direction_ = true;
    }
    if(card_tex_ && uploaded_card_count_ > 0)
    {
        opacity_.update_from_cards(params.cam->get_position(),
                                   std::clamp(params.settings.max_gather_distance, 24.0f, 64.0f),
                                   card_tex_,
                                   uploaded_card_count_,
                                   params.settings.card_thickness);
        ray_query_.bind_opacity(opacity_.volume(),
                                opacity_.origin(),
                                opacity_.voxel_size(),
                                opacity_.dims());
    }
    {
        // Priority: dirty + largest |ΔN·L| first so sun rotate refreshes emitters fast.
        struct scored_row
        {
            uint32_t row = 0;
            float score = 0.0f;
        };
        std::vector<scored_row> scored;
        scored.reserve(uploaded_card_count_);
        const math::vec3 to_sun =
            lights.has_sun ? math::normalize(-lights.sun_direction) : math::vec3{0.0f, 1.0f, 0.0f};
        for(uint32_t row = 0; row < uploaded_card_count_; ++row)
        {
            const uint32_t card_index = sticky_upload_indices_[row];
            if(card_index >= cards_.size())
            {
                continue;
            }
            auto& card = cards_[card_index];
            const float ndotl = std::max(0.0f, math::dot(card.normal, to_sun));
            float delta = (card.last_sun_ndotl < 0.0f) ? 1.0f : std::abs(ndotl - card.last_sun_ndotl);
            card.last_sun_ndotl = ndotl;
            float score = delta * 4.0f + ndotl * 1.5f + (card.dirty ? 3.0f : 0.0f);
            // Prefer floors slightly for bounce seeds.
            score += std::max(0.0f, card.normal.y) * 0.35f;
            scored.push_back({row, score});
        }
        std::sort(scored.begin(),
                  scored.end(),
                  [](const scored_row& a, const scored_row& b) { return a.score > b.score; });
        std::vector<uint32_t> light_rows;
        light_rows.reserve(scored.size());
        for(const auto& s : scored)
        {
            light_rows.push_back(s.row);
        }
        if(light_rows.empty())
        {
            for(uint32_t row = 0; row < std::min(uploaded_card_count_, 24u); ++row)
            {
                light_rows.push_back(row);
            }
        }
        card_lighting::run_params lit{};
        lit.atlas = atlas_;
        lit.material = capture_.material_atlas();
        lit.emissive = capture_.emissive_atlas();
        lit.cards = card_tex_;
        lit.irradiance_sh = params.irradiance_sh;
        lit.opacity_volume = opacity_.volume();
        lit.dirty_upload_indices = &light_rows;
        lit.uploaded_card_count = uploaded_card_count_;
        lit.page_uv_size = float(PAGE_SIZE) / float(ATLAS_SIZE);
        lit.atlas_size = float(ATLAS_SIZE);
        lit.history = sun_soft_reset > 0 ? 0.25f : 0.45f;
        lit.max_gather_distance = params.settings.max_gather_distance;
        lit.gather_intensity = 1.0f;
        lit.bounce_strength = params.settings.bounce_strength;
        lit.card_thickness = params.settings.card_thickness;
        lit.seed_with_skylight = params.settings.seed_with_skylight;
        lit.use_priority_order = true;
        lit.pages_per_frame = std::clamp(params.settings.pages_per_frame + (sun_soft_reset > 0 ? 24 : 0), 8, 64);
        lit.lights = lights;
        lit.opacity_origin = opacity_.origin();
        lit.opacity_dims = opacity_.dims();
        lit.opacity_voxel_size = opacity_.voxel_size();
        lit.opacity_enabled = opacity_.is_valid();
        const std::vector<uint32_t> lit_rows = card_lighting_.update_dirty_pages(lit);
        for(uint32_t row : lit_rows)
        {
            if(row >= sticky_upload_indices_.size())
            {
                continue;
            }
            const uint32_t card_index = sticky_upload_indices_[row];
            if(card_index < cards_.size())
            {
                cards_[card_index].dirty = false;
                cards_[card_index].last_project_frame = frame_index_;
            }
        }
    }
    amortized_age_pages(params.settings);
    gi_probe_volume::update_params probe_up{};
    probe_up.cam = params.cam;
    probe_up.atlas = atlas_;
    probe_up.cards = card_tex_;
    probe_up.irradiance_sh = params.irradiance_sh;
    probe_up.opacity_volume = opacity_.volume();
    probe_up.opacity_origin = opacity_.origin();
    probe_up.opacity_dims = opacity_.dims();
    probe_up.opacity_voxel_size = opacity_.voxel_size();
    probe_up.opacity_enabled = opacity_.is_valid();
    probe_up.card_count = std::min(uploaded_card_count_, gi_probe_volume::PROBE_GATHER_CARDS);
    probe_up.page_uv_size = float(PAGE_SIZE) / float(ATLAS_SIZE);
    probe_up.card_thickness = params.settings.card_thickness;
    probe_up.cfg.near_extent = std::max(params.settings.probe_near_extent, 40.0f);
    probe_up.cfg.far_extent = std::max(params.settings.probe_far_extent, 120.0f);
    probe_up.cfg.probes_per_frame = std::max(params.settings.probes_per_frame, 160);
    probe_up.cfg.probe_history = std::clamp(params.settings.probe_history, 0.75f, 0.95f);
    probe_up.cfg.gather_distance = std::clamp(params.settings.max_gather_distance, 8.0f, 48.0f);
    probe_up.cfg.gather_intensity = params.settings.gather_intensity;
    probe_up.cfg.cache_blend = params.settings.cache_blend;
    probe_up.cfg.seed_with_skylight = params.settings.seed_with_skylight;
    probe_up.cfg.sun_soft_reset_frames = sun_soft_reset;
    ray_query_.select_backend(false);
    probes_.update_world(rview, probe_up);
    rview.tex_get_or_emplace("SURFACE_CACHE_ATLAS") = atlas_srv_ ? atlas_srv_ : atlas_;
    rview.tex_get_or_emplace("SURFACE_CACHE_MATERIAL") = capture_.material_atlas();
    rview.tex_get_or_emplace("SURFACE_CACHE_EMISSIVE") = capture_.emissive_atlas();
}

auto surface_cache_gi_pass::sample_frame(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/Surface Cache GI/Sample");
    if(!params.g_buffer || !params.cam)
    {
        return {};
    }
    resolve_atlas_for_sample();
    if(atlas_srv_)
    {
        rview.tex_get_or_emplace("SURFACE_CACHE_ATLAS") = atlas_srv_;
    }
    gi_probe_volume::sample_params sample{};
    sample.cam = params.cam;
    sample.g_buffer = params.g_buffer;
    sample.irradiance_sh = params.irradiance_sh;
    sample.atlas_srv = atlas_srv_;
    sample.cards = card_tex_;
    sample.card_count = std::min(uploaded_card_count_, gi_probe_volume::PROBE_GATHER_CARDS);
    sample.page_uv_size = float(PAGE_SIZE) / float(ATLAS_SIZE);
    sample.card_thickness = params.settings.card_thickness;
    sample.cfg.near_extent = std::max(params.settings.probe_near_extent, 40.0f);
    sample.cfg.far_extent = std::max(params.settings.probe_far_extent, 120.0f);
    sample.cfg.probes_per_frame = std::max(params.settings.probes_per_frame, 160);
    sample.cfg.probe_history = std::clamp(params.settings.probe_history, 0.75f, 0.95f);
    sample.cfg.gather_distance = std::clamp(params.settings.max_gather_distance, 8.0f, 64.0f);
    sample.cfg.gather_intensity = params.settings.gather_intensity;
    sample.cfg.cache_blend = params.settings.cache_blend;
    sample.cfg.seed_with_skylight = params.settings.seed_with_skylight;
    return probes_.sample_frame(rview, sample);
}

auto surface_cache_gi_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/Surface Cache GI");
    if(!params.g_buffer || !params.direct_lighting || !params.cam || !params.scn)
    {
        return {};
    }
    update_world_cache(rview, params);
    return sample_frame(rview, params);
}

void surface_cache_gi_pass::release_resources(gfx::render_view& rview)
{
    rview.tex_remove("SURFACE_CACHE_GI");
    rview.fbo_remove("SURFACE_CACHE_GI");
    rview.tex_remove("SURFACE_CACHE_ATLAS");
    rview.tex_remove("SURFACE_CACHE_MATERIAL");
    rview.tex_remove("SURFACE_CACHE_EMISSIVE");
    rview.tex_remove("GI_PROBE_VOLUME");
    rview.tex_remove("PREV_SURFACE_CACHE_GI");
}

} // namespace unravel
