#include "asset_watcher.h"
#include "engine/ui/ui_tree.h"
#include "filesystem/filesystem.h"
#include "threadpp/thread.h"
#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_compiler.h>
#include <engine/assets/impl/asset_dependencies.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_manifest.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/assets/asset_dependency_graph.h>
#include <engine/audio/audio_clip.h>
#include <engine/ecs/ecs.h>
#include <engine/ecs/prefab.h>
#include <engine/events.h>
#include <engine/physics/physics_material.h>
#include <engine/rendering/font.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/renderer.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/scripting/script.h>


#include <engine/meta/assets/asset_database.hpp>
#include <engine/threading/threader.h>

#include <editor/editing/editing_manager.h>
#include <editor/editing/thumbnail_invalidation.h>
#include <editor/editing/thumbnail_manager.h>

#include <filesystem/watcher.h>
#include <graphics/graphics.h>
#include <logging/logging.h>

#include <algorithm>
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


template<typename T>
auto checking_for_recompilation_job_name() -> std::string
{
    return fmt::format("Checking for recompilation of {}", ex::get_type<T>());
}



auto get_absolute_source_path(const fs::path& source_file_path) -> fs::path
{
    auto absolute_source = fs::resolve_protocol(
        fs::replace(fs::convert_to_protocol(source_file_path), ex::get_meta_directory(), ex::get_data_directory()));
    if(absolute_source.extension() == ".meta")
    {
        absolute_source.replace_extension();
    }
    return absolute_source;
}

/// Check if recompilation is needed based on manifest.
/// Resolves include dependencies via asset_compiler::resolve_dependencies<T> so that
/// changes in included files are detected through the combined SHA.
template<typename T>
auto needs_recompilation(const fs::path& source_file_path, const fs::path& compiled_output_path) -> bool
{
    fs::error_code err;
    if(!fs::exists(compiled_output_path, err) || err)
    {
        return true;
    }
    auto manifest_path = asset_compiler::get_manifest_path(compiled_output_path);
    if(!fs::exists(manifest_path, err) || err)
    {
        return true;
    }
    asset_compiler::asset_manifest manifest;
    if(!asset_compiler::load_manifest(manifest_path, manifest))
    {
        APPLOG_WARNING("Failed to load manifest for {}, recompilation needed", compiled_output_path.string());
        return true;
    }

    if(asset_compiler::is_compiled_format_changed(source_file_path, manifest))
    {
        APPLOG_WARNING("Compiled format changed for {}, recompilation needed", compiled_output_path.string());
        return true;
    }

    std::vector<fs::path> deps;
    asset_compiler::resolve_dependencies<T>(get_absolute_source_path(source_file_path), deps);
    if(asset_compiler::is_source_file_changed(source_file_path, manifest, deps))
    {
        APPLOG_WARNING("Source file changed for {}, recompilation needed", compiled_output_path.string());
        return true;
    }

    return false;
}

template<typename T>
auto has_depencency(const fs::path& file, const fs::path& dep_to_check) -> bool
{
    std::vector<fs::path> dependecies;
    asset_compiler::resolve_dependencies<T>(file, dependecies);
    return std::find(dependecies.begin(), dependecies.end(), dep_to_check) != dependecies.end();
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
    auto key =
        fs::replace(data_key.generic_string(), ex::get_compiled_directory(), ex::get_data_directory()).generic_string();
    return key;
}

auto get_meta_key(const fs::path& path) -> std::string
{
    auto p = fs::reduce_trailing_extensions(path);
    auto data_key = fs::convert_to_protocol(p);
    auto key =
        fs::replace(data_key.generic_string(), ex::get_compiled_directory(), ex::get_meta_directory()).generic_string();
    return key + ".meta";
}

auto check_files_integrity(const std::string& key, const fs::path& entry_path) -> bool
{
    fs::error_code ec;
    auto key_path = fs::resolve_protocol(key);

    if(!fs::exists(key_path, ec))
    {
        APPLOG_WARNING("{} does not exist. Cleaning up compiled...", key);
        fs::remove(entry_path, ec);
        auto manifest_path = asset_compiler::get_manifest_path(entry_path);
        fs::remove(manifest_path, ec);


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
auto watch_assets(rtti::context& ctx, const fs::path& dir, const fs::pattern_filter& filter, bool reload_async)
    -> uint64_t
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& ts = ctx.get_cached<threader>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    fs::path watch_dir = fs::path(dir).make_preferred();
    hpp::source_location loc = hpp::source_location::current();
    auto callback = [&am, &ts, &tm, &em, loc](const auto& entries, bool is_initial_list)
    {
        std::set<hpp::uuid> changed;
        std::set<hpp::uuid> removed;
        // Dependents of every UUID in `removed`, computed *before* unload.
        // Once `am.unload_asset` runs it invalidates the shared
        // `asset_link_t`, after which other handles (e.g. a material's
        // texture slot) report nil UUIDs and dependent enumeration silently
        // misses them. Capturing here preserves the still-valid chain.
        std::set<hpp::uuid> removed_dependents;

        for(const auto& entry : entries)
        {
            auto key = get_asset_key(entry.path);

            // APPLOG_TRACE_LOC(loc.file_name(), int(loc.line()), loc.function_name(), "{}", fs::to_string(entry));

            if(entry.type == fs::file_type::regular)
            {
                if(entry.status == fs::watcher::entry_status::removed)
                {
                    auto asset = am.find_asset<T>(key);
                    if(asset)
                    {
                        const auto uid = asset.uid();
                        removed.emplace(uid);

                        const auto deps = asset_deps::find_transitive_loaded_dependents(am, uid);
                        removed_dependents.insert(deps.begin(), deps.end());

                        am.unload_asset<T>(key);
                    }
                   
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
                        auto asset = am.get_asset<T>(key, flags, load_mode::deferred);
                        if(asset)
                        {
                            changed.emplace(asset.uid());
                        }
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
                        [&tm, &em, &am, changed, removed, removed_dependents]()
                        {
                            // A change in any of these types can visually
                            // affect a prefab thumbnail (texture in a
                            // material, material on a mesh, mesh in a
                            // model_component, animation on a prefab).
                            // Because prefabs are stored as opaque serialized
                            // buffers we don't walk them for explicit
                            // references — instead the cascade conservatively
                            // marks every loaded prefab thumbnail dirty when
                            // one of these renderable types changes.
                            constexpr bool affects_prefabs =
                                std::is_same_v<T, gfx::texture> ||
                                std::is_same_v<T, material> ||
                                std::is_same_v<T, mesh> ||
                                std::is_same_v<T, animation_clip>;

                            // Deletions: drop the removed thumbnails, regen
                            // every (pre-captured) dependent so missing-slot
                            // visuals propagate.
                            asset_deps::cascade_thumbnail_remove(am,
                                                                 tm,
                                                                 removed,
                                                                 removed_dependents,
                                                                 affects_prefabs);

                            for(const auto& uid : changed)
                            {
                                asset_deps::cascade_thumbnail_regen(am, tm, uid, affects_prefabs);

                                if constexpr(std::is_same<T, prefab>::value)
                                {
                                    auto asset = am.get_asset<T>(uid);
                                    em.on_prefab_updated(asset);
                                }
                            }
                        });
        }
    };

    std::stringstream ss;
    for(const auto& pattern : filter.get_include_patterns())
    {
        ss << pattern.get_pattern() << " ";
    }
    return fs::watcher::watch(watch_dir, filter, true, true, 500ms, callback, "Asset Watcher " + ss.str());
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
            // APPLOG_TRACE("{}", fs::to_string(entry));

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
                                                      auto assets = am.get_assets<T>();
                                                      for(const auto& asset : assets)
                                                      {
                                                          auto meta = am.get_metadata(asset.uid());
                                                          auto absolute_path = fs::resolve_protocol(meta.location);

                                                          if(has_depencency<T>(absolute_path, entry.path))
                                                          {
                                                              fs::watcher::touch(absolute_path, false);
                                                          }
                                                      }
                                                  });
                }
            }
        }
    };

    std::stringstream ss;
    for(const auto& pattern : filter.get_include_patterns())
    {
        ss << pattern.get_pattern() << " ";
    }
    return fs::watcher::watch(watch_dir, filter, true, true, 500ms, callback, "Asset Dependencies Watcher " + ss.str());
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
            if(is_initial_listing && !needs_recompilation<T>(ref_path, output))
            {
                continue;
            }

            auto key = get_asset_key(output);
            if(check_files_integrity(key, output))
            {
                auto job_name = fmt::format("Compiling {}", ex::get_type<T>());
                auto task = ts.pool->schedule(job_name,
                                              [&am, ref_path, output, job_name]()
                                              {
                                                  APPLOG_TRACE_PERF_NAMED_ALLOC(std::chrono::milliseconds, fmt::format("{} - {}", job_name, output.string()));
                                                  asset_compiler::compile<T>(am, ref_path, output);
                                              });
            }
        }
    };

    for(const auto& type : ex::get_suported_formats<T>())
    {
        syncer.set_mapping(type + ".meta",
                           {".asset"},
                           on_modified,
                           on_modified,
                           on_removed,
                           on_renamed);
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
            auto extension = output.extension().string();
            auto it =
                std::find(std::begin(platform_supported), std::end(platform_supported), extension);

            if(it == std::end(platform_supported))
            {
                continue;
            }

            if(is_initial_listing && !needs_recompilation<gfx::shader>(ref_path, output))
            {
                continue;
            }

            auto key = get_asset_key(output);
            if(check_files_integrity(key, output))
            {
                bool high_priority = gfx::get_current_renderer_filename_extension() == extension;
                tpp::priority::group priority = high_priority ? tpp::priority::normal() : tpp::priority::low();

                auto job_name = fmt::format("Compiling {}", ex::get_type<gfx::shader>() + "(" + extension + ")");
                auto task = ts.pool->schedule(job_name,
                                              priority,
                                              [&am, ref_path, output, job_name]()
                                              {
                                                //   APPLOG_TRACE_PERF_NAMED_ALLOC(std::chrono::milliseconds, fmt::format("{} - {}", job_name, output.string()));
                                                  asset_compiler::compile<gfx::shader>(am, ref_path, output);
                                              });
            }
        }
    };

    const auto& platform_supported = gfx::get_renderer_platform_supported_filename_extensions();
    std::vector<std::string> supported_extensions;
    supported_extensions.reserve(platform_supported.size());
    for(const auto& extension : platform_supported)
    {
        supported_extensions.push_back(".asset" + extension);
    }
    for(const auto& type : ex::get_suported_formats<gfx::shader>())
    {
        syncer.set_mapping(type + ".meta",
                           supported_extensions,
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
        const auto watch_id =
            watch_assets<gfx::shader>(ctx, dir, fs::pattern_filter("*" + type + ".asset" + renderer_extension), true);
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
                                      bool wait,
                                      const on_wait_progress_t& on_progress)
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
        [&am, this](const std::string& ext, const auto& ref_path, const auto& synced_paths, bool is_initial_listing)
    {
        for(const auto& synced_path : synced_paths)
        {
            asset_meta meta;
            fs::error_code err;
            if(fs::exists(synced_path, err))
            {
                load_from_file(synced_path.string(), meta);
            }

            bool recreate_meta_file = false;
            {
                std::lock_guard<std::mutex> lock(recreate_meta_files_queue_mutex_);
                if(!recreate_meta_files_queue_.empty())
                {
                    auto it = recreate_meta_files_queue_.find(ref_path.string());
                    if(it != recreate_meta_files_queue_.end())
                    {
                        recreate_meta_files_queue_.erase(it);
                        
                        recreate_meta_file = true;
                    }
                }
            }
            

            if(meta.uid.is_nil() || recreate_meta_file)
            {
                auto new_meta = am.get_metadata_for_path(ref_path).meta;

                if(new_meta.uid.is_nil())
                {
                    const auto protocol_path = fs::convert_to_protocol(ref_path);
                    new_meta = am.generate_metadata(protocol_path);
                    am.remove_asset_info_for_path(ref_path);
                }
                
                if(new_meta.uid != meta.uid)
                {
                    meta = new_meta;
                }
            }
            meta.uid = am.add_asset_info_for_path(ref_path, meta, true);

            // asset_writer::atomic_write_file(synced_path.string(), [&](const fs::path& temp) -> void
            // {
            //     save_to_file(temp.string(), meta);
            // }, err);

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

    for(const auto& dep_ex : ex::get_suported_dependencies_formats<ui_tree>())
    {
        auto id = watch_assets_depenencies<ui_tree>(ctx, data_dir, fs::pattern_filter("*" + dep_ex));
        watchers.emplace_back(id);
    }

    auto on_sync_progress = [on_progress](size_t completed, size_t total, const std::string& current_job)
    {
        if(on_progress)
        {
            on_progress(completed, total, "Indexing Assets Metadata " + current_job);
        }
    };

    syncer.sync(data_dir, meta_dir, on_sync_progress);

    if(wait)
    {
        auto& ts = ctx.get_cached<threader>();
        APPLOG_TRACE("Waiting for jobs to complete... (this may take a while)");

        
        ts.pool->wait_all_polling(tpp::priority::category::normal,
                          [&on_progress](const tpp::thread_pool::progress_info& info)
                          {
                              if(on_progress)
                              {
                                  on_progress(info.current_job, info.total_jobs, info.name);
                              }
                          });
    }
}

void asset_watcher::setup_cache_syncer(rtti::context& ctx,
                                       std::vector<uint64_t>& watchers,
                                       fs::syncer& syncer,
                                       const fs::path& meta_dir,
                                       const fs::path& cache_dir,
                                       bool wait,
                                       const on_wait_progress_t& on_progress)
{
    setup_directory(ctx, syncer);

    auto on_removed = [](const std::string& ext, const auto& /*ref_path*/, const auto& synced_paths)
    {
        for(const auto& synced_path : synced_paths)
        {
            auto synced_asset = remove_meta_tag(synced_path);

            auto manifest_path = asset_compiler::get_manifest_path(synced_asset);
            fs::error_code err;
            fs::remove_all(manifest_path, err);
            fs::remove_all(synced_asset, err);
        }
    };

    auto on_renamed = [](const std::string& ext, const auto& /*ref_path*/, const auto& synced_paths)
    {
        for(const auto& synced_path : synced_paths)
        {
            auto synced_old_asset = remove_meta_tag(synced_path.first);
            auto synced_new_asset = remove_meta_tag(synced_path.second);
            auto manifest_old_path = asset_compiler::get_manifest_path(synced_old_asset);
            auto manifest_new_path = asset_compiler::get_manifest_path(synced_new_asset);
            fs::error_code err;
            fs::rename(manifest_old_path, manifest_new_path, err);
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
    add_to_syncer<ui_tree>(ctx, syncer, on_removed, on_renamed);
    add_to_syncer<style_sheet>(ctx, syncer, on_removed, on_renamed);

    auto on_sync_progress = [on_progress](size_t completed, size_t total, const std::string& current_job)
    {
        if(on_progress)
        {
            on_progress(completed, total, "Indexing Compiled Assets " + current_job);
        }
    };
    syncer.sync(meta_dir, cache_dir, on_sync_progress);

    if(wait)
    {
        auto& ts = ctx.get_cached<threader>();
        APPLOG_TRACE("Waiting for jobs to complete... (this may take a while)");
        hpp::source_location loc = hpp::source_location::current();
        ts.pool->wait_all_polling(tpp::priority::category::normal,
                          [&on_progress](const tpp::thread_pool::progress_info& info)
                          {
                              if(on_progress)
                              {
                                  on_progress(info.current_job, info.total_jobs, info.name);
                              }
                          });
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
    watch_synced<ui_tree>(ctx, watchers, cache_dir);
    watch_synced<style_sheet>(ctx, watchers, cache_dir);
}

asset_watcher::asset_watcher()
{
}

asset_watcher::~asset_watcher()
{
}

void asset_watcher::on_os_event(rtti::context& ctx, os::event& e)
{
    if(e.type == os::events::window)
    {
        if(e.window.type == os::window_event_id::focus_lost)
        {
            if(!os::window::is_any_focused())
            {
                // APPLOG_TRACE("Application lost focus");
                // fs::watcher::pause();
            }
        }
        if(e.window.type == os::window_event_id::focus_gained)
        {
            if(os::window::is_any_focused())
            {
                // APPLOG_TRACE("Application gained focus");
                // fs::watcher::resume();
            }
        }
    }
}

auto asset_watcher::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ev = ctx.get_cached<events>();
    ev.on_os_event.connect(sentinel_, 1000, this, &asset_watcher::on_os_event);

    return true;
}

auto asset_watcher::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void asset_watcher::watch_assets(rtti::context& ctx,
                                 const std::string& protocol,
                                 bool wait,
                                 const on_wait_progress_t& on_progress)
{
    auto start_time = std::chrono::steady_clock::now();
    auto& w = watched_protocols_[protocol];

    auto data_protocol = ex::get_data_directory_no_slash(protocol);
    auto meta_protocol = ex::get_meta_directory_no_slash(protocol);
    auto cache_protocol = ex::get_compiled_directory_no_slash(protocol);

    setup_meta_syncer(ctx,
                      w.watchers,
                      w.meta_syncer,
                      fs::resolve_protocol(data_protocol),
                      fs::resolve_protocol(meta_protocol),
                      wait,
                      on_progress);

    setup_cache_syncer(ctx,
                       w.watchers,
                       w.cache_syncer,
                       fs::resolve_protocol(meta_protocol),
                       fs::resolve_protocol(cache_protocol),
                       wait,
                       on_progress);

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    APPLOG_TRACE("Asset watcher {} took {}ms to watch assets", protocol, elapsed_time.count());
}

void asset_watcher::unwatch_assets(rtti::context& ctx, const std::string& protocol)
{
    auto it = watched_protocols_.find(protocol);
    if(it == watched_protocols_.end())
    {
        return;
    }

    auto& w = it->second;

    unwatch(w.watchers);
    w.meta_syncer.unsync();
    w.cache_syncer.unsync();

    watched_protocols_.erase(it);

    auto& am = ctx.get_cached<asset_manager>();
    am.unload_group(protocol);
}

auto asset_watcher::get_pending_jobs_count(rtti::context& ctx) const -> size_t
{
    auto& ts = ctx.get_cached<threader>();
    return ts.pool->get_jobs_count();
}

void asset_watcher::recreate_meta_files(rtti::context& ctx, const std::string& protocol)
{
    auto& am = ctx.get_cached<asset_manager>();
    auto assets = am.get_all_assets(protocol);

    std::lock_guard<std::mutex> lock(recreate_meta_files_queue_mutex_);
    for(auto& asset : assets)
    {

        auto path = fs::resolve_protocol(asset);
        recreate_meta_files_queue_[path.string()] = true;
    }

    for(auto& kvp : recreate_meta_files_queue_)
    {
        fs::watcher::touch(fs::resolve_protocol(kvp.first), false);
    }
}

} // namespace unravel
