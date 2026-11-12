#include "service.h"
#include "entt/core/fwd.hpp"
#include "entt/meta/meta.hpp"
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <vector>

#include <entt/meta/resolve.hpp>
#include <entt/meta/utility.hpp>
#include <entt/core/hashed_string.hpp>

#include <base/platform/config.hpp>
#include <process/process.h>
#include <process/startup_arguments.h>

#if UNRAVEL_PLATFORM_EMSCRIPTEN
#include <emscripten/emscripten.h>

// Tells the page the application ended: its canvas keeps the last frame, and only the page can say so
// (game/web/wwwroot/main.js, unravelPage.onGameEnded).
EM_JS(void, unravel_notify_page_of_end, (), {
    const page = globalThis.unravelPage;
    if (page && typeof page.onGameEnded === 'function') {
        page.onGameEnded();
    }
});
#endif

using namespace std::chrono_literals;

namespace
{

#if UNRAVEL_PLATFORM_EMSCRIPTEN
/// The application the browser's animation frames run, from service_main until it ends.
std::unique_ptr<service_runner> browser_runner;

/// One frame of the browser's animation-frame loop. When the application ends, the loop stops and the runner
/// unloads it. A browser player neither restarts nor reloads itself: asked to, it ends.
void run_browser_frame()
{
    const int result = browser_runner->run_frame();
    if(result == SERVICE_RESULT_RUN)
    {
        return;
    }
    emscripten_cancel_main_loop();
    if(result != SERVICE_RESULT_EXIT)
    {
        std::cerr << "The application asked to restart or reload, which a browser player cannot: it ends." << std::endl;
    }
    if(!browser_runner->stop())
    {
        std::cerr << "The application did not unload cleanly." << std::endl;
    }
    browser_runner.reset();
    unravel_notify_page_of_end();
}
#endif

auto make_argv_pointers(std::vector<std::string>& storage) -> std::vector<char*>
{
    std::vector<char*> pointers;
    pointers.reserve(storage.size());
    for(auto& argument : storage)
    {
        pointers.push_back(argument.data());
    }
    return pointers;
}

} // namespace

service::service(int argc, char* argv[]) : parser_(argc, argv)
{
}

auto service::load(const module_desc& desc) -> bool
{
    std::cout << "service::" << __func__ << " module " << desc.lib_name << std::endl;
    module_data module;
    module.desc = desc;

    using namespace entt::literals;
    
    auto type = entt::resolve(entt::hashed_string{module.desc.type_name.c_str()});

    if(!type.invoke("create"_hs, {}, entt::forward_as_meta(ctx_), entt::forward_as_meta(parser_)).cast<bool>())
    {
        return false;
    }
    
    modules_.emplace_back(std::move(module));

    return true;
}

auto service::unload(const module_data& module) -> bool
{
    std::cout << "service::" << __func__ << " module " << module.desc.lib_name << std::endl;
    
    using namespace entt::literals;

    auto type = entt::resolve(entt::hashed_string{module.desc.type_name.c_str()});

    if(!type.invoke("deinit"_hs, {}).cast<bool>())
    {
        return false;
    }

    if(!type.invoke("destroy"_hs, {}).cast<bool>())
    {
        return false;
    }

    return true;
}

auto service::load(const std::vector<module_desc>& descs) -> bool
{
    bool batch = true;
    for(const auto& desc : descs)
    {
        batch &= load(desc);
    }

    if(batch)
    {
        batch &= init();
    }

    if(!batch)
    {
        unload();
    }

    return batch;
}

auto service::unload() -> bool
{
    bool batch = true;
    for(auto it = std::rbegin(modules_); it != std::rend(modules_); ++it)
    {
        auto& module = *it;
        batch &= unload(module);
    }

    modules_.clear();
    return batch;
}

auto service::init() -> bool
{
    if(!parser_.run())
    {
        return false;
    }

    for(const auto& module : modules_)
    {
        using namespace entt::literals;

        auto type = entt::resolve(entt::hashed_string{module.desc.type_name.c_str()});

        if(!type.invoke("init"_hs, {}, entt::forward_as_meta(parser_)).cast<bool>())
        {
            return false;
        }
    }

    parser_.reset();

    return true;
}

auto service::interrupt() -> bool
{
    //    std::cout << "service::" << __func__ << std::endl;
    bool processed = false;
    for(const auto& module : modules_)
    {
        using namespace entt::literals;

        auto type = entt::resolve(entt::hashed_string{module.desc.type_name.c_str()});

        if(!type.invoke("interrupt"_hs, {}).cast<bool>())
        {
            return false;
        }

        processed = true;
    }

    return processed;
}

auto service::process() -> int
{
    //    std::cout << "service::" << __func__ << std::endl;
    int processed = SERVICE_RESULT_EXIT;
    for(const auto& module : modules_)
    {
        using namespace entt::literals;

        auto type = entt::resolve(entt::hashed_string{module.desc.type_name.c_str()});

        auto proc_result = type.invoke("process"_hs, {}).cast<int>();

        if(proc_result == SERVICE_RESULT_EXIT)
        {
            return SERVICE_RESULT_EXIT;
        }

        processed = std::max(processed, proc_result);
    }

    return processed;
}

auto service::get_cmd_line_parser() -> cmd_line::parser&
{
    return parser_;
}

void service::prepare_restart(std::vector<std::string>& arguments)
{
    for(const auto& module : modules_)
    {
        using namespace entt::literals;
        auto type = entt::resolve(entt::hashed_string{module.desc.type_name.c_str()});
        auto func = type.func("prepare_restart"_hs);
        if(!func)
        {
            continue;
        }
        func.invoke({}, entt::forward_as_meta(arguments));
    }
}

service_runner::service_runner(std::string name, std::vector<std::string> arguments)
    : name_(std::move(name))
    , arguments_(std::move(arguments))
    , argv_(make_argv_pointers(arguments_))
{
}

auto service_runner::start() -> bool
{
    app_ = std::make_unique<service>(static_cast<int>(argv_.size()), argv_.data());
    // load unloads what it loaded when it fails.
    if(!app_->load(std::vector<module_desc>{{name_, name_}}))
    {
        app_.reset();
        return false;
    }
    return true;
}

auto service_runner::run_frame() -> int
{
    return app_->process();
}

auto service_runner::stop() -> bool
{
    if(!app_)
    {
        return true;
    }
    const bool is_unloaded = app_->unload();
    app_.reset();
    return is_unloaded;
}

auto service_runner::get_service() -> service&
{
    return *app_;
}

auto service_main(const char* name, int argc, char* argv[]) -> int
{
#if UNRAVEL_PLATFORM_EMSCRIPTEN
    // A page shows nothing until each frame returns to the browser, so the browser calls the frames, from its
    // animation frames, after main returns.
    browser_runner = std::make_unique<service_runner>(name, std::vector<std::string>(argv, argv + argc));
    if(!browser_runner->start())
    {
        browser_runner.reset();
        return -1;
    }
    emscripten_set_main_loop(run_browser_frame, 0, false);
    return 0;
#else
    const unravel::process::startup_arguments startup =
        unravel::process::parse_startup_arguments(argc, argv);
    if(startup.has_parse_error)
    {
        std::cerr << "Failed to parse startup arguments: " << startup.parse_error << std::endl;
        return EXIT_FAILURE;
    }
    if(startup.restart_from_pid)
    {
        std::cout << "Restarting: waiting for process " << *startup.restart_from_pid << " to exit"
                  << std::endl;
        if(!unravel::process::wait_for_process_exit(*startup.restart_from_pid))
        {
            std::cerr << "Failed while waiting for process " << *startup.restart_from_pid << std::endl;
            return EXIT_FAILURE;
        }
        std::cout << "Restarting: previous process exited, continuing startup" << std::endl;
    }
    service_runner runner(name, unravel::process::build_service_argv(startup));
    if(!runner.start())
    {
        return -1;
    }
    for(;;)
    {
        int run = SERVICE_RESULT_RUN;
        while(run == SERVICE_RESULT_RUN)
        {
            run = runner.run_frame();
        }
        if(run == SERVICE_RESULT_RELOAD)
        {
            if(!runner.stop() || !runner.start())
            {
                return -1;
            }
            continue;
        }
        if(run == SERVICE_RESULT_RESTART)
        {
            const std::uint32_t next_restart_count = startup.restart_count + 1;
            auto replacement_arguments = unravel::process::build_replacement_application_arguments(startup);
            runner.get_service().prepare_restart(replacement_arguments);
            std::cout << "Restart requested: handing off to replacement process (count=" << next_restart_count
                      << ")" << std::endl;
            const unravel::process::restart_result spawn_result =
                unravel::process::spawn_replacement(replacement_arguments,
                                                    next_restart_count,
                                                    [&]()
                                                    {
                                                        return runner.stop();
                                                    });
            if(!spawn_result.success)
            {
                std::cerr << "Failed to hand off to replacement process: " << spawn_result.error.message() << " ("
                          << spawn_result.error.value() << ")" << std::endl;
                if(spawn_result.resources_released)
                {
                    return EXIT_FAILURE;
                }
                continue;
            }
            std::cout << "Replacement process handoff succeeded" << std::endl;
            return 0;
        }
        break;
    }
    return runner.stop() ? 0 : -1;
#endif
}
