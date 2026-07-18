#include "script_glue_common.h"
#include "script_bindings.h"
#include "material_script_helpers.h"

#include "../script_interop.h"

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_clip.h>
#include <engine/ecs/prefab.h>
#include <engine/physics/physics_material.h>
#include <engine/rendering/font.h>
#include <engine/rendering/mesh.h>
#include <graphics/texture.h>

namespace unravel
{
namespace
{

struct dotnet_asset
{
    virtual auto get_asset_uuid(const hpp::uuid& uid) const -> hpp::uuid = 0;
    virtual auto get_asset_uuid(const std::string& key) const -> hpp::uuid = 0;
};

template<typename T>
struct dotnet_asset_impl : dotnet_asset
{
    auto get_asset_uuid(const hpp::uuid& uid) const -> hpp::uuid override
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<T>(uid);
        return asset.uid();
    }

    auto get_asset_uuid(const std::string& key) const -> hpp::uuid override
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<T>(key);
        return asset.uid();
    }
};

auto get_dotnet_asset(size_t type_hash) -> const dotnet_asset*
{
    // clang-format off
    static std::map<size_t, std::shared_ptr<dotnet_asset>> reg =
    {
        {dotnet::type::get_hash("Unravel.Core.Texture"),         std::make_shared<dotnet_asset_impl<gfx::texture>>()},
        {dotnet::type::get_hash("Unravel.Core.Material"),        std::make_shared<dotnet_asset_impl<material>>()},
        {dotnet::type::get_hash("Unravel.Core.Mesh"),            std::make_shared<dotnet_asset_impl<mesh>>()},
        {dotnet::type::get_hash("Unravel.Core.AnimationClip"),   std::make_shared<dotnet_asset_impl<animation_clip>>()},
        {dotnet::type::get_hash("Unravel.Core.Prefab"),          std::make_shared<dotnet_asset_impl<prefab>>()},
        {dotnet::type::get_hash("Unravel.Core.Scene"),           std::make_shared<dotnet_asset_impl<scene_prefab>>()},
        {dotnet::type::get_hash("Unravel.Core.PhysicsMaterial"), std::make_shared<dotnet_asset_impl<physics_material>>()},
        {dotnet::type::get_hash("Unravel.Core.AudioClip"),       std::make_shared<dotnet_asset_impl<audio_clip>>()},
        {dotnet::type::get_hash("Unravel.Core.Font"),            std::make_shared<dotnet_asset_impl<font>>()}
    };
    // clang-format on

    auto it = reg.find(type_hash);
    if(it != reg.end())
    {
        return it->second.get();
    }
    static const dotnet_asset* empty{};
    return empty;
};

auto internal_m2n_get_asset_by_uuid(const hpp::uuid& uid, const dotnet::type& type) -> hpp::uuid
{
    if(auto asset = get_dotnet_asset(type.get_hash()))
    {
        return asset->get_asset_uuid(uid);
    }

    return {};
}

auto internal_m2n_get_asset_by_key(const std::string& key, const dotnet::type& type) -> hpp::uuid
{
    if(auto asset = get_dotnet_asset(type.get_hash()))
    {
        return asset->get_asset_uuid(key);
    }

    return {};
}

auto internal_m2n_get_material_properties(const hpp::uuid& uid) -> dotnetpp_backend::managed_interface::material_properties
{
    using converter = dotnet::managed_interface::converter;

    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    dotnetpp_backend::managed_interface::material_properties props;
    auto asset = am.get_asset<material>(uid);
    if(!asset)
    {
        return props;
    }
    auto material = asset.get();

    return get_material_properties(material);
}

auto internal_m2n_audio_clip_get_length(const hpp::uuid& uid) -> float
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    auto asset = am.get_asset<audio_clip>(uid);

    if(asset.is_valid())
    {
        if(auto clip = asset.get())
        {
            float secs = clip->get_info().duration.count();
            return secs;
        }
    }

    return 0.0f;
}

auto internal_m2n_animation_clip_get_length(const hpp::uuid& uid) -> float
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    auto asset = am.get_asset<animation_clip>(uid);

    if(asset.is_valid())
    {
        if(auto clip = asset.get())
        {
            return clip->duration.count();
        }
    }

    return 0.0f;
}

auto internal_m2n_animation_clip_get_name(const hpp::uuid& uid) -> std::string
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    auto asset = am.get_asset<animation_clip>(uid);

    if(asset.is_valid())
    {
        if(auto clip = asset.get())
        {
            return clip->name;
        }
    }

    return {};
}

} // namespace

void register_assets_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Assets");
        reg.add_internal_call("internal_m2n_get_asset_by_uuid", dotnet_internal_call(internal_m2n_get_asset_by_uuid));
        reg.add_internal_call("internal_m2n_get_asset_by_key", dotnet_internal_call(internal_m2n_get_asset_by_key));
        reg.add_internal_call("internal_m2n_get_material_properties",
                              dotnet_internal_call(internal_m2n_get_material_properties));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.AudioClip");
        reg.add_internal_call("internal_m2n_audio_clip_get_length", dotnet_internal_call(internal_m2n_audio_clip_get_length));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.AnimationClip");
        reg.add_internal_call("internal_m2n_animation_clip_get_length", dotnet_internal_call(internal_m2n_animation_clip_get_length));
        reg.add_internal_call("internal_m2n_animation_clip_get_name", dotnet_internal_call(internal_m2n_animation_clip_get_name));
    }
}

} // namespace unravel
