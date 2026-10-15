#pragma once

#include "mcp/mcp_tool_registry.h"

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

    auto snapshot_activity() const -> std::vector<mcp_activity_entry>;
    void clear_activity();

    /**
     * @brief Runs `fn` on the editor main thread and waits for it. Safe from MCP HTTP workers.
     *
     * `fn` runs from the main thread's task processing at the frame start (tpp::async_on_process),
     * never from a wait in the middle of a frame. A call either runs or does not: when `timeout`
     * passes before the main thread picks `fn` up, `fn` is abandoned and never runs; once it has
     * started, the caller waits for it to finish however long it takes. So `fn` may capture the
     * caller's locals by reference, and a timeout always means "nothing happened".
     *
     * @param failure When set, receives why the result is empty: the timeout or the exception text.
     * @return `fn`'s result, or nullopt when it timed out before starting or threw.
     */
    template<typename Fn>
    auto invoke_on_main(Fn&& fn,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000),
                        std::string* failure = nullptr) -> hpp::optional<std::invoke_result_t<std::decay_t<Fn>>>
    {
        using result_t = std::invoke_result_t<std::decay_t<Fn>>;
        static_assert(!std::is_void_v<result_t>, "invoke_on_main requires a non-void return type");
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
        if(future.wait_for(timeout) != std::future_status::ready)
        {
            std::lock_guard<std::mutex> lock(shared_gate->mutex);
            if(!shared_gate->started)
            {
                shared_gate->abandoned = true;
                report_main_thread_failure("The main thread did not take the call within " +
                                               std::to_string(timeout.count()) + " ms; it was not run",
                                           failure);
                return std::nullopt;
            }
        }
        try
        {
            return future.get();
        }
        catch(const std::exception& ex)
        {
            report_main_thread_failure(std::string("Exception on main thread: ") + ex.what(), failure);
            return std::nullopt;
        }
        catch(...)
        {
            report_main_thread_failure("Exception on main thread", failure);
            return std::nullopt;
        }
    }

private:
    /// Counts and logs a call that produced no result, and hands the reason to the caller.
    void report_main_thread_failure(const std::string& reason, std::string* failure);
    auto handle_http_request(const std::string& body) -> std::string;
    auto dispatch_json_rpc(const std::string& body) -> std::string;
    auto execute_tool_call(const std::string& tool_name,
                           const std::string& args_json,
                           const hpp::optional<std::string>& id) -> std::string;
    void log_activity(std::string category, std::string message, bool is_error = false);
    /// "Unknown tool" plus the nearest real names, so a guessed name corrects itself.
    auto unknown_tool_message(const std::string& tool_name) const -> std::string;

    rtti::context* ctx_{nullptr};
    mcp::mcp_tool_registry registry_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    std::atomic_bool running_{false};
    bool enabled_{true};
    int port_{k_default_port};

    mutable std::mutex activity_mutex_;
    std::deque<mcp_activity_entry> activity_;
    std::atomic_uint64_t request_count_{0};
    std::atomic_uint64_t tool_call_count_{0};
    std::atomic_uint64_t error_count_{0};
};

} // namespace unravel
