#include "model.h"
#include "gpu_program.h"
#include "graphics/graphics.h"
#include "graphics/index_buffer.h"
#include "graphics/vertex_buffer.h"
#include "material.h"
#include "mesh.h"
#include "camera.h"
#include "batch_collector.h"
#include "skinning_palette_buffer.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace unravel
{

namespace
{

/**
 * Looks up the cached world-space AABB for a submesh instance from the retained proxies.
 * Returns nullptr when no cached data is available (legacy assets, stale poses, etc.).
 */
auto get_cached_submesh_bounds(const model_submit_extras& extras,
                               uint32_t submesh_index,
                               size_t instance_index,
                               bool skinned) -> const math::bbox*
{
    if(extras.proxies == nullptr)
    {
        return nullptr;
    }
    return skinned ? extras.proxies->get_skinned_bounds(submesh_index)
                   : extras.proxies->get_instance_bounds(submesh_index, instance_index);
}

/**
 * Classifies a submesh instance against a frustum using the retained world-space AABB
 * (cheap plane tests, valid for skinned poses too). Bind-pose local bounds are never
 * used for culling - they don't reflect node/bone animation - so when no cached data
 * exists the submesh is conservatively treated as visible.
 */
auto classify_submesh_cached(const math::frustum& frustum,
                             const model_submit_extras& extras,
                             uint32_t submesh_index,
                             size_t instance_index,
                             bool skinned) -> math::volume_query
{
    const auto* bounds = get_cached_submesh_bounds(extras, submesh_index, instance_index, skinned);
    if(bounds != nullptr)
    {
        return frustum.classify_aabb(*bounds);
    }
    // No cached data - conservatively treat as visible.
    return math::volume_query::intersect;
}

auto is_submesh_visible_cached(const math::frustum& frustum,
                               const model_submit_extras& extras,
                               uint32_t submesh_index,
                               size_t instance_index,
                               bool skinned) -> bool
{
    return classify_submesh_cached(frustum, extras, submesh_index, instance_index, skinned) !=
           math::volume_query::outside;
}

auto compute_bounds_screen_radius_squared(const math::vec3& origin,
                                                float radius,
                                                const math::vec3& view_origin,
                                                const math::mat4& projection) -> float
{
    const float screen_multiple = 0.5f * std::max(std::abs(projection[0][0]), std::abs(projection[1][1]));
    float projection_w_scale = std::abs(projection[2][3]);
    if(projection_w_scale < 0.000001f)
    {
        projection_w_scale = 1.0f;
    }
    const float dist_sqr = glm::length2(origin - view_origin) * projection_w_scale;
    // Guard against division by zero only. Clamping to 1.0 froze the projected size of anything closer than
    // 1 world unit (= 1m here), making close-up geometry - especially small individual
    // submeshes - report a tiny screen radius and drop to coarser LODs while filling
    // the screen.
    constexpr float min_dist_sqr = 0.0001f; // (1cm)^2
    return math::square(screen_multiple * radius) / std::max(min_dist_sqr, dist_sqr);
}

auto compute_bounds_screen_radius_squared(const math::vec3& origin, float radius, const camera& view) -> float
{
    return compute_bounds_screen_radius_squared(origin, radius, view.get_position(), view.get_projection().get_matrix());
}

auto compute_submesh_world_bounds_sphere(const mesh::submesh& sm, const math::mat4& world_matrix) -> math::bsphere
{
    const math::transform world_transform(world_matrix);
    const math::vec3 local_center = sm.bbox.get_center();
    const math::vec3 local_extents = sm.bbox.get_extents();
    const float local_radius = glm::length(local_extents);
    const math::vec3 world_center = world_transform.transform_coord(local_center);
    const auto scale = world_transform.get_scale();
    const float max_scale = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
    return math::bsphere{world_center, local_radius * max_scale};
}

auto compute_screen_rect_from_sphere(const camera& cam, const math::vec3& world_center, float screen_radius) -> irect32_t
{
    const auto& viewport_pos = cam.get_viewport_pos();
    const auto& viewport_size = cam.get_viewport_size();
    if(viewport_size.width == 0 || viewport_size.height == 0)
    {
        return {};
    }
    const auto view_proj = cam.get_view_projection();
    math::vec4 clip = view_proj * math::vec4{world_center.x, world_center.y, world_center.z, 1.0f};
    const float clip_w = clip.w;
    if(std::abs(clip_w) < 0.000001f)
    {
        return {viewport_pos.x,
                viewport_pos.y,
                viewport_pos.x + static_cast<std::int32_t>(viewport_size.width),
                viewport_pos.y + static_cast<std::int32_t>(viewport_size.height)};
    }
    const float recip_w = 1.0f / clip_w;
    const float ndc_x = clip.x * recip_w;
    const float ndc_y = clip.y * recip_w;
    const float center_x = ((ndc_x * 0.5f) + 0.5f) * float(viewport_size.width) + float(viewport_pos.x);
    const float center_y = ((ndc_y * -0.5f) + 0.5f) * float(viewport_size.height) + float(viewport_pos.y);
    const float radius_px = screen_radius * float(viewport_size.height);
    const float left_f = center_x - radius_px;
    const float right_f = center_x + radius_px;
    const float top_f = center_y - radius_px;
    const float bottom_f = center_y + radius_px;
    const std::int32_t min_x = viewport_pos.x;
    const std::int32_t min_y = viewport_pos.y;
    const std::int32_t max_x = viewport_pos.x + static_cast<std::int32_t>(viewport_size.width);
    const std::int32_t max_y = viewport_pos.y + static_cast<std::int32_t>(viewport_size.height);
    const std::int32_t left = math::clamp(static_cast<std::int32_t>(std::floor(left_f)), min_x, max_x);
    const std::int32_t right = math::clamp(static_cast<std::int32_t>(std::ceil(right_f)), min_x, max_x);
    const std::int32_t top = math::clamp(static_cast<std::int32_t>(std::floor(top_f)), min_y, max_y);
    const std::int32_t bottom = math::clamp(static_cast<std::int32_t>(std::ceil(bottom_f)), min_y, max_y);
    return {left, top, right, bottom};
}
}

void lod_data::calculate_screen_rect(const camera& cam)
{
    const auto& viewport_size = cam.get_viewport_size();
    const auto& viewport_pos = cam.get_viewport_pos();
  
    float screen_radius = percent * 0.005f;
    rect = compute_screen_rect_from_sphere(cam, center, screen_radius);
}


auto model::is_valid() const -> bool
{
    return !mesh_lods_.empty();
}

auto model::get_lod(uint32_t lod) const -> const asset_handle<mesh>&
{
    static const asset_handle<mesh> none{};
    if(mesh_lods_.empty())
    {
        return none;
    }

    lod = math::clamp<std::size_t>(lod, 0, mesh_lods_.size() - 1);

    for(int i = int(lod); i >= 0; --i)
    {
        const auto& lod_mesh = mesh_lods_[i];
        if(lod_mesh)
        {
            return lod_mesh;
        }
    }

    return none;
}

void model::set_lod(asset_handle<mesh> mesh, uint32_t lod)
{
    bool recalculate_lod_limits = false;
    if(lod >= mesh_lods_.size())
    {
        mesh_lods_.resize(lod + 1);

        recalculate_lod_limits = true;

    }
    mesh_lods_[lod] = mesh;

    if(recalculate_lod_limits)
    {
        recalulate_lod_screen_size_limits(get_lods_count());
    }

    resize_materials(mesh);
}

void model::set_shared_material(asset_handle<material> material, uint32_t index)
{
    if(index >= materials_.size())
    {
        materials_.resize(index + 1);
    }
    materials_[index] = std::move(material);
    sync_material_instances();
}

auto model::apply_default_materials(asset_manager& am, bool wait) -> bool
{
    const auto& lod = get_lod(0);
    if(!lod)
    {
        return false;
    }
    const auto mesh_ptr = wait ? lod.get() : lod.try_get();
    if(!mesh_ptr || asset_handle<mesh>::is_empty(mesh_ptr))
    {
        return false;
    }
    const auto& material_uids = mesh_ptr->get_default_material_uids();
    for(size_t i = 0; i < material_uids.size(); ++i)
    {
        const auto index = static_cast<uint32_t>(i);
        // set_lod fills every new slot with the default material, so that one counts as no choice.
        const auto current = get_shared_material(index);
        if(current && current != default_material())
        {
            continue;
        }
        if(auto mat_asset = am.get_asset<material>(material_uids[i]))
        {
            set_shared_material(mat_asset, index);
        }
    }
    default_materials_pending_ = false;
    return true;
}

void model::set_default_materials_pending(bool pending)
{
    default_materials_pending_ = pending;
}

auto model::is_default_materials_pending() const -> bool
{
    return default_materials_pending_;
}


auto model::get_lods() const -> const std::vector<asset_handle<mesh>>&
{
    return mesh_lods_;
}

auto model::get_lods_count() const -> uint32_t
{
    if(mesh_lods_.empty())
    {
        return 0;
    }
    // If there's only one mesh, it might have internal LODs (automatic generation)
    if(mesh_lods_.size() == 1)
    {
        const auto& mesh_asset = mesh_lods_[0];
        if(mesh_asset)
        {
            // Read in place (waited for while it loads, as get() does): this runs per model per
            // frame from every pool thread, and a get() counts a reference on the mesh every model
            // of it shares.
            const std::optional<uint32_t> count = mesh_asset.visit(
                [](mesh* loaded) -> std::optional<uint32_t>
                {
                    return loaded != nullptr ? std::optional<uint32_t>(loaded->get_lod_count()) : std::nullopt;
                },
                true);
            if(count)
            {
                return *count;
            }
        }
    }
    // Otherwise, return the number of separate mesh LODs (manual LODs)
    return static_cast<uint32_t>(mesh_lods_.size());
}

void model::set_lods(const std::vector<asset_handle<mesh>>& lods)
{
    // Waits for the first mesh: the lod count and the material slots are read from it.
    APP_SCOPE_PERF("Model/Set Lods");
    mesh_lods_ = lods;

    recalulate_lod_screen_size_limits(get_lods_count());

    if(!mesh_lods_.empty())
    {
        auto& mesh = mesh_lods_[0];
        resize_materials(mesh);
    }
}

auto model::get_shared_materials() const -> const std::vector<asset_handle<material>>&
{
    return materials_;
}

auto model::get_material_instances() const -> const std::vector<material_instance>&
{
    return material_instances_;
}

void model::set_shared_materials(const std::vector<asset_handle<material>>& materials)
{
    materials_ = materials;
    sync_material_instances();
}

void model::set_material_instances(const std::vector<material_instance>& instances)
{
    material_instances_ = instances;
    sync_material_instances();
}

auto model::get_shared_material(uint32_t index) const -> const asset_handle<material>&
{
    static const asset_handle<material> none{};
    if(materials_.size() <= index)
    {
        return none;
    }
    return materials_[index];
}

auto model::get_material_instance(uint32_t index) const -> const material_instance&
{
    static const material_instance none;
    return index < material_instances_.size() ? material_instances_[index] : none;
}

auto model::get_material_instance(uint32_t index) -> material_instance&
{
    if(index >= material_instances_.size())
    {
        material_instances_.resize(index + 1);
    }
    return material_instances_[index];
}

auto model::get_render_material(uint32_t index) const -> material::sptr
{
    if(index < material_instances_.size())
    {
        const auto& instance = material_instances_[index];
        if(instance.has_overrides() && instance.get_resolved())
        {
            return instance.get_resolved();
        }
    }
    const auto& shared = get_shared_material(index);
    return shared.is_valid() ? shared.get() : nullptr;
}

auto model::get_slot_batch_material(uint32_t index) const -> slot_batch_material
{
    if(index < material_instances_.size())
    {
        const auto& instance = material_instances_[index];
        const auto& resolved = instance.get_resolved();
        if(resolved && instance.has_overrides() && instance.get_resolved_shares_draw())
        {
            const auto shared = get_shared_material(index);
            if(auto shared_material = shared.is_valid() ? shared.get() : nullptr)
            {
                return {std::move(shared_material), resolved.get()};
            }
        }
    }
    return {get_render_material(index), nullptr};
}

void model::update_material_instances()
{
    for(auto& instance : material_instances_)
    {
        instance.update_resolved();
    }
}

void model::adopt_resolved_materials(model& previous)
{
    const size_t slot_count = std::min(material_instances_.size(), previous.material_instances_.size());
    for(size_t slot = 0; slot < slot_count; ++slot)
    {
        material_instances_[slot].adopt_resolved(previous.material_instances_[slot]);
    }
}

void model::sync_material_instances()
{
    if(material_instances_.size() < materials_.size())
    {
        material_instances_.resize(materials_.size());
    }
    for(size_t i = 0; i < materials_.size(); ++i)
    {
        material_instances_[i].set_shared_material(materials_[i]);
    }
}


auto model::calculate_lod_data(lod_data& data, const math::bbox& world_bounds, const camera& cam, float dt) const -> bool
{
    data.transition_time = get_lod_transition_time().count();
    // The first LOD's handle, checked in place: this runs once per model from every pool thread,
    // and a copy of the handle counts a reference on the link every model of that mesh shares.
    if(mesh_lods_.empty() || !mesh_lods_.front())
    {
        return false;
    }
    // The LOD count is resolved on demand, because for a single mesh with internal LODs it lives
    // on the LOADED asset and get_lods_count() pays a get() to ask (see above). The selection
    // loop needs a screen-size limit per LOD, so with fewer than two limits it can never run
    // whatever the count is; without an override or a bias the count is never needed at all.
    uint32_t lod_count = 0;
    bool lod_count_resolved = false;
    const auto resolve_lod_count = [&]() -> uint32_t
    {
        if(!lod_count_resolved)
        {
            lod_count = get_lods_count();
            lod_count_resolved = true;
        }
        return lod_count;
    };

    // Unpopulated bounds mean the mesh has not been loaded/measured yet - nothing to size.
    if(!world_bounds.is_populated())
    {
        return false;
    }

    // Enclosing sphere of the pose-aware world AABB. Same conservative construction the
    // bind-pose path used, but built from the box that tracks the actual rendered geometry.
    const math::bsphere bsphere{world_bounds.get_center(), glm::length(world_bounds.get_extents())};
    const float screen_radius_squared = compute_bounds_screen_radius_squared(bsphere.position, bsphere.radius, cam);

    const float screen_radius = std::sqrt(std::max(0.0f, screen_radius_squared));
    data.percent = math::clamp(screen_radius * 200.0f, 0.0f, 100.0f);
    data.center = bsphere.position;

    const float lod_screen_size_min = 0.005f;
    const float cull_threshold_squared = math::square(lod_screen_size_min * 0.5f);
    const bool is_visible = cull_threshold_squared <= screen_radius_squared;
    if(!is_visible)
    {
        return false;
    }

    std::size_t lod = 0;
    if(lod_override_enabled_)
    {
        lod = math::clamp<std::size_t>(lod_override_level_, 0, resolve_lod_count() - 1);
    }
    // Left to right: the count is only resolved once the limits exist for it to matter, and
    // the third test reads the value that resolution stored.
    else if(lod_screen_sizes_.size() > 1 && resolve_lod_count() > 1 && lod_screen_sizes_.size() >= lod_count)
    {
        // Use current LOD for hysteresis (what's being displayed, accounting for transitions)
        const uint32_t prev_lod = data.current_lod_index;
        const float hysteresis = lod_hysteresis_;
        
        for(std::int32_t lod_index = static_cast<std::int32_t>(lod_count) - 1; lod_index >= 0; --lod_index)
        {
            const auto index = static_cast<size_t>(lod_index);
            float screen_size = lod_screen_sizes_[index];
            float screen_size_squared = math::square(screen_size * 0.5f);
            
            // Apply hysteresis to create a "sticky" dead zone around the current LOD
            // This prevents rapid switching at LOD boundaries
            if(prev_lod == index)
            {
                // Currently at this LOD - INCREASE threshold to make it easier to stay
                // (larger threshold = condition more likely to be true)
                float adjusted_size = screen_size * (1.0f + hysteresis);
                screen_size_squared = math::square(adjusted_size * 0.5f);
            }
            else
            {
                // Different from current LOD - DECREASE threshold to resist change
                // (smaller threshold = condition less likely to be true)
                float adjusted_size = screen_size * (1.0f - hysteresis);
                screen_size_squared = math::square(adjusted_size * 0.5f);
            }
            
            if(screen_size_squared >= screen_radius_squared)
            {
                lod = static_cast<std::size_t>(lod_index);
                break;
            }
        }
    }

    // Every path above leaves lod below the count, so with no bias the clamp is an identity and
    // the count it would need stays unresolved.
    if(lod_selection_bias_ != 0.0f)
    {
        float biased_lod = static_cast<float>(lod) + lod_selection_bias_;
        biased_lod = math::clamp(biased_lod, 0.0f, static_cast<float>(resolve_lod_count() - 1));
        lod = static_cast<std::size_t>(biased_lod);
    }

    // Hysteresis determined new LOD - now handle transition timing
    // Only trigger a new transition if we're not currently transitioning
    if(data.target_lod_index != lod && data.target_lod_index == data.current_lod_index)
    {
        data.target_lod_index = static_cast<std::uint32_t>(lod);
        data.current_time = 0.0f;
    }

    // Update transition progress
    if(data.current_lod_index != data.target_lod_index)
    {
        data.current_time += dt;
    }

    // Complete transition when time elapsed
    if(data.current_time >= data.transition_time)
    {
        data.current_lod_index = data.target_lod_index;
        data.current_time = 0.0f;
    }

    return true;
}

auto model::select_submesh_lod_for_sphere(const mesh& m,
                                          uint32_t submesh_index,
                                          uint32_t base_lod,
                                          const math::bsphere& world_sphere,
                                          const camera& cam) const -> uint32_t
{
    if(lod_override_enabled_)
    {
        return base_lod;
    }

    const auto lod_count = get_lods_count();
    if(lod_count <= base_lod + 1)
    {
        return base_lod;
    }

    // Per-submesh LOD needs the underlying mesh asset to stay the same across LOD switches so
    // submesh_index keeps its meaning. That is only true with a single mesh + internal LODs;
    // multi-mesh manual LODs are a different asset per level with potentially unrelated
    // submesh layouts.
    if(mesh_lods_.size() != 1)
    {
        return base_lod;
    }

    if(lod_screen_sizes_.size() < lod_count)
    {
        return base_lod;
    }

    const float screen_radius_squared =
        compute_bounds_screen_radius_squared(world_sphere.position, world_sphere.radius, cam);

    // Walk from the lowest-quality LOD toward base_lod and pick the coarsest LOD whose
    // screen-size threshold still fits the submesh's projected size. This mirrors the model-
    // wide selection but without hysteresis / transitions (submesh-level ping-pong is bounded
    // by the model LOD floor and is generally imperceptible for small distant submeshes).
    for(std::int32_t lod_index = static_cast<std::int32_t>(lod_count) - 1;
        lod_index >= static_cast<std::int32_t>(base_lod);
        --lod_index)
    {
        const float screen_size = lod_screen_sizes_[static_cast<std::size_t>(lod_index)];
        const float screen_size_squared = math::square(screen_size * 0.5f);
        if(screen_size_squared < screen_radius_squared)
        {
            continue;
        }
        // If the LOD does not carry this submesh index at all (topology diverges), fall back.
        if(submesh_index >= m.get_submeshes(static_cast<uint32_t>(lod_index)).size())
        {
            continue;
        }

        return static_cast<uint32_t>(lod_index);
    }

    return base_lod;
}

auto model::calculate_submesh_lod(const mesh& m,
                                  uint32_t submesh_index,
                                  uint32_t base_lod,
                                  const math::mat4& world_matrix,
                                  const camera& cam) const -> uint32_t
{
    // Cheap early-outs before paying for the world matrix decomposition.
    if(lod_override_enabled_ || mesh_lods_.size() != 1 || get_lods_count() <= base_lod + 1)
    {
        return base_lod;
    }

    const auto& base_submeshes = m.get_submeshes(base_lod);
    if(submesh_index >= base_submeshes.size())
    {
        return base_lod;
    }
    const auto* sm = base_submeshes[submesh_index];
    if(sm == nullptr || !sm->bbox.is_populated())
    {
        // No per-submesh bbox means the submesh cannot be distinguished from the whole model,
        // so per-submesh LOD would just mirror the model-wide selection.
        return base_lod;
    }

    const auto sphere = compute_submesh_world_bounds_sphere(*sm, world_matrix);
    return select_submesh_lod_for_sphere(m, submesh_index, base_lod, sphere, cam);
}

auto model::calculate_submesh_lod_from_world_bounds(const mesh& m,
                                                    uint32_t submesh_index,
                                                    uint32_t base_lod,
                                                    const math::bbox& world_bounds,
                                                    const camera& cam) const -> uint32_t
{
    if(!world_bounds.is_populated())
    {
        return base_lod;
    }

    const math::bsphere sphere{world_bounds.get_center(), glm::length(world_bounds.get_extents())};
    return select_submesh_lod_for_sphere(m, submesh_index, base_lod, sphere, cam);
}

auto model::compute_lod_index(const math::bbox& world_bounds, const camera& cam, float extra_bias) const
    -> uint32_t
{
    const auto lod_count = get_lods_count();
    if(lod_count <= 1)
    {
        return 0;
    }

    if(lod_override_enabled_)
    {
        return math::clamp<uint32_t>(lod_override_level_, 0, lod_count - 1);
    }

    if(lod_screen_sizes_.size() < lod_count)
    {
        return 0;
    }

    if(!world_bounds.is_populated())
    {
        return 0;
    }

    // Enclosing sphere of the pose-aware world AABB (see calculate_lod_data).
    const math::bsphere bsphere{world_bounds.get_center(), glm::length(world_bounds.get_extents())};
    const float screen_radius_squared = compute_bounds_screen_radius_squared(bsphere.position, bsphere.radius, cam);

    std::size_t lod = 0;
    for(std::int32_t lod_index = static_cast<std::int32_t>(lod_count) - 1; lod_index >= 0; --lod_index)
    {
        const auto index = static_cast<size_t>(lod_index);
        const float screen_size = lod_screen_sizes_[index];
        const float screen_size_squared = math::square(screen_size * 0.5f);
        if(screen_size_squared >= screen_radius_squared)
        {
            lod = index;
            break;
        }
    }

    float biased_lod = static_cast<float>(lod) + lod_selection_bias_ + extra_bias;
    biased_lod = math::clamp(biased_lod, 0.0f, static_cast<float>(lod_count - 1));
    return static_cast<uint32_t>(biased_lod);
}


void model::recalulate_lod_screen_size_limits(uint32_t lod_count)
{
    lod_screen_sizes_.clear();
    if(lod_count == 0)
    {
        return;
    }
    lod_screen_sizes_.resize(lod_count);
    for(uint32_t i = 0; i < lod_count; ++i)
    {
        if(i == 0)
        {
            lod_screen_sizes_[i] = 1.0f;
        }
        else if(i == 1)
        {
            lod_screen_sizes_[i] = 0.3f;
        }
        else
        {
            lod_screen_sizes_[i] = lod_screen_sizes_[i - 1] * 0.5f;
        }
    }
}

auto model::get_lod_override_enabled() const -> bool
{
    return lod_override_enabled_;
}

void model::set_lod_override_enabled(bool enabled)
{
    lod_override_enabled_ = enabled;
}

auto model::get_lod_override_level() const -> uint32_t
{
    return lod_override_level_;
}

void model::set_lod_override_level(uint32_t level)
{
    lod_override_level_ = level;
}

auto model::get_lod_selection_bias() const -> float
{
    return lod_selection_bias_;
}

void model::set_lod_selection_bias(float bias)
{
    lod_selection_bias_ = bias;
}

auto model::get_lod_hysteresis() const -> float
{
    return lod_hysteresis_;
}

void model::set_lod_hysteresis(float hysteresis)
{
    lod_hysteresis_ = hysteresis;
}

auto model::get_lod_transition_time() const -> seconds_t
{
    return lod_transition_time_;
}

void model::set_lod_transition_time(seconds_t time)
{
    lod_transition_time_ = time;
}

auto model::get_lod_screen_size_min() const -> float
{
    return 0.01f;
}

void model::set_lod_screen_size_min(float /*value*/)
{
    // Deprecated - hysteresis is now used instead
}

auto model::get_lod_auto_screen_size_power_base() const -> float
{
    return 0.5f;
}

void model::set_lod_auto_screen_size_power_base(float /*value*/)
{
    // Deprecated - fixed thresholds are now used
}

auto model::get_lod_screen_sizes() const -> const std::vector<float>&
{
    return lod_screen_sizes_;
}

void model::set_lod_screen_sizes(const std::vector<float>& sizes)
{
    lod_screen_sizes_ = sizes;
}


void model::submit(const math::mat4& world_transform,
                   const submesh_pose_mat4& submesh_transforms,
                   const pose_mat4& bone_transforms,
                   const std::vector<pose_mat4>& skinning_transforms,
                   unsigned int lod,
                   const submit_callbacks& callbacks,
                   const math::frustum* frustum,
                   const camera* view,
                   const model_submit_extras& extras) const
{
    const auto& lod_mesh = get_lod(lod);
    if(!lod_mesh)
    {
        return;
    }

    auto mesh = lod_mesh.get();

    auto skinned_submeshes_count = mesh->get_skinned_submeshes_count(lod);
    auto non_skinned_submeshes_count = mesh->get_non_skinned_submeshes_count(lod);
    // Per-submesh culling applies to any multi-submesh mesh: cached world-space AABBs make
    // the test cheap. Submeshes without cached bounds are conservatively drawn - local
    // bind-pose bounds are never used for culling.
    const bool cull_submeshes = frustum != nullptr && mesh->get_submeshes_count(lod) > 1;
    const bool per_submesh_lod = cull_submeshes && view != nullptr;

    submit_callbacks::params params;

    // NON SKINNED
    if(non_skinned_submeshes_count > 0)
    {
        params.skinned = false;

        if(callbacks.setup_begin)
        {
            callbacks.setup_begin(params);
        }

        if(callbacks.setup_params_per_instance)
        {
            callbacks.setup_params_per_instance(params);
        }

        auto render_submesh = [this, frustum, cull_submeshes, per_submesh_lod, view, &extras]
                              (const std::shared_ptr<unravel::mesh>& mesh,
                               uint32_t lod,
                               uint32_t group_id,
                               const math::mat4& matrix,
                               const submesh_pose_mat4& pose,
                               submit_callbacks::params& params,
                               const submit_callbacks& callbacks)
        {
            auto group_mat = get_render_material(group_id);
            if(!group_mat)
            {
                return;
            }

            const auto& submeshes = mesh->get_submeshes(lod);
            const auto& indices = mesh->get_non_skinned_submeshes_indices(group_id, lod);

            // Picks the LOD-adjusted submesh pointer/lod pair used for binding. Culling still
            // uses the base-LOD bbox (higher LODs are simplified within the same envelope, so
            // the base bbox is a valid upper bound and this keeps culling stable).
            const auto resolve = [&](uint32_t submesh_index,
                                     const math::mat4& world,
                                     size_t instance) -> std::pair<const unravel::mesh::submesh*, uint32_t>
            {
                const auto* base_sm = submeshes[submesh_index];
                if(!per_submesh_lod)
                {
                    return {base_sm, lod};
                }
                // Prefer the cached world AABB (no matrix decomposition); fall back to the
                // local bbox + world matrix path when no cached data exists.
                const auto* cached_bounds = get_cached_submesh_bounds(extras, submesh_index, instance, false);
                const uint32_t effective_lod =
                    cached_bounds != nullptr
                        ? calculate_submesh_lod_from_world_bounds(*mesh, submesh_index, lod, *cached_bounds, *view)
                        : calculate_submesh_lod(*mesh, submesh_index, lod, world, *view);
                if(effective_lod == lod)
                {
                    return {base_sm, lod};
                }
                const auto& lod_submeshes = mesh->get_submeshes(effective_lod);
                if(submesh_index >= lod_submeshes.size())
                {
                    return {base_sm, lod};
                }
                const auto* lod_sm = lod_submeshes[submesh_index];
                return lod_sm != nullptr ? std::make_pair(lod_sm, effective_lod)
                                         : std::make_pair(base_sm, lod);
            };

            for(const auto& index : indices)
            {
                if(pose.has_transforms(index))
                {
                    const size_t transform_count = pose.get_transform_count(index);

                    for(size_t i = 0; i < transform_count; ++i)
                    {
                        const auto* transform = pose.get_transform(index, i);
                        if(transform)
                        {
                            if(extras.shadow_pass && !pose.get_transform_casts_shadow(index, i))
                            {
                                continue;
                            }

                            if(cull_submeshes && !is_submesh_visible_cached(*frustum, extras, index, i, false))
                            {
                                continue;
                            }

                            const auto [sm, sm_lod] = resolve(static_cast<uint32_t>(index), *transform, i);
                            gfx::set_world_transform(*transform);
                            if(extras.prev_submesh_transforms != nullptr)
                            {
                                const auto* prev_transform =
                                    extras.prev_submesh_transforms->get_transform(static_cast<uint32_t>(index), i);
                                gfx::set_prev_world_transform(prev_transform != nullptr ? *prev_transform
                                                                                        : *transform);
                            }
                            mesh->bind_render_buffers_for_submesh(sm, sm_lod);
                            params.preserve_state = (&index != &indices.back());
                            callbacks.setup_params_per_submesh(params, *group_mat);
                        }
                    }
                }
                else
                {
                    if(cull_submeshes && !is_submesh_visible_cached(*frustum, extras, index, 0, false))
                    {
                        continue;
                    }

                    const auto [sm, sm_lod] = resolve(static_cast<uint32_t>(index), matrix, 0);
                    gfx::set_world_transform(matrix);
                    if(extras.prev_world_transform != nullptr)
                    {
                        gfx::set_prev_world_transform(*extras.prev_world_transform);
                    }
                    mesh->bind_render_buffers_for_submesh(sm, sm_lod);
                    params.preserve_state = &index != &indices.back();
                    callbacks.setup_params_per_submesh(params, *group_mat);
                }
            }
        };

        for(uint32_t i = 0; i < mesh->get_data_groups_count(); ++i)
        {
            render_submesh(mesh, lod, i, world_transform, submesh_transforms, params, callbacks);
        }

        if(callbacks.setup_end)
        {
            callbacks.setup_end(params);
        }
    }

    // SKINNED
    if(skinned_submeshes_count > 0 && !skinning_transforms.empty())
    {
        params.skinned = true;

        if(callbacks.setup_begin)
        {
            callbacks.setup_begin(params);
        }

        if(callbacks.setup_params_per_instance)
        {
            callbacks.setup_params_per_instance(params);
        }

        auto render_submesh_skinned = [this, frustum, view, &extras]
                                      (const std::shared_ptr<unravel::mesh>& mesh,
                                       uint32_t lod,
                                       uint32_t group_id,
                                       const submesh_pose_mat4& pose,
                                       const std::vector<pose_mat4>& skinning_transforms,
                                       submit_callbacks::params& params,
                                       const submit_callbacks& callbacks)
        {
            auto group_mat = get_render_material(group_id);
            if(!group_mat)
            {
                return;
            }

            const auto& indices = mesh->get_skinned_submeshes_indices(group_id, lod);

            for(const auto& index : indices)
            {
                if(index >= skinning_transforms.size())
                {
                    continue;
                }
                const auto& submesh_skinning_transforms = skinning_transforms[index];
                if(submesh_skinning_transforms.transforms.empty())
                {
                    continue;
                }
                const auto choice =
                    resolve_skinned_submesh(*mesh, lod, static_cast<uint32_t>(index), pose, frustum, view, extras);
                if(choice.submesh == nullptr)
                {
                    continue;
                }

                gfx::set_world_transform(submesh_skinning_transforms.transforms);
                if(extras.prev_skinning_transforms != nullptr)
                {
                    const auto& prev_all = *extras.prev_skinning_transforms;
                    const bool has_matching_prev =
                        index < prev_all.size() &&
                        prev_all[index].transforms.size() == submesh_skinning_transforms.transforms.size();
                    gfx::set_prev_world_transform(has_matching_prev ? prev_all[index].transforms
                                                                    : submesh_skinning_transforms.transforms);
                }

                mesh->bind_render_buffers_for_submesh(choice.submesh, choice.lod);
                params.preserve_state = &index != &indices.back();
                callbacks.setup_params_per_submesh(params, *group_mat);
            }
        };

        for(uint32_t i = 0; i < mesh->get_data_groups_count(); ++i)
        {
            render_submesh_skinned(mesh, lod, i, submesh_transforms, skinning_transforms, params, callbacks);
        }

        if(callbacks.setup_end)
        {
            callbacks.setup_end(params);
        }
    }
}

void model::submit_for_vertex_pulling(const math::mat4& world_transform,
                                      const submesh_pose_mat4& submesh_transforms,
                                      const std::vector<pose_mat4>& skinning_transforms,
                                      unsigned int lod,
                                      const submit_vertex_pulling_callbacks& callbacks,
                                      const math::frustum* frustum,
                                      const camera* view,
                                      const model_submit_extras& extras) const
{
    const auto& lod_mesh = get_lod(lod);
    if(!lod_mesh)
    {
        return;
    }

    auto mesh = lod_mesh.get();

    auto vb = mesh->get_hardware_vb();
    auto ib = mesh->get_hardware_ib(lod);
    if(!vb || !ib || !vb->is_valid() || !ib->is_valid())
    {
        return;
    }

    // Vertex/index buffers are exposed to shaders as raw buffers read in 32-bit words,
    // so all byte offsets are converted to float-sized elements. Layouts used by
    // this engine always keep attributes float-aligned, so integer division is safe.
    constexpr uint32_t float_size = static_cast<uint32_t>(sizeof(float));
    const auto& vertex_format = mesh->get_vertex_format();
    const uint32_t stride_bytes = vertex_format.getStride();
    const uint32_t pos_offset_bytes = vertex_format.getOffset(bgfx::Attrib::Position);

    submit_vertex_pulling_callbacks::params params;
    params.vertex_stride_floats = stride_bytes / float_size;
    params.position_offset_floats = pos_offset_bytes / float_size;

    const auto skinned_count = mesh->get_skinned_submeshes_count(lod);
    const auto non_skinned_count = mesh->get_non_skinned_submeshes_count(lod);
    const bool cull_submeshes = frustum != nullptr && mesh->get_submeshes_count(lod) > 1;
    const bool per_submesh_lod = cull_submeshes && view != nullptr;
    const auto& submeshes = mesh->get_submeshes(lod);
    const uint32_t group_count = static_cast<uint32_t>(mesh->get_data_groups_count());

    // Binds the raw geometry buffers for @p effective_lod's index buffer and reads face
    // range from that LOD's submesh entry. When @p effective_lod matches the model-wide
    // @p lod, the pre-fetched @c ib is reused; otherwise the LOD-specific IB is looked up.
    // The callback is responsible for uniforms, state, vertex count and the actual submit
    // call - the model only guarantees that u_world and the raw buffers on stages 0/1
    // are set.
    auto bind_and_submit = [&](uint32_t submesh_index, uint32_t effective_lod) -> void
    {
        const auto& lod_submeshes = (effective_lod == lod) ? submeshes : mesh->get_submeshes(effective_lod);
        if(submesh_index >= lod_submeshes.size())
        {
            return;
        }
        const auto* sub = lod_submeshes[submesh_index];
        if(!sub || sub->face_count == 0)
        {
            return;
        }

        const auto effective_ib = (effective_lod == lod) ? ib : mesh->get_hardware_ib(effective_lod);
        if(!effective_ib || !effective_ib->is_valid())
        {
            return;
        }

        params.submesh_index = submesh_index;
        params.index_start = static_cast<uint32_t>(sub->face_start) * 3u;
        params.index_count = sub->face_count * 3u;

        bgfx::setBuffer(0, vb->native_handle(), bgfx::Access::Read);
        bgfx::setBuffer(1, effective_ib->native_handle(), bgfx::Access::Read);

        if(callbacks.setup_params_per_submesh)
        {
            callbacks.setup_params_per_submesh(params);
        }
    };

    // ----------------- NON-SKINNED PASS -----------------
    if(non_skinned_count > 0)
    {
        params.skinned = false;
        params.weight_offset_floats = 0;
        params.indices_offset_floats = 0;

        if(callbacks.setup_begin)
        {
            callbacks.setup_begin(params);
        }
        if(callbacks.setup_params_per_instance)
        {
            callbacks.setup_params_per_instance(params);
        }

        for(uint32_t group_id = 0; group_id < group_count; ++group_id)
        {
            const auto& indices = mesh->get_non_skinned_submeshes_indices(group_id, lod);
            for(const auto& index : indices)
            {
                const auto* sub = submeshes[index];
                if(!sub || sub->face_count == 0)
                {
                    continue;
                }

                params.preserve_state = &index != &indices.back();

                if(submesh_transforms.has_transforms(index))
                {
                    const size_t transform_count = submesh_transforms.get_transform_count(index);
                    for(size_t j = 0; j < transform_count; ++j)
                    {
                        const auto* transform = submesh_transforms.get_transform(index, j);
                        if(!transform)
                        {
                            continue;
                        }
                        if(extras.shadow_pass && !submesh_transforms.get_transform_casts_shadow(index, j))
                        {
                            continue;
                        }
                        if(cull_submeshes && !is_submesh_visible_cached(*frustum, extras, index, j, false))
                        {
                            continue;
                        }
                        uint32_t effective_lod = lod;
                        if(per_submesh_lod)
                        {
                            const auto* cached_bounds =
                                get_cached_submesh_bounds(extras, static_cast<uint32_t>(index), j, false);
                            effective_lod =
                                cached_bounds != nullptr
                                    ? calculate_submesh_lod_from_world_bounds(*mesh,
                                                                              static_cast<uint32_t>(index),
                                                                              lod,
                                                                              *cached_bounds,
                                                                              *view)
                                    : calculate_submesh_lod(*mesh, static_cast<uint32_t>(index), lod, *transform, *view);
                        }
                        gfx::set_world_transform(*transform);
                        bind_and_submit(static_cast<uint32_t>(index), effective_lod);
                    }
                }
                else
                {
                    if(cull_submeshes && !is_submesh_visible_cached(*frustum, extras, index, 0, false))
                    {
                        continue;
                    }
                    const uint32_t effective_lod = per_submesh_lod
                        ? calculate_submesh_lod(*mesh, static_cast<uint32_t>(index), lod, world_transform, *view)
                        : lod;
                    gfx::set_world_transform(world_transform);
                    bind_and_submit(static_cast<uint32_t>(index), effective_lod);
                }
            }
        }

        if(callbacks.setup_end)
        {
            callbacks.setup_end(params);
        }
    }

    // ------------------- SKINNED PASS -------------------
    // Skinned rendering additionally needs the bone weight/indices attribute
    // offsets so the shader can blend u_world[bone_i] per vertex.
    if(skinned_count > 0 && !skinning_transforms.empty()
       && vertex_format.has(bgfx::Attrib::Weight) && vertex_format.has(bgfx::Attrib::Indices))
    {
        params.skinned = true;
        params.weight_offset_floats = vertex_format.getOffset(bgfx::Attrib::Weight) / float_size;
        params.indices_offset_floats = vertex_format.getOffset(bgfx::Attrib::Indices) / float_size;

        if(callbacks.setup_begin)
        {
            callbacks.setup_begin(params);
        }
        if(callbacks.setup_params_per_instance)
        {
            callbacks.setup_params_per_instance(params);
        }

        for(uint32_t group_id = 0; group_id < group_count; ++group_id)
        {
            const auto& indices = mesh->get_skinned_submeshes_indices(group_id, lod);
            for(const auto& index : indices)
            {
                if(index >= skinning_transforms.size())
                {
                    continue;
                }
                const auto& bones = skinning_transforms[index];
                if(bones.transforms.empty())
                {
                    continue;
                }
                const auto* sub = submeshes[index];
                if(!sub || sub->face_count == 0)
                {
                    continue;
                }

                if(submesh_transforms.has_transforms(index))
                {
                    if(!submesh_transforms.get_transform_active(index, 0))
                    {
                        continue;
                    }
                    if(extras.shadow_pass && !submesh_transforms.get_transform_casts_shadow(index, 0))
                    {
                        continue;
                    }
                }

                if(cull_submeshes && frustum != nullptr)
                {
                    const auto* bounds = get_cached_submesh_bounds(extras, static_cast<uint32_t>(index), 0, true);
                    if(bounds != nullptr && frustum->classify_aabb(*bounds) == math::volume_query::outside)
                    {
                        continue;
                    }
                }

                params.preserve_state = &index != &indices.back();

                gfx::set_world_transform(bones.transforms);
                bind_and_submit(static_cast<uint32_t>(index), lod);
            }
        }

        if(callbacks.setup_end)
        {
            callbacks.setup_end(params);
        }
    }
}


void model::resize_materials(const asset_handle<mesh>& mesh)
{
    const auto m = mesh.get();
    auto submeshes = m->get_data_groups_count();
    if(materials_.size() != submeshes)
    {
        materials_.resize(submeshes, default_material());
    }
    sync_material_instances();
}

auto model::default_material() -> asset_handle<material>&
{
    static asset_handle<material> asset;
    return asset;
}

auto model::fallback_material() -> asset_handle<material>&
{
    static asset_handle<material> asset;
    return asset;
}

void model::submit_for_batching(batch_collector& collector,
                                const math::mat4& world_transform,
                                const submesh_pose_mat4& submesh_transforms,
                                uint32_t lod_index,
                                float lod_param,
                                const math::frustum* frustum,
                                const camera* view,
                                const model_submit_extras& extras) const
{
    const auto& mesh_asset = get_lod(lod_index);
    if(!mesh_asset)
    {
        return;
    }

    auto mesh = mesh_asset.get();
    if(!mesh)
    {
        return;
    }

    const bool cull_submeshes = frustum != nullptr && mesh->get_submeshes_count(lod_index) > 1;
    // The batch key already includes lod_index, so mixed per-submesh LODs land in distinct
    // batches automatically - the renderer already looks up (mesh, lod, submesh) per batch.
    const bool per_submesh_lod = cull_submeshes && view != nullptr;

    // Iterate over data groups (material groups)
    const auto data_group_count = mesh->get_data_groups_count();

    for (uint32_t data_group_id = 0; data_group_id < data_group_count; ++data_group_id)
    {
        // A material instance that overrides only per-instance values keys on its shared material and
        // carries its own values per instance.
        const auto [group_material, material_override] = get_slot_batch_material(data_group_id);
        if(!group_material)
        {
            continue; // Skip data groups without a valid material
        }

        // Get all non-skinned submeshes for this data group
        const auto& submesh_indices = mesh->get_non_skinned_submeshes_indices(data_group_id, lod_index);
        
        // Collect each submesh in this data group as a separate batch entry
        for (size_t submesh_idx : submesh_indices)
        {
            uint32_t submesh_index = static_cast<uint32_t>(submesh_idx);

            // Check if this submesh has specific transforms
            if (submesh_transforms.has_transforms(submesh_index))
            {
                // This submesh has one or more node transforms - create an instance for each
                const size_t transform_count = submesh_transforms.get_transform_count(submesh_index);
                
                for (size_t instance_idx = 0; instance_idx < transform_count; ++instance_idx)
                {
                    const math::mat4* transform_ptr = submesh_transforms.get_transform(submesh_index, instance_idx);
                    if (!transform_ptr)
                    {
                        continue;
                    }

                    if(extras.shadow_pass
                       && !submesh_transforms.get_transform_casts_shadow(submesh_index, instance_idx))
                    {
                        continue;
                    }

                    if(cull_submeshes && !is_submesh_visible_cached(*frustum, extras, submesh_index, instance_idx, false))
                    {
                        continue;
                    }

                    uint32_t effective_lod = lod_index;
                    if(per_submesh_lod)
                    {
                        const auto* cached_bounds =
                            get_cached_submesh_bounds(extras, submesh_index, instance_idx, false);
                        effective_lod =
                            cached_bounds != nullptr
                                ? calculate_submesh_lod_from_world_bounds(*mesh,
                                                                          submesh_index,
                                                                          lod_index,
                                                                          *cached_bounds,
                                                                          *view)
                                : calculate_submesh_lod(*mesh, submesh_index, lod_index, *transform_ptr, *view);
                    }

                    batch_key key(mesh, group_material, effective_lod, submesh_index);
                    if (!key.is_valid())
                    {
                        continue;
                    }

                    // Create batch instance with the specific transform
                    batch_instance instance(transform_ptr);
                    instance.lod_params.x = lod_param;
                    instance.material_override = material_override;
                    if(extras.prev_submesh_transforms != nullptr)
                    {
                        const auto* prev_ptr =
                            extras.prev_submesh_transforms->get_transform(submesh_index, instance_idx);
                        instance.prev_world_transform_ptr = prev_ptr != nullptr ? prev_ptr : transform_ptr;
                    }

                    // Collect for batching
                    collector.collect_renderable(key, instance);
                }
            }
            else
            {
                if(cull_submeshes && !is_submesh_visible_cached(*frustum, extras, submesh_index, 0, false))
                {
                    continue;
                }

                const uint32_t effective_lod = per_submesh_lod
                    ? calculate_submesh_lod(*mesh, submesh_index, lod_index, world_transform, *view)
                    : lod_index;

                batch_key key(mesh, group_material, effective_lod, submesh_index);
                if (!key.is_valid())
                {
                    continue;
                }

                // Create batch instance with world transform
                batch_instance instance(&world_transform);
                instance.lod_params.x = lod_param;
                instance.material_override = material_override;
                if(extras.prev_world_transform != nullptr)
                {
                    instance.prev_world_transform_ptr = extras.prev_world_transform;
                }

                // Collect for batching
                collector.collect_renderable(key, instance);
            }
        }
    }
}

auto shadow_reach::can_reach_view(const math::bbox& world_bounds) const -> bool
{
    if(view_frustum == nullptr)
    {
        return true;
    }
    // The sweep already covers the caster's extent; the radius is the margin the model-level
    // test has always carried, kept so a caster just outside the range still counts.
    const float sweep_distance = max_distance + math::length(world_bounds.get_extents());
    return view_frustum->test_swept_aabb(world_bounds, light_direction, sweep_distance);
}

auto compute_shadow_view_mask(const math::frustum* frustums,
                              uint8_t view_count,
                              const math::bbox& world_bounds,
                              bool nested_cascades,
                              const shadow_reach& reach,
                              uint8_t candidate_mask) -> uint8_t
{
    if(!reach.can_reach_view(world_bounds))
    {
        return 0u;
    }

    uint8_t view_mask = 0u;
    for(uint8_t ii = 0; ii < view_count; ++ii)
    {
        const uint8_t view_bit = uint8_t(1u << ii);
        if((candidate_mask & view_bit) == 0u)
        {
            continue;
        }
        const auto query = frustums[ii].classify_aabb(world_bounds);
        if(query == math::volume_query::outside)
        {
            continue;
        }
        view_mask |= view_bit;
        // Fully inside this cascade: the farther (larger) ones never sample this caster.
        if(nested_cascades && query == math::volume_query::inside)
        {
            break;
        }
    }
    return view_mask;
}

auto model::submit_for_shadow_batching_cascaded(std::vector<shadow_batch_collector>& collectors,
                                                uint8_t cascade_count,
                                                uint8_t cascade_mask,
                                                const shadow_reach& reach,
                                                const math::mat4& world_transform,
                                                const submesh_pose_mat4& submesh_transforms,
                                                uint32_t lod_index,
                                                float lod_param,
                                                const math::frustum* frustums,
                                                bool nested_cascades,
                                                const model_submit_extras& extras) const -> bool
{
    const auto& mesh_asset = get_lod(lod_index);
    if(!mesh_asset)
    {
        return false;
    }
    auto mesh = mesh_asset.get();
    if(!mesh)
    {
        return false;
    }

    bool collected_any = false;
    auto collect_into_cascades =
        [&](const shadow_batch_key& key, uint32_t submesh_index, const math::mat4& transform, size_t instance_idx) -> void
    {
        // Submesh granularity of the nested-cascade rule: a submesh fully inside a nearer
        // cascade skips the farther ones even while its model straddles them. No cached
        // bounds (stale proxies) - the submesh goes wherever the model can reach.
        const auto* bounds = get_cached_submesh_bounds(extras, submesh_index, instance_idx, false);
        const uint8_t submesh_mask =
            bounds != nullptr
                ? compute_shadow_view_mask(frustums, cascade_count, *bounds, nested_cascades, reach, cascade_mask)
                : cascade_mask;
        for(uint8_t ii = 0; ii < cascade_count; ++ii)
        {
            if((submesh_mask & (1u << ii)) == 0u)
            {
                continue;
            }

            batch_instance instance(&transform);
            instance.lod_params.x = lod_param;
            collectors[ii].collect_renderable(key, instance);
            collected_any = true;
        }
    };

    const auto data_group_count = mesh->get_data_groups_count();
    for(uint32_t data_group_id = 0; data_group_id < data_group_count; ++data_group_id)
    {
        auto group_material = get_render_material(data_group_id);
        if(!group_material)
        {
            continue;
        }

        const auto& submesh_indices = mesh->get_non_skinned_submeshes_indices(data_group_id, lod_index);
        for(size_t submesh_idx : submesh_indices)
        {
            const uint32_t submesh_index = static_cast<uint32_t>(submesh_idx);

            shadow_batch_key key = make_shadow_batch_key(mesh, lod_index, submesh_index, group_material);
            if(!key.is_valid())
            {
                continue;
            }

            if(submesh_transforms.has_transforms(submesh_index))
            {
                const size_t transform_count = submesh_transforms.get_transform_count(submesh_index);
                for(size_t instance_idx = 0; instance_idx < transform_count; ++instance_idx)
                {
                    const math::mat4* transform_ptr = submesh_transforms.get_transform(submesh_index, instance_idx);
                    if(!transform_ptr)
                    {
                        continue;
                    }
                    if(!submesh_transforms.get_transform_casts_shadow(submesh_index, instance_idx))
                    {
                        continue;
                    }
                    collect_into_cascades(key, submesh_index, *transform_ptr, instance_idx);
                }
            }
            else
            {
                collect_into_cascades(key, submesh_index, world_transform, 0);
            }
        }
    }

    return collected_any;
}

auto model::get_empty_skinning() -> const std::vector<pose_mat4>&
{
    static const std::vector<pose_mat4> empty;
    return empty;
}

auto model::get_changes_from(const model& previous) const -> render_change::mask
{
    render_change::mask changes = render_change::none;
    const bool is_same_geometry = mesh_lods_ == previous.mesh_lods_ &&
                                  lod_override_enabled_ == previous.lod_override_enabled_ &&
                                  lod_override_level_ == previous.lod_override_level_ &&
                                  lod_selection_bias_ == previous.lod_selection_bias_ &&
                                  lod_hysteresis_ == previous.lod_hysteresis_ &&
                                  lod_transition_time_ == previous.lod_transition_time_ &&
                                  lod_screen_sizes_ == previous.lod_screen_sizes_;
    if(!is_same_geometry)
    {
        changes |= render_change::geometry;
    }
    const size_t slot_count = std::max({materials_.size(),
                                        previous.materials_.size(),
                                        material_instances_.size(),
                                        previous.material_instances_.size()});
    for(uint32_t slot = 0; slot < slot_count; ++slot)
    {
        if(!(get_shared_material(slot) == previous.get_shared_material(slot)))
        {
            changes |= render_change::material_aspects;
            continue;
        }
        const bool has_instance = slot < material_instances_.size();
        const bool had_instance = slot < previous.material_instances_.size();
        if(has_instance && had_instance)
        {
            changes |= material_instances_[slot].get_changes_from(previous.material_instances_[slot]);
        }
        else if((has_instance && material_instances_[slot].has_overrides()) ||
                (had_instance && previous.material_instances_[slot].has_overrides()))
        {
            changes |= render_change::material_aspects;
        }
    }
    return changes;
}

auto model::get_materials_change_keys() const -> render_change_values
{
    render_change_values keys{};
    const auto add = [&keys](const material& mat)
    {
        mat.add_change_keys(keys);
    };
    const auto& shared_materials = get_shared_materials();
    const size_t slot_count = std::max(shared_materials.size(), material_instances_.size());
    for(size_t slot = 0; slot < slot_count; ++slot)
    {
        // The slot's render material (get_render_material), without waiting for a load.
        const auto shared = slot < shared_materials.size() ? shared_materials[slot].try_get() : nullptr;
        if(shared)
        {
            // A reload is a new object whose loaded values counted no edits. Counted whichever material the slot
            // draws, so a slot that starts or stops drawing its resolved instance keeps this part of the key.
            add_identity_key(keys, render_change::material_aspects, reinterpret_cast<std::uintptr_t>(shared.get()));
        }
        if(slot < material_instances_.size() && material_instances_[slot].has_overrides() &&
           material_instances_[slot].get_resolved())
        {
            add(*material_instances_[slot].get_resolved());
        }
        else if(shared)
        {
            add(*shared);
        }
    }
    return keys;
}

auto model::resolve_skinned_submesh(const mesh& m,
                                    uint32_t lod,
                                    uint32_t submesh_index,
                                    const submesh_pose_mat4& pose,
                                    const math::frustum* frustum,
                                    const camera* view,
                                    const model_submit_extras& extras) const -> skinned_submesh_choice
{
    // Per-submesh enable/shadow flags authored on the owning node entity apply to skinned submeshes too (the node
    // transform itself is unused for skinning).
    if(pose.has_transforms(submesh_index))
    {
        if(!pose.get_transform_active(submesh_index, 0))
        {
            return {};
        }
        if(extras.shadow_pass && !pose.get_transform_casts_shadow(submesh_index, 0))
        {
            return {};
        }
    }
    const auto& submeshes = m.get_submeshes(lod);
    if(submesh_index >= submeshes.size())
    {
        return {};
    }
    // Skinned submeshes cull identically to static ones using the retained animated world bounds (union of
    // bone-transformed bind-space bounds).
    const bool cull_submeshes = frustum != nullptr && m.get_submeshes_count(lod) > 1;
    const auto* bounds = get_cached_submesh_bounds(extras, submesh_index, 0, true);
    if(cull_submeshes && bounds != nullptr && frustum->classify_aabb(*bounds) == math::volume_query::outside)
    {
        return {};
    }
    skinned_submesh_choice choice{submeshes[submesh_index], lod};
    // Per-submesh LOD from the animated world bounds; submesh indices are stable across internal LODs, so the same
    // palette still applies to the simplified index range.
    if(cull_submeshes && view != nullptr && bounds != nullptr)
    {
        const uint32_t effective_lod = calculate_submesh_lod_from_world_bounds(m, submesh_index, lod, *bounds, *view);
        const auto& lod_submeshes = m.get_submeshes(effective_lod);
        if(effective_lod != lod && submesh_index < lod_submeshes.size() && lod_submeshes[submesh_index] != nullptr)
        {
            choice = {lod_submeshes[submesh_index], effective_lod};
        }
    }
    return choice;
}

auto model::submit_for_skinned_batching(batch_collector& collector,
                                        const math::mat4& world_transform,
                                        const submesh_pose_mat4& submesh_transforms,
                                        hpp::span<const uint32_t> palette_rows,
                                        hpp::span<const uint32_t> prev_palette_rows,
                                        uint32_t lod_index,
                                        float lod_param,
                                        const math::frustum* frustum,
                                        const camera* view,
                                        const model_submit_extras& extras) const -> bool
{
    const auto& mesh_asset = get_lod(lod_index);
    auto mesh = mesh_asset ? mesh_asset.get() : nullptr;
    if(!mesh)
    {
        return false;
    }
    bool collected_any = false;
    const auto data_group_count = mesh->get_data_groups_count();
    for(uint32_t data_group_id = 0; data_group_id < data_group_count; ++data_group_id)
    {
        // A material instance that overrides only per-instance values keys on its shared material and carries its own
        // values per instance, as in submit_for_batching.
        const auto [group_material, material_override] = get_slot_batch_material(data_group_id);
        if(!group_material)
        {
            continue;
        }
        for(size_t submesh_idx : mesh->get_skinned_submeshes_indices(data_group_id, lod_index))
        {
            const auto submesh_index = static_cast<uint32_t>(submesh_idx);
            if(submesh_index >= palette_rows.size() || palette_rows[submesh_index] == skinning_palette_rows::INVALID_ROW)
            {
                continue;
            }
            const auto choice =
                resolve_skinned_submesh(*mesh, lod_index, submesh_index, submesh_transforms, frustum, view, extras);
            if(choice.submesh == nullptr)
            {
                continue;
            }
            batch_instance instance(&world_transform);
            instance.lod_params.x = lod_param;
            instance.material_override = material_override;
            instance.palette_row = palette_rows[submesh_index];
            instance.prev_palette_row =
                submesh_index < prev_palette_rows.size() ? prev_palette_rows[submesh_index] : instance.palette_row;
            collector.collect_renderable(batch_key(mesh, group_material, choice.lod, submesh_index), instance);
            collected_any = true;
        }
    }
    return collected_any;
}

auto model::submit_for_skinned_shadow_batching_cascaded(std::vector<shadow_batch_collector>& collectors,
                                                        uint8_t cascade_count,
                                                        uint8_t cascade_mask,
                                                        const shadow_reach& reach,
                                                        const math::mat4& world_transform,
                                                        const submesh_pose_mat4& submesh_transforms,
                                                        hpp::span<const uint32_t> palette_rows,
                                                        uint32_t lod_index,
                                                        const math::frustum* frustums,
                                                        bool nested_cascades,
                                                        const model_submit_extras& extras) const -> bool
{
    const auto& mesh_asset = get_lod(lod_index);
    auto mesh = mesh_asset ? mesh_asset.get() : nullptr;
    if(!mesh)
    {
        return false;
    }
    bool collected_any = false;
    const auto data_group_count = mesh->get_data_groups_count();
    for(uint32_t data_group_id = 0; data_group_id < data_group_count; ++data_group_id)
    {
        auto group_material = get_render_material(data_group_id);
        if(!group_material)
        {
            continue;
        }
        for(size_t submesh_idx : mesh->get_skinned_submeshes_indices(data_group_id, lod_index))
        {
            const auto submesh_index = static_cast<uint32_t>(submesh_idx);
            if(submesh_index >= palette_rows.size() || palette_rows[submesh_index] == skinning_palette_rows::INVALID_ROW)
            {
                continue;
            }
            // The cascades do the culling here: no frustum, no per-submesh LOD (the caster's LOD follows the camera).
            const auto choice =
                resolve_skinned_submesh(*mesh, lod_index, submesh_index, submesh_transforms, nullptr, nullptr, extras);
            if(choice.submesh == nullptr)
            {
                continue;
            }
            const shadow_batch_key key = make_shadow_batch_key(mesh, choice.lod, submesh_index, group_material);
            if(!key.is_valid())
            {
                continue;
            }
            const auto* bounds = get_cached_submesh_bounds(extras, submesh_index, 0, true);
            const uint8_t submesh_mask =
                bounds != nullptr
                    ? compute_shadow_view_mask(frustums, cascade_count, *bounds, nested_cascades, reach, cascade_mask)
                    : cascade_mask;
            batch_instance instance(&world_transform);
            instance.palette_row = palette_rows[submesh_index];
            instance.prev_palette_row = instance.palette_row;
            for(uint8_t ii = 0; ii < cascade_count; ++ii)
            {
                if((submesh_mask & (1u << ii)) != 0u)
                {
                    collectors[ii].collect_renderable(key, instance);
                    collected_any = true;
                }
            }
        }
    }
    return collected_any;
}

} // namespace unravel
