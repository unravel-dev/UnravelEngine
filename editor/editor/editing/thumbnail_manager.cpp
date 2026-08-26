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

#include <engine/ui/style_sheet.h>
#include <engine/meta/ecs/components/all_components.h>

#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/scripting/script.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <seq/seq.h>

#include <filesystem/filesystem.h>
#include <filesystem/watcher.h>

#include <algorithm>

namespace unravel
{

namespace
{

const usize32_t k_thumbnail_size{256, 256};

auto capture_thumbnail_snapshot(const gfx::frame_buffer::ptr& source) -> gfx::texture::ptr
{
    if(!source)
    {
        return nullptr;
    }

    const auto& src_tex = source->get_texture(0);
    if(!src_tex || !src_tex->is_valid())
    {
        return nullptr;
    }

    const auto src_size = source->get_size();

    // The preview camera is built at k_thumbnail_size, so anything else means the capture came
    // correct image in the corner of an otherwise empty thumbnail.
    // from a target that is not the one the preview set up - which is what produces a small
    if(src_size.width != k_thumbnail_size.width || src_size.height != k_thumbnail_size.height)
    {
        APPLOG_WARNING("Thumbnail captured at {}x{}, expected {}x{}.",
                       src_size.width,
                       src_size.height,
                       k_thumbnail_size.width,
                       k_thumbnail_size.height);
    }

    const auto blit_width = static_cast<uint16_t>(std::min(src_size.width, k_thumbnail_size.width));
    const auto blit_height = static_cast<uint16_t>(std::min(src_size.height, k_thumbnail_size.height));
    if(blit_width == 0 || blit_height == 0)
    {
        return nullptr;
    }

    // Sized to what is actually copied, not to a fixed 256. A source smaller than that used
    // to be blitted into the corner of a 256 texture and the rest left as it came - which is
    // the black thumbnail with a small correct image in the top-left.
    auto snapshot = std::make_shared<gfx::texture>(blit_width,
                                                 blit_height,
                                                 false,
                                                 1,
                                                 gfx::texture_format::RGBA8,
                                                 BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT| BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

    if(!snapshot->is_valid())
    {
        return nullptr;
    }

    gfx::render_pass pass("Thumbnail/Capture Blit");
    gfx::blit(pass.id,
              snapshot->native_handle(),
              0,
              0,
              src_tex->native_handle(),
              0,
              0,
              blit_width,
              blit_height);
    return snapshot;
}

/// Rendered through the camera the preview was built with, not through whichever camera the
/// scene happens to contain last. An asset that carries a camera of its own - a prefab with one
/// in it, or one nested inside it - otherwise decided the capture, at whatever viewport size it
/// was authored with.
auto render_thumbnail_preview_scene(rendering_system& rpath,
                                    scene& scn,
                                    entt::handle camera_entity,
                                    delta_t dt,
                                    int frames) -> gfx::frame_buffer::ptr
{
    if(!camera_entity)
    {
        return {};
    }

    auto* camera_comp = camera_entity.try_get<camera_component>();
    if(camera_comp == nullptr)
    {
        return {};
    }

    gfx::frame_buffer::ptr captured;
    for(int i = 0; i < frames; ++i)
    {
        rpath.on_frame_before_render(scn, dt);
        auto new_fbo = rpath.render_scene(camera_entity, *camera_comp, scn, dt, false);
        if(new_fbo)
        {
            captured = std::move(new_fbo);
        }
    }
    return captured;
}


template<typename T>
auto make_thumbnail(thumbnail_manager::generator& gen, const asset_handle<T>& asset, int frames = 2, delta_t dt = delta_t(0.016667f)) -> gfx::texture::ptr
{
    auto& thumbnail = gen.thumbnails[asset.uid()];
    auto current_fbo = thumbnail.get();

    if(gen.remaining > 0 && thumbnail.needs_regeneration && asset.is_ready())
    {
        try
        {
            auto& ctx = engine::context();
            auto& rpath = ctx.get_cached<rendering_system>();

            auto& scn = gen.get_scene();
            rpath.release_pipeline_resources(scn);
            scn.unload();
            auto result = defaults::create_default_3d_scene_for_asset_preview(ctx, scn, asset, k_thumbnail_size, false);

            for(int i = 0; i < frames; i++)
            {
                rpath.on_frame_update(scn, dt);
                rpath.on_frame_before_render(scn, dt);
            }

            defaults::focus_camera_on_3d_scene_for_asset_preview<T>(ctx, result);

            auto captured = render_thumbnail_preview_scene(rpath, scn, result.camera, dt, frames);
            if(!captured)
            {
                APPLOG_WARNING("Thumbnail for {} produced no output (camera valid: {}, object valid: {}).",
                               asset.id(),
                               static_cast<bool>(result.camera),
                               static_cast<bool>(result.object));
            }

            if(captured)
            {
                if(auto snapshot = capture_thumbnail_snapshot(captured))
                {
                    thumbnail.pending_snapshot = std::move(snapshot);
                    gen.remaining--;
                }
            }

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
                        const asset_handle<gfx::texture>& loading,
                        int frames = 2, delta_t dt = delta_t(0.016667f)) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return transparent.get();
    }

    asset.get(false);

    if(!asset.is_ready())
    {
        return loading.get();
    }

    return make_thumbnail(gen, asset, frames, dt);
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
auto thumbnail_manager::get_thumbnail<ui_tree>(const asset_handle<ui_tree>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }

    asset.get(false);

    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.ui_tree.get();
}

template<>
auto thumbnail_manager::get_thumbnail<style_sheet>(const asset_handle<style_sheet>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }

    asset.get(false);

    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.style_sheet.get();
}

template<>
auto thumbnail_manager::get_thumbnail<script>(const asset_handle<script>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }

    asset.get(false);

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

    asset.get(false);

    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.physics_material.get();
}

template<>
auto thumbnail_manager::get_thumbnail<audio_clip>(const asset_handle<audio_clip>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }

    asset.get(false);

    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.audio_clip.get();
}

template<>
auto thumbnail_manager::get_thumbnail<font>(const asset_handle<font>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }

    asset.get(false);

    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.font.get();
}

template<>
auto thumbnail_manager::get_thumbnail<animation_clip>(const asset_handle<animation_clip>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }

    asset.get(false);

    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.animation.get();
}

template<>
auto thumbnail_manager::get_thumbnail<gfx::texture>(const asset_handle<gfx::texture>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }

    asset.get(false);

    return !asset.is_ready() ? thumbnails_.loading.get() : asset.get();
}

template<>
auto thumbnail_manager::get_thumbnail<gfx::shader>(const asset_handle<gfx::shader>& asset) -> gfx::texture::ptr
{
    if(!asset.is_valid())
    {
        return thumbnails_.transparent.get();
    }

    asset.get(false);

    return !asset.is_ready() ? thumbnails_.loading.get() : thumbnails_.shader.get();
}

template<>
auto thumbnail_manager::get_thumbnail<prefab>(const asset_handle<prefab>& asset) -> gfx::texture::ptr
{
    delta_t dt(0.016667f * 5.0f);
    int frames = 2;
    auto thumbnail = get_thumbnail_impl(gen_, asset, thumbnails_.transparent, thumbnails_.loading, frames, dt);

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

    asset.get(false);

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
    auto& entry = gen_.thumbnails[uid];
    entry.needs_regeneration = true;
    entry.pending_snapshot = nullptr;
}
void thumbnail_manager::remove_thumbnail(const hpp::uuid& uid)
{
    gen_.thumbnails.erase(uid);
}

void thumbnail_manager::clear_thumbnails()
{
    auto& ctx = engine::context();
    auto& rpath = ctx.get_cached<rendering_system>();

    seq::scope::stop_all("camera_focus");

    for(auto& scn : gen_.scenes)
    {
        rpath.release_pipeline_resources(scn);
        scn.unload();
    }

    gen_.thumbnails.clear();
    gen_.remaining = static_cast<int>(gen_.scenes.size());
    gen_.wait_frames = 0;
    last_eviction_scan_ = clock::now();
}

void thumbnail_manager::set_cache_directory(const fs::path& cache_dir)
{
    cache_directory_ = cache_dir;
    fs::error_code ec;
    fs::create_directories(cache_directory_, ec);
}

auto thumbnail_manager::get_cache_path(const hpp::uuid& uid) const -> fs::path
{
    if(cache_directory_.empty())
    {
        return {};
    }
    return cache_directory_ / (hpp::to_string(uid) + ".thumb");
}

auto thumbnail_manager::load_cached_thumbnail(const hpp::uuid& uid) -> gfx::texture::ptr
{
    (void)uid;
    // Stub: disk persistence not yet implemented.
    return nullptr;
}

void thumbnail_manager::save_thumbnail_to_cache(const hpp::uuid& uid, gfx::texture::ptr tex)
{
    (void)uid;
    (void)tex;
    // Stub: disk persistence not yet implemented.
}

void thumbnail_manager::invalidate_cache_entry(const hpp::uuid& uid)
{
    auto path = get_cache_path(uid);
    if(!path.empty())
    {
        fs::error_code ec;
        fs::remove(path, ec);
    }
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

    if(e.all_of<particle_emitter_component>())
    {
        icon = gimzmo_icons_.particle_emitter;
    }

    return icon.get();
}


auto thumbnail_manager::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ev = ctx.get_cached<events>();
    ev.on_frame_update.connect(sentinel_, this, &thumbnail_manager::on_frame_update);
    ev.on_frame_end.connect(sentinel_, -10000, this, &thumbnail_manager::on_frame_end);

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
    thumbnails_.ui_tree = am.get_asset<gfx::texture>("editor:/data/icons/rhtml.png");
    thumbnails_.style_sheet = am.get_asset<gfx::texture>("editor:/data/icons/rcss.png");

    gimzmo_icons_.camera = am.get_asset<gfx::texture>("editor:/data/icons/camera.png");
    gimzmo_icons_.sky_light = am.get_asset<gfx::texture>("editor:/data/icons/sky_light.png");
    gimzmo_icons_.directional_light = am.get_asset<gfx::texture>("editor:/data/icons/directional_light.png");
    gimzmo_icons_.point_light = am.get_asset<gfx::texture>("editor:/data/icons/point_light.png");
    gimzmo_icons_.spot_light = am.get_asset<gfx::texture>("editor:/data/icons/spot_light.png");
    gimzmo_icons_.audio_source = am.get_asset<gfx::texture>("editor:/data/icons/audio_source.png");
    gimzmo_icons_.reflection_probe = am.get_asset<gfx::texture>("editor:/data/icons/reflection_probe.png");
    gimzmo_icons_.particle_emitter = am.get_asset<gfx::texture>("editor:/data/icons/particle_emitter.png");

    gen_.remaining = static_cast<int>(gen_.scenes.size());
    gen_.reset_wait();
    return true;
}

auto thumbnail_manager::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void thumbnail_manager::on_frame_update(rtti::context& ctx, delta_t)
{
    auto& rpath = ctx.get_cached<rendering_system>();
    gen_.reset(rpath);
    evict_unused_thumbnails(std::chrono::seconds(30), std::chrono::seconds(60));
}

void thumbnail_manager::on_frame_end(rtti::context& /*ctx*/, delta_t)
{
    commit_pending_snapshots();
}

void thumbnail_manager::commit_pending_snapshots()
{
    for(auto& [uid, entry] : gen_.thumbnails)
    {
        (void)uid;
        if(!entry.pending_snapshot)
        {
            continue;
        }

        entry.set(std::move(entry.pending_snapshot));
    }
}

void thumbnail_manager::evict_unused_thumbnails(std::chrono::seconds scan_interval, std::chrono::seconds idle_timeout)
{
    
    auto now = clock::now();
    if((now - last_eviction_scan_) < scan_interval)
    {
        return;
    }
    last_eviction_scan_ = now;

    auto& thumbnails = gen_.thumbnails;
    for(auto it = thumbnails.begin(); it != thumbnails.end();)
    {
        auto& entry = it->second;
        bool is_idle = (now - entry.last_access_time) > idle_timeout;
        if(is_idle && !entry.needs_regeneration)
        {
            it = thumbnails.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

auto thumbnail_manager::generated_thumbnail::get() -> gfx::texture::ptr
{
    if(!snapshot || !snapshot->is_valid())
    {
        if(snapshot)
        {
            snapshot = nullptr;
            needs_regeneration = true;
        }
        return nullptr;
    }

    last_access_time = clock::now();
    return snapshot;
}

void thumbnail_manager::generated_thumbnail::set(gfx::texture::ptr tex)
{
    if(!tex || !tex->is_valid())
    {
        return;
    }

    snapshot = std::move(tex);
    needs_regeneration = false;
    last_access_time = clock::now();
}

auto thumbnail_manager::generator::get_scene() -> scene&
{
    reset_wait();

    // A different scene per generation in this cycle. make_thumbnail releases a scene's
    // pipeline resources and unloads it before building in it, so handing the same one to
    // three generations in a frame tears down the render view that the first two captures
    // were taken from, while their blits are still queued against it. Three scenes and a
    // budget of three exist exactly so that does not have to happen.
    const auto count = static_cast<int>(scenes.size());
    const auto used = count - std::clamp(remaining, 0, count);
    return scenes[static_cast<size_t>(std::clamp(used, 0, count - 1))];
}

void thumbnail_manager::generator::reset(rendering_system& rpath)
{
    if(wait_frames-- <= 0)
    {
        for(auto& scn : scenes)
        {
            rpath.release_pipeline_resources(scn);
            scn.unload();
        }
        remaining = static_cast<int>(scenes.size());

        reset_wait();
    }
}

void thumbnail_manager::generator::reset_wait()
{
    wait_frames = 2;
}

} // namespace unravel
