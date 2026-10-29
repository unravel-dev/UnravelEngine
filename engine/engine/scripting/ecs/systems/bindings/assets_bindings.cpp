#include "script_glue_common.h"
#include "script_bindings.h"
#include "material_script_helpers.h"

#include "../script_interop.h"

#include <engine/animation/animation.h>
#include <engine/animation/animation_controller.h>
#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_clip.h>
#include <engine/ecs/prefab.h>
#include <engine/physics/physics_material.h>
#include <engine/rendering/font.h>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/mesh.h>
#include <engine/ui/style_sheet.h>
#include <engine/ui/ui_tree.h>
#include <graphics/texture.h>

#include <unordered_map>

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
        {dotnet::type::get_hash("Unravel.Core.AnimationController"),
         std::make_shared<dotnet_asset_impl<animation_controller>>()},
        {dotnet::type::get_hash("Unravel.Core.AudioClip"),       std::make_shared<dotnet_asset_impl<audio_clip>>()},
        {dotnet::type::get_hash("Unravel.Core.Font"),            std::make_shared<dotnet_asset_impl<font>>()},
        {dotnet::type::get_hash("Unravel.Core.VisualTree"),      std::make_shared<dotnet_asset_impl<ui_tree>>()},
        {dotnet::type::get_hash("Unravel.Core.StyleSheet"),      std::make_shared<dotnet_asset_impl<style_sheet>>()}
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

/// A shared material scripts edited, with its values from before the first edit.
struct shared_material_edit
{
    std::weak_ptr<material> live;
    material::sptr original;
};

auto get_shared_material_edits() -> std::unordered_map<const material*, shared_material_edit>&
{
    static std::unordered_map<const material*, shared_material_edit> edits;
    return edits;
}

auto find_shared_material(const hpp::uuid& uid) -> material::sptr
{
    auto& am = engine::context().get_cached<asset_manager>();
    auto asset = am.get_asset<material>(uid);
    if(!asset)
    {
        return {};
    }
    auto shared = asset.get();
    return shared && !asset_handle<material>::is_empty(shared) ? shared : nullptr;
}

auto get_shared_value(const hpp::uuid& uid, int32_t property) -> entt::meta_any
{
    const auto shared = find_shared_material(uid);
    return shared ? get_material_value(*shared, get_material_property_id(property)) : entt::meta_any{};
}

/// Changes the shared material for every renderer that uses it; restored when play stops.
void set_shared_value(const hpp::uuid& uid, int32_t property, const entt::meta_any& value)
{
    if(const auto shared = find_shared_material(uid))
    {
        remember_shared_material_edit(shared);
        set_material_value(*shared, get_material_property_id(property), value);
    }
}

void internal_m2n_material_set_color(const hpp::uuid& uid, int32_t property, const managed_color& value)
{
    set_shared_value(uid, property, make_color_value(value));
}

auto internal_m2n_material_get_color(const hpp::uuid& uid, int32_t property) -> managed_color
{
    return read_color_value(get_shared_value(uid, property));
}

void internal_m2n_material_set_float(const hpp::uuid& uid, int32_t property, float value)
{
    set_shared_value(uid, property, value);
}

auto internal_m2n_material_get_float(const hpp::uuid& uid, int32_t property) -> float
{
    return read_float_value(get_shared_value(uid, property));
}

void internal_m2n_material_set_vector(const hpp::uuid& uid, int32_t property, const managed_vector2& value)
{
    set_shared_value(uid, property, make_vector_value(value));
}

auto internal_m2n_material_get_vector(const hpp::uuid& uid, int32_t property) -> managed_vector2
{
    return read_vector_value(get_shared_value(uid, property));
}

void internal_m2n_material_set_texture(const hpp::uuid& uid, int32_t property, const hpp::uuid& texture)
{
    set_shared_value(uid, property, make_texture_value(texture));
}

auto internal_m2n_material_get_texture(const hpp::uuid& uid, int32_t property) -> hpp::uuid
{
    return read_texture_value(get_shared_value(uid, property));
}

void internal_m2n_material_set_mode(const hpp::uuid& uid, int32_t property, int32_t value)
{
    set_shared_value(uid, property, make_mode_value(property, value));
}

auto internal_m2n_material_get_mode(const hpp::uuid& uid, int32_t property) -> int32_t
{
    return read_mode_value(get_shared_value(uid, property));
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

auto make_texture_value(const hpp::uuid& uid) -> entt::meta_any
{
    texture_handle texture;
    if(!uid.is_nil())
    {
        texture = engine::context().get_cached<asset_manager>().get_asset<gfx::texture>(uid);
    }
    return texture;
}

void remember_shared_material_edit(const material::sptr& shared)
{
    auto& edits = get_shared_material_edits();
    if(edits.find(shared.get()) == edits.end())
    {
        edits.emplace(shared.get(), shared_material_edit{shared, shared->clone()});
    }
}

void restore_shared_material_edits()
{
    auto& edits = get_shared_material_edits();
    for(const auto& [key, edit] : edits)
    {
        // In place: renderers and material instances keep referencing the same material.
        if(const auto live = edit.live.lock())
        {
            copy_material_values(*edit.original, *live);
        }
    }
    edits.clear();
}

void register_assets_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Assets");
        reg.add_internal_call("internal_m2n_get_asset_by_uuid", script_internal_call(internal_m2n_get_asset_by_uuid));
        reg.add_internal_call("internal_m2n_get_asset_by_key", script_internal_call(internal_m2n_get_asset_by_key));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Material");
        reg.add_internal_call("internal_m2n_material_set_color", script_internal_call(internal_m2n_material_set_color));
        reg.add_internal_call("internal_m2n_material_get_color", script_internal_call(internal_m2n_material_get_color));
        reg.add_internal_call("internal_m2n_material_set_float", script_internal_call(internal_m2n_material_set_float));
        reg.add_internal_call("internal_m2n_material_get_float", script_internal_call(internal_m2n_material_get_float));
        reg.add_internal_call("internal_m2n_material_set_vector", script_internal_call(internal_m2n_material_set_vector));
        reg.add_internal_call("internal_m2n_material_get_vector", script_internal_call(internal_m2n_material_get_vector));
        reg.add_internal_call("internal_m2n_material_set_texture",
                              script_internal_call(internal_m2n_material_set_texture));
        reg.add_internal_call("internal_m2n_material_get_texture",
                              script_internal_call(internal_m2n_material_get_texture));
        reg.add_internal_call("internal_m2n_material_set_mode", script_internal_call(internal_m2n_material_set_mode));
        reg.add_internal_call("internal_m2n_material_get_mode", script_internal_call(internal_m2n_material_get_mode));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.AudioClip");
        reg.add_internal_call("internal_m2n_audio_clip_get_length", script_internal_call(internal_m2n_audio_clip_get_length));

    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.AnimationClip");
        reg.add_internal_call("internal_m2n_animation_clip_get_length", script_internal_call(internal_m2n_animation_clip_get_length));
        reg.add_internal_call("internal_m2n_animation_clip_get_name", script_internal_call(internal_m2n_animation_clip_get_name));
    }
}

} // namespace unravel
