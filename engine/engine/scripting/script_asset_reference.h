#pragma once

#include <engine/assets/asset_handle.h>
#include <engine/assets/asset_manager.h>

#include <dotnetpp/dotnetpp.h>
#include <uuid/uuid.h>

namespace unravel
{

/**
 * @brief The asset a script's asset reference points at. A script holds an asset (Prefab, Texture,
 * AnimationClip, ...) as a managed object carrying the asset's uid; a null reference, or one whose
 * uid names no asset, gives an empty handle.
 */
template<typename T>
auto get_script_asset(asset_manager& assets, const dotnet::object& reference) -> asset_handle<T>
{
    if(!reference.valid())
    {
        return {};
    }
    const auto uid_property = reference.get_type().get_property("uid");
    if(!uid_property.is_valid())
    {
        return {};
    }
    return assets.get_asset<T>(dotnet::make_property_invoker<hpp::uuid>(uid_property).get_value(reference));
}

/**
 * @brief Points a script member holding an asset reference (a field or property, read and written
 * through `member`) at `asset`. A null member gets a new reference when there is an asset to hold;
 * an empty handle clears the uid.
 */
template<typename T, typename Invoker>
void set_script_asset(dotnet::object& owner, const Invoker& member, const asset_handle<T>& asset)
{
    const auto& reference_type = member.get_type();
    auto reference = member.get_value(owner);
    if(!reference && asset)
    {
        reference = reference_type.new_instance();
        member.set_value(owner, reference);
    }
    if(reference)
    {
        dotnet::make_property_invoker<hpp::uuid>(reference_type.get_property("uid")).set_value(reference, asset.uid());
    }
}

} // namespace unravel
