#pragma once
#include "asset_extensions.h"
#include "../../threading/threader.h"
#include "../asset_handle.h"
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

} // namespace unravel

namespace unravel::asset_reader
{

auto resolve_compiled_key(const std::string& key) -> std::string;
auto resolve_compiled_path(const std::string& key) -> fs::path;

template<typename T>
auto load_from_file(tpp::thread_pool& pool, asset_handle<T>& output, const std::string& key) -> bool;

#define DECLARE_LOADER_SPEC(T)\
template<>\
auto load_from_file<T>(tpp::thread_pool& pool, asset_handle<T>& output, const std::string& key) -> bool

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
