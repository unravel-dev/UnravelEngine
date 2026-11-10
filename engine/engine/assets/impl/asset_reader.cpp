#include "asset_reader.h"
#include "graphics/graphics.h"

#include <engine/meta/animation/animation.hpp>
#include <engine/meta/audio/audio_clip.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/animation/animation_controller.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <engine/meta/rendering/font.hpp>
#include <engine/meta/rendering/shader_variants.hpp>
#include <engine/meta/rendering/standard_material.hpp>
#include <engine/meta/scripting/script.hpp>
#include <engine/meta/ui/ui_tree.hpp>
#include <engine/meta/ui/style_sheet.hpp>

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/rendering/render_feature_support.h>
#include <engine/threading/main_thread_shared.h>
#include <cstdint>
#include <filesystem/filesystem.h>
#include <filesystem/file_istream.h>
#include <filesystem/pack.h>
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

auto resolve_compiled_asset_path(const std::string& key, const std::string& source_extension) -> fs::path
{
    if(!fs::has_known_protocol(key))
    {
        return {};
    }

    std::string compiled_ext;
    if(ex::is_format<gfx::shader>(source_extension))
    {
        compiled_ext = gfx::get_current_renderer_filename_extension();
    }
    else
    {
        bool known_source = false;
        for(const auto& formats : ex::get_all_formats())
        {
            for(const auto& format : formats)
            {
                if(format == source_extension)
                {
                    known_source = true;
                    break;
                }
            }
            if(known_source)
            {
                break;
            }
        }

        if(!known_source)
        {
            return {};
        }
    }

    return fs::path(resolve_compiled_path(key).string() + compiled_ext);
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

void log_uncompiled_asset_for_key(const std::string& key)
{
    APPLOG_WARNING("Compiled asset {0} does not exist yet; the asset stays empty until it is compiled.", key);
}

void log_missing_raw_asset_for_key(const std::string& key)
{
    APPLOG_ERROR("Asset {0} does not exist!", key);
}

void log_unknown_protocol_for_key(const std::string& key)
{
    APPLOG_ERROR("Asset {0} has unknown protocol!", key);
}

/// Whether a loader can read an asset's source file when its compiled file is missing.
enum class source_fallback
{
    /// The loader parses the source format too (textures, prefabs, fonts).
    allowed,
    /// The loader reads the compiled format only. Handing it the source would make a broken asset (a mesh with no
    /// vertices) that a later reload has to replace, so the load fails and the compiled-asset watcher loads the asset
    /// once the compiler writes it.
    none,
};

auto validate(const std::string& key, const std::string& compiled_ext, source_fallback fallback, std::string& out)
    -> bool
{
    if(!fs::has_known_protocol(key))
    {
        log_unknown_protocol_for_key(key);
        return false;
    }

    auto compiled_absolute_path = resolve_compiled_path(key).string() + compiled_ext;

    // A deployed game reads its compiled files from mounted packs; the editor from its cache.
    if(!fs::is_readable_file(compiled_absolute_path))
    {
        if(fallback == source_fallback::none)
        {
            log_uncompiled_asset_for_key(compiled_absolute_path);
            return false;
        }
        log_missing_compiled_asset_for_key(compiled_absolute_path);

        compiled_absolute_path = resolve_path(key).string();
    }

    if(!fs::is_readable_file(compiled_absolute_path))
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
                   const std::string& compiled_ext, source_fallback fallback, CreateFromPathFn create_fn,
                   load_mode mode) -> bool
{
    if(mode == load_mode::deferred)
    {
        auto deferred_fn = [key, compiled_ext, fallback, create_fn]() -> std::shared_ptr<T>
        {
            std::string path{};
            if(!validate(key, compiled_ext, fallback, path))
            {
                return nullptr;
            }
            return create_fn(path);
        };
        output.set_internal_job(pool.create_job(get_job_name<T>(), deferred_fn).share());
        return true;
    }

    std::string path{};
    if(!validate(key, compiled_ext, fallback, path))
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

/// One compiled variant as a GPU shader, named so a GPU capture tells the variants apart; empty unless @p is_used.
/// A variant compiled without code (one WebGL 2 cannot run, asset_compiler) is empty too.
auto create_shader_variant(const compiled_shader_variant& variant, const std::string& key, bool is_used)
    -> std::shared_ptr<gfx::shader>
{
    if(!is_used || variant.bytes.empty())
    {
        return std::make_shared<gfx::shader>();
    }
    auto shader = std::make_shared<gfx::shader>(bgfx::copy(variant.bytes.data(), static_cast<std::uint32_t>(variant.bytes.size())));
    shader->varyings = variant.varyings;
    if(shader->is_valid())
    {
        shader->set_name(fs::path(key).filename().string() + "#" + variant.name);
    }
    return shader;
}

/// A compiled shader as one asset: the default variant is the shader itself, the others its variants.
auto create_shader_with_variants(const std::string& path, const std::string& key) -> std::shared_ptr<gfx::shader>
{
    std::vector<compiled_shader_variant> compiled;
    load_from_file_bin(path, compiled);
    const auto is_default = [](const compiled_shader_variant& variant)
    {
        return variant.name == gfx::shader::DEFAULT_VARIANT;
    };
    const auto default_variant = std::find_if(compiled.begin(), compiled.end(), is_default);
    if(default_variant == compiled.end())
    {
        APPLOG_ERROR("Compiled shader {0} holds no default variant.", path);
        return nullptr;
    }
    // A compute shader of a feature the device does not run stays empty, as its program does without compute: it may
    // declare storage formats the device lacks, which WebGPU rejects as soon as the shader is created. Its variants
    // keep their names, so a program that picks one finds an empty shader rather than none.
    const bool is_used = get_render_feature_support().runs_shader(key);
    auto result = create_shader_variant(*default_variant, key, is_used);
    for(const auto& variant : compiled)
    {
        if(!is_default(variant))
        {
            result->variants.emplace(variant.name, create_shader_variant(variant, key, is_used));
        }
    }
    return result;
}

} // namespace detail

auto load_texture_from_file(tpp::thread_pool& pool,
                            asset_handle<gfx::texture>& output,
                            const std::string& key,
                            load_mode mode,
                            bool is_normal_map) -> bool
{
    return detail::schedule_load<gfx::texture>(pool, output, key, {}, source_fallback::allowed,
        [is_normal_map](const std::string& path)
        {
            auto texture = std::make_shared<gfx::texture>(path.c_str());
            texture->set_normal_map(is_normal_map);
            return texture;
        }, mode);
}

template<>
auto load_from_file<gfx::texture>(tpp::thread_pool& pool, asset_handle<gfx::texture>& output,
                                   const std::string& key, load_mode mode) -> bool
{
    return load_texture_from_file(pool, output, key, mode, false);
}

template<>
auto load_from_file<gfx::shader>(tpp::thread_pool& pool, asset_handle<gfx::shader>& output,
                                  const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<gfx::shader>(pool, output, key,
        gfx::get_current_renderer_filename_extension(), source_fallback::none,
        [key](const std::string& path)
        {
            return detail::create_shader_with_variants(path, key);
        }, mode);
}

template<>
auto load_from_file<material>(tpp::thread_pool& pool, asset_handle<material>& output,
                               const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<material>(pool, output, key, {}, source_fallback::none,
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
    return detail::schedule_load<mesh>(pool, output, key, {}, source_fallback::none,
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
    return detail::schedule_load<animation_clip>(pool, output, key, {}, source_fallback::none,
        [](const std::string& path)
        {
            auto anim = std::make_shared<animation_clip>();
            load_from_file_bin(path, *anim);
            resolve_rest_transforms(*anim);
            return anim;
        }, mode);
}

template<>
auto load_from_file<prefab>(tpp::thread_pool& pool, asset_handle<prefab>& output,
                             const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<prefab>(pool, output, key, {}, source_fallback::allowed,
        [](const std::string& path) -> std::shared_ptr<prefab>
        {
            auto stream = fs::open_readable_stream(path);
            if(!stream)
            {
                return nullptr;
            }
            auto pfb = std::make_shared<prefab>();
            pfb->buffer = fs::read_stream_buffer(*stream);
            return pfb;
        }, mode);
}

template<>
auto load_from_file<scene_prefab>(tpp::thread_pool& pool, asset_handle<scene_prefab>& output,
                                   const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<scene_prefab>(pool, output, key, {}, source_fallback::allowed,
        [](const std::string& path) -> std::shared_ptr<scene_prefab>
        {
            auto stream = fs::open_readable_stream(path);
            if(!stream)
            {
                return nullptr;
            }
            auto pfb = std::make_shared<scene_prefab>();
            pfb->buffer = fs::read_stream_buffer(*stream);
            return pfb;
        }, mode);
}

template<>
auto load_from_file<animation_controller>(tpp::thread_pool& pool, asset_handle<animation_controller>& output,
                                           const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<animation_controller>(pool, output, key, {}, source_fallback::none,
        [](const std::string& path)
        {
            auto controller = std::make_shared<animation_controller>();
            load_from_file_bin(path, controller);
            return controller;
        }, mode);
}

template<>
auto load_from_file<physics_material>(tpp::thread_pool& pool, asset_handle<physics_material>& output,
                                       const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<physics_material>(pool, output, key, {}, source_fallback::none,
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
    return detail::schedule_load<audio_clip>(pool, output, key, {}, source_fallback::none,
        [](const std::string& path) -> std::shared_ptr<audio_clip>
        {
            encoded_sound encoded;
            audio::sound_data data;
            std::string error;
            if(!load_from_file_bin(path, encoded) || !decode_sound(encoded, data, error))
            {
                APPLOG_ERROR("Failed to load audio clip {}: {}", path, error.empty() ? "not a compiled audio clip" : error);
                return nullptr;
            }

            // Create + destroy on main: OpenAL buffers are context-thread affine.
            // Creation already hops here; main_thread_deleter covers reload/demote
            // when the old shared_ptr is dropped on a watcher/worker thread.
            auto create_job = tpp::async(tpp::main_thread::get_id(),
                                         [data = std::move(data)]() mutable
                                         {
                                             return make_shared_main_thread<audio_clip>(std::move(data), false);
                                         });

            return create_job.get();
        }, mode);
}

template<>
auto load_from_file<font>(tpp::thread_pool& pool, asset_handle<font>& output,
                           const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<font>(pool, output, key, {}, source_fallback::allowed,
        [](const std::string& path)
        {
            auto create_job = tpp::async(tpp::main_thread::get_id(),
                                         [path]()
                                         {
                                             return make_shared_main_thread<font>(path.c_str(), 0, 86,
                                                 FONT_TYPE_DISTANCE_OUTLINE_DROP_SHADOW_IMAGE, 8, 8);
                                         });

            return create_job.get();
        }, mode);
}

template<>
auto load_from_file<script>(tpp::thread_pool& pool, asset_handle<script>& output,
                             const std::string& key, load_mode mode) -> bool
{
    return detail::schedule_load<script>(pool, output, key, {}, source_fallback::none,
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
    return detail::schedule_load<ui_tree>(pool, output, key, {}, source_fallback::none,
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
    return detail::schedule_load<style_sheet>(pool, output, key, {}, source_fallback::none,
        [](const std::string& path)
        {
            auto sheet = std::make_shared<style_sheet>();
            load_from_file(path, sheet);
            return sheet;
        }, mode);
}

} // namespace unravel::asset_reader
