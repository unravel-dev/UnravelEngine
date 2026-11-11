#include "deploy_pipeline.h"
#include "deploy_android.h"
#include "deploy_preflight.h"
#include "deploy_runtime.h"
#include "web_preview_server.h"

#include <asset_compiler/audio_compression.h>
#include <deploy/audio_variants.h>
#include <deploy/cancellation.h>
#include <deploy/content.h>
#include <deploy/dependencies.h>
#include <deploy/files.h>
#include <deploy/folders.h>
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
#include <engine/scripting/ecs/systems/script_system.h>
#include <logging/logging.h>
#include <ospp/url.h>
#include <process/process.h>
#include <version/version.h>

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
constexpr const char* STEP_FOLDER = "Deploy Folder";
constexpr const char* STEP_COOK_TEXTURES = "Cooking Textures";
constexpr const char* STEP_COOK_ENGINE_TEXTURES = "Cooking Engine Textures";
constexpr const char* STEP_COOK_AUDIO = "Cooking Audio";
constexpr const char* STEP_COOK_SHADERS = "Cooking Shaders";
constexpr const char* STEP_COMPILE_SCRIPTS = "Compiling Scripts";
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
    /// The project's script library as the deploy builds it, in the cooked prefabs' folder of the staging.
    fs::path script_library;
    /// The project's textures and the engine's, one plan per texture set: the first cooked to the profile's formats.
    /// A packed web deploy with a phone texture format of its own adds the textures cooked to it, a second set the page
    /// downloads where the browser's GPU does not read the first.
    std::vector<texture_cook_plan> textures;
    std::vector<texture_cook_plan> engine_textures;
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
                            const left_out_assets& left_out,
                            const std::set<std::string>& shader_variants,
                            std::vector<std::string>& uncompiled)
{
    const auto cooked_variants = get_cooked_shader_variants(shader_variants);
    for(const auto& [uid, meta] : database)
    {
        if(left_out.contains(meta.location))
        {
            continue;
        }
        const auto paths = get_compiled_paths(meta.location, protocol_root, shader_variants);
        const bool is_missing = std::any_of(paths.begin(),
                                            paths.end(),
                                            [&cooked_variants](const fs::path& path)
                                            {
                                                return !cooked_variants.contains(path.extension().generic_string()) &&
                                                       !is_present(path);
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

/// The project and engine assets the content selection leaves out; none when everything ships.
auto find_left_out_assets(asset_manager& am, const deploy_settings& params, const settings& shipped_settings)
    -> left_out_assets
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
                       left_out_assets left_out) -> protocol_job
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

/// A packed web deploy whose phone texture format differs from its texture format ships both sets: the protocols'
/// packs hold no texture, and each set's pack holds them as the set compiles them.
void add_texture_sets(deploy_plan& plan)
{
    const auto sets = get_web_texture_sets(plan.params.web);
    if(!is_web(plan) || !plan.params.deploy_packed_assets || sets.size() < 2)
    {
        return;
    }
    const auto second = sets.back();
    plan.textures.push_back(make_texture_set_cook_plan(plan.textures.front(), get_project_root(), CONTENT_PROTOCOL, second));
    plan.engine_textures.push_back(
        make_texture_set_cook_plan(plan.engine_textures.front(), get_project_root(), ENGINE_PROTOCOL, second));
    const auto first_name = get_texture_set_name(sets.front());
    const auto second_name = get_texture_set_name(second);
    APPLOG_INFO("Deploy: profile {} ships two texture sets, {} for the browsers of computers and {} for the phones and "
                "tablets that cannot read it; the page downloads one.",
                plan.params.profile,
                first_name,
                second_name);
    plan.app.texture_sets = {{first_name, {plan.textures[0].variants}}, {second_name, {plan.textures[1].variants}}};
    plan.engine.texture_sets = {{first_name, {plan.engine_textures[0].variants}}, {second_name, {plan.engine_textures[1].variants}}};
}

/// A browser keeps a game's saves per web address under the game's company and product, and a game without a product
/// name would share one folder with every other such game at that address: a web build ships the project's name as
/// its product then, which both the page and the game name the folder by.
void name_web_game(rtti::context& ctx, deploy_plan& plan)
{
    auto& app = plan.runtime_settings.app;
    if(is_web(plan) && app.product.empty())
    {
        app.product = ctx.get_cached<project_manager>().get_name();
    }
}

auto make_plan(rtti::context& ctx, const deploy_settings& params, const fs::path& staging, left_out_assets left_out)
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
    name_web_game(ctx, plan);
    plan.dependency_scanner = fs::resolve_protocol(DEPENDENCY_SCANNER);
    plan.settings_folder = data_params.deploy_location / DATA_FOLDER / CONTENT_PROTOCOL / SETTINGS_FOLDER;
    plan.textures = {make_texture_cook_plan(get_project_root(),
                                            params,
                                            plan.runtime_settings,
                                            CONTENT_PROTOCOL,
                                            am.get_database_snapshot("app:/"),
                                            left_out,
                                            asset_compiler::get_editor_texture_target())};
    plan.engine_textures = {make_texture_cook_plan(get_project_root(),
                                                   params,
                                                   plan.runtime_settings,
                                                   ENGINE_PROTOCOL,
                                                   am.get_database_snapshot("engine:/"),
                                                   left_out,
                                                   asset_compiler::get_editor_texture_target_for("engine:/"))};
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
                                 {staging / CONTENT_PROTOCOL, plan.textures.front().variants, plan.audio.variants, app_shaders},
                                 left_out);
    plan.engine = make_protocol_job(am,
                                    data_params,
                                    ENGINE_PROTOCOL,
                                    {staging / ENGINE_PROTOCOL, plan.engine_textures.front().variants, engine_shaders},
                                    std::move(left_out));
    add_texture_sets(plan);
    plan.app.shader_sets = get_web_shader_sets(params);
    plan.engine.shader_sets = plan.app.shader_sets;
    // The browser player carries its .NET runtime and the script bridge.
    if(!is_web(plan))
    {
        plan.runtime = make_runtime_plan(ctx, data_params);
    }
    plan.cook_staging = staging;
    plan.script_library = staging / CONTENT_PROTOCOL / script_system::get_lib_name(CONTENT_PROTOCOL);
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
    if(is_present(plan.player_symbols))
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
    prune_folder_or_throw(plan.settings_folder,
                          [](const fs::path& name)
                          {
                              return name == SETTINGS_FILE;
                          });
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
    web_page_info page;
    page.title = plan.version_info.product_name;
    page.user_folder = get_web_user_folder(app.company, app.product);
    for(const auto& set : plan.app.texture_sets)
    {
        page.texture_sets.push_back(set.name);
    }
    for(const auto& set : plan.app.shader_sets)
    {
        page.shader_sets.push_back(set.name);
    }
    page.graphics = make_web_graphics_policy(plan.params.web);
    write_web_data_manifest(plan.output, page);
}

/// Runs `step` here, between its log lines; a cancelled deploy starts no step.
void run_step(const deploy_step& step)
{
    throw_if_deploy_cancelled();
    APPLOG_INFO("Deploying {}...", step.name);
    step.run();
    APPLOG_INFO("Deploying {} - Done", step.name);
}

auto schedule_step(threader& th, const deploy_step& step) -> deploy_job
{
    return {step.name,
            th.pool
                ->schedule("Deploying " + step.name,
                           [step]()
                           {
                               run_step(step);
                           })
                .share()};
}

auto get_futures(const deploy_jobs& jobs) -> std::vector<tpp::shared_future<void>>
{
    std::vector<tpp::shared_future<void>> futures;
    futures.reserve(jobs.size());
    for(const auto& job : jobs)
    {
        futures.push_back(job.job);
    }
    return futures;
}

/// Why a step that waits for `prerequisites` is skipped: the deploy was cancelled, or the first of them that failed;
/// empty when it runs.
auto get_skip_reason(const deploy_jobs& prerequisites) -> std::string
{
    if(is_deploy_cancelled())
    {
        return CANCELLED_STEP_ERROR;
    }
    for(const auto& prerequisite : prerequisites)
    {
        if(!get_step_error(prerequisite.job).empty())
        {
            return fmt::format("Skipped: {} failed.", prerequisite.name);
        }
    }
    return {};
}

/// Tells work that waits for a step outside the step list (Android's device steps) that the step did not finish, and
/// why.
using dependents_skip = std::function<void(const std::string& reason)>;

/// Runs `step` once `prerequisites` ended, or skips it when one failed.
void run_step_after(const deploy_step& step, const deploy_jobs& prerequisites, const dependents_skip& skip_dependents)
{
    const auto skip_reason = get_skip_reason(prerequisites);
    if(!skip_reason.empty())
    {
        skip_dependents(skip_reason);
        throw std::runtime_error(skip_reason);
    }
    try
    {
        run_step(step);
    }
    catch(...)
    {
        skip_dependents(is_deploy_cancelled() ? std::string(CANCELLED_STEP_ERROR) : fmt::format("Skipped: {} failed.", step.name));
        throw;
    }
}

/// A step that builds on what `prerequisites` made: it starts once they all ended, and is skipped when one failed.
auto schedule_step_after(
    const deploy_step& step,
    const deploy_jobs& prerequisites,
    const dependents_skip& skip_dependents = [](const std::string&) {}) -> deploy_job
{
    const auto futures = get_futures(prerequisites);
    return {step.name,
            tpp::when_all(futures.begin(), futures.end())
                .then(
                    [step, prerequisites, skip_dependents](auto)
                    {
                        run_step_after(step, prerequisites, skip_dependents);
                    })
                .share()};
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
    const auto page = make_web_page_address(origin,
                                            site_path,
                                            split_run_arguments(plan.params.deploy_run_arguments),
                                            get_web_run_graphics(plan.params.web));
    APPLOG_INFO("Serving {} at {} until the editor closes.", plan.output.generic_string(), page);
    if(!os::url::open(page))
    {
        APPLOG_WARNING("Open {} in a browser to play the deploy.", page);
    }
}

/// The stamp of the build `params` deploys.
auto make_build_stamp(const deploy_settings& params, bool is_finished) -> build_stamp
{
    return {params.profile, version::get_full(), is_finished};
}

/// Says in the log how a deploy with a failed step ended.
void log_unfinished_deploy(const deploy_plan& plan)
{
    const auto folder = plan.output.generic_string();
    if(is_deploy_cancelled())
    {
        APPLOG_WARNING("Deploy to {} was cancelled. The folder holds an unfinished build until the next deploy.", folder);
        return;
    }
    APPLOG_ERROR("Deploy to {} failed. See the Deploy window for the step.", folder);
}

/// Runs on the main thread once every step ended. A failed step left a partial build, which its stamp keeps
/// marked as unfinished: the Deploy window names the step, and running or showing that build would only hide the
/// failure.
void finish_deploy(const deploy_plan& plan, const std::vector<tpp::shared_future<void>>& steps)
{
    fs::error_code ec;
    fs::remove_all(plan.cook_staging, ec);
    --running_deploys;
    if(has_failed_step(steps))
    {
        log_unfinished_deploy(plan);
        return;
    }
    if(!write_build_stamp(plan.output, make_build_stamp(plan.params, true)))
    {
        APPLOG_ERROR("Deploy to {} could not mark its build as finished: {} cannot be written.",
                     plan.output.generic_string(),
                     BUILD_STAMP_NAME);
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

/// The Android package, built from what the other steps deployed. Deploy and Run then installs and starts the game,
/// and that session finishes the device steps.
void deploy_android_package(const deploy_plan& plan)
{
    build_android_package(plan.android);
    if(plan.params.deploy_and_run)
    {
        run_android_game(plan.android);
    }
}

/// Cooks a protocol's textures, one set after the other.
void cook_texture_sets(const std::vector<texture_cook_plan>& sets)
{
    std::for_each(sets.begin(), sets.end(), cook_textures);
}

/// Builds the project's script library for the build: optimized, whatever mode the editor's scripts run in. It
/// reads the script sources as the editor's own builds do.
void build_script_library(asset_manager& am, const deploy_plan& plan)
{
    if(!script_system::build_library_to(am, CONTENT_PROTOCOL, plan.script_library, false))
    {
        throw std::runtime_error("The project's scripts did not compile. The Console lists the errors.");
    }
}

/// The cooks of a deploy, and the ones each data step packs the results of.
struct cook_jobs
{
    /// Every cook, in the order the step list shows them.
    deploy_jobs all;
    /// The engine's textures and the shaders.
    deploy_jobs engine;
    /// The project's textures, its audio, its script library and the shaders.
    deploy_jobs app;
};

auto schedule_cooks(threader& th, asset_manager& am, const std::shared_ptr<const deploy_plan>& plan) -> cook_jobs
{
    const auto textures = schedule_step(th, {STEP_COOK_TEXTURES, [plan]() { cook_texture_sets(plan->textures); }});
    const auto engine_textures =
        schedule_step(th, {STEP_COOK_ENGINE_TEXTURES, [plan]() { cook_texture_sets(plan->engine_textures); }});
    const auto audio = schedule_step(th, {STEP_COOK_AUDIO, [plan]() { cook_audio(plan->audio); }});
    const auto scripts = schedule_step(th, {STEP_COMPILE_SCRIPTS, [plan, &am]() { build_script_library(am, *plan); }});
    cook_jobs cooks{{textures, engine_textures, audio, scripts}, {engine_textures}, {textures, audio, scripts}};
    if(plan->shaders.has_work())
    {
        const auto shaders = schedule_step(th, {STEP_COOK_SHADERS, [plan]() { cook_shaders(plan->shaders); }});
        cooks.all.push_back(shaders);
        cooks.engine.push_back(shaders);
        cooks.app.push_back(shaders);
    }
    return cooks;
}

/// Schedules every step of the plan: the cooks and the steps that need only the plan at once, each data step after its
/// protocol's cooks, the browser player and the Android package after all of those.
auto schedule_steps(threader& th, asset_manager& am, const std::shared_ptr<const deploy_plan>& plan) -> deploy_jobs
{
    const auto cooks = schedule_cooks(th, am, plan);
    deploy_jobs jobs = cooks.all;
    for(const auto& step : make_steps(plan))
    {
        jobs.push_back(schedule_step(th, step));
    }
    jobs.push_back(schedule_step_after({STEP_ENGINE_DATA, [plan]() { deploy_protocol_data(plan->engine); }}, cooks.engine));
    jobs.push_back(schedule_step_after({STEP_PROJECT_DATA, [plan]() { deploy_protocol_data(plan->app); }}, cooks.app));
    // The browser player's list of files is the deployed data's.
    if(is_web(*plan))
    {
        jobs.push_back(schedule_step_after({STEP_WEB_PLAYER, [plan]() { deploy_web_site(*plan); }}, jobs));
    }
    if(is_android(*plan))
    {
        jobs.push_back(schedule_step_after({STEP_ANDROID_PACKAGE, [plan]() { deploy_android_package(*plan); }},
                                           jobs,
                                           [plan](const std::string& reason)
                                           {
                                               fail_device_steps(plan->android, reason);
                                           }));
        const auto device_jobs = get_device_jobs(plan->android);
        jobs.insert(jobs.end(), device_jobs.begin(), device_jobs.end());
        // Beside the deploy's steps: it frees the disk space other builds left, and nothing waits for it.
        th.pool->schedule("Removing unused Android build folders", [plan]() { remove_unused_android_build_dirs(plan->android); });
    }
    return jobs;
}
} // namespace

auto deploy_project(rtti::context& ctx, const deploy_settings& params) -> deploy_jobs
{
    reset_deploy_cancel();
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
    const auto output = get_output_location(params);
    fs::create_directories(output, ec);
    // From here the folder changes: it holds an unfinished build until the last step succeeded.
    if(!write_build_stamp(output, make_build_stamp(params, false)))
    {
        fs::remove_all(staging, ec);
        return make_failed_step(th, STEP_FOLDER, fmt::format("The deploy folder {} cannot be written.", output.generic_string()));
    }
    if(params.deploy_target == deploy_platform::android)
    {
        fs::create_directories(get_android_content_dir(params.profile), ec);
    }
    const auto plan = std::make_shared<const deploy_plan>(make_plan(ctx, params, staging, std::move(left_out)));
    ++running_deploys;
    auto jobs = schedule_steps(th, am, plan);
    const auto futures = get_futures(jobs);
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

void cancel_deploy()
{
    if(!is_deploy_running() || is_deploy_cancelled())
    {
        return;
    }
    APPLOG_INFO("Cancelling the deploy: the running steps stop where they can, and the others are skipped.");
    request_deploy_cancel();
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
