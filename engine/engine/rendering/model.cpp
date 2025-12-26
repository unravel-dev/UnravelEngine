#include "model.h"
#include "gpu_program.h"
#include "graphics/graphics.h"
#include "material.h"
#include "mesh.h"
#include "camera.h"

#include <cmath>

namespace unravel
{
bool model::is_valid() const
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
        auto lodMesh = mesh_lods_[i];
        if(lodMesh)
        {
            return lodMesh;
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
        recalulate_lod_limits(get_lods_count());
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

    recalulate_lod_limits(get_lods_count());

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


auto model::calculate_lod_data(lod_data& data, const math::transform& world_transform, const camera& cam, float transition_time, float dt) const -> bool
{
    
    const auto lod_count = get_lods_count();


    const auto& lod_limits = get_lod_limits();

    const auto base_mesh = get_lod(0);

    data.transition_time = transition_time;
    if(!base_mesh)
    {
        return false;
    }

    auto mesh_ptr = base_mesh.get();
    if(!mesh_ptr)
    {
        return false;
    }


    const auto& viewport = cam.get_viewport_size();
    auto rect = mesh_ptr->calculate_screen_rect(world_transform, cam);
    data.rect = rect;

    float percent = math::clamp((float(rect.height()) / float(viewport.height)) * 100.0f, 0.0f, 100.0f);
    data.percent = percent;

    std::size_t lod = 0;
    
    // If override is enabled, use the override level directly
    if(lod_override_enabled_)
    {
        lod = math::clamp<std::size_t>(lod_override_level_, 0, lod_count - 1);
    }
    else
    {
        // Calculate LOD based on screen percentage
        for(size_t i = 0; i < lod_limits.size(); ++i)
        {
            const auto& range = lod_limits[i];
            if(range.contains(urange32_t::value_type(percent)))
            {
                lod = i;
            }
        }

        lod = math::clamp<std::size_t>(lod, 0, lod_count - 1);
        
        // Apply bias to the calculated LOD
        // Positive bias selects less detailed LODs (higher index)
        // Negative bias selects more detailed LODs (lower index)
        float biased_lod = static_cast<float>(lod) + lod_selection_bias_;
        biased_lod = math::clamp(biased_lod, 0.0f, static_cast<float>(lod_count - 1));
        lod = static_cast<std::size_t>(biased_lod);
    }
    if(data.target_lod_index != lod && data.target_lod_index == data.current_lod_index)
    {
        data.target_lod_index = static_cast<std::uint32_t>(lod);
    }

    if(data.current_lod_index != data.target_lod_index)
    {
        data.current_time += dt;
    }

    if(data.current_time >= transition_time)
    {
        data.current_lod_index = data.target_lod_index;
        data.current_time = 0.0f;
    }

    // Camera
    const float camera_cull_threshold = 0.3f; // 0.3%

    return percent >= camera_cull_threshold;
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

auto model::get_lod_limits() const -> const std::vector<urange32_t>&
{
    return lod_limits_;
}

void model::set_lod_limits(const std::vector<urange32_t>& limits)
{
    lod_limits_ = limits;
}

void model::submit(const math::mat4& world_transform,
                   const submesh_pose_mat4& submesh_transforms,
                   const pose_mat4& bone_transforms,
                   const std::vector<pose_mat4>& skinning_matrices_per_palette,
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
    if(skinned_submeshes_count > 0 && !skinning_matrices_per_palette.empty())
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
                                             const std::vector<pose_mat4>& skinning_matrices_per_palette,
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
                const auto& skinning_matrices = skinning_matrices_per_palette[index];
                gfx::set_world_transform(skinning_matrices.transforms);

                mesh->bind_render_buffers_for_submesh(submesh, lod);
                params.preserve_state = &index != &indices.back();
                callbacks.setup_params_per_submesh(params, *mat);
            }
        };

        for(uint32_t i = 0; i < mesh->get_data_groups_count(); ++i)
        {
            render_submesh_skinned(mesh, lod, i, skinning_matrices_per_palette, params, callbacks);
        }

        if(callbacks.setup_end)
        {
            callbacks.setup_end(params);
        }
    }
}

void model::recalulate_lod_limits(uint32_t lod_count)
{
    lod_limits_.clear();
    if(lod_count == 0)
    {
        return;
    }
    lod_limits_.reserve(lod_count);
    // Unity-style LOD calculation using exponential decay (halving thresholds)
    // LOD 0 (highest detail): Used when screen height is 50% or more
    // Each subsequent LOD halves the threshold: 50%, 25%, 12.5%, 6.25%, etc.
    // The selection algorithm picks the LAST matching range, so we iterate from
    // lowest detail (checked first) to highest detail (checked last)
    const float lod0_threshold = 30.0f; // Start at 30% screen height for highest detail
    const float epsilon = 0.001f; // Small epsilon to avoid floating point overlap issues
    // Calculate ranges for each LOD level
    // The selection algorithm picks the LAST matching range, so higher detail LODs
    // (with higher indices) will be selected when multiple ranges match
    for(uint32_t i = 0; i < lod_count; ++i)
    {
        // Calculate threshold for this LOD level (50%, 25%, 12.5%, etc.)
        float threshold = lod0_threshold / std::pow(2.0f, static_cast<float>(i));
        float lower_limit = 0.0f;
        float upper_limit = 100.0f;
        // For LOD 0 (highest detail), range is [threshold, 100]
        if(i == 0)
        {
            lower_limit = threshold;
            upper_limit = 100.0f;
        }
        // For intermediate LODs, range is [threshold, previous_threshold)
        else if(i < lod_count - 1)
        {
            float previous_threshold = lod0_threshold / std::pow(2.0f, static_cast<float>(i - 1));
            lower_limit = threshold;
            // Subtract epsilon to make upper bound exclusive, avoiding overlap
            upper_limit = previous_threshold - epsilon;
        }
        // For the last LOD (lowest detail), range is [0, previous_threshold)
        else
        {
            float previous_threshold = lod0_threshold / std::pow(2.0f, static_cast<float>(i - 1));
            lower_limit = 0.0f;
            upper_limit = previous_threshold - epsilon;
        }
        // Clamp to valid percentage range [0, 100]
        upper_limit = math::clamp(upper_limit, 0.0f, 100.0f);
        lower_limit = math::clamp(lower_limit, 0.0f, 100.0f);
        // Store range with min=lower_limit, max=upper_limit
        // Example for 4 LODs (with epsilon = 0.001):
        // LOD 0: [50, 100]        - highest detail, used when >= 50%
        // LOD 1: [25, 49.999]    - used when 25% <= percent < 50%
        // LOD 2: [12.5, 24.999]  - used when 12.5% <= percent < 25%
        // LOD 3: [0, 12.499]     - lowest detail, used when < 12.5%
        lod_limits_.emplace_back(urange32_t::value_type(lower_limit), urange32_t::value_type(upper_limit));
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
