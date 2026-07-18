#pragma once

#include "mcp/mcp_tool_registry.h"

#include <atomic>
#include <base/basetypes.hpp>
#include <chrono>
#include <context/context.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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
    static constexpr size_t k_max_activity_entries = 256;

    mcp_manager();
    ~mcp_manager();

    auto init(rtti::context& ctx) -> bool;
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
    auto get_request_count() const -> uint64_t;
    auto get_tool_call_count() const -> uint64_t;
    auto get_error_count() const -> uint64_t;

    auto snapshot_activity() const -> std::vector<mcp_activity_entry>;
    void clear_activity();

    /// Run `fn` on the editor main thread and wait. Safe from MCP HTTP workers.
    /// `fn` must return a value (use bool for success/fail). Returns nullopt on timeout / exception.
    template<typename Fn>
    auto invoke_on_main(Fn&& fn, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
        -> std::optional<std::invoke_result_t<std::decay_t<Fn>>>
    {
        using result_t = std::invoke_result_t<std::decay_t<Fn>>;
        static_assert(!std::is_void_v<result_t>, "invoke_on_main requires a non-void return type");

        tpp::this_thread::register_this_thread();
        auto future = tpp::async(tpp::main_thread::get_id(), std::forward<Fn>(fn));
        if(future.wait_for(timeout) != std::future_status::ready)
        {
            error_count_.fetch_add(1);
            log_activity("rpc", "Timed out waiting for main thread", true);
            return std::nullopt;
        }

        try
        {
            return future.get();
        }
        catch(const std::exception& ex)
        {
            error_count_.fetch_add(1);
            log_activity("rpc", std::string("Exception on main thread: ") + ex.what(), true);
            return std::nullopt;
        }
        catch(...)
        {
            error_count_.fetch_add(1);
            log_activity("rpc", "Exception on main thread", true);
            return std::nullopt;
        }
    }

private:
    using job_fn = std::function<std::string()>;

    auto run_on_main_thread(job_fn job, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
        -> std::string;
    auto handle_http_request(const std::string& body) -> std::string;
    auto dispatch_json_rpc(const std::string& body) -> std::string;
    auto execute_tool_call(const std::string& tool_name,
                           const std::string& args_json,
                           const std::optional<std::string>& id) -> std::string;
    void log_activity(std::string category, std::string message, bool is_error = false);

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
