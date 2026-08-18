#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>

namespace unravel
{

struct engine
{
    static auto create(rtti::context& ctx, cmd_line::parser& parser) -> bool;
    static auto init_core(const cmd_line::parser& parser) -> bool;
    static auto init_systems(const cmd_line::parser& parser) -> bool;

    static auto interrupt() -> bool;

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
