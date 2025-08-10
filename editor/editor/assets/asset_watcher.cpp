#include "asset_watcher.h"
#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_compiler.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/audio/audio_clip.h>
#include <engine/ecs/ecs.h>
#include <engine/ecs/prefab.h>
#include <engine/events.h>
#include <engine/physics/physics_material.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/font.h>
#include <engine/rendering/renderer.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/scripting/script.h>

#include <engine/meta/assets/asset_database.hpp>
#include <engine/threading/threader.h>

#include <editor/editing/editing_manager.h>
#include <editor/editing/thumbnail_manager.h>

#include <filesystem/watcher.h>
#include <graphics/graphics.h>
#include <logging/logging.h>

#include <fstream>
#include <set>

namespace unravel
{
namespace
{
using namespace std::literals;

template<typename T>
auto get_job_name() -> std::string
{
    return fmt::format("Compiling {}", ex::get_type<T>());
}

template<typename T>
auto checking_dependencies_job_name() -> std::string
{
    return fmt::format("Checking dependencies of {}", ex::get_type<T>());
}

void resolve_includes(const fs::path& file_path, std::set<fs::path>& processed_files)
{
    if(!processed_files.insert(file_path).second)
    {
        return; // Avoid processing the same file multiple times
    }

    std::ifstream file(file_path);
    if(!file.is_open())
    {
        // std::cerr << "Failed to open file: " << filePath << '\n';
        return;
    }

    const std::string include_keyword = "#include";
    const std::string bgfx_include_path = "/path/to/bgfx/include"; // Assuming bgfx include path is known

    std::string line;
    while(std::getline(file, line))
    {
        // Trim leading whitespace
        line.erase(0, line.find_first_not_of(" \t"));

        // Check for #include directive
        if(line.compare(0, include_keyword.length(), include_keyword) == 0)
        {
            // Find the start and end of the include path
            size_t start = line.find_first_of("\"<") + 1;
            size_t end = line.find_last_of("\">");

            if(start == std::string::npos || end == std::string::npos || start >= end)
            {
                // std::cerr << "Invalid include directive: " << line << '\n';
                continue;
            }

            std::string include_path = line.substr(start, end - start);
            fs::path resolved_path;

            if(line[start - 1] == '<' && line[end] == '>')
            {
                // Resolve system include path (e.g., bgfx_shader.sh)
                // resolvedPath = fs::path(bgfxIncludePath) / includePath;
                continue;
            }
            else
            {
                // Resolve local include path relative to the current file
                resolved_path = file_path.parent_path() / include_path;
            }

            resolved_path = fs::absolute(resolved_path);
            // std::cout << "Resolved include: " << resolvedPath << '\n';

            // Recurse into the included file
            resolve_includes(resolved_path, processed_files);
        }
    }
}

auto has_depencency(const fs::path& file, const fs::path& dep_to_check) -> bool
{
    std::set<fs::path> dependecies;
    resolve_includes(file, dependecies);

    return dependecies.contains(dep_to_check);
}

auto remove_meta_tag(const fs::path& synced_path) -> fs::path
{
    return fs::replace(synced_path, ".meta", "");
}

auto remove_meta_tag(const std::vector<fs::path>& synced_paths) -> std::vector<fs::path>
{
    std::decay_t<decltype(synced_paths)> reduced;
    reduced.reserve(synced_paths.size());
    for(const auto& synced_path : synced_paths)
    {
        reduced.emplace_back(remove_meta_tag(synced_path));
    }
    return reduced;
}

void unwatch(std::vector<uint64_t>& watchers)
{
    for(const auto& id : watchers)
    {
        fs::watcher::unwatch(id);
    }
    watchers.clear();
}

auto get_asset_key(const fs::path& path) -> std::string
{
    auto p = fs::reduce_trailing_extensions(path);
    auto data_key = fs::convert_to_protocol(p);
    auto key = fs::replace(data_key.generic_string(), ex::get_compiled_directory(), ex::get_data_directory()).generic_string();
    return key;
}

auto get_meta_key(const fs::path& path) -> std::string
{
    auto p = fs::reduce_trailing_extensions(path);
    auto data_key = fs::convert_to_protocol(p);
    auto key = fs::replace(data_key.generic_string(), ex::get_compiled_directory(), ex::get_meta_directory()).generic_string();
    return key + ".meta";
}

auto check_files_integrity(const std::string& key, const fs::path& entry_path) -> bool
{
    fs::error_code ec;
    auto key_path = fs::resolve_protocol(key);

    if(!fs::exists(key_path, ec))
    {
        APPLOG_WARNING("{} does not exist. Cleaning up cached...", key);
        fs::remove(entry_path, ec);

        auto meta = get_meta_key(entry_path);
        auto meta_path = fs::resolve_protocol(meta);
        if(fs::exists(meta_path, ec))
        {
            APPLOG_WARNING("{} does not exist. Cleaning up meta...", key);
            fs::remove(meta_path, ec);
        }

        return false;
    }

    return true;
}

template<typename T>
auto watch_assets(rtti::context& ctx, const fs::path& dir, const fs::pattern_filter& filter, bool reload_async) -> uint64_t
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& ts = ctx.get_cached<threader>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    fs::path watch_dir = fs::path(dir).make_preferred();

    auto callback = [&am, &ts, &tm, &em](const auto& entries, bool is_initial_list)
    {
        std::set<hpp::uuid> changed;
        std::set<hpp::uuid> removed;

        for(const auto& entry : entries)
        {
            auto key = get_asset_key(entry.path);

            APPLOG_TRACE("{}", fs::to_string(entry));

            if(entry.type == fs::file_type::regular)
            {
                if(entry.status == fs::watcher::entry_status::removed)
                {
                    removed.emplace(am.get_asset<T>(key).uid());
                    am.unload_asset<T>(key);

                    if constexpr(std::is_same<T, script>::value)
                    {
                        script_system::set_needs_recompile(fs::extract_protocol(fs::convert_to_protocol(key)).string());
                    }
                }
                else if(entry.status == fs::watcher::entry_status::renamed)
                {
                    auto old_key = get_asset_key(entry.last_path);
                    am.rename_asset<T>(old_key, key);

                    if constexpr(std::is_same<T, script>::value)
                    {
                        script_system::set_needs_recompile(fs::extract_protocol(fs::convert_to_protocol(key)).string());
                    }
                }
                else // created or modified
                {
                    if(check_files_integrity(key, entry.path))
                    {
                        load_flags flags = is_initial_list ? load_flags::standard : load_flags::reload;
                        auto asset = am.get_asset<T>(key, flags);

                        changed.emplace(asset.uid());
                    }

                    if constexpr(std::is_same<T, script>::value)
                    {
                        script_system::set_needs_recompile(fs::extract_protocol(fs::convert_to_protocol(key)).string());
                    }
                }
            }
        }

        if(!changed.empty() || !removed.empty())
        {
            tpp::invoke(tpp::main_thread::get_id(),
                        [&tm, &em, &am, changed, removed]()
                        {
                            for(const auto& uid : removed)
                            {
                                tm.remove_thumbnail(uid);
                            }

                            for(const auto& uid : changed)
                            {
                                tm.regenerate_thumbnail(uid);

                                if constexpr(std::is_same<T, prefab>::value)
                                {
                                    auto asset = am.get_asset<T>(uid);
                                    em.on_prefab_updated(asset);
                                }
                            }
                        });
        }
    };

    return fs::watcher::watch(watch_dir, filter, true, true, 500ms, callback);
}

template<typename T>
auto watch_assets_depenencies(rtti::context& ctx, const fs::path& dir, const fs::pattern_filter& filter) -> uint64_t
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& ts = ctx.get_cached<threader>();

    fs::path watch_dir = fs::path(dir).make_preferred();

    auto callback = [&am, &ts](const auto& entries, bool is_initial_list)
    {
        if(is_initial_list)
        {
            return;
        }

        for(const auto& entry : entries)
        {
            APPLOG_TRACE("{}", fs::to_string(entry));

            if(entry.type == fs::file_type::regular)
            {
                if(entry.status == fs::watcher::entry_status::removed)
                {
                }
                else if(entry.status == fs::watcher::entry_status::renamed)
                {
                }
                else // created or modified
                {
                    auto task = ts.pool->schedule(checking_dependencies_job_name<T>(),
                        [&am, entry]()
                        {
                            auto shaders = am.get_assets<T>();
                            for(const auto& shader : shaders)
                            {
                                auto meta = am.get_metadata(shader.uid());
                                auto absolute_path = fs::resolve_protocol(meta.location);

                                if(has_depencency(absolute_path, entry.path))
                                {
                                    fs::watcher::touch(absolute_path, false);
                                }
                            }
                        });
                }
            }
        }
    };

    return fs::watcher::watch(watch_dir, filter, true, true, 500ms, callback);
}

template<typename T>
static void add_to_syncer(rtti::context& ctx,
                          fs::syncer& syncer,
                          const fs::syncer::on_entry_removed_t& on_removed,
                          const fs::syncer::on_entry_renamed_t& on_renamed)
{
    auto& ts = ctx.get_cached<threader>();
    auto& am = ctx.get_cached<asset_manager>();

    auto on_modified =
        [&ts, &am](const std::string& ext, const auto& ref_path, const auto& synced_paths, bool is_initial_listing)
    {
        auto paths = remove_meta_tag(synced_paths);

        for(const auto& output : paths)
        {
            fs::error_code err;
            if(is_initial_listing && fs::exists(output, err))
            {
                continue;
            }
            auto key = get_asset_key(output);
            if(check_files_integrity(key, output))
            {
                auto task = ts.pool->schedule(get_job_name<T>(),
                    [&am, ref_path, output]()
                    {
                        asset_compiler::compile<T>(am, ref_path, output);
                    });
            }
        }
    };

    for(const auto& type : ex::get_suported_formats<T>())
    {
        syncer.set_mapping(type + ".meta", {".asset"}, on_modified, on_modified, on_removed, on_renamed);
    }
}

template<typename T>
static void watch_synced(rtti::context& ctx, std::vector<uint64_t>& watchers, const fs::path& dir)
{
    for(const auto& type : ex::get_suported_formats<T>())
    {
        const auto watch_id = watch_assets<T>(ctx, dir, fs::pattern_filter("*" + type + ".asset"), true);
        watchers.push_back(watch_id);
    }
}

template<>
void add_to_syncer<gfx::shader>(rtti::context& ctx,
                                fs::syncer& syncer,
                                const fs::syncer::on_entry_removed_t& on_removed,
                                const fs::syncer::on_entry_renamed_t& on_renamed)
{
    auto& ts = ctx.get_cached<threader>();
    auto& am = ctx.get_cached<asset_manager>();

    auto on_modified =
        [&ts, &am](const std::string& ext, const auto& ref_path, const auto& synced_paths, bool is_initial_listing)
    {
        auto paths = remove_meta_tag(synced_paths);
        if(paths.empty())
        {
            return;
        }
        const auto& platform_supported = gfx::get_renderer_platform_supported_filename_extensions();

        for(const auto& output : paths)
        {
            auto it =
                std::find(std::begin(platform_supported), std::end(platform_supported), output.extension().string());

            if(it == std::end(platform_supported))
            {
                continue;
            }

            fs::error_code err;
            if(is_initial_listing && fs::exists(output, err))
            {
                continue;
            }

            auto key = get_asset_key(output);
            if(check_files_integrity(key, output))
            {
                auto task = ts.pool->schedule(get_job_name<gfx::shader>(),
                    [&am, ref_path, output]()
                    {
                        asset_compiler::compile<gfx::shader>(am, ref_path, output);
                    });
            }
        }
    };

    for(const auto& type : ex::get_suported_formats<gfx::shader>())
    {
        syncer.set_mapping(type + ".meta",
                           {".asset.dx11", ".asset.dx12", ".asset.gl", ".asset.spirv"},
                           on_modified,
                           on_modified,
                           on_removed,
                           on_renamed);
    }
}

template<>
void watch_synced<gfx::shader>(rtti::context& ctx, std::vector<uint64_t>& watchers, const fs::path& dir)
{
    const auto& renderer_extension = gfx::get_current_renderer_filename_extension();
    for(const auto& type : ex::get_suported_formats<gfx::shader>())
    {
        const auto watch_id = watch_assets<gfx::shader>(ctx, dir, fs::pattern_filter("*" + type + ".asset" + renderer_extension), true);
        watchers.push_back(watch_id);
    }
}


} // namespace

void asset_watcher::setup_directory(rtti::context& ctx, fs::syncer& syncer)
{
    const auto on_dir_modified =
        [](const std::string& ext, const auto& /*ref_path*/, const auto& /*synced_paths*/, bool /*is_initial_listing*/)
    {

    };
    const auto on_dir_removed = [](const std::string& ext, const auto& /*ref_path*/, const auto& synced_paths)
    {
        for(const auto& synced_path : synced_paths)
        {
            fs::error_code err;
            fs::remove_all(synced_path, err);
        }
    };

    const auto on_dir_renamed = [](const std::string& ext, const auto& /*ref_path*/, const auto& synced_paths)
    {
        for(const auto& synced_path : synced_paths)
        {
            fs::error_code err;
            fs::rename(synced_path.first, synced_path.second, err);
        }
    };
    syncer.set_directory_mapping(on_dir_modified, on_dir_modified, on_dir_removed, on_dir_renamed);
}

void asset_watcher::setup_meta_syncer(rtti::context& ctx,
                                      std::vector<uint64_t>& watchers,
                                      fs::syncer& syncer,
                                      const fs::path& data_dir,
                                      const fs::path& meta_dir,
                                      bool wait)
{
    setup_directory(ctx, syncer);
    auto& am = ctx.get_cached<asset_manager>();

    const auto on_file_removed = [&am](const std::string& ext, const auto& ref_path, const auto& synced_paths)
    {
        for(const auto& synced_path : synced_paths)
        {
            fs::error_code err;
            fs::remove_all(synced_path, err);

            am.remove_asset_info_for_path(ref_path);
        }
    };

    const auto on_file_renamed = [](const std::string& ext, const auto& /*ref_path*/, const auto& synced_paths)
    {
        for(const auto& synced_path : synced_paths)
        {
            fs::error_code err;
            fs::rename(synced_path.first, synced_path.second, err);
        }
    };

    const auto on_file_modified =
        [&am](const std::string& ext, const auto& ref_path, const auto& synced_paths, bool is_initial_listing)
    {
        for(const auto& synced_path : synced_paths)
        {
            asset_meta meta;
            fs::error_code err;
            if(fs::exists(synced_path, err))
            {
                load_from_file(synced_path.string(), meta);
            }

            if(meta.uid.is_nil())
            {
                auto key = fs::convert_to_protocol(ref_path).generic_string();
                meta = am.generate_metadata(key);
            }
            meta.uid = am.add_asset_info_for_path(ref_path, meta, true);

            save_to_file(synced_path.string(), meta);
        }
    };

    for(const auto& asset_set : ex::get_all_formats())
    {
        for(const auto& type : asset_set)
        {
            syncer.set_mapping(type, {".meta"}, on_file_modified, on_file_modified, on_file_removed, on_file_renamed);
        }
    }

    for(const auto& dep_ex : ex::get_suported_dependencies_formats<gfx::shader>())
    {
        auto id = watch_assets_depenencies<gfx::shader>(ctx, data_dir, fs::pattern_filter("*" + dep_ex));
        watchers.emplace_back(id);
    }

    syncer.sync(data_dir, meta_dir);

    if(wait)
    {
        auto& ts = ctx.get_cached<threader>();
        ts.pool->wait_all();
    }
}

void asset_watcher::setup_cache_syncer(rtti::context& ctx,
                                       std::vector<uint64_t>& watchers,
                                       fs::syncer& syncer,
                                       const fs::path& meta_dir,
                                       const fs::path& cache_dir,
                                       bool wait)
{
    setup_directory(ctx, syncer);

    auto on_removed = [](const std::string& ext, const auto& /*ref_path*/, const auto& synced_paths)
    {
        for(const auto& synced_path : synced_paths)
        {
            auto synced_asset = remove_meta_tag(synced_path);
            fs::error_code err;
            fs::remove_all(synced_asset, err);
        }
    };

    auto on_renamed = [](const std::string& ext, const auto& /*ref_path*/, const auto& synced_paths)
    {
        for(const auto& synced_path : synced_paths)
        {
            auto synced_old_asset = remove_meta_tag(synced_path.first);
            auto synced_new_asset = remove_meta_tag(synced_path.second);
            fs::error_code err;
            fs::rename(synced_old_asset, synced_new_asset, err);
        }
    };

    add_to_syncer<gfx::texture>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<gfx::shader>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<mesh>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<material>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<animation_clip>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<prefab>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<scene_prefab>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<physics_material>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<audio_clip>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<font>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<script>(ctx, syncer, on_removed, on_renamed);

    syncer.sync(meta_dir, cache_dir);

    if(wait)
    {
        auto& ts = ctx.get_cached<threader>();
        ts.pool->wait_all();
    }

    watch_synced<gfx::texture>(ctx, watchers, cache_dir);
    watch_synced<gfx::shader>(ctx, watchers, cache_dir);
    watch_synced<mesh>(ctx, watchers, cache_dir);
    watch_synced<material>(ctx, watchers, cache_dir);
    watch_synced<animation_clip>(ctx, watchers, cache_dir);
    watch_synced<prefab>(ctx, watchers, cache_dir);
    watch_synced<scene_prefab>(ctx, watchers, cache_dir);
    watch_synced<physics_material>(ctx, watchers, cache_dir);
    watch_synced<audio_clip>(ctx, watchers, cache_dir);
    watch_synced<font>(ctx, watchers, cache_dir);
    watch_synced<script>(ctx, watchers, cache_dir);
}

asset_watcher::asset_watcher()
{
}

asset_watcher::~asset_watcher()
{
}

void asset_watcher::on_os_event(rtti::context& ctx, os::event& e)
{
    auto& rend = ctx.get_cached<renderer>();
    const auto& window = rend.get_main_window();

    if(e.type == os::events::window)
    {
        if(window && e.window.window_id == window->get_window().get_id())
        {
            if(e.window.type == os::window_event_id::focus_lost)
            {
                fs::watcher::pause();
            }
            if(e.window.type == os::window_event_id::focus_gained)
            {
                fs::watcher::resume();
            }
        }
    }
}

auto asset_watcher::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ev = ctx.get_cached<events>();
    ev.on_os_event.connect(sentinel_, 1000, this, &asset_watcher::on_os_event);

    watch_assets(ctx, "engine:/", true);

    return true;
}

auto asset_watcher::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    unwatch_assets(ctx, "engine:/");
    return true;
}

void asset_watcher::watch_assets(rtti::context& ctx, const std::string& protocol, bool wait)
{
    auto& w = watched_protocols_[protocol];

    auto data_protocol = ex::get_data_directory_no_slash(protocol);
    auto meta_protocol = ex::get_meta_directory_no_slash(protocol);
    auto cache_protocol = ex::get_compiled_directory_no_slash(protocol);

    setup_meta_syncer(ctx,
                      w.watchers,
                      w.meta_syncer,
                      fs::resolve_protocol(data_protocol),
                      fs::resolve_protocol(meta_protocol),
                      wait);

    setup_cache_syncer(ctx,
                       w.watchers,
                       w.cache_syncer,
                       fs::resolve_protocol(meta_protocol),
                       fs::resolve_protocol(cache_protocol),
                       wait);
}

void asset_watcher::unwatch_assets(rtti::context& ctx, const std::string& protocol)
{
    auto& w = watched_protocols_[protocol];

    unwatch(w.watchers);
    w.meta_syncer.unsync();
    w.cache_syncer.unsync();

    watched_protocols_.erase(protocol);

    auto& am = ctx.get_cached<asset_manager>();
    am.unload_group(protocol);
}

} // namespace unravel
