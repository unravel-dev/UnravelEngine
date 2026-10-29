#include "instanced_draw_list.h"

#include <base/hash.hpp>
#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/rendering/batch_instance.h>
#include <engine/rendering/instanced_mesh_utils.h>
#include <engine/rendering/render_change.h>
#define XXH_INLINE_ALL
#include <filesystem/xxhash.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace unravel
{
namespace
{
/// Side of the ground tiles instances are grouped by for culling (m).
constexpr float CLUSTER_SIZE = 8.0f;
static_assert(sizeof(math::mat4) == 16 * sizeof(float), "add_unaligned copies 16 floats per matrix");
/// A model no draw used for this many frames is released.
constexpr uint32_t MAX_UNUSED_MODEL_FRAMES = 120;
/// Packed instances draw with their LOD fully (no LOD transition).
constexpr float PACKED_LOD_PARAM = 0.0f;

/// The tile of instances whose position is not finite: a cluster of their own, so their bounds spoil no other one.
constexpr uint64_t NON_FINITE_TILE_KEY = std::numeric_limits<uint64_t>::max();

/// The tile index of @p coordinate, ordered like the coordinates as an unsigned number. Clamped below the index whose
/// key would be NON_FINITE_TILE_KEY: scripts may place instances anywhere.
auto get_tile(float coordinate) -> uint32_t
{
    constexpr uint32_t SIGN_BIT = 0x80000000u;
    constexpr double MIN_TILE = double(std::numeric_limits<int32_t>::min());
    constexpr double MAX_TILE = double(std::numeric_limits<int32_t>::max() - 1);
    const double tile = std::clamp(std::floor(double(coordinate) / double(CLUSTER_SIZE)), MIN_TILE, MAX_TILE);
    return uint32_t(int32_t(tile)) ^ SIGN_BIT;
}

/// The tile of the matrix's position, as a key that orders tiles row by row (z, then x) like their coordinates.
auto get_tile_key(const math::mat4& transform) -> uint64_t
{
    if(!std::isfinite(transform[3].x) || !std::isfinite(transform[3].z))
    {
        return NON_FINITE_TILE_KEY;
    }
    return (uint64_t(get_tile(transform[3].z)) << 32u) | get_tile(transform[3].x);
}

/// The largest scale of the matrix's axes: how far its mesh reaches compared to the unscaled mesh.
auto get_max_axis_scale(const math::mat4& transform) -> float
{
    const float x = math::length(math::vec3(transform[0]));
    const float y = math::length(math::vec3(transform[1]));
    const float z = math::length(math::vec3(transform[2]));
    return std::max(x, std::max(y, z));
}

auto get_kind_index(bool is_baked) -> size_t
{
    return is_baked ? 1u : 0u;
}
} // namespace

auto instanced_cluster::get_mesh_bounds() const -> const math::bbox&
{
    return bounds_;
}

auto instanced_cluster::get_layer_mask() const -> int
{
    return owner_->draws_[draw_].settings.layer_mask;
}

auto instanced_cluster::casts_shadow() const -> bool
{
    return owner_->draws_[draw_].settings.casts_shadow;
}

auto instanced_cluster::make_run(uint32_t part, instanced_layout layout) const -> instanced_run
{
    const auto& packed = owner_->parts_[part];
    const auto& block = owner_->blocks_[packed.block];
    instanced_run run;
    run.part = &packed.part;
    run.buffer = &owner_->packed_[size_t(layout)][get_kind_index(block.is_baked)].buffer;
    run.first = block.first[size_t(layout)] + first_;
    run.count = count_;
    return run;
}

void instanced_cluster::collect_mesh_runs(instanced_run_list& runs, instanced_layout layout) const
{
    const auto& item = owner_->draws_[draw_];
    if(layout == instanced_layout::velocity && !item.has_previous)
    {
        return;
    }
    for(uint32_t part = item.first_part; part < item.first_part + item.part_count; ++part)
    {
        runs.add(make_run(part, layout));
    }
}

auto instanced_cluster::collect_mesh_shadow_runs(std::vector<instanced_run_list>& runs, uint8_t view_mask) const -> bool
{
    const auto& item = owner_->draws_[draw_];
    if(!item.settings.casts_shadow)
    {
        return false;
    }
    bool is_collected = false;
    for(uint32_t part = item.first_part; part < item.first_part + item.part_count; ++part)
    {
        if(!owner_->parts_[part].part.shadow_key.is_valid())
        {
            continue;
        }
        const instanced_run run = make_run(part, instanced_layout::base);
        for(size_t view = 0; view < runs.size(); ++view)
        {
            if((view_mask & (1u << view)) != 0u)
            {
                runs[view].add(run);
                is_collected = true;
            }
        }
    }
    return is_collected;
}

auto instanced_cluster::get_mesh_shadow_key() const -> uint64_t
{
    const auto& item = owner_->draws_[draw_];
    const uint32_t stride = owner_->get_stride(item);
    const size_t first_placed = item.placed_first + size_t(first_) * stride;
    const math::mat4* first = owner_->placed_transforms_.data() + first_placed;
    std::size_t key = XXH3_64bits(first, size_t(count_) * stride * sizeof(math::mat4));
    if(item.animation)
    {
        utils::hash_combine(key,
                            XXH3_64bits(owner_->placed_frames_.data() + first_placed,
                                        size_t(count_) * stride * sizeof(instanced_draw_list::baked_frames)));
    }
    for(const auto& lod : item.model->mesh_model.get_lods())
    {
        utils::hash_combine(key, lod.version());
    }
    const auto material_keys = instanced_draw_list::get_material_change_keys(item);
    for_each_aspect(render_interest::shadow_caster & render_change::material_aspects,
                    [&](uint32_t aspect)
                    {
                        utils::hash_combine(key, material_keys[aspect]);
                    });
    return key;
}

void instanced_draw_list::add(const draw_settings& settings,
                              hpp::span<const math::mat4> transforms,
                              hpp::span<const math::mat4> previous)
{
    const bool has_previous = previous.size() == transforms.size();
    add_unaligned(settings,
                  reinterpret_cast<const float*>(transforms.data()),
                  transforms.size(),
                  has_previous ? reinterpret_cast<const float*>(previous.data()) : nullptr);
}

void instanced_draw_list::add_unaligned(const draw_settings& settings,
                                        const float* transforms,
                                        size_t count,
                                        const float* previous)
{
    if(count == 0 || transforms == nullptr || !settings.render_mesh.is_valid())
    {
        return;
    }
    draw item;
    item.settings = settings;
    item.model = find_model(settings);
    item.has_previous = previous != nullptr;
    item.input_first = uint32_t(input_transforms_.size());
    item.input_count = uint32_t(count);
    input_transforms_.resize(input_transforms_.size() + count);
    input_previous_.resize(input_previous_.size() + count);
    input_frames_.resize(input_frames_.size() + count);
    std::memcpy(&input_transforms_[item.input_first], transforms, count * sizeof(math::mat4));
    std::memcpy(&input_previous_[item.input_first], item.has_previous ? previous : transforms, count * sizeof(math::mat4));
    draws_.push_back(std::move(item));
    are_clusters_dirty_ = true;
}

void instanced_draw_list::add_baked(const draw_settings& settings,
                                    std::shared_ptr<const baked_animation> animation,
                                    const float* transforms,
                                    const baked_animation_state* states,
                                    size_t count,
                                    const float* previous,
                                    const baked_animation_state* previous_states)
{
    if(!animation || states == nullptr)
    {
        return;
    }
    draw_settings baked_settings = settings;
    baked_settings.render_mesh = animation->get_mesh();
    const size_t first = input_transforms_.size();
    add_unaligned(baked_settings, transforms, count, previous);
    if(input_transforms_.size() == first)
    {
        return;
    }
    auto& item = draws_.back();
    item.animation = animation;
    // Rigid submeshes on animated nodes move with the animation even when the instances stand still.
    item.has_previous = item.has_previous || previous_states != nullptr;
    for(size_t instance = 0; instance < count; ++instance)
    {
        auto& frames = input_frames_[first + instance];
        frames.current = animation->get_frame(states[instance]);
        frames.previous = previous_states != nullptr ? animation->get_frame(previous_states[instance]) : frames.current;
    }
}

auto instanced_draw_list::get_clusters() -> const std::vector<instanced_cluster>&
{
    if(are_clusters_dirty_)
    {
        build_clusters();
        are_clusters_dirty_ = false;
    }
    return clusters_;
}

void instanced_draw_list::prepare(instanced_layout layout, bool with_baked)
{
    get_clusters();
    pack(layout, false);
    if(with_baked)
    {
        pack(layout, true);
    }
}

auto instanced_draw_list::get_packed_count(instanced_layout layout, bool is_baked) const -> uint32_t
{
    return packed_counts_[size_t(layout)][get_kind_index(is_baked)];
}

auto instanced_draw_list::get_packed_stride(instanced_layout layout, bool is_baked) -> uint16_t
{
    if(layout == instanced_layout::velocity)
    {
        return uint16_t(is_baked ? baked_velocity_instance_vertex_data::packed_size()
                                 : velocity_instance_vertex_data::packed_size());
    }
    return uint16_t(is_baked ? baked_instance_vertex_data::packed_size() : instance_vertex_data::packed_size());
}

void instanced_draw_list::write_packed(instanced_layout layout, bool is_baked, uint8_t* out, uint32_t count) const
{
    for(const auto& item : draws_)
    {
        // Only instances with last frame's matrices draw in the velocity pass.
        if(layout == instanced_layout::velocity && !item.has_previous)
        {
            continue;
        }
        for(uint32_t block = item.first_block; block < item.first_block + item.block_count; ++block)
        {
            if(blocks_[block].is_baked == is_baked)
            {
                write_block(item, blocks_[block], layout, out, count);
            }
        }
    }
}

void instanced_draw_list::clear()
{
    draws_.clear();
    input_transforms_.clear();
    input_previous_.clear();
    input_frames_.clear();
    placed_transforms_.clear();
    placed_previous_.clear();
    placed_frames_.clear();
    clusters_.clear();
    parts_.clear();
    blocks_.clear();
    reset_packed();
    are_clusters_dirty_ = false;
    for(auto it = models_.begin(); it != models_.end();)
    {
        it = ++it->second.unused_frames > MAX_UNUSED_MODEL_FRAMES ? models_.erase(it) : std::next(it);
    }
}

void instanced_draw_list::end_frame()
{
    // The frame's instance buffers are gone with the frame; the counts and blocks still describe the draws.
    for(auto& layout : packed_)
    {
        std::fill(std::begin(layout), std::end(layout), packed_buffer{});
    }
}

void instanced_draw_list::reset_packed()
{
    for(auto& layout : packed_counts_)
    {
        std::fill(std::begin(layout), std::end(layout), 0u);
    }
    end_frame();
}

auto instanced_draw_list::model_key_hash::operator()(const model_key& key) const noexcept -> std::size_t
{
    std::size_t seed = std::hash<hpp::uuid>{}(key.mesh_uid);
    utils::hash_combine(seed, key.material_uid);
    return seed;
}

auto instanced_draw_list::find_model(const draw_settings& settings) -> cached_model*
{
    auto& cached = models_[model_key{settings.render_mesh.uid(), settings.render_material.uid()}];
    cached.unused_frames = 0;
    return &cached;
}

void instanced_draw_list::refresh_model(const draw_settings& settings, cached_model& cached) const
{
    // A scene loads its asset handles deferred: try_get starts the load, is_ready alone never would.
    const auto source = settings.render_mesh.try_get();
    if(!source || asset_handle<mesh>::is_empty(source) || source.get() == cached.source)
    {
        return;
    }
    model built;
    built.set_lod(settings.render_mesh, 0);
    if(settings.render_material.is_valid())
    {
        for(uint32_t group = 0; group < source->get_data_groups_count(); ++group)
        {
            built.set_shared_material(settings.render_material, group);
        }
    }
    else
    {
        built.apply_default_materials(engine::context().get_cached<asset_manager>(), false);
    }
    cached.mesh_model = std::move(built);
    cached.node_transforms.clear();
    bool has_node_transforms = false;
    for(const auto& node : source->get_submesh_node_transforms(0))
    {
        cached.node_transforms.push_back(node.get_matrix());
        has_node_transforms = has_node_transforms || cached.node_transforms.back() != math::mat4(1.0f);
    }
    if(!has_node_transforms)
    {
        cached.node_transforms.clear();
    }
    // The farthest corner of the bounds from the origin.
    const auto& bounds = source->get_bounds();
    cached.mesh_radius = math::length(math::max(math::abs(bounds.min), math::abs(bounds.max)));
    cached.source = source.get();
}

void instanced_draw_list::build_clusters()
{
    clusters_.clear();
    parts_.clear();
    blocks_.clear();
    // Packed data of an earlier build stays in the frame's buffers unread.
    reset_packed();
    size_t placed_count = 0;
    for(auto& item : draws_)
    {
        refresh_model(item.settings, *item.model);
        item.placed_first = placed_count;
        placed_count += size_t(item.input_count) * get_stride(item);
    }
    placed_transforms_.resize(placed_count);
    placed_previous_.resize(placed_count);
    placed_frames_.assign(placed_count, baked_frames{});
    for(uint32_t draw_index = 0; draw_index < draws_.size(); ++draw_index)
    {
        if(draws_[draw_index].model->mesh_model.is_valid())
        {
            build_draw_clusters(draw_index);
            build_draw_parts(draws_[draw_index]);
        }
    }
}

void instanced_draw_list::build_draw_clusters(uint32_t draw_index)
{
    const auto& item = draws_[draw_index];
    const auto& cached = *item.model;
    const uint32_t stride = get_stride(item);
    const float mesh_radius =
        item.animation ? std::max(cached.mesh_radius, item.animation->get_radius()) : cached.mesh_radius;
    const size_t first_cluster = clusters_.size();
    cluster_by_tile_.clear();
    instance_clusters_.resize(item.input_count);
    for(uint32_t instance = 0; instance < item.input_count; ++instance)
    {
        const math::mat4& transform = input_transforms_[item.input_first + instance];
        const uint64_t tile = get_tile_key(transform);
        const auto [found, is_new] = cluster_by_tile_.try_emplace(tile, uint32_t(clusters_.size()));
        if(is_new)
        {
            auto& cluster = clusters_.emplace_back();
            cluster.owner_ = this;
            cluster.draw_ = draw_index;
            cluster.tile_ = tile;
        }
        auto& cluster = clusters_[found->second];
        ++cluster.count_;
        const math::vec3 position(transform[3]);
        const float reach = mesh_radius * get_max_axis_scale(transform);
        cluster.bounds_.add_point(position - math::vec3(reach));
        cluster.bounds_.add_point(position + math::vec3(reach));
        instance_clusters_[instance] = found->second;
    }
    sort_draw_clusters(first_cluster);
    // Each cluster takes one contiguous range of the draw's placed matrices.
    uint32_t first = 0;
    for(size_t cluster = first_cluster; cluster < clusters_.size(); ++cluster)
    {
        clusters_[cluster].first_ = first;
        first += clusters_[cluster].count_;
        clusters_[cluster].count_ = 0;
    }
    for(uint32_t instance = 0; instance < item.input_count; ++instance)
    {
        auto& cluster = clusters_[instance_clusters_[instance]];
        const size_t slot = item.placed_first + size_t(cluster.first_ + cluster.count_++) * stride;
        if(item.animation)
        {
            place_baked_instance(item, slot, instance);
            continue;
        }
        const math::mat4& world = input_transforms_[item.input_first + instance];
        const math::mat4& previous = input_previous_[item.input_first + instance];
        if(cached.node_transforms.empty())
        {
            placed_transforms_[slot] = world;
            placed_previous_[slot] = previous;
            continue;
        }
        for(size_t node = 0; node < cached.node_transforms.size(); ++node)
        {
            placed_transforms_[slot + node] = world * cached.node_transforms[node];
            placed_previous_[slot + node] = previous * cached.node_transforms[node];
        }
    }
}

void instanced_draw_list::sort_draw_clusters(size_t first_cluster)
{
    const size_t count = clusters_.size() - first_cluster;
    if(count < 2)
    {
        return;
    }
    cluster_order_.resize(count);
    std::iota(cluster_order_.begin(), cluster_order_.end(), 0u);
    std::sort(cluster_order_.begin(),
              cluster_order_.end(),
              [&](uint32_t left, uint32_t right)
              {
                  return clusters_[first_cluster + left].tile_ < clusters_[first_cluster + right].tile_;
              });
    cluster_places_.resize(count);
    sorted_clusters_.clear();
    for(uint32_t place = 0; place < count; ++place)
    {
        sorted_clusters_.push_back(clusters_[first_cluster + cluster_order_[place]]);
        cluster_places_[cluster_order_[place]] = place;
    }
    std::copy(sorted_clusters_.begin(), sorted_clusters_.end(), clusters_.begin() + std::ptrdiff_t(first_cluster));
    for(auto& cluster : instance_clusters_)
    {
        cluster = uint32_t(first_cluster + cluster_places_[cluster - first_cluster]);
    }
}

void instanced_draw_list::place_baked_instance(const draw& item, size_t slot, uint32_t instance)
{
    // The bake places every submesh from the armature's root, rigid ones by their animated nodes: the instance's
    // matrix alone goes under all of them.
    placed_transforms_[slot] = input_transforms_[item.input_first + instance];
    placed_previous_[slot] = input_previous_[item.input_first + instance];
    placed_frames_[slot] = input_frames_[item.input_first + instance];
}

auto instanced_draw_list::get_stride(const draw& item) const -> uint32_t
{
    if(item.animation || item.model->node_transforms.empty())
    {
        return 1u;
    }
    return uint32_t(item.model->node_transforms.size());
}

void instanced_draw_list::build_draw_parts(draw& item)
{
    item.first_part = uint32_t(parts_.size());
    item.first_block = uint32_t(blocks_.size());
    const uint32_t stride = get_stride(item);
    // A material instance draws in place of the shared material in every slot; without overrides it is the shared
    // material itself.
    material::sptr instance_material;
    if(item.settings.render_material_instance)
    {
        item.settings.render_material_instance->update_resolved();
        instance_material = item.settings.render_material_instance->get_resolved();
    }
    const auto make_part =
        [&](const std::shared_ptr<mesh>& mesh_ptr, const material::sptr& material_ptr, uint32_t submesh)
    {
        instanced_part part;
        part.mesh_ptr = mesh_ptr;
        part.material_ptr = instance_material ? instance_material : material_ptr;
        part.submesh = submesh;
        return part;
    };
    if(!item.animation)
    {
        for_each_instanced_submesh(
            item.model->mesh_model,
            [&](const std::shared_ptr<mesh>& mesh_ptr, const material::sptr& material_ptr, uint32_t submesh)
            {
                packed_block wanted;
                wanted.slot = stride == 1 ? 0 : submesh;
                add_part(item, make_part(mesh_ptr, material_ptr, submesh), wanted);
            });
    }
    else if(item.animation->get_frame_count() > 0)
    {
        // Every submesh draws from the bake, skinned by its palette or rigid on its node.
        const auto add_baked_part =
            [&](const std::shared_ptr<mesh>& mesh_ptr, const material::sptr& material_ptr, uint32_t submesh)
        {
            auto part = make_part(mesh_ptr, material_ptr, submesh);
            part.is_baked = true;
            part.palette_offset = item.animation->get_palette_offset(submesh);
            part.is_rigid = item.animation->is_rigid(submesh);
            packed_block wanted;
            wanted.is_baked = true;
            if(part.palette_offset != baked_animation::INVALID_ROW)
            {
                add_part(item, std::move(part), wanted);
            }
        };
        for_each_instanced_skinned_submesh(item.model->mesh_model, add_baked_part);
        for_each_instanced_submesh(item.model->mesh_model, add_baked_part);
    }
    item.part_count = uint32_t(parts_.size()) - item.first_part;
    item.block_count = uint32_t(blocks_.size()) - item.first_block;
}

void instanced_draw_list::add_part(draw& item, instanced_part part, const packed_block& wanted)
{
    if(!part.mesh_ptr || !part.material_ptr)
    {
        return;
    }
    part.shadow_key = make_shadow_batch_key(part.mesh_ptr, 0, part.submesh, part.material_ptr);
    auto& packed = parts_.emplace_back();
    packed.part = std::move(part);
    packed.block = find_or_add_block(item, wanted);
}

auto instanced_draw_list::find_or_add_block(const draw& item, const packed_block& wanted) -> uint32_t
{
    for(uint32_t block = item.first_block; block < blocks_.size(); ++block)
    {
        const auto& existing = blocks_[block];
        if(existing.is_baked == wanted.is_baked && existing.slot == wanted.slot)
        {
            return block;
        }
    }
    auto& added = blocks_.emplace_back(wanted);
    const size_t kind = get_kind_index(wanted.is_baked);
    for(size_t layout = 0; layout < size_t(instanced_layout::count); ++layout)
    {
        // Only instances with last frame's matrices draw in the velocity pass.
        if(layout == size_t(instanced_layout::velocity) && !item.has_previous)
        {
            continue;
        }
        added.first[layout] = packed_counts_[layout][kind];
        packed_counts_[layout][kind] += item.input_count;
    }
    return uint32_t(blocks_.size() - 1);
}

auto instanced_draw_list::get_material_change_keys(const draw& item) -> render_change_values
{
    const auto& instance = item.settings.render_material_instance;
    if(!instance || !instance->get_resolved())
    {
        return item.model->mesh_model.get_materials_change_keys();
    }
    render_change_values keys{};
    instance->get_resolved()->add_change_keys(keys);
    return keys;
}

void instanced_draw_list::pack(instanced_layout layout, bool is_baked)
{
    auto& packed = packed_[size_t(layout)][get_kind_index(is_baked)];
    const uint32_t count = get_packed_count(layout, is_baked);
    if(packed.is_packed || count == 0)
    {
        return;
    }
    packed.is_packed = true;
    const uint16_t stride = get_packed_stride(layout, is_baked);
    // A full frame's instance memory takes what fits; runs past its end are not drawn.
    const uint32_t available = bgfx::getAvailInstanceDataBuffer(count, stride);
    if(available == 0)
    {
        return;
    }
    bgfx::allocInstanceDataBuffer(&packed.buffer, available, stride);
    write_packed(layout, is_baked, packed.buffer.data, packed.buffer.num);
}

void instanced_draw_list::write_block(const draw& item,
                                      const packed_block& block,
                                      instanced_layout layout,
                                      uint8_t* out,
                                      uint32_t count) const
{
    const uint32_t stride = get_stride(item);
    const uint32_t first_packed = block.first[size_t(layout)];
    const uint32_t written = first_packed < count ? std::min(item.input_count, count - first_packed) : 0u;
    const size_t size = get_packed_stride(layout, block.is_baked);
    uint8_t* target = out + size_t(first_packed) * size;
    for(uint32_t instance = 0; instance < written; ++instance, target += size)
    {
        const size_t first = item.placed_first + size_t(instance) * stride;
        if(block.is_baked)
        {
            write_baked_instance(item, block, layout, first, target);
            continue;
        }
        write_static_instance(block, layout, first, target);
    }
}

void instanced_draw_list::write_static_instance(const packed_block& block,
                                                instanced_layout layout,
                                                size_t first,
                                                uint8_t* target) const
{
    const size_t index = first + block.slot;
    const math::mat4& world = placed_transforms_[index];
    if(layout == instanced_layout::velocity)
    {
        *reinterpret_cast<velocity_instance_vertex_data*>(target) =
            velocity_instance_vertex_data(world, placed_previous_[index], PACKED_LOD_PARAM);
        return;
    }
    *reinterpret_cast<instance_vertex_data*>(target) = instance_vertex_data(world, PACKED_LOD_PARAM);
}

void instanced_draw_list::write_baked_instance(const draw& item,
                                               const packed_block& block,
                                               instanced_layout layout,
                                               size_t first,
                                               uint8_t* target) const
{
    const size_t index = first + block.slot;
    const auto& frames = placed_frames_[index];
    const uint32_t row = item.animation->get_frame_row(frames.current);
    const uint32_t previous_row = item.animation->get_frame_row(frames.previous);
    const math::mat4& world = placed_transforms_[index];
    if(layout == instanced_layout::velocity)
    {
        *reinterpret_cast<baked_velocity_instance_vertex_data*>(target) =
            baked_velocity_instance_vertex_data(world, placed_previous_[index], row, previous_row);
        return;
    }
    *reinterpret_cast<baked_instance_vertex_data*>(target) = baked_instance_vertex_data(world, row, previous_row);
}

} // namespace unravel
