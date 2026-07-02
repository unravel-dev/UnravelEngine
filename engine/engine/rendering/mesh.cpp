#include "mesh.h"
#include "camera.h"
#include "generator/generator.hpp"
#include "glm/gtc/epsilon.hpp"

#include <graphics/index_buffer.h>
#include <graphics/vertex_buffer.h>
#include <logging/logging.h>
#include <memory/checked_delete.h>
#include <meshoptimizer/src/meshoptimizer.h>


#include <engine/engine.h>
#include <engine/assets/asset_manager.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace unravel
{

namespace
{
//-----------------------------------------------------------------------------
// Local Module Level Namespaces.
//-----------------------------------------------------------------------------

auto hf_height_at(hpp::span<const float> heights, uint32_t vx, int32_t sx, int32_t sz, int32_t ix, int32_t iz) -> double
{
    ix = std::clamp(ix, 0, sx);
    iz = std::clamp(iz, 0, sz);
    const size_t idx = static_cast<size_t>(iz) * vx + static_cast<size_t>(ix);
    return static_cast<double>(heights[idx]);
}

auto hf_dh_dt0(hpp::span<const float> heights, uint32_t vx, int32_t sx, int32_t sz, int32_t ix, int32_t iz) -> double
{
    if(sx < 1)
    {
        return 0.0;
    }
    if(ix <= 0)
    {
        return static_cast<double>(sx) * (hf_height_at(heights, vx, sx, sz, 1, iz) - hf_height_at(heights, vx, sx, sz, 0, iz));
    }
    if(ix >= sx)
    {
        return static_cast<double>(sx) *
               (hf_height_at(heights, vx, sx, sz, sx, iz) - hf_height_at(heights, vx, sx, sz, sx - 1, iz));
    }
    return static_cast<double>(sx) * 0.5 *
           (hf_height_at(heights, vx, sx, sz, ix + 1, iz) - hf_height_at(heights, vx, sx, sz, ix - 1, iz));
}

auto hf_dh_dt1(hpp::span<const float> heights, uint32_t vx, int32_t sx, int32_t sz, int32_t ix, int32_t iz) -> double
{
    if(sz < 1)
    {
        return 0.0;
    }
    if(iz <= 0)
    {
        return static_cast<double>(sz) * (hf_height_at(heights, vx, sx, sz, ix, 1) - hf_height_at(heights, vx, sx, sz, ix, 0));
    }
    if(iz >= sz)
    {
        return static_cast<double>(sz) *
               (hf_height_at(heights, vx, sx, sz, ix, sz) - hf_height_at(heights, vx, sx, sz, ix, sz - 1));
    }
    return static_cast<double>(sz) * 0.5 *
           (hf_height_at(heights, vx, sx, sz, ix, iz + 1) - hf_height_at(heights, vx, sx, sz, ix, iz - 1));
}

void create_mesh(const gfx::vertex_layout& format,
                 const generator::any_mesh& mesh,
                 mesh::preparation_data& data,
                 math::bbox& bbox)
{
    // Determine the correct offset to any relevant elements in the vertex
    bool has_position = format.has(gfx::attribute::Position);
    bool has_texcoord0 = format.has(gfx::attribute::TexCoord0);
    bool has_normals = format.has(gfx::attribute::Normal);
    bool has_tangents = format.has(gfx::attribute::Tangent);
    bool has_bitangents = format.has(gfx::attribute::Bitangent);
    uint16_t vertex_stride = format.getStride();

    auto triangle_count = generator::count(mesh.triangles());
    auto vertex_count = generator::count(mesh.vertices());
    data.triangle_count = uint32_t(triangle_count);
    data.vertex_count = uint32_t(vertex_count);

    // Allocate enough space for the new vertex and triangle data
    data.vertex_data.resize(data.vertex_count * vertex_stride);
    data.vertex_flags.resize(data.vertex_count);
    data.triangle_data.resize(data.triangle_count);
    mesh::submesh submesh;
    submesh.data_group_id = 0;
    submesh.face_count = data.triangle_count;
    submesh.face_start = 0;
    submesh.vertex_count = data.vertex_count;
    submesh.vertex_start = 0;

    uint8_t* current_vertex_ptr = data.vertex_data.data();
    size_t i = 0;
    for(const auto& v : mesh.vertices())
    {
        math::vec3 position = v.position;
        math::vec4 normal = math::vec4(v.normal, 0.0f);
        math::vec2 texcoords0 = v.tex_coord;
        // Store vertex components
        if(has_position)
            gfx::vertex_pack(math::value_ptr(position),
                             false,
                             gfx::attribute::Position,
                             format,
                             current_vertex_ptr,
                             uint32_t(i));
        if(has_normals)
            gfx::vertex_pack(math::value_ptr(normal),
                             true,
                             gfx::attribute::Normal,
                             format,
                             current_vertex_ptr,
                             uint32_t(i));
        if(has_texcoord0)
            gfx::vertex_pack(math::value_ptr(texcoords0),
                             true,
                             gfx::attribute::TexCoord0,
                             format,
                             current_vertex_ptr,
                             uint32_t(i));

        bbox.add_point(position);
        i++;
    }

    size_t tri_idx = 0;
    for(const auto& triangle : mesh.triangles())
    {
        const auto& indices = triangle.vertices;
        auto& tri = data.triangle_data[tri_idx];
        tri.indices[0] = uint32_t(indices[0]);
        tri.indices[1] = uint32_t(indices[1]);
        tri.indices[2] = uint32_t(indices[2]);

        tri_idx++;
    }

    // We need to generate binormals / tangents?
    data.compute_binormals = has_bitangents;
    data.compute_tangents = has_tangents;
    submesh.bbox = bbox;
    data.submeshes.emplace_back(submesh);

}

} // namespace

mesh::mesh() : hardware_vb_(std::make_shared<gfx::vertex_buffer>()), hardware_ib_(std::make_shared<gfx::index_buffer>())
{
}

mesh::~mesh()
{
    dispose();
}

void mesh::dispose()
{
    // Iterate through the different submeshes in the mesh and clean up
    for(auto submesh : mesh_submeshes_)
    {
        // Just perform a standard 'disconnect' in the
        // regular unload case.
        checked_delete(submesh);
    }

    mesh_submeshes_.clear();
    // submesh_lookup_.clear();
    data_groups_.clear();

    // Release bone palettes and skin data (if any)
    bone_palettes_.clear();
    skin_bind_data_.clear();

    // Clean up preparation data.
    if(preparation_data_.owns_source)
    {
        checked_array_delete(preparation_data_.vertex_source);
    }
    preparation_data_.vertex_source = nullptr;
    preparation_data_.source_format = {};
    preparation_data_.owns_source = false;
    preparation_data_.vertex_data.clear();
    preparation_data_.vertex_flags.clear();
    preparation_data_.vertex_records.clear();
    preparation_data_.triangle_data.clear();

    // Release mesh data memory
    checked_array_delete(system_vb_);
    checked_array_delete(system_ib_);

    // Clean up LOD data
    for(auto& lod : lods_)
    {
        // Clean up submeshes
        for(auto* submesh : lod.submeshes_)
        {
            checked_delete(submesh);
        }
        lod.submeshes_.clear();
        // Clean up index buffer
        checked_array_delete(lod.system_ib_);
        lod.hardware_ib_.reset();
    }
    lods_.clear();
    lod_count_ = 1;

    triangle_data_.clear();

    // Release resources
    hardware_vb_.reset();
    hardware_ib_.reset();

    // Clear variables
    preparation_data_.vertex_source = nullptr;
    preparation_data_.owns_source = false;
    preparation_data_.source_format = {};
    preparation_data_.triangle_count = 0;
    preparation_data_.vertex_count = 0;
    preparation_data_.compute_normals = false;
    preparation_data_.compute_binormals = false;
    preparation_data_.compute_tangents = false;
    prepare_status_ = mesh_status::not_prepared;
    face_count_ = 0;
    vertex_count_ = 0;
    system_vb_ = nullptr;
    vertex_format_ = {};
    system_ib_ = nullptr;
    force_tangent_generation_ = false;
    force_normal_generation_ = false;
    force_barycentric_generation_ = true;

    // Reset structures
    bbox_.reset();
}

auto mesh::get_info() const -> info
{
    info result{
        .vertices = vertex_count_,
        .triangles = face_count_,
        .submeshes = uint32_t(mesh_submeshes_.size()),
        .data_groups = uint32_t(data_groups_.size()),
        .lods = {}
    };

    // Add simplified LODs
    for(size_t i = 0; i < lods_.size(); ++i)
    {
        const auto& lod = lods_[i];
        result.lods.push_back(info::lod_info{
            .triangles = lod.face_count_,
            .percent = (static_cast<float>(lod.face_count_) / static_cast<float>(face_count_)) * 100.0f,
        });
    }

    return result;
}
auto mesh::prepare_mesh(const gfx::vertex_layout& format) -> bool
{
    // APPLOG_TRACE_PERF(std::chrono::milliseconds);

    // If we are already in the process of preparing, this is a no-op.
    if(prepare_status_ == mesh_status::preparing)
    {
        return false;
    }

    if((prepare_status_ != mesh_status::preparing))
    {
        // Clear out anything which is currently loaded in the mesh.
        dispose();

    } // End if not rolling back or no need to roll back

    // We are in the process of preparing the mesh
    prepare_status_ = mesh_status::preparing;
    vertex_format_ = format;

    return true;
}

// #define SET_VERTICES_WHEN_SETTING_PRIMITIVES 1

auto mesh::set_vertex_source(byte_array_t&& source, uint32_t vertex_count, const gfx::vertex_layout& source_format)
    -> bool
{
    // APPLOG_TRACE_PERF(std::chrono::milliseconds);

    // We can only do this if we are in the process of preparing the mesh
    if(prepare_status_ != mesh_status::preparing)
    {
        APPLOG_ERROR("Attempting to set a mesh vertex source without first calling "
                     "'prepareMesh' is not allowed.\n");
        return false;

    } // End if not preparing

    // Clear any existing source information.
    if(preparation_data_.owns_source)
    {
        checked_array_delete(preparation_data_.vertex_source);
    }
    preparation_data_.vertex_source = nullptr;
    preparation_data_.source_format = {};
    preparation_data_.owns_source = false;
    preparation_data_.vertex_records.clear();

    // Validate requirements
    if(vertex_count == 0)
    {
        return false;
    }

    // If source format matches the format we're using to prepare
    // then just store the pointer for this vertex source. Otherwise
    // we need to allocate a temporary buffer and convert the data.
    preparation_data_.source_format = source_format;
    if(source_format.m_hash == vertex_format_.m_hash)
    {
        preparation_data_.vertex_source = reinterpret_cast<uint8_t*>(source.data());

    } // End if matching
    else
    {
        preparation_data_.vertex_source = new uint8_t[vertex_count * vertex_format_.getStride()];
        preparation_data_.owns_source = true;
        gfx::vertex_convert(vertex_format_,
                            preparation_data_.vertex_source,
                            source_format,
                            reinterpret_cast<uint8_t*>(source.data()),
                            vertex_count);
    } // End if !matching

    // Some data needs computing? These variables are essentially 'toggles'
    // that are set largely so that we can early out if it was NEVER necessary
    // to generate these components (i.e. not one single vertex needed it).
    if(!source_format.has(gfx::attribute::Normal) && vertex_format_.has(gfx::attribute::Normal))
    {
        preparation_data_.compute_normals = true;
    }
    if(!source_format.has(gfx::attribute::Bitangent) && vertex_format_.has(gfx::attribute::Bitangent))
    {
        preparation_data_.compute_binormals = true;
    }
    if(!source_format.has(gfx::attribute::Tangent) && vertex_format_.has(gfx::attribute::Tangent))
    {
        preparation_data_.compute_tangents = true;
    }

    math::vec4 normal{};
    math::vec4 tangent{};
    math::vec4 bitangent{};
    gfx::vertex_unpack(math::value_ptr(normal), gfx::attribute::Normal, vertex_format_, preparation_data_.vertex_source, 0);
    gfx::vertex_unpack(math::value_ptr(tangent), gfx::attribute::Tangent, vertex_format_, preparation_data_.vertex_source, 0);
    gfx::vertex_unpack(math::value_ptr(bitangent), gfx::attribute::Bitangent, vertex_format_, preparation_data_.vertex_source, 0);
    if(math::epsilonEqual(math::length(normal), 0.0f, math::epsilon<float>()))
    {
        preparation_data_.compute_normals = true;
    }
    if(math::epsilonEqual(math::length(tangent), 0.0f, math::epsilon<float>()))
    {
        preparation_data_.compute_tangents = true;
    }
    if(math::epsilonEqual(math::length(bitangent), 0.0f, math::epsilon<float>()))
    {
        preparation_data_.compute_binormals = true;
    }

#ifdef SET_VERTICES_WHEN_SETTING_PRIMITIVES
    // Allocate the vertex records for the new vertex buffer
    preparation_data_.vertex_records.clear();
    preparation_data_.vertex_records.resize(vertex_count);

    // Fill with 0xFFFFFFFF initially to indicate that no vertex
    // originally in this location has yet been inserted into the
    // final vertex list.
    memset(preparation_data_.vertex_records.data(), 0xFF, vertex_count * sizeof(uint32_t));
#else
    preparation_data_.vertex_data = std::move(source);
    preparation_data_.vertex_count = vertex_count;
#endif
    // Success!
    return true;
}

auto mesh::set_bounding_box(const math::bbox& box) -> bool
{
    // APPLOG_TRACE_PERF(std::chrono::milliseconds);

    bbox_ = box;
    return true;
}

auto mesh::set_submeshes(const std::vector<submesh>& submeshes) -> bool
{
    // APPLOG_TRACE_PERF(std::chrono::milliseconds);

    // We can only do this if we are in the process of preparing the mesh
    if(prepare_status_ != mesh_status::preparing)
    {
        APPLOG_ERROR("Attempting to add primitives to a mesh without first calling "
                     "'prepareMesh' is not allowed.\n");
        return false;

    } // End if not preparing

    preparation_data_.submeshes = submeshes;

    return true;
}

auto mesh::set_primitives(triangle_array_t&& triangles) -> bool
{
    // APPLOG_TRACE_PERF(std::chrono::milliseconds);

    // We can only do this if we are in the process of preparing the mesh
    if(prepare_status_ != mesh_status::preparing)
    {
        APPLOG_ERROR("Attempting to add primitives to a mesh without first calling "
                     "'prepareMesh' is not allowed.\n");
        return false;

    } // End if not preparing

#ifdef SET_VERTICES_WHEN_SETTING_PRIMITIVES

    preparation_data_.triangle_count = 0;
    preparation_data_.triangle_data.clear();

    // Determine the correct offset to any relevant elements in the vertex
    bool has_position = vertex_format_.has(gfx::attribute::Position);
    bool has_normal = vertex_format_.has(gfx::attribute::Normal);
    uint16_t vertex_stride = vertex_format_.getStride();

    // During the construction process we test to see if any specified
    // vertex normal contains invalid data. If the original source vertex
    // data did not contain a normal, we can optimize and skip this step.
    bool source_has_normals = preparation_data_.source_format.has(gfx::attribute::Normal);
    bool source_has_binormal = preparation_data_.source_format.has(gfx::attribute::Bitangent);
    bool source_has_tangent = preparation_data_.source_format.has(gfx::attribute::Tangent);

    // In addition, we also record which of the required components each
    // vertex actually contained based on the following information.
    uint8_t vertex_flags = 0;
    if(source_has_normals)
    {
        vertex_flags |= preparation_data::source_contains_normal;
    }
    if(source_has_binormal)
    {
        vertex_flags |= preparation_data::source_contains_binormal;
    }
    if(source_has_tangent)
    {
        vertex_flags |= preparation_data::source_contains_tangent;
    }

    // Loop through the specified faces and process them.
    uint8_t* src_vertices_ptr = preparation_data_.vertex_source;

    for(const auto& src_tri : triangles)
    {
        // Retrieve vertex positions (if there are any) so that we can perform
        // degenerate testing.
        if(preparation_data_.check_for_degenerates)
        {
            if(has_position)
            {
                math::vec3 v1;
                float vf1[4];
                gfx::vertex_unpack(vf1, gfx::attribute::Position, vertex_format_, src_vertices_ptr, src_tri.indices[0]);
                math::vec3 v2;
                float vf2[4];
                gfx::vertex_unpack(vf2, gfx::attribute::Position, vertex_format_, src_vertices_ptr, src_tri.indices[1]);
                math::vec3 v3;
                float vf3[4];
                gfx::vertex_unpack(vf3, gfx::attribute::Position, vertex_format_, src_vertices_ptr, src_tri.indices[2]);
                std::memcpy(&v1[0], vf1, 3 * sizeof(float));
                std::memcpy(&v2[0], vf2, 3 * sizeof(float));
                std::memcpy(&v3[0], vf3, 3 * sizeof(float));

                // Skip triangle if it is degenerate.
                if(math::all(math::equal(v1, v2, math::epsilon<float>())) ||
                   math::all(math::equal(v1, v3, math::epsilon<float>())) ||
                   math::all(math::equal(v2, v3, math::epsilon<float>())))
                {
                    continue;
                }
            } // End if has position.
        }
        // Prepare a triangle structure ready for population
        preparation_data_.triangle_count++;
        preparation_data_.triangle_data.resize(preparation_data_.triangle_count);
        triangle& triangle_data = preparation_data_.triangle_data[preparation_data_.triangle_count - 1];

        // Set triangle's submesh information.
        triangle_data.data_group_id = src_tri.data_group_id;

        // For each index in the face
        for(uint32_t j = 0; j < 3; ++j)
        {
            // Extract the original index from the specified index buffer
            uint32_t orig_index = src_tri.indices[j];

            // Retrieve the vertex record for the original vertex
            uint32_t index = preparation_data_.vertex_records[orig_index];

            // Have we inserted this vertex into the vertex buffer previously?
            if(index == 0xFFFFFFFF)
            {
                // Vertex does not yet exist in the vertex buffer we are preparing
                // so copy the vertex in and record the index mapping for this vertex.
                index = preparation_data_.vertex_count++;
                preparation_data_.vertex_records[orig_index] = index;

                // Resize the output vertex buffer ready to hold this new data.
                size_t initial_size = preparation_data_.vertex_data.size();
                preparation_data_.vertex_data.resize(initial_size + vertex_stride);

                // Copy the data in.
                uint8_t* src_ptr = src_vertices_ptr + (orig_index * vertex_stride);
                uint8_t* dst_ptr = &preparation_data_.vertex_data[initial_size];
                std::memcpy(dst_ptr, src_ptr, vertex_stride);

                // Also record other pertenant details about this vertex.
                preparation_data_.vertex_flags.push_back(vertex_flags);

                // Clear any invalid normals (completely messes up HDR if ANY NaNs make
                // it this far)
                // if(has_normal && source_has_normals)
                // {
                //     float fnorm[4];
                //     gfx::vertex_unpack(fnorm, gfx::attribute::Normal, vertex_format_, dst_ptr);
                //     if(std::isnan(fnorm[0]) || std::isnan(fnorm[1]) || std::isnan(fnorm[2]))
                //     {
                //         gfx::vertex_pack(fnorm, true, gfx::attribute::Normal, vertex_format_, dst_ptr);
                //     }
                // } // End if have normal

                // Grow the size of the bounding box
                if(has_position)
                {
                    float fpos[4];
                    gfx::vertex_unpack(fpos, gfx::attribute::Position, vertex_format_, dst_ptr);
                    bbox_.add_point(math::vec3(fpos[0], fpos[1], fpos[2]));
                }

            } // End if vertex not recorded in this buffer yet

            // Copy the index in
            triangle_data.indices[j] = index;

        } // Next Index

    } // Next Face

#else
    preparation_data_.triangle_count = triangles.size();
    preparation_data_.triangle_data = std::move(triangles);

#endif

    // Success!
    return true;
}

auto mesh::bind_skin(const skin_bind_data& bind_data) -> bool
{
    // APPLOG_TRACE_PERF(std::chrono::milliseconds);

    if(!bind_data.has_bones())
    {
        return true;
    }

    if(prepare_status_ == mesh_status::prepared)
    {
        return false;
    }

    skin_bind_data::vertex_data_array_t vertex_table;
    skin_bind_data_.clear();
    skin_bind_data_ = bind_data;

    // Build a list of all bone indices and associated weights for each vertex.
    skin_bind_data_.build_vertex_table(preparation_data_.vertex_count, preparation_data_.vertex_records, vertex_table);
    skin_bind_data_.clear_vertex_influences(); // Clear unneeded data to save space.

    uint32_t palette_size = gfx::get_max_blend_transforms();

    // Destroy any previous palette entries.
    bone_palettes_.clear();

    triangle_array_t& tri_data = preparation_data_.triangle_data;

    bone_palettes_.reserve(preparation_data_.submeshes.size());
    // Iterate over each submesh to generate palettes.
    for(size_t palette_id = 0; palette_id < preparation_data_.submeshes.size(); ++palette_id)
    {
        auto& submesh = preparation_data_.submeshes[palette_id];
        // face_influences used_bones;

        std::vector<bool> used_bones(gfx::get_max_blend_transforms(), false);
        std::vector<uint32_t> faces; // Collect faces in this submesh
        faces.reserve(submesh.face_count);
        // Collect all unique bone indices influencing this submesh and the faces.
        for(uint32_t i = submesh.face_start; i < submesh.face_start + submesh.face_count; ++i)
        {
            faces.push_back(i);

            for(uint32_t vertex_index : tri_data[i].indices)
            {
                const auto& data = vertex_table[vertex_index];
                for(const auto& influence : data.influences)
                {
                    // used_bones.bones[static_cast<uint32_t>(influence)] = 1;
                    used_bones[static_cast<uint32_t>(influence)] = true;
                }
            }
        }

        // Create a bone palette for this submesh.
        bone_palette new_palette(palette_size);
        new_palette.set_data_group(submesh.data_group_id);

        // Assign bones and faces to the palette.
        // new_palette.assign_bones(used_bones.bones, faces);
        new_palette.assign_bones(used_bones, faces);
        bone_palettes_.push_back(new_palette);

        // Assign the palette ID to each vertex in this submesh.

        auto face_start = submesh.face_start;
        auto face_end = submesh.face_start + submesh.face_count;
        for(uint32_t i = face_start; i < face_end; ++i)
        {
            for(uint32_t k = 0; k < 3; ++k)
            {
                uint32_t vertex_index = tri_data[i].indices[k];
                auto& data = vertex_table[vertex_index];

                // If the vertex is not already assigned to a palette, assign it.
                if(data.palette == -1)
                {
                    data.palette = static_cast<int32_t>(palette_id);

                    // Check if the vertex index falls within the submesh's vertex range
                    if(submesh.vertex_start == -1 || vertex_index < submesh.vertex_start)
                    {
                        submesh.vertex_start = vertex_index;
                    }
                    if(vertex_index >= submesh.vertex_start + submesh.vertex_count)
                    {
                        submesh.vertex_count = (vertex_index - submesh.vertex_start) + 1;
                    }
                }
                else if(data.palette != static_cast<int32_t>(palette_id))
                {
                    // Vertex is shared between submeshes, need to duplicate it.
                    uint32_t new_index = static_cast<uint32_t>(vertex_table.size());

                    // Create a new vertex_data.
                    skin_bind_data::vertex_data new_vertex(data);
                    new_vertex.original_vertex = vertex_index;
                    new_vertex.palette = static_cast<int32_t>(palette_id);
                    vertex_table.push_back(new_vertex);

                    // Update submesh's vertex range
                    if(submesh.vertex_start == -1 || new_index < submesh.vertex_start)
                    {
                        submesh.vertex_start = new_index;
                    }
                    if(new_index >= submesh.vertex_start + submesh.vertex_count)
                    {
                        submesh.vertex_count = (new_index - submesh.vertex_start) + 1;
                    }

                    // Update triangle index to point to new vertex.
                    tri_data[i].indices[k] = new_index;
                }
                // Else, the vertex is already assigned to this palette; no action needed.
            }
        }
    }

    // Adjust vertex format to include blend weights and indices if necessary.
    gfx::vertex_layout new_format(vertex_format_);
    gfx::vertex_layout original_format = vertex_format_;
    bool has_weights = new_format.has(gfx::attribute::Weight);
    bool has_indices = new_format.has(gfx::attribute::Indices);
    if(!has_weights || !has_indices)
    {
        new_format.m_hash = 0;
        if(!has_weights)
        {
            new_format.add(gfx::attribute::Weight, 4, gfx::attribute_type::Float);
        }
        if(!has_indices)
        {
            new_format.add(gfx::attribute::Indices, 4, gfx::attribute_type::Float, false, true);
        }

        new_format.end();
        // Update the vertex format.
        vertex_format_ = new_format;
    }

    // Get access to final data offset information.
    uint16_t vertex_stride = vertex_format_.getStride();

    // Now we need to update the vertex data as required.
    uint32_t original_vertex_count = preparation_data_.vertex_count;
    if(vertex_format_.m_hash != original_format.m_hash)
    {
        // Format has changed, run conversion.
        byte_array_t original_buffer(preparation_data_.vertex_data);
        preparation_data_.vertex_data.clear();
        preparation_data_.vertex_data.resize(vertex_table.size() * vertex_stride);
        preparation_data_.vertex_flags.resize(vertex_table.size());

        gfx::vertex_convert(vertex_format_,
                            preparation_data_.vertex_data.data(),
                            original_format,
                            original_buffer.data(),
                            original_vertex_count);
    }
    else
    {
        // No conversion required, just ensure buffer is large enough.
        preparation_data_.vertex_data.resize(vertex_table.size() * vertex_stride);
        preparation_data_.vertex_flags.resize(vertex_table.size());
    }

    // Update vertex data with new bone indices and weights.
    uint8_t* src_vertices_ptr = preparation_data_.vertex_data.data();
    for(size_t i = 0; i < vertex_table.size(); ++i)
    {
        auto& data = vertex_table[i];

        // Determine which palette this vertex belongs to.
        int32_t palette_id = data.palette;

        // Skip if the vertex isn't assigned to any palette.
        if(palette_id < 0)
        {
            continue;
        }

        const auto& palette = bone_palettes_[static_cast<size_t>(palette_id)];

        // If this is a new vertex, duplicate data from original vertex.
        if(i >= original_vertex_count)
        {
            std::memcpy(src_vertices_ptr + (i * vertex_stride),
                        src_vertices_ptr + (data.original_vertex * vertex_stride),
                        vertex_stride);

            // Also duplicate additional vertex data.
            preparation_data_.vertex_flags[i] = preparation_data_.vertex_flags[data.original_vertex];
        }

        uint32_t max_bones = std::min<uint32_t>(4, uint32_t(data.influences.size()));

        if(max_bones > 0)
        {
            // Assign bone indices (the index to the relevant entry in the palette, not the main bone list index) and
            // weights.
            math::vec4 blend_weights(0.0f, 0.0f, 0.0f, 0.0f);
            math::vec4 blend_indices(0.0f, 0.0f, 0.0f, 0.0f);

            for(uint32_t j = 0; j < max_bones; ++j)
            {
                // Map global bone index to local palette index.
                uint32_t palette_bone_index =
                    palette.translate_bone_to_palette(static_cast<uint32_t>(data.influences[j]));

                blend_indices[static_cast<math::vec4::length_type>(j)] = static_cast<float>(palette_bone_index);
                blend_weights[static_cast<math::vec4::length_type>(j)] = data.weights[j];
            }

            gfx::vertex_pack(math::value_ptr(blend_weights),
                             false,
                             gfx::attribute::Weight,
                             vertex_format_,
                             src_vertices_ptr,
                             uint32_t(i));

            gfx::vertex_pack(math::value_ptr(blend_indices),
                             false,
                             gfx::attribute::Indices,
                             vertex_format_,
                             src_vertices_ptr,
                             uint32_t(i));
        }
    }

    // Update vertex count to match final size.
    preparation_data_.vertex_count = static_cast<uint32_t>(vertex_table.size());

    // Skin is now bound.
    return true;
}

auto mesh::bind_armature(std::unique_ptr<armature_node>& root) -> bool
{
    // APPLOG_TRACE_PERF(std::chrono::milliseconds);

    root_ = std::move(root);
    return true;
}

auto mesh::load_mesh(load_data&& data) -> bool
{
    // APPLOG_TRACE_PERF(std::chrono::milliseconds);

    default_material_uids_ = std::move(data.default_material_uids);

    const bool has_skin_data = data.skin_data.has_bones();
    const bool skin_is_prepared = data.skin_is_prepared;
    auto bone_palette_bones = std::move(data.bone_palette_bones);

    bool result = true;
    result &= prepare_mesh(data.vertex_format);
    result &= set_bounding_box(data.bbox);
    result &= set_vertex_source(std::move(data.vertex_data), data.vertex_count, data.vertex_format);
    result &= set_primitives(std::move(data.triangle_data));
    result &= set_submeshes(data.submeshes);
    if(has_skin_data && skin_is_prepared)
    {
        skin_bind_data_.clear();
        skin_bind_data_ = data.skin_data;
    }
    else
    {
        result &= bind_skin(data.skin_data);
    }
    result &= bind_armature(data.root_node);
    result &= end_prepare();

    // Restore bone palettes for pre-skinned meshes (compiled assets) without re-running bind_skin.
    if(result && has_skin_data && skin_is_prepared)
    {
        bone_palettes_.clear();
        const uint32_t palette_size = gfx::get_max_blend_transforms();
        bone_palettes_.reserve(data.submeshes.size());
        for(size_t palette_id = 0; palette_id < data.submeshes.size(); ++palette_id)
        {
            bone_palette palette(palette_size);
            palette.set_data_group(data.submeshes[palette_id].data_group_id);
            if(palette_id < bone_palette_bones.size())
            {
                palette.assign_bones(bone_palette_bones[palette_id]);
            }
            bone_palettes_.push_back(std::move(palette));
        }
    }

    // Restore LODs from load_data if they exist
    if(result)
    {
        result &= restore_lods_from_load_data(data);
    }

    return result;
}

auto mesh::create_plane(const gfx::vertex_layout& format,
                        float width,
                        float height,
                        uint32_t width_segments,
                        uint32_t height_segments,
                        mesh_create_origin origin,
                        bool hardware_copy /* = true */) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    plane_mesh_t plane({width * 0.5f, height * 0.5f}, {width_segments, height_segments});
    math::quat rot1(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    math::quat rot2(math::vec3(math::radians(90.0f), 0.f, 0.0f));

    auto plane1 = rotate_mesh(plane, rot1);
    auto plane2 = rotate_mesh(plane, rot2);
    auto mesh = merge_mesh(plane1, plane2);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_heightfield(const gfx::vertex_layout& format,
                              hpp::span<const float> heights,
                              uint32_t segments_x,
                              uint32_t segments_z,
                              float half_extent_x,
                              float half_extent_z,
                              float height_scale,
                              mesh_create_origin origin,
                              bool hardware_copy) -> bool
{
    (void)origin;
    const uint32_t sx = segments_x;
    const uint32_t sz = segments_z;
    if(sx < 1u || sz < 1u)
    {
        return false;
    }
    const uint32_t vx = sx + 1u;
    if(heights.size() < static_cast<size_t>(vx) * static_cast<size_t>(sz + 1u))
    {
        return false;
    }

    prepare_mesh(format);

    const double hx = static_cast<double>(half_extent_x);
    const double hz = static_cast<double>(half_extent_z);
    const double hs = static_cast<double>(height_scale);
    const int32_t isx = static_cast<int32_t>(sx);
    const int32_t isz = static_cast<int32_t>(sz);

    auto eval = [heights, vx, sx, sz, hx, hz, hs, isx, isz](const gml::dvec2& t) -> generator::mesh_vertex_t
    {
        const double fx = t[0] * static_cast<double>(sx);
        const double fz = t[1] * static_cast<double>(sz);
        int32_t ix = static_cast<int32_t>(std::lround(fx));
        int32_t iz = static_cast<int32_t>(std::lround(fz));
        ix = std::clamp(ix, 0, isx);
        iz = std::clamp(iz, 0, isz);

        generator::mesh_vertex_t v{};
        v.position[0] = (t[0] - 0.5) * 2.0 * hx;
        v.position[1] = hs * hf_height_at(heights, vx, isx, isz, ix, iz);
        v.position[2] = (t[1] - 0.5) * 2.0 * hz;
        v.tex_coord[0] = t[0];
        v.tex_coord[1] = t[1];

        const gml::dvec3 dr_dt0{2.0 * hx, hs * hf_dh_dt0(heights, vx, isx, isz, ix, iz), 0.0};
        const gml::dvec3 dr_dt1{0.0, hs * hf_dh_dt1(heights, vx, isx, isz, ix, iz), 2.0 * hz};
        v.normal = -gml::normalize(gml::cross(dr_dt1, dr_dt0));

        return v;
    };

    generator::parametric_mesh_t hf_mesh(eval, gml::ivec2{static_cast<int>(sx), static_cast<int>(sz)});
    math::quat rot(math::vec3(math::radians(-180.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(hf_mesh, rot);
    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    
    return end_prepare(hardware_copy);
}

auto mesh::create_cube(const gfx::vertex_layout& format,
                       float width,
                       float height,
                       float depth,
                       uint32_t width_segments,
                       uint32_t height_segments,
                       uint32_t depth_segments,
                       mesh_create_origin origin,
                       bool hardware_copy /* = true */) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    box_mesh_t box({width * 0.5f, height * 0.5f, depth * 0.5f}, {width_segments, height_segments, depth_segments});
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(box, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}


auto mesh::create_rounded_cube(const gfx::vertex_layout& format,
    float width,
    float height,
    float depth,
    uint32_t width_segments,
    uint32_t height_segments,
    uint32_t depth_segments,
    mesh_create_origin origin,
    bool hardware_copy /* = true */) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    rounded_box_mesh_t rounded_box(0.05, {width * 0.5f, height * 0.5f, depth * 0.5f}, 4, {width_segments, height_segments, depth_segments});
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(rounded_box, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_sphere(const gfx::vertex_layout& format,
                         float radius,
                         uint32_t stacks,
                         uint32_t slices,
                         mesh_create_origin origin,
                         bool hardware_copy /* = true */) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    sphere_mesh_t sphere(radius, static_cast<int>(slices), static_cast<int>(stacks));
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(sphere, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_cylinder(const gfx::vertex_layout& format,
                           float radius,
                           float height,
                           uint32_t stacks,
                           uint32_t slices,
                           mesh_create_origin origin,
                           bool hardware_copy /* = true */) -> bool
{
    // Clear out old data.
    dispose();

    // We are in the process of preparing.
    prepare_status_ = mesh_status::preparing;
    vertex_format_ = format;

    using namespace generator;
    capped_cylinder_mesh_t cylinder(radius, height * 0.5, static_cast<int>(slices), static_cast<int>(stacks));
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(cylinder, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_capsule(const gfx::vertex_layout& format,
                          float radius,
                          float height,
                          uint32_t stacks,
                          uint32_t slices,
                          mesh_create_origin origin,
                          bool hardware_copy /* = true */) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    capsule_mesh_t capsule(radius, height * 0.5, static_cast<int>(slices), static_cast<int>(stacks));
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(capsule, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_cone(const gfx::vertex_layout& format,
                       float radius,
                       float radius_tip,
                       float height,
                       uint32_t stacks,
                       uint32_t slices,
                       mesh_create_origin origin,
                       bool hardware_copy /* = true */) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    capped_cone_mesh_t cone(radius, 1.0, static_cast<int>(stacks), static_cast<int>(slices));
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(cone, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_torus(const gfx::vertex_layout& format,
                        float outer_radius,
                        float inner_radius,
                        uint32_t bands,
                        uint32_t sides,
                        mesh_create_origin origin,
                        bool hardware_copy /* = true */) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    torus_mesh_t torus(inner_radius, outer_radius, static_cast<int>(sides), static_cast<int>(bands));
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(torus, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_teapot(const gfx::vertex_layout& format, bool hardware_copy /*= true*/) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    teapot_mesh_t teapot;
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(teapot, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_icosahedron(const gfx::vertex_layout& format, bool hardware_copy /*= true*/) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    icosahedron_mesh_t icosahedron;
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(icosahedron, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_dodecahedron(const gfx::vertex_layout& format, bool hardware_copy /*= true*/) -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    dodecahedron_mesh_t dodecahedron;
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(dodecahedron, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

auto mesh::create_icosphere(const gfx::vertex_layout& format, int tesselation_level, bool hardware_copy /*= true*/)
    -> bool
{
    // We are in the process of preparing.
    prepare_mesh(format);

    using namespace generator;
    ico_sphere_mesh_t icosphere(1, tesselation_level + 1);
    math::quat rot(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    auto mesh = rotate_mesh(icosphere, rot);

    create_mesh(vertex_format_, mesh, preparation_data_, bbox_);
    // Finish up
    return end_prepare(hardware_copy);
}

void mesh::check_for_degenerates()
{
    // Scan the preparation data for degenerate triangles.
    uint16_t position_offset = vertex_format_.getOffset(gfx::attribute::Position);
    // uint16_t vertex_stride = _vertex_format.getStride();
    uint8_t* src_vertices_ptr = preparation_data_.vertex_data.data() + position_offset;

    if(preparation_data_.check_for_degenerates)
    {
        for(uint32_t i = 0; i < preparation_data_.triangle_count; ++i)
        {
            triangle& tri = preparation_data_.triangle_data[i];
            math::vec3 v1;
            float vf1[4];
            gfx::vertex_unpack(vf1, gfx::attribute::Position, vertex_format_, src_vertices_ptr, tri.indices[0]);
            math::vec3 v2;
            float vf2[4];
            gfx::vertex_unpack(vf2, gfx::attribute::Position, vertex_format_, src_vertices_ptr, tri.indices[1]);
            math::vec3 v3;
            float vf3[4];
            gfx::vertex_unpack(vf3, gfx::attribute::Position, vertex_format_, src_vertices_ptr, tri.indices[2]);
            std::memcpy(&v1[0], vf1, 3 * sizeof(float));
            std::memcpy(&v2[0], vf2, 3 * sizeof(float));
            std::memcpy(&v3[0], vf3, 3 * sizeof(float));

            math::vec3 c = math::cross(v2 - v1, v3 - v1);
            if(math::length2(c) < (4.0f * 0.000001f * 0.000001f))
            {
                tri.flags |= triangle_flags::degenerate;
            }

        } // Next triangle
    }
}

auto mesh::end_prepare(bool hardware_copy, bool build_buffers, bool weld, bool optimize) -> bool
{
    // APPLOG_TRACE_PERF(std::chrono::milliseconds);

    // Were we previously preparing?
    if(prepare_status_ != mesh_status::preparing)
    {
        APPLOG_ERROR("Attempting to call 'end_prepare' on a mesh without first "
                     "calling 'prepare_mesh' is not "
                     "allowed.\n");
        return false;

    } // End if previously preparing

    // Check for degenerates
    check_for_degenerates();

    // Process the vertex data in order to generate any additional components that
    // may be necessary
    // (i.e. Normal, Binormal and Tangent)
    if(!generate_vertex_components(weld))
    {
        return false;
    }

    // Allocate the system memory vertex buffer ready for population.
    vertex_count_ = preparation_data_.vertex_count;
    system_vb_ = new uint8_t[vertex_count_ * vertex_format_.getStride()];

    // Copy vertex data into the new buffer and dispose of the temporary data.
    std::memcpy(system_vb_, preparation_data_.vertex_data.data(), vertex_count_ * vertex_format_.getStride());
    preparation_data_.vertex_data.clear();
    preparation_data_.vertex_flags.clear();
    preparation_data_.vertex_count = 0;

    // Index data has been updated and potentially needs to be serialized.
    if(build_buffers)
    {
        build_vb(hardware_copy);
    }

    // Allocate the memory for our system memory index buffer
    face_count_ = preparation_data_.triangle_count;
    system_ib_ = new uint32_t[face_count_ * 3];

    // Finally perform the final sort of the mesh data in order
    // to build the index buffer and submesh tables
    if(!sort_mesh_data())
    {
        return false;
    }

    // Hardware versions of the final buffer were required?
    if(build_buffers)
    {
        build_ib(hardware_copy);
    }

    if(preparation_data_.owns_source)
    {
        checked_array_delete(preparation_data_.vertex_source);
    }
    preparation_data_.vertex_source = nullptr;

    // The mesh is now prepared
    prepare_status_ = mesh_status::prepared;
    hardware_mesh_ = hardware_copy;
    optimize_mesh_ = optimize;

    // Success!
    return true;
}

void mesh::build_vb(bool hardware_copy)
{
    // A video memory copy of the mesh was requested?
    if(hardware_copy)
    {
        // Calculate the required size of the vertex buffer
        auto buffer_size = vertex_count_ * vertex_format_.getStride();

        // Compute-read flags so the vertex buffer can be bound as a raw read-only
        // Buffer<float> inside shaders (e.g. vertex pulling for wireframe overlay).
        const uint16_t vb_flags =
            BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_COMPUTE_FORMAT_32X1 | BGFX_BUFFER_COMPUTE_TYPE_FLOAT;

        const gfx::memory_view* mem = gfx::make_ref(system_vb_, buffer_size);
        hardware_vb_ = std::make_shared<gfx::vertex_buffer>(mem, vertex_format_, vb_flags);

    } // End if video memory vertex buffer required
}

void mesh::build_ib(bool hardware_copy)
{
    // Hardware versions of the final buffer were required?
    if(hardware_copy)
    {
        // Calculate the required size of the index buffer
        auto buffer_size = static_cast<uint32_t>(size_t(face_count_ * 3) * sizeof(uint32_t));

        // Compute-read flags so the (32-bit) index buffer can be bound as a raw
        // read-only Buffer<uint> inside shaders (e.g. vertex pulling for wireframe overlay).
        const uint16_t ib_flags = BGFX_BUFFER_INDEX32
            | BGFX_BUFFER_COMPUTE_READ
            | BGFX_BUFFER_COMPUTE_FORMAT_32X1
            | BGFX_BUFFER_COMPUTE_TYPE_UINT;

        // Allocate hardware buffer if required (i.e. it does not already exist).
        if(!hardware_ib_)
        {
            const gfx::memory_view* mem = gfx::make_ref(system_ib_, buffer_size);
            hardware_ib_ = std::make_shared<gfx::index_buffer>(mem, ib_flags);
        } // End if not allocated
        else
        {
            auto ib = std::static_pointer_cast<gfx::index_buffer>(hardware_ib_);
            if(!ib->is_valid())
            {
                const gfx::memory_view* mem = gfx::make_ref(system_ib_, buffer_size);
                hardware_ib_ = std::make_shared<gfx::index_buffer>(mem, ib_flags);
            }
        }

    } // End if hardware buffer required
}

auto mesh::generate_adjacency(std::vector<uint32_t>& adjacency) -> bool
{
    std::map<adjacent_edge_key, uint32_t> edge_tree;
    std::map<adjacent_edge_key, uint32_t>::iterator it_edge;

    // What is the status of the mesh?
    if(prepare_status_ != mesh_status::prepared)
    {
        // Validate requirements
        if(preparation_data_.triangle_count == 0)
        {
            return false;
        }

        // Retrieve useful data offset information.
        uint16_t position_offset = vertex_format_.getOffset(gfx::attribute::Position);
        uint16_t vertex_stride = vertex_format_.getStride();

        // Insert all edges into the edge tree
        uint8_t* src_vertices_ptr = preparation_data_.vertex_data.data() + position_offset;
        for(uint32_t i = 0; i < preparation_data_.triangle_count; ++i)
        {
            adjacent_edge_key edge;

            // Degenerate triangles cannot participate.
            const triangle& tri = preparation_data_.triangle_data[i];
            if(tri.flags & triangle_flags::degenerate)
                continue;

            // Retrieve positions of each referenced vertex.
            const math::vec3* v1 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (tri.indices[0] * vertex_stride));
            const math::vec3* v2 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (tri.indices[1] * vertex_stride));
            const math::vec3* v3 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (tri.indices[2] * vertex_stride));

            // edge 1
            edge.vertex1 = v1;
            edge.vertex2 = v2;
            edge_tree[edge] = i;

            // edge 2
            edge.vertex1 = v2;
            edge.vertex2 = v3;
            edge_tree[edge] = i;

            // edge 3
            edge.vertex1 = v3;
            edge.vertex2 = v1;
            edge_tree[edge] = i;

        } // Next Face

        // Size the output array.
        adjacency.resize(preparation_data_.triangle_count * 3, 0xFFFFFFFF);

        // Now, find any adjacent edges for each triangle edge
        for(uint32_t i = 0; i < preparation_data_.triangle_count; ++i)
        {
            adjacent_edge_key edge;

            // Degenerate triangles cannot participate.
            const triangle& tri = preparation_data_.triangle_data[i];
            if(tri.flags & triangle_flags::degenerate)
            {
                continue;
            }

            // Retrieve positions of each referenced vertex.
            const math::vec3* v1 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (tri.indices[0] * vertex_stride));
            const math::vec3* v2 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (tri.indices[1] * vertex_stride));
            const math::vec3* v3 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (tri.indices[2] * vertex_stride));

            // Note: Notice below that the order of the edge vertices
            //       is swapped. This is because we want to find the
            //       matching ADJACENT edge, rather than simply finding
            //       the same edge that we're currently processing.

            // edge 1
            edge.vertex2 = v1;
            edge.vertex1 = v2;

            // Find the matching adjacent edge
            it_edge = edge_tree.find(edge);
            if(it_edge != edge_tree.end())
            {
                adjacency[(i * 3)] = it_edge->second;
            }

            // edge 2
            edge.vertex2 = v2;
            edge.vertex1 = v3;

            // Find the matching adjacent edge
            it_edge = edge_tree.find(edge);
            if(it_edge != edge_tree.end())
            {
                adjacency[(i * 3) + 1] = it_edge->second;
            }

            // edge 3
            edge.vertex2 = v3;
            edge.vertex1 = v1;

            // Find the matching adjacent edge
            it_edge = edge_tree.find(edge);
            if(it_edge != edge_tree.end())
            {
                adjacency[(i * 3) + 2] = it_edge->second;
            }

        } // Next Face

    } // End if not prepared
    else
    {
        // Validate requirements
        if(face_count_ == 0)
        {
            return false;
        }

        // Retrieve useful data offset information.
        uint16_t position_offset = vertex_format_.getOffset(gfx::attribute::Position);
        uint16_t vertex_stride = vertex_format_.getStride();

        // Insert all edges into the edge tree
        uint8_t* src_vertices_ptr = system_vb_ + position_offset;
        uint32_t* src_indices_ptr = system_ib_;
        for(uint32_t i = 0; i < face_count_; ++i, src_indices_ptr += 3)
        {
            adjacent_edge_key edge;

            // Retrieve positions of each referenced vertex.
            const auto* v1 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (src_indices_ptr[0] * vertex_stride));
            const auto* v2 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (src_indices_ptr[1] * vertex_stride));
            const auto* v3 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (src_indices_ptr[2] * vertex_stride));

            // edge 1
            edge.vertex1 = v1;
            edge.vertex2 = v2;
            edge_tree[edge] = i;

            // edge 2
            edge.vertex1 = v2;
            edge.vertex2 = v3;
            edge_tree[edge] = i;

            // edge 3
            edge.vertex1 = v3;
            edge.vertex2 = v1;
            edge_tree[edge] = i;

        } // Next Face

        // Size the output array.
        adjacency.resize(face_count_ * 3, 0xFFFFFFFF);

        // Now, find any adjacent edges for each triangle edge
        src_indices_ptr = system_ib_;
        for(uint32_t i = 0; i < face_count_; ++i, src_indices_ptr += 3)
        {
            adjacent_edge_key edge;

            // Retrieve positions of each referenced vertex.
            const math::vec3* v1 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (src_indices_ptr[0] * vertex_stride));
            const math::vec3* v2 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (src_indices_ptr[1] * vertex_stride));
            const math::vec3* v3 =
                reinterpret_cast<const math::vec3*>(src_vertices_ptr + (src_indices_ptr[2] * vertex_stride));

            // Note: Notice below that the order of the edge vertices
            //       is swapped. This is because we want to find the
            //       matching ADJACENT edge, rather than simply finding
            //       the same edge that we're currently processing.

            // edge 1
            edge.vertex2 = v1;
            edge.vertex1 = v2;

            // Find the matching adjacent edge
            it_edge = edge_tree.find(edge);
            if(it_edge != edge_tree.end())
            {
                adjacency[(i * 3)] = it_edge->second;
            }

            // edge 2
            edge.vertex2 = v2;
            edge.vertex1 = v3;

            // Find the matching adjacent edge
            it_edge = edge_tree.find(edge);
            if(it_edge != edge_tree.end())
            {
                adjacency[(i * 3) + 1] = it_edge->second;
            }

            // edge 3
            edge.vertex2 = v3;
            edge.vertex1 = v1;

            // Find the matching adjacent edge
            it_edge = edge_tree.find(edge);
            if(it_edge != edge_tree.end())
            {
                adjacency[(i * 3) + 2] = it_edge->second;
            }

        } // Next Face

    } // End if prepared

    // Success!
    return true;
}

auto mesh::get_face_count() const -> uint32_t
{
    if(prepare_status_ == mesh_status::prepared)
    {
        return face_count_;
    }
    if(prepare_status_ == mesh_status::preparing)
    {
        return static_cast<uint32_t>(preparation_data_.triangle_data.size());
    }

    return 0;
}

auto mesh::get_vertex_count() const -> uint32_t
{
    if(prepare_status_ == mesh_status::prepared)
    {
        return vertex_count_;
    }
    if(prepare_status_ == mesh_status::preparing)
    {
        return preparation_data_.vertex_count;
    }

    return 0;
}

auto mesh::get_system_vb() -> uint8_t*
{
    return system_vb_;
}

auto mesh::get_system_ib() -> uint32_t*
{
    return system_ib_;
}

auto mesh::get_vertex_format() const -> const gfx::vertex_layout&
{
    return vertex_format_;
}

auto mesh::get_hardware_vb() const -> std::shared_ptr<gfx::vertex_buffer>
{
    return std::static_pointer_cast<gfx::vertex_buffer>(hardware_vb_);
}

auto mesh::get_hardware_ib(uint32_t lod_index) const -> std::shared_ptr<gfx::index_buffer>
{
    if(lod_index == 0 || lods_.empty())
    {
        return std::static_pointer_cast<gfx::index_buffer>(hardware_ib_);
    }

    if(lod_index <= lods_.size())
    {
        return std::static_pointer_cast<gfx::index_buffer>(lods_[lod_index - 1].hardware_ib_);
    }

    return std::static_pointer_cast<gfx::index_buffer>(hardware_ib_);
}

auto mesh::get_skin_bind_data() const -> const skin_bind_data&
{
    return skin_bind_data_;
}

auto mesh::get_bone_palettes() const -> const mesh::bone_palette_array_t&
{
    return bone_palettes_;
}

auto mesh::get_armature() const -> const std::unique_ptr<mesh::armature_node>&
{
    return root_;
}

namespace
{
void accumulate_submesh_node_transforms(const std::unique_ptr<mesh::armature_node>& node,
                                        const math::transform& parent_global,
                                        std::vector<math::transform>& out)
{
    if(!node)
    {
        return;
    }
    const math::transform global = parent_global * node->local_transform;
    for(auto submesh_index : node->submeshes)
    {
        if(submesh_index < out.size())
        {
            out[submesh_index] = global;
        }
    }
    for(const auto& child : node->children)
    {
        accumulate_submesh_node_transforms(child, global, out);
    }
}
} // namespace

auto mesh::get_submesh_node_transforms(uint32_t lod_index) const -> std::vector<math::transform>
{
    const size_t submesh_count = get_submeshes_count(lod_index);
    std::vector<math::transform> transforms(submesh_count);
    accumulate_submesh_node_transforms(root_, math::transform{}, transforms);
    return transforms;
}

auto mesh::calculate_screen_rect_precise(const math::transform& world, const camera& cam) const -> irect32_t
{
    auto bounds = math::bbox::mul(get_bounds(), world);
    math::vec3 cen = bounds.get_center();
    math::vec3 ext = bounds.get_extents();
    
    const auto view_proj = cam.get_view_projection();
    const auto& viewport_size = cam.get_viewport_size();
    const auto& viewport_pos = cam.get_viewport_pos();
    const float near_plane_epsilon = 0.001f;
    
    std::array<math::vec3, 8> corners = {{
        math::vec3(cen.x - ext.x, cen.y - ext.y, cen.z - ext.z),
        math::vec3(cen.x + ext.x, cen.y - ext.y, cen.z - ext.z),
        math::vec3(cen.x - ext.x, cen.y - ext.y, cen.z + ext.z),
        math::vec3(cen.x + ext.x, cen.y - ext.y, cen.z + ext.z),
        math::vec3(cen.x - ext.x, cen.y + ext.y, cen.z - ext.z),
        math::vec3(cen.x + ext.x, cen.y + ext.y, cen.z - ext.z),
        math::vec3(cen.x - ext.x, cen.y + ext.y, cen.z + ext.z),
        math::vec3(cen.x + ext.x, cen.y + ext.y, cen.z + ext.z),
    }};
    
    // Bounding box edges (pairs of corner indices)
    constexpr std::array<std::pair<int, int>, 12> edges = {{
        {0, 1}, {2, 3}, {4, 5}, {6, 7},  // X-aligned edges
        {0, 2}, {1, 3}, {4, 6}, {5, 7},  // Z-aligned edges
        {0, 4}, {1, 5}, {2, 6}, {3, 7}   // Y-aligned edges
    }};
    
    math::vec2 min = math::vec2(std::numeric_limits<float>::max());
    math::vec2 max = math::vec2(std::numeric_limits<float>::lowest());
    bool has_valid_point = false;
    
    // Project corners and clip edges
    std::array<math::vec4, 8> clip_coords;
    std::array<bool, 8> is_visible;
    
    for(int i = 0; i < 8; ++i)
    {
        clip_coords[i] = view_proj * math::vec4{corners[i].x, corners[i].y, corners[i].z, 1.0f};
        is_visible[i] = clip_coords[i].w > near_plane_epsilon;
        
        if(is_visible[i])
        {
            const float recip_w = 1.0f / clip_coords[i].w;
            const float ndc_x = clip_coords[i].x * recip_w;
            const float ndc_y = clip_coords[i].y * recip_w;
            
            math::vec2 screen_point;
            screen_point.x = ((ndc_x * 0.5f) + 0.5f) * float(viewport_size.width) + float(viewport_pos.x);
            screen_point.y = ((ndc_y * -0.5f) + 0.5f) * float(viewport_size.height) + float(viewport_pos.y);
            
            min = math::min(min, screen_point);
            max = math::max(max, screen_point);
            has_valid_point = true;
        }
    }
    
    // Clip edges that cross the near plane
    for(const auto& edge : edges)
    {
        const int idx0 = edge.first;
        const int idx1 = edge.second;
        
        const bool v0_visible = is_visible[idx0];
        const bool v1_visible = is_visible[idx1];
        
        if(v0_visible == v1_visible)
        {
            continue;
        }
        
        const math::vec4& clip0 = clip_coords[idx0];
        const math::vec4& clip1 = clip_coords[idx1];
        
        const float w0 = clip0.w;
        const float w1 = clip1.w;
        
        const float t = (near_plane_epsilon - w0) / (w1 - w0);
        
        if(t >= 0.0f && t <= 1.0f)
        {
            const math::vec4 clipped_clip = clip0 + t * (clip1 - clip0);
            
            const float recip_w = 1.0f / clipped_clip.w;
            const float ndc_x = clipped_clip.x * recip_w;
            const float ndc_y = clipped_clip.y * recip_w;
            
            math::vec2 screen_point;
            screen_point.x = ((ndc_x * 0.5f) + 0.5f) * float(viewport_size.width) + float(viewport_pos.x);
            screen_point.y = ((ndc_y * -0.5f) + 0.5f) * float(viewport_size.height) + float(viewport_pos.y);
            
            min = math::min(min, screen_point);
            max = math::max(max, screen_point);
            has_valid_point = true;
        }
    }
    
    if(!has_valid_point)
    {
        min = math::vec2(float(viewport_pos.x), float(viewport_pos.y));
        max = math::vec2(float(viewport_pos.x + viewport_size.width), float(viewport_pos.y + viewport_size.height));
    }
    
    return irect32_t(irect32_t::value_type(min.x),
                     irect32_t::value_type(min.y),
                     irect32_t::value_type(max.x),
                     irect32_t::value_type(max.y));
}

auto mesh::calculate_screen_rect(const math::transform& world, const camera& cam) const -> irect32_t
{
    auto bounds = math::bbox::mul(get_bounds(), world);
    math::vec3 cen = bounds.get_center();
    math::vec3 ext = bounds.get_extents();
    
    const auto view_proj = cam.get_view_projection();
    const auto& viewport_size = cam.get_viewport_size();
    const auto& viewport_pos = cam.get_viewport_pos();
    
    std::array<math::vec3, 8> corners = {{
        math::vec3(cen.x - ext.x, cen.y - ext.y, cen.z - ext.z),
        math::vec3(cen.x + ext.x, cen.y - ext.y, cen.z - ext.z),
        math::vec3(cen.x - ext.x, cen.y - ext.y, cen.z + ext.z),
        math::vec3(cen.x + ext.x, cen.y - ext.y, cen.z + ext.z),
        math::vec3(cen.x - ext.x, cen.y + ext.y, cen.z - ext.z),
        math::vec3(cen.x + ext.x, cen.y + ext.y, cen.z - ext.z),
        math::vec3(cen.x - ext.x, cen.y + ext.y, cen.z + ext.z),
        math::vec3(cen.x + ext.x, cen.y + ext.y, cen.z + ext.z),
    }};
    
    math::vec2 min = math::vec2(std::numeric_limits<float>::max());
    math::vec2 max = math::vec2(std::numeric_limits<float>::lowest());
    int valid_count = 0;
    int behind_count = 0;
    
    for(const auto& corner : corners)
    {
        math::vec4 clip = view_proj * math::vec4{corner.x, corner.y, corner.z, 1.0f};
        
        // Check if point is behind or very close to the camera (near plane)
        if(clip.w <= 0.001f)
        {
            behind_count++;
            continue;
        }
        
        // Project to normalized device coordinates
        const float recip_w = 1.0f / clip.w;
        const float ndc_x = clip.x * recip_w;
        const float ndc_y = clip.y * recip_w;
        
        // Transform to screen space
        math::vec2 screen_point;
        screen_point.x = ((ndc_x * 0.5f) + 0.5f) * float(viewport_size.width) + float(viewport_pos.x);
        screen_point.y = ((ndc_y * -0.5f) + 0.5f) * float(viewport_size.height) + float(viewport_pos.y);
        
        min = math::min(min, screen_point);
        max = math::max(max, screen_point);
        valid_count++;
    }
    
    // If some points are behind camera, the bounds cross the near plane
    // This means the object extends beyond the viewport edges - clamp to full screen
    if(behind_count > 0 && valid_count > 0)
    {
        min.x = float(viewport_pos.x);
        min.y = float(viewport_pos.y);
        max.x = float(viewport_pos.x + viewport_size.width);
        max.y = float(viewport_pos.y + viewport_size.height);
    }
    else if(valid_count == 0)
    {
        // All points behind camera - object encompasses the entire screen
        min = math::vec2(float(viewport_pos.x), float(viewport_pos.y));
        max = math::vec2(float(viewport_pos.x + viewport_size.width), float(viewport_pos.y + viewport_size.height));
    }
    
    return irect32_t(irect32_t::value_type(min.x),
                     irect32_t::value_type(min.y),
                     irect32_t::value_type(max.x),
                     irect32_t::value_type(max.y));
}

auto mesh::get_submeshes(uint32_t lod_index) const -> const submesh_array_t&
{
    if(lod_index == 0)
    {
        return mesh_submeshes_;
    }

    if(lod_index > 0 && lod_index <= lods_.size())
    {
        return lods_[lod_index - 1].submeshes_;
    }

    // Invalid LOD index, return base
    return mesh_submeshes_;
}

auto mesh::get_submeshes_count(uint32_t lod_index) const -> size_t
{
    if(lod_index == 0)
    {
        return mesh_submeshes_.size();
    }

    if(lod_index > 0 && lod_index <= lods_.size())
    {
        return lods_[lod_index - 1].submeshes_.size();
    }

    // Invalid LOD index, return base
    return mesh_submeshes_.size();
}

auto mesh::get_submesh(uint32_t submesh_index, uint32_t lod_index) const -> const submesh*
{
    const auto& submeshes = get_submeshes(lod_index);
    if(submesh_index < submeshes.size())
    {
        return submeshes[submesh_index];
    }
    return nullptr;
}

auto mesh::get_submesh_index(const submesh* s, uint32_t lod_index) const -> int
{
    const auto& submeshes = get_submeshes(lod_index);
    int index = -1;
    for(const auto& submesh : submeshes)
    {
        index++;
        if(submesh == s)
        {
            return index;
        }
    }

    return -1;
}

auto mesh::find_submesh_index_by_stable_id(uint32_t stable_id, uint32_t lod_index) const -> int
{
    if(stable_id == 0)
    {
        return -1;
    }

    const auto& submeshes = get_submeshes(lod_index);
    for(size_t i = 0; i < submeshes.size(); ++i)
    {
        if(submeshes[i] != nullptr && submeshes[i]->stable_id == stable_id)
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}


auto mesh::get_lod_count() const -> uint32_t
{
    return lod_count_;
}

auto mesh::get_lod_submeshes(uint32_t lod_index) const -> const submesh_array_t*
{
    if(lod_index == 0)
    {
        return &mesh_submeshes_;
    }

    if(lod_index > 0 && lod_index <= lods_.size())
    {
        return &lods_[lod_index - 1].submeshes_;
    }

    return nullptr;
}

auto mesh::get_lod_face_count(uint32_t lod_index) const -> uint32_t
{
    if(lod_index == 0)
    {
        return face_count_;
    }

    if(lod_index > 0 && lod_index <= lods_.size())
    {
        return lods_[lod_index - 1].face_count_;
    }

    return 0;
}

auto mesh::get_lod_index_data(uint32_t lod_index, std::vector<uint32_t>& out_indices, float& out_error) const -> bool
{
    if(lod_index == 0)
    {
        // Base LOD - return original index buffer
        if(!system_ib_ || face_count_ == 0)
        {
            return false;
        }
        out_indices.resize(face_count_ * 3);
        std::memcpy(out_indices.data(), system_ib_, face_count_ * 3 * sizeof(uint32_t));
        out_error = 0.0f;
        return true;
    }

    if(lod_index > 0 && lod_index <= lods_.size())
    {
        const auto& lod = lods_[lod_index - 1];
        if(!lod.system_ib_ || lod.face_count_ == 0)
        {
            return false;
        }
        out_indices.resize(lod.face_count_ * 3);
        std::memcpy(out_indices.data(), lod.system_ib_, lod.face_count_ * 3 * sizeof(uint32_t));
        out_error = lod.simplification_error_;
        return true;
    }

    return false;
}

auto mesh::get_max_lod_count() -> uint32_t
{
    return 1 + get_max_generated_lod_count();
}

auto mesh::get_max_generated_lod_count() -> uint32_t
{
    return 5;
}

auto mesh::generate_default_lod_configs(const load_data& data, float target_error) -> std::vector<std::pair<size_t, float>>
{
    std::vector<std::pair<size_t, float>> lod_configs;
    
    auto max_lod_count = get_max_generated_lod_count();
    for(uint32_t i = 1; i <= max_lod_count; ++i)
    {
        // Only generate LODs for meshes with enough triangles
        size_t base_tri_count = data.triangle_count;
        lod_configs.push_back({base_tri_count / (1 << i), target_error * i});
    }
    
    return lod_configs;
}

auto mesh::apply_skin_to_load_data(load_data& data) -> bool
{
    if(!data.skin_data.has_bones())
    {
        return true; // No skinning needed
    }

    // Build vertex table with bone influences
    skin_bind_data::vertex_data_array_t vertex_table;
    data.skin_data.build_vertex_table(data.vertex_count, {}, vertex_table);

    uint32_t palette_size = gfx::get_max_blend_transforms();
    std::vector<bone_palette> bone_palettes;
    bone_palettes.reserve(data.submeshes.size());

    // Iterate over each submesh to generate palettes and duplicate vertices
    for(size_t palette_id = 0; palette_id < data.submeshes.size(); ++palette_id)
    {
        auto& submesh = data.submeshes[palette_id];
        
        std::vector<bool> used_bones(gfx::get_max_blend_transforms(), false);
        std::vector<uint32_t> faces;
        faces.reserve(submesh.face_count);

        // Collect all unique bone indices influencing this submesh
        for(uint32_t i = submesh.face_start; i < submesh.face_start + submesh.face_count; ++i)
        {
            faces.push_back(i);
            for(uint32_t vertex_index : data.triangle_data[i].indices)
            {
                const auto& vdata = vertex_table[vertex_index];
                for(const auto& influence : vdata.influences)
                {
                    used_bones[static_cast<uint32_t>(influence)] = true;
                }
            }
        }

        // Create bone palette for this submesh
        bone_palette new_palette(palette_size);
        new_palette.set_data_group(submesh.data_group_id);
        new_palette.assign_bones(used_bones, faces);
        bone_palettes.push_back(new_palette);

        // Assign palette ID to each vertex and duplicate shared vertices
        auto face_start = submesh.face_start;
        auto face_end = submesh.face_start + submesh.face_count;
        for(uint32_t i = face_start; i < face_end; ++i)
        {
            for(uint32_t k = 0; k < 3; ++k)
            {
                uint32_t vertex_index = data.triangle_data[i].indices[k];
                auto& vdata = vertex_table[vertex_index];

                if(vdata.palette == -1)
                {
                    vdata.palette = static_cast<int32_t>(palette_id);
                    if(submesh.vertex_start == -1 || vertex_index < submesh.vertex_start)
                    {
                        submesh.vertex_start = vertex_index;
                    }
                    if(vertex_index >= submesh.vertex_start + submesh.vertex_count)
                    {
                        submesh.vertex_count = (vertex_index - submesh.vertex_start) + 1;
                    }
                }
                else if(vdata.palette != static_cast<int32_t>(palette_id))
                {
                    // Vertex is shared between submeshes, need to duplicate it
                    uint32_t new_index = static_cast<uint32_t>(vertex_table.size());

                    skin_bind_data::vertex_data new_vertex(vdata);
                    new_vertex.original_vertex = vertex_index;
                    new_vertex.palette = static_cast<int32_t>(palette_id);
                    vertex_table.push_back(new_vertex);

                    if(submesh.vertex_start == -1 || new_index < submesh.vertex_start)
                    {
                        submesh.vertex_start = new_index;
                    }
                    if(new_index >= submesh.vertex_start + submesh.vertex_count)
                    {
                        submesh.vertex_count = (new_index - submesh.vertex_start) + 1;
                    }

                    // Update triangle index to point to new vertex
                    data.triangle_data[i].indices[k] = new_index;
                }
            }
        }
    }

    // Adjust vertex format to include blend weights and indices
    gfx::vertex_layout new_format(data.vertex_format);
    gfx::vertex_layout original_format = data.vertex_format;
    bool has_weights = new_format.has(gfx::attribute::Weight);
    bool has_indices = new_format.has(gfx::attribute::Indices);
    
    if(!has_weights || !has_indices)
    {
        new_format.m_hash = 0;
        if(!has_weights)
        {
            new_format.add(gfx::attribute::Weight, 4, gfx::attribute_type::Float);
        }
        if(!has_indices)
        {
            new_format.add(gfx::attribute::Indices, 4, gfx::attribute_type::Float, false, true);
        }
        new_format.end();
        data.vertex_format = new_format;
    }

    uint16_t vertex_stride = data.vertex_format.getStride();
    uint32_t original_vertex_count = data.vertex_count;

    // Resize vertex buffer to accommodate duplicated vertices
    if(data.vertex_format.m_hash != original_format.m_hash)
    {
        // Format changed, need to convert
        byte_array_t original_buffer(data.vertex_data);
        data.vertex_data.clear();
        data.vertex_data.resize(vertex_table.size() * vertex_stride);

        gfx::vertex_convert(data.vertex_format,
                            data.vertex_data.data(),
                            original_format,
                            original_buffer.data(),
                            original_vertex_count);
    }
    else
    {
        // No conversion, just resize
        data.vertex_data.resize(vertex_table.size() * vertex_stride);
    }

    // Update vertex data with bone indices and weights, and duplicate vertices
    uint8_t* src_vertices_ptr = data.vertex_data.data();
    for(size_t i = 0; i < vertex_table.size(); ++i)
    {
        auto& vdata = vertex_table[i];
        int32_t palette_id = vdata.palette;

        if(palette_id < 0)
        {
            continue;
        }

        const auto& palette = bone_palettes[static_cast<size_t>(palette_id)];

        // If this is a duplicated vertex, copy data from original
        if(i >= original_vertex_count)
        {
            std::memcpy(src_vertices_ptr + (i * vertex_stride),
                        src_vertices_ptr + (vdata.original_vertex * vertex_stride),
                        vertex_stride);
        }

        uint32_t max_bones = std::min<uint32_t>(4, uint32_t(vdata.influences.size()));
        if(max_bones > 0)
        {
            math::vec4 blend_weights(0.0f, 0.0f, 0.0f, 0.0f);
            math::vec4 blend_indices(0.0f, 0.0f, 0.0f, 0.0f);

            for(uint32_t j = 0; j < max_bones; ++j)
            {
                uint32_t palette_bone_index =
                    palette.translate_bone_to_palette(static_cast<uint32_t>(vdata.influences[j]));

                blend_indices[static_cast<math::vec4::length_type>(j)] = static_cast<float>(palette_bone_index);
                blend_weights[static_cast<math::vec4::length_type>(j)] = vdata.weights[j];
            }

            gfx::vertex_pack(math::value_ptr(blend_weights),
                             false,
                             gfx::attribute::Weight,
                             data.vertex_format,
                             src_vertices_ptr,
                             uint32_t(i));

            gfx::vertex_pack(math::value_ptr(blend_indices),
                             false,
                             gfx::attribute::Indices,
                             data.vertex_format,
                             src_vertices_ptr,
                             uint32_t(i));
        }
    }

    // Update vertex count
    data.vertex_count = static_cast<uint32_t>(vertex_table.size());

    // Mark topology as baked and store palette bones so runtime can restore palettes without running bind_skin.
    data.skin_is_prepared = true;
    data.bone_palette_bones.clear();
    data.bone_palette_bones.reserve(bone_palettes.size());
    for(const auto& palette : bone_palettes)
    {
        data.bone_palette_bones.push_back(palette.get_bones());
    }
    return true;
}

auto mesh::generate_lods_for_load_data(load_data& data, const std::vector<std::pair<size_t, float>>& lod_configs) -> bool
{
    if(data.vertex_data.empty() || data.triangle_count == 0 || data.vertex_count == 0)
    {
        APPLOG_ERROR("Cannot generate LODs for empty mesh data\n");
        return false;
    }

    if(!data.vertex_format.has(gfx::attribute::Position))
    {
        APPLOG_ERROR("Mesh must have position data to generate LODs\n");
        return false;
    }

    if(data.submeshes.empty())
    {
        APPLOG_ERROR("Cannot generate LODs for mesh with no submeshes\n");
        return false;
    }

    // Get position offset and stride for meshoptimizer
    uint16_t position_offset = data.vertex_format.getOffset(gfx::attribute::Position);
    uint16_t vertex_stride = data.vertex_format.getStride();
    const uint8_t* vertex_data_ptr = data.vertex_data.data();
    
    // Point directly to position data in vertex buffer (cast to float*)
    const float* vertex_positions = reinterpret_cast<const float*>(vertex_data_ptr + position_offset);

    // Build index buffer from triangle data
    std::vector<uint32_t> base_indices(data.triangle_count * 3);
    for(uint32_t i = 0; i < data.triangle_count; ++i)
    {
        base_indices[i * 3 + 0] = data.triangle_data[i].indices[0];
        base_indices[i * 3 + 1] = data.triangle_data[i].indices[1];
        base_indices[i * 3 + 2] = data.triangle_data[i].indices[2];
    }

    // Clear existing LODs
    data.lods.clear();

    // Track previous LOD face count to detect when simplification stops making progress
    uint32_t previous_face_count = data.triangle_count;
    constexpr float MIN_FACE_COUNT_DIFFERENCE_RATIO = 0.02f; //minimum difference

    // Prepare attribute packing data (shared across all submeshes and LODs)
    bool use_attribute_simplify = false;
    std::vector<float> packed_attributes;
    const float* attribute_ptr = nullptr;
    size_t attribute_stride = 0;
    std::vector<float> attribute_weights;
    uint32_t attribute_component_count = 0;
    uint32_t total_components = 0;
    
    // Check for available attributes and pack them into a single interleaved buffer
    bool has_normal = data.vertex_format.has(gfx::attribute::Normal);
    bool has_texcoord = data.vertex_format.has(gfx::attribute::TexCoord0);
    bool has_tangent = data.vertex_format.has(gfx::attribute::Tangent);
    
    if(has_normal || has_texcoord || has_tangent)
    {
        // Calculate total components needed: normal(3) + uv(2) + tangent(3) + bitangent(3) + weight(4) + indices(4) = 19 max
        if(has_normal) total_components += 3;
        if(has_texcoord) total_components += 2;
        if(has_tangent) total_components += 3;
        
        packed_attributes.resize(data.vertex_count * total_components);
        attribute_weights.resize(total_components);
        
        // Setup attribute weights
        uint32_t weight_offset = 0;
        if(has_normal)
        {
            attribute_weights[weight_offset + 0] = 1.5f;
            attribute_weights[weight_offset + 1] = 1.5f;
            attribute_weights[weight_offset + 2] = 1.5f;
            weight_offset += 3;
        }
        if(has_texcoord)
        {
            attribute_weights[weight_offset + 0] = 1.0f;
            attribute_weights[weight_offset + 1] = 1.0f;
            weight_offset += 2;
        }
        if(has_tangent)
        {
            attribute_weights[weight_offset + 0] = 0.75f;
            attribute_weights[weight_offset + 1] = 0.75f;
            attribute_weights[weight_offset + 2] = 0.75f;
            weight_offset += 3;
        }


        // Extract and pack all attributes in a single pass over vertices
        for(uint32_t i = 0; i < data.vertex_count; ++i)
        {
            float* dst = &packed_attributes[i * total_components];
            uint32_t component_offset = 0;
            
            if(has_normal)
            {
                float attr[4];
                gfx::vertex_unpack(attr, gfx::attribute::Normal, data.vertex_format, vertex_data_ptr, i);
                dst[component_offset + 0] = attr[0];
                dst[component_offset + 1] = attr[1];
                dst[component_offset + 2] = attr[2];
                component_offset += 3;
            }
            
            if(has_texcoord)
            {
                float attr[4];
                gfx::vertex_unpack(attr, gfx::attribute::TexCoord0, data.vertex_format, vertex_data_ptr, i);
                dst[component_offset + 0] = attr[0];
                dst[component_offset + 1] = attr[1];
                component_offset += 2;
            }
            
            if(has_tangent)
            {
                float attr[4];
                gfx::vertex_unpack(attr, gfx::attribute::Tangent, data.vertex_format, vertex_data_ptr, i);
                dst[component_offset + 0] = attr[0];
                dst[component_offset + 1] = attr[1];
                dst[component_offset + 2] = attr[2];
                component_offset += 3;
            }

            
        }
        
        if(total_components > 0)
        {
            attribute_ptr = packed_attributes.data();
            attribute_stride = total_components * sizeof(float);
            attribute_component_count = total_components;
            use_attribute_simplify = true;
        }
    }
    

    // Computes a tight bounding box for a simplified index set so per-LOD submeshes carry
    // accurate bounds instead of inheriting the (potentially much larger) base LOD box.
    auto compute_bbox_from_indices = [&](const uint32_t* indices, size_t index_count) -> math::bbox
    {
        math::bbox box{};
        for(size_t i = 0; i < index_count; ++i)
        {
            const auto* pos =
                reinterpret_cast<const float*>(vertex_data_ptr + indices[i] * vertex_stride + position_offset);
            box.add_point(math::vec3(pos[0], pos[1], pos[2]));
        }
        return box;
    };

    // Generate each LOD level
    for(const auto& config : lod_configs)
    {
        size_t target_tri_count = config.first;
        float target_error = config.second;

        // Clamp target triangle count
        target_tri_count = std::min(target_tri_count, static_cast<size_t>(data.triangle_count));
        if(target_tri_count < 1)
        {
            continue;
        }

        // Create LOD load data for this level
        lod_load_data lod_data;
        lod_data.face_count = 0;
        lod_data.simplification_error = 0.0f;
        
        uint32_t current_face_start = 0;

            
        unsigned int options = meshopt_SimplifyLockBorder;

        if(data.submeshes.size() > 1)
        {
            options |= meshopt_SimplifySparse;
        }

        
        // Process each submesh independently
        for(const auto& base_submesh : data.submeshes)
        {
            // Skip empty submeshes
            if(base_submesh.face_count == 0)
            {
                submesh lod_submesh = base_submesh;
                lod_submesh.face_start = static_cast<int32_t>(current_face_start);
                lod_data.submeshes.push_back(lod_submesh);
                continue;
            }
            
            // Calculate target triangle count for this submesh based on ratio
            float submesh_ratio = static_cast<float>(base_submesh.face_count) / static_cast<float>(data.triangle_count);
            size_t submesh_target_tri_count = static_cast<size_t>(std::max(1.0f, static_cast<float>(target_tri_count) * submesh_ratio));
            submesh_target_tri_count = std::min(submesh_target_tri_count, static_cast<size_t>(base_submesh.face_count));
            
            // Extract indices for this submesh
            std::vector<uint32_t> submesh_indices(base_submesh.face_count * 3);
            for(uint32_t i = 0; i < base_submesh.face_count; ++i)
            {
                uint32_t tri_idx = base_submesh.face_start + i;
                submesh_indices[i * 3 + 0] = base_indices[tri_idx * 3 + 0];
                submesh_indices[i * 3 + 1] = base_indices[tri_idx * 3 + 1];
                submesh_indices[i * 3 + 2] = base_indices[tri_idx * 3 + 2];
            }
            
            // Convert triangle count to index count
            size_t submesh_target_index_count = submesh_target_tri_count * 3;
            
            // Allocate destination index buffer
            std::vector<uint32_t> submesh_lod_indices(base_submesh.face_count * 3);
            
            // Simplify this submesh
            float submesh_result_error = 0.0f;
            size_t submesh_lod_index_count = 0;
        
            
            // Use attribute-preserving simplification if available, otherwise fall back to position-only
            if(use_attribute_simplify)
            {
                submesh_lod_index_count = meshopt_simplifyWithAttributes(
                    submesh_lod_indices.data(),
                    submesh_indices.data(),
                    base_submesh.face_count * 3,
                    vertex_positions,
                    data.vertex_count,
                    vertex_stride,
                    attribute_ptr,
                    attribute_stride,
                    attribute_weights.data(),
                    attribute_component_count,
                    nullptr,
                    submesh_target_index_count,
                    target_error,
                    options,
                    &submesh_result_error);
            }
            else
            {
                submesh_lod_index_count = meshopt_simplify(
                    submesh_lod_indices.data(),
                    submesh_indices.data(),
                    base_submesh.face_count * 3,
                    vertex_positions,
                    data.vertex_count,
                    vertex_stride,
                    submesh_target_index_count,
                    target_error,
                    options,
                    &submesh_result_error);
            }
            
            // Convert index count back to triangle count
            size_t submesh_lod_tri_count = submesh_lod_index_count / 3;
            if(submesh_lod_tri_count == 0)
            {
                APPLOG_WARNING("Failed to generate LOD for submesh with target {} triangles (error: {})\n", submesh_target_tri_count, target_error);
                // Use original submesh as fallback
                submesh lod_submesh = base_submesh;
                lod_submesh.face_start = static_cast<int32_t>(current_face_start);
                lod_data.submeshes.push_back(lod_submesh);
                
                // Append original indices
                for(uint32_t i = 0; i < base_submesh.face_count * 3; ++i)
                {
                    lod_data.index_data.push_back(submesh_indices[i]);
                }
                current_face_start += base_submesh.face_count;
                lod_data.face_count += base_submesh.face_count;
                continue;
            }
            
            // Create submesh descriptor for this LOD
            submesh lod_submesh = base_submesh;
            lod_submesh.face_count = static_cast<uint32_t>(submesh_lod_tri_count);
            lod_submesh.face_start = static_cast<int32_t>(current_face_start);
            const math::bbox lod_bbox = compute_bbox_from_indices(submesh_lod_indices.data(), submesh_lod_index_count);
            if(lod_bbox.is_populated())
            {
                lod_submesh.bbox = lod_bbox;
            }
            lod_data.submeshes.push_back(lod_submesh);
            
            // Append simplified indices to LOD index buffer
            for(size_t i = 0; i < submesh_lod_index_count; ++i)
            {
                lod_data.index_data.push_back(submesh_lod_indices[i]);
            }
            
            // Update counters
            current_face_start += static_cast<uint32_t>(submesh_lod_tri_count);
            lod_data.face_count += static_cast<uint32_t>(submesh_lod_tri_count);
            lod_data.simplification_error = std::max(lod_data.simplification_error, submesh_result_error);
        }
        
        // Check if LOD generation was successful
        if(lod_data.face_count == 0)
        {
            APPLOG_WARNING("Failed to generate LOD with target {} triangles (error: {})\n", target_tri_count, target_error);
            break;
        }
        
        if(lod_data.face_count >= data.triangle_count)
        {
            APPLOG_WARNING("Could not simplify mesh. Not enough triangles to simplify.\n");
            break;
        }
        
        // Check if this LOD has significant difference from previous LOD
        if(previous_face_count > 0)
        {
            float face_count_difference = static_cast<float>(previous_face_count - lod_data.face_count);
            float difference_ratio = face_count_difference / static_cast<float>(previous_face_count);
            
            if(difference_ratio < MIN_FACE_COUNT_DIFFERENCE_RATIO)
            {
                APPLOG_INFO("Stopping LOD generation: new LOD face count ({}) is not significantly different from previous ({}, {:.2f}% difference)\n",
                           lod_data.face_count, previous_face_count, difference_ratio * 100.0f);
                break;
            }
        }
        
        data.lods.push_back(std::move(lod_data));
        previous_face_count = lod_data.face_count;
    }

    APPLOG_INFO("Generated {} LOD levels\n", data.lods.size());
    return true;
}

auto mesh::restore_lods_from_load_data(const load_data& data) -> bool
{
    if(data.lods.empty())
    {
        return true; // No LODs to restore, not an error
    }

    // Clear any existing LODs
    for(auto& lod : lods_)
    {
        for(auto* submesh : lod.submeshes_)
        {
            checked_delete(submesh);
        }
        lod.submeshes_.clear();
        checked_array_delete(lod.system_ib_);
        lod.hardware_ib_.reset();
    }
    lods_.clear();
    lod_count_ = 1;

    // Restore each LOD level
    for(const auto& lod_data : data.lods)
    {
        mesh::lod_level lod;
        lod.face_count_ = lod_data.face_count;
        lod.simplification_error_ = lod_data.simplification_error;

        // Copy index buffer
        if(!lod_data.index_data.empty())
        {
            lod.system_ib_ = new uint32_t[lod_data.index_data.size()];
            std::memcpy(lod.system_ib_, lod_data.index_data.data(), lod_data.index_data.size() * sizeof(uint32_t));
        }

        // Copy submeshes and build cached indices maps
        for(size_t i = 0; i < lod_data.submeshes.size(); ++i)
        {
            const auto& submesh_data = lod_data.submeshes[i];
            auto* lod_submesh = new submesh(submesh_data);
            lod.submeshes_.push_back(lod_submesh);

            // Cache submesh indices by data group
            if(lod_submesh->skinned)
            {
                lod.skinned_submesh_indices_[lod_submesh->data_group_id].emplace_back(i);
            }
            else
            {
                lod.non_skinned_submesh_indices_[lod_submesh->data_group_id].emplace_back(i);
            }
        }

        // Build hardware buffer for this LOD if needed
        if(hardware_mesh_ && lod.system_ib_ && lod.face_count_ > 0)
        {
            auto buffer_size = static_cast<uint32_t>(lod.face_count_ * 3 * sizeof(uint32_t));
            const gfx::memory_view* mem = gfx::make_ref(lod.system_ib_, buffer_size);
            // Same compute-read flags as base LOD so any LOD can be used as a
            // read-only buffer inside shaders (e.g. vertex pulling for wireframe overlay).
            const uint16_t ib_flags = BGFX_BUFFER_INDEX32
                | BGFX_BUFFER_COMPUTE_READ
                | BGFX_BUFFER_COMPUTE_FORMAT_32X1
                | BGFX_BUFFER_COMPUTE_TYPE_UINT;
            lod.hardware_ib_ = std::make_shared<gfx::index_buffer>(mem, ib_flags);
        }

        lods_.push_back(std::move(lod));
        lod_count_++;
    }

    return true;
}

auto mesh::get_skinned_submeshes_count(uint32_t lod_index) const -> size_t
{
    const auto& submeshes = get_submeshes(lod_index);
    size_t count = 0;
    for(const auto* submesh : submeshes)
    {
        if(submesh && submesh->skinned)
        {
            count++;
        }
    }
    return count;
}

auto mesh::get_skinned_submeshes_indices(uint32_t data_group_id, uint32_t lod_index) const -> const submesh_array_indices_t&
{
    // For base LOD (lod_index == 0), use cached map for performance
    if(lod_index == 0 || lods_.empty())
    {
        auto it = skinned_submesh_indices_.find(data_group_id);
        if(it != skinned_submesh_indices_.end())
        {
            return it->second;
        }
        static const submesh_array_indices_t empty;
        return empty;
    }

    // For LOD levels, use cached map
    if(lod_index > 0 && lod_index <= lods_.size())
    {
        const auto& lod = lods_[lod_index - 1];
        auto it = lod.skinned_submesh_indices_.find(data_group_id);
        if(it != lod.skinned_submesh_indices_.end())
        {
            return it->second;
        }
    }

    static const submesh_array_indices_t empty;
    return empty;
}

auto mesh::get_non_skinned_submeshes_count(uint32_t lod_index) const -> size_t
{
    const auto& submeshes = get_submeshes(lod_index);
    size_t count = 0;
    for(const auto* submesh : submeshes)
    {
        if(submesh && !submesh->skinned)
        {
            count++;
        }
    }
    return count;
}

auto mesh::get_non_skinned_submeshes_indices(uint32_t data_group_id, uint32_t lod_index) const -> const submesh_array_indices_t&
{
    // For base LOD (lod_index == 0), use cached map for performance
    if(lod_index == 0 || lods_.empty())
    {
        auto it = non_skinned_submesh_indices_.find(data_group_id);
        if(it != non_skinned_submesh_indices_.end())
        {
            return it->second;
        }
        static const submesh_array_indices_t empty;
        return empty;
    }

    // For LOD levels, use cached map
    if(lod_index > 0 && lod_index <= lods_.size())
    {
        const auto& lod = lods_[lod_index - 1];
        auto it = lod.non_skinned_submesh_indices_.find(data_group_id);
        if(it != lod.non_skinned_submesh_indices_.end())
        {
            return it->second;
        }
    }

    static const submesh_array_indices_t empty;
    return empty;
}

auto mesh::get_bounds() const -> const math::bbox&
{
    return bbox_;
}

auto mesh::get_status() const -> mesh_status
{
    return prepare_status_;
}

auto mesh::get_data_groups_count() const -> size_t
{
    if(prepare_status_ == mesh_status::prepared)
    {
        return data_groups_.size();
    }
    if(prepare_status_ == mesh_status::preparing)
    {
        uint32_t groups_count = 0;
        for(const auto& sub : preparation_data_.submeshes)
        {
            groups_count = std::max(groups_count, sub.data_group_id + 1);
        }
        return groups_count;
    }

    return 0;
}

auto mesh::get_default_material_uids() const -> const std::vector<hpp::uuid>&
{
    return default_material_uids_;
}


auto mesh::get_imported_materials() const -> std::vector<asset_handle<material>>
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    std::vector<asset_handle<material>> imported_materials;
    for(const auto& uid : default_material_uids_)
    {
        auto mat = am.get_asset<material>(uid);
        imported_materials.push_back(mat);
    }
    return imported_materials;
}


auto operator<(const mesh::adjacent_edge_key& key1, const mesh::adjacent_edge_key& key2) -> bool
{
    // Test vertex positions.
    if(math::epsilonNotEqual(key1.vertex1->x, key2.vertex1->x, math::epsilon<float>()))
    {
        return (key2.vertex1->x < key1.vertex1->x);
    }
    if(math::epsilonNotEqual(key1.vertex1->y, key2.vertex1->y, math::epsilon<float>()))
    {
        return (key2.vertex1->y < key1.vertex1->y);
    }
    if(math::epsilonNotEqual(key1.vertex1->z, key2.vertex1->z, math::epsilon<float>()))
    {
        return (key2.vertex1->z < key1.vertex1->z);
    }

    if(math::epsilonNotEqual(key1.vertex2->x, key2.vertex2->x, math::epsilon<float>()))
    {
        return (key2.vertex2->x < key1.vertex2->x);
    }
    if(math::epsilonNotEqual(key1.vertex2->y, key2.vertex2->y, math::epsilon<float>()))
    {
        return (key2.vertex2->y < key1.vertex2->y);
    }
    if(math::epsilonNotEqual(key1.vertex2->z, key2.vertex2->z, math::epsilon<float>()))
    {
        return (key2.vertex2->z < key1.vertex2->z);
    }

    // Exactly equal
    return false;
}

auto operator<(const mesh::mesh_submesh_key& key1, const mesh::mesh_submesh_key& key2) -> bool
{
    return key1.data_group_id < key2.data_group_id;
}

auto operator<(const mesh::weld_key& key1, const mesh::weld_key& key2) -> bool
{
    auto vertex_compare =
        [](const uint8_t* pVtx1, const uint8_t* pVtx2, const gfx::vertex_layout& layout, float tolerance) -> int
    {
        float diff{};
        int ndifference{};

        for(uint16_t i = 0; i < gfx::attribute::Count; ++i)
        {
            if(!layout.has(static_cast<gfx::attribute>(i)))
            {
                continue; // Skip attributes not present in this layout.
            }

            // Get the offset for this attribute in the vertex data
            uint16_t offset = layout.getOffset(static_cast<gfx::attribute>(i));

            // Retrieve the vertex data pointers
            const uint8_t* p1 = pVtx1 + offset;
            const uint8_t* p2 = pVtx2 + offset;

            // Decode the attribute information
            uint8_t num_components{};
            bgfx::AttribType::Enum type{};
            bool normalized{}, as_int{};
            layout.decode(static_cast<gfx::attribute>(i), num_components, type, normalized, as_int);

            // Compare the attributes based on the type
            switch(type)
            {
                case bgfx::AttribType::Float:
                {
                    for(uint8_t j = 0; j < num_components; ++j)
                    {
                        diff = ((float*)p1)[j] - ((float*)p2)[j];
                        if(fabsf(diff) > tolerance)
                            return (diff < 0) ? -1 : 1;
                    }
                    break;
                }

                case bgfx::AttribType::Uint8:
                case bgfx::AttribType::Int16:
                {
                    if(as_int)
                    {
                        ndifference = memcmp(p1, p2, num_components * (type == bgfx::AttribType::Uint8 ? 1 : 2));
                        if(ndifference != 0)
                        {
                            return (ndifference < 0) ? -1 : 1;
                        }
                    }
                    else
                    {
                        for(uint8_t j = 0; j < num_components; ++j)
                        {
                            float f1{}, f2{};
                            if(type == bgfx::AttribType::Uint8)
                            {
                                f1 = normalized ? ((float)p1[j] / 255.0f) : (float)p1[j];
                                f2 = normalized ? ((float)p2[j] / 255.0f) : (float)p2[j];
                            }
                            else // Int16
                            {
                                f1 = normalized ? ((float)((int16_t*)p1)[j] / 32767.0f) : (float)((int16_t*)p1)[j];
                                f2 = normalized ? ((float)((int16_t*)p2)[j] / 32767.0f) : (float)((int16_t*)p2)[j];
                            }
                            diff = f1 - f2;
                            if(fabsf(diff) > tolerance)
                            {
                                return (diff < 0) ? -1 : 1;
                            }
                        }
                    }
                    break;
                }

                default:
                    // Handle other types if necessary.
                    break;
            }
        }

        // Both vertices are equal for the purposes of this test.
        return 0;
    };

    int ndifference = vertex_compare(key1.vertex, key2.vertex, key1.format, key1.tolerance);
    if(ndifference != 0)
    {
        return (ndifference < 0);
    }

    // Exactly equal
    return false;
}

auto operator<(const mesh::bone_combination_key& key1, const mesh::bone_combination_key& key2) -> bool
{
    // Data group id must match.
    if(key1.data_group_id != key2.data_group_id)
    {
        return key1.data_group_id < key2.data_group_id;
    }

    const mesh::face_influences* p1 = key1.influences;
    const mesh::face_influences* p2 = key2.influences;

    // The bone count must match.
    if(p1->bones.size() != p2->bones.size())
    {
        return p1->bones.size() < p2->bones.size();
    }

    // Compare the bone indices in each list
    auto it_bone1 = p1->bones.begin();
    auto it_bone2 = p2->bones.begin();
    for(; it_bone1 != p1->bones.end() && it_bone2 != p2->bones.end(); ++it_bone1, ++it_bone2)
    {
        if(it_bone1->first != it_bone2->first)
        {
            return it_bone1->first < it_bone2->first;
        }

    } // Next Bone

    // Exact match (for the purposes of combining influences)
    return false;
}

auto mesh::generate_vertex_components(bool weld) -> bool
{
    // Vertex normals were requested (and at least some were not yet provided?)
    if(force_normal_generation_ || preparation_data_.compute_normals)
    {
        // Generate the adjacency information for vertex normal computation
        std::vector<uint32_t> adjacency;
        if(!generate_adjacency(adjacency))
        {
            APPLOG_ERROR("Failed to generate adjacency buffer mesh containing {0} faces.\n",
                         preparation_data_.triangle_count);
            return false;

        } // End if failed to generate
        if(force_barycentric_generation_ || preparation_data_.compute_barycentric)
        {
            // Generate any vertex barycentric coords that have not been provided
            if(!generate_vertex_barycentrics(&adjacency.front()))
            {
                APPLOG_ERROR("Failed to generate vertex barycentric coords for mesh "
                             "containing {0} faces.\n",
                             preparation_data_.triangle_count);
                return false;

            } // End if failed to generate

        } // End if compute

        // Generate any vertex normals that have not been provided
        if(!generate_vertex_normals(&adjacency.front()))
        {
            APPLOG_ERROR("Failed to generate vertex normals for mesh containing {0} faces.\n",
                         preparation_data_.triangle_count);
            return false;

        } // End if failed to generate

    } // End if compute

    // Weld vertices at this point
    if(weld)
    {
        if(!weld_vertices())
        {
            APPLOG_ERROR("Failed to weld vertices for mesh containing {0} faces.\n", preparation_data_.triangle_count);
            return false;

        } // End if failed to weld

    } // End if optional weld

    // Binormals and / or tangents were requested (and at least some where not yet
    // provided?)
    if(force_tangent_generation_ || preparation_data_.compute_binormals || preparation_data_.compute_tangents)
    {
        // Requires normals
        if(vertex_format_.has(gfx::attribute::Normal))
        {
            // Generate any vertex tangents that have not been provided
            if(!generate_vertex_tangents())
            {
                APPLOG_ERROR("Failed to generate vertex tangents for mesh containing "
                             "{0} faces.\n",
                             preparation_data_.triangle_count);
                return false;

            } // End if failed to generate

        } // End if has normals

    } // End if compute

    // Success!
    return true;
}

auto mesh::generate_vertex_normals(uint32_t* adjacency_ptr, std::vector<uint32_t>* remap_array_ptr /* = nullptr */)
    -> bool
{
    uint32_t start_tri, previous_tri, current_tri;
    math::vec3 vec_edge1, vec_edge2, vec_normal;
    uint32_t i, j, k, index;

    // Get access to useful data offset information.
    uint16_t position_offset = vertex_format_.getOffset(gfx::attribute::Position);
    bool has_normals = vertex_format_.has(gfx::attribute::Normal);
    uint16_t vertex_stride = vertex_format_.getStride();

    // Final format requests vertex normals?
    if(!has_normals)
    {
        return true;
    }

    // Size the remap array accordingly and populate it with the default mapping.
    uint32_t original_vertex_count = preparation_data_.vertex_count;
    if(remap_array_ptr)
    {
        remap_array_ptr->resize(preparation_data_.vertex_count);
        for(i = 0; i < preparation_data_.vertex_count; ++i)
        {
            (*remap_array_ptr)[i] = i;
        }

    } // End if supplied

    // Pre-compute surface normals for each triangle
    uint8_t* src_vertices_ptr = preparation_data_.vertex_data.data();
    auto* normals_ptr = new math::vec3[preparation_data_.triangle_count];
    memset(normals_ptr, 0, preparation_data_.triangle_count * sizeof(math::vec3));
    for(i = 0; i < preparation_data_.triangle_count; ++i)
    {
        // Retrieve positions of each referenced vertex.
        const triangle& tri = preparation_data_.triangle_data[i];
        const auto* v1 =
            reinterpret_cast<const math::vec3*>(src_vertices_ptr + (tri.indices[0] * vertex_stride) + position_offset);
        const auto* v2 =
            reinterpret_cast<const math::vec3*>(src_vertices_ptr + (tri.indices[1] * vertex_stride) + position_offset);
        const auto* v3 =
            reinterpret_cast<const math::vec3*>(src_vertices_ptr + (tri.indices[2] * vertex_stride) + position_offset);

        // Compute the two edge vectors required for generating our normal
        // We normalize here to prevent problems when the triangles are very small.
        vec_edge1 = math::normalize(*v2 - *v1);
        vec_edge2 = math::normalize(*v3 - *v1);

        // Generate the normal
        vec_normal = math::cross(vec_edge1, vec_edge2);
        normals_ptr[i] = math::normalize(vec_normal);

    } // Next Face

    // Now compute the actual VERTEX normals using face adjacency information
    for(i = 0; i < preparation_data_.triangle_count; ++i)
    {
        triangle& tri = preparation_data_.triangle_data[i];
        if(tri.flags & triangle_flags::degenerate)
        {
            continue;
        }

        // Process each vertex in the face
        for(j = 0; j < 3; ++j)
        {
            // Retrieve the index for this vertex.
            index = tri.indices[j];

            // Skip this vertex if normal information was already provided.
            if(!force_normal_generation_ &&
               (preparation_data_.vertex_flags[index] & preparation_data::source_contains_normal))
            {
                continue;
            }

            // To generate vertex normals using the adjacency information we first
            // need to walk backwards
            // through the list to find the first triangle that references this vertex
            // (using entrance/exit
            // edge strategy).
            // Once we have the first triangle, step forwards and sum the normals of
            // each of the faces
            // for each triangle we touch. This is essentially a flood fill through
            // all of the triangles
            // that touch this vertex, without ever having to test the entire set for
            // shared vertices.
            // The initial backwards traversal prevents us from having to store (and
            // test) a 'visited' flag
            // for
            // every triangle in the buffer.

            // First walk backwards...
            start_tri = i;
            previous_tri = i;
            current_tri = adjacency_ptr[(i * 3) + ((j + 2) % 3)];
            for(;;)
            {
                // Stop walking if we reach the starting triangle again, or if there
                // is no connectivity out of this edge
                if(current_tri == start_tri || current_tri == 0xFFFFFFFF)
                {
                    break;
                }

                // Find the edge in the adjacency list that we came in through
                for(k = 0; k < 3; ++k)
                {
                    if(adjacency_ptr[(current_tri * 3) + k] == previous_tri)
                    {
                        break;
                    }

                } // Next item in adjacency list

                // If we found the edge we entered through, the exit edge will
                // be the edge counter-clockwise from this one when walking backwards
                if(k < 3)
                {
                    previous_tri = current_tri;
                    current_tri = adjacency_ptr[(current_tri * 3) + ((k + 2) % 3)];

                } // End if found entrance edge
                else
                {
                    break;

                } // End if failed to find entrance edge

            } // Next Test

            // We should now be at the starting triangle, we can start to walk
            // forwards
            // collecting the face normals. First find the exit edge so we can start
            // walking.
            if(current_tri != 0xFFFFFFFF)
            {
                for(k = 0; k < 3; ++k)
                {
                    if(adjacency_ptr[(current_tri * 3) + k] == previous_tri)
                    {
                        break;
                    }

                } // Next item in adjacency list
            }
            else
            {
                // Couldn't step back, so first triangle is the current triangle
                current_tri = i;
                k = j;
            }

            if(k < 3)
            {
                start_tri = current_tri;
                previous_tri = current_tri;
                current_tri = adjacency_ptr[(current_tri * 3) + k];
                vec_normal = normals_ptr[start_tri];
                for(;;)
                {
                    // Stop walking if we reach the starting triangle again, or if there
                    // is no connectivity out of this edge
                    if(current_tri == start_tri || current_tri == 0xFFFFFFFF)
                    {
                        break;
                    }

                    // Add this normal.
                    vec_normal += normals_ptr[current_tri];

                    // Find the edge in the adjacency list that we came in through
                    for(k = 0; k < 3; ++k)
                    {
                        if(adjacency_ptr[(current_tri * 3) + k] == previous_tri)
                        {
                            break;
                        }

                    } // Next item in adjacency list

                    // If we found the edge we came entered through, the exit edge will
                    // be the edge clockwise from this one when walking forwards
                    if(k < 3)
                    {
                        previous_tri = current_tri;
                        current_tri = adjacency_ptr[(current_tri * 3) + ((k + 1) % 3)];

                    } // End if found entrance edge
                    else
                    {
                        break;

                    } // End if failed to find entrance edge

                } // Next Test

            } // End if found entrance edge

            // Normalize the new vertex normal
            vec_normal = math::normalize(vec_normal);

            // If the normal we are about to store is significantly different from any
            // normal
            // already stored in this vertex (excepting the case where it is <0,0,0>),
            // we need
            // to split the vertex into two.
            float fn[4];
            gfx::vertex_unpack(fn, gfx::attribute::Normal, vertex_format_, src_vertices_ptr, index);
            math::vec3 ref_normal;
            ref_normal[0] = fn[0];
            ref_normal[1] = fn[1];
            ref_normal[2] = fn[2];
            if(ref_normal.x == 0.0f && ref_normal.y == 0.0f && ref_normal.z == 0.0f)
            {
                gfx::vertex_pack(fn, true, gfx::attribute::Normal, vertex_format_, src_vertices_ptr, index);
            } // End if no normal stored here yet
            else
            {
                // Split and store in a new vertex if it is different (enough)
                if(math::abs(ref_normal.x - vec_normal.x) >= 1e-3f || math::abs(ref_normal.y - vec_normal.y) >= 1e-3f ||
                   math::abs(ref_normal.z - vec_normal.z) >= 1e-3f)
                {
                    // Make room for new vertex data.
                    preparation_data_.vertex_data.resize(preparation_data_.vertex_data.size() + vertex_stride);

                    // Ensure that we update the 'src_vertices_ptr' pointer (used
                    // throughout the
                    // loop). The internal buffer wrapped by the resized vertex data
                    // vector
                    // may have been re-allocated.
                    src_vertices_ptr = preparation_data_.vertex_data.data();

                    // Duplicate the vertex at the end of the buffer
                    std::memcpy(src_vertices_ptr + (preparation_data_.vertex_count * vertex_stride),
                                src_vertices_ptr + (index * vertex_stride),
                                vertex_stride);

                    // Duplicate any other remaining information.
                    preparation_data_.vertex_flags.push_back(preparation_data_.vertex_flags[index]);

                    // Record the split
                    if(remap_array_ptr)
                    {
                        (*remap_array_ptr)[index] = preparation_data_.vertex_count;
                    }

                    // Store the new normal and finally record the fact that we have
                    // added a new vertex.
                    index = preparation_data_.vertex_count++;
                    math::vec4 norm(vec_normal, 0.0f);
                    gfx::vertex_pack(math::value_ptr(norm),
                                     true,
                                     gfx::attribute::Normal,
                                     vertex_format_,
                                     src_vertices_ptr,
                                     index);

                    // Update the index
                    tri.indices[j] = index;

                } // End if normal is different

            } // End if normal already stored here

        } // Next Vertex

    } // Next Face

    // We're done with the surface normals
    checked_array_delete(normals_ptr);

    // If no new vertices were introduced, then it is not necessary
    // for the caller to remap anything.
    if(remap_array_ptr && original_vertex_count == preparation_data_.vertex_count)
    {
        remap_array_ptr->clear();
    }

    // Success!
    return true;
}

auto mesh::generate_vertex_barycentrics(uint32_t* adjacency) -> bool
{
    (void)adjacency;
    return true;
}

auto mesh::generate_vertex_tangents() -> bool
{
    math::vec3 *tangents = nullptr, *bitangents = nullptr;
    uint32_t i, i1, i2, i3, num_faces, num_verts;
    math::vec3 P, Q, T, B, cross_vec, normal_vec;

    // Get access to useful data offset information.
    uint16_t vertex_stride = vertex_format_.getStride();

    bool has_normals = vertex_format_.has(gfx::attribute::Normal);
    // This will fail if we don't already have normals however.
    if(!has_normals)
    {
        return false;
    }

    // Final format requests tangents?
    bool requires_tangents = vertex_format_.has(gfx::attribute::Tangent);
    bool requires_bitangents = vertex_format_.has(gfx::attribute::Bitangent);
    if(!force_tangent_generation_ && !requires_bitangents && !requires_tangents)
    {
        return true;
    }

    // Allocate storage space for the tangent and bitangent vectors
    // that we will effectively need to average for shared vertices.
    num_faces = preparation_data_.triangle_count;
    num_verts = preparation_data_.vertex_count;
    tangents = new math::vec3[num_verts];
    bitangents = new math::vec3[num_verts];
    memset(tangents, 0, sizeof(math::vec3) * num_verts);
    memset(bitangents, 0, sizeof(math::vec3) * num_verts);

    // Iterate through each triangle in the mesh
    uint8_t* src_vertices_ptr = preparation_data_.vertex_data.data();
    for(i = 0; i < num_faces; ++i)
    {
        triangle& tri = preparation_data_.triangle_data[i];

        // Compute the three indices for the triangle
        i1 = tri.indices[0];
        i2 = tri.indices[1];
        i3 = tri.indices[2];

        // Retrieve references to the positions of the three vertices in the
        // triangle.
        math::vec3 E;
        float fE[4];
        gfx::vertex_unpack(fE, gfx::attribute::Position, vertex_format_, src_vertices_ptr, i1);
        math::vec3 F;
        float fF[4];
        gfx::vertex_unpack(fF, gfx::attribute::Position, vertex_format_, src_vertices_ptr, i2);
        math::vec3 G;
        float fG[4];
        gfx::vertex_unpack(fG, gfx::attribute::Position, vertex_format_, src_vertices_ptr, i3);
        std::memcpy(&E[0], fE, 3 * sizeof(float));
        std::memcpy(&F[0], fF, 3 * sizeof(float));
        std::memcpy(&G[0], fG, 3 * sizeof(float));

        // Retrieve references to the base texture coordinates of the three vertices
        // in the triangle.
        // TODO: Allow customization of which tex coordinates to generate from.
        math::vec2 Et;
        float fEt[4];
        gfx::vertex_unpack(&fEt[0], gfx::attribute::TexCoord0, vertex_format_, src_vertices_ptr, i1);
        math::vec2 Ft;
        float fFt[4];
        gfx::vertex_unpack(&fFt[0], gfx::attribute::TexCoord0, vertex_format_, src_vertices_ptr, i2);
        math::vec2 Gt;
        float fGt[4];
        gfx::vertex_unpack(&fGt[0], gfx::attribute::TexCoord0, vertex_format_, src_vertices_ptr, i3);
        std::memcpy(&Et[0], fEt, 2 * sizeof(float));
        std::memcpy(&Ft[0], fFt, 2 * sizeof(float));
        std::memcpy(&Gt[0], fGt, 2 * sizeof(float));

        // Compute the known variables P & Q, where "P = F-E" and "Q = G-E"
        // based on our original discussion of the tangent vector
        // calculation.
        P = F - E;
        Q = G - E;

        // Also compute the know variables <s1,t1> and <s2,t2>. Recall that
        // these are the texture coordinate deltas similarly for "F-E"
        // and "G-E".
        float s1 = Ft.x - Et.x;
        float t1 = Ft.y - Et.y;
        float s2 = Gt.x - Et.x;
        float t2 = Gt.y - Et.y;

        // Next we can pre-compute part of the equation we developed
        // earlier: "1/(s1 * t2 - s2 * t1)". We do this in two separate
        // stages here in order to ensure that the texture coordinates
        // are not invalid.
        float r = (s1 * t2 - s2 * t1);
        if(math::abs(r) < math::epsilon<float>())
        {
            continue;
        }
        r = 1.0f / r;

        // All that's left for us to do now is to run the matrix
        // multiplication and multiply the result by the scalar portion
        // we precomputed earlier.
        T.x = r * (t2 * P.x - t1 * Q.x);
        T.y = r * (t2 * P.y - t1 * Q.y);
        T.z = r * (t2 * P.z - t1 * Q.z);
        B.x = r * (s1 * Q.x - s2 * P.x);
        B.y = r * (s1 * Q.y - s2 * P.y);
        B.z = r * (s1 * Q.z - s2 * P.z);

        // Add the tangent and bitangent vectors (summed average) to
        // any previous values computed for each vertex.
        tangents[i1] += T;
        tangents[i2] += T;
        tangents[i3] += T;
        bitangents[i1] += B;
        bitangents[i2] += B;
        bitangents[i3] += B;

    } // Next triangle

    // Generate final tangent vectors
    for(i = 0; i < num_verts; i++, src_vertices_ptr += vertex_stride)
    {
        // Skip if the original imported data already provided a bitangent /
        // tangent.
        bool has_bitangent = false;
        bool has_tangent = false;

        if(!preparation_data_.vertex_flags.empty())
        {
            has_bitangent = ((preparation_data_.vertex_flags[i] & preparation_data::source_contains_binormal) != 0);
            has_tangent = ((preparation_data_.vertex_flags[i] & preparation_data::source_contains_tangent) != 0);
        }
        if(!force_tangent_generation_ && has_bitangent && has_tangent)
        {
            continue;
        }

        // Retrieve the normal vector from the vertex and the computed
        // tangent vector.
        float normal[4];
        gfx::vertex_unpack(normal, gfx::attribute::Normal, vertex_format_, src_vertices_ptr);
        std::memcpy(&normal_vec[0], normal, 3 * sizeof(float));

        T = tangents[i];

        // GramSchmidt orthogonalize
        T = T - (normal_vec * math::dot(normal_vec, T));
        
        // Check if the resulting tangent is too small (parallel to normal case)
        float length_sq = math::dot(T, T);
        if(length_sq < 1e-6f)
        {
            // Tangent was parallel to normal, generate a perpendicular vector
            // Find the axis that the normal is least aligned with
            math::vec3 axis;
            if(std::abs(normal_vec.x) < std::abs(normal_vec.y) && std::abs(normal_vec.x) < std::abs(normal_vec.z))
            {
                axis = math::vec3(1.0f, 0.0f, 0.0f);
            }
            else if(std::abs(normal_vec.y) < std::abs(normal_vec.z))
            {
                axis = math::vec3(0.0f, 1.0f, 0.0f);
            }
            else
            {
                axis = math::vec3(0.0f, 0.0f, 1.0f);
            }
            T = math::cross(normal_vec, axis);
        }
        
        T = math::normalize(T);

        // Store tangent if required
        if(force_tangent_generation_ || (!has_tangent && requires_tangents))
        {
            math::vec4 t(T, 1.0f);
            gfx::vertex_pack(math::value_ptr(t), true, gfx::attribute::Tangent, vertex_format_, src_vertices_ptr);
        }

        // Compute and store bitangent if required
        if(force_tangent_generation_ || (!has_bitangent && requires_bitangents))
        {
            // Calculate the new orthogonal bitangent
            B = math::cross(normal_vec, T);
            B = math::normalize(B);

            // Compute the "handedness" of the tangent and bitangent. This
            // ensures the inverted / mirrored texture coordinates still have
            // an accurate matrix.
            cross_vec = math::cross(normal_vec, T);
            if(math::dot(cross_vec, bitangents[i]) < 0.0f)
            {
                // Flip the bitangent
                B = -B;

            } // End if coordinates inverted

            // Store.
            math::vec4 b(B, 1.0f);
            gfx::vertex_pack(math::value_ptr(b), true, gfx::attribute::Bitangent, vertex_format_, src_vertices_ptr);

        } // End if requires bitangent

    } // Next vertex

    // Cleanup
    checked_array_delete(tangents);
    checked_array_delete(bitangents);

    // Return success
    return true;
}

auto mesh::weld_vertices(float tolerance, std::vector<uint32_t>* vertex_remap_ptr /* = nullptr */) -> bool
{
    weld_key key;
    std::map<weld_key, uint32_t> vertex_tree;
    std::map<weld_key, uint32_t>::const_iterator it_key;
    byte_array_t new_vertex_data, new_vertex_flags;
    uint32_t new_vertex_count = 0;

    // Allocate enough space to build the remap array for the existing vertices
    if(vertex_remap_ptr)
    {
        vertex_remap_ptr->resize(preparation_data_.vertex_count);
    }
    auto collapse_map = new uint32_t[preparation_data_.vertex_count];

    // Retrieve useful data offset information.
    uint16_t vertex_stride = vertex_format_.getStride();

    // For each vertex to be welded.
    for(uint32_t i = 0; i < preparation_data_.vertex_count; ++i)
    {
        // Build a new key structure for inserting
        key.vertex = (&preparation_data_.vertex_data[0]) + (i * vertex_stride);
        key.format = vertex_format_;
        key.tolerance = tolerance;

        // Does a vertex with matching details already exist in the tree.
        it_key = vertex_tree.find(key);
        if(it_key == vertex_tree.end())
        {
            // No matching vertex. Insert into the tree (value = NEW index of vertex).
            vertex_tree[key] = new_vertex_count;
            collapse_map[i] = new_vertex_count;
            if(vertex_remap_ptr)
            {
                (*vertex_remap_ptr)[i] = new_vertex_count;
            }

            // Store the vertex in the new buffer
            new_vertex_data.resize((new_vertex_count + 1) * vertex_stride);
            std::memcpy(&new_vertex_data[new_vertex_count * vertex_stride], key.vertex, vertex_stride);
            new_vertex_flags.push_back(preparation_data_.vertex_flags[i]);
            new_vertex_count++;

        } // End if no matching vertex
        else
        {
            // A vertex already existed at this location.
            // Just mark the 'collapsed' index for this vertex in the remap array.
            collapse_map[i] = it_key->second;
            if(vertex_remap_ptr)
            {
                (*vertex_remap_ptr)[i] = 0xFFFFFFFF;
            }

        } // End if vertex already existed

    } // Next Vertex

    // If nothing was welded, just bail
    if(preparation_data_.vertex_count == new_vertex_count)
    {
        checked_array_delete(collapse_map);

        if(vertex_remap_ptr)
        {
            vertex_remap_ptr->clear();
        }
        return true;

    } // End if nothing to do

    // Otherwise, replace the old preparation vertices and remap
    preparation_data_.vertex_data.clear();
    preparation_data_.vertex_data.resize(new_vertex_data.size());
    std::memcpy(preparation_data_.vertex_data.data(), new_vertex_data.data(), new_vertex_data.size());
    preparation_data_.vertex_flags.clear();
    preparation_data_.vertex_flags.resize(new_vertex_flags.size());
    std::memcpy(preparation_data_.vertex_flags.data(), new_vertex_flags.data(), new_vertex_flags.size());
    preparation_data_.vertex_count = new_vertex_count;

    // Now remap all the triangle indices
    for(uint32_t i = 0; i < preparation_data_.triangle_count; ++i)
    {
        triangle& tri = preparation_data_.triangle_data[i];
        tri.indices[0] = collapse_map[tri.indices[0]];
        tri.indices[1] = collapse_map[tri.indices[1]];
        tri.indices[2] = collapse_map[tri.indices[2]];

    } // Next triangle

    // Clean up
    checked_array_delete(collapse_map);

    // Success!
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// skin_bind_data Member Definitions
///////////////////////////////////////////////////////////////////////////////
void skin_bind_data::add_bone(const bone_influence& bone)
{
    bones_.push_back(bone);
}

void skin_bind_data::remove_empty_bones()
{
    for(size_t i = 0; i < bones_.size();)
    {
        if(bones_[i].influences.empty())
        {
            bones_.erase(bones_.begin() + static_cast<int>(i));

        } // End if empty
        else
        {
            ++i;
        }

    } // Next Bone
}

void skin_bind_data::clear_vertex_influences()
{
    for(auto& bone : bones_)
    {
        bone.influences.clear();
    }
}

void skin_bind_data::clear()
{
    bones_.clear();
}

void skin_bind_data::remap_vertices(const std::vector<uint32_t>& remap)
{
    // Iterate through all bone information and remap vertex indices.
    for(auto& bone : bones_)
    {
        vertex_influence_array_t new_influences;
        vertex_influence_array_t& influences = bone.influences;
        new_influences.reserve(influences.size());
        for(auto& influence : influences)
        {
            uint32_t new_index = remap[influence.vertex_index];
            if(new_index != 0xFFFFFFFF)
            {
                // Insert an influence at the new index
                new_influences.push_back(vertex_influence{new_index, influence.weight});

                // If the vertex was split into two, we want to retain an
                // influence to the original index too.
                if(new_index >= remap.size())
                {
                    new_influences.push_back(vertex_influence{influence.vertex_index, influence.weight});
                }

            } // End if !removed

        } // Next source influence
        bone.influences = new_influences;

    } // Next bone
}

void skin_bind_data::build_vertex_table(uint32_t vertex_count,
                                        const std::vector<uint32_t>& vertex_remap,
                                        vertex_data_array_t& table)
{
    uint32_t vertex{};

    // Initialize the vertex table with the required number of vertices.
    table.reserve(vertex_count);
    for(vertex = 0; vertex < vertex_count; ++vertex)
    {
        vertex_data data;
        data.palette = -1;
        data.original_vertex = vertex;
        table.push_back(data);

    } // Next Vertex

    // Iterate through all bone information and populate the above array.
    for(size_t i = 0; i < bones_.size(); ++i)
    {
        vertex_influence_array_t& influences = bones_[i].influences;
        for(auto& influence : influences)
        {
            // Vertex data has been remapped?
            if(!vertex_remap.empty())
            {
                vertex = vertex_remap[influence.vertex_index];
                if(vertex == 0xFFFFFFFF)
                {
                    continue;
                }
                auto& data = table[vertex];
                // Push influence data.
                data.influences.push_back(static_cast<int32_t>(i));
                data.weights.push_back(influence.weight);
            } // End if remap
            else
            {
                auto& data = table[influence.vertex_index];
                // Push influence data.
                data.influences.push_back(static_cast<int32_t>(i));
                data.weights.push_back(influence.weight);
            }

        } // Next Influence

    } // Next Bone
}

auto skin_bind_data::get_bones() const -> const std::vector<skin_bind_data::bone_influence>&
{
    return bones_;
}

auto skin_bind_data::get_bones() -> std::vector<skin_bind_data::bone_influence>&
{
    return bones_;
}

auto skin_bind_data::has_bones() const -> bool
{
    return !get_bones().empty();
}

auto skin_bind_data::find_bone_by_id(const std::string& name) const -> bone_query
{
    bone_query query{};
    auto it = std::find_if(std::begin(bones_),
                           std::end(bones_),
                           [name](const auto& bone)
                           {
                               return name == bone.bone_id;
                           });
    if(it != std::end(bones_))
    {
        query.bone = &(*it);
        query.index = std::distance(std::begin(bones_), it);
    }

    return query;
}

///////////////////////////////////////////////////////////////////////////////
// bone_palette Member Definitions
///////////////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//  Name : bone_palette() (Constructor)
/// <summary>
/// Class constructor.
/// </summary>
//-----------------------------------------------------------------------------
bone_palette::bone_palette(uint32_t palette_size)
    : data_group_id_(0)
    , maximum_size_(palette_size)
    , maximum_blend_index_(-1)
{
}

auto bone_palette::get_skinning_matrices(const std::vector<math::transform>& node_transforms,
                                         const skin_bind_data& bind_data) const -> const std::vector<math::mat4>&
{
    // Retrieve the main list of bones from the skin bind data that will
    // be referenced by the palette's bone index list.
    const auto& bind_list = bind_data.get_bones();

    // Compute transformation matrix for each bone in the palette
    const size_t bones_size = bones_.size();
    const size_t count = std::min(bones_size, node_transforms.size());
    
    thread_local static std::vector<math::mat4> skinning_transforms_;
    skinning_transforms_.resize(bones_size, math::identity<math::mat4>());
    
    // Optimize: cache bind list size check
    const size_t bind_list_size = bind_list.size();
    
    for(size_t i = 0; i < count; ++i)
    {
        const auto bone = bones_[i];
        
        // Bounds check to avoid out-of-range access
        if(bone >= node_transforms.size() || bone >= bind_list_size)
        {
            continue;
        }
        
        const auto& bone_transform = node_transforms[bone];
        const auto& bone_data = bind_list[bone];
        
        // Direct matrix multiplication - cache the bind pose matrix
        const auto& bind_pose_matrix = bone_data.bind_pose_transform.get_matrix();
        skinning_transforms_[i] = bone_transform.get_matrix() * bind_pose_matrix;
    }

    return skinning_transforms_;
}

auto bone_palette::get_skinning_matrices(const std::vector<math::mat4>& node_transforms,
                                         const skin_bind_data& bind_data) const -> const std::vector<math::mat4>&
{
    // Retrieve the main list of bones from the skin bind data that will
    // be referenced by the palette's bone index list.
    const auto& bind_list = bind_data.get_bones();

    // Compute transformation matrix for each bone in the palette
    const size_t bones_size = bones_.size();
    const size_t count = std::min(bones_size, node_transforms.size());
    
    thread_local static std::vector<math::mat4> skinning_transforms_;
    skinning_transforms_.resize(bones_size, math::identity<math::mat4>());

    // Optimize: cache bind list size check
    const size_t bind_list_size = bind_list.size();
    
    for(size_t i = 0; i < count; ++i)
    {
        const auto bone = bones_[i];
        
        // Bounds check to avoid out-of-range access
        if(bone >= node_transforms.size() || bone >= bind_list_size)
        {
            continue;
        }
        
        const auto& bone_transform = node_transforms[bone];
        const auto& bone_data = bind_list[bone];
        
        // Direct matrix multiplication - cache the bind pose matrix
        const auto& bind_pose_matrix = bone_data.bind_pose_transform.get_matrix();
        skinning_transforms_[i] = bone_transform * bind_pose_matrix;
    }

    return skinning_transforms_;
}

void bone_palette::assign_bones(bone_index_map_t& bones, std::vector<uint32_t>& faces)
{
    bone_index_map_t::iterator it_bone, it_bone2;

    // Iterate through newly specified input bones and add any unique ones to the
    // palette.
    for(it_bone = bones.begin(); it_bone != bones.end(); ++it_bone)
    {
        it_bone2 = bones_lut_.find(it_bone->first);
        if(it_bone2 == bones_lut_.end())
        {
            bones_lut_[it_bone->first] = static_cast<uint32_t>(bones_.size());
            bones_.push_back(it_bone->first);

        } // End if not already added

    } // Next Bone

    // Merge the new face list with ours.
    // faces_.insert(faces_.end(), faces.begin(), faces.end());

    faces_.resize(faces_.size() + faces.size());
    std::memcpy(faces_.data(), faces.data(), faces.size() * sizeof(uint32_t));
}

void bone_palette::assign_bones(std::vector<bool>& bones, std::vector<uint32_t>& faces)
{
    bone_index_map_t::iterator it_bone, it_bone2;

    // Iterate through newly specified input bones and add any unique ones to the
    // palette.
    // for(it_bone = bones.begin(); it_bone != bones.end(); ++it_bone)
    for(size_t i = 0, j = bones.size(); i < j; ++i)
    {
        if(!bones[i])
        {
            continue;
        }

        it_bone2 = bones_lut_.find(i);
        if(it_bone2 == bones_lut_.end())
        {
            bones_lut_[i] = static_cast<uint32_t>(bones_.size());
            bones_.push_back(i);

        } // End if not already added

    } // Next Bone

    // Merge the new face list with ours.
    // faces_.insert(faces_.end(), faces.begin(), faces.end());

    faces_.resize(faces_.size() + faces.size());
    std::memcpy(faces_.data(), faces.data(), faces.size() * sizeof(uint32_t));
}

void bone_palette::assign_bones(const std::vector<uint32_t>& bones)
{
    bone_index_map_t::iterator it_bone;

    // Clear out prior data.
    bones_.clear();
    bones_lut_.clear();

    // Iterate through newly specified input bones and add any unique ones to the
    // palette.
    for(size_t i = 0; i < bones.size(); ++i)
    {
        it_bone = bones_lut_.find(bones[i]);
        if(it_bone == bones_lut_.end())
        {
            bones_lut_[bones[i]] = static_cast<uint32_t>(bones_.size());
            bones_.push_back(bones[i]);

        } // End if not already added

    } // Next Bone
}

void bone_palette::compute_palette_fit(bone_index_map_t& input,
                                       int32_t& current_space,
                                       int32_t& common_bones,
                                       int32_t& additional_bones)
{
    // Reset values
    current_space = static_cast<int32_t>(maximum_size_ - static_cast<uint32_t>(bones_.size()));
    common_bones = 0;
    additional_bones = 0;

    // Early out if possible
    if(bones_.size() == 0)
    {
        additional_bones = static_cast<int32_t>(input.size());
        return;

    } // End if no bones stored
    else if(input.size() == 0)
    {
        return;

    } // End if no bones input

    // Iterate through newly specified input bones and see how many
    // indices it has in common with our existing set.
    bone_index_map_t::iterator it_bone, it_bone2;
    for(it_bone = input.begin(); it_bone != input.end(); ++it_bone)
    {
        it_bone2 = bones_lut_.find(it_bone->first);
        if(it_bone2 != bones_lut_.end())
            common_bones++;
        else
            additional_bones++;

    } // Next Bone
}

auto bone_palette::translate_bone_to_palette(uint32_t bone_index) const -> uint32_t
{
    auto it_bone = bones_lut_.find(bone_index);
    if(it_bone == bones_lut_.end())
        return 0xFFFFFFFF;
    return it_bone->second;
}

auto bone_palette::get_data_group() const -> uint32_t
{
    return data_group_id_;
}

void bone_palette::set_data_group(uint32_t group)
{
    data_group_id_ = group;
}

auto bone_palette::get_maximum_blend_index() const -> int32_t
{
    return maximum_blend_index_;
}

void bone_palette::set_maximum_blend_index(int nIndex)
{
    maximum_blend_index_ = nIndex;
}

auto bone_palette::get_maximum_size() const -> uint32_t
{
    return maximum_size_;
}

auto bone_palette::get_influenced_faces() -> std::vector<uint32_t>&
{
    return faces_;
}

void bone_palette::clear_influenced_faces()
{
    faces_.clear();
}

auto bone_palette::get_bones() const -> const std::vector<uint32_t>&
{
    return bones_;
}

///////////////////////////////////////////////////////////////////////////////
// Global Operator Definitions
///////////////////////////////////////////////////////////////////////////////
auto mesh::sort_mesh_data() -> bool
{
    if(preparation_data_.compute_per_triangle_material_data)
    {
        // math::transform triangle indices, material and data group information
        // to the final triangle data arrays. We keep the latter two handy so
        // that we know precisely which material each triangle belongs to.
        triangle_data_.resize(face_count_);
    }
    uint32_t* dst_indices_ptr = system_ib_;
    for(uint32_t i = 0; i < face_count_; ++i)
    {
        // Copy indices.
        const triangle& tri_in = preparation_data_.triangle_data[i];
        *dst_indices_ptr++ = tri_in.indices[0];
        *dst_indices_ptr++ = tri_in.indices[1];
        *dst_indices_ptr++ = tri_in.indices[2];

        if(preparation_data_.compute_per_triangle_material_data)
        {
            // Copy triangle submesh information.
            mesh_submesh_key& tri_out = triangle_data_[i];
            tri_out.data_group_id = tri_in.data_group_id;
        }

    } // Next triangle

    preparation_data_.triangle_count = 0;
    preparation_data_.triangle_data.clear();

    // Clear out any old data EXCEPT the old submesh index
    // We'll need this in order to understand how to update
    // the material reference counting later on.
    data_groups_.clear();
    //  Destroy old submesh data.
    for(auto submesh : mesh_submeshes_)
    {
        checked_delete(submesh);
    }
    mesh_submeshes_.clear();

    skinned_submesh_indices_.clear();
    skinned_submesh_count_ = {};

    non_skinned_submesh_indices_.clear();
    non_skinned_submesh_count_ = {};

    for(size_t i = 0; i < preparation_data_.submeshes.size(); ++i)
    {
        const auto& s = preparation_data_.submeshes[i];
        auto* sub = new submesh(s);

        if(sub->skinned)
        {
            skinned_submesh_count_++;
            skinned_submesh_indices_[sub->data_group_id].emplace_back(i);
        }
        else
        {
            non_skinned_submesh_count_++;
            non_skinned_submesh_indices_[sub->data_group_id].emplace_back(i);
        }

        mesh_submeshes_.emplace_back(sub);
        data_groups_[sub->data_group_id].emplace_back(sub);
    }

    preparation_data_.submeshes.clear();

    return true;
}

void mesh::bind_render_buffers_for_submesh(const submesh* submesh, uint32_t lod_index)
{
    // Select appropriate index buffer based on LOD
    uint32_t* ib_data = nullptr;
    std::shared_ptr<void> ib_hardware = nullptr;
    uint32_t lod_face_count = 0;

    if(lod_index == 0)
    {
        // Use base LOD (LOD 0)
        ib_data = system_ib_;
        ib_hardware = hardware_ib_;
        lod_face_count = face_count_;
    }
    else if(lod_index > 0 && lod_index <= lods_.size())
    {
        // Use simplified LOD
        const auto& lod = lods_[lod_index - 1];
        ib_data = lod.system_ib_;
        ib_hardware = lod.hardware_ib_;
        lod_face_count = lod.face_count_;
    }
    else
    {
        // Invalid LOD index, fall back to base
        ib_data = system_ib_;
        ib_hardware = hardware_ib_;
        lod_face_count = face_count_;
    }

    uint32_t index_start = submesh->face_start * 3;
    uint32_t index_count = submesh->face_count * 3;

    // Hardware or software rendering?
    if(hardware_mesh_)
    {
        // Render using hardware streams
        auto vb = std::static_pointer_cast<gfx::vertex_buffer>(hardware_vb_);
        auto ib = std::static_pointer_cast<gfx::index_buffer>(ib_hardware);

        gfx::set_vertex_buffer(0, vb->native_handle()); // submesh->vertex_start, submesh->vertex_count);
        gfx::set_index_buffer(ib->native_handle(), index_start, index_count);

    } // End if has hardware copy
    else
    {
        if(submesh->vertex_count == gfx::get_avail_transient_vertex_buffer(submesh->vertex_count, vertex_format_))
        {
            gfx::transient_vertex_buffer vb;
            gfx::alloc_transient_vertex_buffer(&vb, submesh->vertex_count, vertex_format_);
            std::memcpy(vb.data,
                        system_vb_ + submesh->vertex_start * vertex_format_.getStride(),
                        vb.size); // Adjust the pointer to start at the correct vertex
            gfx::set_vertex_buffer(0, &vb, 0, submesh->vertex_count);
        }

        if(index_count == gfx::get_avail_transient_index_buffer(index_count, true))
        {
            gfx::transient_index_buffer ib;
            gfx::alloc_transient_index_buffer(&ib, index_count, true);
            std::memcpy(ib.data,
                        ib_data + index_start,
                        index_count * sizeof(uint32_t)); // Adjust the pointer to start at the correct index
            gfx::set_index_buffer(&ib, 0, index_count);
        }

    } // End if software only copy
}

} // namespace unravel
