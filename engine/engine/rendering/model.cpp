#include "model.h"
#include "gpu_program.h"
#include "graphics/graphics.h"
#include "graphics/index_buffer.h"
#include "graphics/vertex_buffer.h"
#include "material.h"
#include "mesh.h"
#include "camera.h"
#include "batch_collector.h"

#include <algorithm>
#include <cmath>

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

/**
 * Resolves the material used for a specific submesh: per-submesh override first (when
 * provided via extras), then the model material for the submesh's data group.
 */
auto resolve_submesh_material(const model_submit_extras& extras,
                              uint32_t submesh_index,
                              const material::sptr& group_material) -> const material::sptr&
{
    if(extras.material_overrides != nullptr && submesh_index < extras.material_overrides->size())
    {
        const auto& override_material = (*extras.material_overrides)[submesh_index];
        if(override_material)
        {
            return override_material;
        }
    }
    return group_material;
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

auto model::get_lod(uint32_t lod) const -> asset_handle<mesh>
{
    if(mesh_lods_.empty())
    {
        return {};
    }

    lod = math::clamp<std::size_t>(lod, 0, mesh_lods_.size() - 1);

    for(int i = int(lod); i >= 0; --i)
    {
        auto lod_mesh = mesh_lods_[i];
        if(lod_mesh)
        {
            return lod_mesh;
        }
    }
    
    return {};
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

void model::set_material(asset_handle<material> material, uint32_t index)
{
    if(index >= materials_.size())
    {
        materials_.resize(index + 1);
    }

    materials_[index] = std::move(material);
}

void model::set_material_instance(material::sptr material, uint32_t index)
{
    if(index >= material_instances_.size())
    {
        material_instances_.resize(index + 1, nullptr);
    }

    material_instances_[index] = std::move(material);
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
        if(mesh_asset && mesh_asset.get())
        {
            return mesh_asset.get()->get_lod_count();
        }
    }
    // Otherwise, return the number of separate mesh LODs (manual LODs)
    return static_cast<uint32_t>(mesh_lods_.size());
}

void model::set_lods(const std::vector<asset_handle<mesh>>& lods)
{
    mesh_lods_ = lods;

    recalulate_lod_screen_size_limits(get_lods_count());

    if(!mesh_lods_.empty())
    {
        auto& mesh = mesh_lods_[0];
        resize_materials(mesh);
    }
}

auto model::get_materials() const -> const std::vector<asset_handle<material>>&
{
    return materials_;
}

auto model::get_material_instances() const -> const std::vector<material::sptr>&
{
    return material_instances_;
}


void model::set_materials(const std::vector<asset_handle<material>>& materials)
{
    materials_ = materials;
}

void model::set_material_instances(const std::vector<material::sptr>& materials)
{
    material_instances_ = materials;
}

auto model::get_material(uint32_t index) const -> asset_handle<material>
{
    if(materials_.size() <= index)
    {
        return {};
    }

    return materials_[index];
}

auto model::get_material_instance(uint32_t index) const -> material::sptr
{
    if(index < material_instances_.size())
    {
        auto instance =  material_instances_[index];
        if(instance)
        {
            return instance;
        }
    }

    auto instance = get_material(index);
    if(instance.is_valid())
    {
        return instance.get();
    }

    return nullptr;
}

auto model::get_or_emplace_material_instance(uint32_t index) -> material::sptr
{
    if(index >= material_instances_.size())
    {
        auto asset_instance = get_material_instance(index);

        material_instances_.resize(index + 1, nullptr);
        material_instances_[index] = asset_instance->clone();
    }

    auto& instance = material_instances_[index];

    if(!instance)
    {
        auto asset_instance = get_material_instance(index);

        // if we already have an asset for that slot, promote it to instance
        if(asset_instance)
        {
            instance = asset_instance->clone();
        }
        else
        {
            // create a new one
            instance = std::make_shared<pbr_material>();
        }
    }

    return instance;
}


auto model::calculate_lod_data(lod_data& data, const math::bbox& world_bounds, const camera& cam, float dt) const -> bool
{
    data.transition_time = get_lod_transition_time().count();
    const auto lod_count = get_lods_count();
    const auto base_mesh = get_lod(0);
    if(!base_mesh)
    {
        return false;
    }

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
        lod = math::clamp<std::size_t>(lod_override_level_, 0, lod_count - 1);
    }
    else if(lod_count > 1 && lod_screen_sizes_.size() >= lod_count)
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

    float biased_lod = static_cast<float>(lod) + lod_selection_bias_;
    biased_lod = math::clamp(biased_lod, 0.0f, static_cast<float>(lod_count - 1));
    lod = static_cast<std::size_t>(biased_lod);

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
    const auto lod_mesh = get_lod(lod);
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
            auto group_mat = get_material_instance(group_id);

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
                const auto& mat = resolve_submesh_material(extras, static_cast<uint32_t>(index), group_mat);
                if(!mat)
                {
                    continue;
                }

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
                            callbacks.setup_params_per_submesh(params, *mat);
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
                    callbacks.setup_params_per_submesh(params, *mat);
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

        auto render_submesh_skinned = [this, frustum, cull_submeshes, per_submesh_lod, view, &extras]
                                      (const std::shared_ptr<unravel::mesh>& mesh,
                                       uint32_t lod,
                                       uint32_t group_id,
                                       const submesh_pose_mat4& pose,
                                       const std::vector<pose_mat4>& skinning_transforms,
                                       submit_callbacks::params& params,
                                       const submit_callbacks& callbacks)
        {
            auto group_mat = get_material_instance(group_id);

            const auto& submeshes = mesh->get_submeshes(lod);
            const auto& indices = mesh->get_skinned_submeshes_indices(group_id, lod);

            for(const auto& index : indices)
            {
                if(index >= skinning_transforms.size())
                {
                    continue;
                }

                const auto& mat = resolve_submesh_material(extras, static_cast<uint32_t>(index), group_mat);
                if(!mat)
                {
                    continue;
                }

                // Per-submesh enable/shadow flags authored on the owning node entity apply to
                // skinned submeshes too (the node transform itself is unused for skinning).
                if(pose.has_transforms(index))
                {
                    if(!pose.get_transform_active(index, 0))
                    {
                        continue;
                    }
                    if(extras.shadow_pass && !pose.get_transform_casts_shadow(index, 0))
                    {
                        continue;
                    }
                }

                // Skinned submeshes cull identically to static ones using the retained
                // animated world bounds (union of bone-transformed bind-space bounds).
                if(cull_submeshes && frustum != nullptr)
                {
                    const auto* bounds = get_cached_submesh_bounds(extras, static_cast<uint32_t>(index), 0, true);
                    if(bounds != nullptr && frustum->classify_aabb(*bounds) == math::volume_query::outside)
                    {
                        continue;
                    }
                }

                const auto& submesh_skinning_transforms = skinning_transforms[index];

                if(!submesh_skinning_transforms.transforms.empty())
                {
                    // Per-submesh LOD for skinned submeshes uses the animated world bounds;
                    // submesh indices are stable across internal LODs so the same palette
                    // still applies to the simplified index range.
                    const auto* base_sm = submeshes[index];
                    const auto* sm = base_sm;
                    uint32_t sm_lod = lod;
                    if(per_submesh_lod)
                    {
                        const auto* bounds = get_cached_submesh_bounds(extras, static_cast<uint32_t>(index), 0, true);
                        if(bounds != nullptr)
                        {
                            const uint32_t effective_lod = calculate_submesh_lod_from_world_bounds(
                                *mesh, static_cast<uint32_t>(index), lod, *bounds, *view);
                            if(effective_lod != lod)
                            {
                                const auto& lod_submeshes = mesh->get_submeshes(effective_lod);
                                if(index < lod_submeshes.size() && lod_submeshes[index] != nullptr)
                                {
                                    sm = lod_submeshes[index];
                                    sm_lod = effective_lod;
                                }
                            }
                        }
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

                    mesh->bind_render_buffers_for_submesh(sm, sm_lod);
                    params.preserve_state = &index != &indices.back();
                    callbacks.setup_params_per_submesh(params, *mat);
                }
                
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
    const auto lod_mesh = get_lod(lod);
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

    // Vertex/index buffers are exposed to shaders as Buffer<float> / Buffer<uint>
    // so all byte offsets are converted to float-sized elements. Layouts used by
    // this engine always keep attributes float-aligned, so integer division is safe.
    constexpr uint32_t float_size = static_cast<uint32_t>(sizeof(float));
    const auto& vertex_format = mesh->get_vertex_format();
    const uint32_t stride_bytes = vertex_format.getStride();
    const uint32_t pos_offset_bytes = vertex_format.getOffset(gfx::attribute::Position);

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

        gfx::set_buffer(0, vb->native_handle(), gfx::access::Read);
        gfx::set_buffer(1, effective_ib->native_handle(), gfx::access::Read);

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
       && vertex_format.has(gfx::attribute::Weight) && vertex_format.has(gfx::attribute::Indices))
    {
        params.skinned = true;
        params.weight_offset_floats = vertex_format.getOffset(gfx::attribute::Weight) / float_size;
        params.indices_offset_floats = vertex_format.getOffset(gfx::attribute::Indices) / float_size;

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
    auto mesh_asset = get_lod(lod_index);
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
        // Get material for this data group
        auto group_material = get_material_instance(data_group_id);

        // Get all non-skinned submeshes for this data group
        const auto& submesh_indices = mesh->get_non_skinned_submeshes_indices(data_group_id, lod_index);
        
        // Collect each submesh in this data group as a separate batch entry
        for (size_t submesh_idx : submesh_indices)
        {
            uint32_t submesh_index = static_cast<uint32_t>(submesh_idx);

            // Per-submesh material overrides participate in the batch key, so overridden
            // instances automatically batch separately from the model-material ones.
            const auto& material_ptr = resolve_submesh_material(extras, submesh_index, group_material);
            if(!material_ptr)
            {
                continue; // Skip submeshes without valid materials
            }

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

                    batch_key key(mesh, material_ptr, effective_lod, submesh_index);
                    if (!key.is_valid())
                    {
                        continue;
                    }

                    // Create batch instance with the specific transform
                    batch_instance instance(transform_ptr);
                    instance.lod_params.x = lod_param;
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

                batch_key key(mesh, material_ptr, effective_lod, submesh_index);
                if (!key.is_valid())
                {
                    continue;
                }

                // Create batch instance with world transform
                batch_instance instance(&world_transform);
                instance.lod_params.x = lod_param;
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

auto model::submit_for_shadow_batching_cascaded(std::vector<shadow_batch_collector>& collectors,
                                                uint8_t cascade_count,
                                                const math::mat4& world_transform,
                                                const submesh_pose_mat4& submesh_transforms,
                                                uint32_t lod_index,
                                                float lod_param,
                                                const math::frustum* frustums,
                                                bool nested_cascades,
                                                const model_submit_extras& extras) const -> bool
{
    auto mesh_asset = get_lod(lod_index);
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
        for(uint8_t ii = 0; ii < cascade_count; ++ii)
        {
            const auto query = classify_submesh_cached(frustums[ii], extras, submesh_index, instance_idx, false);
            if(query == math::volume_query::outside)
            {
                continue;
            }

            batch_instance instance(&transform);
            instance.lod_params.x = lod_param;
            collectors[ii].collect_renderable(key, instance);
            collected_any = true;

            if(nested_cascades && query == math::volume_query::inside)
            {
                break;
            }
        }
    };

    const auto data_group_count = mesh->get_data_groups_count();
    for(uint32_t data_group_id = 0; data_group_id < data_group_count; ++data_group_id)
    {
        auto group_material = get_material_instance(data_group_id);

        const auto& submesh_indices = mesh->get_non_skinned_submeshes_indices(data_group_id, lod_index);
        for(size_t submesh_idx : submesh_indices)
        {
            const uint32_t submesh_index = static_cast<uint32_t>(submesh_idx);

            const auto& material_ptr = resolve_submesh_material(extras, submesh_index, group_material);
            if(!material_ptr)
            {
                continue;
            }

            shadow_batch_key key = make_shadow_batch_key(mesh, lod_index, submesh_index, material_ptr);
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

} // namespace unravel
