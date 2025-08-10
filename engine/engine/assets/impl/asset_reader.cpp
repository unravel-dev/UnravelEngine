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

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <cstdint>
#include <filesystem/filesystem.h>
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

template<>
auto load_from_file<gfx::texture>(tpp::thread_pool& pool, asset_handle<gfx::texture>& output, const std::string& key)
    -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path]()
    {
        return std::make_shared<gfx::texture>(compiled_absolute_path.c_str());
    };

    auto job = pool.schedule(get_job_name<gfx::texture>(), create_resource_func).share();

    output.set_internal_job(job);

    return true;
}

template<>
auto load_from_file<gfx::shader>(tpp::thread_pool& pool, asset_handle<gfx::shader>& output, const std::string& key)
    -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, gfx::get_current_renderer_filename_extension(), compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path, key]()
    {
        auto stream = std::ifstream{compiled_absolute_path, std::ios::binary};
        auto read_memory = fs::read_stream(stream);

        const gfx::memory_view* mem = gfx::copy(read_memory.data(), static_cast<std::uint32_t>(read_memory.size()));

        auto shader = std::make_shared<gfx::shader>(mem); 

        gfx::set_name(shader->native_handle(), key.c_str(), static_cast<int32_t>(key.size()));

        return shader;
    };

    auto job = pool.schedule(get_job_name<gfx::shader>(), create_resource_func).share();
    output.set_internal_job(job);

    return true;
}

template<>
auto load_from_file<material>(tpp::thread_pool& pool, asset_handle<material>& output, const std::string& key) -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path]()
    {
        std::shared_ptr<unravel::material> material;
        load_from_file_bin(compiled_absolute_path, material);
        return material;
    };

    auto job = pool.schedule(get_job_name<material>(), create_resource_func).share();
    output.set_internal_job(job);

    return true;
}

template<>
auto load_from_file<mesh>(tpp::thread_pool& pool, asset_handle<mesh>& output, const std::string& key) -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path]()
    {
        mesh::load_data data;
        load_from_file_bin(compiled_absolute_path, data);

        auto mesh = std::make_shared<unravel::mesh>();
        mesh->load_mesh(std::move(data));
        return mesh;
    };

    auto job = pool.schedule(get_job_name<mesh>(), create_resource_func).share();
    output.set_internal_job(job);

    return true;
}


template<>
auto load_from_file<animation_clip>(tpp::thread_pool& pool, asset_handle<animation_clip>& output, const std::string& key) -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path]()
    {
        auto anim = std::make_shared<animation_clip>();
        load_from_file_bin(compiled_absolute_path, *anim);

        return anim;
    };

    auto job = pool.schedule(get_job_name<animation_clip>(), create_resource_func).share();
    output.set_internal_job(job);

    return true;
}

template<>
auto load_from_file<prefab>(tpp::thread_pool& pool, asset_handle<prefab>& output, const std::string& key) -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path]()
    {
        auto pfb = std::make_shared<prefab>();

        auto stream = std::ifstream{compiled_absolute_path};
        pfb->buffer = fs::read_stream_buffer(stream);
        return pfb;
    };

    auto job = pool.schedule(get_job_name<prefab>(), create_resource_func).share();
    output.set_internal_job(job);

    return true;
}

template<>
auto load_from_file<scene_prefab>(tpp::thread_pool& pool, asset_handle<scene_prefab>& output, const std::string& key)
    -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path]()
    {
        auto pfb = std::make_shared<scene_prefab>();

        auto stream = std::ifstream{compiled_absolute_path};
        pfb->buffer = fs::read_stream_buffer(stream);
        return pfb;
    };

    auto job = pool.schedule(get_job_name<scene_prefab>(), create_resource_func).share();
    output.set_internal_job(job);

    return true;
}

template<>
auto load_from_file<physics_material>(tpp::thread_pool& pool,
                                      asset_handle<physics_material>& output,
                                      const std::string& key) -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path]()
    {
        auto material = std::make_shared<physics_material>();
        load_from_file_bin(compiled_absolute_path, material);
        return material;
    };

    auto job = pool.schedule(get_job_name<physics_material>(), create_resource_func).share();
    output.set_internal_job(job);

    return true;
}

template<>
auto load_from_file<audio_clip>(tpp::thread_pool& pool, asset_handle<audio_clip>& output, const std::string& key)
    -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_load_func = [compiled_absolute_path]()
    {
        auto data = std::make_shared<audio::sound_data>();
        load_from_file_bin(compiled_absolute_path, *data);
        return data;
    };

    auto create_resource_func = [compiled_absolute_path]()
    {
        audio::sound_data data;
        load_from_file_bin(compiled_absolute_path, data);

        auto create_job = tpp::async(tpp::main_thread::get_id(),
                                     [data = std::move(data)]() mutable
                                     {
                                         auto clip = std::make_shared<audio_clip>(std::move(data), false);
                                         return clip;
                                     });

        return create_job.get();
    };

    auto job = pool.schedule(get_job_name<audio_clip>(), create_resource_func).share();

    output.set_internal_job(job);

    return true;
}

template<>
auto load_from_file<font>(tpp::thread_pool& pool, asset_handle<font>& output, const std::string& key)
    -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path]()
    {
        auto create_job = tpp::async(tpp::main_thread::get_id(),
                                     [compiled_absolute_path]() mutable
                                     {
                                         auto fnt = std::make_shared<font>(compiled_absolute_path.c_str(), 0, 86, FONT_TYPE_DISTANCE_OUTLINE_DROP_SHADOW_IMAGE, 8, 8);
                                         return fnt;
                                     });

        return create_job.get();
    };

    auto job = pool.schedule(get_job_name<font>(), create_resource_func).share();

    output.set_internal_job(job);

    return true;
}

template<>
auto load_from_file<script>(tpp::thread_pool& pool, asset_handle<script>& output, const std::string& key)
    -> bool
{
    std::string compiled_absolute_path{};

    if(!validate(key, {}, compiled_absolute_path))
    {
        return false;
    }

    auto create_resource_func = [compiled_absolute_path]()
    {
        auto scr = std::make_shared<script>();
        load_from_file_bin(compiled_absolute_path, scr);
        return scr;
    };

    auto job = pool.schedule(get_job_name<script>(), create_resource_func).share();
    output.set_internal_job(job);

    return true;
}

} // namespace unravel::asset_reader
