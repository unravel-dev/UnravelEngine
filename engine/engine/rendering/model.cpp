#include "model.h"
#include "gpu_program.h"
#include "graphics/graphics.h"
#include "material.h"
#include "mesh.h"

namespace unravel
{
bool model::is_valid() const
{
    return !mesh_lods_.empty();
}

auto model::get_lod(uint32_t lod) const -> asset_handle<mesh>
{
    if(mesh_lods_.size() > lod)
    {
        auto lodMesh = mesh_lods_[lod];
        if(lodMesh)
        {
            return lodMesh;
        }

        for(unsigned int i = lod; i < mesh_lods_.size(); ++i)
        {
            auto lodMesh = mesh_lods_[i];
            if(lodMesh)
            {
                return lodMesh;
            }
        }
        for(unsigned int i = lod; i > 0; --i)
        {
            auto lodMesh = mesh_lods_[i];
            if(lodMesh)
            {
                return lodMesh;
            }
        }
    }
    return {};
}

void model::set_lod(asset_handle<mesh> mesh, uint32_t lod)
{
    if(lod >= mesh_lods_.size())
    {
        mesh_lods_.resize(lod + 1);
        recalulate_lod_limits();
    }
    mesh_lods_[lod] = mesh;

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

void model::set_lods(const std::vector<asset_handle<mesh>>& lods)
{
    auto sz1 = lods.size();
    auto sz2 = mesh_lods_.size();

    mesh_lods_ = lods;

    if(sz1 != sz2)
    {
        recalulate_lod_limits();
    }

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


auto model::get_lod_limits() const -> const std::vector<urange32_t>&
{
    return lod_limits_;
}

void model::set_lod_limits(const std::vector<urange32_t>& limits)
{
    lod_limits_ = limits;
}

void model::submit(const math::mat4& world_transform,
                   const pose_mat4& submesh_transforms,
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

    auto skinned_submeshes_count = mesh->get_skinned_submeshes_count();
    auto non_skinned_submeshes_count = mesh->get_non_skinned_submeshes_count();

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
                                     uint32_t group_id,
                                     const math::mat4& matrix,
                                     const pose_mat4& pose,
                                     submit_callbacks::params& params,
                                     const submit_callbacks& callbacks)
        {
            auto mat = get_material_instance(group_id);
            if(!mat)
            {
                return;
            }

            const auto& submeshes = mesh->get_submeshes();
            const auto& indices = mesh->get_non_skinned_submeshes_indices(group_id);

            for(const auto& index : indices)
            {
                const auto& submesh = submeshes[index];

                if(index < pose.transforms.size())
                {
                    const auto& world = pose.transforms[index];
                    gfx::set_world_transform(world);
                }
                else
                {
                    gfx::set_world_transform(matrix);
                }

                mesh->bind_render_buffers_for_submesh(submesh);
                params.preserve_state = &index != &indices.back();
                callbacks.setup_params_per_submesh(params, *mat);
            }
        };

        for(uint32_t i = 0; i < mesh->get_data_groups_count(); ++i)
        {
            render_submesh(mesh, i, world_transform, submesh_transforms, params, callbacks);
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

            const auto& submeshes = mesh->get_submeshes();
            const auto& indices = mesh->get_skinned_submeshes_indices(group_id);
            const auto& palettes = mesh->get_bone_palettes();
            const auto& skin_data = mesh->get_skin_bind_data();

            for(const auto& index : indices)
            {
                const auto& submesh = submeshes[index];
                const auto& skinning_matrices = skinning_matrices_per_palette[index];
                gfx::set_world_transform(skinning_matrices.transforms);

                mesh->bind_render_buffers_for_submesh(submesh);
                params.preserve_state = &index != &indices.back();
                callbacks.setup_params_per_submesh(params, *mat);
            }
        };

        for(uint32_t i = 0; i < mesh->get_data_groups_count(); ++i)
        {
            render_submesh_skinned(mesh, i, skinning_matrices_per_palette, params, callbacks);
        }

        if(callbacks.setup_end)
        {
            callbacks.setup_end(params);
        }
    }
}

void model::recalulate_lod_limits()
{
    float upper_limit = 100.0f;
    lod_limits_.clear();
    lod_limits_.reserve(mesh_lods_.size());

    float initial = 0.1f;
    float step = initial / float(mesh_lods_.size());
    for(size_t i = 0; i < mesh_lods_.size(); ++i)
    {
        float lower_limit = 0.0f;

        if(mesh_lods_.size() - 1 != i)
        {
            lower_limit = upper_limit * (initial - ((i)*step));
        }

        lod_limits_.emplace_back(urange32_t::value_type(lower_limit), urange32_t::value_type(upper_limit));
        upper_limit = lower_limit;
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
