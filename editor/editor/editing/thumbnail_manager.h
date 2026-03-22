#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <graphics/frame_buffer.h>
#include <graphics/shader.h>
#include <graphics/texture.h>

#include <engine/assets/asset_handle.h>
#include <engine/ecs/scene.h>


namespace gfx
{
struct texture;
struct shader;
} // namespace gfx

namespace unravel
{
class mesh;
class material;
struct prefab;
struct scene_prefab;
struct animation_clip;
struct physics_material;
struct audio_clip;
struct script;
struct font;
struct ui_tree;
struct style_sheet;
} // namespace unravel


namespace unravel
{

    
struct thumbnail_manager
{
    struct generated_thumbnail
    {
        auto get() -> gfx::texture::ptr;
        void set(gfx::frame_buffer::ptr fbo);

        bool needs_regeneration{true};
        gfx::frame_buffer::ptr thumbnail;

        bool is_cached_on_disk{false};
        fs::path cache_path{};
    };

    struct generator
    {
        std::map<hpp::uuid, generated_thumbnail> thumbnails;

        auto get_scene() -> scene&;

        void reset();

        void reset_wait();

        int remaining{0};

        std::array<scene, 3> scenes{scene{"thumbnail"}, scene{"thumbnail"}, scene{"thumbnail"}};

        int wait_frames{};
    };

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;
    void on_frame_update(rtti::context& ctx, delta_t);

    template<typename T>
    auto get_thumbnail(const asset_handle<T>& asset) -> gfx::texture::ptr
    {
        return thumbnails_.file.get();
    }



    auto get_thumbnail(const fs::path& path) -> gfx::texture::ptr;

    auto get_gizmo_icon(entt::handle e) -> gfx::texture::ptr;


    void regenerate_thumbnail(const hpp::uuid& uid);
    void remove_thumbnail(const hpp::uuid& uid);
    void clear_thumbnails();

    /// Sets the directory for persistent thumbnail caching.
    void set_cache_directory(const fs::path& cache_dir);

    /// Gets the disk cache path for a given asset uid.
    auto get_cache_path(const hpp::uuid& uid) const -> fs::path;

    /// Loads a cached thumbnail from disk (stub -- returns nullptr for now).
    auto load_cached_thumbnail(const hpp::uuid& uid) -> gfx::texture::ptr;

    /// Saves a generated thumbnail to disk (stub -- no-op for now).
    void save_thumbnail_to_cache(const hpp::uuid& uid, gfx::texture::ptr tex);

    /// Invalidates a cached thumbnail on disk.
    void invalidate_cache_entry(const hpp::uuid& uid);

private:
    struct thumbnail_cache
    {
        asset_handle<gfx::texture> transparent;
        asset_handle<gfx::texture> folder;
        asset_handle<gfx::texture> folder_empty;
        asset_handle<gfx::texture> file;
        asset_handle<gfx::texture> font;
        asset_handle<gfx::texture> loading;
        asset_handle<gfx::texture> shader;
        asset_handle<gfx::texture> material;
        asset_handle<gfx::texture> physics_material;
        asset_handle<gfx::texture> mesh;
        asset_handle<gfx::texture> animation;
        asset_handle<gfx::texture> audio_clip;
        asset_handle<gfx::texture> prefab;
        asset_handle<gfx::texture> scene_prefab;
        asset_handle<gfx::texture> script;
        asset_handle<gfx::texture> ui_tree;
        asset_handle<gfx::texture> style_sheet;

    } thumbnails_;


    struct gizmo_cache
    {
        asset_handle<gfx::texture> camera;
        asset_handle<gfx::texture> sky_light;
        asset_handle<gfx::texture> directional_light;
        asset_handle<gfx::texture> point_light;
        asset_handle<gfx::texture> spot_light;
        asset_handle<gfx::texture> audio_source;
        asset_handle<gfx::texture> reflection_probe;
        asset_handle<gfx::texture> particle_emitter;

    } gimzmo_icons_;


    generator gen_;

    std::map<std::string, asset_handle<gfx::texture>> icons_;
    std::map<std::string, asset_handle<gfx::texture>> gizmo_icons_;

    /// Directory for persistent thumbnail cache (e.g., "app:/.cache/").
    fs::path cache_directory_{};

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};


#define DECLARE_THUMBNAIL_SPEC(T) template<>\
auto thumbnail_manager::get_thumbnail<T>(const asset_handle<T>& asset) -> gfx::texture::ptr;

DECLARE_THUMBNAIL_SPEC(gfx::texture);
DECLARE_THUMBNAIL_SPEC(gfx::shader);
DECLARE_THUMBNAIL_SPEC(material);
DECLARE_THUMBNAIL_SPEC(mesh);
DECLARE_THUMBNAIL_SPEC(animation_clip);
DECLARE_THUMBNAIL_SPEC(prefab);
DECLARE_THUMBNAIL_SPEC(scene_prefab);
DECLARE_THUMBNAIL_SPEC(physics_material);
DECLARE_THUMBNAIL_SPEC(audio_clip);
DECLARE_THUMBNAIL_SPEC(font);
DECLARE_THUMBNAIL_SPEC(script);
DECLARE_THUMBNAIL_SPEC(ui_tree);
DECLARE_THUMBNAIL_SPEC(style_sheet);
} // namespace unravel
