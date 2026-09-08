#pragma once

#include <engine/assets/asset_handle.h>
#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/meta/core/common/basetypes.hpp>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

namespace ser20
{

template<typename Archive, typename T>
inline void SAVE_FUNCTION_NAME(Archive& ar, asset_handle<T> const& obj)
{
    try_save(ar, ser20::make_nvp("uid", obj.uid()));
}

template<typename Archive, typename T>
inline void LOAD_FUNCTION_NAME(Archive& ar, asset_handle<T>& obj)
{
    hpp::uuid uid{};
    try_load(ar, ser20::make_nvp("uid", uid));

    // try_get_asset rather than get_asset: a handle can be read before asset_manager::init
    // has registered the storages - cold boot peeks project settings before any system is
    // initialized - and an unresolvable handle has to come back empty, not take the load down.
    auto& am = unravel::engine::context().get_cached<unravel::asset_manager>();
    obj = am.try_get_asset<T>(uid, unravel::load_flags::standard, unravel::load_mode::deferred);
}
} // namespace ser20
