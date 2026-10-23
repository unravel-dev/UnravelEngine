#include "material_instance.h"

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
}

material_instance::material_instance(const material_instance& other)
    : shared_(other.shared_)
    , material_type_(other.material_type_)
    , legacy_copy_(other.legacy_copy_)
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
       legacy_copy_ != other.legacy_copy_)
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
    if(!data)
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
}

auto material_instance::has_overrides() const -> bool
{
    return !overrides_.empty() || legacy_copy_ != nullptr;
}

auto material_instance::get_overrides() const -> const std::vector<property_override>&
{
    return overrides_;
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
    if(overrides_.empty())
    {
        resolved_.reset();
        resolved_shared_ = nullptr;
        changed_properties_.clear();
        return;
    }
    // Rendering must not wait for the shared material: until it is loaded, the slot draws with it as it is.
    const auto shared = shared_.try_get();
    if(!is_loaded(shared))
    {
        return;
    }
    if(!resolved_ || resolved_shared_ != shared.get() || resolved_shared_revision_ != shared->get_revision())
    {
        rebuild_resolved(shared);
    }
    else if(!changed_properties_.empty())
    {
        sync_changed_properties(shared);
    }
    changed_properties_.clear();
    resolved_shares_draw_ = shares_draw();
}

auto material_instance::get_resolved() const -> const material::sptr&
{
    return resolved_;
}

auto material_instance::get_resolved_shares_draw() const -> bool
{
    return resolved_shares_draw_;
}

void material_instance::set_legacy_copy(material::sptr copy)
{
    legacy_copy_ = std::move(copy);
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
    legacy_copy_.reset();
    return true;
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
    resolved_shared_ = shared.get();
    resolved_shared_revision_ = shared->get_revision();
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
