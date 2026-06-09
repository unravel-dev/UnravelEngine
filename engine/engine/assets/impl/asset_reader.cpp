#include "asset_reader.h"
#include "graphics/graphics.h"

#include <engine/meta/animation/animation.hpp>
#include <engine/meta/audio/audio_clip.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <engine/meta/rendering/font.hpp>
#include <engine/meta/rendering/standard_material.hpp>
#include <engine/meta/scripting/script.hpp>
#include <engine/meta/ui/ui_tree.hpp>
#include <engine/meta/ui/style_sheet.hpp>

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <cstdint>
#include <filesystem/filesystem.h>
#include <filesystem/file_istream.h>
#include <graphics/shader.h>
#include <graphics/texture.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

namespace unravel::asset_reader
{

auto resolve_compiled_key(const std::string& key) -> std::string
{
    return string_utils::replace(key + ".asset", ex::get_data_directory(), ex::get_compiled_directory());
}

auto resolve_compiled_path(const std::string& key) -> fs::path
{
    auto cache_key = resolve_compiled_key(key);
    return fs::absolute(fs::resolve_protocol(cache_key));
}

auto resolve_path(const std::string& key) -> fs::path
{
    return fs::absolute(fs::resolve_protocol(key));
}

void log_missing_compiled_asset_for_key(const std::string& key)
{
    APPLOG_WARNING("Compiled asset {0} does not exist!"
                   "Falling back to raw asset.",
                   key);
}

void log_missing_raw_asset_for_key(const std::string& key)
{
    APPLOG_ERROR("Asset {0} does not exist!", key);
}

void log_unknown_protocol_for_key(const std::string& key)
{
    APPLOG_ERROR("Asset {0} has unknown protocol!", key);
}

auto validate(const std::string& key, const std::string& compiled_ext, std::string& out) -> bool
{
    if(!fs::has_known_protocol(key))
    {
        log_unknown_protocol_for_key(key);
        return false;
    }

    auto compiled_absolute_path = resolve_compiled_path(key).string() + compiled_ext;

    fs::error_code err;
    if(!fs::exists(compiled_absolute_path, err))
    {
        log_missing_compiled_asset_for_key(compiled_absolute_path);

        compiled_absolute_path = resolve_path(key).string();
    }

    if(!fs::exists(compiled_absolute_path, err))
    {
        log_missing_raw_asset_for_key(key);
        return false;
    }

    out = compiled_absolute_path;
    return true;
}

namespace detail
{

template<typename T, typename CreateFromPathFn>
auto schedule_load(tpp::thread_pool& pool, asset_handle<T>& output, const std::string& key,
                   const std::string& compiled_ext, CreateFromPathFn create_fn, load_mode mode) -> bool
{
    if(mode == load_mode::deferred)
    {
        auto deferred_fn = [key, compiled_ext, create_fn]() -> std::shared_ptr<T>
        {
            std::string path{};
            if(!validate(key, compiled_ext, path))
            {
                return nullptr;
            }
            return create_fn(path);
        };
        output.set_internal_job(pool.create_job(get_job_name<T>(), deferred_fn).share());
        return true;
    }

    std::string path{};
    if(!validate(key, compiled_ext, path))
    {
        return false;
    }
    auto immediate_fn = [path, create_fn]() -> std::shared_ptr<T>
    {
        return create_fn(path);
    };
    output.set_internal_job(pool.schedule(get_job_name<T>(), immediate_fn).share());
    return true;
}

} // namespace detail

template<>
auto load_from_file<gfx::texture>(tpp::thread_pool& pool, asset_handle<gfx::texture>& output,
                                   const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<gfx::texture>(pool, output, key, {},
        [](const std::string& path)
        {
            return std::make_shared<gfx::texture>(path.c_str());
        }, mode);
}

template<>
auto load_from_file<gfx::shader>(tpp::thread_pool& pool, asset_handle<gfx::shader>& output,
                                  const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<gfx::shader>(pool, output, key,
        gfx::get_current_renderer_filename_extension(),
        [key](const std::string& path)
        {
            return std::make_shared<gfx::shader>(path);
        }, mode);
}

template<>
auto load_from_file<material>(tpp::thread_pool& pool, asset_handle<material>& output,
                               const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<material>(pool, output, key, {},
        [](const std::string& path)
        {
            std::shared_ptr<unravel::material> mat;
            load_from_file_bin(path, mat);
            return mat;
        }, mode);
}

template<>
auto load_from_file<mesh>(tpp::thread_pool& pool, asset_handle<mesh>& output,
                           const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<mesh>(pool, output, key, {},
        [](const std::string& path)
        {
            mesh::load_data data;
            load_from_file_bin(path, data);

            auto m = std::make_shared<unravel::mesh>();
            m->load_mesh(std::move(data));
            return m;
        }, mode);
}

template<>
auto load_from_file<animation_clip>(tpp::thread_pool& pool, asset_handle<animation_clip>& output,
                                     const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<animation_clip>(pool, output, key, {},
        [](const std::string& path)
        {
            auto anim = std::make_shared<animation_clip>();
            load_from_file_bin(path, *anim);
            return anim;
        }, mode);
}

template<>
auto load_from_file<prefab>(tpp::thread_pool& pool, asset_handle<prefab>& output,
                             const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<prefab>(pool, output, key, {},
        [](const std::string& path)
        {
            auto pfb = std::make_shared<prefab>();
            auto stream = std::ifstream{path};
            pfb->buffer = fs::read_stream_buffer(stream);
            return pfb;
        }, mode);
}

template<>
auto load_from_file<scene_prefab>(tpp::thread_pool& pool, asset_handle<scene_prefab>& output,
                                   const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<scene_prefab>(pool, output, key, {},
        [](const std::string& path)
        {
            auto pfb = std::make_shared<scene_prefab>();
            auto stream = std::ifstream{path};
            pfb->buffer = fs::read_stream_buffer(stream);
            return pfb;
        }, mode);
}

template<>
auto load_from_file<physics_material>(tpp::thread_pool& pool, asset_handle<physics_material>& output,
                                       const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<physics_material>(pool, output, key, {},
        [](const std::string& path)
        {
            auto mat = std::make_shared<physics_material>();
            load_from_file_bin(path, mat);
            return mat;
        }, mode);
}

template<>
auto load_from_file<audio_clip>(tpp::thread_pool& pool, asset_handle<audio_clip>& output,
                                 const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<audio_clip>(pool, output, key, {},
        [](const std::string& path)
        {
            audio::sound_data data;
            load_from_file_bin(path, data);

            auto create_job = tpp::async(tpp::main_thread::get_id(),
                                         [data = std::move(data)]() mutable
                                         {
                                             return std::make_shared<audio_clip>(std::move(data), false);
                                         });

            return create_job.get();
        }, mode);
}

template<>
auto load_from_file<font>(tpp::thread_pool& pool, asset_handle<font>& output,
                           const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<font>(pool, output, key, {},
        [](const std::string& path)
        {
            auto create_job = tpp::async(tpp::main_thread::get_id(),
                                         [path]()
                                         {
                                             return std::make_shared<font>(path.c_str(), 0, 86,
                                                 FONT_TYPE_DISTANCE_OUTLINE_DROP_SHADOW_IMAGE, 8, 8);
                                         });

            return create_job.get();
        }, mode);
}

template<>
auto load_from_file<script>(tpp::thread_pool& pool, asset_handle<script>& output,
                             const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<script>(pool, output, key, {},
        [](const std::string& path)
        {
            auto scr = std::make_shared<script>();
            load_from_file_bin(path, scr);
            return scr;
        }, mode);
}

template<>
auto load_from_file<ui_tree>(tpp::thread_pool& pool, asset_handle<ui_tree>& output,
                              const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<ui_tree>(pool, output, key, {},
        [](const std::string& path)
        {
            auto tree = std::make_shared<ui_tree>();
            load_from_file(path, tree);
            return tree;
        }, mode);
}

template<>
auto load_from_file<style_sheet>(tpp::thread_pool& pool, asset_handle<style_sheet>& output,
                                  const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<style_sheet>(pool, output, key, {},
        [](const std::string& path)
        {
            auto sheet = std::make_shared<style_sheet>();
            load_from_file(path, sheet);
            return sheet;
        }, mode);
}

} // namespace unravel::asset_reader
