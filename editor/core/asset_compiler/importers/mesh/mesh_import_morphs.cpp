#include "mesh_import_morphs.h"

#include <logging/logging.h>
#include <string_utils/utils.h>

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <hpp/optional.hpp>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

namespace unravel
{
namespace importer
{
namespace mesh_import
{
namespace
{

/// The third component of every morph signature: a value no texture coordinate a file stores is likely to hold, so
/// remove_morph_signatures removes only what prepare_morph_targets wrote.
constexpr float k_signature_mark = -98765.5f;

/// Deltas are compared to a millionth of the mesh's unit.
constexpr double k_signature_step = 1e-6;

void hash_value(uint64_t& hash, int64_t value)
{
    for(int byte = 0; byte < 8; ++byte)
    {
        hash ^= uint64_t((value >> (byte * 8)) & 0xff);
        hash *= 1099511628211ull;
    }
}

void hash_vector(uint64_t& hash, const aiVector3D& value)
{
    hash_value(hash, std::llround(double(value.x) / k_signature_step));
    hash_value(hash, std::llround(double(value.y) / k_signature_step));
    hash_value(hash, std::llround(double(value.z) / k_signature_step));
}

/// A hash of how every anim mesh of @p mesh moves vertex @p vertex: equal for two vertices only targets moving them
/// alike.
auto get_morph_signature(const aiMesh* mesh, unsigned int vertex) -> uint64_t
{
    uint64_t hash = 14695981039346656037ull;
    for(unsigned int a = 0; a < mesh->mNumAnimMeshes; ++a)
    {
        const aiAnimMesh* shape = mesh->mAnimMeshes[a];
        if(shape->mNumVertices != mesh->mNumVertices)
        {
            continue;
        }
        if(shape->HasPositions())
        {
            hash_vector(hash, shape->mVertices[vertex] - mesh->mVertices[vertex]);
        }
        if(shape->HasNormals() && mesh->HasNormals())
        {
            hash_vector(hash, shape->mNormals[vertex] - mesh->mNormals[vertex]);
        }
    }
    return hash;
}

/// Writes each vertex's morph signature into the first texture coordinate set @p mesh does not use (welding compares
/// the sets from the first one on, up to the first missing one).
void add_morph_signature(aiMesh* mesh)
{
    const unsigned int set = mesh->GetNumUVChannels();
    if(set >= AI_MAX_NUMBER_OF_TEXTURECOORDS)
    {
        APPLOG_WARNING("Mesh Importer: the mesh {} uses every texture coordinate set, so welding may join vertices only "
                       "its morph targets separate.",
                       mesh->mName.C_Str());
        return;
    }
    mesh->mTextureCoords[set] = new aiVector3D[mesh->mNumVertices];
    mesh->mNumUVComponents[set] = 3;
    for(unsigned int v = 0; v < mesh->mNumVertices; ++v)
    {
        const uint64_t signature = get_morph_signature(mesh, v);
        mesh->mTextureCoords[set][v] =
            aiVector3D(float(signature & 0xfffffu), float((signature >> 20) & 0xfffffu), k_signature_mark);
    }
}

void remove_anim_meshes(aiMesh* mesh)
{
    for(unsigned int a = 0; a < mesh->mNumAnimMeshes; ++a)
    {
        delete mesh->mAnimMeshes[a];
    }
    delete[] mesh->mAnimMeshes;
    mesh->mAnimMeshes = nullptr;
    mesh->mNumAnimMeshes = 0;
}

void remove_morph_curves(aiAnimation* animation)
{
    for(unsigned int c = 0; c < animation->mNumMorphMeshChannels; ++c)
    {
        delete animation->mMorphMeshChannels[c];
    }
    delete[] animation->mMorphMeshChannels;
    animation->mMorphMeshChannels = nullptr;
    animation->mNumMorphMeshChannels = 0;
}

auto has_morph_targets(const aiScene& scene) -> bool
{
    for(unsigned int i = 0; i < scene.mNumMeshes; ++i)
    {
        if(scene.mMeshes[i]->mNumAnimMeshes > 0)
        {
            return true;
        }
    }
    return false;
}

/// Hashes a position by its bits, so vertices at exactly one rest position share a normal.
struct position_key
{
    uint32_t bits[3]{};

    auto operator==(const position_key& other) const -> bool
    {
        return std::memcmp(bits, other.bits, sizeof(bits)) == 0;
    }
};

struct position_key_hash
{
    auto operator()(const position_key& key) const -> size_t
    {
        return size_t(key.bits[0]) * 73856093u ^ size_t(key.bits[1]) * 19349663u ^ size_t(key.bits[2]) * 83492791u;
    }
};

/// The group of each vertex of @p mesh: vertices at one rest position (a UV seam) share one.
auto group_vertices_by_rest_position(const aiMesh* mesh, uint32_t& group_count) -> std::vector<uint32_t>
{
    std::unordered_map<position_key, uint32_t, position_key_hash> groups;
    std::vector<uint32_t> group_of(mesh->mNumVertices);
    for(unsigned int v = 0; v < mesh->mNumVertices; ++v)
    {
        position_key key;
        std::memcpy(key.bits, &mesh->mVertices[v], sizeof(key.bits));
        group_of[v] = groups.try_emplace(key, uint32_t(groups.size())).first->second;
    }
    group_count = uint32_t(groups.size());
    return group_of;
}

/// Smooth normals of @p positions over @p mesh's faces (area weighted), vertices of one group sharing theirs.
auto calculate_smooth_normals(const aiMesh* mesh,
                              const aiVector3D* positions,
                              const std::vector<uint32_t>& group_of,
                              uint32_t group_count) -> std::vector<math::vec3>
{
    std::vector<math::vec3> sums(group_count, math::vec3(0.0f));
    for(unsigned int f = 0; f < mesh->mNumFaces; ++f)
    {
        const aiFace& face = mesh->mFaces[f];
        if(face.mNumIndices != 3)
        {
            continue;
        }
        const aiVector3D& a = positions[face.mIndices[0]];
        const aiVector3D& b = positions[face.mIndices[1]];
        const aiVector3D& c = positions[face.mIndices[2]];
        const aiVector3D n = (b - a) ^ (c - a);
        for(unsigned int corner = 0; corner < 3; ++corner)
        {
            sums[group_of[face.mIndices[corner]]] += math::vec3(n.x, n.y, n.z);
        }
    }
    std::vector<math::vec3> normals(mesh->mNumVertices, math::vec3(0.0f));
    for(unsigned int v = 0; v < mesh->mNumVertices; ++v)
    {
        const math::vec3 sum = sums[group_of[v]];
        const float length = math::length(sum);
        normals[v] = length > 0.0f ? sum / length : math::vec3(0.0f);
    }
    return normals;
}

/// The rest shape's calculated normals of @p mesh, facing the way its own normals face.
struct calculated_rest
{
    std::vector<uint32_t> group_of;
    uint32_t group_count{0};
    std::vector<math::vec3> normals;
    float facing{1.0f};
};

auto calculate_rest_normals(const aiMesh* mesh) -> calculated_rest
{
    calculated_rest rest;
    rest.group_of = group_vertices_by_rest_position(mesh, rest.group_count);
    rest.normals = calculate_smooth_normals(mesh, mesh->mVertices, rest.group_of, rest.group_count);
    // The winding the faces were converted to may point the cross products inward; the file's normals say outward.
    if(mesh->HasNormals())
    {
        float agreement = 0.0f;
        for(unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            agreement += math::dot(rest.normals[v], math::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z));
        }
        rest.facing = agreement < 0.0f ? -1.0f : 1.0f;
    }
    return rest;
}

/// Whether the file's normals of target @p shape turn any normal of @p mesh. An exporter that writes a shape's normals
/// as zero deltas leaves Assimp's shape normals equal to the rest normals, so the target would never change lighting.
auto has_turned_normals(const aiMesh* mesh, const aiAnimMesh* shape) -> bool
{
    for(unsigned int v = 0; v < mesh->mNumVertices; ++v)
    {
        if((shape->mNormals[v] - mesh->mNormals[v]).Length() >= k_morph_normal_threshold)
        {
            return true;
        }
    }
    return false;
}

/// Where the normals of target @p shape come from: Import calculates them when the file stores none for the target or
/// only normals that do not turn.
auto resolve_normal_source(const aiMesh* mesh, const aiAnimMesh* shape, const morph_import_options& options)
    -> mesh_importer_meta::model_meta::morph_normals
{
    using morph_normals = mesh_importer_meta::model_meta::morph_normals;
    const bool has_file_normals = shape->HasNormals() && mesh->HasNormals() && has_turned_normals(mesh, shape);
    if(options.normals == morph_normals::import && !has_file_normals)
    {
        return morph_normals::calculate;
    }
    return options.normals;
}

/// The node @p name names, without the "*<geometry>" FBX gives a morph channel.
auto get_morph_channel_node_name(const std::string& name) -> std::string
{
    const size_t star = name.rfind('*');
    if(star == std::string::npos || star + 1 == name.size())
    {
        return name;
    }
    const bool is_index = std::all_of(name.begin() + long(star) + 1,
                                      name.end(),
                                      [](char c)
                                      {
                                          return std::isdigit(static_cast<unsigned char>(c)) != 0;
                                      });
    return is_index ? name.substr(0, star) : name;
}

/// The mesh with morph targets a morph channel named @p name drives: the first of its node's, or one of that name.
auto find_morph_mesh(const aiScene* scene, const std::string& name) -> const aiMesh*
{
    const std::string node_name = get_morph_channel_node_name(name);
    const aiNode* node = scene->mRootNode != nullptr ? scene->mRootNode->FindNode(node_name.c_str()) : nullptr;
    if(node != nullptr)
    {
        for(unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            const unsigned int index = node->mMeshes[i];
            if(index < scene->mNumMeshes && scene->mMeshes[index]->mNumAnimMeshes > 0)
            {
                return scene->mMeshes[index];
            }
        }
    }
    for(unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        const aiMesh* mesh = scene->mMeshes[i];
        if(mesh->mNumAnimMeshes > 0 && (node_name == mesh->mName.C_Str() || name == mesh->mName.C_Str()))
        {
            return mesh;
        }
    }
    return nullptr;
}

auto has_curve(const animation_clip& clip, const std::string& name) -> bool
{
    return std::any_of(clip.curves.begin(),
                       clip.curves.end(),
                       [&name](const animation_curve& curve)
                       {
                           return curve.name == name;
                       });
}

} // namespace

auto get_morph_import_options(const mesh_importer_meta& import_meta, const fs::path& path) -> morph_import_options
{
    const auto extension = string_utils::to_lower(path.extension().string());
    morph_import_options options;
    options.import_morph_targets = import_meta.model.import_morph_targets;
    options.normals = import_meta.model.morph_normal_mode;
    options.position_threshold = std::max(import_meta.model.morph_threshold, 0.0f);
    options.file_default_weights = extension == ".gltf" || extension == ".glb";
    options.fbx_channels = extension == ".fbx";
    return options;
}

auto prepare_morph_targets(aiScene& scene, const morph_import_options& options, uint32_t flags) -> uint32_t
{
    if(!options.import_morph_targets)
    {
        for(unsigned int i = 0; i < scene.mNumMeshes; ++i)
        {
            remove_anim_meshes(scene.mMeshes[i]);
        }
        for(unsigned int i = 0; i < scene.mNumAnimations; ++i)
        {
            remove_morph_curves(scene.mAnimations[i]);
        }
        return flags;
    }
    if(!has_morph_targets(scene))
    {
        return flags;
    }
    const uint32_t losing_steps = aiProcess_OptimizeMeshes | aiProcess_SplitLargeMeshes;
    if((flags & losing_steps) != 0)
    {
        APPLOG_INFO("Mesh Importer: Optimize Meshes and Split Large Meshes are off because the file has morph targets.");
        flags &= ~losing_steps;
    }
    if((flags & aiProcess_JoinIdenticalVertices) != 0)
    {
        for(unsigned int i = 0; i < scene.mNumMeshes; ++i)
        {
            aiMesh* mesh = scene.mMeshes[i];
            if(mesh->mNumAnimMeshes > 0 && mesh->HasPositions())
            {
                add_morph_signature(mesh);
            }
        }
    }
    return flags;
}

void remove_morph_signatures(aiScene& scene)
{
    for(unsigned int i = 0; i < scene.mNumMeshes; ++i)
    {
        aiMesh* mesh = scene.mMeshes[i];
        if(mesh->mNumVertices == 0)
        {
            continue;
        }
        // Post-processing can add a set after the signature (a generated mapping) or rebuild the list without its
        // component counts (texture transforms), so every set is checked by its mark, and the sets after the
        // signature move down one place.
        for(unsigned int set = 0; set < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++set)
        {
            if(mesh->mTextureCoords[set] == nullptr || mesh->mTextureCoords[set][0].z != k_signature_mark)
            {
                continue;
            }
            delete[] mesh->mTextureCoords[set];
            for(unsigned int next = set; next + 1 < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++next)
            {
                mesh->mTextureCoords[next] = mesh->mTextureCoords[next + 1];
                mesh->mNumUVComponents[next] = mesh->mNumUVComponents[next + 1];
            }
            mesh->mTextureCoords[AI_MAX_NUMBER_OF_TEXTURECOORDS - 1] = nullptr;
            mesh->mNumUVComponents[AI_MAX_NUMBER_OF_TEXTURECOORDS - 1] = 0;
            break;
        }
    }
}

auto get_morph_channel_shapes(const aiMesh* mesh, bool fbx_channels) -> std::vector<unsigned int>
{
    std::vector<unsigned int> shapes;
    shapes.reserve(mesh->mNumAnimMeshes);
    for(unsigned int a = 0; a < mesh->mNumAnimMeshes; ++a)
    {
        const float weight = mesh->mAnimMeshes[a]->mWeight;
        // The in-between shapes of an FBX channel come before its full shape, all with the channel's percentage.
        const bool next_in_same_channel = fbx_channels && weight != 1.0f && a + 1 < mesh->mNumAnimMeshes &&
                                          mesh->mAnimMeshes[a + 1]->mWeight == weight;
        if(!next_in_same_channel)
        {
            shapes.push_back(a);
        }
    }
    return shapes;
}

auto get_morph_target_name(const aiMesh* mesh, unsigned int shape, unsigned int channel) -> std::string
{
    const char* name = shape < mesh->mNumAnimMeshes ? mesh->mAnimMeshes[shape]->mName.C_Str() : "";
    return name[0] != '\0' ? std::string(name) : fmt::format("Target {}", channel);
}

void read_morph_targets(const aiMesh* mesh,
                        uint32_t vertex_offset,
                        const morph_import_options& options,
                        morph_target_builder& builder)
{
    using morph_normals = mesh_importer_meta::model_meta::morph_normals;
    if(mesh->mNumAnimMeshes == 0 || !mesh->HasPositions())
    {
        return;
    }
    const auto shapes = get_morph_channel_shapes(mesh, options.fbx_channels);
    if(shapes.size() < mesh->mNumAnimMeshes)
    {
        APPLOG_INFO("Mesh Importer: the mesh {} has blend shape channels with in-between shapes. Each channel keeps only "
                    "its full shape, and {} in-between shapes were left out.",
                    mesh->mName.C_Str(),
                    mesh->mNumAnimMeshes - shapes.size());
    }
    hpp::optional<calculated_rest> rest;
    for(unsigned int channel = 0; channel < shapes.size(); ++channel)
    {
        const aiAnimMesh* shape = mesh->mAnimMeshes[shapes[channel]];
        const std::string name = get_morph_target_name(mesh, shapes[channel], channel);
        if(!shape->HasPositions() || shape->mNumVertices != mesh->mNumVertices)
        {
            APPLOG_WARNING("Mesh Importer: the morph target {} of the mesh {} has no positions for its vertices, so it "
                           "was left out.",
                           name,
                           mesh->mName.C_Str());
            continue;
        }
        const auto normal_source = resolve_normal_source(mesh, shape, options);
        std::vector<math::vec3> shape_normals;
        if(normal_source == morph_normals::calculate)
        {
            if(!rest)
            {
                rest = calculate_rest_normals(mesh);
            }
            shape_normals = calculate_smooth_normals(mesh, shape->mVertices, rest->group_of, rest->group_count);
        }
        std::vector<morph_target_builder::vertex_delta> deltas;
        for(unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            const aiVector3D moved = shape->mVertices[v] - mesh->mVertices[v];
            math::vec3 normal(0.0f);
            if(normal_source == morph_normals::import)
            {
                const aiVector3D turned = shape->mNormals[v] - mesh->mNormals[v];
                normal = math::vec3(turned.x, turned.y, turned.z);
            }
            else if(normal_source == morph_normals::calculate)
            {
                normal = (shape_normals[v] - rest->normals[v]) * rest->facing;
            }
            const math::vec3 position(moved.x, moved.y, moved.z);
            if(position == math::vec3(0.0f) && normal == math::vec3(0.0f))
            {
                continue;
            }
            deltas.push_back({vertex_offset + v, position, normal});
        }
        const float default_weight = options.file_default_weights ? shape->mWeight : 0.0f;
        builder.add(name, default_weight, deltas);
    }
}

void read_morph_curves(const aiScene* scene,
                       const aiAnimation* animation,
                       double ticks_per_second,
                       const morph_import_options& options,
                       animation_clip& clip)
{
    const double duration = double(clip.duration.count());
    for(unsigned int c = 0; c < animation->mNumMorphMeshChannels; ++c)
    {
        const aiMeshMorphAnim* channel = animation->mMorphMeshChannels[c];
        const aiMesh* mesh = find_morph_mesh(scene, channel->mName.C_Str());
        if(mesh == nullptr)
        {
            APPLOG_WARNING("Mesh Importer: the morph target curves of {} in the animation {} drive no mesh with morph "
                           "targets, so they were left out.",
                           channel->mName.C_Str(),
                           animation->mName.C_Str());
            continue;
        }
        const auto shapes = get_morph_channel_shapes(mesh, options.fbx_channels);
        std::map<unsigned int, math::curve<float>> curves;
        for(unsigned int k = 0; k < channel->mNumKeys; ++k)
        {
            const aiMeshMorphKey& key = channel->mKeys[k];
            const double seconds = key.mTime / ticks_per_second;
            const float position = duration > 0.0 ? float(std::clamp(seconds / duration, 0.0, 1.0)) : 0.0f;
            for(unsigned int j = 0; j < key.mNumValuesAndWeights; ++j)
            {
                const unsigned int target = key.mValues[j];
                if(target < shapes.size())
                {
                    curves[target].get_channel(0).add_key(position, float(key.mWeights[j]));
                }
            }
        }
        for(auto& [target, curve] : curves)
        {
            std::string name = get_morph_target_name(mesh, shapes[target], target);
            if(!has_curve(clip, name))
            {
                clip.curves.push_back({std::move(name), std::move(curve)});
            }
        }
    }
}

} // namespace mesh_import
} // namespace importer
} // namespace unravel
