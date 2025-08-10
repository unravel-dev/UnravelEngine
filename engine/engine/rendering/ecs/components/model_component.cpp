#include "model_component.h"
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>


namespace unravel
{
namespace
{

auto get_bone_entity(const std::string& bone_id, const std::vector<entt::handle>& entities) -> entt::handle
{
    for(const auto& e : entities)
    {
        if(e)
        {
            const auto& tag = e.get<tag_component>();
            if(tag.name == bone_id)
            {
                return e;
            }
        }
    }

    return {};
}

auto process_node_impl(const std::unique_ptr<mesh::armature_node>& node,
                       const skin_bind_data& bind_data,
                       entt::handle& parent,
                       std::vector<entt::handle>& nodes,
                       animation_pose& ref_pose) -> entt::handle
{
    auto entity_node = parent;

    if(entity_node == parent)
    {
        auto& parent_trans_comp = parent.get<transform_component>();
        const auto& children = parent_trans_comp.get_children();
        auto found_node = get_bone_entity(node->name, children);
        if(found_node)
        {
            entity_node = found_node;
        }
        else
        {
            auto& reg = *entity_node.registry();
            entity_node = scene::create_entity(reg, node->name, parent);
        }
        auto& trans_comp = entity_node.get<transform_component>();
        trans_comp.set_transform_local(node->local_transform);

        nodes.emplace_back(entity_node);

        if(!node->submeshes.empty())
        {
            auto& comp = entity_node.get_or_emplace<submesh_component>();
            comp.submeshes = node->submeshes;
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
        ref_node.transform = node->local_transform; // Save its local (bind) transform
        ref_pose.nodes.push_back(ref_node);
    }

    return entity_node;
}

void process_node(const std::unique_ptr<mesh::armature_node>& node,
                  const skin_bind_data& bind_data,
                  entt::handle parent,
                  std::vector<entt::handle>& nodes,
                  animation_pose& ref_pose)
{
    if(!parent)
        return;

    auto entity_node = process_node_impl(node, bind_data, parent, nodes, ref_pose);
    for(auto& child : node->children)
    {
        process_node(child, bind_data, entity_node, nodes, ref_pose);
    }
}

auto process_armature(const mesh& render_mesh,
                      entt::handle parent,
                      std::vector<entt::handle>& nodes,
                      animation_pose& ref_pose) -> bool
{
    const auto& root = render_mesh.get_armature();
    if(!root)
    {
        return false;
    }

    const auto& skin_data = render_mesh.get_skin_bind_data();
    process_node(root, skin_data, parent, nodes, ref_pose);

    return true;
}

void get_transforms_for_entities(const std::vector<entt::handle>& entities,
                                 size_t submesh_count,
                                 pose_mat4& submesh_pose,
                                 size_t bone_count,
                                 pose_mat4& bone_pose)
{
    size_t entities_count = entities.size();

    submesh_pose.transforms.clear();
    submesh_pose.transforms.reserve(submesh_count);
    bone_pose.transforms.resize(bone_count);

    // Use std::for_each with the view's iterators
    std::for_each(entities.begin(),
                  entities.end(),
                  [&](entt::handle e)
                  {
                      auto&& [transform_comp, submesh_comp, bone_comp] =
                          e.try_get<transform_component, submesh_component, bone_component>();
                      if(transform_comp)
                      {
                          const auto& transform_global = transform_comp->get_transform_global().get_matrix();

                          if(submesh_comp)
                          {
                              submesh_pose.transforms.emplace_back(transform_global);
                          }

                          if(bone_comp)
                          {
                              auto bone_index = bone_comp->bone_index;
                              bone_pose.transforms[bone_index] = transform_global;
                          }
                      }
                  });
}

} // namespace

auto model_component::create_armature(bool force) -> bool
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

        std::vector<entt::handle> armature_entities;
        if(process_armature(*mesh, owner, armature_entities, bind_pose_))
        {
            set_armature_entities(armature_entities);

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
    auto lod = model_.get_lod(0);
    if(!lod)
    {
        return false;
    }

    auto mesh = lod.get();

    const auto& armature_entities = get_armature_entities();
    const auto& skin_data = mesh->get_skin_bind_data();

    auto bones_count = skin_data.get_bones().size();
    auto submeshes_count = mesh->get_submeshes_count();

    get_transforms_for_entities(armature_entities, submeshes_count, submesh_pose_, bones_count, bone_pose_);

    // Has skinning data?
    if(skin_data.has_bones())
    {
        const auto& palettes = mesh->get_bone_palettes();
        skinning_pose_.resize(palettes.size());
        for(size_t i = 0; i < palettes.size(); ++i)
        {
            const auto& palette = palettes[i];
            // Apply the bone palette.
            skinning_pose_[i].transforms = palette.get_skinning_matrices(bone_pose_.transforms, skin_data);
        }
    }

    return true;
}

auto model_component::init_armature(bool force) -> bool
{
    auto lod = model_.get_lod(0);
    if(!lod)
    {
        return false;
    }

    auto mesh = lod.get();
    const auto& skin_data = mesh->get_skin_bind_data();
    const auto& armature = mesh->get_armature();

    bool recreate_armature = force;
    recreate_armature |= armature && submesh_pose_.transforms.empty();
    recreate_armature |= skin_data.has_bones() && skinning_pose_.empty();

    if(recreate_armature)
    {
        if(create_armature(force))
        {
            return update_armature();
        }
    }

    return false;
}

void model_component::update_world_bounds(const math::transform& world_transform)
{
    auto lod = model_.get_lod(0);
    if(!lod)
    {
        return;
    }

    auto mesh = lod.get();
    if(mesh)
    {
        const auto& bounds = mesh->get_bounds();

        world_bounds_ = math::bbox::mul(bounds, world_transform);
        world_bounds_transform_ = world_transform;
    }
}

auto model_component::get_world_bounds() const -> const math::bbox&
{
    return world_bounds_;
}

auto model_component::get_world_bounds_transform() const -> const math::transform&
{
    return world_bounds_transform_;
}

auto model_component::get_local_bounds() const -> const math::bbox&
{
    auto lod = model_.get_lod(0);
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

auto model_component::was_used_last_frame() const noexcept -> bool
{
    auto current_frame = gfx::get_render_frame();
    bool is_newly_created = last_render_frame_ == 0;
    bool was_used_recently = current_frame - last_render_frame_ <= 1;
    return is_newly_created || was_used_recently;
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

void model_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);

    auto& component = entity.get<model_component>();
    component.set_owner(entity);

    component.set_armature_entities({});
}

void model_component::on_destroy_component(entt::registry& r, entt::entity e)
{
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

void model_component::set_casts_reflection(bool casts_reflection)
{
    if(casts_reflection_ == casts_reflection)
    {
        return;
    }

    touch();

    casts_reflection_ = casts_reflection;
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
    model_ = model;

    touch();
}

auto model_component::casts_reflection() const -> bool
{
    return casts_reflection_;
}

auto model_component::get_bone_transforms() const -> const pose_mat4&
{
    return bone_pose_;
}

auto model_component::get_skinning_transforms() const -> const std::vector<pose_mat4>&
{
    return skinning_pose_;
}

auto model_component::get_submesh_transforms() const -> const pose_mat4&
{
    return submesh_pose_;
}

void model_component::set_armature_entities(const std::vector<entt::handle>& entities)
{
    armature_entities_ = entities;

    touch();
}

auto model_component::get_armature_entities() const -> const std::vector<entt::handle>&
{
    return armature_entities_;
}

auto model_component::get_armature_by_id(const std::string& node_id) const -> entt::handle
{
    for(const auto& e : armature_entities_)
    {
        const auto& tag_comp = e.get<tag_component>();
        if(tag_comp.name == node_id)
        {
            return e;
        }
    }

    return {};
}

auto model_component::get_armature_index_by_id(const std::string& node_id) const -> int
{
    int result = 0;
    for(const auto& e : armature_entities_)
    {
        const auto& tag_comp = e.get<tag_component>();
        if(tag_comp.name == node_id)
        {
            return result;
        }

        result++;
    }

    return -1;
}

auto model_component::get_armature_by_index(size_t index) const -> entt::handle
{
    if(index >= armature_entities_.size())
    {
        return {};
    }

    return armature_entities_[index];
}

} // namespace unravel
