#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>
#include <filesystem/filesystem.h>

#include <functional>
#include <string>

namespace unravel
{

struct engine
{
    static auto create(rtti::context& ctx, cmd_line::parser& parser) -> bool;
    /**
     * @brief Initializes threading, the renderer, audio and the asset manager.
     * @param create_main_window Creates the host's main window through the renderer
     *        (create_window / create_window_for_display). It runs before the graphics backend,
     *        which is then initialized on that window.
     */
    static auto init_core(const cmd_line::parser& parser, const std::function<bool()>& create_main_window) -> bool;
    static auto init_systems(const cmd_line::parser& parser) -> bool;

    /**
     * @brief Applies the --raytracing option: the ray tracer the offline bakes build their scenes with.
     *
     * init_core does this. A headless host that skips init_core, such as the test runner, calls it itself.
     */
    static void select_raytracing_backend(const cmd_line::parser& parser);
    /**
     * @brief Points user:/ and cache:/ at the per-user folders of a game.
     *
     * user:/ holds what the game saves and must keep; cache:/ holds files it can rebuild, which the
     * user or the system may delete. On desktop both are named after `company` and `product`. On
     * Android user:/ is the "user" folder of the application's internal storage, the only folder
     * its backup rules keep, and cache:/ is the application's cache folder. Without such folders
     * they stay in binary:/, where create() points them.
     * @param company The company that makes the game; may be empty.
     * @param product The game's name; the caller substitutes one when the settings leave it empty.
     */
    static void set_user_protocols(const std::string& company, const std::string& product);

    /**
     * @brief Writes the log, the crash log and its minidump into @p folder, created when missing.
     *
     * create() keeps the log in memory and points the crash log at binary:/, so a host names the folder once
     * it knows it: the editor and the test runner binary:/, the player a folder of the user's data, as an
     * install folder may not be writable. The log file begins with the lines logged before.
     * @return False when the log file cannot be created there; the log and the crash log stay where they were.
     */
    static auto open_log(const fs::path& folder) -> bool;

    static auto interrupt() -> bool;

    /**
     * @brief Whether interrupt() or request_restart() asked the frame loop to end.
     */
    static auto is_interrupted() -> bool;

    /**
     * @brief Requests a graceful application restart after the current frame.
     *
     * The running process shuts down normally, then service_main spawns a
     * replacement process. Prefer this over interrupt() when the app should
     * come back with the same user arguments.
     */
    static auto request_restart() -> bool;

    static auto deinit() -> bool;
    static auto destroy() -> bool;
    static auto process() -> int;

    static auto context() -> rtti::context&;

    /**
     * @brief The ambient context, or nullptr when none is installed.
     *
     * context() dereferences unconditionally. Use this where "there may be no engine here"
     * is a legitimate state - headless tools, test harnesses, or code that only wants to
     * ask the context a question if one exists.
     */
    static auto try_context() -> rtti::context*;

    /**
     * @brief Installs the ambient context without constructing any subsystem.
     *
     * create() does this as one step of building the whole engine. Headless tools and test
     * harnesses need only the pointer: asset_handle deserialization reaches for
     * engine::context() to resolve a uid, so anything that loads an entity needs an
     * ambient context even when it has no renderer, audio device or script domain.
     *
     * Not for use inside a running application - create() owns the pointer there. Pass
     * nullptr to clear.
     */
    static void set_context(rtti::context* ctx);
};
} // namespace unravel
