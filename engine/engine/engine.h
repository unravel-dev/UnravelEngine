#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>

#include <functional>

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
