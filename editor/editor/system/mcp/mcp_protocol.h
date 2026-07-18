#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace unravel::mcp
{

struct tool_result;

struct json_rpc_request
{
    std::string method;
    std::string params_json;
    std::optional<std::string> id_json;
    bool has_id{false};
};

auto json_escape(const std::string& value) -> std::string;
auto make_json_string(const std::string& value) -> std::string;
auto make_json_rpc_result(const std::optional<std::string>& id_json, const std::string& result_json) -> std::string;
auto make_json_rpc_error(const std::optional<std::string>& id_json, int code, const std::string& message) -> std::string;
auto make_tool_result(const std::string& text, bool is_error = false) -> std::string;
auto make_tool_result(const tool_result& result) -> std::string;

auto parse_json_rpc_request(const std::string& body, json_rpc_request& out, std::string& error) -> bool;

} // namespace unravel::mcp
