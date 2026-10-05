#include "mesh_import_geometry.h"

#include <logging/logging.h>

#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>

namespace unravel
{
namespace importer
{
namespace mesh_import
{
namespace
{

auto process_matrix(const aiMatrix4x4& assimp_matrix) -> math::mat4
{
    math::mat4 matrix;

    matrix[0][0] = assimp_matrix.a1;
    matrix[1][0] = assimp_matrix.a2;
    matrix[2][0] = assimp_matrix.a3;
    matrix[3][0] = assimp_matrix.a4;

    matrix[0][1] = assimp_matrix.b1;
    matrix[1][1] = assimp_matrix.b2;
    matrix[2][1] = assimp_matrix.b3;
    matrix[3][1] = assimp_matrix.b4;

    matrix[0][2] = assimp_matrix.c1;
    matrix[1][2] = assimp_matrix.c2;
    matrix[2][2] = assimp_matrix.c3;
    matrix[3][2] = assimp_matrix.c4;

    matrix[0][3] = assimp_matrix.d1;
    matrix[1][3] = assimp_matrix.d2;
    matrix[2][3] = assimp_matrix.d3;
    matrix[3][3] = assimp_matrix.d4;

    return matrix;
}

void process_vertices(aiMesh* mesh, mesh::load_data& load_data)
{
    auto& submesh = load_data.submeshes.back();

    // Determine the correct offset to any relevant elements in the vertex
    bool has_position = load_data.vertex_format.has(bgfx::Attrib::Position);
    bool has_normal = load_data.vertex_format.has(bgfx::Attrib::Normal);
    bool has_bitangent = load_data.vertex_format.has(bgfx::Attrib::Bitangent);
    bool has_tangent = load_data.vertex_format.has(bgfx::Attrib::Tangent);
    bool has_texcoord0 = load_data.vertex_format.has(bgfx::Attrib::TexCoord0);
    auto vertex_stride = load_data.vertex_format.getStride();

    std::uint32_t current_vertex = load_data.vertex_count;
    load_data.vertex_count += mesh->mNumVertices;
    load_data.vertex_data.resize(load_data.vertex_count * vertex_stride);

    std::uint8_t* current_vertex_ptr = load_data.vertex_data.data() + current_vertex * vertex_stride;

    for(size_t i = 0; i < mesh->mNumVertices; ++i, current_vertex_ptr += vertex_stride)
    {
        // position
        if(mesh->HasPositions() && has_position)
        {
            float position[4];
            std::memcpy(position, &mesh->mVertices[i], sizeof(aiVector3D));

            bgfx::vertexPack(position, false, bgfx::Attrib::Position, load_data.vertex_format, current_vertex_ptr);

            submesh.bbox.add_point(math::vec3(position[0], position[1], position[2]));
        }

        // tex coords

        if(has_texcoord0)
        {
            float textureCoords[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if(mesh->HasTextureCoords(0))
            {
                std::memcpy(textureCoords, &mesh->mTextureCoords[0][i], sizeof(aiVector2D));

                bgfx::vertexPack(textureCoords,
                                 true,
                                 bgfx::Attrib::TexCoord0,
                                 load_data.vertex_format,
                                 current_vertex_ptr);
            }
            else
            {
                bgfx::vertexPack(textureCoords,
                                 true,
                                 bgfx::Attrib::TexCoord0,
                                 load_data.vertex_format,
                                 current_vertex_ptr);
            }
        }


        ////normals
        math::vec4 normal{};
        if(mesh->HasNormals() && has_normal)
        {
            std::memcpy(math::value_ptr(normal), &mesh->mNormals[i], sizeof(aiVector3D));

            bgfx::vertexPack(math::value_ptr(normal),
                             true,
                             bgfx::Attrib::Normal,
                             load_data.vertex_format,
                             current_vertex_ptr);
        }

        math::vec4 tangent{};
        // tangents
        if(has_tangent)
        {
            if(mesh->HasTangentsAndBitangents())
            {
                std::memcpy(math::value_ptr(tangent), &mesh->mTangents[i], sizeof(aiVector3D));
                tangent.w = 1.0f;

            }
            else
            {
                tangent = math::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }
                
            bgfx::vertexPack(math::value_ptr(tangent),
            true,
            bgfx::Attrib::Tangent,
            load_data.vertex_format,
            current_vertex_ptr);
        }
       

        // binormals
        math::vec4 bitangent{};
        if(has_bitangent)
        {
            if(mesh->HasTangentsAndBitangents())
            {
                std::memcpy(math::value_ptr(bitangent), &mesh->mBitangents[i], sizeof(aiVector3D));
            }
            else
            {
                bitangent = math::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }

                  // float handedness =
            //     math::dot(math::vec3(bitangent), math::normalize(math::cross(math::vec3(normal), math::vec3(tangent))));
            // tangent.w = handedness;

            bgfx::vertexPack(math::value_ptr(bitangent),
                             true,
                             bgfx::Attrib::Bitangent,
                             load_data.vertex_format,
                             current_vertex_ptr);
        }
    }
}

void process_faces(aiMesh* mesh, std::uint32_t submesh_offset, mesh::load_data& load_data)
{
    load_data.triangle_count += mesh->mNumFaces;

    load_data.triangle_data.reserve(load_data.triangle_data.size() + mesh->mNumFaces);

    for(size_t i = 0; i < mesh->mNumFaces; ++i)
    {
        aiFace face = mesh->mFaces[i];

        auto& triangle = load_data.triangle_data.emplace_back();
        triangle.data_group_id = mesh->mMaterialIndex;

        auto num_indices = std::min<size_t>(face.mNumIndices, 3);
        for(size_t j = 0; j < num_indices; ++j)
        {
            triangle.indices[j] = face.mIndices[j] + submesh_offset;
        }
    }
}

void process_bones(aiMesh* mesh, std::uint32_t submesh_offset, mesh::load_data& load_data)
{
    if(mesh->HasBones())
    {
        auto& bone_influences = load_data.skin_data.get_bones();

        for(size_t i = 0; i < mesh->mNumBones; ++i)
        {
            aiBone* assimp_bone = mesh->mBones[i];
            const std::string bone_name = assimp_bone->mName.C_Str();

            auto it = std::find_if(std::begin(bone_influences),
                                   std::end(bone_influences),
                                   [&bone_name](const auto& bone)
                                   {
                                       return bone_name == bone.bone_id;
                                   });

            skin_bind_data::bone_influence* bone_ptr = nullptr;
            if(it != std::end(bone_influences))
            {
                bone_ptr = &(*it);
            }
            else
            {
                const auto& assimp_matrix = assimp_bone->mOffsetMatrix;
                skin_bind_data::bone_influence bone_influence;
                bone_influence.bone_id = bone_name;
                bone_influence.bind_pose_transform = process_matrix(assimp_matrix);
                bone_influences.emplace_back(std::move(bone_influence));
                bone_ptr = &bone_influences.back();
            }

            if(bone_ptr == nullptr)
            {
                continue;
            }

            for(size_t j = 0; j < assimp_bone->mNumWeights; ++j)
            {
                aiVertexWeight assimp_influence = assimp_bone->mWeights[j];

                skin_bind_data::vertex_influence influence;
                influence.vertex_index = assimp_influence.mVertexId + submesh_offset;
                influence.weight = assimp_influence.mWeight;

                bone_ptr->influences.emplace_back(influence);

                // Accumulate the bone-space bounds of every influenced vertex (mesh-space
                // position pre-multiplied by the offset/bind-pose matrix). At runtime,
                // bone_world_transform * bounds yields a conservative world-space bound of
                // the skinned geometry for frustum culling of animated meshes.
                if(assimp_influence.mVertexId < mesh->mNumVertices && assimp_influence.mWeight > 0.0f)
                {
                    const auto& vertex = mesh->mVertices[assimp_influence.mVertexId];
                    const math::vec3 mesh_space_position(vertex.x, vertex.y, vertex.z);
                    const math::vec3 bone_space_position =
                        bone_ptr->bind_pose_transform.transform_coord(mesh_space_position);
                    bone_ptr->bounds.add_point(bone_space_position);
                }
            }
        }
    }
}

auto make_stable_submesh_id(const char* name, const mesh::load_data& load_data) -> uint32_t
{
    // FNV-1a hash of the source mesh name. The id must be deterministic across reimports so
    // scene/prefab references (submesh_component entries) survive submesh reordering.
    uint32_t hash = 2166136261u;
    bool empty = true;
    for(const char* c = name; *c != '\0'; ++c)
    {
        hash ^= static_cast<uint8_t>(*c);
        hash *= 16777619u;
        empty = false;
    }
    if(empty)
    {
        // Unnamed meshes fall back to an ordinal-derived id (still deterministic as long as
        // the exporter emits meshes in a stable order).
        hash = 2166136261u ^ static_cast<uint32_t>(load_data.submeshes.size() + 1);
    }
    if(hash == 0)
    {
        hash = 1;
    }
    // Disambiguate duplicate names with deterministic probing.
    auto collides = [&](uint32_t candidate)
    {
        return std::any_of(load_data.submeshes.begin(),
                           load_data.submeshes.end(),
                           [candidate](const mesh::submesh& sm)
                           {
                               return sm.stable_id == candidate;
                           });
    };
    while(collides(hash))
    {
        hash = hash * 16777619u + 1u;
        if(hash == 0)
        {
            hash = 1;
        }
    }
    return hash;
}

void process_mesh(aiMesh* mesh, mesh::load_data& load_data)
{
    load_data.submeshes.emplace_back();
    auto& submesh = load_data.submeshes.back();
    submesh.vertex_start = load_data.vertex_count;
    submesh.vertex_count = mesh->mNumVertices;
    submesh.face_start = load_data.triangle_count;
    submesh.face_count = mesh->mNumFaces;
    submesh.data_group_id = mesh->mMaterialIndex;
    submesh.skinned = mesh->HasBones();
    submesh.stable_id = make_stable_submesh_id(mesh->mName.C_Str(), load_data);
    load_data.material_count = std::max(load_data.material_count, submesh.data_group_id + 1);

    process_faces(mesh, submesh.vertex_start, load_data);
    process_bones(mesh, submesh.vertex_start, load_data);
    process_vertices(mesh, load_data);
}

void process_node(const aiScene* scene,
                  mesh::load_data& load_data,
                  const aiNode* node,
                  const std::unique_ptr<mesh::armature_node>& armature_node,
                  const math::transform& parent_transform,
                  const submesh_placements_t& placements,
                  node_index_lut_t& node_to_index_lut)
{
    armature_node->name = node->mName.C_Str();
    armature_node->local_transform = process_matrix(node->mTransformation);
    armature_node->children.resize(node->mNumChildren);
    armature_node->index = node_to_index_lut[armature_node->name];
    auto resolved_transform = parent_transform * armature_node->local_transform;

    for(uint32_t i = 0; i < node->mNumMeshes; ++i)
    {
        uint32_t submesh_index = node->mMeshes[i];
        armature_node->submeshes.emplace_back(submesh_index);

        auto& submesh = load_data.submeshes[submesh_index];
        const bool is_replaced = submesh_index < placements.size() && placements[submesh_index].has_value();
        const math::transform placement =
            is_replaced ? resolved_transform * *placements[submesh_index] : resolved_transform;

        auto transformed_bbox = math::bbox::mul(submesh.bbox, placement);
        load_data.bbox.add_point(transformed_bbox.min);
        load_data.bbox.add_point(transformed_bbox.max);
    }

    for(size_t i = 0; i < node->mNumChildren; ++i)
    {
        armature_node->children[i] = std::make_unique<mesh::armature_node>();
        process_node(scene,
                     load_data,
                     node->mChildren[i],
                     armature_node->children[i],
                     resolved_transform,
                     placements,
                     node_to_index_lut);
    }
}

/// Offset matrix each bone was first registered with: the one bind space all skinned meshes share.
using bind_offsets_t = std::unordered_map<std::string, aiMatrix4x4>;

/// Relative per-element tolerance under which two offset matrices count as equal.
constexpr float k_bind_space_tolerance = 1e-3f;

auto are_matrices_nearly_equal(const aiMatrix4x4& left, const aiMatrix4x4& right) -> bool
{
    for(unsigned int row = 0; row < 4; ++row)
    {
        for(unsigned int column = 0; column < 4; ++column)
        {
            const float value = left[row][column];
            if(std::abs(value - right[row][column]) > k_bind_space_tolerance * std::max(1.0f, std::abs(value)))
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief Transform taking @p mesh's vertices into the bind space its bones were first registered in.
 *
 * Assimp gives every mesh its own offset matrices (FBX: inverse bone bind * mesh transform), while the
 * engine keeps one bind pose per bone. Two meshes under different transforms therefore disagree on a
 * shared bone by inverse(first offset) * this offset - one matrix, the same for every shared bone.
 * Identity when the spaces agree; nullopt when the shared bones disagree with each other, where no
 * single re-expression exists.
 */
auto find_bind_space_correction(const aiMesh* mesh, const bind_offsets_t& offsets) -> std::optional<aiMatrix4x4>
{
    std::optional<aiMatrix4x4> correction;
    for(unsigned int i = 0; i < mesh->mNumBones; ++i)
    {
        const aiBone* bone = mesh->mBones[i];
        const auto registered = offsets.find(bone->mName.C_Str());
        if(registered == offsets.end())
        {
            continue;
        }
        const aiMatrix4x4 candidate = aiMatrix4x4(registered->second).Inverse() * bone->mOffsetMatrix;
        if(!correction)
        {
            correction = candidate;
        }
        else if(!are_matrices_nearly_equal(*correction, candidate))
        {
            return std::nullopt;
        }
    }
    return correction.value_or(aiMatrix4x4());
}

/// Re-express @p mesh (vertices, tangent frame, bone offsets) through @p correction.
void apply_bind_space_correction(aiMesh* mesh, const aiMatrix4x4& correction)
{
    const aiMatrix3x3 linear(correction);
    aiMatrix3x3 normal_matrix = linear;
    normal_matrix.Inverse().Transpose();
    for(unsigned int i = 0; i < mesh->mNumVertices; ++i)
    {
        mesh->mVertices[i] = correction * mesh->mVertices[i];
        if(mesh->HasNormals())
        {
            mesh->mNormals[i] = (normal_matrix * mesh->mNormals[i]).Normalize();
        }
        if(mesh->HasTangentsAndBitangents())
        {
            mesh->mTangents[i] = (linear * mesh->mTangents[i]).Normalize();
            mesh->mBitangents[i] = (linear * mesh->mBitangents[i]).Normalize();
        }
    }
    const aiMatrix4x4 inverse = aiMatrix4x4(correction).Inverse();
    for(unsigned int i = 0; i < mesh->mNumBones; ++i)
    {
        mesh->mBones[i]->mOffsetMatrix = mesh->mBones[i]->mOffsetMatrix * inverse;
    }
}

/**
 * @brief Bring a skinned mesh into the shared bind space, then register its bones. Returns the
 * transform from its node to the space its vertices now live in (nullopt: unchanged).
 */
auto align_to_shared_bind_space(aiMesh* mesh, bind_offsets_t& offsets) -> std::optional<math::transform>
{
    std::optional<math::transform> placement;
    const auto correction = find_bind_space_correction(mesh, offsets);
    if(!correction)
    {
        APPLOG_WARNING("Mesh Importer: Mesh '{}' binds shared bones inconsistently; it may skin incorrectly",
                       mesh->mName.C_Str());
    }
    else if(!are_matrices_nearly_equal(*correction, aiMatrix4x4()))
    {
        APPLOG_TRACE("Mesh Importer: Re-expressing mesh '{}' in the bind space of its shared bones",
                     mesh->mName.C_Str());
        apply_bind_space_correction(mesh, *correction);
        placement = math::transform(process_matrix(aiMatrix4x4(*correction).Inverse()));
    }
    for(unsigned int i = 0; i < mesh->mNumBones; ++i)
    {
        offsets.emplace(mesh->mBones[i]->mName.C_Str(), mesh->mBones[i]->mOffsetMatrix);
    }
    return placement;
}

void dfs_assign_indices(const aiNode* node,
                        node_index_lut_t& node_indices,
                        unsigned int& current_index)
{
    // Assign the current index to this node
    node_indices[node->mName.C_Str()] = current_index;

    // Increment the index for the next node
    current_index++;

    // Recursively visit all children (DFS)
    for(unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        dfs_assign_indices(node->mChildren[i], node_indices, current_index);
    }
}

} // namespace

void apply_import_facing_correction_to_load_data(mesh::load_data& load_data)
{
    if(!load_data.root_node)
    {
        return;
    }

    load_data.root_node->local_transform.rotate(math::radians(math::vec3{0.0f, 180.0f, 0.0f}));

    if(!load_data.bbox.is_populated())
    {
        return;
    }

    math::transform correction;
    correction.set_rotation(glm::angleAxis(math::pi<float>(), math::vec3(0.0f, 1.0f, 0.0f)));
    load_data.bbox = math::bbox::mul(load_data.bbox, correction);
}

void accumulate_bounds_from_armature(const mesh::load_data& load_data, math::bbox& out)
{
    if(!load_data.root_node)
    {
        return;
    }

    const std::function<void(const mesh::armature_node&, const math::transform&)> visit =
        [&](const mesh::armature_node& node, const math::transform& parent_transform)
    {
        const math::transform world_transform = parent_transform * node.local_transform;

        for(uint32_t submesh_index : node.submeshes)
        {
            if(submesh_index >= load_data.submeshes.size())
            {
                continue;
            }

            const auto transformed_bbox =
                math::bbox::mul(load_data.submeshes[submesh_index].bbox, world_transform);
            out.add_point(transformed_bbox.min);
            out.add_point(transformed_bbox.max);
        }

        for(const auto& child : node.children)
        {
            if(child)
            {
                visit(*child, world_transform);
            }
        }
    };

    visit(*load_data.root_node, math::transform::identity());
}

auto process_meshes(const aiScene* scene, mesh::load_data& load_data) -> submesh_placements_t
{
    submesh_placements_t placements(scene->mNumMeshes);
    bind_offsets_t bind_offsets;
    for(size_t i = 0; i < scene->mNumMeshes; ++i)
    {
        aiMesh* mesh = scene->mMeshes[i];
        if(mesh->HasBones())
        {
            placements[i] = align_to_shared_bind_space(mesh, bind_offsets);
        }
        process_mesh(mesh, load_data);
    }
    return placements;
}

void process_nodes(const aiScene* scene,
                   mesh::load_data& load_data,
                   const submesh_placements_t& placements,
                   node_index_lut_t& node_to_index_lut)
{
    if(scene->mRootNode == nullptr)
    {
        return;
    }
    load_data.bbox = {};
    load_data.root_node = std::make_unique<mesh::armature_node>();
    process_node(scene,
                 load_data,
                 scene->mRootNode,
                 load_data.root_node,
                 math::transform::identity(),
                 placements,
                 node_to_index_lut);
}

auto assign_node_indices(const aiScene* scene) -> node_index_lut_t
{
    node_index_lut_t node_indices;
    unsigned int current_index = 0;

    // Start DFS traversal from the root node
    if(scene->mRootNode)
    {
        dfs_assign_indices(scene->mRootNode, node_indices, current_index);
    }

    return node_indices;
}

} // namespace mesh_import
} // namespace importer
} // namespace unravel
