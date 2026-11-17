#include "post_process_material_component.h"

#include <algorithm>
#include <utility>

namespace unravel
{

auto post_process_material_entry::get_material() const -> const asset_handle<material>&
{
    return material_.get_shared_material();
}

void post_process_material_entry::set_material(const asset_handle<material>& material)
{
    if(material_.get_shared_material() == material)
    {
        return;
    }
    material_.set_shared_material(material);
    material_.update_resolved();
}

auto post_process_material_entry::get_material_instance() const -> const material_instance&
{
    return material_;
}

auto post_process_material_entry::get_material_instance() -> material_instance&
{
    return material_;
}

void post_process_material_entry::set_material_instance(const material_instance& instance)
{
    const auto shared = material_.get_shared_material();
    material_ = instance;
    material_.set_shared_material(shared);
    material_.update_resolved();
}

auto post_process_material_entry::get_resolved_material() const -> material::sptr
{
    if(const auto& resolved = material_.get_resolved())
    {
        return resolved;
    }
    return material_.get_shared_material().try_get();
}

auto post_process_material_component::find(const asset_handle<material>& material) -> post_process_material_entry*
{
    return const_cast<post_process_material_entry*>(std::as_const(*this).find(material));
}

auto post_process_material_component::find(const asset_handle<material>& material) const -> const post_process_material_entry*
{
    const auto it = std::find_if(entries.begin(),
                                 entries.end(),
                                 [&](const post_process_material_entry& entry)
                                 {
                                     return entry.get_material() == material;
                                 });
    return it == entries.end() ? nullptr : &*it;
}

auto post_process_material_component::add(const asset_handle<material>& material, float weight) -> post_process_material_entry&
{
    if(auto* existing = find(material))
    {
        return *existing;
    }
    auto& entry = entries.emplace_back();
    entry.set_material(material);
    entry.weight = weight;
    return entry;
}

auto post_process_material_component::remove(const asset_handle<material>& material) -> bool
{
    const auto it = std::find_if(entries.begin(),
                                 entries.end(),
                                 [&](const post_process_material_entry& entry)
                                 {
                                     return entry.get_material() == material;
                                 });
    if(it == entries.end())
    {
        return false;
    }
    entries.erase(it);
    return true;
}

} // namespace unravel
