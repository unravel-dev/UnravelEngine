#include "thumbnail_manager.h"

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_clip.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/physics/physics_material.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/systems/rendering_system.h>

#include <engine/meta/ecs/components/all_components.h>

#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/scripting/script.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

#include <filesystem/filesystem.h>
#include <filesystem/watcher.h>

namespace unravel
{

namespace
{

template<typename T>
auto make_thumbnail(thumbnail_manager::generator& gen, const asset_handle<T>& asset) -> gfx::texture::ptr
{
    auto& thumbnail = gen.thumbnails[asset.uid()];
    auto current_fbo = thumbnail.get();

    if(gen.remaining > 0 && thumbnail.needs_regeneration)
    {
        try
        {
            auto& scn = gen.get_scene();
            scn.unload();
            auto& ctx = engine::context();
            defaults::create_default_3d_scene_for_asset_preview(ctx, scn, asset, {256, 256});

            delta_t dt(0.016667f);

            auto& rpath = ctx.get_cached<rendering_system>();
            rpath.on_frame_update(scn, dt);
            rpath.on_frame_before_render(scn, dt);
            auto new_fbo = rpath.render_scene(scn, dt);
            thumbnail.set(new_fbo);
        }
        catch(const std::exception& e)
        {
            APPLOG_ERROR("{}", e.what());
        }
    }

    return current_fbo;
}

template<typename T>
auto get_thumbnail_impl(thumbnail_manager::generator& gen,
                        const asset_handle<T>& asset,
                        const asset_handle<gfx::texture>& transparent,
                        const asset_handle<gfx::texture>& loading) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return transparent.get();
    }

    if(!asset.is_ready())
    {
        return loading.get();
    }

    return make_thumbnail(gen, asset);
}

} // namespace


template<>
auto thumbnail_manager::get_thumbnail<mesh>(const asset_handle<mesh>& asset) -> gfx::texture::ptr
{
    auto thumbnail = get_thumbnail_impl(gen_, asset, thumbnails_.transparent, thumbnails_.loading);

    if(thumbnail)
    {
        return thumbnail;
    }

    return thumbnails_.mesh.get();
}

template<>
auto thumbnail_manager::get_thumbnail<material>(const asset_handle<material>& asset) -> gfx::texture::ptr
{
    auto thumbnail = get_thumbnail_impl(gen_, asset, thumbnails_.transparent, thumbnails_.loading);

    if(thumbnail)
    {
        return thumbnail;
    }

    return thumbnails_.material.get();
}

template<>
auto thumbnail_manager::get_thumbnail<script>(const asset_handle<script>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }
    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.script.get();
}

template<>
auto thumbnail_manager::get_thumbnail<physics_material>(const asset_handle<physics_material>& asset)
    -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }
    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.physics_material.get();
}

template<>
auto thumbnail_manager::get_thumbnail<audio_clip>(const asset_handle<audio_clip>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }
    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.audio_clip.get();
}

template<>
auto thumbnail_manager::get_thumbnail<font>(const asset_handle<font>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }
    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.font.get();
}

template<>
auto thumbnail_manager::get_thumbnail<animation_clip>(const asset_handle<animation_clip>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }
    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.animation.get();
}

template<>
auto thumbnail_manager::get_thumbnail<gfx::texture>(const asset_handle<gfx::texture>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }

    return !asset.is_ready() ? thumbnails_.loading.get() : asset.get();
}

template<>
auto thumbnail_manager::get_thumbnail<gfx::shader>(const asset_handle<gfx::shader>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }
    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.shader.get();
}

template<>
auto thumbnail_manager::get_thumbnail<prefab>(const asset_handle<prefab>& asset) -> gfx::texture::ptr
{
    auto thumbnail = get_thumbnail_impl(gen_, asset, thumbnails_.transparent, thumbnails_.loading);

    if(thumbnail)
    {
        return thumbnail;
    }

    return thumbnails_.prefab.get();
}

template<>
auto thumbnail_manager::get_thumbnail<scene_prefab>(const asset_handle<scene_prefab>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }
    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.scene_prefab.get();
}

auto thumbnail_manager::get_thumbnail(const fs::path& path) -> gfx::texture::ptr
{
    fs::error_code ec;
    if(fs::is_directory(path, ec))
    {
        return thumbnails_.folder.get();
    }

    return thumbnails_.file.get();
}

void thumbnail_manager::regenerate_thumbnail(const hpp::uuid& uid)
{
    gen_.thumbnails[uid].needs_regeneration = true;
}
void thumbnail_manager::remove_thumbnail(const hpp::uuid& uid)
{
    gen_.thumbnails.erase(uid);
}

void thumbnail_manager::clear_thumbnails()
{
    gen_.thumbnails.clear();
}

auto thumbnail_manager::get_gizmo_icon(entt::handle e) -> gfx::texture::ptr
{
    asset_handle<gfx::texture> icon;

    if(e.all_of<camera_component>())
    {
        icon = gimzmo_icons_.camera;
    }

    if(e.all_of<light_component>())
    {
        const auto& light_comp = e.get<light_component>();
        const auto& light = light_comp.get_light();

        auto type = [&]() -> asset_handle<gfx::texture>
        {
            switch(light.type)
            {
                case light_type::directional:
                {
                    if(e.all_of<skylight_component>())
                        return gimzmo_icons_.sky_light;

                    return gimzmo_icons_.directional_light;
                }
                case light_type::point:
                    return gimzmo_icons_.point_light;
                case light_type::spot:
                    return gimzmo_icons_.spot_light;
                default:
                    return gimzmo_icons_.sky_light;
            }
        }();

        icon = type;
    }

    if(e.all_of<reflection_probe_component>())
    {
        icon = gimzmo_icons_.reflection_probe;
    }

    if(e.all_of<audio_source_component>())
    {
        icon = gimzmo_icons_.audio_source;
    }

    return icon.get();
}


auto thumbnail_manager::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ev = ctx.get_cached<events>();
    ev.on_frame_update.connect(sentinel_, this, &thumbnail_manager::on_frame_update);

    auto& am = ctx.get_cached<asset_manager>();
    thumbnails_.transparent = am.get_asset<gfx::texture>("engine:/data/textures/transparent.png");

    thumbnails_.file = am.get_asset<gfx::texture>("editor:/data/icons/file.png");
    thumbnails_.folder = am.get_asset<gfx::texture>("editor:/data/icons/folder.png");
    thumbnails_.folder_empty = am.get_asset<gfx::texture>("editor:/data/icons/folder_empty.png");
    thumbnails_.loading = am.get_asset<gfx::texture>("editor:/data/icons/loading.png");
    thumbnails_.font = am.get_asset<gfx::texture>("editor:/data/icons/font.png");
    thumbnails_.shader = am.get_asset<gfx::texture>("editor:/data/icons/shader.png");
    thumbnails_.material = am.get_asset<gfx::texture>("editor:/data/icons/material.png");
    thumbnails_.physics_material = am.get_asset<gfx::texture>("editor:/data/icons/material.png");
    thumbnails_.mesh = am.get_asset<gfx::texture>("editor:/data/icons/mesh.png");
    thumbnails_.animation = am.get_asset<gfx::texture>("editor:/data/icons/animation.png");
    thumbnails_.prefab = am.get_asset<gfx::texture>("editor:/data/icons/prefab.png");
    thumbnails_.scene_prefab = am.get_asset<gfx::texture>("editor:/data/icons/scene.png");
    thumbnails_.audio_clip = am.get_asset<gfx::texture>("editor:/data/icons/sound.png");
    thumbnails_.script = am.get_asset<gfx::texture>("editor:/data/icons/script.png");


    gimzmo_icons_.camera = am.get_asset<gfx::texture>("editor:/data/icons/camera.png");
    gimzmo_icons_.sky_light = am.get_asset<gfx::texture>("editor:/data/icons/sky_light.png");
    gimzmo_icons_.directional_light = am.get_asset<gfx::texture>("editor:/data/icons/directional_light.png");
    gimzmo_icons_.point_light = am.get_asset<gfx::texture>("editor:/data/icons/point_light.png");
    gimzmo_icons_.spot_light = am.get_asset<gfx::texture>("editor:/data/icons/spot_light.png");
    gimzmo_icons_.audio_source = am.get_asset<gfx::texture>("editor:/data/icons/audio_source.png");
    gimzmo_icons_.reflection_probe = am.get_asset<gfx::texture>("editor:/data/icons/reflection_probe.png");
    return true;
}

auto thumbnail_manager::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void thumbnail_manager::on_frame_update(rtti::context& ctx, delta_t)
{
    gen_.reset();
}

auto thumbnail_manager::generated_thumbnail::get() -> gfx::texture::ptr
{
    if(!thumbnail)
    {
        return nullptr;
    }

    return thumbnail->get_texture();
}

void thumbnail_manager::generated_thumbnail::set(gfx::frame_buffer::ptr fbo)
{
    thumbnail = fbo;
    needs_regeneration = false;
}

auto thumbnail_manager::generator::get_scene() -> scene&
{
    reset_wait();
    remaining--;
    return scenes[remaining];
}

void thumbnail_manager::generator::reset()
{
    if(wait_frames-- <= 0)
    {
        for(auto& scn : scenes)
        {
            scn.unload();
        }
        remaining = scenes.size();

        reset_wait();
    }
}

void thumbnail_manager::generator::reset_wait()
{
    wait_frames = 1;
}

} // namespace unravel
