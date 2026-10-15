#include "mcp_manager.h"

#include "mcp/mcp_protocol.h"

#include <logging/logging.h>
#include <version/version.h>

#include <httplib.h>
// After httplib: it needs winsock2 before anything pulls in the older windows headers.
#include <editor/editing/editing_manager.h>
#include <ser20/external/simdjson/simdjson.h>

#include <hpp/string_view.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>
#include <vector>

namespace unravel
{

namespace
{
/// Developer builds expose the engine diagnostics by default; a shipped editor does not.
#if defined(UNRAVEL_DEVELOPER_BUILD)
constexpr auto k_default_profile = mcp::mcp_profile::developer;
#else
constexpr auto k_default_profile = mcp::mcp_profile::user;
#endif
} // namespace

namespace
{
/// Protocol revisions this server speaks, newest first. initialize answers with the client's
/// revision when it is one of these, else with the newest.
constexpr std::array<hpp::string_view, 3> k_protocol_versions{"2025-06-18", "2025-03-26", "2024-11-05"};

/// The host part of an Origin ("http://localhost:3000") or Host ("127.0.0.1:27182") value.
auto host_of(std::string value) -> std::string
{
    const auto scheme = value.find("://");
    if(scheme != std::string::npos)
    {
        value = value.substr(scheme + 3);
    }
    if(!value.empty() && value.front() == '[')
    {
        return value.substr(0, value.find(']') + 1);
    }
    return value.substr(0, value.find_first_of(":/"));
}

auto is_local_host(const std::string& host) -> bool
{
    std::string lower(host);
    std::transform(lower.begin(),
                   lower.end(),
                   lower.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return lower == "127.0.0.1" || lower == "localhost" || lower == "[::1]";
}

/**
 * @brief Whether a request may reach the tools. Local MCP clients send no Origin and name the
 *        loopback host; a web page sends its own Origin, and a DNS-rebinding page its own Host,
 *        so both are refused - otherwise any site open in a browser could call every tool.
 *        A JSON content type keeps plain HTML forms out as well.
 */
auto is_allowed_request(const httplib::Request& req, std::string& refusal) -> bool
{
    if(req.has_header("Origin") && !is_local_host(host_of(req.get_header_value("Origin"))))
    {
        refusal = "Requests from web pages are refused (Origin " + req.get_header_value("Origin") + ")";
        return false;
    }
    if(req.has_header("Host") && !is_local_host(host_of(req.get_header_value("Host"))))
    {
        refusal = "Requests must address the loopback host (Host " + req.get_header_value("Host") + ")";
        return false;
    }
    if(req.get_header_value("Content-Type").rfind("application/json", 0) != 0)
    {
        refusal = "Content-Type must be application/json";
        return false;
    }
    return true;
}

/// The protocol revision to answer an initialize request with.
auto negotiate_protocol_version(const std::string& params_json) -> hpp::string_view
{
    simdjson::dom::parser parser;
    simdjson::dom::element params;
    hpp::string_view requested;
    if(!parser.parse(params_json).get(params) && !params["protocolVersion"].get(requested))
    {
        const auto known = std::find(k_protocol_versions.begin(), k_protocol_versions.end(), requested);
        if(known != k_protocol_versions.end())
        {
            return *known;
        }
    }
    return k_protocol_versions.front();
}

/// Words of a tool name, for matching a near miss against real names.
auto split_tool_words(const std::string& name) -> std::vector<std::string>
{
    std::vector<std::string> words;
    std::string word;
    for(const char c : name)
    {
        if(c == '_')
        {
            if(!word.empty())
            {
                words.push_back(word);
            }
            word.clear();
            continue;
        }
        word.push_back(c);
    }
    if(!word.empty())
    {
        words.push_back(word);
    }
    return words;
}
} // namespace

auto mcp_manager::unknown_tool_message(const std::string& tool_name) const -> std::string
{
    // A guessed name usually shares most of its words with the real one
    // (scene_set_transform -> scene_set_transforms_batch), so rank by shared words.
    const auto wanted = split_tool_words(tool_name);
    std::vector<std::pair<size_t, std::string>> scored;
    for(const auto& tool : registry_.tools())
    {
        if(!registry_.is_available(tool))
        {
            continue;
        }
        size_t shared = 0;
        for(const auto& word : split_tool_words(tool.name))
        {
            for(const auto& want : wanted)
            {
                if(word == want || (want.size() >= 4 && word.rfind(want, 0) == 0))
                {
                    ++shared;
                    break;
                }
            }
        }
        if(shared > 0)
        {
            scored.emplace_back(shared, tool.name);
        }
    }
    std::sort(scored.begin(),
              scored.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  return lhs.first != rhs.first ? lhs.first > rhs.first : lhs.second < rhs.second;
              });
    std::string message = "Unknown tool: " + tool_name;
    constexpr size_t k_max_suggestions = 3;
    for(size_t i = 0; i < scored.size() && i < k_max_suggestions; ++i)
    {
        message += (i == 0 ? ". Did you mean: " : ", ") + scored[i].second;
    }
    return message;
}

mcp_manager::mcp_manager(cmd_line::parser& parser)
{
    parser.set_optional<int>("", "mcp-port", k_default_port, "Port of the editor MCP server.");
    parser.set_optional<std::string>("",
                                     "mcp-profile",
                                     std::string(mcp::to_string(k_default_profile)),
                                     "MCP tool surface: user (game authoring) or developer (adds "
                                     "engine diagnostics).");
}

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
/// How long a main-thread tool waits for the main thread to pick it up; once picked up it runs to the end.
constexpr auto k_main_thread_tool_timeout = std::chrono::milliseconds(15000);
} // namespace

auto mcp_manager::init(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    ctx_ = &ctx;
    int port = k_default_port;
    if(parser.try_get("mcp-port", port) && port > 0 && port <= k_max_port)
    {
        port_ = port;
    }

    auto profile = k_default_profile;
    std::string profile_name;
    if(parser.try_get("mcp-profile", profile_name) && !profile_name.empty() &&
       !mcp::parse_mcp_profile(profile_name, profile))
    {
        APPLOG_WARNING("Unknown --mcp-profile '{}'; using '{}'",
                       profile_name,
                       mcp::to_string(k_default_profile));
        profile = k_default_profile;
    }
    registry_.set_profile(profile);

    mcp::register_scene_tools(registry_);
    mcp::register_scene_batch_tools(registry_);
    mcp::register_create_tools(registry_);
    mcp::register_ops_batch_tools(registry_);
    mcp::register_asset_tools(registry_);
    mcp::register_viewport_tools(registry_);
    mcp::register_material_tools(registry_);
    mcp::register_asset_property_tools(registry_);
    mcp::register_project_tools(registry_);
    mcp::register_settings_tools(registry_);
    mcp::register_deploy_tools(registry_);
    mcp::register_input_tools(registry_);
    mcp::register_input_action_tools(registry_);
    mcp::register_ui_tools(registry_);
    mcp::register_ui_inspect_tools(registry_);
    mcp::register_prefab_tools(registry_);
    mcp::register_prefab_instance_tools(registry_);
    mcp::register_spatial_tools(registry_);
    mcp::register_wait_tools(registry_);
    mcp::register_script_tools(registry_);
    mcp::register_editor_tools(registry_);
    mcp::register_skill_tools(registry_);

    const auto visible = registry_.visible_tool_count();
    const auto total = registry_.tools().size();
    APPLOG_INFO("MCP profile '{}': {} of {} tools exposed",
                mcp::to_string(registry_.get_profile()),
                visible,
                total);
    log_activity("lifecycle",
                 fmt::format("Registered {} tools ({} exposed on the '{}' profile)",
                             total,
                             visible,
                             mcp::to_string(registry_.get_profile())));

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

    // No CORS headers and no preflight route: browsers must not reach the tools.
    server_->Post("/mcp",
                  [this](const httplib::Request& req, httplib::Response& res)
                  {
                      std::string refusal;
                      if(!is_allowed_request(req, refusal))
                      {
                          error_count_.fetch_add(1);
                          log_activity("http", refusal, true);
                          res.status = 403;
                          res.set_content(mcp::make_json_rpc_error(std::nullopt, k_invalid_request, refusal),
                                          "application/json");
                          return;
                      }
                      auto response = handle_http_request(req.body);
                      if(response.empty())
                      {
                          // A notification: accepted, nothing to answer.
                          res.status = 202;
                          return;
                      }
                      res.status = 200;
                      res.set_content(response, "application/json");
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

auto mcp_manager::get_visible_tool_count() const -> size_t
{
    return registry_.visible_tool_count();
}

auto mcp_manager::get_profile() const -> mcp::mcp_profile
{
    return registry_.get_profile();
}

auto mcp_manager::get_profile_name() const -> const char*
{
    return mcp::to_string(registry_.get_profile());
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

void mcp_manager::report_main_thread_failure(const std::string& reason, std::string* failure)
{
    error_count_.fetch_add(1);
    log_activity("rpc", reason, true);
    if(failure != nullptr)
    {
        *failure = reason;
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
        const auto protocol_version = negotiate_protocol_version(request.params_json);
        const auto result = fmt::format(
            R"({{"protocolVersion":{},"capabilities":{{"tools":{{}}}},"serverInfo":{{"name":"unravel-editor","version":{}}},"instructions":{}}})",
            mcp::make_json_string(std::string(protocol_version.data(), protocol_version.size())),
            mcp::make_json_string(version::get_full()),
            mcp::make_json_string(mcp::get_server_instructions()));
        return mcp::make_json_rpc_result(id, result);
    }

    // Notifications (no id) get no response body; the transport answers 202.
    if(!request.has_id && request.method.rfind("notifications/", 0) == 0)
    {
        log_activity("rpc", request.method);
        return {};
    }

    if(request.method == "ping")
    {
        return mcp::make_json_rpc_result(id, "{}");
    }

    if(request.method == "tools/list")
    {
        log_activity("rpc", fmt::format("tools/list ({} tools)", registry_.visible_tool_count()));
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
            return mcp::make_json_rpc_error(id, k_method_not_found, unknown_tool_message(tool_name));
        }

        if(!registry_.is_available(*tool))
        {
            error_count_.fetch_add(1);
            log_activity("tool", "Tool outside the active profile: " + tool_name, true);
            return mcp::make_json_rpc_error(
                id,
                k_method_not_found,
                fmt::format("Tool '{}' is developer-only and this editor runs the '{}' MCP profile. "
                            "Restart the editor with --mcp-profile=developer to expose it.",
                            tool_name,
                            mcp::to_string(registry_.get_profile())));
        }

        std::string args_json = "{}";
        simdjson::dom::element args_el;
        if(!params["arguments"].get(args_el))
        {
            args_json = std::string(simdjson::minify(args_el));
        }

        if(tool->requires_main_thread)
        {
            // On-process: a tool that changes the scene (opens it, destroys entities) must not run from a wait of
            // the main thread, such as a render pass blocked on an asset, which would free what that pass uses.
            std::string failure;
            auto response = invoke_on_main(
                [this, &tool_name, &args_json, &id]() -> std::string
                {
                    return execute_tool_call(tool_name, args_json, id);
                },
                k_main_thread_tool_timeout,
                &failure);
            return response ? *response : mcp::make_json_rpc_error(id, k_internal_error, failure);
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
    // Editor actions are undoable only inside an undo scope, which the panels open around their own
    // edits. A tool call runs outside any panel - or, by chance, nested inside one that is waiting
    // on assets - so without a scope of its own its undo would depend on timing.
    struct undo_scope
    {
        editing_manager* em{};
        explicit undo_scope(editing_manager* manager) : em(manager)
        {
            if(em)
            {
                em->push_undo_stack_enabled(true);
            }
        }
        ~undo_scope()
        {
            if(em)
            {
                em->pop_undo_stack_enabled();
            }
        }
        undo_scope(const undo_scope&) = delete;
        auto operator=(const undo_scope&) -> undo_scope& = delete;
    };
    const bool opens_undo_scope = tool->mutates_scene && tool->requires_main_thread && ctx_->has<editing_manager>();
    const undo_scope scope(opens_undo_scope ? &ctx_->get_cached<editing_manager>() : nullptr);
    if(opens_undo_scope)
    {
        // One call is one undo step, never merged into the previous call's.
        ctx_->get_cached<editing_manager>().break_undo_merge();
    }
    auto result = tool->handler(*ctx_, args);
    auto unknown_arguments = mcp::find_unknown_arguments(*tool, args_root);
    result.warnings.insert(result.warnings.begin(), unknown_arguments.begin(), unknown_arguments.end());
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
