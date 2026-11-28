#include "material_instance.h"
#include "material_graph/graph_material.h"
#include "material_graph/named_values.h"
#include "material_graph/standard_material.h"

#include <reflection/reflection.h>

#include <algorithm>

namespace unravel
{
namespace
{

/// @p mat as its most derived reflected type, so the properties of every type in its hierarchy are found.
auto make_material_object(const material& mat) -> entt::meta_any
{
    entt::meta_any object = entt::forward_as_meta(const_cast<material&>(mat));
    entt::as_derived(object);
    return object;
}

auto are_equal(const entt::meta_any& lhs, const entt::meta_any& rhs) -> bool
{
    return lhs.type() == rhs.type() && lhs == rhs;
}

auto is_loaded(const material::sptr& mat) -> bool
{
    return mat && !asset_handle<material>::is_empty(mat);
}

/// Whether two lists of named values hold the same entries, in any order.
template<typename Value>
auto are_same_named_values(const std::vector<Value>& lhs, const std::vector<Value>& rhs) -> bool
{
    return lhs.size() == rhs.size() && std::all_of(lhs.begin(),
                                                   lhs.end(),
                                                   [&rhs](const Value& entry)
                                                   {
                                                       return std::find(rhs.begin(), rhs.end(), entry) != rhs.end();
                                                   });
}

/// Sets @p value as the entry named @p name of @p values; false when it was already its value.
template<typename Value, typename Field>
auto set_named(std::vector<Value>& values, const std::string& name, const Field& value) -> bool
{
    const Value wanted{name, value};
    if(auto* entry = find_named(values, name))
    {
        if(*entry == wanted)
        {
            return false;
        }
        *entry = wanted;
        return true;
    }
    values.push_back(wanted);
    return true;
}

/// Notes @p name in @p names once.
void note_changed_name(std::vector<std::string>& names, hpp::string_view name)
{
    if(std::find(names.begin(), names.end(), name) == names.end())
    {
        names.emplace_back(name);
    }
}

/// The aspects whose revisions differ between two snapshots of one material.
auto get_moved_aspects(const material& before, const material& after) -> render_change::mask
{
    render_change::mask moved = render_change::none;
    for_each_aspect(render_change::all,
                    [&](uint32_t i)
                    {
                        const render_change::mask aspect = 1u << i;
                        if(before.get_revision(aspect) != after.get_revision(aspect))
                        {
                            moved |= aspect;
                        }
                    });
    return moved;
}

/**
 * The aspects an override going from @p before to @p after changes (null: the shared material's value), as the
 * property's setter counts them: replayed on a copy of the shared material, so a setter that counts less than its
 * table entry (a base color edit that keeps the alpha) does here too. The table entry while @p shared is not loaded.
 */
auto get_override_changes(const material::sptr& shared,
                          const entt::meta_type& type,
                          entt::id_type property,
                          const entt::meta_any* before,
                          const entt::meta_any* after) -> render_change::mask
{
    if(!is_loaded(shared))
    {
        return get_material_property_changes(type, property);
    }
    const auto scratch = shared->clone();
    const auto shared_value = get_material_value(*shared, property);
    if(!set_material_value(*scratch, property, before ? *before : shared_value))
    {
        return get_material_property_changes(type, property);
    }
    const auto snapshot = scratch->clone();
    if(!set_material_value(*scratch, property, after ? *after : shared_value))
    {
        return get_material_property_changes(type, property);
    }
    return get_moved_aspects(*snapshot, *scratch);
}

} // namespace

auto get_material_value(const material& mat, entt::id_type property) -> entt::meta_any
{
    const auto object = make_material_object(mat);
    const auto data = object.type().data(property);
    return data ? entt::copy_meta_any(data.get(object)) : entt::meta_any{};
}

auto set_material_value(material& mat, entt::id_type property, const entt::meta_any& value) -> bool
{
    auto object = make_material_object(mat);
    const auto data = object.type().data(property);
    if(!data)
    {
        return false;
    }
    auto converted = entt::copy_meta_any(value);
    return converted.allow_cast(data.type()) && data.set(object, converted);
}

auto get_material_property_changes(const entt::meta_type& type, entt::id_type property) -> render_change::mask
{
    const auto data = type ? type.data(property) : entt::meta_data{};
    auto entry = data ? entt::get_attribute(data, "material_property") : entt::meta_any{};
    if(!entry.allow_cast<material_property_id>())
    {
        return render_change::material_aspects;
    }
    return get_material_property_traits(entry.cast<material_property_id>()).changes;
}

void copy_material_values(const material& source, material& target)
{
    const auto source_object = make_material_object(source);
    auto target_object = make_material_object(target);
    for_each_material_property(source_object.type(),
                               [&](entt::id_type, const entt::meta_data& data)
                               {
                                   auto incoming = data.get(source_object);
                                   if(!are_equal(data.get(target_object), incoming))
                                   {
                                       data.set(target_object, incoming);
                                   }
                               });
    target.copy_unreflected_values(source);
}

auto make_instance_values(const material_instance& instance, const material& shared) -> material::sptr
{
    auto values = shared.clone();
    for(const auto& entry : instance.get_overrides())
    {
        set_material_value(*values, entry.property, entry.value);
    }
    return values;
}

auto override_changed_values(material_instance& instance, const material& values) -> instance_value_changes
{
    instance_value_changes changes;
    const auto object = make_material_object(values);
    for_each_material_property(object.type(),
                               [&](entt::id_type property, const entt::meta_data& data)
                               {
                                   auto edited = data.get(object);
                                   if(are_equal(instance.get_value(property), edited))
                                   {
                                       return;
                                   }
                                   auto& outcome = instance.set_override(property, edited) ? changes.overridden
                                                                                           : changes.refused;
                                   outcome.push_back(property);
                               });
    return changes;
}

material_instance::material_instance(const material_instance& other)
    : shared_(other.shared_)
    , material_type_(other.material_type_)
    , graph_parameters_(other.graph_parameters_)
    , graph_textures_(other.graph_textures_)
    , legacy_copy_(other.legacy_copy_)
    , are_graph_overrides_changed_(other.has_graph_overrides())
{
    overrides_.reserve(other.overrides_.size());
    for(const auto& entry : other.overrides_)
    {
        overrides_.push_back({entry.property, entt::copy_meta_any(entry.value)});
    }
}

auto material_instance::operator=(const material_instance& other) -> material_instance&
{
    if(this != &other)
    {
        *this = material_instance(other);
    }
    return *this;
}

auto material_instance::operator==(const material_instance& other) const -> bool
{
    if(!(shared_ == other.shared_) || overrides_.size() != other.overrides_.size() ||
       legacy_copy_ != other.legacy_copy_ || !are_same_named_values(graph_parameters_, other.graph_parameters_) ||
       !are_same_named_values(graph_textures_, other.graph_textures_))
    {
        return false;
    }
    return std::all_of(overrides_.begin(),
                       overrides_.end(),
                       [&other](const property_override& entry)
                       {
                           const auto* match = other.find_override(entry.property);
                           return match && are_equal(entry.value, match->value);
                       });
}

void material_instance::set_shared_material(const asset_handle<material>& shared)
{
    shared_ = shared;
}

auto material_instance::get_shared_material() const -> const asset_handle<material>&
{
    return shared_;
}

auto material_instance::set_override(entt::id_type property, const entt::meta_any& value) -> bool
{
    const auto shared = shared_.get();
    if(!is_loaded(shared))
    {
        return false;
    }
    const auto type = make_material_object(*shared).type();
    const auto data = type.data(property);
    if(!data || data.is_const())
    {
        return false;
    }
    auto converted = entt::copy_meta_any(value);
    if(!converted.allow_cast(data.type()))
    {
        return false;
    }
    material_type_ = type.id();
    if(auto* existing = find_override(property))
    {
        if(are_equal(existing->value, converted))
        {
            return true;
        }
        existing->value = std::move(converted);
    }
    else
    {
        overrides_.push_back({property, std::move(converted)});
    }
    changed_properties_.push_back(property);
    return true;
}

auto material_instance::get_value(entt::id_type property) const -> entt::meta_any
{
    if(const auto* entry = find_override(property))
    {
        return entt::copy_meta_any(entry->value);
    }
    const auto shared = shared_.get();
    return is_loaded(shared) ? get_material_value(*shared, property) : entt::meta_any{};
}

auto material_instance::is_overridden(entt::id_type property) const -> bool
{
    return find_override(property) != nullptr;
}

void material_instance::revert(entt::id_type property)
{
    const auto found = std::find_if(overrides_.begin(),
                                    overrides_.end(),
                                    [property](const property_override& entry)
                                    {
                                        return entry.property == property;
                                    });
    if(found == overrides_.end())
    {
        return;
    }
    overrides_.erase(found);
    changed_properties_.push_back(property);
}

void material_instance::revert_all()
{
    for(const auto& entry : overrides_)
    {
        changed_properties_.push_back(entry.property);
    }
    overrides_.clear();
    legacy_copy_.reset();
    are_graph_overrides_changed_ = are_graph_overrides_changed_ || has_graph_overrides();
    graph_parameters_.clear();
    graph_textures_.clear();
}

auto material_instance::has_overrides() const -> bool
{
    return !overrides_.empty() || legacy_copy_ != nullptr || has_graph_overrides();
}

auto material_instance::set_graph_parameter(const std::string& name, const math::vec4& value) -> bool
{
    const auto shared = get_shared_graph(true);
    if(!shared || shared->find_parameter(name) == nullptr)
    {
        return false;
    }
    material_type_ = make_material_object(*shared).type().id();
    if(set_named(graph_parameters_, name, value))
    {
        note_changed_name(changed_graph_parameters_, name);
    }
    return true;
}

auto material_instance::get_graph_parameter(hpp::string_view name) const -> hpp::optional<math::vec4>
{
    const auto shared = get_shared_graph(true);
    if(!shared || shared->find_parameter(name) == nullptr)
    {
        return {};
    }
    const auto* entry = find_named(graph_parameters_, name);
    return entry ? entry->value : shared->get_parameter(name);
}

auto material_instance::is_graph_parameter_overridden(hpp::string_view name) const -> bool
{
    return find_named(graph_parameters_, name) != nullptr;
}

void material_instance::revert_graph_parameter(hpp::string_view name)
{
    if(erase_named(graph_parameters_, name))
    {
        note_changed_name(changed_graph_parameters_, name);
    }
}

auto material_instance::get_graph_parameter_overrides() const -> const std::vector<graph_parameter_value>&
{
    return graph_parameters_;
}

auto material_instance::set_graph_texture(const std::string& name, const asset_handle<gfx::texture>& texture) -> bool
{
    const auto shared = get_shared_graph(true);
    if(!shared || shared->find_texture(name) == nullptr)
    {
        return false;
    }
    material_type_ = make_material_object(*shared).type().id();
    if(set_named(graph_textures_, name, texture))
    {
        note_changed_name(changed_graph_textures_, name);
    }
    return true;
}

void material_instance::set_known_graph_texture(const std::string& name, const asset_handle<gfx::texture>& texture)
{
    material_type_ = entt::resolve<graph_material>().id();
    if(set_named(graph_textures_, name, texture))
    {
        note_changed_name(changed_graph_textures_, name);
    }
}

auto material_instance::get_graph_texture(hpp::string_view name) const -> asset_handle<gfx::texture>
{
    if(const auto* entry = find_named(graph_textures_, name))
    {
        return entry->texture;
    }
    const auto shared = get_shared_graph(true);
    return shared ? shared->get_texture(name) : asset_handle<gfx::texture>{};
}

auto material_instance::is_graph_texture_overridden(hpp::string_view name) const -> bool
{
    return find_named(graph_textures_, name) != nullptr;
}

void material_instance::revert_graph_texture(hpp::string_view name)
{
    if(erase_named(graph_textures_, name))
    {
        note_changed_name(changed_graph_textures_, name);
    }
}

auto material_instance::get_graph_texture_overrides() const -> const std::vector<graph_texture_value>&
{
    return graph_textures_;
}

auto material_instance::get_overrides() const -> const std::vector<property_override>&
{
    return overrides_;
}

auto material_instance::get_changes_from(const material_instance& previous) const -> render_change::mask
{
    if(!(shared_ == previous.shared_) || legacy_copy_ || previous.legacy_copy_)
    {
        return render_change::material_aspects;
    }
    const auto shared = shared_.try_get();
    const auto type = get_material_type();
    render_change::mask changes = render_change::none;
    for(const auto& entry : overrides_)
    {
        const auto* match = previous.find_override(entry.property);
        if(match == nullptr || !are_equal(match->value, entry.value))
        {
            changes |= get_override_changes(shared, type, entry.property, match ? &match->value : nullptr, &entry.value);
        }
    }
    for(const auto& entry : previous.overrides_)
    {
        if(find_override(entry.property) == nullptr)
        {
            changes |= get_override_changes(shared, type, entry.property, &entry.value, nullptr);
        }
    }
    // What each graph value feeds, by the shared graph; every aspect while it is not loaded.
    const auto* graph = is_loaded(shared) ? shared->safe_cast<graph_material>() : nullptr;
    changes |= get_named_changes(graph_parameters_,
                                 previous.graph_parameters_,
                                 [graph](const std::string& name)
                                 {
                                     const auto* parameter = graph ? graph->find_parameter(name) : nullptr;
                                     return parameter ? parameter->changes : render_change::material_aspects;
                                 });
    changes |= get_named_changes(graph_textures_,
                                 previous.graph_textures_,
                                 [graph](const std::string& name)
                                 {
                                     const auto* texture = graph ? graph->find_texture(name) : nullptr;
                                     return texture ? texture->changes : render_change::material_aspects;
                                 });
    return changes;
}

auto material_instance::collect_overrides() const -> std::vector<property_override>
{
    std::vector<property_override> result;
    result.reserve(overrides_.size());
    for(const auto& entry : overrides_)
    {
        result.push_back({entry.property, entt::copy_meta_any(entry.value)});
    }
    if(legacy_copy_)
    {
        for(auto& entry : diff_legacy_copy(shared_.get()))
        {
            result.push_back(std::move(entry));
        }
    }
    return result;
}

auto material_instance::get_material_type() const -> entt::meta_type
{
    if(material_type_ != entt::id_type{})
    {
        return entt::resolve(material_type_);
    }
    if(legacy_copy_)
    {
        return make_material_object(*legacy_copy_).type();
    }
    return {};
}

auto material_instance::shares_draw() const -> bool
{
    if(legacy_copy_)
    {
        return false;
    }
    if(has_graph_overrides() && !shares_graph_draw())
    {
        return false;
    }
    if(overrides_.empty())
    {
        return true;
    }
    const auto type = get_material_type();
    if(!type)
    {
        return false;
    }
    return std::all_of(overrides_.begin(),
                       overrides_.end(),
                       [&type](const property_override& entry)
                       {
                           const auto data = type.data(entry.property);
                           return data && entt::get_attribute_as<bool>(data, "per_instance");
                       });
}

void material_instance::update_resolved()
{
    if(legacy_copy_ && !convert_legacy_copy(false))
    {
        return;
    }
    if(overrides_.empty() && !has_graph_overrides())
    {
        resolved_.reset();
        resolved_shared_ = nullptr;
        changed_properties_.clear();
        changed_graph_parameters_.clear();
        changed_graph_textures_.clear();
        are_graph_overrides_changed_ = false;
        return;
    }
    // Rendering must not wait for the shared material: until it is loaded, the slot draws with it as it is, and its
    // shadow with the resolved material it draws with.
    const auto shared = shared_.try_get();
    if(!is_loaded(shared))
    {
        resolved_shares_shadow_ = false;
        return;
    }
    const bool is_rebuilt = !resolved_ || resolved_shared_ != shared.get() ||
                            resolved_shared_revision_ != shared->get_revision() || are_graph_overrides_changed_;
    const bool is_synced = !changed_properties_.empty() || !changed_graph_parameters_.empty() || !changed_graph_textures_.empty();
    if(is_rebuilt)
    {
        rebuild_resolved(shared);
    }
    else
    {
        sync_changed_properties(shared);
        sync_changed_graph_values(shared);
    }
    changed_properties_.clear();
    changed_graph_parameters_.clear();
    changed_graph_textures_.clear();
    are_graph_overrides_changed_ = false;
    // What the renderer batches by changes only with the overrides or the shared material.
    if(is_rebuilt || is_synced)
    {
        resolved_shares_draw_ = shares_draw();
        resolved_shares_shadow_ = shares_shadow();
    }
}

auto material_instance::get_resolved() const -> const material::sptr&
{
    return resolved_;
}

auto material_instance::get_resolved_shares_draw() const -> bool
{
    return resolved_shares_draw_;
}

auto material_instance::get_resolved_shares_shadow() const -> bool
{
    return resolved_shares_shadow_;
}

auto material_instance::shares_shadow() const -> bool
{
    // Against no overrides over the same shared material: the aspects every override feeds.
    material_instance plain;
    plain.shared_ = shared_;
    return (get_changes_from(plain) & render_interest::shadow_caster) == 0u;
}

void material_instance::adopt_resolved(material_instance& previous)
{
    if(resolved_ || !previous.resolved_ || previous.shared_ != shared_)
    {
        return;
    }
    resolved_ = std::move(previous.resolved_);
    // Synced with nothing: the next update_resolved() rebuilds it in place from these overrides.
    resolved_shared_ = nullptr;
}

void material_instance::set_legacy_copy(material::sptr copy)
{
    legacy_copy_ = upgrade_legacy_material(std::move(copy));
    if(legacy_copy_)
    {
        material_type_ = make_material_object(*legacy_copy_).type().id();
    }
}

auto material_instance::has_legacy_copy() const -> bool
{
    return legacy_copy_ != nullptr;
}

auto material_instance::convert_legacy_copy(bool wait) -> bool
{
    if(!legacy_copy_)
    {
        return true;
    }
    const auto shared = wait ? shared_.get() : shared_.try_get();
    if(!shared)
    {
        return false;
    }
    for(auto& entry : diff_legacy_copy(shared))
    {
        changed_properties_.push_back(entry.property);
        overrides_.push_back(std::move(entry));
    }
    diff_legacy_graph_copy(shared, graph_parameters_, graph_textures_);
    are_graph_overrides_changed_ = are_graph_overrides_changed_ || has_graph_overrides();
    legacy_copy_.reset();
    return true;
}

void material_instance::collect_graph_overrides(std::vector<graph_parameter_value>& parameters,
                                                std::vector<graph_texture_value>& textures) const
{
    parameters = graph_parameters_;
    textures = graph_textures_;
    if(legacy_copy_)
    {
        diff_legacy_graph_copy(shared_.get(), parameters, textures);
    }
}

void material_instance::diff_legacy_graph_copy(const material::sptr& shared,
                                               std::vector<graph_parameter_value>& parameters,
                                               std::vector<graph_texture_value>& textures) const
{
    const auto* copy = legacy_copy_ ? legacy_copy_->safe_cast<graph_material>() : nullptr;
    const auto* graph = is_loaded(shared) ? shared->safe_cast<graph_material>() : nullptr;
    if(copy == nullptr || graph == nullptr)
    {
        return;
    }
    for(const auto& parameter : copy->get_parameters())
    {
        const auto value = copy->get_parameter(parameter.name);
        const auto own = graph->get_parameter(parameter.name);
        if(value && own && *value != *own && find_named(parameters, parameter.name) == nullptr)
        {
            parameters.push_back({parameter.name, *value});
        }
    }
    for(const auto& texture : copy->get_textures())
    {
        const auto value = copy->get_texture(texture.name);
        if(graph->find_texture(texture.name) != nullptr && value != graph->get_texture(texture.name) &&
           find_named(textures, texture.name) == nullptr)
        {
            textures.push_back({texture.name, value});
        }
    }
}

auto material_instance::find_override(entt::id_type property) -> property_override*
{
    const auto found = std::find_if(overrides_.begin(),
                                    overrides_.end(),
                                    [property](const property_override& entry)
                                    {
                                        return entry.property == property;
                                    });
    return found == overrides_.end() ? nullptr : &*found;
}

auto material_instance::find_override(entt::id_type property) const -> const property_override*
{
    return const_cast<material_instance*>(this)->find_override(property);
}

auto material_instance::get_shared_graph(bool wait) const -> std::shared_ptr<graph_material>
{
    const auto shared = wait ? shared_.get() : shared_.try_get();
    return is_loaded(shared) ? safe_pointer_cast<graph_material>(shared) : nullptr;
}

auto material_instance::has_graph_overrides() const -> bool
{
    return !graph_parameters_.empty() || !graph_textures_.empty();
}

auto material_instance::shares_graph_draw() const -> bool
{
    const auto shared = get_shared_graph(false);
    // A vertex shader computing values reads the material's own values. A masked graph's shadow does too, but the shadow
    // casters are keyed by the material they cast with (model::get_shadow_material), so they clip by the instance's
    // values while the G-buffer draw stays shared.
    if(!shared || !graph_textures_.empty() || shared->computes_vertex_values())
    {
        return false;
    }
    // An override of a parameter the graph no longer has changes nothing it draws.
    return std::all_of(graph_parameters_.begin(),
                       graph_parameters_.end(),
                       [&shared](const graph_parameter_value& entry)
                       {
                           const auto* parameter = shared->find_parameter(entry.name);
                           return parameter == nullptr || parameter->register_index < material::MAX_INSTANCE_CONSTANTS;
                       });
}

void material_instance::apply_graph_overrides()
{
    auto* graph = resolved_ ? resolved_->safe_cast<graph_material>() : nullptr;
    if(graph == nullptr)
    {
        return;
    }
    for(const auto& entry : graph_parameters_)
    {
        graph->set_parameter(entry.name, entry.value);
    }
    for(const auto& entry : graph_textures_)
    {
        graph->set_texture(entry.name, entry.texture);
    }
}

auto material_instance::diff_legacy_copy(const material::sptr& shared) const -> std::vector<property_override>
{
    std::vector<property_override> result;
    const auto legacy_object = make_material_object(*legacy_copy_);
    const bool comparable = is_loaded(shared) && make_material_object(*shared).type() == legacy_object.type();
    const auto shared_object = comparable ? make_material_object(*shared) : entt::meta_any{};
    for_each_material_property(legacy_object.type(),
                               [&](entt::id_type id, const entt::meta_data& data)
                               {
                                   auto value = data.get(legacy_object);
                                   // Without a comparable shared material every value of the copy is the look.
                                   if(comparable && are_equal(value, data.get(shared_object)))
                                   {
                                       return;
                                   }
                                   result.push_back({id, entt::copy_meta_any(value)});
                               });
    return result;
}

void material_instance::rebuild_resolved(const material::sptr& shared)
{
    if(!resolved_ || make_material_object(*resolved_).type() != make_material_object(*shared).type())
    {
        resolved_ = shared->clone();
    }
    else
    {
        // In place: the resolved material keeps its identity, which caches of its look (the GI cards) key on.
        copy_material_values(*shared, *resolved_);
    }
    for(const auto& entry : overrides_)
    {
        set_material_value(*resolved_, entry.property, entry.value);
    }
    apply_graph_overrides();
    resolved_shared_ = shared.get();
    resolved_shared_revision_ = shared->get_revision();
}

void material_instance::sync_changed_graph_values(const material::sptr& shared)
{
    auto* resolved = resolved_->safe_cast<graph_material>();
    const auto* source = shared->safe_cast<graph_material>();
    if(resolved == nullptr || source == nullptr)
    {
        return;
    }
    for(const auto& name : changed_graph_parameters_)
    {
        const auto* entry = find_named(graph_parameters_, name);
        const auto value = entry ? hpp::optional<math::vec4>(entry->value) : source->get_parameter(name);
        if(value)
        {
            resolved->set_parameter(name, *value);
        }
    }
    for(const auto& name : changed_graph_textures_)
    {
        const auto* entry = find_named(graph_textures_, name);
        resolved->set_texture(name, entry ? entry->texture : source->get_texture(name));
    }
}

void material_instance::sync_changed_properties(const material::sptr& shared)
{
    for(const auto property : changed_properties_)
    {
        const auto* entry = find_override(property);
        auto incoming = entry ? entt::copy_meta_any(entry->value) : get_material_value(*shared, property);
        if(incoming && !are_equal(get_material_value(*resolved_, property), incoming))
        {
            set_material_value(*resolved_, property, incoming);
        }
    }
}

} // namespace unravel
