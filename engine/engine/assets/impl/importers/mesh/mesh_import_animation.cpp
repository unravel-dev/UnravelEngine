#include "mesh_import_animation.h"

#include "mesh_import_paths.h"

#include <logging/logging.h>
#include <string_utils/utils.h>

#include <assimp/scene.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace unravel
{
namespace importer
{
namespace mesh_import
{
namespace
{

/// Fallback when a file reports no tick rate (Assimp convention).
constexpr double k_default_ticks_per_second = 25.0;
constexpr double k_min_ticks_per_second = 0.001;

enum class channel_requirement
{
    translation,
    rotation
};

using node_name_set_t = std::unordered_set<std::string>;

/// The channel of @p animation that animates @p node_name, or null.
auto find_channel(const aiAnimation* animation, const std::string& node_name) -> const aiNodeAnim*
{
    for(unsigned int ch = 0; animation != nullptr && ch < animation->mNumChannels; ++ch)
    {
        const aiNodeAnim* channel = animation->mChannels[ch];
        if(channel && node_name == channel->mNodeName.C_Str())
        {
            return channel;
        }
    }
    return nullptr;
}

/**
 * @brief Whether @p animation keys @p node_name more than once in the required kind. A channel keyed
 * with one value throughout counts too: an in-place clip keys its root bone at the origin, and that
 * root, not the hips below it, owns the clip's (absent) root motion.
 */
auto has_animated_channel(const aiAnimation* animation, const std::string& node_name, channel_requirement requirement)
    -> bool
{
    const aiNodeAnim* channel = find_channel(animation, node_name);
    if(channel == nullptr)
    {
        return false;
    }
    const unsigned int key_count =
        requirement == channel_requirement::rotation ? channel->mNumRotationKeys : channel->mNumPositionKeys;
    return key_count > 1;
}

/// Skeleton nodes in the subtree of @p node, @p node included.
auto count_skeleton_nodes(const aiNode* node, const node_name_set_t& skeleton_nodes) -> size_t
{
    size_t count = skeleton_nodes.count(node->mName.C_Str());
    for(unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        count += count_skeleton_nodes(node->mChildren[i], skeleton_nodes);
    }
    return count;
}

/**
 * @brief The child of @p node the skeleton's trunk continues in, or null where the skeleton
 * branches (or ends). Under the scene root, a file holding several rigs side by side (a character
 * and a prop, a character and a camera rig) continues in the one with the most skeleton nodes.
 */
auto find_trunk_child(const aiNode* node, bool is_scene_root, const node_name_set_t& skeleton_nodes) -> const aiNode*
{
    const aiNode* found = nullptr;
    size_t found_size = 0;
    bool branches = false;
    for(unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        const aiNode* child = node->mChildren[i];
        if(skeleton_nodes.count(child->mName.C_Str()) == 0)
        {
            continue;
        }
        if(found == nullptr)
        {
            found = child;
            continue;
        }
        branches = true;
        if(!is_scene_root)
        {
            break;
        }
        const size_t child_size = count_skeleton_nodes(child, skeleton_nodes);
        if(found_size == 0)
        {
            found_size = count_skeleton_nodes(found, skeleton_nodes);
        }
        if(child_size > found_size)
        {
            found = child;
            found_size = child_size;
        }
    }
    return branches && !is_scene_root ? nullptr : found;
}

/**
 * @brief The node carrying a clip's root motion, on the skeleton's trunk.
 *
 * The trunk is the chain from the scene root down to the first node where the skeleton branches -
 * the hips, where the legs and the spine part. The shallowest trunk node that @p animation animates
 * as @p requirement asks carries the motion. Root motion belongs to the skeleton's root: a rigid
 * prop's pivot or a light swaying in a level must not move the entity playing the clip, and neither
 * may a limb - a seated loop that keeps its hips still and only animates the spine has no root
 * motion, so it gets the hips, which report none.
 *
 * A translated node satisfies a rotation request too, so the rotation root is the shallowest node
 * that moves at all: for a rig with a translation-only root bone above the hips it stays on that
 * root (no turn to extract) rather than moving to the hips, whose sway would turn the entity.
 */
auto find_root_motion_node(const aiScene* scene,
                           const aiAnimation* animation,
                           const node_name_set_t& skeleton_nodes,
                           channel_requirement requirement) -> const aiNode*
{
    if(!scene || !scene->mRootNode)
    {
        return nullptr;
    }
    const aiNode* current = scene->mRootNode;
    while(true)
    {
        const std::string name = current->mName.C_Str();
        const bool is_skeleton = skeleton_nodes.count(name) > 0;
        const bool is_animated = has_animated_channel(animation, name, requirement) ||
                                 has_animated_channel(animation, name, channel_requirement::translation);
        if(is_animated && is_skeleton)
        {
            return current;
        }
        const aiNode* next = find_trunk_child(current, current == scene->mRootNode, skeleton_nodes);
        if(next == nullptr)
        {
            return is_skeleton && find_channel(animation, name) != nullptr ? current : nullptr;
        }
        current = next;
    }
}

/// Insert @p node and the name of every ancestor of it.
void insert_node_and_ancestors(const aiNode* node, node_name_set_t& names)
{
    for(const aiNode* current = node; current != nullptr; current = current->mParent)
    {
        names.insert(current->mName.C_Str());
    }
}

/// Insert every node that holds meshes together with its ancestors.
void collect_mesh_nodes_and_ancestors(const aiNode* node, node_name_set_t& names)
{
    if(node->mNumMeshes > 0)
    {
        insert_node_and_ancestors(node, names);
    }
    for(unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        collect_mesh_nodes_and_ancestors(node->mChildren[i], names);
    }
}

/// Insert every node a clip of @p scene animates together with its ancestors.
void collect_animated_nodes_and_ancestors(const aiScene* scene, node_name_set_t& names)
{
    for(unsigned int a = 0; a < scene->mNumAnimations; ++a)
    {
        const aiAnimation* animation = scene->mAnimations[a];
        for(unsigned int ch = 0; animation != nullptr && ch < animation->mNumChannels; ++ch)
        {
            const aiNodeAnim* channel = animation->mChannels[ch];
            if(channel != nullptr)
            {
                insert_node_and_ancestors(scene->mRootNode->FindNode(channel->mNodeName), names);
            }
        }
    }
}

/**
 * @brief Names of the skeleton: every bone and every ancestor of one. A file without meshes (an
 * animation-only export) lists no bones: there the skeleton is every animated node and its ancestors.
 */
auto collect_skeleton_nodes(const aiScene* scene) -> node_name_set_t
{
    if(scene->mNumMeshes == 0)
    {
        node_name_set_t animated;
        if(scene->mRootNode)
        {
            collect_animated_nodes_and_ancestors(scene, animated);
        }
        return animated;
    }
    node_name_set_t names;
    const aiNode* root = scene->mRootNode;
    for(unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        const aiMesh* mesh = scene->mMeshes[i];
        for(unsigned int j = 0; j < mesh->mNumBones; ++j)
        {
            names.insert(mesh->mBones[j]->mName.C_Str());
            if(root)
            {
                insert_node_and_ancestors(root->FindNode(mesh->mBones[j]->mName), names);
            }
        }
    }
    return names;
}

/**
 * @brief Names of the nodes whose channels a clip keeps: the skeleton, and nodes holding meshes and
 * their ancestors. Other nodes animate nothing the mesh can show.
 */
auto collect_relevant_animation_nodes(const aiScene* scene, const node_name_set_t& skeleton_nodes) -> node_name_set_t
{
    node_name_set_t names = skeleton_nodes;
    if(scene->mRootNode)
    {
        collect_mesh_nodes_and_ancestors(scene->mRootNode, names);
    }
    return names;
}

/// The node sets a clip is filtered with: channels kept, and nodes that may drive root motion.
struct animation_node_sets
{
    node_name_set_t relevant;
    node_name_set_t skeleton;
};

template<typename Key, typename AssimpKey, typename CopyValue>
void copy_keys(std::vector<Key>& keys,
               const AssimpKey* assimp_keys,
               unsigned int count,
               double ticks_per_second,
               CopyValue copy_value)
{
    keys.resize(count);
    for(unsigned int idx = 0; idx < count; ++idx)
    {
        keys[idx].time = decltype(keys[idx].time)(assimp_keys[idx].mTime / ticks_per_second);
        copy_value(keys[idx].value, assimp_keys[idx].mValue);
    }
}

void copy_channel_keys(const aiNodeAnim* assimp_node_anim, double ticks_per_second, animation_channel& node_anim)
{
    const auto copy_vec3 = [](math::vec3& value, const aiVector3D& source)
    {
        value.x = source.x;
        value.y = source.y;
        value.z = source.z;
    };
    copy_keys(node_anim.position_keys,
              assimp_node_anim->mPositionKeys,
              assimp_node_anim->mNumPositionKeys,
              ticks_per_second,
              copy_vec3);
    copy_keys(node_anim.rotation_keys,
              assimp_node_anim->mRotationKeys,
              assimp_node_anim->mNumRotationKeys,
              ticks_per_second,
              [](math::quat& value, const aiQuaternion& source)
              {
                  value.x = source.x;
                  value.y = source.y;
                  value.z = source.z;
                  value.w = source.w;
              });
    copy_keys(node_anim.scaling_keys,
              assimp_node_anim->mScalingKeys,
              assimp_node_anim->mNumScalingKeys,
              ticks_per_second,
              copy_vec3);
}

void assign_root_motion_nodes(const aiScene* scene,
                              const aiAnimation* assimp_anim,
                              const node_name_set_t& skeleton_nodes,
                              node_index_lut_t& node_to_index_lut,
                              animation_clip& anim)
{
    const aiNode* translation_node =
        find_root_motion_node(scene, assimp_anim, skeleton_nodes, channel_requirement::translation);
    const aiNode* rotation_node = find_root_motion_node(scene, assimp_anim, skeleton_nodes, channel_requirement::rotation);
    if(translation_node)
    {
        anim.root_motion.position_node_name = translation_node->mName.C_Str();
        anim.root_motion.position_node_index = node_to_index_lut[anim.root_motion.position_node_name];
    }
    if(rotation_node)
    {
        anim.root_motion.rotation_node_name = rotation_node->mName.C_Str();
        anim.root_motion.rotation_node_index = node_to_index_lut[anim.root_motion.rotation_node_name];
    }
}

auto to_transform(const aiMatrix4x4& matrix) -> math::transform
{
    aiVector3D scaling;
    aiQuaternion rotation;
    aiVector3D position;
    matrix.Decompose(scaling, rotation, position);
    math::transform result;
    result.set_position({position.x, position.y, position.z});
    result.set_rotation(math::normalize(math::quat(rotation.w, rotation.x, rotation.y, rotation.z)));
    result.set_scale({scaling.x, scaling.y, scaling.z});
    return result;
}

/**
 * @brief Records the skeleton the keys were authored against: each channel's rest local transform,
 * the channel animating its nearest ancestor, and the rest transform of the unanimated nodes in
 * between. Rest-aligned retargeting maps poses from this skeleton onto the playing model's, so clips
 * from files with other root frames, units or proportions play correctly.
 *
 * Runs after the channels are sorted, since it stores channel indices.
 */
void record_source_rest_pose(const aiScene* scene, animation_clip& anim)
{
    if(!scene || !scene->mRootNode)
    {
        return;
    }
    std::unordered_map<std::string, int> channel_of;
    for(size_t i = 0; i < anim.channels.size(); ++i)
    {
        channel_of[anim.channels[i].node_name] = int(i);
    }
    for(auto& channel : anim.channels)
    {
        const aiNode* node = scene->mRootNode->FindNode(channel.node_name.c_str());
        if(node == nullptr)
        {
            continue;
        }
        channel.rest_local = to_transform(node->mTransformation);
        // Walk up until an animated ancestor, accumulating the unanimated nodes on the way.
        aiMatrix4x4 offset;
        channel.parent_channel = -1;
        for(const aiNode* ancestor = node->mParent; ancestor != nullptr; ancestor = ancestor->mParent)
        {
            auto it = channel_of.find(ancestor->mName.C_Str());
            if(it != channel_of.end())
            {
                channel.parent_channel = it->second;
                break;
            }
            offset = ancestor->mTransformation * offset;
        }
        channel.parent_offset = to_transform(offset);
        channel.has_rest = true;
    }
}

void process_animation(const aiScene* scene,
                       const fs::path& filename,
                       const aiAnimation* assimp_anim,
                       const animation_node_sets& node_sets,
                       node_index_lut_t& node_to_index_lut,
                       animation_clip& anim)
{
    anim.name = replace_invalid_file_name_characters(filename.string() + "_" +
                                                     string_utils::replace(assimp_anim->mName.C_Str(), ".", "_"));
    double ticks_per_second = assimp_anim->mTicksPerSecond;
    if(ticks_per_second < k_min_ticks_per_second)
    {
        ticks_per_second = k_default_ticks_per_second;
    }
    anim.duration = decltype(anim.duration)(assimp_anim->mDuration / ticks_per_second);
    anim.channels.reserve(assimp_anim->mNumChannels);
    bool needs_sort = false;
    size_t skipped = 0;
    for(unsigned int i = 0; i < assimp_anim->mNumChannels; ++i)
    {
        const aiNodeAnim* assimp_node_anim = assimp_anim->mChannels[i];
        if(node_sets.relevant.count(assimp_node_anim->mNodeName.C_Str()) == 0)
        {
            ++skipped;
            continue;
        }
        auto& node_anim = anim.channels.emplace_back();
        node_anim.node_name = assimp_node_anim->mNodeName.C_Str();
        node_anim.node_index = node_to_index_lut[node_anim.node_name];
        if(!needs_sort && anim.channels.size() > 1)
        {
            needs_sort = node_anim.node_index < anim.channels[anim.channels.size() - 2].node_index;
        }
        copy_channel_keys(assimp_node_anim, ticks_per_second, node_anim);
    }
    assign_root_motion_nodes(scene, assimp_anim, node_sets.skeleton, node_to_index_lut, anim);
    if(needs_sort)
    {
        std::sort(anim.channels.begin(),
                  anim.channels.end(),
                  [](const auto& lhs, const auto& rhs)
                  {
                      return lhs.node_index < rhs.node_index;
                  });
    }
    record_source_rest_pose(scene, anim);
    APPLOG_TRACE("Mesh Importer: Animation {} discarded {} non-relevant node channels", anim.name, skipped);
}

/**
 * @brief Clip names become file names, so a repeated name - unnamed clips all come out as "<file>_" -
 * would overwrite the earlier clip's file. The first keeps its name, later repeats take their index.
 */
void make_clip_names_unique(std::vector<animation_clip>& animations)
{
    std::unordered_set<std::string> used_names;
    for(size_t i = 0; i < animations.size(); ++i)
    {
        auto& clip = animations[i];
        if(used_names.insert(clip.name).second)
        {
            continue;
        }
        std::string unique_name = fmt::format("{}_{}", clip.name, i);
        while(!used_names.insert(unique_name).second)
        {
            unique_name += "_";
        }
        APPLOG_WARNING("Mesh Importer: Animation name '{}' repeats; importing clip {} as '{}'", clip.name, i, unique_name);
        clip.name = unique_name;
    }
}

} // namespace

void process_animations(const aiScene* scene,
                        const fs::path& filename,
                        node_index_lut_t& node_to_index_lut,
                        std::vector<animation_clip>& animations)
{
    if(scene->mNumAnimations == 0)
    {
        return;
    }
    animation_node_sets node_sets;
    node_sets.skeleton = collect_skeleton_nodes(scene);
    node_sets.relevant = collect_relevant_animation_nodes(scene, node_sets.skeleton);
    animations.resize(scene->mNumAnimations);
    for(unsigned int i = 0; i < scene->mNumAnimations; ++i)
    {
        process_animation(scene, filename, scene->mAnimations[i], node_sets, node_to_index_lut, animations[i]);
    }
    make_clip_names_unique(animations);
}

} // namespace mesh_import
} // namespace importer
} // namespace unravel
