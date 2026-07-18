#include "mcp_manager.h"

#include "mcp/mcp_protocol.h"

#include <logging/logging.h>
#include <version/version.h>

#include <httplib.h>
#include <ser20/external/simdjson/simdjson.h>

#include <utility>

namespace unravel
{

mcp_manager::mcp_manager() = default;
mcp_manager::~mcp_manager()
{
    stop();
}

namespace
{
constexpr int k_parse_error = -32700;
constexpr int k_invalid_request = -32600;
constexpr int k_method_not_found = -32601;
constexpr int k_invalid_params = -32602;
constexpr int k_internal_error = -32603;
} // namespace

auto mcp_manager::init(rtti::context& ctx) -> bool
{
    ctx_ = &ctx;
    mcp::register_scene_tools(registry_);
    mcp::register_asset_tools(registry_);
    mcp::register_viewport_tools(registry_);
    mcp::register_material_tools(registry_);
    mcp::register_project_tools(registry_);
    mcp::register_script_tools(registry_);
    mcp::register_editor_tools(registry_);

    log_activity("lifecycle",
                 fmt::format("Registered {} tools", registry_.tools().size()));

    if(enabled_)
    {
        start();
    }
    return true;
}

auto mcp_manager::deinit(rtti::context& ctx) -> bool
{
    stop();
    ctx_ = nullptr;
    return true;
}

void mcp_manager::start()
{
    if(running_.load())
    {
        return;
    }

    server_ = std::make_unique<httplib::Server>();
    server_->Get("/health",
                 [this](const httplib::Request&, httplib::Response& res)
                 {
                     res.status = 200;
                     res.set_content(R"({"ok":true,"service":"unravel-editor-mcp"})", "application/json");
                 });

    server_->Post("/mcp",
                  [this](const httplib::Request& req, httplib::Response& res)
                  {
                      res.set_header("Access-Control-Allow-Origin", "*");
                      auto response = handle_http_request(req.body);
                      res.status = 200;
                      res.set_content(response, "application/json");
                  });

    server_->Options("/mcp",
                     [](const httplib::Request&, httplib::Response& res)
                     {
                         res.set_header("Access-Control-Allow-Origin", "*");
                         res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
                         res.set_header("Access-Control-Allow-Headers", "Content-Type");
                         res.status = 204;
                     });

    running_.store(true);
    server_thread_ = std::thread(
        [this]()
        {
            const auto endpoint = get_endpoint_url();
            APPLOG_INFO("MCP server listening on {}", endpoint);
            log_activity("lifecycle", "Listening on " + endpoint);
            if(!server_->listen(k_host, port_))
            {
                APPLOG_ERROR("MCP server failed to listen on {}:{}", k_host, port_);
                log_activity("lifecycle", fmt::format("Failed to listen on {}:{}", k_host, port_), true);
                running_.store(false);
            }
        });
}

void mcp_manager::stop()
{
    if(!running_.load() && !server_)
    {
        return;
    }

    running_.store(false);
    if(server_)
    {
        server_->stop();
    }
    if(server_thread_.joinable())
    {
        server_thread_.join();
    }
    server_.reset();
    log_activity("lifecycle", "Server stopped");
    APPLOG_INFO("MCP server stopped");
}

auto mcp_manager::is_running() const -> bool
{
    return running_.load();
}

auto mcp_manager::is_enabled() const -> bool
{
    return enabled_;
}

auto mcp_manager::get_host() const -> const char*
{
    return k_host;
}

auto mcp_manager::get_port() const -> int
{
    return port_;
}

auto mcp_manager::get_endpoint_url() const -> std::string
{
    return fmt::format("http://{}:{}/mcp", k_host, port_);
}

auto mcp_manager::get_health_url() const -> std::string
{
    return fmt::format("http://{}:{}/health", k_host, port_);
}

auto mcp_manager::get_tool_count() const -> size_t
{
    return registry_.tools().size();
}

auto mcp_manager::get_request_count() const -> uint64_t
{
    return request_count_.load();
}

auto mcp_manager::get_tool_call_count() const -> uint64_t
{
    return tool_call_count_.load();
}

auto mcp_manager::get_error_count() const -> uint64_t
{
    return error_count_.load();
}

auto mcp_manager::snapshot_activity() const -> std::vector<mcp_activity_entry>
{
    std::lock_guard<std::mutex> lock(activity_mutex_);
    return {activity_.begin(), activity_.end()};
}

void mcp_manager::clear_activity()
{
    std::lock_guard<std::mutex> lock(activity_mutex_);
    activity_.clear();
}

void mcp_manager::log_activity(std::string category, std::string message, bool is_error)
{
    mcp_activity_entry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.category = std::move(category);
    entry.message = std::move(message);
    entry.is_error = is_error;

    std::lock_guard<std::mutex> lock(activity_mutex_);
    activity_.push_back(std::move(entry));
    while(activity_.size() > k_max_activity_entries)
    {
        activity_.pop_front();
    }
}

auto mcp_manager::run_on_main_thread(job_fn work, std::chrono::milliseconds timeout) -> std::string
{
    tpp::this_thread::register_this_thread();
    auto future = tpp::async(tpp::main_thread::get_id(), std::move(work));
    if(future.wait_for(timeout) != std::future_status::ready)
    {
        error_count_.fetch_add(1);
        log_activity("rpc", "Timed out waiting for main thread", true);
        return mcp::make_json_rpc_error(std::nullopt, k_internal_error, "Timed out waiting for main thread");
    }

    try
    {
        return future.get();
    }
    catch(const std::exception& ex)
    {
        error_count_.fetch_add(1);
        log_activity("rpc", std::string("Exception on main thread: ") + ex.what(), true);
        return mcp::make_json_rpc_error(std::nullopt, k_internal_error, ex.what());
    }
    catch(...)
    {
        error_count_.fetch_add(1);
        log_activity("rpc", "Exception on main thread", true);
        return mcp::make_json_rpc_error(std::nullopt, k_internal_error, "Unknown MCP job error");
    }
}

auto mcp_manager::handle_http_request(const std::string& body) -> std::string
{
    request_count_.fetch_add(1);
    return dispatch_json_rpc(body);
}

auto mcp_manager::dispatch_json_rpc(const std::string& body) -> std::string
{
    mcp::json_rpc_request request;
    std::string parse_error;
    if(!mcp::parse_json_rpc_request(body, request, parse_error))
    {
        error_count_.fetch_add(1);
        log_activity("rpc", "Parse error: " + parse_error, true);
        return mcp::make_json_rpc_error(std::nullopt, k_parse_error, parse_error);
    }

    std::optional<std::string> id = request.has_id ? request.id_json : std::nullopt;

    if(request.method == "initialize")
    {
        log_activity("rpc", "initialize");
        const auto result = fmt::format(
            R"({{"protocolVersion":"2024-11-05","capabilities":{{"tools":{{}}}},"serverInfo":{{"name":"unravel-editor","version":{}}}}})",
            mcp::make_json_string(version::get_full()));
        return mcp::make_json_rpc_result(id, result);
    }

    if(request.method == "notifications/initialized" || request.method == "initialized")
    {
        log_activity("rpc", "initialized");
        return mcp::make_json_rpc_result(id, "null");
    }

    if(request.method == "ping")
    {
        return mcp::make_json_rpc_result(id, "{}");
    }

    if(request.method == "tools/list")
    {
        log_activity("rpc", fmt::format("tools/list ({} tools)", registry_.tools().size()));
        return mcp::make_json_rpc_result(id, fmt::format(R"({{"tools":{}}})", registry_.list_tools_json()));
    }

    if(request.method == "tools/call")
    {
        if(!ctx_)
        {
            error_count_.fetch_add(1);
            log_activity("tool", "tools/call failed: no context", true);
            return mcp::make_json_rpc_error(id, k_internal_error, "MCP manager has no context");
        }

        simdjson::dom::parser parser;
        simdjson::dom::element params_root;
        if(parser.parse(request.params_json).get(params_root))
        {
            error_count_.fetch_add(1);
            log_activity("tool", "tools/call invalid params", true);
            return mcp::make_json_rpc_error(id, k_invalid_params, "Invalid tools/call params");
        }

        simdjson::dom::object params;
        if(params_root.get(params))
        {
            error_count_.fetch_add(1);
            log_activity("tool", "tools/call params must be an object", true);
            return mcp::make_json_rpc_error(id, k_invalid_params, "tools/call params must be an object");
        }

        std::string_view tool_name_view;
        if(params["name"].get(tool_name_view))
        {
            error_count_.fetch_add(1);
            log_activity("tool", "tools/call missing name", true);
            return mcp::make_json_rpc_error(id, k_invalid_params, "tools/call missing name");
        }
        const std::string tool_name(tool_name_view);

        const auto* tool = registry_.find(tool_name);
        if(!tool)
        {
            error_count_.fetch_add(1);
            log_activity("tool", "Unknown tool: " + tool_name, true);
            return mcp::make_json_rpc_error(id, k_method_not_found, "Unknown tool: " + tool_name);
        }

        std::string args_json = "{}";
        simdjson::dom::element args_el;
        if(!params["arguments"].get(args_el))
        {
            args_json = std::string(simdjson::minify(args_el));
        }

        if(tool->requires_main_thread)
        {
            return run_on_main_thread(
                [this, tool_name, args_json, id]() -> std::string
                {
                    return execute_tool_call(tool_name, args_json, id);
                },
                std::chrono::milliseconds(15000));
        }

        // Action+wait tools: run on HTTP worker so waiting does not stall the frame loop.
        return execute_tool_call(tool_name, args_json, id);
    }

    error_count_.fetch_add(1);
    log_activity("rpc", "Method not found: " + request.method, true);
    return mcp::make_json_rpc_error(id, k_method_not_found, "Method not found: " + request.method);
}

auto mcp_manager::execute_tool_call(const std::string& tool_name,
                                    const std::string& args_json,
                                    const std::optional<std::string>& id) -> std::string
{
    const auto* tool = registry_.find(tool_name);
    if(!tool || !ctx_)
    {
        error_count_.fetch_add(1);
        return mcp::make_json_rpc_error(id, k_method_not_found, "Unknown tool: " + tool_name);
    }

    simdjson::dom::parser args_parser;
    simdjson::dom::element args_root;
    simdjson::dom::object args;
    if(args_parser.parse(args_json).get(args_root) || args_root.get(args))
    {
        error_count_.fetch_add(1);
        log_activity("tool", "Invalid arguments for " + tool_name, true);
        return mcp::make_json_rpc_error(id, k_invalid_params, "Invalid tool arguments");
    }

    tool_call_count_.fetch_add(1);
    auto result = tool->handler(*ctx_, args);
    if(result.is_error)
    {
        error_count_.fetch_add(1);
        log_activity("tool", fmt::format("{} -> error: {}", tool_name, result.text), true);
    }
    else
    {
        const auto preview = result.text.size() > 120 ? result.text.substr(0, 117) + "..." : result.text;
        const auto image_note = result.image_base64.empty() ? "" : " [image]";
        log_activity("tool", fmt::format("{} -> {}{}", tool_name, preview, image_note));
    }
    return mcp::make_json_rpc_result(id, mcp::make_tool_result(result));
}

} // namespace unravel
