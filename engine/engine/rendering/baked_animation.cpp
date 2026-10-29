#include "baked_animation.h"

#include <engine/animation/animation_player.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/mesh.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace unravel
{
namespace
{
/// A clip whose length is a hair over whole frames gets no extra frame for the rounding.
constexpr float FRAME_COUNT_TOLERANCE = 1e-3f;
constexpr float MIN_FRAMES_PER_SECOND = 1.0f;
constexpr float MAX_FRAMES_PER_SECOND = 240.0f;
constexpr uint32_t INITIAL_CAPACITY_ROWS = 4096;
/// A base name (no FBX namespace) that two bones share binds neither.
constexpr int AMBIGUOUS_INDEX = -2;

/// The mesh's armature as flat arrays by armature index, numbered as the model's armature entities are.
struct bake_skeleton
{
    std::vector<std::string> names;
    std::vector<int> parents;
    std::vector<math::transform> rest;
    /// The node of each submesh of the first LOD, -1 for none.
    std::vector<int> submesh_nodes;
    /// The node of each skin bone, -1 for none.
    std::vector<int> bone_nodes;
    std::unordered_map<std::string, int> index_by_name;
    std::unordered_map<std::string, int> index_by_base_name;
    animation_pose ref_pose;
};

void add_node(const mesh::armature_node& node, int parent, bake_skeleton& skeleton)
{
    if(node.index >= 0)
    {
        const auto index = size_t(node.index);
        if(index >= skeleton.names.size())
        {
            skeleton.names.resize(index + 1);
            skeleton.parents.resize(index + 1, -1);
            skeleton.rest.resize(index + 1);
        }
        skeleton.names[index] = node.name;
        skeleton.parents[index] = parent;
        skeleton.rest[index] = node.local_transform;
        for(const uint32_t submesh : node.submeshes)
        {
            if(submesh < skeleton.submesh_nodes.size())
            {
                skeleton.submesh_nodes[submesh] = node.index;
            }
        }
    }
    for(const auto& child : node.children)
    {
        add_node(*child, node.index >= 0 ? node.index : parent, skeleton);
    }
}

auto describe_skeleton(const mesh& source) -> bake_skeleton
{
    bake_skeleton skeleton;
    skeleton.submesh_nodes.assign(source.get_submeshes_count(0), -1);
    add_node(*source.get_armature(), -1, skeleton);
    for(size_t index = 0; index < skeleton.names.size(); ++index)
    {
        skeleton.index_by_name.emplace(skeleton.names[index], int(index));
        const auto [base, is_new] =
            skeleton.index_by_base_name.emplace(std::string(get_name_without_namespace(skeleton.names[index])),
                                                int(index));
        if(!is_new)
        {
            base->second = AMBIGUOUS_INDEX;
        }
        auto& node = skeleton.ref_pose.nodes.emplace_back();
        node.desc.index = index;
        node.transform = skeleton.rest[index];
    }
    const auto& bones = source.get_skin_bind_data().get_bones();
    skeleton.bone_nodes.assign(bones.size(), -1);
    for(size_t bone = 0; bone < bones.size(); ++bone)
    {
        const auto found = skeleton.index_by_name.find(bones[bone].bone_id);
        if(found != skeleton.index_by_name.end())
        {
            skeleton.bone_nodes[bone] = found->second;
        }
    }
    return skeleton;
}

/// Binds clip channels as playback binds them by name (animation_system::make_skeleton_binding): the exact name,
/// else the name without its FBX namespace.
auto make_binding(const bake_skeleton& skeleton, const mesh& source, baked_animation_retarget retarget)
    -> animation_skeleton_binding
{
    animation_skeleton_binding binding;
    binding.version = 1;
    binding.resolve = [&skeleton](const animation_channel& channel)
    {
        const auto found = skeleton.index_by_name.find(channel.node_name);
        if(found != skeleton.index_by_name.end())
        {
            return found->second;
        }
        const auto base = skeleton.index_by_base_name.find(std::string(get_name_without_namespace(channel.node_name)));
        return base != skeleton.index_by_base_name.end() && base->second >= 0 ? base->second : -1;
    };
    if(retarget == baked_animation_retarget::rest_aligned)
    {
        auto target = std::make_shared<animation_target_skeleton>();
        target->set_bones(skeleton.names, skeleton.parents, skeleton.ref_pose);
        target->humanoid_overrides = source.get_humanoid_overrides();
        target->compute_rest_globals();
        target->compute_humanoid_bones();
        target->compute_reference_pose(false, {});
        binding.rest_aligned_target = std::move(target);
    }
    return binding;
}

/// How far from the model origin @p bounds reach once moved by @p transform.
auto get_reach(const math::bbox& bounds, const math::mat4& transform) -> float
{
    float reach = 0.0f;
    for(const auto& corner : bounds.get_corners())
    {
        reach = std::max(reach, math::length(math::vec3(transform * math::vec4(corner, 1.0f))));
    }
    return reach;
}
} // namespace

auto baked_animation::get_mesh() const -> const asset_handle<mesh>&
{
    return mesh_;
}

auto baked_animation::get_frames_per_second() const -> float
{
    return frames_per_second_;
}

auto baked_animation::get_clip_count() const -> uint32_t
{
    return uint32_t(clips_.size());
}

auto baked_animation::get_clip_length(int32_t clip) const -> float
{
    return clip >= 0 && size_t(clip) < clips_.size() ? clips_[size_t(clip)].length : 0.0f;
}

auto baked_animation::get_frame(const baked_animation_state& state) const -> uint32_t
{
    if(state.clip < 0 || size_t(state.clip) >= clips_.size())
    {
        return clips_.empty() ? 0u : clips_.front().first_frame;
    }
    const auto& clip = clips_[size_t(state.clip)];
    const float time = std::isfinite(state.time) ? std::clamp(state.time, 0.0f, clip.length) : 0.0f;
    const auto frame = std::min(uint32_t(std::lround(time * frames_per_second_)), clip.frame_count - 1u);
    return clip.first_frame + frame;
}

auto baked_animation::get_frame_row(uint32_t frame) const -> uint32_t
{
    return frame < frame_rows_.size() ? frame_rows_[frame] : INVALID_ROW;
}

auto baked_animation::get_palette_offset(uint32_t submesh) const -> uint32_t
{
    return submesh < palette_offsets_.size() ? palette_offsets_[submesh] : INVALID_ROW;
}

auto baked_animation::is_rigid(uint32_t submesh) const -> bool
{
    return submesh < rigid_.size() && rigid_[submesh] != 0;
}

auto baked_animation::get_submesh_count() const -> uint32_t
{
    return submesh_count_;
}

auto baked_animation::get_radius() const -> float
{
    return radius_;
}

auto baked_animation::get_frame_count() const -> uint32_t
{
    return uint32_t(frame_rows_.size());
}

baked_animation_store::baked_animation_store()
{
    layout_.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
}

baked_animation_store::~baked_animation_store()
{
    destroy();
}

auto baked_animation_store::create(const asset_handle<mesh>& render_mesh,
                                   float frames_per_second,
                                   baked_animation_retarget retarget) -> uint32_t
{
    const auto source = render_mesh ? render_mesh.get() : nullptr;
    if(!source || asset_handle<mesh>::is_empty(source) || !source->get_armature())
    {
        return 0;
    }
    // Every baked program reads the blend attributes, rigid submeshes included; only skinned meshes have them.
    const auto& vertex_format = source->get_vertex_format();
    if(!vertex_format.has(bgfx::Attrib::Weight) || !vertex_format.has(bgfx::Attrib::Indices))
    {
        return 0;
    }
    auto baked = std::make_shared<baked_animation>();
    baked->mesh_ = render_mesh;
    baked->frames_per_second_ = std::isfinite(frames_per_second)
                                    ? std::clamp(frames_per_second, MIN_FRAMES_PER_SECOND, MAX_FRAMES_PER_SECOND)
                                    : MIN_FRAMES_PER_SECOND;
    baked->retarget_ = retarget;
    baked->submesh_count_ = uint32_t(source->get_submeshes_count(0));
    baked->palette_offsets_.assign(baked->submesh_count_, baked_animation::INVALID_ROW);
    baked->rigid_.assign(baked->submesh_count_, 1);
    // Palettes map 1:1 to the first LOD's submeshes. A frame holds the skinned ones' rows one after another, then one
    // bone per rigid submesh (add_clip appends them in this order).
    const auto& palettes = source->get_bone_palettes();
    const auto& submeshes = source->get_submeshes(0);
    uint32_t rows_per_frame = 0;
    for(uint32_t submesh = 0; submesh < baked->submesh_count_; ++submesh)
    {
        const bool is_skinned = submesh < palettes.size() && submesh < submeshes.size() &&
                                submeshes[submesh] != nullptr && submeshes[submesh]->skinned &&
                                !palettes[submesh].get_bones().empty();
        if(is_skinned)
        {
            baked->palette_offsets_[submesh] = rows_per_frame;
            baked->rigid_[submesh] = 0;
            rows_per_frame += uint32_t(palettes[submesh].get_bones().size()) * skinning_palette_rows::ROWS_PER_BONE;
        }
    }
    for(uint32_t submesh = 0; submesh < baked->submesh_count_; ++submesh)
    {
        if(baked->rigid_[submesh] != 0)
        {
            baked->palette_offsets_[submesh] = rows_per_frame;
            rows_per_frame += skinning_palette_rows::ROWS_PER_BONE;
        }
    }
    baked->rows_per_frame_ = rows_per_frame;
    baked->radius_ = get_reach(source->get_bounds(), math::mat4(1.0f));
    const uint32_t id = next_id_++;
    animations_.emplace(id, std::move(baked));
    return id;
}

auto baked_animation_store::add_clip(uint32_t id, const asset_handle<animation_clip>& clip) -> int32_t
{
    APP_SCOPE_PERF("Rendering/Bake Animation");
    const auto found = animations_.find(id);
    if(found == animations_.end() || !clip)
    {
        return -1;
    }
    auto& baked = *found->second;
    const auto source = baked.mesh_.get();
    const auto clip_ptr = clip.get();
    if(!source || !clip_ptr || asset_handle<animation_clip>::is_empty(clip_ptr))
    {
        return -1;
    }
    const auto skeleton = describe_skeleton(*source);
    const auto binding = make_binding(skeleton, *source, baked.retarget_);
    // A player of its own, seeked frame by frame: the clip's own time, without crossfades or loops.
    animation_player player;
    animation_play_params params;
    params.duration = animation_play_params::seconds_t(0.0f);
    params.loop = false;
    player.play_clip(0, clip, params);
    player.play();
    const float length = std::max(clip_ptr->duration.count(), 0.0f);
    const float frames = std::max(length * baked.frames_per_second_ - FRAME_COUNT_TOLERANCE, 0.0f);
    auto& entry = baked.clips_.emplace_back();
    entry.first_frame = baked.get_frame_count();
    entry.frame_count = uint32_t(std::ceil(frames)) + 1u;
    entry.length = length;
    const auto& palettes = source->get_bone_palettes();
    const auto& submeshes = source->get_submeshes(0);
    const auto& skin = source->get_skin_bind_data();
    const auto& skin_bones = skin.get_bones();
    std::vector<math::transform> locals;
    std::vector<math::mat4> globals(skeleton.names.size(), math::mat4(1.0f));
    std::vector<math::mat4> bone_transforms(skeleton.bone_nodes.size(), math::mat4(1.0f));
    for(uint32_t frame = 0; frame < entry.frame_count; ++frame)
    {
        const float time = std::min(float(frame) / baked.frames_per_second_, length);
        player.seek(0, length > 0.0f ? time / length : 0.0f);
        player.request_pose_refresh();
        player.update_time(animation_play_params::seconds_t(0.0f), true);
        locals = skeleton.rest;
        // Root motion is extracted and dropped: the clip plays in place, the instance's matrix moves it.
        player.update_poses(skeleton.ref_pose,
                            binding,
                            true,
                            [&](const animation_pose::node_desc& desc,
                                const math::transform& transform,
                                const animation_pose::root_motion_result& /*motion*/)
                            {
                                if(desc.index < locals.size())
                                {
                                    locals[desc.index] = transform;
                                }
                            });
        // Armature indices are numbered parents first.
        for(size_t node = 0; node < locals.size(); ++node)
        {
            const int parent = skeleton.parents[node];
            globals[node] = parent >= 0 && size_t(parent) < node ? globals[size_t(parent)] * locals[node].get_matrix()
                                                                 : locals[node].get_matrix();
        }
        for(size_t bone = 0; bone < bone_transforms.size(); ++bone)
        {
            const int node = skeleton.bone_nodes[bone];
            bone_transforms[bone] = node >= 0 ? globals[size_t(node)] : math::mat4(1.0f);
        }
        baked.frame_rows_.push_back(baked_animation::INVALID_ROW);
        const auto add_rows = [&](hpp::span<const math::mat4> bones)
        {
            baked.frame_rows_.back() = std::min(baked.frame_rows_.back(), rows_.append(bones));
        };
        for(uint32_t submesh = 0; submesh < baked.submesh_count_; ++submesh)
        {
            if(baked.rigid_[submesh] != 0)
            {
                continue;
            }
            add_rows(palettes[submesh].get_skinning_matrices(bone_transforms, skin));
            for(const uint32_t bone : palettes[submesh].get_bones())
            {
                if(bone < skin_bones.size() && skin_bones[bone].bounds.is_populated())
                {
                    baked.radius_ = std::max(baked.radius_, get_reach(skin_bones[bone].bounds, bone_transforms[bone]));
                }
            }
        }
        for(uint32_t submesh = 0; submesh < baked.submesh_count_; ++submesh)
        {
            if(baked.rigid_[submesh] != 0)
            {
                const int node = skeleton.submesh_nodes[submesh];
                const math::mat4 node_transform = node >= 0 ? globals[size_t(node)] : math::mat4(1.0f);
                add_rows(hpp::span<const math::mat4>(&node_transform, 1));
                if(submesh < submeshes.size() && submeshes[submesh] != nullptr &&
                   submeshes[submesh]->bbox.is_populated())
                {
                    baked.radius_ = std::max(baked.radius_, get_reach(submeshes[submesh]->bbox, node_transform));
                }
            }
        }
    }
    is_upload_pending_ = rows_.get_row_count() > 0;
    return int32_t(baked.clips_.size() - 1);
}

auto baked_animation_store::find(uint32_t id) const -> std::shared_ptr<const baked_animation>
{
    const auto found = animations_.find(id);
    return found != animations_.end() ? found->second : nullptr;
}

void baked_animation_store::release(uint32_t id)
{
    const auto found = animations_.find(id);
    if(found == animations_.end())
    {
        return;
    }
    released_.push_back(std::move(found->second));
    animations_.erase(found);
}

void baked_animation_store::reclaim_released()
{
    // Held by the store alone: no draw reads its rows any more.
    const auto is_unheld = [](const std::shared_ptr<baked_animation>& baked)
    {
        return baked.use_count() == 1;
    };
    const auto kept_end = std::remove_if(released_.begin(), released_.end(), is_unheld);
    if(kept_end == released_.end())
    {
        return;
    }
    released_.erase(kept_end, released_.end());
    compact_rows();
}

void baked_animation_store::compact_rows()
{
    const auto rows = rows_.get_rows();
    std::vector<math::vec4> compacted;
    const auto keep_rows = [&](baked_animation& baked)
    {
        for(auto& frame_row : baked.frame_rows_)
        {
            if(frame_row == baked_animation::INVALID_ROW)
            {
                continue;
            }
            const auto first = rows.begin() + std::ptrdiff_t(frame_row);
            frame_row = uint32_t(compacted.size());
            compacted.insert(compacted.end(), first, first + std::ptrdiff_t(baked.rows_per_frame_));
        }
    };
    for(auto& [id, baked] : animations_)
    {
        keep_rows(*baked);
    }
    for(auto& baked : released_)
    {
        keep_rows(*baked);
    }
    rows_.swap_storage(compacted);
    is_upload_pending_ = rows_.get_row_count() > 0;
}

void baked_animation_store::clear()
{
    animations_.clear();
    released_.clear();
    rows_.clear();
    is_upload_pending_ = false;
}

void baked_animation_store::bind(uint32_t palette_offset, bool is_rigid)
{
    if(!bgfx::isValid(buffer_))
    {
        buffer_ = bgfx::createDynamicVertexBuffer(std::max(rows_.get_row_count(), INITIAL_CAPACITY_ROWS),
                                                  layout_,
                                                  BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_ALLOW_RESIZE);
    }
    if(is_upload_pending_)
    {
        // Every row again: a grown buffer starts empty. Bakes are rare, and bgfx runs the update ahead of the frame's
        // draws, also the ones recorded before it.
        const auto rows = rows_.get_rows();
        bgfx::update(buffer_, 0, bgfx::copy(rows.data(), uint32_t(rows.size() * sizeof(math::vec4))));
        is_upload_pending_ = false;
    }
    bgfx::setBuffer(SKINNING_PALETTE_STAGE, buffer_, bgfx::Access::Read);
    if(!bgfx::isValid(part_uniform_))
    {
        part_uniform_ = bgfx::createUniform("u_baked_part", bgfx::UniformType::Vec4);
    }
    const math::vec4 part(float(palette_offset), is_rigid ? 1.0f : 0.0f, 0.0f, 0.0f);
    bgfx::setUniform(part_uniform_, math::value_ptr(part));
}

void baked_animation_store::destroy()
{
    if(bgfx::isValid(buffer_))
    {
        bgfx::destroy(buffer_);
        buffer_ = BGFX_INVALID_HANDLE;
    }
    if(bgfx::isValid(part_uniform_))
    {
        bgfx::destroy(part_uniform_);
        part_uniform_ = BGFX_INVALID_HANDLE;
    }
}

auto baked_animation_store::get_rows() const -> hpp::span<const math::vec4>
{
    return rows_.get_rows();
}

} // namespace unravel
