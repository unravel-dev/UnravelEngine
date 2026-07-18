#include "mcp_protocol.h"

#include "mcp_tool_registry.h"

#include <logging/logging.h>
#include <ser20/external/simdjson/simdjson.h>

namespace unravel::mcp
{

auto json_escape(const std::string& value) -> std::string
{
    std::string out;
    out.reserve(value.size() + 8);
    for(char c : value)
    {
        switch(c)
        {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if(static_cast<unsigned char>(c) < 0x20)
                {
                    out += fmt::format("\\u{:04x}", static_cast<unsigned char>(c));
                }
                else
                {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

auto make_json_string(const std::string& value) -> std::string
{
    return "\"" + json_escape(value) + "\"";
}

auto make_json_rpc_result(const std::optional<std::string>& id_json, const std::string& result_json) -> std::string
{
    const std::string id = id_json ? *id_json : "null";
    return fmt::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, result_json);
}

auto make_json_rpc_error(const std::optional<std::string>& id_json, int code, const std::string& message) -> std::string
{
    const std::string id = id_json ? *id_json : "null";
    return fmt::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":{},"message":{}}}}})",
                       id,
                       code,
                       make_json_string(message));
}

auto make_tool_result(const std::string& text, bool is_error) -> std::string
{
    tool_result result;
    result.text = text;
    result.is_error = is_error;
    return make_tool_result(result);
}

auto make_tool_result(const tool_result& result) -> std::string
{
    std::string content = "[";
    content += fmt::format(R"({{"type":"text","text":{}}})", make_json_string(result.text));
    if(!result.image_base64.empty())
    {
        content += fmt::format(R"(,{{"type":"image","data":{},"mimeType":{}}})",
                               make_json_string(result.image_base64),
                               make_json_string(result.image_mime.empty() ? "image/png" : result.image_mime));
    }
    content += "]";
    return fmt::format(R"({{"content":{},"isError":{}}})", content, result.is_error ? "true" : "false");
}

auto parse_json_rpc_request(const std::string& body, json_rpc_request& out, std::string& error) -> bool
{
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    auto parse_error = parser.parse(body).get(root);
    if(parse_error)
    {
        error = "Invalid JSON body";
        return false;
    }

    simdjson::dom::object obj;
    if(root.get(obj))
    {
        error = "JSON-RPC request must be an object";
        return false;
    }

    std::string_view method;
    if(obj["method"].get(method))
    {
        error = "Missing JSON-RPC method";
        return false;
    }
    out.method.assign(method);

    simdjson::dom::element params_el;
    if(!obj["params"].get(params_el))
    {
        out.params_json = std::string(simdjson::minify(params_el));
    }
    else
    {
        out.params_json = "{}";
    }

    simdjson::dom::element id_el;
    if(!obj["id"].get(id_el))
    {
        out.has_id = true;
        out.id_json = std::string(simdjson::minify(id_el));
    }
    else
    {
        out.has_id = false;
        out.id_json.reset();
    }

    return true;
}

} // namespace unravel::mcp
