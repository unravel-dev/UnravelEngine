#include "mesh_importer.h"
#include "bimg/bimg.h"

#include <graphics/graphics.h>
#include <logging/logging.h>
#include <math/math.h>
#include <string_utils/utils.h>

#include <assimp/DefaultLogger.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <array>
#include <bx/allocator.h>
#include <bx/file.h>
#include <graphics/utils/bgfx_utils.h>

#include <algorithm>
#include <cmath>
#include <filesystem/filesystem.h>
#include <queue>
#include <tuple>

namespace unravel
{
namespace importer
{
namespace
{

// Forward declarations
void apply_texture_conversion(bimg::ImageContainer* image, const std::string& semantic, bool inverse);
void process_raw_texture_data(const aiTexture* assimp_tex, const fs::path& output_file,
                             const std::string& semantic, bool inverse);
void apply_specular_to_metallic_roughness_conversion(bimg::ImageContainer* image);

/**
 * @brief Per-material scalar/vector multipliers from the KHR_materials_pbrSpecularGlossiness
 * extension. The conversion bakes these into the produced base-color and MR textures so the
 * caller can set the material's base_color / metallic / roughness factors to identity and
 * avoid double-application at sample time.
 */
struct spec_gloss_factors_t
{
    float diffuse_r{1.0f};
    float diffuse_g{1.0f};
    float diffuse_b{1.0f};
    float diffuse_a{1.0f};
    float specular_r{1.0f};
    float specular_g{1.0f};
    float specular_b{1.0f};
    float glossiness{1.0f};
};

void apply_diffuse_to_base_color_conversion(bimg::ImageContainer* diffuse_image,
                                            const bimg::ImageContainer* specular_image,
                                            const spec_gloss_factors_t& factors,
                                            std::vector<uint8_t>* out_mr_rgba8 = nullptr);
auto image_has_meaningful_alpha(const uint8_t* image_data, uint32_t pixel_count, uint32_t bytes_per_pixel) -> bool;
auto perceived_brightness(float r, float g, float b) -> float;
auto solve_metallic(float perceived_diffuse, float perceived_specular, float one_minus_specular_strength) -> float;
auto convert_specular_gloss_to_metallic_roughness(const aiColor3D& diffuse_color,
                                                 const aiColor3D& specular_color,
                                                 float glossiness_factor) -> std::tuple<aiColor3D, float, float>;

/**
 * @brief Check that an image format is an uncompressed, non-float LDR layout we can
 * safely byte-walk in our per-pixel conversion routines. Floating-point/HDR and
 * block-compressed formats would silently corrupt if treated as 8-bit channels.
 */
inline auto is_supported_ldr_format(bimg::TextureFormat::Enum format) -> bool
{
    if(bimg::isCompressed(format) || bimg::isFloat(format))
    {
        return false;
    }
    uint32_t bpp = bimg::getBitsPerPixel(format);
    return bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32;
}

/**
 * @brief Process-wide allocator used by bimg's conversion / allocation entry points.
 * bgfx's entry::getAllocator() is in an anonymous namespace inside bgfx_utils.cpp so
 * we keep our own bx::DefaultAllocator instance here.
 */
inline auto get_bimg_allocator() -> bx::AllocatorI*
{
    static thread_local bx::DefaultAllocator allocator;
    return &allocator;
}

/**
 * @brief Ensure an image container is RGBA8 layout. If the source is anything else
 * (RGB8, R8, RG8 …) the result is a freshly allocated RGBA8 container — the caller
 * is responsible for freeing it. Returns nullptr on failure. Pass-through (no copy)
 * when the input is already RGBA8.
 *
 * Critical because the downstream PNG writer expects pitch = width*4 / RGBA8.
 * Feeding it a 3-bpp RGB8 buffer produces garbage in the saved file.
 */
auto ensure_rgba8(bimg::ImageContainer* image, bool& owns_result) -> bimg::ImageContainer*
{
    owns_result = false;
    if(!image)
    {
        return nullptr;
    }
    if(image->m_format == bimg::TextureFormat::RGBA8)
    {
        return image;
    }
    auto* converted = bimg::imageConvert(get_bimg_allocator(), bimg::TextureFormat::RGBA8, *image);
    if(!converted)
    {
        APPLOG_WARNING("Mesh Importer: Failed to convert image to RGBA8 (source format = {})",
                       bimg::getName(image->m_format));
        return nullptr;
    }
    owns_result = true;
    return converted;
}

auto has_rotation_channel(const aiAnimation* animation, const std::string& nodeName) -> bool
{
    if(!animation)
    {
        return false;
    }

    for(unsigned int ch = 0; ch < animation->mNumChannels; ++ch)
    {
        aiNodeAnim* channel = animation->mChannels[ch];
        if(!channel)
        {
            continue;
        }

        // Compare the channel's node name with the given nodeName.
        if(std::string(channel->mNodeName.C_Str()) == nodeName)
        {
            // If the channel has position keys, then the node is animated in translation.
            if(channel->mNumRotationKeys > 1)
            {
                return true;
            }
        }
    }

    return false;
}

auto has_rotation_channel(const aiScene* scene, const std::string& nodeName) -> bool
{
    for(unsigned int animIdx = 0; animIdx < scene->mNumAnimations; ++animIdx)
    {
        aiAnimation* animation = scene->mAnimations[animIdx];

        if(has_rotation_channel(animation, nodeName))
        {
            return true;
        }
    }
    return false;
}

auto has_trannslation_channel(const aiAnimation* animation, const std::string& nodeName) -> bool
{
    if(!animation)
    {
        return false;
    }

    for(unsigned int ch = 0; ch < animation->mNumChannels; ++ch)
    {
        aiNodeAnim* channel = animation->mChannels[ch];
        if(!channel)
        {
            continue;
        }

        // Compare the channel's node name with the given nodeName.
        if(std::string(channel->mNodeName.C_Str()) == nodeName)
        {
            // If the channel has position keys, then the node is animated in translation.
            if(channel->mNumPositionKeys > 1)
            {
                return true;
            }
        }
    }

    return false;
}

// Helper function to check whether a given node name has an animation channel
// with translation (position) keys.
auto has_trannslation_channel(const aiScene* scene, const std::string& nodeName) -> bool
{
    for(unsigned int animIdx = 0; animIdx < scene->mNumAnimations; ++animIdx)
    {
        aiAnimation* animation = scene->mAnimations[animIdx];

        if(has_trannslation_channel(animation, nodeName))
        {
            return true;
        }
    }
    return false;
}

enum channel_requirement
{
    translation,
    rotation
};

// Recursive, top-down search: returns the first node (in a depth-first search)
// that has an animation channel with translation keys.
auto find_first_animated_node_dfs(aiNode* node,
                                  const aiScene* scene,
                                  const aiAnimation* animation,
                                  channel_requirement req) -> aiNode*
{
    if(!node)
        return nullptr;

    switch(req)
    {
        case channel_requirement::translation:
            // Check if the current node is animated (i.e. has translation keys).
            if(has_trannslation_channel(animation, std::string(node->mName.C_Str())))
            {
                return node;
            }
            break;
        case channel_requirement::rotation:
            // Check if the current node is animated (i.e. has translation keys).
            if(has_rotation_channel(animation, std::string(node->mName.C_Str())))
            {
                return node;
            }
            break;
        default:
            // Check if the current node is animated (i.e. has translation keys).
            if(has_trannslation_channel(animation, std::string(node->mName.C_Str())))
            {
                return node;
            }
            break;
    }

    // Recursively search in the children.
    for(unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        aiNode* found = find_first_animated_node_dfs(node->mChildren[i], scene, animation, req);
        if(found)
        {
            return found;
        }
    }

    // No matching node found in this branch.
    return nullptr;
}

// Top-level function that starts at the scene's root node.
auto find_root_motion_node_dfs(const aiScene* scene, const aiAnimation* animation, channel_requirement req) -> aiNode*
{
    if(!scene || !scene->mRootNode)
    {
        return nullptr;
    }

    return find_first_animated_node_dfs(scene->mRootNode, scene, animation, req);
}

// Breadth-first search to find the first node (level-by-level) with translation animation.
auto find_first_animated_node_bfs(const aiScene* scene, const aiAnimation* animation, channel_requirement req)
    -> aiNode*
{
    if(!scene || !scene->mRootNode)
    {
        return nullptr;
    }

    std::queue<aiNode*> nodeQueue;
    nodeQueue.push(scene->mRootNode);

    while(!nodeQueue.empty())
    {
        aiNode* current = nodeQueue.front();
        nodeQueue.pop();

        switch(req)
        {
            case channel_requirement::translation:
                // Check if the current node is animated (i.e. has translation keys).
                if(has_trannslation_channel(animation, std::string(current->mName.C_Str())))
                {
                    return current;
                }
                break;
            case channel_requirement::rotation:
                // Check if the current node is animated (i.e. has translation keys).
                if(has_rotation_channel(animation, std::string(current->mName.C_Str())))
                {
                    return current;
                }
                break;
            default:
                // Check if the current node is animated (i.e. has translation keys).
                if(has_trannslation_channel(animation, std::string(current->mName.C_Str())))
                {
                    return current;
                }
                break;
        }

        // Check if the current node is animated (i.e. has translation keys).
        if(has_trannslation_channel(animation, std::string(current->mName.C_Str())))
        {
            return current;
        }

        // Enqueue all children of the current node.
        for(unsigned int i = 0; i < current->mNumChildren; ++i)
        {
            nodeQueue.push(current->mChildren[i]);
        }
    }

    // If no matching node is found, return nullptr.
    return nullptr;
}

// Top-level function that returns the root motion node using breadth-first search.
auto find_root_motion_node_bfs(const aiScene* scene, const aiAnimation* animation, channel_requirement req) -> aiNode*
{
    return find_first_animated_node_bfs(scene, animation, req);
}

// Helper function to interpolate between two keyframes for position
auto interpolate_position(float animation_time, const aiNodeAnim* node_anim) -> aiVector3D
{
    if(node_anim->mNumPositionKeys == 1)
    {
        return node_anim->mPositionKeys[0].mValue;
    }

    for(unsigned int i = 0; i < node_anim->mNumPositionKeys - 1; ++i)
    {
        if(animation_time < (float)node_anim->mPositionKeys[i + 1].mTime)
        {
            float time1 = (float)node_anim->mPositionKeys[i].mTime;
            float time2 = (float)node_anim->mPositionKeys[i + 1].mTime;
            float factor = (animation_time - time1) / (time2 - time1);
            const aiVector3D& start = node_anim->mPositionKeys[i].mValue;
            const aiVector3D& end = node_anim->mPositionKeys[i + 1].mValue;
            aiVector3D delta = end - start;
            return start + factor * delta;
        }
    }
    return node_anim->mPositionKeys[0].mValue; // Default to first position
}

// Helper function to interpolate between two keyframes for rotation
auto interpolate_rotation(float animation_time, const aiNodeAnim* node_anim) -> aiQuaternion
{
    if(node_anim->mNumRotationKeys == 1)
    {
        return node_anim->mRotationKeys[0].mValue;
    }

    for(unsigned int i = 0; i < node_anim->mNumRotationKeys - 1; ++i)
    {
        if(animation_time < (float)node_anim->mRotationKeys[i + 1].mTime)
        {
            float time1 = (float)node_anim->mRotationKeys[i].mTime;
            float time2 = (float)node_anim->mRotationKeys[i + 1].mTime;
            float factor = (animation_time - time1) / (time2 - time1);
            const aiQuaternion& start = node_anim->mRotationKeys[i].mValue;
            const aiQuaternion& end = node_anim->mRotationKeys[i + 1].mValue;
            aiQuaternion result;
            aiQuaternion::Interpolate(result, start, end, factor);
            return result.Normalize();
        }
    }
    return node_anim->mRotationKeys[0].mValue; // Default to first rotation
}

// Helper function to interpolate between two keyframes for scaling
auto interpolate_scaling(float animation_time, const aiNodeAnim* node_anim) -> aiVector3D
{
    if(node_anim->mNumScalingKeys == 1)
    {
        return node_anim->mScalingKeys[0].mValue;
    }

    for(unsigned int i = 0; i < node_anim->mNumScalingKeys - 1; ++i)
    {
        if(animation_time < (float)node_anim->mScalingKeys[i + 1].mTime)
        {
            float time1 = (float)node_anim->mScalingKeys[i].mTime;
            float time2 = (float)node_anim->mScalingKeys[i + 1].mTime;
            float factor = (animation_time - time1) / (time2 - time1);
            const aiVector3D& start = node_anim->mScalingKeys[i].mValue;
            const aiVector3D& end = node_anim->mScalingKeys[i + 1].mValue;
            aiVector3D delta = end - start;
            return start + factor * delta;
        }
    }
    return node_anim->mScalingKeys[0].mValue; // Default to first scaling
}

// Find the animation channel that matches the node name (bone)
auto find_node_anim(const aiAnimation* animation, const aiString& node_name) -> const aiNodeAnim*
{
    for(unsigned int i = 0; i < animation->mNumChannels; ++i)
    {
        const aiNodeAnim* node_anim = animation->mChannels[i];
        if(std::string(node_anim->mNodeName.C_Str()) == node_name.C_Str())
        {
            return node_anim;
        }
    }
    return nullptr;
}

// Recursively calculate the bone transform for the current node (bone)
auto calculate_bone_transform(const aiNode* node,
                              const aiString& bone_name,
                              const aiAnimation* animation,
                              float animation_time,
                              const aiMatrix4x4& parent_transform) -> aiMatrix4x4
{
    std::string node_name(node->mName.C_Str());

    // Find the corresponding animation channel for this bone/node
    const aiNodeAnim* node_anim = find_node_anim(animation, node->mName);

    // Local transformation matrix
    aiMatrix4x4 local_transform = node->mTransformation;

    // If we have animation data for this node, interpolate the transformation
    if(node_anim)
    {
        // Interpolate translation, rotation, and scaling
        aiVector3D interpolated_position = interpolate_position(animation_time, node_anim);
        aiQuaternion interpolated_rotation = interpolate_rotation(animation_time, node_anim);
        aiVector3D interpolated_scaling = interpolate_scaling(animation_time, node_anim);

        // Build the transformation matrix from interpolated values
        aiMatrix4x4 position_matrix;
        aiMatrix4x4::Translation(interpolated_position, position_matrix);

        aiMatrix4x4 rotation_matrix = aiMatrix4x4(interpolated_rotation.GetMatrix());

        aiMatrix4x4 scaling_matrix;
        aiMatrix4x4::Scaling(interpolated_scaling, scaling_matrix);

        // Combine them into a single local transformation matrix
        local_transform = position_matrix * rotation_matrix * scaling_matrix;
    }

    // Combine with parent transformation
    aiMatrix4x4 global_transform = parent_transform * local_transform;

    // If this node is the bone we're looking for, return the global transformation
    if(node_name == bone_name.C_Str())
    {
        return global_transform;
    }

    // Recursively calculate the bone transform for all child nodes
    for(unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        auto child_transform =
            calculate_bone_transform(node->mChildren[i], bone_name, animation, animation_time, global_transform);
        if(child_transform != aiMatrix4x4())
        {
            return child_transform;
        }
    }

    // If not found, return identity matrix
    return aiMatrix4x4();
}

using animation_bounding_box_map = std::unordered_map<const aiAnimation*, std::vector<math::bbox>>;

auto transform_point(const aiMatrix4x4& transform, const aiVector3D& point) -> math::vec3
{
    aiVector3D transformed_point = transform * point;
    return math::vec3(transformed_point.x, transformed_point.y, transformed_point.z);
}

auto get_transformed_vertices(const aiMesh* mesh,
                              const aiScene* scene,
                              float time_in_seconds,
                              const aiAnimation* animation) -> std::vector<math::vec3>
{
    std::vector<math::vec3> transformed_vertices(mesh->mNumVertices, math::vec3(0.0f));

    // Iterate over bones in the mesh using parallel execution
    std::for_each(
        //poolstl::par,//std::execution::par,
        mesh->mBones,
        mesh->mBones + mesh->mNumBones,
        [&](const aiBone* bone)
        {
            aiMatrix4x4 bone_offset = bone->mOffsetMatrix;

            // Calculate or retrieve the cached bone transformation for this frame
            aiMatrix4x4 bone_transform =
                calculate_bone_transform(scene->mRootNode, bone->mName, animation, time_in_seconds, aiMatrix4x4());

            // Apply the bone transformation to vertices influenced by this bone
            std::for_each(bone->mWeights,
                          bone->mWeights + bone->mNumWeights,
                          [&](const aiVertexWeight& weight)
                          {
                              unsigned int vertex_id = weight.mVertexId;
                              float weight_value = weight.mWeight;

                              aiVector3D position = mesh->mVertices[vertex_id];
                              math::vec3 transformed_pos = transform_point(bone_transform * bone_offset, position);

                              // Accumulate the influence of this bone for each vertex
                              transformed_vertices[vertex_id] += transformed_pos * weight_value;
                          });
        });

    return transformed_vertices;
}

// Calculate the bounding box in parallel
auto calculate_bounding_box(const std::vector<math::vec3>& vertices) -> math::bbox
{
    math::bbox box;

    // Use parallel execution to find the min/max extents of the bounding box
    std::for_each(vertices.begin(),
                  vertices.end(),
                  [&](const math::vec3& vertex)
                  {
                      box.add_point(vertex);
                  });

    return box;
}

// Recursive function to propagate bone influence to child nodes
void propagate_bone_influence(const aiNode* node, std::unordered_set<std::string>& affected_bones)
{
    // Mark this node as affected
    affected_bones.insert(node->mName.C_Str());

    // Recursively propagate to all child nodes
    for(unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        propagate_bone_influence(node->mChildren[i], affected_bones);
    }
}

// Helper function to collect directly and indirectly affected bones (nodes) by the animation
auto get_affected_bones_and_children(const aiScene* scene, const aiAnimation* animation)
    -> std::unordered_set<std::string>
{
    std::unordered_set<std::string> affected_bones;

    // Step 1: Collect directly affected bones (from animation channels)
    for(unsigned int i = 0; i < animation->mNumChannels; ++i)
    {
        const aiNodeAnim* node_anim = animation->mChannels[i];
        affected_bones.insert(node_anim->mNodeName.C_Str());

        // Step 2: Find the corresponding node in the scene and propagate influence to its children
        const aiNode* affected_node = scene->mRootNode->FindNode(node_anim->mNodeName);
        if(affected_node)
        {
            propagate_bone_influence(affected_node, affected_bones); // Recursively mark all children
        }
    }

    return affected_bones;
}

// Function to check if a mesh is affected by the animation (directly or indirectly)
auto is_mesh_affected_by_animation(const aiMesh* mesh, const std::unordered_set<std::string>& affected_bones) -> bool
{
    for(unsigned int i = 0; i < mesh->mNumBones; ++i)
    {
        if(affected_bones.find(mesh->mBones[i]->mName.C_Str()) != affected_bones.end())
        {
            return true; // This mesh is influenced by at least one bone affected by the animation
        }
    }
    return false; // No bones from this mesh are affected by the animation
}

auto get_affected_meshes(const aiScene* scene,
                         const aiAnimation* animation,
                         const std::unordered_set<std::string>& affected_bones)
{
    std::vector<const aiMesh*> affected_meshes;
    for(unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index)
    {
        const aiMesh* mesh = scene->mMeshes[mesh_index];

        // Skip the mesh if it is not affected by the animation
        if(is_mesh_affected_by_animation(mesh, affected_bones))
        {
            affected_meshes.emplace_back(mesh);
        }
    }

    return affected_meshes;
}

// Main function to compute bounding boxes for animations, skipping unaffected meshes
auto compute_bounding_boxes_for_animations(const aiScene* scene, float sample_interval = 0.2f)
    -> animation_bounding_box_map
{
    APPLOG_TRACE_PERF(std::chrono::seconds);

    animation_bounding_box_map animation_bounding_boxes;

    if(!scene->HasAnimations())
    {
        return animation_bounding_boxes;
    }

    float total_steps = 0;
    for(unsigned int anim_index = 0; anim_index < scene->mNumAnimations; ++anim_index)
    {
        const aiAnimation* animation = scene->mAnimations[anim_index];

        animation_bounding_boxes[animation].clear();

        float animation_duration = (float)animation->mDuration;
        float ticks_per_second = (animation->mTicksPerSecond != 0.0f) ? (float)animation->mTicksPerSecond : 25.0f;
        float steps = animation_duration / (sample_interval * ticks_per_second);
        total_steps += steps;
    }

    std::atomic<size_t> current_steps = 0;

    std::for_each(
        //poolstl::par,//std::execution::par,
        scene->mAnimations,
        scene->mAnimations + scene->mNumAnimations,
        [&](const aiAnimation* animation)
        {
            float animation_duration = (float)animation->mDuration;
            float ticks_per_second = (animation->mTicksPerSecond != 0.0f) ? (float)animation->mTicksPerSecond : 25.0f;
            float steps = animation_duration / (sample_interval * ticks_per_second);

            auto& boxes = animation_bounding_boxes[animation];
            boxes.reserve(size_t(steps));

            // Collect the bones affected by the animation (both direct and indirect)
            auto affected_bones = get_affected_bones_and_children(scene, animation);
            auto affected_meshes = get_affected_meshes(scene, animation, affected_bones);
            // For each keyframe (or sample the animation at regular intervals)
            // for(float time = 0.0f; time <= animation_duration; time += (sample_interval * ticks_per_second))
            {
                float time = 0.0f;
                float percent = (float(current_steps) / total_steps) * 100.0f;

                for(const auto& mesh : affected_meshes)
                {
                    // Get transformed vertices for this time/frame
                    auto transformed_vertices = get_transformed_vertices(mesh, scene, time, animation);

                    // Compute the bounding box for this frame
                    auto frame_bounding_box = calculate_bounding_box(transformed_vertices);

                    // Inflate the box by some margin to account for skipped frames
                    frame_bounding_box.inflate(frame_bounding_box.get_extents() * 0.05f);

                    // Store the bounding box (for later use)
                    boxes.push_back(frame_bounding_box);
                }

                // APPLOG_TRACE("Mesh Importer : Animation precompute bounding box progress {:.2f}%", percent);
                current_steps++;
            }
        });

    return animation_bounding_boxes;
}

// Helper function to get the file extension from the compressed texture format

auto get_texture_extension_from_texture(const aiTexture* texture) -> std::string
{
    if(texture->achFormatHint[0] != '\0')
    {
        return std::string(".") + texture->achFormatHint;
    }
    return ".tga"; // Fallback extension raw
}

auto get_texture_extension(const aiTexture* texture) -> std::string
{
    auto extension = get_texture_extension_from_texture(texture);

    if(extension == ".jpg" || extension == ".jpeg")
    {
        extension = ".dds";
    }

    return extension;
}

auto get_embedded_texture_name(const aiTexture* texture,
                               size_t index,
                               const fs::path& filename,
                               const std::string& semantic) -> std::string
{
    return fmt::format("[{}] {} {}{}", index, semantic, filename.string(), get_texture_extension(texture));
}

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
    bool has_position = load_data.vertex_format.has(gfx::attribute::Position);
    bool has_normal = load_data.vertex_format.has(gfx::attribute::Normal);
    bool has_bitangent = load_data.vertex_format.has(gfx::attribute::Bitangent);
    bool has_tangent = load_data.vertex_format.has(gfx::attribute::Tangent);
    bool has_texcoord0 = load_data.vertex_format.has(gfx::attribute::TexCoord0);
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

            gfx::vertex_pack(position, false, gfx::attribute::Position, load_data.vertex_format, current_vertex_ptr);

            submesh.bbox.add_point(math::vec3(position[0], position[1], position[2]));
        }

        // tex coords

        if(has_texcoord0)
        {
            float textureCoords[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if(mesh->HasTextureCoords(0))
            {
                std::memcpy(textureCoords, &mesh->mTextureCoords[0][i], sizeof(aiVector2D));

                gfx::vertex_pack(textureCoords,
                                 true,
                                 gfx::attribute::TexCoord0,
                                 load_data.vertex_format,
                                 current_vertex_ptr);
            }
            else
            {
                gfx::vertex_pack(textureCoords,
                                 true,
                                 gfx::attribute::TexCoord0,
                                 load_data.vertex_format,
                                 current_vertex_ptr);
            }
        }


        ////normals
        math::vec4 normal{};
        if(mesh->HasNormals() && has_normal)
        {
            std::memcpy(math::value_ptr(normal), &mesh->mNormals[i], sizeof(aiVector3D));

            gfx::vertex_pack(math::value_ptr(normal),
                             true,
                             gfx::attribute::Normal,
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
                
            gfx::vertex_pack(math::value_ptr(tangent),
            true,
            gfx::attribute::Tangent,
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

            gfx::vertex_pack(math::value_ptr(bitangent),
                             true,
                             gfx::attribute::Bitangent,
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
            }
        }
    }
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
    load_data.material_count = std::max(load_data.material_count, submesh.data_group_id + 1);

    process_faces(mesh, submesh.vertex_start, load_data);
    process_bones(mesh, submesh.vertex_start, load_data);
    process_vertices(mesh, load_data);
}

void process_meshes(const aiScene* scene, mesh::load_data& load_data)
{
    for(size_t i = 0; i < scene->mNumMeshes; ++i)
    {
        aiMesh* mesh = scene->mMeshes[i];
        process_mesh(mesh, load_data);
    }
}

void process_node(const aiScene* scene,
                  mesh::load_data& load_data,
                  const aiNode* node,
                  const std::unique_ptr<mesh::armature_node>& armature_node,
                  const math::transform& parent_transform,
                  std::unordered_map<std::string, unsigned int>& node_to_index_lut)
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

        auto transformed_bbox = math::bbox::mul(submesh.bbox, resolved_transform);
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
                     node_to_index_lut);
    }
}

void process_nodes(const aiScene* scene,
                   mesh::load_data& load_data,
                   std::unordered_map<std::string, unsigned int>& node_to_index_lut)
{
    size_t index = 0;
    if(scene->mRootNode != nullptr)
    {
        load_data.bbox = {};
        load_data.root_node = std::make_unique<mesh::armature_node>();

        process_node(scene,
                     load_data,
                     scene->mRootNode,
                     load_data.root_node,
                     math::transform::identity(),
                     node_to_index_lut);

        auto get_axis = [&](const std::string& name, math::vec3 fallback)
        {
            if(!scene->mMetaData)
            {
                return fallback;
            }

            int axis = 0;
            if(!scene->mMetaData->Get<int>(name, axis))
            {
                return fallback;
            }
            int axis_sign = 1;
            if(!scene->mMetaData->Get<int>(name + "Sign", axis_sign))
            {
                return fallback;
            }
            math::vec3 result{0.0f, 0.0f, 0.0f};

            if(axis < 0 || axis >= 3)
            {
                return fallback;
            }

            result[axis] = float(axis_sign);

            return result;
        };
        auto x_axis = get_axis("CoordAxis", {1.0f, 0.0f, 0.0f});
        auto y_axis = get_axis("UpAxis", {0.0f, 1.0f, 0.0f});
        auto z_axis = get_axis("FrontAxis", {0.0f, 0.0f, 1.0f});
        // load_data.root_node->local_transform.set_rotation(x_axis, y_axis, z_axis);
    }
}

void dfs_assign_indices(const aiNode* node,
                        std::unordered_map<std::string, unsigned int>& node_indices,
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

auto assign_node_indices(const aiScene* scene) -> std::unordered_map<std::string, unsigned int>
{
    std::unordered_map<std::string, unsigned int> node_indices;
    unsigned int current_index = 0;

    // Start DFS traversal from the root node
    if(scene->mRootNode)
    {
        dfs_assign_indices(scene->mRootNode, node_indices, current_index);
    }

    return node_indices;
}

auto is_node_a_bone(const std::string& node_name, const aiScene* scene) -> bool
{
    for(unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        const aiMesh* mesh = scene->mMeshes[i];
        for(unsigned int j = 0; j < mesh->mNumBones; ++j)
        {
            if(mesh->mBones[j]->mName.C_Str() == node_name)
            {
                return true;
            }
        }
    }
    return false;
}

auto is_node_a_parent_of_bone(const std::string& node_name, const aiScene* scene) -> bool
{
    for(unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        const aiMesh* mesh = scene->mMeshes[i];
        for(unsigned int j = 0; j < mesh->mNumBones; ++j)
        {
            const aiNode* bone_node = scene->mRootNode->FindNode(mesh->mBones[j]->mName);
            const aiNode* current_node = bone_node;

            while(current_node != nullptr)
            {
                if(current_node->mName.C_Str() == node_name)
                {
                    return true;
                }
                current_node = current_node->mParent;
            }
        }
    }
    return false;
}

auto is_node_a_submesh(const std::string& node_name, const aiScene* scene) -> bool
{
    const aiNode* node = scene->mRootNode->FindNode(node_name.c_str());
    return node != nullptr && node->mNumMeshes > 0;
}

auto is_node_a_parent_of_submesh(const std::string& node_name, const aiScene* scene) -> bool
{
    const aiNode* root = scene->mRootNode;

    for(unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        const aiMesh* mesh = scene->mMeshes[i];
        const aiNode* submesh_node = root->FindNode(mesh->mName);
        const aiNode* current_node = submesh_node;

        while(current_node != nullptr)
        {
            if(current_node->mName.C_Str() == node_name)
            {
                return true;
            }
            current_node = current_node->mParent;
        }
    }
    return false;
}

void process_animation(const aiScene* scene,
                       const fs::path& filename,
                       const aiAnimation* assimp_anim,
                       mesh::load_data& load_data,
                       std::unordered_map<std::string, unsigned int>& node_to_index_lut,
                       animation_clip& anim)
{
    auto fixed_name = filename.string() + "_" + string_utils::replace(assimp_anim->mName.C_Str(), ".", "_");
    anim.name = fixed_name;
    auto ticks_per_second = assimp_anim->mTicksPerSecond;
    if(ticks_per_second < 0.001)
    {
        ticks_per_second = 25.0;
    }

    auto ticks = assimp_anim->mDuration;

    anim.duration = decltype(anim.duration)(ticks / ticks_per_second);

    if(assimp_anim->mNumChannels > 0)
    {
        anim.channels.reserve(assimp_anim->mNumChannels);
    }
    bool needs_sort = false;

    size_t skipped = 0;
    for(size_t i = 0; i < assimp_anim->mNumChannels; ++i)
    {
        const aiNodeAnim* assimp_node_anim = assimp_anim->mChannels[i];

        bool is_bone = is_node_a_bone(assimp_node_anim->mNodeName.C_Str(), scene);
        bool is_parent_of_bone = is_node_a_parent_of_bone(assimp_node_anim->mNodeName.C_Str(), scene);
        bool is_submesh = is_node_a_submesh(assimp_node_anim->mNodeName.C_Str(), scene);
        bool is_parent_of_submesh = is_node_a_parent_of_submesh(assimp_node_anim->mNodeName.C_Str(), scene);

        bool is_relevant = is_bone || is_parent_of_bone || is_submesh || is_parent_of_submesh;

        // skip frames for non relevant nodes
        if(!is_relevant)
        {
            skipped++;
            continue;
        }

        auto& node_anim = anim.channels.emplace_back();
        node_anim.node_name = assimp_node_anim->mNodeName.C_Str();
        node_anim.node_index = node_to_index_lut[node_anim.node_name];
        if(!needs_sort && anim.channels.size() > 1)
        {
            auto& prev_node_anim = anim.channels[anim.channels.size() - 2];
            if(node_anim.node_index < prev_node_anim.node_index)
            {
                needs_sort = true;
            }
        }

        if(assimp_node_anim->mNumPositionKeys > 0)
        {
            node_anim.position_keys.resize(assimp_node_anim->mNumPositionKeys);
        }

        for(size_t idx = 0; idx < assimp_node_anim->mNumPositionKeys; ++idx)
        {
            const auto& anim_key = assimp_node_anim->mPositionKeys[idx];
            auto& key = node_anim.position_keys[idx];
            key.time = decltype(key.time)(anim_key.mTime / ticks_per_second);
            key.value.x = anim_key.mValue.x;
            key.value.y = anim_key.mValue.y;
            key.value.z = anim_key.mValue.z;
        }

        if(assimp_node_anim->mNumRotationKeys > 0)
        {
            node_anim.rotation_keys.resize(assimp_node_anim->mNumRotationKeys);
        }

        for(size_t idx = 0; idx < assimp_node_anim->mNumRotationKeys; ++idx)
        {
            const auto& anim_key = assimp_node_anim->mRotationKeys[idx];
            auto& key = node_anim.rotation_keys[idx];
            key.time = decltype(key.time)(anim_key.mTime / ticks_per_second);
            key.value.x = anim_key.mValue.x;
            key.value.y = anim_key.mValue.y;
            key.value.z = anim_key.mValue.z;
            key.value.w = anim_key.mValue.w;
        }

        if(assimp_node_anim->mNumScalingKeys > 0)
        {
            node_anim.scaling_keys.resize(assimp_node_anim->mNumScalingKeys);
        }

        for(size_t idx = 0; idx < assimp_node_anim->mNumScalingKeys; ++idx)
        {
            const auto& anim_key = assimp_node_anim->mScalingKeys[idx];
            auto& key = node_anim.scaling_keys[idx];
            key.time = decltype(key.time)(anim_key.mTime / ticks_per_second);
            key.value.x = anim_key.mValue.x;
            key.value.y = anim_key.mValue.y;
            key.value.z = anim_key.mValue.z;
        }
    }

    auto root_motion_translation_candidate =
        find_root_motion_node_bfs(scene, assimp_anim, channel_requirement::translation);
    auto root_motion_rotation_candidate = find_root_motion_node_bfs(scene, assimp_anim, channel_requirement::rotation);

    if(root_motion_translation_candidate)
    {
        anim.root_motion.position_node_name = root_motion_translation_candidate->mName.C_Str();
        anim.root_motion.position_node_index = node_to_index_lut[anim.root_motion.position_node_name];
    }
    if(root_motion_rotation_candidate)
    {
        anim.root_motion.rotation_node_name = root_motion_rotation_candidate->mName.C_Str();
        anim.root_motion.rotation_node_index = node_to_index_lut[anim.root_motion.rotation_node_name];
    }

    if(needs_sort)
    {
        std::sort(anim.channels.begin(),
                  anim.channels.end(),
                  [](const auto& lhs, const auto& rhs)
                  {
                      return lhs.node_index < rhs.node_index;
                  });
    }

    APPLOG_TRACE("Mesh Importer : Animation {} discarded {} non relevat node keys", anim.name, skipped);
}
void process_animations(const aiScene* scene,
                        const fs::path& filename,
                        mesh::load_data& load_data,
                        std::unordered_map<std::string, unsigned int>& node_to_index_lut,
                        std::vector<animation_clip>& animations)
{
    if(scene->mNumAnimations > 0)
    {
        animations.resize(scene->mNumAnimations);
    }

    for(size_t i = 0; i < scene->mNumAnimations; ++i)
    {
        const aiAnimation* assimp_anim = scene->mAnimations[i];
        auto& anim = animations[i];
        process_animation(scene, filename, assimp_anim, load_data, node_to_index_lut, anim);
    }
}

void process_embedded_texture(const aiTexture* assimp_tex,
                              size_t assimp_tex_idx,
                              const fs::path& filename,
                              const fs::path& output_dir,
                              std::vector<imported_texture>& textures)
{
    imported_texture texture{};
    // Search backwards: the caller just pushed the target entry at the back.
    auto rit = std::find_if(textures.rbegin(),
                            textures.rend(),
                            [&](const imported_texture& texture)
                            {
                                return texture.embedded_index == static_cast<int>(assimp_tex_idx);
                            });
    if(rit != textures.rend())
    {
        if(rit->process_count > 0)
        {
            return;
        }

        rit->process_count++;
        texture = *rit;
    }
    else if(assimp_tex->mFilename.length > 0)
    {
        texture.name = fs::path(assimp_tex->mFilename.C_Str()).filename().string();
    }
    else
    {
        texture.name = get_embedded_texture_name(assimp_tex, assimp_tex_idx, filename, "Texture");
    }

    fs::path output_file = output_dir / texture.name;

    if(assimp_tex->pcData)
    {
        bool compressed = assimp_tex->mHeight == 0;
        bool raw = assimp_tex->mHeight > 0;

        if(compressed)
        {
            // Compressed texture (e.g., PNG, JPEG)
            size_t texture_size = assimp_tex->mWidth;

            // Parse the image using bimg
            bimg::ImageContainer* image = imageLoad(assimp_tex->pcData, static_cast<uint32_t>(texture_size));
            if(image)
            {
                // Apply workflow-specific texture conversions
                apply_texture_conversion(image, texture.semantic, texture.inverse);

                imageSave(output_file.string().c_str(), image);

                bimg::imageFree(image);
            }
        }
        else if(raw)
        {
            // Uncompressed texture (e.g., raw RGBA)
            // For raw data, we need to process it differently
            process_raw_texture_data(assimp_tex, output_file, texture.semantic, texture.inverse);
        }
    }
}

/**
 * @brief Pixel transformation functions for different texture formats
 */
namespace pixel_transforms
{
    /**
     * @brief Quantize a normalized float [0,1] to uint8 with proper rounding.
     * Using truncation (static_cast<uint8_t>(x * 255.0f)) introduces a half-LSB
     * bias toward zero that compounds when a texture is re-converted.
     */
    inline auto to_uint8(float value) -> uint8_t
    {
        return static_cast<uint8_t>(std::lround(math::clamp(value, 0.0f, 1.0f) * 255.0f));
    }

    /**
     * @brief Transform a single pixel based on format and transformation function
     */
    template<typename TransformFunc>
    void transform_pixel(uint8_t* pixel_data, uint32_t bytes_per_pixel, TransformFunc transform_func)
    {
        if (bytes_per_pixel >= 4)
        {
            // RGBA format
            float r = pixel_data[0] / 255.0f;
            float g = pixel_data[1] / 255.0f;
            float b = pixel_data[2] / 255.0f;
            float a = pixel_data[3] / 255.0f;

            auto [new_r, new_g, new_b, new_a] = transform_func(r, g, b, a);

            pixel_data[0] = to_uint8(new_r);
            pixel_data[1] = to_uint8(new_g);
            pixel_data[2] = to_uint8(new_b);
            pixel_data[3] = to_uint8(new_a);
        }
        else if (bytes_per_pixel >= 3)
        {
            // RGB format
            float r = pixel_data[0] / 255.0f;
            float g = pixel_data[1] / 255.0f;
            float b = pixel_data[2] / 255.0f;
            float a = 1.0f; // Default alpha

            auto [new_r, new_g, new_b, new_a] = transform_func(r, g, b, a);

            pixel_data[0] = to_uint8(new_r);
            pixel_data[1] = to_uint8(new_g);
            pixel_data[2] = to_uint8(new_b);
        }
        else if (bytes_per_pixel == 2)
        {
            // Grayscale + Alpha format
            float luminance = pixel_data[0] / 255.0f;
            float a = pixel_data[1] / 255.0f;

            auto [new_r, new_g, new_b, new_a] = transform_func(luminance, luminance, luminance, a);

            pixel_data[0] = to_uint8(new_r); // Use red as luminance
            pixel_data[1] = to_uint8(new_a);
        }
        else if (bytes_per_pixel == 1)
        {
            // Grayscale format
            float luminance = pixel_data[0] / 255.0f;

            auto [new_r, new_g, new_b, new_a] = transform_func(luminance, luminance, luminance, 1.0f);

            pixel_data[0] = to_uint8(new_r);
        }
    }

    /**
     * @brief Approximate metallic from specular-only pixel using perceived brightness.
     * Without the corresponding diffuse pixel we assume a mid-grey diffuse (0.5) for
     * the quadratic solve. This gives a much better approximation than a linear remap.
     */
    auto specular_to_metallic_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        constexpr float assumed_diffuse = 0.5f;
        float max_specular = std::max({r, g, b});
        float one_minus_specular_strength = 1.0f - max_specular;
        float perc_diffuse = perceived_brightness(assumed_diffuse, assumed_diffuse, assumed_diffuse);
        float perc_specular = perceived_brightness(r, g, b);
        float metallic = solve_metallic(perc_diffuse, perc_specular, one_minus_specular_strength);
        return std::make_tuple(metallic, metallic, metallic, 1.0f);
    }

    /**
     * @brief Convert glossiness to roughness
     */
    auto gloss_to_roughness_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        // This semantic is only used for dedicated glossiness maps (aiTextureType_SHININESS).
        // Simply invert RGB: Roughness = 1 - Glossiness. Preserve alpha.
        return std::make_tuple(1.0f - r, 1.0f - g, 1.0f - b, a);
    }

    /**
     * @brief Convert specular texture alpha (gloss) to roughness.
     * Used when the texture is known to carry glossiness in the alpha channel.
     */
    auto specular_alpha_to_roughness_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        float roughness = 1.0f - a;
        return std::make_tuple(roughness, roughness, roughness, 1.0f);
    }

    /**
     * @brief Convert specular intensity to roughness estimate.
     * Used when the texture has no meaningful alpha channel.
     */
    auto specular_intensity_to_roughness_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        float specular_intensity = (r + g + b) / 3.0f;
        float roughness = 1.0f - specular_intensity;
        return std::make_tuple(roughness, roughness, roughness, 1.0f);
    }

    /**
     * @brief Shared helper: approximate metallic from specular-only pixel.
     * Uses the quadratic solver with an assumed mid-grey diffuse.
     */
    auto compute_metallic_from_specular(float r, float g, float b) -> float
    {
        constexpr float assumed_diffuse = 0.5f;
        float max_specular = std::max({r, g, b});
        float one_minus_specular_strength = 1.0f - max_specular;
        float perc_diffuse = perceived_brightness(assumed_diffuse, assumed_diffuse, assumed_diffuse);
        float perc_specular = perceived_brightness(r, g, b);
        return solve_metallic(perc_diffuse, perc_specular, one_minus_specular_strength);
    }

    /**
     * @brief Convert specular to combined metallic/roughness using alpha as gloss.
     * glTF convention: R=Occlusion(unused), G=Roughness, B=Metallic, A=1.0
     */
    auto specular_to_metallic_roughness_alpha_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        float metallic = compute_metallic_from_specular(r, g, b);
        float roughness = 1.0f - a;
        return std::make_tuple(1.0f, roughness, metallic, 1.0f);
    }

    /**
     * @brief Convert specular to combined metallic/roughness using intensity for roughness.
     * glTF convention: R=Occlusion(unused), G=Roughness, B=Metallic, A=1.0
     */
    auto specular_to_metallic_roughness_intensity_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        float metallic = compute_metallic_from_specular(r, g, b);
        float avg_specular = (r + g + b) / 3.0f;
        float roughness = 1.0f - avg_specular;
        return std::make_tuple(1.0f, roughness, metallic, 1.0f);
    }

    /**
     * @brief Simple inversion transformation
     */
    auto simple_invert_pixel(float r, float g, float b, float a) -> std::tuple<float, float, float, float>
    {
        return std::make_tuple(1.0f - r, 1.0f - g, 1.0f - b, 1.0f - a);
    }
}

/**
 * @brief Apply texture conversion using modular pixel transformations
 */
void apply_texture_conversion(bimg::ImageContainer* image, const std::string& semantic, bool inverse)
{
    if(!image || !image->m_data)
    {
        return;
    }
    if(!is_supported_ldr_format(image->m_format))
    {
        APPLOG_WARNING("Mesh Importer: Skipping {} conversion on unsupported texture format (compressed/float/non-byte-aligned)", semantic);
        return;
    }

    uint8_t* image_data = static_cast<uint8_t*>(image->m_data);
    uint32_t pixel_count = image->m_width * image->m_height;
    uint32_t bpp = bimg::getBitsPerPixel(image->m_format);
    uint32_t bytes_per_pixel = bpp / 8;
    
    if(semantic == "SpecularToMetallicRoughness")
    {
        // Use the combined conversion function
        apply_specular_to_metallic_roughness_conversion(image);
        return;
    }
    else if(semantic == "GlossToRoughness")
    {
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel, 
                                            pixel_transforms::gloss_to_roughness_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied GlossToRoughness conversion to texture");
    }
    else if(semantic == "SpecularToRoughness")
    {
        bool alpha_has_gloss = image_has_meaningful_alpha(image_data, pixel_count, bytes_per_pixel);

        if(alpha_has_gloss)
        {
            for(uint32_t i = 0; i < pixel_count; ++i)
            {
                uint32_t pixel_index = i * bytes_per_pixel;
                pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel,
                                                pixel_transforms::specular_alpha_to_roughness_pixel);
            }
            APPLOG_TRACE("Mesh Importer: Applied SpecularToRoughness conversion (alpha=gloss) to texture");
        }
        else
        {
            for(uint32_t i = 0; i < pixel_count; ++i)
            {
                uint32_t pixel_index = i * bytes_per_pixel;
                pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel,
                                                pixel_transforms::specular_intensity_to_roughness_pixel);
            }
            APPLOG_TRACE("Mesh Importer: Applied SpecularToRoughness conversion (intensity) to texture");
        }
    }
    else if(semantic == "SpecularToMetallic")
    {
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel, 
                                            pixel_transforms::specular_to_metallic_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied SpecularToMetallic conversion to texture");
    }
    else if(semantic == "ExtractMetallicChannel")
    {
        // Extract metallic channel from combined texture (Blue channel in glTF standard)
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel, 
                                            [](float r, float g, float b, float a) {
                                                // Extract metallic from blue channel and make it grayscale
                                                return std::make_tuple(b, b, b, 1.0f);
                                            });
        }
        APPLOG_TRACE("Mesh Importer: Extracted metallic channel for debugging");
    }
    else if(semantic == "ExtractRoughnessChannel")
    {
        // Extract roughness channel from combined texture (Green channel in glTF standard)
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel, 
                                            [](float r, float g, float b, float a) {
                                                // Extract roughness from green channel and make it grayscale
                                                return std::make_tuple(g, g, g, 1.0f);
                                            });
        }
        APPLOG_TRACE("Mesh Importer: Extracted roughness channel for debugging");
    }
    else if(inverse)
    {
        // Simple inversion for other cases where inverse flag is set
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel, 
                                            pixel_transforms::simple_invert_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied simple inversion to texture");
    }
}

/**
 * @brief Detect whether an image has meaningful alpha data (not all-opaque).
 * Samples a subset of pixels for performance.
 */
auto image_has_meaningful_alpha(const uint8_t* image_data, uint32_t pixel_count, uint32_t bytes_per_pixel) -> bool
{
    if(bytes_per_pixel < 4)
    {
        return false;
    }
    uint32_t sample_count = std::min(pixel_count, 256u);
    uint32_t step = std::max(1u, pixel_count / sample_count);
    uint32_t non_opaque = 0;
    for(uint32_t i = 0; i < pixel_count; i += step)
    {
        if(image_data[i * bytes_per_pixel + 3] < 255)
        {
            non_opaque++;
        }
    }
    return (non_opaque > sample_count / 10);
}

/**
 * @brief Apply combined specular to metallic+roughness conversion (glTF style).
 * Output layout matches the glTF Metallic-Roughness texture convention:
 *   R = (occlusion placeholder, set to 1.0)
 *   G = roughness
 *   B = metallic
 *   A = 1.0
 * The deferred geometry shader samples G for roughness and B for metallic when the
 * same texture is bound to both the metalness and roughness slots.
 */
void apply_specular_to_metallic_roughness_conversion(bimg::ImageContainer* image)
{
    if(!image || !image->m_data)
    {
        return;
    }
    if(!is_supported_ldr_format(image->m_format))
    {
        APPLOG_WARNING("Mesh Importer: Skipping SpecularToMetallicRoughness conversion on unsupported texture format");
        return;
    }

    uint8_t* image_data = static_cast<uint8_t*>(image->m_data);
    uint32_t pixel_count = image->m_width * image->m_height;
    uint32_t bpp = bimg::getBitsPerPixel(image->m_format);
    uint32_t bytes_per_pixel = bpp / 8;

    // The MR pack uses three distinct channels (R/G/B), so a single-channel or
    // luminance+alpha source cannot represent the result. Bail loudly rather than
    // silently dropping the metallic / roughness data via transform_pixel's
    // channel-reduction fallback.
    if(bytes_per_pixel < 3)
    {
        APPLOG_WARNING("Mesh Importer: Skipping SpecularToMetallicRoughness conversion on <3-channel source (cannot pack R/G/B)");
        return;
    }

    bool alpha_has_gloss = image_has_meaningful_alpha(image_data, pixel_count, bytes_per_pixel);

    if(alpha_has_gloss)
    {
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel,
                                            pixel_transforms::specular_to_metallic_roughness_alpha_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied SpecularToMetallicRoughness conversion (alpha=gloss) to texture");
    }
    else
    {
        for(uint32_t i = 0; i < pixel_count; ++i)
        {
            uint32_t pixel_index = i * bytes_per_pixel;
            pixel_transforms::transform_pixel(&image_data[pixel_index], bytes_per_pixel,
                                            pixel_transforms::specular_to_metallic_roughness_intensity_pixel);
        }
        APPLOG_TRACE("Mesh Importer: Applied SpecularToMetallicRoughness conversion (intensity) to texture");
    }
}

/**
 * @brief Convert a diffuse/specular pair into PBR metallic-roughness textures.
 *
 * The Khronos spec-gloss → metal-rough math is performed in sRGB-encoded float space
 * (i.e. 8-bit channels divided by 255), matching the reference Babylon.js / Khronos
 * implementations. The perceptual luminance weighting in solve_metallic only matches
 * when inputs stay in sRGB; do NOT degamma here.
 *
 * Per pixel we solve metallic with the proper diffuse luminance (much better than the
 * mid-gray fallback used by `compute_metallic_from_specular`), then reconstruct base
 * color using the official Khronos / Babylon reference identity:
 *   baseColor = mix(diffuse * (1 - F0) / (1 - metallic * F0),
 *                   specular - F0 * (1 - metallic),
 *                   metallic^2)
 *
 * The diffuse_image is rewritten in-memory with the base color result. specular_image
 * is read-only.
 *
 * If `out_mr_rgba8` is non-null, it is filled with `width*height*4` bytes of RGBA8
 * metallic-roughness data matching the glTF MR convention (R=1, G=roughness,
 * B=metallic_per_pixel, A=1). This gives a much more accurate metallic map than the
 * fallback `SpecularToMetallic` path which has to assume a mid-gray diffuse.
 *
 * Both images must have the same dimensions and be byte-aligned LDR formats with at
 * least 3 channels.
 */
void apply_diffuse_to_base_color_conversion(bimg::ImageContainer* diffuse_image,
                                            const bimg::ImageContainer* specular_image,
                                            const spec_gloss_factors_t& factors,
                                            std::vector<uint8_t>* out_mr_rgba8)
{
    if(!diffuse_image || !diffuse_image->m_data || !specular_image || !specular_image->m_data)
    {
        return;
    }
    if(diffuse_image->m_width != specular_image->m_width || diffuse_image->m_height != specular_image->m_height)
    {
        APPLOG_WARNING("Mesh Importer: Diffuse/specular texture size mismatch for base color conversion");
        return;
    }
    if(!is_supported_ldr_format(diffuse_image->m_format) || !is_supported_ldr_format(specular_image->m_format))
    {
        APPLOG_WARNING("Mesh Importer: Diffuse-to-base-color conversion requires uncompressed LDR textures; skipping");
        return;
    }

    uint32_t d_bpp = bimg::getBitsPerPixel(diffuse_image->m_format) / 8;
    uint32_t s_bpp = bimg::getBitsPerPixel(specular_image->m_format) / 8;
    if(d_bpp < 3 || s_bpp < 3)
    {
        // We need actual RGB channels in both inputs; grayscale/L+A sources do not
        // carry the chromatic information the spec-gloss identity needs.
        APPLOG_WARNING("Mesh Importer: Diffuse-to-base-color conversion requires RGB inputs (diffuse={} bpp, specular={} bpp); skipping",
                       d_bpp * 8, s_bpp * 8);
        return;
    }

    constexpr float dielectric_f0 = 0.04f;
    constexpr float epsilon = 1e-6f;

    uint32_t width = diffuse_image->m_width;
    uint32_t height = diffuse_image->m_height;
    uint32_t pixel_count = width * height;
    auto* d_data = static_cast<uint8_t*>(diffuse_image->m_data);
    const auto* s_data = static_cast<const uint8_t*>(specular_image->m_data);

    // The specular texture's alpha channel carries glossiness in many spec-gloss
    // workflows (KHR_materials_pbrSpecularGlossiness "specularGlossinessTexture").
    // Fall back to specular intensity as a roughness estimate when there is no
    // meaningful alpha. Decide once for the whole texture to avoid per-pixel jitter.
    bool spec_alpha_has_gloss = (s_bpp >= 4) && image_has_meaningful_alpha(s_data, pixel_count, s_bpp);

    if(out_mr_rgba8 != nullptr)
    {
        out_mr_rgba8->assign(static_cast<size_t>(pixel_count) * 4, 0);
    }

    for(uint32_t i = 0; i < pixel_count; ++i)
    {
        // Apply per-material multipliers from KHR_materials_pbrSpecularGlossiness:
        //   final_diffuse_color    = diffuseTexture.rgb * diffuseFactor.rgb
        //   final_specular_color   = specularTexture.rgb * specularFactor
        //   final_glossiness       = specularTexture.a * glossinessFactor
        // Baking the factors in here lets the caller set the material's base-color /
        // metallic / roughness factor uniforms to identity and avoid double-application
        // (the deferred shader does `albedo *= u_base_color` and `roughness *= tex.g`).
        float dr = static_cast<float>(d_data[i * d_bpp + 0]) / 255.0f * factors.diffuse_r;
        float dg = static_cast<float>(d_data[i * d_bpp + 1]) / 255.0f * factors.diffuse_g;
        float db = static_cast<float>(d_data[i * d_bpp + 2]) / 255.0f * factors.diffuse_b;

        float sr = static_cast<float>(s_data[i * s_bpp + 0]) / 255.0f * factors.specular_r;
        float sg = static_cast<float>(s_data[i * s_bpp + 1]) / 255.0f * factors.specular_g;
        float sb = static_cast<float>(s_data[i * s_bpp + 2]) / 255.0f * factors.specular_b;
        float sa = (s_bpp >= 4) ? static_cast<float>(s_data[i * s_bpp + 3]) / 255.0f * factors.glossiness
                                : factors.glossiness;

        float max_specular = std::max({sr, sg, sb});
        float one_minus_spec_str = 1.0f - max_specular;
        float perc_d = perceived_brightness(dr, dg, db);
        float perc_s = perceived_brightness(sr, sg, sb);
        float metallic = solve_metallic(perc_d, perc_s, one_minus_spec_str);

        // Exact Khronos/Babylon reference formula for base color reconstruction:
        //   baseColorFromDiffuse  = diffuse * (1 - F0) / (1 - metallic * F0)
        //   baseColorFromSpecular = specular - F0 * (1 - metallic)
        //   baseColor = mix(baseColorFromDiffuse, baseColorFromSpecular, metallic²)
        // Earlier we used a `one_minus_spec_str / (1 - metallic)` factor in the diffuse
        // term, which over-weighted the diffuse for high-metallic pixels and let things
        // like the rust tones on a metal helm bleed into the final base color.
        float denom = std::max(1.0f - metallic * dielectric_f0, epsilon);
        float spec_offset = dielectric_f0 * (1.0f - metallic);
        float t = metallic * metallic;

        auto base_d = [&](float d) -> float { return d * (1.0f - dielectric_f0) / denom; };
        auto base_s = [&](float s) -> float { return s - spec_offset; };

        float br = math::mix(base_d(dr), base_s(sr), t);
        float bg = math::mix(base_d(dg), base_s(sg), t);
        float bb = math::mix(base_d(db), base_s(sb), t);

        d_data[i * d_bpp + 0] = pixel_transforms::to_uint8(br);
        d_data[i * d_bpp + 1] = pixel_transforms::to_uint8(bg);
        d_data[i * d_bpp + 2] = pixel_transforms::to_uint8(bb);

        // Bake diffuseFactor.a into the base color alpha so material transparency
        // doesn't get lost. Skip when no alpha channel is present.
        if(d_bpp >= 4)
        {
            float da = static_cast<float>(d_data[i * d_bpp + 3]) / 255.0f * factors.diffuse_a;
            d_data[i * d_bpp + 3] = pixel_transforms::to_uint8(da);
        }

        if(out_mr_rgba8 != nullptr)
        {
            float roughness = spec_alpha_has_gloss
                                  ? (1.0f - sa)
                                  : (1.0f - (sr + sg + sb) / 3.0f);

            uint8_t* mr = out_mr_rgba8->data() + static_cast<size_t>(i) * 4;
            mr[0] = 255;                                  // R = occlusion placeholder
            mr[1] = pixel_transforms::to_uint8(roughness); // G = roughness
            mr[2] = pixel_transforms::to_uint8(metallic);  // B = metallic
            mr[3] = 255;                                  // A = 1.0
        }
    }

    APPLOG_TRACE("Mesh Importer: Applied diffuse-to-base-color conversion using specular texture{}",
                 out_mr_rgba8 ? " (with sibling metallic-roughness map)" : "");
}

/**
 * @brief Write a raw RGBA8 buffer to a PNG file. Used for sibling outputs that
 * we synthesize directly without going through bimg::ImageContainer.
 *
 * We deliberately avoid TGA here: bimg::imageWriteTga dumps the source buffer
 * raw under a Type-2 header, but the TGA spec mandates BGRA byte order on disk.
 * Feeding it RGBA bytes (as bimg::ImageContainer stores them) produces a file
 * that stb_image — and any other compliant TGA reader — re-interprets as BGRA,
 * yielding an R↔B swap at load time. PNG carries explicit format metadata and
 * imageWritePng honors the RGBA8 parameter, so this round-trips correctly.
 */
auto write_rgba8_png(const fs::path& output_file,
                     uint32_t width,
                     uint32_t height,
                     const uint8_t* rgba8_data) -> bool
{
    bx::FileWriter writer;
    bx::Error err;
    if(!bx::open(&writer, output_file.string().c_str(), false, &err))
    {
        return false;
    }
    bimg::imageWritePng(&writer,
                        width,
                        height,
                        width * 4,
                        rgba8_data,
                        bimg::TextureFormat::RGBA8,
                        false,
                        &err);
    bx::close(&writer);
    return err.isOk();
}

/**
 * @brief Result of a spec-gloss → PBR texture conversion.
 */
struct spec_gloss_pbr_result
{
    bool diffuse_converted{false};    /// Diffuse mutated to base color and saved to base_color_relative.
    std::string base_color_relative;  /// Relative path of the converted base color file (empty if not produced).
    std::string mr_relative;          /// Relative path of the sibling metallic-roughness file (empty if not produced).
};

/**
 * @brief Convert a diffuse+specular pair into PBR textures and save them.
 *
 * The caller supplies the exact destination paths so the file names can match the
 * material's final semantic (e.g. `[2] BaseColor <model>.png`,
 * `[3] MetallicRoughness <model>.png`) instead of leaking the source-texture
 * naming through an awkward `_BaseColor` / `_MetallicRoughness` suffix.
 *
 * Both inputs are normalized to RGBA8 before processing — the per-pixel math
 * reads R/G/B/A by byte offset, and `bimg::imageWritePng` expects pitch = width*4
 * with an explicit format argument. The caller still owns and frees the original
 * `diffuse_img` / `specular_img`; any intermediate RGBA8 conversions are managed
 * internally.
 */
auto convert_spec_gloss_to_pbr_textures(const fs::path& output_dir,
                                        const std::string& base_color_relative,
                                        const std::string& mr_relative,
                                        bimg::ImageContainer* diffuse_img,
                                        const bimg::ImageContainer* specular_img,
                                        const spec_gloss_factors_t& factors) -> spec_gloss_pbr_result
{
    spec_gloss_pbr_result result{};

    if(!diffuse_img || !specular_img)
    {
        return result;
    }

    // Normalize both inputs to RGBA8 so byte-offset reads and the PNG writer see
    // a consistent layout. imageConvert may return the same pointer if the source
    // is already RGBA8.
    bool diffuse_was_converted = false;
    bool specular_was_converted = false;
    bimg::ImageContainer* diffuse_rgba8 = ensure_rgba8(diffuse_img, diffuse_was_converted);
    bimg::ImageContainer* specular_rgba8 = ensure_rgba8(const_cast<bimg::ImageContainer*>(specular_img), specular_was_converted);

    auto free_intermediates = [&]()
    {
        if(diffuse_was_converted && diffuse_rgba8)
        {
            bimg::imageFree(diffuse_rgba8);
        }
        if(specular_was_converted && specular_rgba8)
        {
            bimg::imageFree(specular_rgba8);
        }
    };

    if(!diffuse_rgba8 || !specular_rgba8)
    {
        APPLOG_WARNING("Mesh Importer: Spec-gloss conversion skipped — could not normalize inputs to RGBA8");
        free_intermediates();
        return result;
    }

    std::vector<uint8_t> mr_buffer;
    apply_diffuse_to_base_color_conversion(diffuse_rgba8, specular_rgba8, factors, &mr_buffer);

    // The conversion bails (logged) if formats are incompatible or sizes mismatch.
    if(mr_buffer.empty())
    {
        APPLOG_WARNING("Mesh Importer: Spec-gloss conversion produced no output (size mismatch or unsupported format)");
        free_intermediates();
        return result;
    }

    if(!write_rgba8_png(output_dir / base_color_relative,
                        diffuse_rgba8->m_width,
                        diffuse_rgba8->m_height,
                        static_cast<const uint8_t*>(diffuse_rgba8->m_data)))
    {
        APPLOG_WARNING("Mesh Importer: Failed to save converted base color texture: {}", base_color_relative);
        free_intermediates();
        return result;
    }
    result.diffuse_converted = true;
    result.base_color_relative = base_color_relative;

    if(write_rgba8_png(output_dir / mr_relative, diffuse_rgba8->m_width, diffuse_rgba8->m_height, mr_buffer.data()))
    {
        result.mr_relative = mr_relative;
    }
    else
    {
        APPLOG_WARNING("Mesh Importer: Failed to save sibling metallic-roughness texture for spec-gloss conversion");
    }

    free_intermediates();
    return result;
}

/**
 * @brief Process raw texture data with conversions
 */
void process_raw_texture_data(const aiTexture* assimp_tex, const fs::path& output_file, 
                             const std::string& semantic, bool inverse)
{
    // For raw textures, we need to create a temporary image container to apply conversions
    uint32_t width = assimp_tex->mWidth;
    uint32_t height = assimp_tex->mHeight;
    
    // Create a copy of the raw data to modify
    std::vector<uint8_t> data(width * height * 4);
    std::memcpy(data.data(), assimp_tex->pcData, width * height * 4);
    
    // Apply conversions to the copied data
    if(semantic == "GlossToRoughness" || semantic == "SpecularToRoughness" || semantic == "SpecularToMetallic" || semantic == "SpecularToMetallicRoughness")
    {
        // Create a temporary image container for conversion
        bimg::ImageContainer image;
        image.m_data = data.data();
        image.m_width = width;
        image.m_height = height;
        image.m_depth = 1;
        image.m_format = bimg::TextureFormat::RGBA8;
        image.m_numMips = 1;
        image.m_hasAlpha = true;
        
        apply_texture_conversion(&image, semantic, inverse);
    }
    else if(inverse)
    {
        // Simple inversion
        for(size_t i = 0; i < data.size(); ++i)
        {
            data[i] = 255 - data[i];
        }
    }
    
    // Write the processed data as PNG. Avoid TGA here for the same reason as
    // write_rgba8_png: bimg::imageWriteTga writes the buffer raw under a Type-2
    // header but TGA's wire format is BGRA, so RGBA bytes load back R↔B-swapped.
    write_rgba8_png(output_file, width, height, data.data());
}

template<typename T>
void log_prop_value(aiMaterialProperty* prop, const char* name1)
{
    auto data = (T*)prop->mData;

    auto count = prop->mDataLength / sizeof(T);

    if(count == 1)
    {
        APPLOG_TRACE("  {} = {}", name1, data[0]);
    }
    else
    {
        std::vector<T> vals(count);
        std::memcpy(vals.data(), data, count * sizeof(T));
        APPLOG_TRACE("  {}[{}] = {}", name1, count, vals);
    }
}

void log_materials(const aiMaterial* material)
{
    for(uint32_t i = 0; i < material->mNumProperties; i++)
    {
        auto prop = material->mProperties[i];

        APPLOG_TRACE("Material Property:");
        APPLOG_TRACE("  name = {0}", prop->mKey.C_Str());

        if(prop->mDataLength > 0 && prop->mData)
        {
            auto semantic = aiTextureType(prop->mSemantic);
            if(semantic != aiTextureType_NONE && semantic != aiTextureType_UNKNOWN)
            {
                APPLOG_TRACE("  semantic = {0}", aiTextureTypeToString(semantic));
            }

            switch(prop->mType)
            {
                case aiPropertyTypeInfo::aiPTI_Float:
                {
                    log_prop_value<float>(prop, "float");
                    break;
                }

                case aiPropertyTypeInfo::aiPTI_Double:
                {
                    log_prop_value<double>(prop, "double");
                    break;
                }
                case aiPropertyTypeInfo::aiPTI_Integer:
                {
                    log_prop_value<int32_t>(prop, "int");
                    break;
                }

                case aiPropertyTypeInfo::aiPTI_Buffer:
                {
                    log_prop_value<uint8_t>(prop, "buffer");
                    break;
                }
                case aiPropertyTypeInfo::aiPTI_String:
                {
                    aiString str;
                    if(aiGetMaterialString(material, prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, &str) ==
                       AI_SUCCESS)
                    {
                        APPLOG_TRACE("  string = {0}", str.C_Str());
                    }
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
    }
}

// Add workflow detection and conversion functions
enum class material_workflow
{
    unknown,
    metallic_roughness,
    specular_gloss
};

/**
 * @brief Check if the same texture is used for both metallic and roughness conversion
 */
auto detect_duplicate_specular_usage(const aiMaterial* material, material_workflow workflow) -> bool
{
    if(workflow != material_workflow::specular_gloss)
    {
        return false;
    }
    
    // Check if we have dedicated metallic/roughness textures - if so, no duplication
    bool has_metallic_texture = (material->GetTextureCount(aiTextureType_METALNESS) > 0) ||
                               (material->GetTextureCount(aiTextureType_GLTF_METALLIC_ROUGHNESS) > 0);
    bool has_roughness_texture = (material->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0) ||
                                (material->GetTextureCount(aiTextureType_GLTF_METALLIC_ROUGHNESS) > 0);
    bool has_glossiness_texture = (material->GetTextureCount(aiTextureType_SHININESS) > 0);
    
    // If we have dedicated textures, no need for specular conversion
    if(has_metallic_texture || has_roughness_texture || has_glossiness_texture)
    {
        return false;
    }
    
    // Check if we have a specular texture that would be used for both conversions
    bool has_specular_texture = (material->GetTextureCount(aiTextureType_SPECULAR) > 0);
    
    if(has_specular_texture)
    {
        // The logic in get_workflow_aware_texture would use the same specular texture for:
        // 1. "Metallic" -> SpecularToMetallic conversion 
        // 2. "Roughness" -> SpecularToRoughness conversion
        // This is a duplication that should use SpecularToMetallicRoughness instead
        
        APPLOG_TRACE("Mesh Importer: Detected duplicate specular usage - same texture would be used for both metallic and roughness conversion");
        return true;
    }
    
    return false;
}

/**
 * @brief Detect the material workflow used by analyzing available properties and textures.
 *
 * Layering:
 *   1. Definitive checks — an unambiguous indicator pins the workflow immediately. These
 *      are the markers the glTF and KHR_materials_pbrSpecularGlossiness specs use to
 *      identify the workflow, so we trust them and skip the heuristic.
 *   2. Scored heuristic — only used when the definitive checks were inconclusive, e.g.
 *      legacy formats (FBX/OBJ) that emit a soup of overlapping properties without a
 *      strong workflow marker.
 */
auto detect_material_workflow(const aiMaterial* material) -> material_workflow
{
    ai_real dummy_value{};
    aiColor3D dummy_color{};
    aiString path{};

    // ----- 1. Definitive specular-glossiness markers (checked first!) -----
    // KHR_materials_pbrSpecularGlossiness mandates that authoring tools also write
    // pbrMetallicRoughness fallback properties so MR-only engines can still load the
    // asset. That means dual-authored SG files ALSO carry BASE_COLOR_TEXTURE /
    // METALLIC_FACTOR / etc. Checking SG first ensures we honor the author's primary
    // representation and route the spec-gloss texture through our conversion pipeline.
    //
    // We only treat the explicit KHR_materials_pbrSpecularGlossiness factors as
    // definitive. aiTextureType_SHININESS / AI_MATKEY_SHININESS are intentionally
    // NOT used here because legacy Phong materials also set them.
    if(material->Get(AI_MATKEY_GLOSSINESS_FACTOR, dummy_value) == AI_SUCCESS
       || material->Get(AI_MATKEY_SPECULAR_FACTOR, dummy_value) == AI_SUCCESS)
    {
        return material_workflow::specular_gloss;
    }

    // ----- 2. Definitive metallic-roughness markers -----
    // Each marker below is set ONLY by PBR-MR pipelines (glTF MR, modern FBX PBR
    // exporters). Hitting any of them is enough to pin the workflow.
    if(material->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, &path) == AI_SUCCESS
       || material->GetTexture(AI_MATKEY_BASE_COLOR_TEXTURE, &path) == AI_SUCCESS
       || material->GetTexture(AI_MATKEY_METALLIC_TEXTURE, &path) == AI_SUCCESS
       || material->GetTexture(AI_MATKEY_ROUGHNESS_TEXTURE, &path) == AI_SUCCESS
       || material->Get(AI_MATKEY_METALLIC_FACTOR, dummy_value) == AI_SUCCESS
       || material->Get(AI_MATKEY_ROUGHNESS_FACTOR, dummy_value) == AI_SUCCESS
       || material->Get(AI_MATKEY_BASE_COLOR, dummy_color) == AI_SUCCESS)
    {
        return material_workflow::metallic_roughness;
    }

    // ----- 3. Scored heuristic fallback (legacy / mixed-signal materials) -----
    bool has_diffuse_color = material->Get(AI_MATKEY_COLOR_DIFFUSE, dummy_color) == AI_SUCCESS;
    bool has_specular_color = material->Get(AI_MATKEY_COLOR_SPECULAR, dummy_color) == AI_SUCCESS;

    bool has_shininess = material->Get(AI_MATKEY_SHININESS, dummy_value) == AI_SUCCESS;
    bool has_reflectivity = material->Get(AI_MATKEY_REFLECTIVITY, dummy_value) == AI_SUCCESS;

    bool has_specular_texture = material->GetTexture(aiTextureType_SPECULAR, 0, &path) == AI_SUCCESS;
    bool has_glossiness_texture = material->GetTexture(aiTextureType_SHININESS, 0, &path) == AI_SUCCESS;
    bool has_diffuse_texture = material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS;

    int specular_gloss_score = 0;

    // Specular/Gloss indicators
    if(has_diffuse_color) specular_gloss_score += 4;
    if(has_specular_color) specular_gloss_score += 6;
    if(has_specular_texture) specular_gloss_score += 10;
    if(has_glossiness_texture) specular_gloss_score += 10;
    if(has_diffuse_texture) specular_gloss_score += 6;

    // Legacy Phong indicators (only meaningful as a tie-breaker now that we exited
    // before reaching here only when no definitive PBR markers were present)
    if(has_shininess) specular_gloss_score += 4;
    if(has_reflectivity) specular_gloss_score += 3;

    // Classic spec-gloss combinations
    if(has_specular_texture && has_diffuse_texture)
    {
        specular_gloss_score += 8;
    }
    if(has_specular_color && has_diffuse_color)
    {
        specular_gloss_score += 6;
    }

    APPLOG_TRACE("Mesh Importer: Material workflow heuristic score - Specular/Gloss: {}", specular_gloss_score);

    if(specular_gloss_score >= 5)
    {
        return material_workflow::specular_gloss;
    }
    return material_workflow::unknown;
}

/**
 * @brief Get workflow-aware texture with proper semantic mapping
 */
template<typename GetTextureFunc>
auto get_workflow_aware_texture(const aiMaterial* material,
                               material_workflow workflow,
                               const std::string& target_semantic,
                               imported_texture& tex,
                               GetTextureFunc get_imported_texture,
                               bool use_combined_specular = false) -> bool
{
    if(target_semantic == "BaseColor")
    {
        // Try base color first, then diffuse as fallback
        if(get_imported_texture(material, AI_MATKEY_BASE_COLOR_TEXTURE, "BaseColor", tex))
        {
            return true;
        }
        else if(get_imported_texture(material, aiTextureType_DIFFUSE, 0, "BaseColor", tex))
        {
            return true;
        }
    }
    else if(target_semantic == "Metallic")
    {
        // For metallic, check if we have a combined metallic/roughness texture
        if(get_imported_texture(material, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, "MetallicRoughness", tex))
        {
            return true;
        }
        // Otherwise try standalone metallic
        else if(get_imported_texture(material, AI_MATKEY_METALLIC_TEXTURE, "Metallic", tex))
        {
            return true;
        }
        // Heuristic fallback for MR workflows: FBX exporters (Blender, Substance) often
        // emit the combined MR texture under a property name Assimp can't categorize, so
        // it ends up in aiTextureType_UNKNOWN. When we've already classified the material
        // as MR and no canonical metallic texture is present, trust the unknown slot as
        // the combined metallic-roughness map. Tagging it "MetallicRoughness" makes the
        // extracted file land with a semantic-suffixed name instead of bare "Texture".
        else if(workflow == material_workflow::metallic_roughness
                && get_imported_texture(material, aiTextureType_UNKNOWN, 0, "MetallicRoughness", tex))
        {
            APPLOG_TRACE("Mesh Importer: Recovering metallic-roughness texture from aiTextureType_UNKNOWN slot");
            return true;
        }
        // For specular workflow, check if we should use combined processing
        else if(workflow == material_workflow::specular_gloss)
        {
            if(use_combined_specular)
            {
                // Skip individual processing - combined processing will handle this
                return false;
            }
            else if(get_imported_texture(material, aiTextureType_SPECULAR, 0, "SpecularToMetallic", tex))
            {
                tex.inverse = false; // Special processing: extract metallic info from specular
                return true;
            }
        }
    }
    else if(target_semantic == "Roughness")
    {
        // For roughness, check if we have a combined metallic/roughness texture
        if(get_imported_texture(material, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, "MetallicRoughness", tex))
        {
            return true;
        }
        // Try standalone roughness
        else if(get_imported_texture(material, AI_MATKEY_ROUGHNESS_TEXTURE, "Roughness", tex))
        {
            return true;
        }
        // Same UNKNOWN-slot fallback as the metallic branch — the FBX MR pipeline reuses
        // the same combined texture for both maps. The shader's combined-MR sampling path
        // (G = roughness, B = metallic) handles this correctly per glTF convention.
        else if(workflow == material_workflow::metallic_roughness
                && get_imported_texture(material, aiTextureType_UNKNOWN, 0, "MetallicRoughness", tex))
        {
            APPLOG_TRACE("Mesh Importer: Recovering metallic-roughness texture from aiTextureType_UNKNOWN slot");
            return true;
        }
        // For specular/gloss workflow, convert gloss to roughness
        else if(workflow == material_workflow::specular_gloss)
        {
            if(use_combined_specular)
            {
                // Skip individual processing - combined processing will handle this
                return false;
            }
            else if(get_imported_texture(material, aiTextureType_SHININESS, 0, "GlossToRoughness", tex))
            {
                tex.inverse = true; // Roughness = 1 - Gloss
                return true;
            }
            // Try specular texture as fallback (may contain gloss in alpha)
            else if(get_imported_texture(material, aiTextureType_SPECULAR, 0, "SpecularToRoughness", tex))
            {
                tex.inverse = true;
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Process material with intelligent property extraction and conversion.
 * First tries to get actual PBR properties, then converts missing ones from available data.
 * For specular/gloss workflows, gathers inputs once and performs a single conversion call.
 */
void process_material_with_workflow_conversion(const aiMaterial* material, 
                                             material_workflow workflow,
                                             aiColor3D& base_color,
                                             float& metallic,
                                             float& roughness)
{
    bool has_base_color = (material->Get(AI_MATKEY_BASE_COLOR, base_color) == AI_SUCCESS);
    bool has_metallic = (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS);
    bool has_roughness = (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS);
    
    if (workflow == material_workflow::specular_gloss && (!has_base_color || !has_metallic || !has_roughness))
    {
        aiColor3D diffuse_color{1.0f, 1.0f, 1.0f};
        material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse_color);
        
        aiColor3D specular_color{0.04f, 0.04f, 0.04f};
        float specular_factor = 1.0f;
        material->Get(AI_MATKEY_COLOR_SPECULAR, specular_color);
        material->Get(AI_MATKEY_SPECULAR_FACTOR, specular_factor);
        specular_color.r *= specular_factor;
        specular_color.g *= specular_factor;
        specular_color.b *= specular_factor;
        
        float glossiness = 0.5f;
        if(material->Get(AI_MATKEY_GLOSSINESS_FACTOR, glossiness) != AI_SUCCESS)
        {
            float shininess = 32.0f;
            if(material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
            {
                glossiness = math::clamp(1.0f - std::sqrt(2.0f / (shininess + 2.0f)), 0.0f, 1.0f);
            }
        }
        
        auto [converted_base_color, converted_metallic, converted_roughness] = 
            convert_specular_gloss_to_metallic_roughness(diffuse_color, specular_color, glossiness);
        
        if (!has_base_color)
        {
            base_color = converted_base_color;
            APPLOG_TRACE("Mesh Importer: Converted base color from specular/diffuse workflow");
        }
        if (!has_metallic)
        {
            metallic = converted_metallic;
            APPLOG_TRACE("Mesh Importer: Converted metallic factor from specular workflow: {:.3f}", metallic);
        }
        if (!has_roughness)
        {
            roughness = converted_roughness;
            APPLOG_TRACE("Mesh Importer: Converted roughness from specular workflow: {:.3f}", roughness);
        }
    }
    else
    {
        if (!has_base_color)
        {
            if (material->Get(AI_MATKEY_COLOR_DIFFUSE, base_color) != AI_SUCCESS)
            {
                base_color = aiColor3D{1.0f, 1.0f, 1.0f};
            }
        }
        
        if (!has_metallic)
        {
            if (material->Get(AI_MATKEY_REFLECTIVITY, metallic) != AI_SUCCESS)
            {
                metallic = 0.0f;
            }
        }
        
        if (!has_roughness)
        {
            float shininess = 32.0f;
            if(material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
            {
                roughness = std::sqrt(2.0f / (shininess + 2.0f));
                APPLOG_TRACE("Mesh Importer: Converted roughness from legacy shininess: {:.1f} -> {:.3f}", 
                           shininess, roughness);
            }
            else
            {
                roughness = 0.5f;
            }
        }
    }
    
    APPLOG_TRACE("Mesh Importer: Final PBR values - BaseColor: ({:.3f}, {:.3f}, {:.3f}), "
                "Metallic: {:.3f}, Roughness: {:.3f} [{}{}{}]",
                base_color.r, base_color.g, base_color.b, metallic, roughness,
                has_base_color ? "B" : "b",
                has_metallic ? "M" : "m", 
                has_roughness ? "R" : "r");
}

void process_material(asset_manager& am,
                      const fs::path& filename,
                      const fs::path& output_dir,
                      const aiScene* scene,
                      const aiMaterial* material,
                      pbr_material& mat,
                      std::vector<imported_texture>& textures)
{
    if(!material)
    {
        return;
    }

    // Diagnostic: enumerate every texture slot Assimp populated on this material.
    // Crucial for triaging "why isn't this texture assigned" cases — different exporters
    // (Blender, Maya, 3DSMax, glTF) park PBR textures under wildly different aiTextureType
    // values, and a single look at this log makes the routing obvious.
    {
        struct slot_info
        {
            aiTextureType type;
            const char* name;
        };
        static constexpr std::array<slot_info, 21> slot_table = {{
            {aiTextureType_DIFFUSE,           "DIFFUSE"},
            {aiTextureType_SPECULAR,          "SPECULAR"},
            {aiTextureType_AMBIENT,           "AMBIENT"},
            {aiTextureType_EMISSIVE,          "EMISSIVE"},
            {aiTextureType_HEIGHT,            "HEIGHT"},
            {aiTextureType_NORMALS,           "NORMALS"},
            {aiTextureType_SHININESS,         "SHININESS"},
            {aiTextureType_OPACITY,           "OPACITY"},
            {aiTextureType_DISPLACEMENT,      "DISPLACEMENT"},
            {aiTextureType_LIGHTMAP,          "LIGHTMAP"},
            {aiTextureType_REFLECTION,        "REFLECTION"},
            {aiTextureType_BASE_COLOR,        "BASE_COLOR"},
            {aiTextureType_NORMAL_CAMERA,     "NORMAL_CAMERA"},
            {aiTextureType_EMISSION_COLOR,    "EMISSION_COLOR"},
            {aiTextureType_METALNESS,         "METALNESS"},
            {aiTextureType_DIFFUSE_ROUGHNESS, "DIFFUSE_ROUGHNESS"},
            {aiTextureType_AMBIENT_OCCLUSION, "AMBIENT_OCCLUSION"},
            {aiTextureType_SHEEN,             "SHEEN"},
            {aiTextureType_CLEARCOAT,         "CLEARCOAT"},
            {aiTextureType_TRANSMISSION,      "TRANSMISSION"},
            {aiTextureType_UNKNOWN,           "UNKNOWN"},
        }};

        std::string slot_log;
        for(const auto& slot : slot_table)
        {
            auto count = material->GetTextureCount(slot.type);
            if(count == 0)
            {
                continue;
            }
            for(unsigned int i = 0; i < count; ++i)
            {
                aiString path{};
                if(material->GetTexture(slot.type, i, &path) == AI_SUCCESS && path.length > 0)
                {
                    if(!slot_log.empty())
                    {
                        slot_log += ", ";
                    }
                    slot_log += fmt::format("{}[{}]={}", slot.name, i, path.C_Str());
                }
            }
        }
        aiString mat_name{};
        material->Get(AI_MATKEY_NAME, mat_name);
        APPLOG_TRACE("Mesh Importer: Material '{}' texture slots: {}",
                     mat_name.length > 0 ? mat_name.C_Str() : "<unnamed>",
                     slot_log.empty() ? "<none>" : slot_log);
    }

    // Detect the material workflow before processing
    auto workflow = detect_material_workflow(material);

    APPLOG_TRACE("Mesh Importer: Material workflow detected: {}",
                 workflow == material_workflow::metallic_roughness ? "Metallic/Roughness" :
                 workflow == material_workflow::specular_gloss ? "Specular/Gloss" : "Unknown");

    // log_materials(material);

    auto get_imported_texture = [&](const aiMaterial* material,
                                    aiTextureType type,
                                    unsigned int index,
                                    const std::string& semantic,
                                    imported_texture& tex) -> bool
    {
        aiString path{};
        aiTextureMapping mapping{};
        unsigned int uvindex{};
        float blend{};
        aiTextureOp op{};
        aiTextureMapMode mapmode{};
        unsigned int flags{};

        // Call the function
        aiReturn result = aiGetMaterialTexture(material, // The material pointer
                                               type,     // The type of texture (e.g., diffuse)
                                               index,    // The texture index
                                               &path     // The path where the texture file path will be stored
                                                         // &mapping, // The mapping method
                                                         // &uvindex, // The UV index
                                                         // &blend,   // The blend factor
                                                         // &op,      // The texture operation
                                                         // &mapmode, // The texture map mode
                                                         // &flags    // Additional flags
        );

        if(path.length > 0)
        {
            auto tex_pair = scene->GetEmbeddedTextureAndIndex(path.C_Str());
            
            const auto embedded_texture = tex_pair.first;
            if(embedded_texture)
            {
                const auto index = tex_pair.second;

                // std::string s = aiTextureTypeToString(type);
                tex.name = get_embedded_texture_name(embedded_texture, index, filename, semantic);
                tex.embedded_index = index;
            }
            else
            {
                tex.name = path.C_Str();
                auto texture_filepath = fs::path(tex.name);

                auto extension = texture_filepath.extension().string();
                auto texture_dir = texture_filepath.parent_path();
                auto texture_filename = texture_filepath.filename().stem().string();
                auto fixed_name = string_utils::replace(texture_filename, ".", "_");
                if(fixed_name != texture_filename)
                {
                    auto old_filepath = output_dir / tex.name;
                    auto fixed_relative = texture_dir / (fixed_name + extension);
                    auto fixed_filepath = output_dir / fixed_relative;

                    fs::error_code ec;
                    if(fs::exists(old_filepath, ec))
                    {
                        fs::rename(old_filepath, fixed_filepath, ec);
                    }
                    else
                    {
                        // doesnt exist. so try to import it
                        fs::copy_file(old_filepath, fixed_filepath, ec);
                    }
                    tex.name = fixed_relative.generic_string();
                }
            }
            tex.semantic = semantic;
            bool use_alpha = flags & aiTextureFlags_UseAlpha;
            bool ignore_alpha = flags & aiTextureFlags_IgnoreAlpha;
            bool invert = flags & aiTextureFlags_Invert;
            tex.inverse = invert;

            switch(mapmode)
            {
                case aiTextureMapMode_Mirror:
                    tex.flags = BGFX_SAMPLER_UVW_MIRROR;
                    break;
                case aiTextureMapMode_Clamp:
                    tex.flags = BGFX_SAMPLER_UVW_CLAMP;
                    break;
                case aiTextureMapMode_Decal:
                    tex.flags = BGFX_SAMPLER_UVW_BORDER;
                    break;
                default:
                    break;
            }

            return true;
        }

        return false;
    };

    auto needs_external_conversion = [](const imported_texture& tex) -> bool
    {
        return tex.embedded_index < 0 &&
               (tex.semantic == "GlossToRoughness" ||
                tex.semantic == "SpecularToRoughness" ||
                tex.semantic == "SpecularToMetallic" ||
                tex.semantic == "SpecularToMetallicRoughness" ||
                tex.inverse);
    };

    auto make_converted_name = [](const std::string& original_name, const std::string& semantic) -> std::string
    {
        fs::path p(original_name);
        std::string suffix = semantic.empty() ? "converted" : semantic;
        // Use .png — TGA's wire format is BGRA but bimg::imageWriteTga dumps source
        // bytes raw, so RGBA8 buffers round-trip back R↔B-swapped via stb_image.
        // PNG carries explicit format metadata in the file.
        return (p.parent_path() / (p.stem().string() + "_" + suffix + ".png")).generic_string();
    };

    auto process_texture = [&](imported_texture& texture, std::vector<imported_texture>& textures, bool force_process = false)
    {
        if(texture.embedded_index >= 0)
        {
            auto it = std::find_if(std::begin(textures),
                                   std::end(textures),
                                   [&](const imported_texture& rhs)
                                   {
                                       return rhs.embedded_index == texture.embedded_index
                                           && rhs.semantic == texture.semantic;
                                   });
            if(it != std::end(textures))
            {
                texture.name = it->name;
                texture.flags = it->flags;
                texture.inverse = it->inverse;
                texture.process_count = it->process_count;
                return;
            }
        }
        else
        {
            auto it = std::find_if(std::begin(textures),
                                   std::end(textures),
                                   [&](const imported_texture& rhs)
                                   {
                                       return rhs.embedded_index < 0
                                           && rhs.name == texture.name
                                           && rhs.semantic == texture.semantic;
                                   });
            if(it != std::end(textures))
            {
                if(needs_external_conversion(texture))
                {
                    texture.name = make_converted_name(texture.name, texture.semantic);
                }
                return;
            }
        }

        textures.emplace_back(texture);

        if(texture.embedded_index >= 0)
        {
            const auto& embedded_texture = scene->mTextures[texture.embedded_index];
            process_embedded_texture(embedded_texture, texture.embedded_index, filename, output_dir, textures);
        }
        else if(needs_external_conversion(texture))
        {
            fs::path original_file = output_dir / texture.name;
            auto converted_name = make_converted_name(texture.name, texture.semantic);
            fs::path converted_file = output_dir / converted_name;
            bimg::ImageContainer* image = imageLoad(bx::FilePath(original_file.string().c_str()));
            if(image)
            {
                apply_texture_conversion(image, texture.semantic, texture.inverse);
                imageSave(converted_file.string().c_str(), image);
                bimg::imageFree(image);
                texture.name = converted_name;
                APPLOG_TRACE("Mesh Importer: Applied {} conversion to external texture: {}", texture.semantic, texture.name);
            }
        }
    };

    // technically there is a difference between MASK and BLEND mode
    // but for our purposes it's enough if we sort properly
    // aiString alpha_mode;
    // material->Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode);
    // aiString alpha_mode_opaque;
    // alpha_mode_opaque.Set("OPAQUE");

    // out.blend = alphaMode != alphaModeOpaque;

    // bool double_sided{};
    // if(material->Get(AI_MATKEY_TWOSIDED, double_sided) == AI_SUCCESS)
    // {
    //     mat.set_cull_type(double_sided ? cull_type::none : cull_type::counter_clockwise);
    // }

    // Spec-gloss workflows: when both the diffuse and specular textures are present we
    // produce a metallic-roughness map directly from the pair (using the proper per-pixel
    // quadratic metallic solve). When this fires we record the MR file here so the
    // METALLIC & ROUGHNESS block below can short-circuit and skip the less accurate
    // SpecularToMetallic / SpecularToRoughness fallback path.
    std::string combined_mr_relative;

    // Mark an embedded-texture index as "already processed" so the post-import
    // `process_embedded_textures` pass doesn't extract it under a generic name.
    // Used by the spec-gloss path to suppress extraction of the raw diffuse/specular
    // sources — we read them straight from pcData and only write the converted output.
    auto mark_embedded_consumed = [&](int idx) -> void
    {
        if(idx < 0)
        {
            return;
        }
        auto it = std::find_if(textures.begin(),
                               textures.end(),
                               [&](const imported_texture& t) -> bool { return t.embedded_index == idx; });
        if(it == textures.end())
        {
            imported_texture entry{};
            entry.embedded_index = idx;
            entry.process_count = 1;
            textures.emplace_back(std::move(entry));
        }
        else if(it->process_count == 0)
        {
            it->process_count = 1;
        }
    };

    // Build the final-semantic file name for a converted PBR output. For embedded
    // sources we use the natural `[N] <Semantic> <model>.png` form so the file lives
    // at the same slot the source would have occupied. For external sources we fall
    // back to a `<stem>_<Semantic>.png` sibling.
    auto build_converted_name = [&](int embedded_idx, const std::string& source_relative, const std::string& target_semantic) -> std::string
    {
        if(embedded_idx >= 0 && embedded_idx < static_cast<int>(scene->mNumTextures))
        {
            // Always emit PNG — write_rgba8_png is the only writer this path uses.
            return fmt::format("[{}] {} {}.png", embedded_idx, target_semantic, filename.string());
        }
        fs::path src(source_relative);
        return (src.parent_path() / (src.stem().string() + "_" + target_semantic + ".png")).generic_string();
    };

    // Track whether the per-pixel spec-gloss conversion succeeded for the BASE COLOR
    // PROPERTY block below: when it did, the factors are already baked into the texture
    // and the per-material uniforms must be set to identity to avoid double-application
    // (the deferred shader does `albedo *= u_base_color`, `roughness *= tex.g`, etc.).
    bool sg_textures_converted = false;

    // BASE COLOR TEXTURE - Use workflow-aware detection
    {
        imported_texture texture;
        if(get_workflow_aware_texture(material, workflow, "BaseColor", texture, get_imported_texture, false))
        {
            if(workflow == material_workflow::specular_gloss)
            {
                // For spec-gloss we DO NOT extract the source diffuse/specular to disk.
                // Instead we decode them directly from pcData (or from an external file
                // when not embedded), run the conversion in memory, and write only the
                // final-semantic outputs. This keeps the asset browser clean:
                //   [N] BaseColor <model>.png
                //   [M] MetallicRoughness <model>.png
                aiString specular_path{};
                bool has_specular = (material->GetTexture(aiTextureType_SPECULAR, 0, &specular_path) == AI_SUCCESS)
                                    && specular_path.length > 0;

                // Read per-material factors from the KHR_materials_pbrSpecularGlossiness
                // extension and bake them into the converted textures. Defaults from the
                // glTF spec: diffuseFactor=1, specularFactor=1, glossinessFactor=1.
                spec_gloss_factors_t factors{};
                {
                    aiColor4D diffuse_factor_color{1.0f, 1.0f, 1.0f, 1.0f};
                    if(material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse_factor_color) == AI_SUCCESS)
                    {
                        factors.diffuse_r = diffuse_factor_color.r;
                        factors.diffuse_g = diffuse_factor_color.g;
                        factors.diffuse_b = diffuse_factor_color.b;
                        factors.diffuse_a = diffuse_factor_color.a;
                    }

                    aiColor3D specular_factor_color{1.0f, 1.0f, 1.0f};
                    material->Get(AI_MATKEY_COLOR_SPECULAR, specular_factor_color);
                    float specular_factor_scalar = 1.0f;
                    material->Get(AI_MATKEY_SPECULAR_FACTOR, specular_factor_scalar);
                    factors.specular_r = specular_factor_color.r * specular_factor_scalar;
                    factors.specular_g = specular_factor_color.g * specular_factor_scalar;
                    factors.specular_b = specular_factor_color.b * specular_factor_scalar;

                    float glossiness_factor = 1.0f;
                    material->Get(AI_MATKEY_GLOSSINESS_FACTOR, glossiness_factor);
                    factors.glossiness = glossiness_factor;
                }

                // Load diffuse — pcData if embedded, file on disk if external.
                bimg::ImageContainer* diffuse_img = nullptr;
                if(texture.embedded_index >= 0 && texture.embedded_index < static_cast<int>(scene->mNumTextures))
                {
                    const auto* embedded = scene->mTextures[texture.embedded_index];
                    if(embedded->pcData && embedded->mHeight == 0)
                    {
                        diffuse_img = imageLoad(embedded->pcData, static_cast<uint32_t>(embedded->mWidth));
                    }
                }
                else
                {
                    fs::path diffuse_file = output_dir / texture.name;
                    diffuse_img = imageLoad(bx::FilePath(diffuse_file.string().c_str()));
                }

                // Load specular.
                bimg::ImageContainer* specular_img = nullptr;
                int specular_idx = -1;
                if(has_specular)
                {
                    auto spec_pair = scene->GetEmbeddedTextureAndIndex(specular_path.C_Str());
                    if(spec_pair.first && spec_pair.first->pcData && spec_pair.first->mHeight == 0)
                    {
                        specular_img = imageLoad(spec_pair.first->pcData,
                                                 static_cast<uint32_t>(spec_pair.first->mWidth));
                        specular_idx = spec_pair.second;
                    }
                    else if(!spec_pair.first)
                    {
                        fs::path specular_file = output_dir / specular_path.C_Str();
                        specular_img = imageLoad(bx::FilePath(specular_file.string().c_str()));
                    }
                }

                if(!diffuse_img)
                {
                    APPLOG_WARNING("Mesh Importer: Spec-gloss conversion skipped — failed to decode diffuse texture");
                }
                if(has_specular && !specular_img)
                {
                    APPLOG_WARNING("Mesh Importer: Spec-gloss conversion skipped — failed to decode specular texture: {}", specular_path.C_Str());
                }

                if(diffuse_img && specular_img)
                {
                    const std::string base_color_relative =
                        build_converted_name(texture.embedded_index, texture.name, "BaseColor");
                    const std::string mr_relative =
                        build_converted_name(specular_idx, specular_path.C_Str(), "MetallicRoughness");

                    auto conv = convert_spec_gloss_to_pbr_textures(output_dir,
                                                                   base_color_relative,
                                                                   mr_relative,
                                                                   diffuse_img,
                                                                   specular_img,
                                                                   factors);
                    if(conv.diffuse_converted)
                    {
                        // Replace the texture name with the converted output, and suppress
                        // extraction of the raw source under whatever auto-name it would
                        // otherwise get.
                        texture.name = conv.base_color_relative;
                        mark_embedded_consumed(texture.embedded_index);
                        sg_textures_converted = true;
                        APPLOG_TRACE("Mesh Importer: Wrote converted base color (factors baked: D[{:.2f},{:.2f},{:.2f},{:.2f}] S[{:.2f},{:.2f},{:.2f}] G[{:.2f}]): {}",
                                     factors.diffuse_r, factors.diffuse_g, factors.diffuse_b, factors.diffuse_a,
                                     factors.specular_r, factors.specular_g, factors.specular_b, factors.glossiness,
                                     texture.name);
                    }
                    if(!conv.mr_relative.empty())
                    {
                        combined_mr_relative = conv.mr_relative;
                        mark_embedded_consumed(specular_idx);
                        APPLOG_TRACE("Mesh Importer: Wrote converted metallic-roughness: {}", combined_mr_relative);
                    }
                }
                else
                {
                    // Conversion didn't run — fall back to extracting the source so we
                    // still ship *something* in the color slot, and the later texture
                    // assignment finds a valid file.
                    process_texture(texture, textures);
                }

                if(specular_img)
                {
                    bimg::imageFree(specular_img);
                }
                if(diffuse_img)
                {
                    bimg::imageFree(diffuse_img);
                }
            }
            else
            {
                // Non-SG: extract the source as usual.
                process_texture(texture, textures);
            }

            auto key = fs::convert_to_protocol(output_dir / texture.name);
            mat.set_color_map(am.get_asset<gfx::texture>(key.generic_string()));
        }
    }
    // BASE COLOR PROPERTY - Use workflow-aware conversion
    {
        aiColor3D base_color_property{1.0f, 1.0f, 1.0f};
        float metallic_property = 0.0f;
        float roughness_property = 0.5f;

        if(sg_textures_converted)
        {
            // The per-pixel SG conversion already baked the diffuseFactor, specularFactor
            // and glossinessFactor into the texture output. The shader does
            // `albedo = sample * u_base_color`, `metalness = u_surface_metalness * tex.b`,
            // `roughness = u_surface_roughness * tex.g` — so we MUST set the per-material
            // factors to identity here to avoid double-applying them.
            base_color_property = {1.0f, 1.0f, 1.0f};
            metallic_property = 1.0f;
            roughness_property = 1.0f;
        }
        else
        {
            // No SG-texture conversion ran (no textures or non-SG workflow): fall back to
            // the per-material conversion that maps property values from the material's
            // workflow into MR equivalents.
            process_material_with_workflow_conversion(material, workflow,
                                                      base_color_property,
                                                      metallic_property,
                                                      roughness_property);
        }

        math::color base_color{};
        base_color = {base_color_property.r, base_color_property.g, base_color_property.b};
        base_color = math::clamp(base_color.value, 0.0f, 1.0f);
        mat.set_base_color(base_color);

        mat.set_metalness(math::clamp(metallic_property, 0.0f, 1.0f));
        mat.set_roughness(math::clamp(roughness_property, 0.0f, 1.0f));
    }

    // METALLIC & ROUGHNESS TEXTURES - Check for duplicate specular usage first
    bool uses_duplicate_specular = detect_duplicate_specular_usage(material, workflow);
    bool has_metallic_tex = false;
    bool has_roughness_tex = false;

    if(!combined_mr_relative.empty())
    {
        // The diffuse+specular conversion above already produced a sibling MR texture using the
        // proper per-pixel metallic solve. Use it directly and skip the cheaper SpecularToMetallic /
        // SpecularToRoughness fallback path that would otherwise overwrite this with a coarser result.
        auto key = fs::convert_to_protocol(output_dir / combined_mr_relative);
        auto texture_asset = am.get_asset<gfx::texture>(key.generic_string());

        mat.set_metalness_map(texture_asset);
        mat.set_roughness_map(texture_asset);
        has_metallic_tex = true;
        has_roughness_tex = true;

        APPLOG_TRACE("Mesh Importer: Using sibling metallic-roughness map from spec-gloss conversion: {}", combined_mr_relative);
    }
    else if(uses_duplicate_specular)
    {
        imported_texture combined_texture;
        if(get_imported_texture(material, aiTextureType_SPECULAR, 0, "SpecularToMetallicRoughness", combined_texture))
        {
            process_texture(combined_texture, textures);
            
            auto key = fs::convert_to_protocol(output_dir / combined_texture.name);
            auto texture_asset = am.get_asset<gfx::texture>(key.generic_string());
            
            mat.set_metalness_map(texture_asset);
            mat.set_roughness_map(texture_asset);
            has_metallic_tex = true;
            has_roughness_tex = true;
            
            APPLOG_TRACE("Mesh Importer: Converting single specular texture to combined metallic/roughness: {}", combined_texture.name);
        }
    }
    else
    {
        {
            imported_texture texture;
            if(get_workflow_aware_texture(material, workflow, "Metallic", texture, get_imported_texture, uses_duplicate_specular))
            {
                process_texture(texture, textures);

                auto key = fs::convert_to_protocol(output_dir / texture.name);
                mat.set_metalness_map(am.get_asset<gfx::texture>(key.generic_string()));
                has_metallic_tex = true;
                
                if(texture.semantic == "SpecularToMetallic")
                {
                    APPLOG_TRACE("Mesh Importer: Converting specular texture to metallic: {}", texture.name);
                }
            }
        }
        
        {
            imported_texture texture;
            if(get_workflow_aware_texture(material, workflow, "Roughness", texture, get_imported_texture, uses_duplicate_specular))
            {
                process_texture(texture, textures);

                auto key = fs::convert_to_protocol(output_dir / texture.name);
                mat.set_roughness_map(am.get_asset<gfx::texture>(key.generic_string()));
                has_roughness_tex = true;
                
                if(texture.semantic == "GlossToRoughness")
                {
                    APPLOG_TRACE("Mesh Importer: Converting gloss texture to roughness: {}", texture.name);
                }
                else if(texture.semantic == "SpecularToRoughness")
                {
                    APPLOG_TRACE("Mesh Importer: Converting specular texture to roughness: {}", texture.name);
                }
            }
        }
    }

    // Converted textures already contain the full PBR values. The shader multiplies
    // factor × texture, so set factors to 1.0 to avoid double-application.
    if(workflow == material_workflow::specular_gloss)
    {
        if(has_metallic_tex)
        {
            mat.set_metalness(1.0f);
        }
        if(has_roughness_tex)
        {
            mat.set_roughness(1.0f);
        }
    }

    // NORMAL TEXTURE
    aiTextureType normals_type = aiTextureType_NORMALS;
    {
        static const std::string semantic = "Normals";

        imported_texture texture;
        bool has_texture = false;

        if(!has_texture)
        {
            has_texture |= get_imported_texture(material, aiTextureType_NORMALS, 0, semantic, texture);
        }

        if(!has_texture)
        {
            has_texture |= get_imported_texture(material, aiTextureType_NORMAL_CAMERA, 0, semantic, texture);

            if(has_texture)
            {
                normals_type = aiTextureType_NORMAL_CAMERA;
            }
        }

        if(has_texture)
        {
            process_texture(texture, textures);

            auto key = fs::convert_to_protocol(output_dir / texture.name);
            mat.set_normal_map(am.get_asset<gfx::texture>(key.generic_string()));
        }
    }
    // NORMAL BUMP PROPERTY
    {
        ai_real property{};
        bool has_property = false;

        if(!has_property)
        {
            has_property |= material->Get(AI_MATKEY_GLTF_TEXTURE_SCALE(normals_type, 0), property) == AI_SUCCESS;
        }

        if(has_property)
        {
            mat.set_bumpiness(property);
        }
    }

    // OCCLUSION TEXTURE
    aiTextureType occlusion_type = aiTextureType_AMBIENT_OCCLUSION;
    {
        static const std::string semantic = "Occlusion";

        imported_texture texture;
        bool has_texture = false;

        if(!has_texture)
        {
            has_texture |= get_imported_texture(material, aiTextureType_AMBIENT_OCCLUSION, 0, semantic, texture);
        }

        if(!has_texture)
        {
            has_texture |= get_imported_texture(material, aiTextureType_AMBIENT, 0, semantic, texture);

            if(has_texture)
            {
                occlusion_type = aiTextureType_AMBIENT;
            }
        }

        if(!has_texture)
        {
            has_texture |= get_imported_texture(material, aiTextureType_LIGHTMAP, 0, semantic, texture);
            if(has_texture)
            {
                occlusion_type = aiTextureType_LIGHTMAP;
            }
        }

        if(has_texture)
        {
            process_texture(texture, textures);

            auto key = fs::convert_to_protocol(output_dir / texture.name);
            mat.set_ao_map(am.get_asset<gfx::texture>(key.generic_string()));
        }
    }

    // OCCLUSION STERNGTH PROPERTY
    {
        ai_real property{};
        bool has_property = false;

        if(!has_property)
        {
            has_property |= material->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(occlusion_type, 0), property) == AI_SUCCESS;
        }

        if(has_property)
        {
        }
    }

    // EMISSIVE TEXTURE
    {
        static const std::string semantic = "Emissive";

        imported_texture texture;
        bool has_texture = false;

        if(!has_texture)
        {
            has_texture |= get_imported_texture(material, aiTextureType_EMISSION_COLOR, 0, semantic, texture);
        }

        if(!has_texture)
        {
            has_texture |= get_imported_texture(material, aiTextureType_EMISSIVE, 0, semantic, texture);
        }

        if(has_texture)
        {
            process_texture(texture, textures);

            auto key = fs::convert_to_protocol(output_dir / texture.name);
            mat.set_emissive_map(am.get_asset<gfx::texture>(key.generic_string()));
        }
    }
    // EMISSIVE COLOR PROPERTY
    {
        aiColor3D property{};
        bool has_property = false;

        if(!has_property)
        {
            has_property |= material->Get(AI_MATKEY_COLOR_EMISSIVE, property) == AI_SUCCESS;
        }

        if(has_property)
        {
            math::color emissive{};
            emissive = {property.r, property.g, property.b};
            emissive = math::clamp(emissive.value, 0.0f, 1.0f);
            mat.set_emissive_color(emissive);
        }
    }
}

void process_materials(asset_manager& am,
                       const fs::path& filename,
                       const fs::path& output_dir,
                       const aiScene* scene,
                       std::vector<imported_material>& materials,
                       std::vector<imported_texture>& textures)
{
    if(scene->mNumMaterials > 0)
    {
        materials.resize(scene->mNumMaterials);
    }

    for(size_t i = 0; i < scene->mNumMaterials; ++i)
    {
        const aiMaterial* assimp_mat = scene->mMaterials[i];

        auto mat = std::make_shared<pbr_material>();
        process_material(am, filename, output_dir, scene, assimp_mat, *mat, textures);
        std::string assimp_mat_name = assimp_mat->GetName().C_Str();
        if(assimp_mat_name.empty())
        {
            assimp_mat_name = fmt::format("Material {}", filename.string());
        }
        materials[i].mat = mat;
        materials[i].name = string_utils::replace(fmt::format("[{}] {}", i, assimp_mat_name), ".", "_");
    }
}

void process_embedded_textures(asset_manager& am,
                               const fs::path& filename,
                               const fs::path& output_dir,
                               const aiScene* scene,
                               std::vector<imported_texture>& textures)
{
    if(scene->mNumTextures > 0)
    {
        for(size_t i = 0; i < scene->mNumTextures; ++i)
        {
            const aiTexture* assimp_tex = scene->mTextures[i];

            process_embedded_texture(assimp_tex, i, filename, output_dir, textures);
        }
    }
}

void process_imported_scene(asset_manager& am,
                            const fs::path& filename,
                            const fs::path& output_dir,
                            const aiScene* scene,
                            mesh::load_data& load_data,
                            std::vector<animation_clip>& animations,
                            std::vector<imported_material>& materials,
                            std::vector<imported_texture>& textures)
{
    int meshes_with_bones = 0;
    int meshes_without_bones = 0;

    APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "Mesh Importer: Parse Imported Data");

    load_data.vertex_format = gfx::mesh_vertex::get_layout();

    auto name_to_index_lut = assign_node_indices(scene);

    APPLOG_TRACE("Mesh Importer: Processing materials ...");
    process_materials(am, filename, output_dir, scene, materials, textures);

    APPLOG_TRACE("Mesh Importer: Processing embedded textures ...");
    process_embedded_textures(am, filename, output_dir, scene, textures);

    APPLOG_TRACE("Mesh Importer: Processing meshes ...");
    process_meshes(scene, load_data);

    APPLOG_TRACE("Mesh Importer: Processing nodes ...");
    process_nodes(scene, load_data, name_to_index_lut);

    APPLOG_TRACE("Mesh Importer: Processing animations ...");
    process_animations(scene, filename, load_data, name_to_index_lut, animations);

    APPLOG_TRACE("Mesh Importer: Processing animations bounding boxes ...");
    auto boxes = compute_bounding_boxes_for_animations(scene);

    if(!boxes.empty())
    {
        load_data.bbox = {};
        for(const auto& kvp : boxes)
        {
            for(const auto& box : kvp.second)
            {
                load_data.bbox.add_point(box.min);
                load_data.bbox.add_point(box.max);
            }
        }
    }
    else if(!load_data.bbox.is_populated())
    {
        for(const auto& submesh : load_data.submeshes)
        {
            load_data.bbox.add_point(submesh.bbox.min);
            load_data.bbox.add_point(submesh.bbox.max);
        }
    }

    APPLOG_TRACE("Mesh Importer: bbox min {}, max {}", load_data.bbox.min, load_data.bbox.max);
}

auto read_file(Assimp::Importer& importer, const fs::path& file, uint32_t flags) -> const aiScene*
{
    APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "Importer Read File");
    return importer.ReadFile(file.string(), flags);
}

/**
 * @brief Perceived brightness using ITU BT.601 luminance coefficients.
 * Matches the reference Khronos/Babylon.js conversion utilities.
 */
auto perceived_brightness(float r, float g, float b) -> float
{
    return std::sqrt(0.299f * r * r + 0.587f * g * g + 0.114f * b * b);
}

/**
 * @brief Solve for metallic using the official Khronos quadratic formula.
 * Reference: babylon.pbrUtilities.js solveMetallic(), lygia/lighting/toMetallic.glsl
 *
 * The PBR identity for specular is: specular = lerp(dielectricF0, baseColor, metallic)
 * Combined with the diffuse identity, this yields a quadratic in metallic that we solve here.
 *
 * IMPORTANT: this routine is intentionally evaluated in sRGB-ENCODED float space (i.e. 8-bit
 * channels divided by 255), matching the reference Khronos/Babylon implementation. The
 * BT.601 perceptual luminance in `perceived_brightness` and the dielectric F0=0.04 are both
 * calibrated for that color space. Do NOT degamma to linear before calling — it will skew
 * the metallic estimate.
 */
auto solve_metallic(float perceived_diffuse, float perceived_specular, float one_minus_specular_strength) -> float
{
    constexpr float dielectric_f0 = 0.04f;

    if(perceived_specular < dielectric_f0)
    {
        return 0.0f;
    }

    float a = dielectric_f0;
    float b = perceived_diffuse * one_minus_specular_strength / (1.0f - dielectric_f0) + perceived_specular - 2.0f * dielectric_f0;
    float c = dielectric_f0 - perceived_specular;
    float discriminant = std::max(b * b - 4.0f * a * c, 0.0f);

    return math::clamp((-b + std::sqrt(discriminant)) / (2.0f * a), 0.0f, 1.0f);
}

/**
 * @brief Convert specular/gloss to metallic/roughness using official Khronos formulas.
 * Reference: glTF KHR_materials_pbrSpecularGlossiness specification appendix,
 *            babylon.pbrUtilities.js ConvertToMetallicRoughness()
 */
auto convert_specular_gloss_to_metallic_roughness(const aiColor3D& diffuse_color,
                                                 const aiColor3D& specular_color,
                                                 float glossiness_factor) -> std::tuple<aiColor3D, float, float>
{
    constexpr float dielectric_f0 = 0.04f;
    constexpr float epsilon = 1e-6f;

    float max_specular = std::max({specular_color.r, specular_color.g, specular_color.b});
    float one_minus_specular_strength = 1.0f - max_specular;

    float perceived_diffuse = perceived_brightness(diffuse_color.r, diffuse_color.g, diffuse_color.b);
    float perceived_specular = perceived_brightness(specular_color.r, specular_color.g, specular_color.b);

    float metallic = solve_metallic(perceived_diffuse, perceived_specular, one_minus_specular_strength);

    // Khronos/Babylon reference formula for base color reconstruction:
    //   baseColorFromDiffuse  = diffuse * (1 - F0) / (1 - metallic * F0)
    //   baseColorFromSpecular = specular - F0 * (1 - metallic)
    //   baseColor = mix(baseColorFromDiffuse, baseColorFromSpecular, metallic²)
    float denom = std::max(1.0f - metallic * dielectric_f0, epsilon);
    float spec_offset = dielectric_f0 * (1.0f - metallic);

    auto base_from_diffuse = [&](float d) -> float { return d * (1.0f - dielectric_f0) / denom; };
    auto base_from_specular = [&](float s) -> float { return s - spec_offset; };

    float t = metallic * metallic;
    aiColor3D base_color;
    base_color.r = math::mix(base_from_diffuse(diffuse_color.r), base_from_specular(specular_color.r), t);
    base_color.g = math::mix(base_from_diffuse(diffuse_color.g), base_from_specular(specular_color.g), t);
    base_color.b = math::mix(base_from_diffuse(diffuse_color.b), base_from_specular(specular_color.b), t);

    float roughness = 1.0f - glossiness_factor;

    base_color.r = math::clamp(base_color.r, 0.0f, 1.0f);
    base_color.g = math::clamp(base_color.g, 0.0f, 1.0f);
    base_color.b = math::clamp(base_color.b, 0.0f, 1.0f);
    metallic = math::clamp(metallic, 0.0f, 1.0f);
    roughness = math::clamp(roughness, 0.0f, 1.0f);

    return std::make_tuple(base_color, metallic, roughness);
}



} // namespace

void mesh_importer_init()
{
    struct log_stream : public Assimp::LogStream
    {
        log_stream(Assimp::Logger::ErrorSeverity s) : severity(s)
        {
        }

        void write(const char* message) override
        {
            switch(severity)
            {
                case Assimp::Logger::Info:
                    APPLOG_INFO("Mesh Importer: {0}", message);
                    break;
                case Assimp::Logger::Warn:
                    APPLOG_WARNING("Mesh Importer: {0}", message);
                    break;
                case Assimp::Logger::Err:
                    APPLOG_ERROR("Mesh Importer: {0}", message);
                    break;
                default:
                    APPLOG_TRACE("Mesh Importer: {0}", message);
                    break;
            }
        }

        Assimp::Logger::ErrorSeverity severity{};
    };

    // if(Assimp::DefaultLogger::isNullLogger())
    // {
    //     auto logger = Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE);

    //     logger->attachStream(new log_stream(Assimp::Logger::Debugging), Assimp::Logger::Debugging);
    //     logger->attachStream(new log_stream(Assimp::Logger::Info), Assimp::Logger::Info);
    //     logger->attachStream(new log_stream(Assimp::Logger::Warn), Assimp::Logger::Warn);
    //     logger->attachStream(new log_stream(Assimp::Logger::Err), Assimp::Logger::Err);
    // }
}

auto load_mesh_data_from_file(asset_manager& am,
                              const fs::path& path,
                              const mesh_importer_meta& import_meta,
                              mesh::load_data& load_data,
                              std::vector<animation_clip>& animations,
                              std::vector<imported_material>& materials,
                              std::vector<imported_texture>& textures) -> bool
{
    Assimp::Importer importer;

    int rvc_flags = aiComponent_CAMERAS | aiComponent_LIGHTS;

    if(!import_meta.model.import_meshes)
    {
        rvc_flags |= aiComponent_MESHES;
    }

    if(!import_meta.animations.import_animations)
    {
        rvc_flags |= aiComponent_ANIMATIONS;
    }

    if(!import_meta.materials.import_materials)
    {
        rvc_flags |= aiComponent_MATERIALS;
    }

    importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, rvc_flags);
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
    importer.SetPropertyBool(AI_CONFIG_FBX_CONVERT_TO_M, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

    fs::path file = path.stem();
    fs::path output_dir = path.parent_path();

    // clang-format off

    uint32_t flags = aiProcess_FlipUVs                      |
                     aiProcess_RemoveComponent              |
                     aiProcess_Triangulate                  |
                     aiProcess_CalcTangentSpace             |
                     aiProcess_GenUVCoords                  |
                     aiProcess_GenSmoothNormals             |
                     aiProcess_GenBoundingBoxes             |
                     aiProcess_ImproveCacheLocality         |
                     aiProcess_LimitBoneWeights             |
                     aiProcess_SortByPType                  |
                     aiProcess_TransformUVCoords            |
                     aiProcess_GlobalScale;

    // clang-format on

    if(import_meta.model.weld_vertices)
    {
        flags |= aiProcess_JoinIdenticalVertices;
    }
    if(import_meta.model.optimize_meshes)
    {
        flags |= aiProcess_OptimizeMeshes;
    }
    if(import_meta.model.split_large_meshes)
    {
        flags |= aiProcess_SplitLargeMeshes;
    }
    if(import_meta.model.find_degenerates)
    {
        flags |= aiProcess_FindDegenerates;
    }
    if(import_meta.model.find_invalid_data)
    {
        flags |= aiProcess_FindInvalidData;
    }
    if(import_meta.materials.remove_redundant_materials)
    {
        flags |= aiProcess_RemoveRedundantMaterials;
    }

    APPLOG_TRACE("Mesh Importer: Loading {}", path.generic_string());

    const aiScene* scene = read_file(importer, path, flags);

    if(scene == nullptr)
    {
        APPLOG_ERROR(importer.GetErrorString());
        return false;
    }

    // We need to modify the scene, so we cast away const (be cautious in production).
    aiScene* modScene = const_cast<aiScene*>(scene);

    //CollapseAssimpFBXPivotsAndAnimations(modScene);
    process_imported_scene(am, file, output_dir, modScene, load_data, animations, materials, textures);

    APPLOG_TRACE("Mesh Importer: Done with {}", path.generic_string());

    return true;
}
} // namespace importer

} // namespace unravel
