#include "deploy_pipeline.h"
#include "deploy_android.h"
#include "deploy_preflight.h"
#include "deploy_runtime.h"
#include "web_preview_server.h"

#include <asset_compiler/audio_compression.h>
#include <deploy/audio_variants.h>
#include <deploy/content.h>
#include <deploy/dependencies.h>
#include <deploy/files.h>
#include <deploy/icon_image.h>
#include <deploy/keys.h>
#include <deploy/profile_cache.h>
#include <deploy/protocol_data.h>
#include <deploy/releases.h>
#include <deploy/run_arguments.h>
#include <deploy/shader_cook.h>
#include <deploy/texture_variants.h>
#include <deploy/web.h>
#include <deploy/windows_resources.h>
#include <editor/editing/prefab_bake.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/meta/settings/settings.hpp>
#include <logging/logging.h>
#include <ospp/url.h>
#include <process/process.h>

// must be below all
#include <engine/assets/impl/asset_writer.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
// Step names, as the Deploy window and the deploy tool list them.
constexpr const char* STEP_CHECK_ASSETS = "Checking Assets";
constexpr const char* STEP_COOK = "Cooking Prefabs";
constexpr const char* STEP_COOK_TEXTURES = "Cooking Textures";
constexpr const char* STEP_COOK_AUDIO = "Cooking Audio";
constexpr const char* STEP_COOK_SHADERS = "Cooking Shaders";
constexpr const char* STEP_DEPENDENCIES = "Dependencies";
constexpr const char* STEP_SETTINGS = "Project Settings";
constexpr const char* STEP_PROJECT_DATA = "Project Data";
constexpr const char* STEP_ENGINE_DATA = "Engine Data";
constexpr const char* STEP_ANDROID_PACKAGE = "Android Package";
constexpr const char* STEP_WEB_PLAYER = "Browser Player";
/// Uncompiled assets named in the failed step before "and N more".
constexpr std::size_t LISTED_UNCOMPILED_MAX = 10;
constexpr const char* COOK_STAGING_PREFIX = "unravel-deploy-cook-";
constexpr const char* DEPENDENCY_SCANNER = "editor:/tools/dependencies/Dependencies.exe";
constexpr const char* SETTINGS_FOLDER = "settings";
constexpr const char* SETTINGS_FILE = "settings.cfg";
/// The player's symbols beside it, where its build put them (Windows).
constexpr const char* PLAYER_SYMBOLS_EXTENSION = ".pdb";

std::atomic<int> running_deploys{0};

/// Everything the steps of one deploy read, resolved on the main thread before they start.
struct deploy_plan
{
    deploy_settings params;
    /// The profile's folder in the deploy folder.
    fs::path output;
    fs::path executable;
    fs::path player;
    /// The player's symbols; missing where its build leaves none beside it.
    fs::path player_symbols;
    /// The profile's cache of the build's debug files, which Keep as Release takes along.
    fs::path symbols;
    /// The image of the profile's executable icon; empty: the player keeps the engine's.
    fs::path icon_image;
    /// What the executable says of itself (Windows): the project's product, company and version.
    windows_version_info version_info;
    fs::path dependency_scanner;
    /// The project settings the profile runs with: what the game loads.
    settings runtime_settings;
    /// The build's data/app/settings folder.
    fs::path settings_folder;
    protocol_job app;
    protocol_job engine;
    runtime_plan runtime;
    fs::path cook_staging;
    /// The project's textures and the engine's, each cooked to the profile's formats.
    texture_cook_plan textures;
    texture_cook_plan engine_textures;
    /// The project's audio clips the profile compiles differently from the editor.
    audio_cook_plan audio;
    shader_cook_plan shaders;
    /// Android deploys only: the package built from the deployed data.
    android_package_plan android;
    /// Web deploys only: the browser player copied beside the data.
    web_template web_player;
};

auto is_android(const deploy_plan& plan) -> bool
{
    return plan.params.deploy_target == deploy_platform::android;
}

auto is_web(const deploy_plan& plan) -> bool
{
    return plan.params.deploy_target == deploy_platform::web;
}

struct deploy_step
{
    std::string name;
    std::function<void()> run;
};

/// A deploy refused before the folder is touched: one step that fails with the reason.
auto make_failed_step(threader& th, const std::string& name, const std::string& reason) -> deploy_jobs
{
    APPLOG_ERROR("Deploy aborted: {}", reason);
    auto job = th.pool
                   ->schedule(name,
                              [reason]()
                              {
                                  throw std::runtime_error(reason);
                              })
                   .share();
    return {{name, job}};
}

/// Shipped assets of a protocol's index without their compiled files. With the compiler idle that is a
/// source it failed to compile, and a build without it silently lacks the asset. Shaders for the renderers the
/// deploy cooks have no compiled file: cooking them checks them.
void find_uncompiled_assets(const asset_database::database_t& database,
                            const fs::path& protocol_root,
                            const std::unordered_set<std::string>& left_out,
                            const std::set<std::string>& shader_variants,
                            std::vector<std::string>& uncompiled)
{
    const auto cooked_variants = get_cooked_shader_variants(shader_variants);
    for(const auto& [uid, meta] : database)
    {
        if(left_out.contains(fs::make_path_key(meta.location)))
        {
            continue;
        }
        const auto paths = get_compiled_paths(meta.location, protocol_root, shader_variants);
        const bool is_missing = std::any_of(paths.begin(),
                                            paths.end(),
                                            [&cooked_variants](const fs::path& path)
                                            {
                                                fs::error_code ec;
                                                return !cooked_variants.contains(path.extension().generic_string()) &&
                                                       !fs::exists(path, ec);
                                            });
        if(is_missing)
        {
            uncompiled.push_back(meta.location);
        }
    }
}

auto describe_uncompiled_assets(const std::vector<std::string>& uncompiled) -> std::string
{
    return fmt::format("{} asset(s) have no compiled file (the log says why they failed to compile). Fix or remove "
                       "them and deploy again: {}",
                       uncompiled.size(),
                       list_keys(uncompiled, LISTED_UNCOMPILED_MAX));
}

/// A folder no other deploy uses: two editors cooking at once must not share one.
auto make_cook_staging() -> fs::path
{
    std::random_device device;
    return fs::temp_directory_path() /
           fmt::format("{}{}-{:08x}", COOK_STAGING_PREFIX, process::get_current_process_id(), device());
}

/// The path keys of the project and engine assets the content selection leaves out; empty when everything ships.
auto find_left_out_assets(asset_manager& am, const deploy_settings& params, const settings& shipped_settings)
    -> std::unordered_set<std::string>
{
    if(!is_filtered(params))
    {
        return {};
    }
    const auto source = make_project_content_source(am, shipped_settings);
    const auto found = needs_walk(params) ? detect_content(source, params, shipped_settings.standalone.startup_scene.id())
                                          : list_content(source);
    auto left_out = get_left_out_keys(params, found);
    APPLOG_INFO("Deploy: shipping {} of {} project and engine assets; {} left out (see the Deploy window's Content tab).",
                found.entries.size() - left_out.size(),
                found.entries.size(),
                left_out.size());
    return left_out;
}

/// Cooks the prefabs into `staging`; why it could not, or empty.
auto cook_prefabs(rtti::context& ctx, const deploy_settings& params, const fs::path& staging) -> std::string
{
    if(!params.deploy_cooked_assets)
    {
        clear_prefab_nesting_marker_once(ctx);
        return {};
    }
    size_t baked = 0;
    const auto report = bake_prefab_nesting(ctx, staging, &baked);
    if(!report.is_valid())
    {
        return report.describe_problem();
    }
    APPLOG_INFO("Deploy: cooked {} asset(s) into staging.", baked);
    return {};
}

auto make_protocol_job(asset_manager& am,
                       const deploy_settings& params,
                       const std::string& protocol,
                       cooked_folders cooked,
                       std::unordered_set<std::string> left_out) -> protocol_job
{
    protocol_job job;
    job.protocol = protocol;
    job.compiled_root = fs::resolve_protocol(ex::get_compiled_directory(protocol));
    job.cooked = std::move(cooked);
    job.protocol_root = params.deploy_location / DATA_FOLDER / protocol;
    job.database = am.get_database_snapshot(protocol + ":/");
    job.left_out = std::move(left_out);
    job.is_packed = params.deploy_packed_assets;
    job.with_debug_symbols = params.deploy_debug_symbols;
    job.shader_variants = get_shader_variants(params);
    // An Android package and a web build carry every pack whole: patches are for desktop deploys.
    if(!params.deploy_patch_release.empty() && params.deploy_target == deploy_platform::desktop)
    {
        job.patch_release_pack =
            get_release_pack(get_profile_releases_folder(get_project_root(), params.profile), params.deploy_patch_release, protocol);
    }
    return job;
}

/// The version information of the game's executable: the project's product (the executable's name when it
/// names none, as the game itself does), company and version.
auto make_version_info(const settings::app_settings& app, const fs::path& executable) -> windows_version_info
{
    windows_version_info info;
    info.version = app.version;
    info.product_name = app.product.empty() ? fs::to_utf8_string(executable.stem()) : app.product;
    info.company_name = app.company;
    info.original_filename = fs::to_utf8_string(executable.filename());
    return info;
}

auto make_plan(rtti::context& ctx, const deploy_settings& params, const fs::path& staging, std::unordered_set<std::string> left_out)
    -> deploy_plan
{
    auto& am = ctx.get_cached<asset_manager>();
    deploy_plan plan;
    plan.params = params;
    plan.output = get_output_location(params);
    plan.runtime_settings = ctx.get_cached<project_manager>().get_profile_settings(params.profile);
    // The data steps write data/ into the profile's folder; for Android into the build folder, where Gradle
    // packages it, so the profile's folder holds the package only.
    auto data_params = params;
    data_params.deploy_location = is_android(plan) ? get_android_content_dir(params.profile) : plan.output;
    plan.executable = get_deployed_executable(ctx, params);
    plan.player = get_player_file();
    plan.player_symbols = fs::path(plan.player).replace_extension(PLAYER_SYMBOLS_EXTENSION);
    plan.symbols = get_profile_symbols_folder(get_project_root(), params.profile);
    plan.icon_image = params.desktop.icon ? fs::resolve_protocol(params.desktop.icon.id()) : fs::path{};
    plan.version_info = make_version_info(plan.runtime_settings.app, plan.executable);
    plan.dependency_scanner = fs::resolve_protocol(DEPENDENCY_SCANNER);
    plan.settings_folder = data_params.deploy_location / DATA_FOLDER / CONTENT_PROTOCOL / SETTINGS_FOLDER;
    plan.textures = make_texture_cook_plan(get_project_root(),
                                           params,
                                           plan.runtime_settings,
                                           CONTENT_PROTOCOL,
                                           am.get_database_snapshot("app:/"),
                                           left_out,
                                           asset_compiler::get_editor_texture_target());
    plan.engine_textures = make_texture_cook_plan(get_project_root(),
                                                  params,
                                                  plan.runtime_settings,
                                                  ENGINE_PROTOCOL,
                                                  am.get_database_snapshot("engine:/"),
                                                  left_out,
                                                  asset_compiler::get_editor_texture_target_for("engine:/"));
    plan.audio = make_audio_cook_plan(get_project_root(),
                                      params,
                                      plan.runtime_settings,
                                      am.get_database_snapshot("app:/"),
                                      left_out,
                                      asset_compiler::get_editor_audio_defaults());
    plan.shaders = make_shader_cook_plan(get_project_root(),
                                         params,
                                         am.get_database_snapshot("engine:/"),
                                         am.get_database_snapshot("app:/"),
                                         left_out);
    // The cooked prefabs, the texture and audio variants and the cooked shaders: no file is in two of them, so their
    // order does not matter.
    const auto engine_shaders = plan.shaders.has_work() ? plan.shaders.protocols.front().variants : fs::path{};
    const auto app_shaders = plan.shaders.has_work() ? plan.shaders.protocols.back().variants : fs::path{};
    plan.app = make_protocol_job(am,
                                 data_params,
                                 CONTENT_PROTOCOL,
                                 {staging / CONTENT_PROTOCOL, plan.textures.variants, plan.audio.variants, app_shaders},
                                 left_out);
    plan.engine = make_protocol_job(am,
                                    data_params,
                                    ENGINE_PROTOCOL,
                                    {staging / ENGINE_PROTOCOL, plan.engine_textures.variants, engine_shaders},
                                    std::move(left_out));
    // The browser player carries its .NET runtime and the script bridge.
    if(!is_web(plan))
    {
        plan.runtime = make_runtime_plan(ctx, data_params);
    }
    plan.cook_staging = staging;
    if(is_android(plan))
    {
        // The managed bridge ships as for the desktop; the .NET runtime comes with the Android player.
        plan.runtime.host_source.clear();
        plan.runtime.framework_source.clear();
        plan.android = make_android_package_plan(ctx, params);
    }
    if(is_web(plan))
    {
        plan.web_player = get_web_template().found;
    }
    return plan;
}

/// The profile's icon image; empty for a profile without one.
auto load_profile_icon(const deploy_plan& plan) -> icon_image
{
    if(plan.icon_image.empty())
    {
        return {};
    }
    auto image = load_icon_image(plan.icon_image);
    if(image.is_empty())
    {
        throw std::runtime_error(fmt::format("The icon image {} could not be read as an image.", plan.icon_image.generic_string()));
    }
    return image;
}

/// Writes the profile's icon and the project's version information into the game's copy of the player, so
/// Explorer, the taskbar and crash reports show the game, not the engine. Elsewhere an executable carries
/// neither: the icon goes beside it as the image the game shows on its window, and a deploy without an icon
/// removes the one an earlier deploy wrote.
void write_executable_resources(const deploy_plan& plan)
{
    const auto image = load_profile_icon(plan);
#ifdef _WIN32
    const auto icon = image.is_empty() ? std::vector<windows_icon_image>{} : make_windows_icon_images(image);
    const auto written = set_windows_executable_resources(plan.executable, icon, plan.version_info);
    if(!written.is_ok())
    {
        throw std::runtime_error(written.error);
    }
#else
    const auto file = plan.output / ex::get_window_icon_file_name();
    fs::error_code ec;
    if(image.is_empty())
    {
        fs::remove(file, ec);
        return;
    }
    if(!write_window_icon(file, image))
    {
        throw std::runtime_error(fmt::format("Writing {} failed", file.generic_string()));
    }
#endif
}

/// Keeps the executable and the player's symbols in the profile's cache, never in the build: a crash dump from
/// players is read with the executable and symbols of its own build, which Keep as Release stores.
void keep_build_debug_files(const deploy_plan& plan)
{
    std::vector<fs::path> files{plan.executable};
    fs::error_code ec;
    if(fs::exists(plan.player_symbols, ec))
    {
        files.push_back(plan.player_symbols);
    }
    const auto kept = keep_debug_files(files, plan.symbols);
    if(!kept.is_ok())
    {
        throw std::runtime_error(kept.error);
    }
}

/// The player renamed after the project, and the libraries it loads beside it; binaries an older build
/// left at the top of the folder are removed.
void deploy_dependencies(const deploy_plan& plan)
{
    std::set<std::string> shipped;
    for(const auto& dependency : find_dependencies(plan.player, plan.dependency_scanner))
    {
        const fs::path library(dependency);
        copy_file_or_throw(library, plan.output / library.filename());
        shipped.insert(get_binary_key(library));
    }
    copy_file_or_throw(plan.player, plan.executable);
    write_executable_resources(plan);
    keep_build_debug_files(plan);
    shipped.insert(get_binary_key(plan.executable));
    remove_stale_binaries(plan.output, shipped);
}

/// The settings the game loads: the project settings with the profile's overrides and its build section.
/// Nothing else ships in the folder, so neither profiles nor files left from an earlier build reach the game.
void deploy_settings_file(const deploy_plan& plan)
{
    fs::error_code ec;
    fs::create_directories(plan.settings_folder, ec);
    const auto file = plan.settings_folder / SETTINGS_FILE;
    if(!asset_writer::atomic_save_to_file(file.string(), plan.runtime_settings))
    {
        throw std::runtime_error(fmt::format("Writing {} failed", file.generic_string()));
    }
    for(const auto& entry : fs::directory_iterator(plan.settings_folder, ec))
    {
        if(entry.path().filename() != SETTINGS_FILE)
        {
            fs::remove_all(entry.path(), ec);
        }
    }
}

/// The steps that need nothing but the plan; the data steps wait for the cooks instead.
auto make_steps(const std::shared_ptr<const deploy_plan>& plan) -> std::vector<deploy_step>
{
    std::vector<deploy_step> steps;
    // The player executable and its libraries are the desktop's; Android's come with the package, the browser
    // player's in its own step.
    if(plan->params.deploy_dependencies && plan->params.deploy_target == deploy_platform::desktop)
    {
        steps.push_back({STEP_DEPENDENCIES, [plan]() { deploy_dependencies(*plan); }});
    }
    steps.push_back({STEP_SETTINGS, [plan]() { deploy_settings_file(*plan); }});
    // The browser player carries its .NET runtime and the script bridge.
    if(!is_web(*plan))
    {
        steps.push_back({plan->runtime.step_name, [plan]() { deploy_runtime(plan->runtime); }});
    }
    return steps;
}

/// The browser player beside the deployed data, and the list of the data's files the page loads.
void deploy_web_site(const deploy_plan& plan)
{
    if(plan.params.deploy_dependencies)
    {
        copy_web_player(plan.web_player, plan.output);
    }
    const auto& app = plan.runtime_settings.app;
    write_web_data_manifest(plan.output, {plan.version_info.product_name, get_web_user_folder(app.company, app.product)});
}

auto schedule_step(threader& th, const deploy_step& step) -> tpp::shared_future<void>
{
    return th.pool
        ->schedule("Deploying " + step.name,
                   [step]()
                   {
                       APPLOG_INFO("Deploying {}...", step.name);
                       step.run();
                       APPLOG_INFO("Deploying {} - Done", step.name);
                   })
        .share();
}

/// A step that packs what cooking steps made, so it starts once they all succeeded.
auto schedule_after_cooks(const deploy_step& step, const deploy_jobs& cooks) -> tpp::shared_future<void>
{
    std::vector<tpp::shared_future<void>> futures;
    for(const auto& cook : cooks)
    {
        futures.push_back(cook.job);
    }
    return tpp::when_all(futures.begin(), futures.end())
        .then(
            [step, cooks](auto)
            {
                for(const auto& cook : cooks)
                {
                    if(!get_step_error(cook.job).empty())
                    {
                        throw std::runtime_error(fmt::format("Skipped: {} failed.", cook.name));
                    }
                }
                APPLOG_INFO("Deploying {}...", step.name);
                step.run();
                APPLOG_INFO("Deploying {} - Done", step.name);
            })
        .share();
}

auto has_failed_step(const std::vector<tpp::shared_future<void>>& steps) -> bool
{
    return std::any_of(steps.begin(),
                       steps.end(),
                       [](const tpp::shared_future<void>& step)
                       {
                           return !get_step_error(step).empty();
                       });
}

/// A browser loads the game only from a web server: the editor serves the deploy's folder on this computer, under a
/// path of the project and profile, and opens the page in the default browser.
void open_web_deploy(const deploy_plan& plan)
{
    const auto site_path = get_project_site_path(plan.params.profile);
    const auto origin = get_web_preview_server().serve(plan.output, site_path);
    if(origin.empty())
    {
        APPLOG_ERROR("Deploy and Run could not serve {}: no port on this computer could be opened.",
                     plan.output.generic_string());
        fs::show_in_graphical_env(plan.output);
        return;
    }
    const auto page = make_web_page_address(origin, site_path, split_run_arguments(plan.params.deploy_run_arguments));
    APPLOG_INFO("Serving {} at {} until the editor closes.", plan.output.generic_string(), page);
    if(!os::url::open(page))
    {
        APPLOG_WARNING("Open {} in a browser to play the deploy.", page);
    }
}

/// Runs on the main thread once every step ended. A failed step left a partial build: the Deploy window
/// names it, and running or showing that build would only hide the failure.
void finish_deploy(const deploy_plan& plan, const std::vector<tpp::shared_future<void>>& steps)
{
    fs::error_code ec;
    fs::remove_all(plan.cook_staging, ec);
    --running_deploys;
    if(has_failed_step(steps))
    {
        APPLOG_ERROR("Deploy to {} failed. See the Deploy window for the step.", get_output_location(plan.params).generic_string());
        return;
    }
    // An Android game was started by the package step, and its session finishes the device steps.
    if(plan.params.deploy_and_run)
    {
        if(is_web(plan))
        {
            open_web_deploy(plan);
        }
        else if(!is_android(plan))
        {
            run_game(plan.executable, split_run_arguments(plan.params.deploy_run_arguments));
        }
        return;
    }
    fs::show_in_graphical_env(get_output_location(plan.params));
}

/// The browser player step: the list of files it writes is the deployed data's, so it starts once every other
/// step succeeded.
auto schedule_web_player(const std::shared_ptr<const deploy_plan>& plan, const std::vector<tpp::shared_future<void>>& steps)
    -> tpp::shared_future<void>
{
    return tpp::when_all(steps.begin(), steps.end())
        .then(
            [plan](auto all)
            {
                if(has_failed_step(all.get()))
                {
                    throw std::runtime_error("Skipped: an earlier step failed.");
                }
                APPLOG_INFO("Deploying {}...", STEP_WEB_PLAYER);
                deploy_web_site(*plan);
                APPLOG_INFO("Deploying {} - Done", STEP_WEB_PLAYER);
            })
        .share();
}

/// The Android package step: it builds from what the other steps deployed, so it starts once they all
/// succeeded.
auto schedule_android_package(const std::shared_ptr<const deploy_plan>& plan, const std::vector<tpp::shared_future<void>>& steps)
    -> tpp::shared_future<void>
{
    return tpp::when_all(steps.begin(), steps.end())
        .then(
            [plan](auto all)
            {
                const std::string skipped = "Skipped: an earlier step failed.";
                if(has_failed_step(all.get()))
                {
                    fail_device_steps(plan->android, skipped);
                    throw std::runtime_error(skipped);
                }
                APPLOG_INFO("Deploying {}...", STEP_ANDROID_PACKAGE);
                try
                {
                    build_android_package(plan->android);
                }
                catch(...)
                {
                    fail_device_steps(plan->android, skipped);
                    throw;
                }
                APPLOG_INFO("Deploying {} - Done", STEP_ANDROID_PACKAGE);
                // Deploy and Run: the session installs and starts the game and finishes the device steps.
                if(plan->params.deploy_and_run)
                {
                    run_android_game(plan->android);
                }
            })
        .share();
}
} // namespace

auto deploy_project(rtti::context& ctx, const deploy_settings& params) -> deploy_jobs
{
    auto& th = ctx.get_cached<threader>();
    auto& am = ctx.get_cached<asset_manager>();
    const auto shipped_settings = ctx.get_cached<project_manager>().get_profile_settings(params.profile);
    auto left_out = find_left_out_assets(am, params, shipped_settings);
    // Before anything touches the destination, so a refused deploy leaves the previous build there.
    std::vector<std::string> uncompiled;
    const auto shader_variants = get_shader_variants(params);
    find_uncompiled_assets(am.get_database_snapshot("engine:/"), fs::resolve_protocol("engine:/"), left_out, shader_variants, uncompiled);
    find_uncompiled_assets(am.get_database_snapshot("app:/"), get_project_root(), left_out, shader_variants, uncompiled);
    if(!uncompiled.empty())
    {
        return make_failed_step(th, STEP_CHECK_ASSETS, describe_uncompiled_assets(uncompiled));
    }
    const auto staging = make_cook_staging();
    const auto cook_error = cook_prefabs(ctx, params, staging);
    if(!cook_error.empty())
    {
        fs::error_code ec;
        fs::remove_all(staging, ec);
        return make_failed_step(th, STEP_COOK, cook_error);
    }
    fs::error_code ec;
    fs::create_directories(get_output_location(params), ec);
    if(params.deploy_target == deploy_platform::android)
    {
        fs::create_directories(get_android_content_dir(params.profile), ec);
    }
    const auto plan = std::make_shared<const deploy_plan>(make_plan(ctx, params, staging, std::move(left_out)));
    ++running_deploys;
    deploy_jobs jobs;
    std::vector<tpp::shared_future<void>> futures;
    const auto add_job = [&](const std::string& name, const tpp::shared_future<void>& job)
    {
        jobs.push_back({name, job});
        futures.push_back(job);
    };
    // Each data step packs what the cooks of its protocol made: its textures, the project's audio, and its shaders.
    const auto textures_cook = schedule_step(th,
                                             {STEP_COOK_TEXTURES,
                                              [plan]()
                                              {
                                                  cook_textures(plan->textures);
                                                  cook_textures(plan->engine_textures);
                                              }});
    const auto audio_cook = schedule_step(th, {STEP_COOK_AUDIO, [plan]() { cook_audio(plan->audio); }});
    deploy_jobs engine_cooks{{STEP_COOK_TEXTURES, textures_cook}};
    deploy_jobs app_cooks{{STEP_COOK_TEXTURES, textures_cook}, {STEP_COOK_AUDIO, audio_cook}};
    if(plan->shaders.has_work())
    {
        engine_cooks.push_back({STEP_COOK_SHADERS, schedule_step(th, {STEP_COOK_SHADERS, [plan]() { cook_shaders(plan->shaders); }})});
        app_cooks.push_back(engine_cooks.back());
    }
    add_job(STEP_COOK_TEXTURES, textures_cook);
    add_job(STEP_COOK_AUDIO, audio_cook);
    if(plan->shaders.has_work())
    {
        add_job(STEP_COOK_SHADERS, engine_cooks.back().job);
    }
    for(const auto& step : make_steps(plan))
    {
        add_job(step.name, schedule_step(th, step));
    }
    add_job(STEP_ENGINE_DATA, schedule_after_cooks({STEP_ENGINE_DATA, [plan]() { deploy_protocol_data(plan->engine); }}, engine_cooks));
    add_job(STEP_PROJECT_DATA, schedule_after_cooks({STEP_PROJECT_DATA, [plan]() { deploy_protocol_data(plan->app); }}, app_cooks));
    if(is_web(*plan))
    {
        add_job(STEP_WEB_PLAYER, schedule_web_player(plan, futures));
    }
    if(is_android(*plan))
    {
        add_job(STEP_ANDROID_PACKAGE, schedule_android_package(plan, futures));
        for(auto& device_job : get_device_jobs(plan->android))
        {
            futures.push_back(device_job.job);
            jobs.push_back(std::move(device_job));
        }
    }
    tpp::when_all(futures.begin(), futures.end())
        .then(tpp::this_thread::get_id(),
              [plan](auto all)
              {
                  finish_deploy(*plan, all.get());
              });
    return jobs;
}

auto is_deploy_running() -> bool
{
    return running_deploys.load() > 0;
}

auto get_last_lines(const std::string& output, std::size_t count) -> std::string
{
    std::size_t begin = output.size();
    for(std::size_t found = 0; begin > 0 && found <= count; --begin)
    {
        if(output[begin - 1] == '\n')
        {
            ++found;
        }
    }
    return output.substr(begin);
}

/// A ready future rethrows its job's exception.
auto get_step_error(const tpp::shared_future<void>& step) -> std::string
{
    if(!step.is_ready())
    {
        return {};
    }
    try
    {
        step.get();
    }
    catch(const std::exception& ex)
    {
        return ex.what();
    }
    catch(...)
    {
        return "unknown error";
    }
    return {};
}

void run_game(const fs::path& executable, const std::vector<std::string>& arguments)
{
    std::vector<std::string> command{fs::to_utf8_string(executable)};
    command.insert(command.end(), arguments.begin(), arguments.end());
    // The game runs on its own: the editor neither waits for it nor reads its output, and the game writes
    // its log beside itself rather than into the editor's folder.
    const auto error = process::spawn_detached(command, fs::to_utf8_string(executable.parent_path()));
    if(error)
    {
        APPLOG_ERROR("Running {} failed: {}", executable.generic_string(), error.message());
    }
}

} // namespace unravel::deployment
