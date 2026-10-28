#include "instanced_draw_list.h"

#include <base/hash.hpp>
#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/rendering/instanced_mesh_utils.h>
#include <engine/rendering/render_change.h>
#define XXH_INLINE_ALL
#include <filesystem/xxhash.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace unravel
{
namespace
{
/// Side of the ground tiles instances are grouped by for culling (m).
constexpr float CLUSTER_SIZE = 8.0f;
static_assert(sizeof(math::mat4) == 16 * sizeof(float), "add_unaligned copies 16 floats per matrix");
/// A model no draw used for this many frames is released.
constexpr uint32_t MAX_UNUSED_MODEL_FRAMES = 120;

auto get_tile_key(const math::mat4& transform) -> uint64_t
{
    const auto tile_x = uint32_t(int32_t(std::floor(transform[3].x / CLUSTER_SIZE)));
    const auto tile_z = uint32_t(int32_t(std::floor(transform[3].z / CLUSTER_SIZE)));
    return (uint64_t(tile_x) << 32u) | tile_z;
}

/// The largest scale of the matrix's axes: how far its mesh reaches compared to the unscaled mesh.
auto get_max_axis_scale(const math::mat4& transform) -> float
{
    const float x = math::length(math::vec3(transform[0]));
    const float y = math::length(math::vec3(transform[1]));
    const float z = math::length(math::vec3(transform[2]));
    return std::max(x, std::max(y, z));
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

void instanced_cluster::collect_mesh_instances(batch_collector& collector, bool with_previous) const
{
    const auto& item = owner_->draws_[draw_];
    const uint32_t stride = owner_->get_stride(item);
    const bool has_previous = with_previous && item.has_previous;
    for_each_instanced_submesh(item.model->mesh_model,
                               [&](const std::shared_ptr<mesh>& mesh_ptr, const material::sptr& material_ptr, uint32_t submesh)
                               {
                                   const batch_key key(mesh_ptr, material_ptr, 0, submesh);
                                   if(!key.is_valid())
                                   {
                                       return;
                                   }
                                   const size_t offset = stride == 1 ? 0 : submesh;
                                   for(uint32_t instance = first_; instance < first_ + count_; ++instance)
                                   {
                                       const size_t index = item.placed_first + size_t(instance) * stride + offset;
                                       batch_instance batch(&owner_->placed_transforms_[index]);
                                       if(has_previous)
                                       {
                                           batch.prev_world_transform_ptr = &owner_->placed_previous_[index];
                                       }
                                       collector.collect_renderable(key, batch);
                                   }
                               });
}

auto instanced_cluster::collect_mesh_shadow_instances(std::vector<shadow_batch_collector>& collectors, uint8_t view_mask) const
    -> bool
{
    const auto& item = owner_->draws_[draw_];
    if(!item.settings.casts_shadow)
    {
        return false;
    }
    const uint32_t stride = owner_->get_stride(item);
    bool is_collected = false;
    for_each_instanced_submesh(item.model->mesh_model,
                               [&](const std::shared_ptr<mesh>& mesh_ptr, const material::sptr& material_ptr, uint32_t submesh)
                               {
                                   const shadow_batch_key key = make_shadow_batch_key(mesh_ptr, 0, submesh, material_ptr);
                                   if(!key.is_valid())
                                   {
                                       return;
                                   }
                                   const size_t offset = stride == 1 ? 0 : submesh;
                                   for(size_t view = 0; view < collectors.size(); ++view)
                                   {
                                       if((view_mask & (1u << view)) == 0u)
                                       {
                                           continue;
                                       }
                                       for(uint32_t instance = first_; instance < first_ + count_; ++instance)
                                       {
                                           const size_t index = item.placed_first + size_t(instance) * stride + offset;
                                           collectors[view].collect_renderable(key, batch_instance(&owner_->placed_transforms_[index]));
                                       }
                                       is_collected = true;
                                   }
                               });
    return is_collected;
}

auto instanced_cluster::get_mesh_shadow_key() const -> uint64_t
{
    const auto& item = owner_->draws_[draw_];
    const uint32_t stride = owner_->get_stride(item);
    const math::mat4* first = owner_->placed_transforms_.data() + item.placed_first + size_t(first_) * stride;
    std::size_t key = XXH3_64bits(first, size_t(count_) * stride * sizeof(math::mat4));
    for(const auto& lod : item.model->mesh_model.get_lods())
    {
        utils::hash_combine(key, lod.version());
    }
    const auto material_keys = item.model->mesh_model.get_materials_change_keys();
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
    std::memcpy(&input_transforms_[item.input_first], transforms, count * sizeof(math::mat4));
    std::memcpy(&input_previous_[item.input_first], item.has_previous ? previous : transforms, count * sizeof(math::mat4));
    draws_.push_back(std::move(item));
    are_clusters_dirty_ = true;
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

void instanced_draw_list::clear()
{
    draws_.clear();
    input_transforms_.clear();
    input_previous_.clear();
    placed_transforms_.clear();
    placed_previous_.clear();
    clusters_.clear();
    are_clusters_dirty_ = false;
    for(auto it = models_.begin(); it != models_.end();)
    {
        it = ++it->second.unused_frames > MAX_UNUSED_MODEL_FRAMES ? models_.erase(it) : std::next(it);
    }
}

auto instanced_draw_list::find_model(const draw_settings& settings) -> cached_model*
{
    auto& cached = models_[settings.render_mesh.id() + "|" + settings.render_material.id()];
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
    const auto& bounds = source->get_bounds();
    cached.mesh_radius = math::max(math::length(bounds.min), math::length(bounds.max));
    cached.source = source.get();
}

void instanced_draw_list::build_clusters()
{
    clusters_.clear();
    size_t placed_count = 0;
    for(auto& item : draws_)
    {
        refresh_model(item.settings, *item.model);
        item.placed_first = placed_count;
        placed_count += size_t(item.input_count) * get_stride(item);
    }
    placed_transforms_.resize(placed_count);
    placed_previous_.resize(placed_count);
    for(uint32_t draw_index = 0; draw_index < draws_.size(); ++draw_index)
    {
        if(draws_[draw_index].model->mesh_model.is_valid())
        {
            build_draw_clusters(draw_index);
        }
    }
}

void instanced_draw_list::build_draw_clusters(uint32_t draw_index)
{
    const auto& item = draws_[draw_index];
    const auto& cached = *item.model;
    const uint32_t stride = get_stride(item);
    const size_t first_cluster = clusters_.size();
    cluster_by_tile_.clear();
    instance_clusters_.resize(item.input_count);
    for(uint32_t instance = 0; instance < item.input_count; ++instance)
    {
        const math::mat4& transform = input_transforms_[item.input_first + instance];
        const auto [found, is_new] = cluster_by_tile_.try_emplace(get_tile_key(transform), uint32_t(clusters_.size()));
        if(is_new)
        {
            auto& cluster = clusters_.emplace_back();
            cluster.owner_ = this;
            cluster.draw_ = draw_index;
        }
        auto& cluster = clusters_[found->second];
        ++cluster.count_;
        const math::vec3 position(transform[3]);
        const float reach = cached.mesh_radius * get_max_axis_scale(transform);
        cluster.bounds_.add_point(position - math::vec3(reach));
        cluster.bounds_.add_point(position + math::vec3(reach));
        instance_clusters_[instance] = found->second;
    }
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

auto instanced_draw_list::get_stride(const draw& item) const -> uint32_t
{
    return item.model->node_transforms.empty() ? 1u : uint32_t(item.model->node_transforms.size());
}

} // namespace unravel
