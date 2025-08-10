#include "project_manager.h"
#include <editor/assets/asset_watcher.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/editing/thumbnail_manager.h>
#include <editor/meta/deploy/deploy.hpp>
#include <editor/meta/settings/settings.hpp>
#include <editor/meta/system/project_manager.hpp>

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_compiler.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/ecs/ecs.h>
#include <engine/events.h>
#include <engine/meta/settings/settings.hpp>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/threading/threader.h>

// must be below all
#include <engine/assets/impl/asset_writer.h>

#include <filesystem/watcher.h>
#include <graphics/graphics.h>
#include <logging/logging.h>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>

namespace unravel
{

namespace
{
fs::path app_deploy_cfg = "app:/deploy/deploy.cfg";
fs::path app_deploy_file = "deploy/deploy.cfg";
fs::path app_settings_cfg = "app:/settings/settings.cfg";
fs::path editor_cfg = fs::persistent_path() / "unravel" / "editor.cfg";

} // namespace

void project_manager::close_project(rtti::context& ctx)
{
    if(has_open_project())
    {
        save_editor_settings();
        save_project_settings(ctx);
        save_deploy_settings();
        project_settings_ = {};
        deploy_settings_ = {};
    }

    ctx.remove<settings>();

    auto& scr = ctx.get_cached<script_system>();
    scr.unload_app_domain();

    auto& em = ctx.get_cached<editing_manager>();
    em.clear();

    auto& tm = ctx.get_cached<thumbnail_manager>();
    tm.clear_thumbnails();

    auto& ec = ctx.get_cached<ecs>();
    ec.unload_scene();

    set_name({});

    auto& aw = ctx.get_cached<asset_watcher>();
    aw.unwatch_assets(ctx, "app:/");
}

auto project_manager::open_project(rtti::context& ctx, const fs::path& project_path) -> bool
{
    close_project(ctx);

    fs::error_code err;
    if(!fs::exists(project_path, err))
    {
        APPLOG_ERROR("Project directory doesn't exist {0}", project_path.string());
        return false;
    }

    APPLOG_TRACE("Opening project directory {0}", project_path.string());

    fs::add_path_protocol("app", project_path);

    {
        fs::error_code err;
        fs::create_directories(fs::resolve_protocol(ex::get_data_directory("app")), err);
        fs::create_directories(fs::resolve_protocol(ex::get_compiled_directory("app")), err);
        fs::create_directories(fs::resolve_protocol(ex::get_meta_directory("app")), err);
        fs::create_directories(fs::resolve_protocol("app:/settings"), err);
        fs::create_directories(fs::resolve_protocol("app:/deploy"), err);

    }

    set_name(project_path.filename().string());

    save_editor_settings();


    editor_actions::generate_script_workspace();


    auto& aw = ctx.get_cached<asset_watcher>();
    aw.watch_assets(ctx, "app:/");

    auto& scr = ctx.get_cached<script_system>();
    scr.load_app_domain(ctx, true);

    load_project_settings();
    save_project_settings(ctx);

    load_deploy_settings();
    save_deploy_settings();


    auto scn = project_settings_.standalone.startup_scene;

    if(scn)
    {
        if(!editor_actions::open_scene_from_asset(ctx, scn))
        {
            editor_actions::new_scene(ctx);
        }
    }
    else
    {
        editor_actions::new_scene(ctx);
    }

    return true;
}

void project_manager::load_project_settings()
{
    load_from_file(fs::resolve_protocol(app_settings_cfg).string(), project_settings_);
}

void project_manager::save_project_settings(rtti::context& ctx)
{
    asset_writer::atomic_save_to_file(fs::resolve_protocol(app_settings_cfg).string(), project_settings_);

    ctx.add<settings>(project_settings_);
}

void project_manager::load_deploy_settings()
{
    load_from_file(fs::resolve_protocol(app_deploy_cfg).string(), deploy_settings_);

    fs::error_code ec;
    if(!fs::exists(deploy_settings_.deploy_location, ec))
    {
        deploy_settings_.deploy_location.clear();
    }
}

void project_manager::save_deploy_settings()
{
    asset_writer::atomic_save_to_file(fs::resolve_protocol(app_deploy_cfg).string(), deploy_settings_);
}

void project_manager::create_project(rtti::context& ctx, const fs::path& project_path)
{
    fs::error_code err;
    if(fs::exists(project_path, err) && !fs::is_empty(project_path, err))
    {
        APPLOG_ERROR("Project directory already exists and is not empty {0}", project_path.string());
        return;
    }

    fs::create_directories(project_path, err);

    if(err)
    {
        APPLOG_ERROR("Failed to create project directory {0}", project_path.string());
        return;
    }

    fs::add_path_protocol("app", project_path);

    open_project(ctx, project_path);
}

void project_manager::fixup_editor_settings_on_save()
{
    // fixup recent_projects
    if(has_open_project())
    {
        auto& rp = editor_settings_.projects.recent_projects;
        auto project_path = fs::resolve_protocol("app:/");
        if(std::find_if(std::begin(rp),
                        std::end(rp),
                        [&](const auto& prj)
                        {
                            return project_path.generic_string() == prj;
                        }) == std::end(rp))
        {
            rp.emplace_back(std::move(project_path));
        }

        std::sort(std::begin(rp),
                  std::end(rp),
                  [](const auto& lhs_path, const auto& rhs_path)
                  {
                      fs::error_code ec;
                      auto lhs_time = fs::last_write_time(lhs_path / app_deploy_file, ec);
                      auto rhs_time = fs::last_write_time(rhs_path / app_deploy_file, ec);

                      return lhs_time > rhs_time;
                  });
    }
}
void project_manager::fixup_editor_settings_on_load()
{
    fs::error_code err;

    // fixup recent_projects
    {
        auto& items = editor_settings_.projects.recent_projects;
        auto iter = std::begin(items);
        while(iter != items.end())
        {
            auto& item = *iter;

            if(!fs::exists(item, err))
            {
                iter = items.erase(iter);
            }
            else
            {
                ++iter;
            }
        }
    }
}

void project_manager::load_editor_settings()
{
    fs::error_code err;
    const fs::path config = editor_cfg;
    if(!fs::exists(config, err))
    {
        save_editor_settings();
    }
    else
    {
        APPLOG_INFO("Loading editor settings {}", config.string());
        if(load_from_file(config.string(), editor_settings_))
        {
            fixup_editor_settings_on_load();
        }
    }
}

void project_manager::save_editor_settings()
{
    fixup_editor_settings_on_save();

    fs::error_code err;
    fs::create_directories(editor_cfg.parent_path(), err);

    const fs::path config = editor_cfg;
    asset_writer::atomic_save_to_file(config.string(), editor_settings_);
}

auto project_manager::get_name() const -> const std::string&
{
    return project_name_;
}

void project_manager::set_name(const std::string& name)
{
    project_name_ = name;
}

auto project_manager::get_settings() -> settings&
{
    return project_settings_;
}

auto project_manager::get_deploy_settings() -> deploy_settings&
{
    return deploy_settings_;
}

auto project_manager::get_editor_settings() -> editor_settings&
{
    return editor_settings_;
}

auto project_manager::has_open_project() const -> bool
{
    return !get_name().empty();
}

project_manager::project_manager(rtti::context& ctx, cmd_line::parser& parser)
{
    load_editor_settings();

    auto& scripting = ctx.get_cached<script_system>();
    scripting.set_debug_config(editor_settings_.debugger.ip,
                               editor_settings_.debugger.port,
                               editor_settings_.debugger.loglevel);

    auto& ev = ctx.get_cached<events>();
    ev.on_script_recompile.connect(sentinel_,
                                   -1000,
                                   [this](rtti::context& ctx, const std::string& protocol, uint64_t version)
                                   {
                                       if(protocol == "app" && has_open_project())
                                       {
                                           editor_actions::generate_script_workspace();
                                       }
                                   });

    parser.set_optional<std::string>("p", "project", "", "Project folder to open.");

}

auto project_manager::init(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    std::string project;
    if(parser.try_get("project", project) && !project.empty())
    {
        if(project == "recent")
        {
            const auto& items = editor_settings_.projects.recent_projects;
            if(!items.empty())
            {
                fs::path project_path = items.front();
                return open_project(ctx, project_path);
            }
        }
        else
        {
            fs::path project_path = project;
            return open_project(ctx, project_path);
        }
    }

    return true;
}

auto project_manager::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    close_project(ctx);

    return true;
}

project_manager::~project_manager()
{
    save_editor_settings();
}
} // namespace unravel
