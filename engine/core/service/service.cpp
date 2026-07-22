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

#include <process/process.h>
#include <process/startup_arguments.h>

using namespace std::chrono_literals;

namespace
{

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

auto service_main(const char* name, int argc, char* argv[]) -> int
{
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
    std::vector<std::string> service_argv_storage = unravel::process::build_service_argv(startup);
    std::vector<char*> service_argv = make_argv_pointers(service_argv_storage);
    const int service_argc = static_cast<int>(service_argv.size());
    std::vector<module_desc> modules{{name, name}};
    int run = SERVICE_RESULT_RUN;
    while(run != SERVICE_RESULT_EXIT)
    {
        service app(service_argc, service_argv.data());
        if(!app.load(modules))
        {
            return -1;
        }
        for(;;)
        {
            while(run == SERVICE_RESULT_RUN)
            {
                run = app.process();
            }
            if(run == SERVICE_RESULT_RESTART)
            {
                const std::uint32_t next_restart_count = startup.restart_count + 1;
                auto replacement_arguments =
                    unravel::process::build_replacement_application_arguments(startup);
                app.prepare_restart(replacement_arguments);
                std::cout << "Restart requested: spawning replacement process (count="
                          << next_restart_count << ")" << std::endl;
                const unravel::process::restart_result spawn_result =
                    unravel::process::spawn_replacement(replacement_arguments, next_restart_count);
                if(!spawn_result.success)
                {
                    std::cerr << "Failed to spawn replacement process: " << spawn_result.error.message()
                              << " (" << spawn_result.error.value() << ")" << std::endl;
                    run = SERVICE_RESULT_RUN;
                    continue;
                }
                std::cout << "Replacement process spawned successfully" << std::endl;
                if(!app.unload())
                {
                    return -1;
                }
                return 0;
            }
            break;
        }
        if(!app.unload())
        {
            return -1;
        }
    }
    return 0;
}
