#pragma once

#include <cmd_line/parser.h>
#include <context/context.hpp>
#include <entt/meta/meta.hpp>

#include <memory>
#include <string>
#include <vector>

struct module_desc
{
    std::string lib_name;
    std::string type_name;
};

struct module_data
{
    module_desc desc;
};

#define SERVICE_RESULT_EXIT 0
#define SERVICE_RESULT_RUN 1
#define SERVICE_RESULT_RELOAD 2
#define SERVICE_RESULT_RESTART 3

struct service
{
    service(int argc, char* argv[]);
    auto load(const module_desc& desc) -> bool;
    auto unload(const module_data& module) -> bool;

    auto load(const std::vector<module_desc>& descs) -> bool;
    auto unload() -> bool;

    auto init() -> bool;
    auto process() -> int;
    auto interrupt() -> bool;

    /**
     * @brief Lets loaded modules prepare for process restart (e.g. adjust spawn args).
     *
     * Optional per-module meta hook: prepare_restart.
     */
    void prepare_restart(std::vector<std::string>& arguments);

    auto get_cmd_line_parser() -> cmd_line::parser&;

private:
    rtti::context ctx_;
    cmd_line::parser parser_;
    std::vector<module_data> modules_;
};

/**
 * @brief Runs one application a frame at a time, for whoever owns the frame loop.
 *
 * service_main runs it in a loop. A browser cannot: a page shows nothing until each frame returns to it, so the
 * browser's frame callback calls run_frame() once per frame instead.
 */
class service_runner
{
public:
    /**
     * @param name The application's module (ENTRY_APP_NAME).
     * @param arguments Its command line, the program first.
     */
    service_runner(std::string name, std::vector<std::string> arguments);

    /// Loads and initializes the application. Returns false when it did not start; nothing is left to stop then.
    auto start() -> bool;

    /// Runs one frame. Returns SERVICE_RESULT_RUN while the application wants more frames, else what it ends with.
    auto run_frame() -> int;

    /// Unloads the application, if it runs. Returns false when an unload failed.
    auto stop() -> bool;

    /// The running application; only between a successful start() and stop().
    auto get_service() -> service&;

private:
    std::string name_;
    /// Owns the strings argv_ points into, which the running service reads.
    std::vector<std::string> arguments_;
    std::vector<char*> argv_;
    std::unique_ptr<service> app_;
};

int service_main(const char* name, int argc, char* argv[]);
