#include "service.h"
#include "entt/core/fwd.hpp"
#include "entt/meta/meta.hpp"
#include <chrono>
#include <csignal>
#include <iostream>

#include <entt/meta/resolve.hpp>
#include <entt/meta/utility.hpp>
#include <entt/core/hashed_string.hpp>

using namespace std::chrono_literals;

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

auto service_main(const char* name, int argc, char* argv[]) -> int
{
    std::vector<module_desc> modules{{name, name}};

    int run = SERVICE_RESULT_RUN;
    while(run != SERVICE_RESULT_EXIT)
    {
        service app(argc, argv);

        if(!app.load(modules))
        {
            return -1;
        }

        while(run == SERVICE_RESULT_RUN)
        {
            run = app.process(); 
        }

        if(!app.unload())
        {
            return -1;
        }
    }

    return 0;
}
