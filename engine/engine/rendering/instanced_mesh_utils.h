#pragma once

#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>

#include <cstdint>
#include <memory>

namespace unravel
{

/**
 * @brief Calls @p visit(mesh, material, submesh index) for every non-skinned submesh of the model's first LOD that
 * has a material: what an instanced mesh source draws of a model.
 */
template<typename Visitor>
void for_each_instanced_submesh(const model& mesh_model, Visitor&& visit)
{
    const auto mesh_asset = mesh_model.get_lod(0);
    const auto mesh_ptr = mesh_asset ? mesh_asset.get(false) : nullptr;
    if(!mesh_ptr)
    {
        return;
    }
    for(uint32_t group = 0; group < mesh_ptr->get_data_groups_count(); ++group)
    {
        const auto material_ptr = mesh_model.get_render_material(group);
        if(!material_ptr)
        {
            continue;
        }
        for(const size_t submesh : mesh_ptr->get_non_skinned_submeshes_indices(group, 0))
        {
            visit(mesh_ptr, material_ptr, uint32_t(submesh));
        }
    }
}

/**
 * @brief for_each_instanced_submesh() for the skinned submeshes: what a baked animation draws of a model.
 */
template<typename Visitor>
void for_each_instanced_skinned_submesh(const model& mesh_model, Visitor&& visit)
{
    const auto mesh_asset = mesh_model.get_lod(0);
    const auto mesh_ptr = mesh_asset ? mesh_asset.get(false) : nullptr;
    if(!mesh_ptr)
    {
        return;
    }
    for(uint32_t group = 0; group < mesh_ptr->get_data_groups_count(); ++group)
    {
        const auto material_ptr = mesh_model.get_render_material(group);
        if(!material_ptr)
        {
            continue;
        }
        for(const size_t submesh : mesh_ptr->get_skinned_submeshes_indices(group, 0))
        {
            visit(mesh_ptr, material_ptr, uint32_t(submesh));
        }
    }
}

} // namespace unravel
