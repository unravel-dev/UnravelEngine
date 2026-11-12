#pragma once

#include "mcp/mcp_protocol.h"
#include "mcp/mcp_tool_registry.h"

#include <algorithm>
#include <atomic>
#include <base/basetypes.hpp>
#include <chrono>
#include <cmd_line/parser.h>
#include <context/context.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <hpp/optional.hpp>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <threadpp/future.hpp>
#include <threadpp/thread.h>

namespace httplib
{
class Server;
}

namespace unravel
{

struct mcp_activity_entry
{
    std::chrono::system_clock::time_point timestamp{};
    std::string category;
    std::string message;
    bool is_error{false};
};

class mcp_manager
{
public:
    static constexpr const char* k_host = "127.0.0.1";
    static constexpr int k_default_port = 27182;
    static constexpr int k_max_port = 65535;
    static constexpr size_t k_max_activity_entries = 256;

    /// Registers --mcp-port (editors running side by side - other sessions, agents - need a port
    /// each) and --mcp-profile (which tool surface clients see).
    explicit mcp_manager(cmd_line::parser& parser);
    ~mcp_manager();

    /// Registers the tools and starts the server when enabled; --mcp-port overrides the port when in 1..k_max_port.
    auto init(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void start();
    void stop();
    auto is_running() const -> bool;
    auto is_enabled() const -> bool;
    auto get_host() const -> const char*;
    auto get_port() const -> int;
    auto get_endpoint_url() const -> std::string;
    auto get_health_url() const -> std::string;
    auto get_tool_count() const -> size_t;
    /// Tools the active profile exposes; what a client actually sees in tools/list.
    auto get_visible_tool_count() const -> size_t;
    auto get_profile() const -> mcp::mcp_profile;
    auto get_profile_name() const -> const char*;
    auto get_request_count() const -> uint64_t;
    auto get_tool_call_count() const -> uint64_t;
    auto get_error_count() const -> uint64_t;
    /// Tool calls running right now.
    auto get_active_call_count() const -> int;
    /// When the last request arrived; nothing before the first one.
    auto get_last_request_time() const -> hpp::optional<std::chrono::steady_clock::time_point>;
    /// The tool called last, running or finished; empty before the first call.
    auto get_last_tool_name() const -> std::string;
    /// The last start could not listen on the port (another editor holds it, say).
    auto has_listen_failed() const -> bool;

    auto snapshot_activity() const -> std::vector<mcp_activity_entry>;
    void clear_activity();

    /// The server is stopping (the editor closes, or the MCP panel turned it off): waits end now.
    auto is_stopping() const -> bool;

    /// Sleeps @p interval between two polls of a wait; false once the server is stopping, so the wait ends.
    auto sleep_unless_stopping(std::chrono::milliseconds interval) const -> bool;

    /**
     * @brief Runs `fn` on the editor main thread and waits for it. Safe from MCP HTTP workers.
     *
     * `fn` runs from the main thread's task processing at the frame start (tpp::async_on_process),
     * never from a wait in the middle of a frame. A call either runs or does not: when `timeout`
     * passes, or the server stops, before the main thread picks `fn` up, `fn` is abandoned and never
     * runs; once it has started, the caller waits for it to finish however long it takes. So `fn` may
     * capture the caller's locals by reference, and a timeout always means "nothing happened".
     * Cleanup that must happen however late belongs in post_on_main. An exception `fn` throws reaches the
     * caller, as if `fn` had run there: a tool reports it as its failure, not as a timeout.
     *
     * @param failure When set, receives why the result is empty: the timeout, or the server stopping.
     * @return `fn`'s result, or nullopt when it was not run.
     */
    template<typename Fn>
    auto invoke_on_main(Fn&& fn,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000),
                        std::string* failure = nullptr) -> hpp::optional<std::invoke_result_t<std::decay_t<Fn>>>
    {
        using result_t = std::invoke_result_t<std::decay_t<Fn>>;
        static_assert(!std::is_void_v<result_t>, "invoke_on_main requires a non-void return type");
        if(is_stopping())
        {
            report_main_thread_failure("The editor's MCP server is stopping; the call was not run", failure);
            return std::nullopt;
        }
        // The task holds the gate while it runs, so a caller that gives up either cancels a task
        // that has not started or waits on the gate for one that has.
        struct gate
        {
            std::mutex mutex;
            bool started{};
            bool abandoned{};
        };
        auto shared_gate = std::make_shared<gate>();
        tpp::this_thread::register_this_thread();
        auto future = tpp::async_on_process(tpp::main_thread::get_id(),
                                            [shared_gate, &fn]() -> hpp::optional<result_t>
                                            {
                                                std::lock_guard<std::mutex> lock(shared_gate->mutex);
                                                if(shared_gate->abandoned)
                                                {
                                                    return std::nullopt;
                                                }
                                                shared_gate->started = true;
                                                return fn();
                                            });
        if(!wait_while_running(future, timeout))
        {
            std::lock_guard<std::mutex> lock(shared_gate->mutex);
            if(!shared_gate->started)
            {
                shared_gate->abandoned = true;
                report_main_thread_failure(is_stopping() ? std::string("The editor's MCP server is stopping; the call was not run")
                                                         : "The main thread did not take the call within " +
                                                               std::to_string(timeout.count()) + " ms; it was not run",
                                           failure);
                return std::nullopt;
            }
        }
        return future.get();
    }

    /**
     * @brief Runs `fn` on the editor main thread at a frame start and waits up to `timeout` for it.
     *
     * Unlike invoke_on_main the task is never abandoned: it runs however late the main thread takes it,
     * so it suits cleanup that must happen whether or not the caller still waits (releasing a held key,
     * handing the pointer back). `fn` must own everything it uses. An exception it throws is logged.
     *
     * @return Whether `fn` ran within `timeout`.
     */
    auto post_on_main(std::function<void()> fn,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) -> bool;

private:
    /// Waits for `future` until it is ready, `timeout` passes or the server stops. True when ready.
    template<typename Future>
    auto wait_while_running(const Future& future, std::chrono::milliseconds timeout) const -> bool
    {
        constexpr auto k_slice = std::chrono::milliseconds(50);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while(true)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if(future.wait_for(std::min(left, k_slice)) == std::future_status::ready)
            {
                return true;
            }
            if(left <= k_slice || is_stopping())
            {
                return false;
            }
        }
    }

    /// Counts and logs a call that produced no result, and hands the reason to the caller.
    void report_main_thread_failure(const std::string& reason, std::string* failure);
    auto handle_http_request(const std::string& body) -> std::string;
    auto dispatch_json_rpc(const std::string& body) -> std::string;
    /// resources/read: the skill a unravel://skills/<name> uri names.
    auto read_resource(const mcp::json_rpc_request& request, const hpp::optional<std::string>& id) -> std::string;
    auto dispatch_tool_call(const mcp::json_rpc_request& request, const hpp::optional<std::string>& id) -> std::string;
    /// Runs a tool whose arguments passed check_arguments; `warnings` join its result.
    auto execute_tool_call(const mcp::mcp_tool& tool,
                           const std::string& args_json,
                           std::vector<std::string> warnings) -> mcp::tool_result;
    /// A tool result that refuses or fails the call, counted and logged as an error.
    auto make_failed_call(const std::string& tool_name, const std::string& message) -> mcp::tool_result;
    void log_activity(std::string category, std::string message, bool is_error = false);
    /// "Unknown tool" plus the nearest real names, so a guessed name corrects itself.
    auto unknown_tool_message(const std::string& tool_name) const -> std::string;

    rtti::context* ctx_{nullptr};
    mcp::mcp_tool_registry registry_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    std::atomic_bool running_{false};
    std::atomic_bool stopping_{false};
    bool enabled_{true};
    int port_{k_default_port};

    mutable std::mutex activity_mutex_;
    std::deque<mcp_activity_entry> activity_;
    std::atomic_uint64_t request_count_{0};
    std::atomic_uint64_t tool_call_count_{0};
    std::atomic_uint64_t error_count_{0};
    std::atomic_int active_call_count_{0};
    /// steady_clock ticks of the last request; 0 before the first one.
    std::atomic<std::chrono::steady_clock::rep> last_request_ticks_{0};
    std::atomic_bool has_listen_failed_{false};
    mutable std::mutex last_tool_mutex_;
    std::string last_tool_name_;
};

} // namespace unravel
