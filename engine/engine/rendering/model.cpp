#include "model.h"
#include "gpu_program.h"
#include "graphics/graphics.h"
#include "material.h"
#include "mesh.h"
#include "camera.h"

#include <algorithm>
#include <cmath>

namespace unravel
{

namespace
{
    
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
    return math::square(screen_multiple * radius) / std::max(1.0f, dist_sqr);
}

auto compute_bounds_screen_radius_squared(const math::vec3& origin, float radius, const camera& view) -> float
{
    return compute_bounds_screen_radius_squared(origin, radius, view.get_position(), view.get_projection().get_matrix());
}

auto compute_conservative_world_bounds_sphere(const mesh& m, const math::transform& world_transform) -> math::bsphere
{
    const auto& bounds = m.get_bounds();
    const math::vec3 local_center = bounds.get_center();
    const math::vec3 local_extents = bounds.get_extents();
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


auto model::calculate_lod_data(lod_data& data, const math::transform& world_transform, const camera& cam, float dt) const -> bool
{
    data.transition_time = get_lod_transition_time().count();
    const auto lod_count = get_lods_count();
    const auto base_mesh = get_lod(0);
    if(!base_mesh)
    {
        return false;
    }

    auto mesh_ptr = base_mesh.get();
    if(!mesh_ptr)
    {
        return false;
    }

    const auto bsphere = compute_conservative_world_bounds_sphere(*mesh_ptr, world_transform);
    const float screen_radius_squared = compute_bounds_screen_radius_squared(bsphere.position, bsphere.radius, cam);

    const float screen_radius = std::sqrt(std::max(0.0f, screen_radius_squared));
    data.percent = math::clamp(screen_radius * 200.0f, 0.0f, 100.0f);
    data.center = bsphere.position;

    const float lod_screen_size_min = 0.01f;
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
                   const submit_callbacks& callbacks) const
{
    const auto lod_mesh = get_lod(lod);
    if(!lod_mesh)
    {
        return;
    }

    auto mesh = lod_mesh.get();

    auto skinned_submeshes_count = mesh->get_skinned_submeshes_count(lod);
    auto non_skinned_submeshes_count = mesh->get_non_skinned_submeshes_count(lod);

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

        auto render_submesh = [this](const std::shared_ptr<unravel::mesh>& mesh,
                                     uint32_t lod,
                                     uint32_t group_id,
                                     const math::mat4& matrix,
                                     const submesh_pose_mat4& pose,
                                     submit_callbacks::params& params,
                                     const submit_callbacks& callbacks)
        {
            auto mat = get_material_instance(group_id);
            if(!mat)
            {
                return;
            }

            const auto& submeshes = mesh->get_submeshes(lod);
            const auto& indices = mesh->get_non_skinned_submeshes_indices(group_id, lod);

            for(const auto& index : indices)
            {
                const auto& submesh = submeshes[index];

                if(pose.has_transforms(index))
                {
                    const size_t transform_count = pose.get_transform_count(index);

                    for(size_t i = 0; i < transform_count; ++i)
                    {
                        const auto* transform = pose.get_transform(index, i);
                        if(transform)
                        {
                            gfx::set_world_transform(*transform);
                            mesh->bind_render_buffers_for_submesh(submesh, lod);
                            params.preserve_state = (&index != &indices.back());
                            callbacks.setup_params_per_submesh(params, *mat);
                        }
                    }
                }
                else
                {
                    gfx::set_world_transform(matrix);
                    mesh->bind_render_buffers_for_submesh(submesh, lod);
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

        auto render_submesh_skinned = [this](const std::shared_ptr<unravel::mesh>& mesh,
                                             uint32_t lod,
                                             uint32_t group_id,
                                             const std::vector<pose_mat4>& skinning_transforms,
                                             submit_callbacks::params& params,
                                             const submit_callbacks& callbacks)
        {
            auto mat = get_material_instance(group_id);
            if(!mat)
            {
                return;
            }

            const auto& submeshes = mesh->get_submeshes(lod);
            const auto& indices = mesh->get_skinned_submeshes_indices(group_id, lod);

            for(const auto& index : indices)
            {
                const auto& submesh = submeshes[index];
                const auto& submesh_skinning_transforms = skinning_transforms[index];
                gfx::set_world_transform(submesh_skinning_transforms.transforms);

                mesh->bind_render_buffers_for_submesh(submesh, lod);
                params.preserve_state = &index != &indices.back();
                callbacks.setup_params_per_submesh(params, *mat);
            }
        };

        for(uint32_t i = 0; i < mesh->get_data_groups_count(); ++i)
        {
            render_submesh_skinned(mesh, lod, i, skinning_transforms, params, callbacks);
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

} // namespace unravel
