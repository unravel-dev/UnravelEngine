#pragma once

#include <cstdint>
#include <hpp/optional.hpp>
#include <string>

namespace unravel::mcp
{

struct tool_result;

struct json_rpc_request
{
    std::string method;
    std::string params_json;
    hpp::optional<std::string> id_json;
    bool has_id{false};
};

auto json_escape(const std::string& value) -> std::string;
auto make_json_string(const std::string& value) -> std::string;
auto make_json_rpc_result(const hpp::optional<std::string>& id_json, const std::string& result_json) -> std::string;
auto make_json_rpc_error(const hpp::optional<std::string>& id_json, int code, const std::string& message) -> std::string;
auto make_tool_result(const std::string& text, bool is_error = false) -> std::string;
auto make_tool_result(const tool_result& result) -> std::string;

/// Why a body is not a request JSON-RPC answers: it is no JSON, or it is JSON but no single request object.
enum class json_rpc_parse_error : uint8_t
{
    none,
    not_json,
    not_a_request,
};

auto parse_json_rpc_request(const std::string& body, json_rpc_request& out, std::string& error) -> json_rpc_parse_error;

} // namespace unravel::mcp
