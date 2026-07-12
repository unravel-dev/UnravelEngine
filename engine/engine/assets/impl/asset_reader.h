#pragma once
#include "asset_extensions.h"
#include "../../threading/threader.h"
#include "../asset_handle.h"
#include "../asset_flags.h"
#include <graphics/shader.h>

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

namespace unravel::asset_reader
{

auto resolve_compiled_key(const std::string& key) -> std::string;
auto resolve_compiled_path(const std::string& key) -> fs::path;

namespace detail
{
template<typename T>
inline auto compiled_extension() -> std::string
{
    return {};
}

template<>
inline auto compiled_extension<gfx::shader>() -> std::string
{
    return gfx::get_current_renderer_filename_extension();
}
} // namespace detail

/// Resolves the on-disk path of the cooked artifact for `key`, appending any
/// type-specific compiled suffix (e.g. the active renderer extension for shaders).
template<typename T>
inline auto resolve_compiled_path(const std::string& key) -> fs::path
{
    return fs::path(resolve_compiled_path(key).string() + detail::compiled_extension<T>());
}

/// Resolves the cooked artifact path for a source asset identified by `key`
/// and `source_extension`. Returns an empty path when the extension is not a
/// known importable source format.
auto resolve_compiled_asset_path(const std::string& key, const std::string& source_extension) -> fs::path;

template<typename T>
auto load_from_file(tpp::thread_pool& pool, asset_handle<T>& output, const std::string& key,
                    load_mode mode = load_mode::immediate) -> bool;

#define DECLARE_LOADER_SPEC(T)\
template<>\
auto load_from_file<T>(tpp::thread_pool& pool, asset_handle<T>& output, const std::string& key, load_mode mode) -> bool

DECLARE_LOADER_SPEC(gfx::shader);
DECLARE_LOADER_SPEC(gfx::texture);
DECLARE_LOADER_SPEC(material);
DECLARE_LOADER_SPEC(mesh);
DECLARE_LOADER_SPEC(animation_clip);
DECLARE_LOADER_SPEC(prefab);
DECLARE_LOADER_SPEC(scene_prefab);
DECLARE_LOADER_SPEC(physics_material);
DECLARE_LOADER_SPEC(audio_clip);
DECLARE_LOADER_SPEC(font);
DECLARE_LOADER_SPEC(script);
DECLARE_LOADER_SPEC(ui_tree);
DECLARE_LOADER_SPEC(style_sheet);

template<typename T>
inline auto get_job_name() -> std::string
{
    return fmt::format("Loading {}", ex::get_type<T>());
}

template<typename T>
inline auto load_from_instance(tpp::thread_pool& pool, asset_handle<T>& output, std::shared_ptr<T> instance) -> bool
{
    auto job = pool.schedule(get_job_name<T>(),
                       [](std::shared_ptr<T> instance)
                       {
                           return instance;
                       },
                       instance)
                   .share();

    output.set_internal_job(job);

    return true;
}
} // namespace unravel::asset_reader
