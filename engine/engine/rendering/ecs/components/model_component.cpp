#include "model_component.h"
#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/mesh.h>

#include <hpp/finally.hpp>

#include <algorithm>
#include <atomic>
#include <limits>


namespace unravel
{
namespace
{

/// Last armature version handed out. Versions are unique across components, so a cache built for
/// one armature never matches another: a model removed and added again starts over at 1 otherwise.
std::atomic<uint64_t> last_armature_version{1};

/// Marks a name without namespace that several armature nodes share.
constexpr size_t AMBIGUOUS_ARMATURE_INDEX = std::numeric_limits<size_t>::max();

/**
 * Finds the existing child entity an armature node binds to: the first child named @p name
 * that no earlier sibling node claimed in this walk.
 *
 * Binding by name keeps the node entities a scene or prefab saved. The claim keeps sibling
 * nodes that share a name (a kit model's eight "slice" nodes, each with its own transform) on
 * separate entities: the Nth such node binds to the Nth such child, or gets a new one.
 */
auto find_unclaimed_child(const std::string& name,
                          const std::vector<entt::handle>& children,
                          const std::vector<entt::handle>& claimed) -> entt::handle
{
    for(const auto& e : children)
    {
        if(!e || e.get<tag_component>().name != name)
        {
            continue;
        }
        if(std::find(claimed.begin(), claimed.end(), e) == claimed.end())
        {
            return e;
        }
    }

    return {};
}

/**
 * Rebuilds the submesh_component entries for a node from the current mesh asset.
 *
 * The mesh is the source of truth for which submesh indices the node references
 * (@p node_submeshes). Any previously authored per-submesh settings (shadow/enabled flags)
 * are preserved by matching on the import-stable submesh id first, then - for legacy data
 * without ids - on the raw index.
 */
void rebuild_submesh_entries(submesh_component& comp,
                             const std::vector<uint32_t>& node_submeshes,
                             const mesh& render_mesh)
{
    const std::vector<submesh_entry> previous_entries = std::move(comp.entries);

    const auto& submeshes = render_mesh.get_submeshes(0);

    comp.entries.clear();
    comp.entries.reserve(node_submeshes.size());
    for(uint32_t index : node_submeshes)
    {
        submesh_entry entry;
        entry.submesh_index = index;
        entry.stable_id = index < submeshes.size() && submeshes[index] != nullptr ? submeshes[index]->stable_id : 0;

        // Preserve authored settings: stable id match wins (survives reimports that
        // reorder submeshes), then fall back to an index match for legacy data.
        const submesh_entry* match = nullptr;
        if(entry.stable_id != 0)
        {
            for(const auto& previous : previous_entries)
            {
                if(previous.stable_id == entry.stable_id)
                {
                    match = &previous;
                    break;
                }
            }
        }
        if(match == nullptr)
        {
            for(const auto& previous : previous_entries)
            {
                if(previous.stable_id == 0 && previous.submesh_index == index)
                {
                    match = &previous;
                    break;
                }
            }
        }
        if(match != nullptr)
        {
            entry.casts_shadow = match->casts_shadow;
            entry.enabled = match->enabled;
        }

        comp.entries.emplace_back(std::move(entry));
    }
}

/// The node entities a rebuild walk binds, and among them those it had to create.
struct armature_walk
{
    std::vector<entt::handle> nodes;
    std::vector<entt::handle> created_nodes;
};

auto process_node_impl(const std::unique_ptr<mesh::armature_node>& node,
                       const mesh& render_mesh,
                       entt::handle& parent,
                       std::vector<entt::handle>& claimed_siblings,
                       armature_walk& walk,
                       animation_pose& ref_pose) -> entt::handle
{
    const auto& bind_data = render_mesh.get_skin_bind_data();
    auto entity_node = parent;

    if(entity_node == parent)
    {
        auto& parent_trans_comp = parent.get<transform_component>();
        const auto& children = parent_trans_comp.get_children();
        auto found_node = find_unclaimed_child(node->name, children, claimed_siblings);
        if(found_node)
        {
            entity_node = found_node;
        }
        else
        {
            auto& reg = *entity_node.registry();
            entity_node = scene::create_entity(reg, node->name, parent);
            walk.created_nodes.emplace_back(entity_node);
        }
        // A created entity is claimed too: it is now a child with this name a later sibling would find.
        claimed_siblings.emplace_back(entity_node);
        auto& trans_comp = entity_node.get<transform_component>();
        trans_comp.set_transform_local(node->local_transform);

        walk.nodes.emplace_back(entity_node);

        if(!node->submeshes.empty())
        {
            auto& comp = entity_node.get_or_emplace<submesh_component>();
            rebuild_submesh_entries(comp, node->submeshes, render_mesh);
        }

        auto query = bind_data.find_bone_by_id(node->name);
        if(query.bone && query.index >= 0)
        {
            auto& comp = entity_node.get_or_emplace<bone_component>();
            comp.bone_index = query.index;
        }

        // Instead of storing anything in a bone_component,
        // immediately add this node to the reference pose.
        animation_pose::node ref_node;
        ref_node.desc.index = node->index;          // Use the node's index
        ref_node.transform = node->local_transform;
        ref_pose.nodes.push_back(ref_node);
    }

    return entity_node;
}

void process_node(const std::unique_ptr<mesh::armature_node>& node,
                  const mesh& render_mesh,
                  entt::handle parent,
                  std::vector<entt::handle>& claimed_siblings,
                  armature_walk& walk,
                  animation_pose& ref_pose)
{
    if(!parent)
    {
        return;
    }

    auto entity_node = process_node_impl(node, render_mesh, parent, claimed_siblings, walk, ref_pose);
    // The child nodes bind among this entity's children, so only they can claim those.
    std::vector<entt::handle> claimed_children;
    for(auto& child : node->children)
    {
        process_node(child, render_mesh, entity_node, claimed_children, walk, ref_pose);
    }
}

auto process_armature(const mesh& render_mesh, entt::handle parent, armature_walk& walk, animation_pose& ref_pose)
    -> bool
{
    const auto& root = render_mesh.get_armature();
    if(!root)
    {
        return false;
    }

    std::vector<entt::handle> claimed_roots;
    process_node(root, render_mesh, parent, claimed_roots, walk, ref_pose);

    return true;
}

auto contains_entity(const std::vector<entt::handle>& entities, entt::handle entity) -> bool
{
    return std::find(entities.begin(), entities.end(), entity) != entities.end();
}

/// The parent of @p entity in the transform hierarchy, or an empty handle.
auto get_parent_entity(entt::handle entity) -> entt::handle
{
    const auto* transform_comp = entity.try_get<transform_component>();
    return transform_comp ? transform_comp->get_parent() : entt::handle{};
}

/// Whether @p entity and everything under it belongs to @p entities.
auto is_whole_subtree_in(entt::handle entity, const std::vector<entt::handle>& entities) -> bool
{
    if(!contains_entity(entities, entity))
    {
        return false;
    }
    const auto* transform_comp = entity.try_get<transform_component>();
    if(!transform_comp)
    {
        return true;
    }
    const auto& children = transform_comp->get_children();
    return std::all_of(children.begin(),
                       children.end(),
                       [&](entt::handle child)
                       {
                           return is_whole_subtree_in(child, entities);
                       });
}

/**
 * The topmost entities of @p entities whose whole subtree belongs to @p entities: removing or
 * restoring those covers the set, and an entity something else lives under (a child the user
 * added under a node) stays with it.
 */
auto find_subtree_roots(const std::vector<entt::handle>& entities) -> std::vector<entt::handle>
{
    std::vector<entt::handle> roots;
    for(const auto& entity : entities)
    {
        if(!entity.valid() || !is_whole_subtree_in(entity, entities))
        {
            continue;
        }
        const auto parent = get_parent_entity(entity);
        if(!parent || !is_whole_subtree_in(parent, entities))
        {
            roots.emplace_back(entity);
        }
    }
    return roots;
}

/// Consumer slot in transform_component's indexed dirty flags owned by the model pose
/// refresh. Set on any transform/flags change, cleared per node when the pose consumes it.
constexpr uint8_t pose_dirty_id = dirty_ids::model_pose;
/// The owner's slot for the same refresh (submeshes the owner places directly).
constexpr uint8_t owner_pose_dirty_id = dirty_ids::model_owner_pose;

using submesh_mapping = decltype(submesh_pose_mat4::submesh_to_transform_indices);

/// Exact equality of two submesh-to-transform maps: the same transform indices with the same flags.
auto is_same_mapping(const submesh_mapping& lhs, const submesh_mapping& rhs) -> bool
{
    if(lhs.size() != rhs.size())
    {
        return false;
    }
    const auto is_same_entry = [](const submesh_pose_mat4::transform_index& a, const submesh_pose_mat4::transform_index& b)
    {
        return a.index == b.index && a.active == b.active && a.casts_shadow == b.casts_shadow;
    };
    for(size_t i = 0; i < lhs.size(); ++i)
    {
        if(!std::equal(lhs[i].begin(), lhs[i].end(), rhs[i].begin(), rhs[i].end(), is_same_entry))
        {
            return false;
        }
    }
    return true;
}

/**
 * Refreshes the pose outputs from the armature entities in a single change-driven walk.
 *
 * Pass 1 resolves each entity's transform_component once into a scratch list while OR-ing
 * the model_pose dirty bits (and the owner's model_owner_pose bit). When nothing is dirty
 * (and @p force is false) the function returns false WITHOUT touching any output - the
 * cached poses/proxies are still valid. Pass 2 reuses the resolved pointers (no second
 * transform lookup) to rebuild the outputs and clears the consumed dirty bits.
 *
 * Submeshes no armature node places (meshes without an armature at all - primitives - or
 * nodes without entities) are drawn by the submit paths with the owner's world transform as
 * instance 0; pass 2 publishes exactly that box as their proxy, so per-submesh culling and
 * the nested-cascade shadow culling see them like any placed submesh instead of falling
 * back to "intersect".
 *
 * @param owner_transform The model entity's own transform, or null.
 * @param is_pose_changed Set when the rebuild placed a bone or submesh node elsewhere, or mapped the submeshes
 *        differently, than the outputs it replaced (or @p force). A rebuild of the same pose leaves it false:
 *        animation writes its pose and an IK pass writes the same final one back every frame.
 * @return True when a rebuild ran, false when everything was clean and outputs were kept.
 */
auto get_transforms_for_entities(const std::vector<entt::handle>& entities,
                                 transform_component* owner_transform,
                                 const mesh& render_mesh,
                                 submesh_pose_mat4& submesh_pose,
                                 pose_mat4& bone_pose,
                                 submesh_render_proxies& proxies,
                                 bool force,
                                 bool& is_pose_changed) -> bool
{
    // Reused per pool-thread; update_armature runs one task per model with no interleaving.
    thread_local std::vector<transform_component*> transform_scratch;
    transform_scratch.clear();
    transform_scratch.reserve(entities.size());

    bool any_dirty = force;
    any_dirty |= owner_transform != nullptr && owner_transform->is_dirty(owner_pose_dirty_id);
    for(const auto& e : entities)
    {
        auto* transform_comp = e.try_get<transform_component>();
        transform_scratch.push_back(transform_comp);
        any_dirty |= transform_comp != nullptr && transform_comp->is_dirty(pose_dirty_id);
    }

    if(!any_dirty)
    {
        return false;
    }

    const size_t submesh_count = render_mesh.get_submeshes_count(0);
    const size_t bone_count = render_mesh.get_skin_bind_data().get_bones().size();
    const auto& submeshes = render_mesh.get_submeshes(0);

    // The outputs being replaced, to compare the rebuild with. Swapped, not copied: the scratch and the pose trade
    // their storage.
    thread_local std::vector<math::mat4> previous_transforms;
    thread_local submesh_mapping previous_mapping;
    std::swap(previous_transforms, submesh_pose.transforms);
    std::swap(previous_mapping, submesh_pose.submesh_to_transform_indices);
    is_pose_changed = force || bone_pose.transforms.size() != bone_count;

    submesh_pose.clear();
    submesh_pose.reserve(submesh_count);
    bone_pose.transforms.resize(bone_count);
    proxies.begin_refresh(submesh_count);

    for(size_t i = 0; i < entities.size(); ++i)
    {
        auto* transform_comp = transform_scratch[i];
        if(transform_comp == nullptr)
        {
            continue;
        }

        const auto e = entities[i];
        auto&& [submesh_comp, bone_comp, active_comp] =
            e.try_get<submesh_component, bone_component, active_component>();

        const auto& transform_global = transform_comp->get_transform_global();
        const auto& transform_matrix = transform_global.get_matrix();

        // The pose has consumed this node's transform; clear our dirty slot so
        // the next update_armature can skip when nothing changes again.
        transform_comp->set_dirty(pose_dirty_id, false);

        if(submesh_comp && !submesh_comp->entries.empty())
        {
            const bool node_active = active_comp != nullptr;

            // Add the transform once; each submesh entry maps to it with its
            // own per-instance flags.
            const uint32_t trans_index = submesh_pose.add_transform(transform_matrix);

            for(const auto& entry : submesh_comp->entries)
            {
                const uint32_t submesh_index = entry.submesh_index;
                submesh_pose.map_submesh(submesh_index,
                                         trans_index,
                                         node_active && entry.enabled,
                                         entry.casts_shadow);

                // Cache the world-space bounds for this instance so culling
                // and per-submesh LOD become cheap AABB tests. Alignment with
                // the pose instance list is maintained by always pushing a
                // bounds record (possibly unpopulated) per mapped instance.
                //
                // Skinned submeshes are excluded: their geometry lives in mesh
                // bind space and is driven by bone palettes, so the owning
                // node's transform is meaningless for bounds. Their world
                // bounds come from skinned_bounds in update_armature instead;
                // an unpopulated record keeps the instance list aligned.
                math::bbox world_bounds{};
                const auto* sm = submesh_index < submeshes.size() ? submeshes[submesh_index] : nullptr;
                if(sm != nullptr && !sm->skinned && sm->bbox.is_populated())
                {
                    world_bounds = math::bbox::mul(sm->bbox, transform_global);
                }
                proxies.add_instance_bounds(submesh_index, world_bounds);
            }
        }

        if(bone_comp)
        {
            auto bone_index = bone_comp->bone_index;
            if(bone_index < bone_pose.transforms.size())
            {
                is_pose_changed = is_pose_changed || bone_pose.transforms[bone_index] != transform_matrix;
                bone_pose.transforms[bone_index] = transform_matrix;
            }
        }
    }
    is_pose_changed = is_pose_changed || submesh_pose.transforms != previous_transforms ||
                      !is_same_mapping(submesh_pose.submesh_to_transform_indices, previous_mapping);

    // Owner-placed submeshes: the box the submit paths' fallback instance actually covers.
    if(owner_transform != nullptr)
    {
        owner_transform->set_dirty(owner_pose_dirty_id, false);
        const auto& owner_global = owner_transform->get_transform_global();
        for(size_t submesh_index = 0; submesh_index < submesh_count; ++submesh_index)
        {
            if(submesh_pose.has_transforms(static_cast<uint32_t>(submesh_index)))
            {
                continue;
            }
            const auto* sm = submesh_index < submeshes.size() ? submeshes[submesh_index] : nullptr;
            if(sm == nullptr || sm->skinned || !sm->bbox.is_populated())
            {
                continue;
            }
            proxies.add_instance_bounds(static_cast<uint32_t>(submesh_index),
                                        math::bbox::mul(sm->bbox, owner_global));
        }
    }

    return true;
}

} // namespace

auto model_component::create_armature(bool force, std::vector<entt::handle>* created_roots) -> bool
{
    bool has_processed_armature = !get_armature_entities().empty();

    if(force || !has_processed_armature)
    {
        auto lod = model_.get_lod(0);
        if(!lod)
        {
            return false;
        }
        auto mesh = lod.get();

        auto owner = get_owner();

        armature_walk walk;
        // The walk appends every node to the reference pose, so it starts over.
        bind_pose_ = {};
        if(process_armature(*mesh, owner, walk, bind_pose_))
        {
            set_armature_entities(walk.nodes);
            settle_previous_armature();
            if(created_roots)
            {
                *created_roots = find_subtree_roots(walk.created_nodes);
            }

            const auto& skin_data = mesh->get_skin_bind_data();
            // Has skinning data?
            if(skin_data.has_bones())
            {
                set_static(false);
            }

            return true;
        }
    }

    return false;
}

auto model_component::update_armature() -> bool
{
    // APPLOG_TRACE_PERF_NAMED(std::chrono::microseconds, "Model/Update Armature");

    // Visibility gate FIRST - before even touching the mesh asset handle. When no view
    // (camera or shadow pass) consumed this model recently and conservative culling bounds
    // exist, skip everything: no asset access, no dirty scan, no refresh. Correctness does
    // NOT depend on this gate being right: update_world_bounds then uses the grow-only
    // culling bounds anchored to the root bone, which track bone-driven root motion and
    // never depend on fresh proxies. If those bounds enter a frustum the model is drawn
    // (conservatively, last cached pose) and marked used, un-gating the refresh next
    // frame. Explicit forces (pose_dirty_: set_model, armature rebuilds, inspector edits)
    // bypass the gate since they can change what the bounds should be.
    if(!pose_dirty_ && !was_used_last_frame() && culling_bounds_local_.is_populated())
    {
        // Cached per-submesh proxies may no longer match the real pose; submit paths must
        // fall back to conservative behavior (draw) until the next full refresh.
        render_proxies_stale_ = true;
        return false;
    }

    auto lod = model_.get_lod(0);
    if(!lod)
    {
        return false;
    }

    auto mesh = lod.get();

    const auto& armature_entities = get_armature_entities();
    const auto& skin_data = mesh->get_skin_bind_data();
    auto* owner_transform = get_owner().try_get<transform_component>();

    // Change-driven refresh in ONE walk: the dirty check and the rebuild share the same
    // pass over the armature entities (the transform lookup is done once and reused).
    // When nothing changed and nothing forced a refresh, outputs are left untouched and
    // we early-out - visible idle models cost a single pointer+bit scan.
    bool is_pose_changed = false;
    const bool refreshed = get_transforms_for_entities(armature_entities,
                                                       owner_transform,
                                                       *mesh,
                                                       submesh_pose_,
                                                       bone_pose_,
                                                       render_proxies_,
                                                       pose_dirty_,
                                                       is_pose_changed);
    if(!refreshed)
    {
        // Nothing changed since the last full refresh, so proxies flagged stale while
        // off-screen turned out to be valid after all.
        render_proxies_stale_ = false;
        return false;
    }
    pose_dirty_ = false;
    render_proxies_stale_ = false;

    // Has skinning data?
    if(skin_data.has_bones())
    {
        const auto& palettes = mesh->get_bone_palettes();
        const size_t palette_count = palettes.size();
        
        // Early exit if no palettes
        if(palette_count == 0)
        {
            return is_pose_changed;
        }
        
        skinning_pose_.resize(palette_count);
        
        // Cache bone transforms reference to avoid repeated lookups
        const auto& bone_transforms = bone_pose_.transforms;
        const auto& bones = skin_data.get_bones();

        // Animated world-space bounds per skinned submesh: union of each palette bone's
        // bind-space bounds transformed by its current world transform. Any vertex skinned
        // by the palette is a convex combination of per-bone transformed points, so the
        // enclosing AABB of all bone boxes is a valid conservative bound.
        render_proxies_.skinned_bounds.assign(mesh->get_submeshes_count(0), math::bbox{});
        const auto& submeshes = mesh->get_submeshes(0);

        for(size_t i = 0; i < palette_count; ++i)
        {
            const auto& palette = palettes[i];
            // Apply the bone palette.
            skinning_pose_[i].transforms = palette.get_skinning_matrices(bone_transforms, skin_data);

            // Palettes map 1:1 to submeshes (bind_skin creates one per submesh, in order);
            // only skinned submeshes carry meaningful palette bones/bounds.
            if(i < submeshes.size() && submeshes[i] != nullptr && !submeshes[i]->skinned)
            {
                continue;
            }

            math::bbox submesh_bounds{};
            for(uint32_t bone_index : palette.get_bones())
            {
                // Unpopulated bounds mean the bone has no weighted vertex influences (e.g.
                // assimp's zero-weight root joint entry) - it deforms nothing, so skipping
                // it is exact, not merely conservative. On legacy assets without per-bone
                // bounds every bone is skipped and the bounds simply stay unpopulated,
                // which consumers already treat as "no cached data" (draw conservatively).
                if(bone_index >= bones.size() || bone_index >= bone_transforms.size() ||
                   !bones[bone_index].bounds.is_populated())
                {
                    continue;
                }

                const auto bone_world_bounds = math::bbox::mul(bones[bone_index].bounds, bone_transforms[bone_index]);
                submesh_bounds.add_point(bone_world_bounds.min);
                submesh_bounds.add_point(bone_world_bounds.max);
            }

            if(submesh_bounds.is_populated() && i < render_proxies_.skinned_bounds.size())
            {
                render_proxies_.skinned_bounds[i] = submesh_bounds;
                render_proxies_.animated_bounds.add_point(submesh_bounds.min);
                render_proxies_.animated_bounds.add_point(submesh_bounds.max);
            }
        }
    }

    return is_pose_changed;
}

auto model_component::init_armature(bool force, std::vector<entt::handle>* created_roots) -> bool
{
    auto lod = model_.get_lod(0);
    if(!lod)
    {
        // No mesh binds any node: every node the previous mesh bound is stale.
        settle_previous_armature();
        return false;
    }

    auto mesh = lod.get();
    const auto& skin_data = mesh->get_skin_bind_data();
    const auto& armature = mesh->get_armature();

    bool recreate_armature = force;
    // Built once its node entities exist. The pose is no proof: a model activated after this
    // frame's update reaches update_armature first, which fills the pose from no entities.
    recreate_armature |= armature && get_armature_entities().empty();
    recreate_armature |= skin_data.has_bones() && skinning_pose_.empty();

    if(recreate_armature)
    {
        if(create_armature(force, created_roots))
        {
            return update_armature();
        }
    }
    if(!armature)
    {
        // A mesh without an armature binds no node: every node the previous mesh bound is stale.
        settle_previous_armature();
    }

    return false;
}

void model_component::resolve_pending_default_materials()
{
    if(!model_.is_default_materials_pending())
    {
        return;
    }
    auto& am = engine::context().get_cached<asset_manager>();
    if(model_.apply_default_materials(am, false))
    {
        touch();
    }
}

auto model_component::rebuild_armature() -> std::vector<entt::handle>
{
    std::vector<entt::handle> created_roots;
    if(is_armature_rebuild_pending())
    {
        init_armature(false, &created_roots);
    }
    return created_roots;
}

auto model_component::is_armature_rebuild_pending() const -> bool
{
    return armature_rebuild_pending_;
}

auto model_component::take_stale_armature_entities() -> std::vector<entt::handle>
{
    std::vector<entt::handle> stale;
    std::copy_if(stale_armature_entities_.begin(),
                 stale_armature_entities_.end(),
                 std::back_inserter(stale),
                 [](entt::handle entity)
                 {
                     return entity.valid();
                 });
    stale_armature_entities_.clear();
    return stale;
}

void model_component::settle_previous_armature()
{
    if(!armature_rebuild_pending_)
    {
        return;
    }
    // The nodes the current armature binds again are not stale.
    std::vector<entt::handle> stale;
    std::copy_if(previous_armature_entities_.begin(),
                 previous_armature_entities_.end(),
                 std::back_inserter(stale),
                 [&](entt::handle entity)
                 {
                     return entity.valid() && !contains_entity(armature_entities_, entity);
                 });
    const auto roots = find_subtree_roots(stale);
    stale_armature_entities_.insert(stale_armature_entities_.end(), roots.begin(), roots.end());
    previous_armature_entities_.clear();
    armature_rebuild_pending_ = false;
}

void model_component::update_world_bounds(const math::transform& world_transform)
{
    auto lod = model_.get_lod(0);
    if(!lod)
    {
        return;
    }

    auto mesh = lod.get();
    if(!mesh)
    {
        return;
    }

    // Any real move of the box is a caster change, whatever produced it: node / bone
    // animation, the conservative fallback taking over from the tight pose box, or a mesh
    // asset finishing its load. None of those touch the owner's transform, so this is the
    // only signal a bounds consumer gets. Checked on every return path below.
    const auto previous_world_bounds = world_bounds_;
    auto signal_bounds_change = hpp::finally(
        [this, previous_world_bounds]()
        {
            if(world_bounds_ != previous_world_bounds)
            {
                render_dirty_.set(dirty_ids::shadow_caster);
            }
        });

    world_bounds_transform_ = world_transform;

    // Anchor transform for the conservative culling bounds. Root motion baked into bone
    // animation moves the topmost bone, not the owner, so anchoring to it keeps the
    // conservative box tracking the character even while the pose refresh is skipped.
    // Boneless armatures fall back to the owner transform.
    math::transform anchor_transform = world_transform;
    if(bounds_anchor_)
    {
        if(const auto* anchor_transform_comp = bounds_anchor_.try_get<transform_component>())
        {
            anchor_transform = anchor_transform_comp->get_transform_global();
        }
    }

    const auto is_invertible = [](const math::transform& t) -> bool
    {
        constexpr float min_scale = 0.000001f;
        const auto scale = t.get_scale();
        return std::abs(scale.x) > min_scale && std::abs(scale.y) > min_scale && std::abs(scale.z) > min_scale;
    };

    // Fresh proxies this frame: rebuild the pose bounds union in world space.
    // Replace the bind-pose bounds with the cached pose bounds so whole-model culling
    // tracks the actual pose instead of the import-time rest pose (compiled mesh bounds
    // are bind-pose only; animation-driven expansion happens exclusively here at runtime):
    //  - per-instance bounds cover node-attached rigid submeshes driven by node animation,
    //  - animated bounds cover skinned geometry (bone-transformed bind-space boxes).
    // A model can have both kinds at once, so the result is the UNION of the two;
    // dropping either would cull geometry that is actually on screen.
    if(render_proxies_.version != captured_proxies_version_)
    {
        const bool has_instance = render_proxies_.has_instance_bounds();
        const bool has_animated = render_proxies_.has_animated_bounds();
        if(has_instance || has_animated)
        {
            // If the mesh has skinned geometry but no animated bounds could be derived
            // (legacy asset without per-bone bounds), the pose bounds don't cover the
            // skinned parts - keep the bind-pose box unioned in as a conservative
            // fallback instead of culling them away.
            const bool skinned_uncovered = !has_animated && mesh->get_skinned_submeshes_count(0) > 0;
            world_bounds_ = skinned_uncovered ? math::bbox::mul(mesh->get_bounds(), world_transform) : math::bbox{};
            if(has_instance)
            {
                world_bounds_.add_point(render_proxies_.instance_bounds_union.min);
                world_bounds_.add_point(render_proxies_.instance_bounds_union.max);
            }
            if(has_animated)
            {
                world_bounds_.add_point(render_proxies_.animated_bounds.min);
                world_bounds_.add_point(render_proxies_.animated_bounds.max);
            }

            // Capture two boxes from the tight world bounds (degenerate scales cannot be
            // inverted - keep the previous captures in that case):
            //  - pose_local_bounds_ (owner space): tight, re-anchored on visible idle frames.
            //  - culling_bounds_local_ (anchor space): grow-only union of every observed
            //    pose, used whenever the refresh is skipped. Culling therefore never
            //    depends on fresh proxies and cannot deadlock a model into invisibility.
            if(is_invertible(world_transform) && is_invertible(anchor_transform))
            {
                pose_local_bounds_ = math::bbox::mul(world_bounds_, math::inverse(world_transform));

                const auto anchor_local = math::bbox::mul(world_bounds_, math::inverse(anchor_transform));
                culling_bounds_local_.add_point(anchor_local.min);
                culling_bounds_local_.add_point(anchor_local.max);

                captured_proxies_version_ = render_proxies_.version;
            }
        }
        else
        {
            world_bounds_ = math::bbox::mul(mesh->get_bounds(), world_transform);
        }
        return;
    }

    // Refresh skipped by the visibility gate: the cached pose (and thus the tight snapshot)
    // may be stale. Use the conservative grow-only bounds re-anchored to the root bone's
    // CURRENT transform - one transform read + one bbox transform - so a model whose
    // animation carries it toward the frustum is re-discovered and re-enters rendering.
    if(render_proxies_stale_ && culling_bounds_local_.is_populated())
    {
        world_bounds_ = math::bbox::mul(culling_bounds_local_, anchor_transform);
        return;
    }

    // Proxies valid, simply nothing moved (visible idle model): re-anchor the tight
    // pose-local bounds to the current owner transform so culling stays tight while
    // game logic moves the whole character.
    if(pose_local_bounds_.is_populated())
    {
        world_bounds_ = math::bbox::mul(pose_local_bounds_, world_transform);
        return;
    }

    // No pose bounds at all (plain mesh without armature-driven placements): bind-pose box.
    world_bounds_ = math::bbox::mul(mesh->get_bounds(), world_transform);
}

auto model_component::get_world_bounds() const -> const math::bbox&
{
    return world_bounds_;
}

auto model_component::get_world_bounds_transform() const -> const math::transform&
{
    return world_bounds_transform_;
}

auto model_component::get_local_bounds(uint32_t lod_index) const -> const math::bbox&
{
    auto lod = model_.get_lod(lod_index);
    if(!lod)
    {
        return math::bbox::empty;
    }

    auto mesh = lod.get();
    if(mesh)
    {
        return mesh->get_bounds();
    }

    return math::bbox::empty;
}

void model_component::set_last_render_frame(uint64_t frame)
{
    last_render_frame_ = frame;
}

auto model_component::get_last_render_frame() const noexcept -> uint64_t
{
    return last_render_frame_;
}

auto model_component::is_newly_created() const noexcept -> bool
{
    return last_render_frame_ == 0;
}

auto model_component::was_used_last_frame() const noexcept -> bool
{
    auto current_frame = gfx::get_render_frame();
    bool is_new = is_newly_created();
    bool was_used_recently = current_frame - last_render_frame_ <= 1;
    return is_new || was_used_recently;
}

auto model_component::is_skinned() const -> bool
{
    auto lod = model_.get_lod(0);
    if(!lod)
    {
        return false;
    }

    auto mesh = lod.get();
    if(mesh)
    {
        return mesh->get_skinned_submeshes_count() > 0;
    }

    return false;
}

auto model_component::get_bind_pose() const -> const animation_pose&
{
    return bind_pose_;
}

auto model_component::get_armature_root_entity() const -> entt::handle
{
    auto lod = model_.get_lod(0);
    if(!lod)
    {
        return {};
    }

    const auto mesh = lod.get();
    const auto& armature = mesh->get_armature();
    if(!armature)
    {
        return {};
    }

    for(const auto& entity : armature_entities_)
    {
        if(entity && entity.get<tag_component>().name == armature->name)
        {
            return entity;
        }
    }

    if(!armature_entities_.empty())
    {
        return armature_entities_.front();
    }

    return {};
}

auto model_component::get_facing_adjustment_rotation() const -> math::quat
{
    if(auto root = get_armature_root_entity())
    {
        return root.get<transform_component>().get_rotation_local();
    }

    if(!bind_pose_.nodes.empty())
    {
        return bind_pose_.nodes.front().transform.get_rotation();
    }

    return math::identity<math::quat>();
}

void model_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);

    auto& component = entity.get<model_component>();
    component.set_owner(entity);

    // A copied component starts without the source's node bookkeeping: those are its entities.
    component.set_armature_entities({});
    component.previous_armature_entities_.clear();
    component.stale_armature_entities_.clear();
    component.armature_rebuild_pending_ = false;
}

void model_component::on_destroy_component(entt::registry& r, entt::entity e)
{
    // A caster left the scene. The per-frame walk cannot see this - the entity is already out
    // of its view - so the cached caster lists have to be retired here. Covers entity
    // destruction too: destroying an entity destroys its components.
    bump_shadow_caster_revision(r, e);
}

void bump_shadow_caster_revision(entt::registry& registry, entt::entity /*entity*/)
{
    if(auto* revision = registry.ctx().find<shadow_caster_revision>())
    {
        ++revision->value;
    }
}

void model_component::touch()
{
    render_dirty_.set();
    // Membership of the caster set can change here (enabled / static / casts_shadow / a
    // different model), and the walk cannot tell a model that LEFT the set from one that was
    // never in it.
    auto owner = get_owner();
    if(owner)
    {
        bump_shadow_caster_revision(*owner.registry(), owner.entity());
    }
}

auto model_component::is_dirty(uint8_t id) const noexcept -> bool
{
    return render_dirty_[id];
}

void model_component::set_dirty(uint8_t id, bool dirty) noexcept
{
    render_dirty_.set(id, dirty);
}

void model_component::set_enabled(bool enabled)
{
    if(enabled_ == enabled)
    {
        return;
    }

    touch();

    enabled_ = enabled;
}

void model_component::set_casts_shadow(bool cast_shadow)
{
    if(casts_shadow_ == cast_shadow)
    {
        return;
    }

    touch();

    casts_shadow_ = cast_shadow;
}

void model_component::set_static(bool is_static)
{
    if(static_ == is_static)
    {
        return;
    }

    touch();

    static_ = is_static;
}

auto model_component::is_enabled() const -> bool
{
    return enabled_;
}

auto model_component::casts_shadow() const -> bool
{
    return casts_shadow_;
}

auto model_component::is_static() const -> bool
{
    return static_;
}

auto model_component::get_model() const -> const model&
{
    return model_;
}

void model_component::set_model(const model& model)
{
    const bool is_mesh_changed = model_.get_lod(0).uid() != model.get_lod(0).uid();
    model_ = model;
    if(is_mesh_changed && !armature_entities_.empty())
    {
        // The new mesh binds its own nodes: the armature is rebuilt, and the nodes of the old one
        // it does not bind again become stale (take_stale_armature_entities).
        previous_armature_entities_.insert(previous_armature_entities_.end(),
                                           armature_entities_.begin(),
                                           armature_entities_.end());
        set_armature_entities({});
        armature_rebuild_pending_ = true;
    }

    // Different mesh asset - poses/proxies and captured bounds derived from the old one
    // are invalid.
    pose_local_bounds_ = {};
    culling_bounds_local_ = {};
    captured_proxies_version_ = ~0ULL;
    mark_pose_dirty();

    touch();
}

void model_component::set_shared_material(const asset_handle<material>& material, uint32_t index)
{
    model_.set_shared_material(material, index);

    touch();
}

auto model_component::get_material_instance(uint32_t index) const -> const material_instance&
{
    return model_.get_material_instance(index);
}

auto model_component::set_material_instance_value(uint32_t index, entt::id_type property, const entt::meta_any& value)
    -> bool
{
    const bool applied = model_.get_material_instance(index).set_override(property, value);
    touch();
    return applied;
}

void model_component::revert_material_instance_value(uint32_t index, entt::id_type property)
{
    model_.get_material_instance(index).revert(property);
    touch();
}

void model_component::revert_material_instance(uint32_t index)
{
    model_.get_material_instance(index).revert_all();
    touch();
}

void model_component::update_material_instances()
{
    model_.update_material_instances();
}

auto model_component::get_bone_transforms() const -> const pose_mat4&
{
    return bone_pose_;
}

auto model_component::get_skinning_transforms() const -> const std::vector<pose_mat4>&
{
    return skinning_pose_;
}

auto model_component::get_submesh_transforms() const -> const submesh_pose_mat4&
{
    return submesh_pose_;
}

std::atomic<uint32_t> model_component::velocity_recording_request_frame_{0};

void model_component::request_velocity_recording(uint32_t frame)
{
    // Stored with +1 so 0 keeps meaning "never requested" even for render frame 0.
    velocity_recording_request_frame_.store(frame + 1, std::memory_order_relaxed);
}

auto model_component::is_velocity_recording_active(uint32_t frame) -> bool
{
    const uint32_t request = velocity_recording_request_frame_.load(std::memory_order_relaxed);
    // The request is made during render of frame N; recording happens in before-render of
    // frame N+1, so keep a 2-frame window before the recording decays to zero cost.
    return request != 0u && frame + 1u <= request + 2u;
}

void model_component::record_velocity_state(uint32_t frame, const math::mat4& current_world, bool transform_moved)
{
    if(!is_velocity_recording_active(frame))
    {
        // Reset so a later re-enable re-initializes (prev = current, no bogus first-frame motion).
        velocity_initialized_ = false;
        has_motion_ = false;
        return;
    }
    if(velocity_initialized_ && velocity_state_frame_ == frame)
    {
        // Editor panels (scene + game + thumbnails) invoke before-render more than once per
        // frame; the promotion must run exactly once or prev state collapses onto current.
        return;
    }
    const bool first = !velocity_initialized_;
    velocity_state_frame_ = frame;
    velocity_initialized_ = true;
    // recorded_world_ holds the matrix the previous frame rendered with (world transforms are
    // already THIS frame's value here). The pose caches still hold LAST frame's values because
    // update_armature runs after this promotion, so a plain copy is the correct prev snapshot.
    prev_world_ = first ? current_world : recorded_world_;
    recorded_world_ = current_world;
    prev_submesh_pose_ = submesh_pose_;
    prev_skinning_pose_ = skinning_pose_;
    // Skinned models count as movers unconditionally: an animated palette changes every frame
    // without necessarily touching any entity transform.
    has_motion_ = !first && (transform_moved || !skinning_pose_.empty());
}

void model_component::mark_motion(bool moved)
{
    if(moved && velocity_initialized_)
    {
        has_motion_ = true;
    }
}

auto model_component::has_motion() const -> bool
{
    return has_motion_;
}

void model_component::update_shadow_change(uint64_t frame, bool changed)
{
    // A material edit (a cutout threshold, a cull mode, a texture) changes the shadow without moving the model.
    const uint64_t materials_revision = model_.get_materials_revision();
    if(changed || materials_revision != shadow_materials_revision_)
    {
        shadow_change_frame_ = frame;
        shadow_materials_revision_ = materials_revision;
    }
}

auto model_component::get_shadow_change_frame() const noexcept -> uint64_t
{
    return shadow_change_frame_;
}

auto model_component::get_prev_world_transform() const -> const math::mat4&
{
    return prev_world_;
}

auto model_component::get_prev_submesh_transforms() const -> const submesh_pose_mat4&
{
    return prev_submesh_pose_;
}

auto model_component::get_prev_skinning_transforms() const -> const std::vector<pose_mat4>&
{
    return prev_skinning_pose_;
}

auto model_component::get_render_proxies() const -> const submesh_render_proxies&
{
    return render_proxies_;
}

auto model_component::get_submit_extras(bool shadow_pass) const -> model_submit_extras
{
    model_submit_extras extras;
    // Proxies flagged stale (pose refresh skipped while off-screen and transforms may have
    // changed) are withheld: submit paths treat missing cached bounds as "draw
    // conservatively", which only costs extra draws on the re-entry frame. The next
    // update_armature runs a full refresh and clears the flag.
    extras.proxies = render_proxies_stale_ ? nullptr : &render_proxies_;
    extras.shadow_pass = shadow_pass;
    return extras;
}

void model_component::set_armature_entities(const std::vector<entt::handle>& entities)
{
    armature_entities_ = entities;
    armature_version_ = last_armature_version.fetch_add(1, std::memory_order_relaxed) + 1;
    rebuild_armature_cache();

    // Culling-bounds anchor: the topmost bone (entities are in hierarchy order, parents
    // first), so root motion baked into bone animation moves the conservative culling box
    // with the character. Null (-> owner transform) when the armature has no bones.
    bounds_anchor_ = {};
    for(const auto& e : armature_entities_)
    {
        if(e && e.any_of<bone_component>())
        {
            bounds_anchor_ = e;
            break;
        }
    }

    // The entity set changed - cached poses/proxies and captured bounds no longer match it.
    pose_local_bounds_ = {};
    culling_bounds_local_ = {};
    captured_proxies_version_ = ~0ULL;
    mark_pose_dirty();

    touch();
}

void model_component::mark_pose_dirty() noexcept
{
    pose_dirty_ = true;
}

void model_component::rebuild_armature_cache()
{
    armature_name_to_index_.clear();
    armature_name_to_index_.reserve(armature_entities_.size());
    armature_base_name_to_index_.clear();
    armature_base_name_to_index_.reserve(armature_entities_.size());
    
    for(size_t i = 0; i < armature_entities_.size(); ++i)
    {
        const auto& e = armature_entities_[i];
        if(e)
        {
            const auto& tag_comp = e.get<tag_component>();
            armature_name_to_index_[tag_comp.name] = i;
            const auto [base, inserted] =
                armature_base_name_to_index_.emplace(std::string(get_name_without_namespace(tag_comp.name)), i);
            if(!inserted)
            {
                base->second = AMBIGUOUS_ARMATURE_INDEX;
            }
        }
    }
}

auto model_component::get_armature_index_by_name_cached(const std::string& node_name) const -> int
{
    auto it = armature_name_to_index_.find(node_name);
    if(it != armature_name_to_index_.end())
    {
        return static_cast<int>(it->second);
    }
    return -1;
}

auto model_component::get_armature_index_by_base_name_cached(const std::string& node_name) const -> int
{
    const auto it = armature_base_name_to_index_.find(std::string(get_name_without_namespace(node_name)));
    if(it == armature_base_name_to_index_.end() || it->second == AMBIGUOUS_ARMATURE_INDEX)
    {
        return -1;
    }
    return static_cast<int>(it->second);
}

auto model_component::get_armature_version() const noexcept -> uint64_t
{
    return armature_version_;
}

auto model_component::get_armature_entities() const -> const std::vector<entt::handle>&
{
    return armature_entities_;
}

auto model_component::get_armature_by_index(size_t index) const -> entt::handle
{
    if(index >= armature_entities_.size())
    {
        return {};
    }

    return armature_entities_[index];
}

auto model_component::get_lod_data_for_camera(const camera* cam, uint64_t current_frame) -> lod_data&
{
    if(!cam)
    {
        static thread_local lod_data empty_lod_data;
        return empty_lod_data;
    }
    const auto unique_id = reinterpret_cast<uintptr_t>(cam);
    // Get or create entry for this view
    auto& camera_state = per_camera_lod_data_[unique_id];
    
    // Update last access frame
    camera_state.last_access_frame = current_frame;
    
    return camera_state.data;
}

void model_component::cleanup_stale_lod_data(uint64_t current_frame, uint64_t max_frames_inactive)
{
    // Remove entries that haven't been accessed recently
    for(auto it = per_camera_lod_data_.begin(); it != per_camera_lod_data_.end();)
    {
        const auto frames_since_access = current_frame - it->second.last_access_frame;
        if(frames_since_access > max_frames_inactive)
        {
            it = per_camera_lod_data_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace unravel
