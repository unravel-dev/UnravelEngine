#include "mcp_protocol.h"

#include "mcp_tool_registry.h"

#include <logging/logging.h>
#include <ser20/external/simdjson/simdjson.h>

#include <array>
#include <cstring>

namespace unravel::mcp
{
namespace
{

/// Length of the non-finite number fmt printed at `pos` ("nan", "-inf", ...), or 0 when none starts there.
auto non_finite_token_length(const std::string& text, size_t pos) -> size_t
{
    constexpr std::array<const char*, 4> tokens{"-nan", "-inf", "nan", "inf"};
    constexpr const char* token_end = ",]} \t\r\n";
    for(const char* token : tokens)
    {
        const size_t length = std::strlen(token);
        const size_t end = pos + length;
        if(text.compare(pos, length, token) == 0 && (end == text.size() || std::strchr(token_end, text[end]) != nullptr))
        {
            return length;
        }
    }
    return 0;
}

/**
 * @brief `json` with every bare nan / inf number replaced by null. JSON has neither, and one float
 *        printed that way (an unloaded mesh's bounds, a degenerate rotation) would make the whole
 *        result unparsable. Strings are copied as they are.
 */
auto replace_non_finite_numbers(const std::string& json) -> std::string
{
    std::string out;
    out.reserve(json.size());
    bool in_string = false;
    for(size_t i = 0; i < json.size(); ++i)
    {
        const char c = json[i];
        if(in_string)
        {
            out.push_back(c);
            if(c == '\\' && i + 1 < json.size())
            {
                out.push_back(json[++i]);
            }
            else if(c == '"')
            {
                in_string = false;
            }
            continue;
        }
        if(const size_t length = non_finite_token_length(json, i); length > 0)
        {
            out += "null";
            i += length - 1;
            continue;
        }
        in_string = c == '"';
        out.push_back(c);
    }
    return out;
}

} // namespace

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
    // Warnings join a JSON object result as its "warnings" field and follow any other text as a
    // block of their own, so a client parsing the result as JSON still can.
    const bool is_json = !result.text.empty() && (result.text.front() == '{' || result.text.front() == '[');
    std::string text = is_json ? replace_non_finite_numbers(result.text) : result.text;
    std::string warnings_block;
    if(!result.warnings.empty())
    {
        std::string warnings = "[";
        for(size_t i = 0; i < result.warnings.size(); ++i)
        {
            warnings += (i > 0 ? "," : "") + make_json_string(result.warnings[i]);
        }
        warnings += "]";
        if(text.size() >= 2 && text.front() == '{' && text.back() == '}')
        {
            text.insert(text.size() - 1, (text.size() > 2 ? R"(,"warnings":)" : R"("warnings":)") + warnings);
        }
        else
        {
            warnings_block = fmt::format(R"(,{{"type":"text","text":{}}})",
                                         make_json_string(R"({"warnings":)" + warnings + "}"));
        }
    }
    std::string content = "[";
    content += fmt::format(R"({{"type":"text","text":{}}})", make_json_string(text));
    content += warnings_block;
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
