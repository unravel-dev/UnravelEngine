#include "document_convert.h"

#include "archives/tagged_binary.hpp"

#include <ser20/external/simdjson/simdjson.h>

#include <climits>

namespace serialization
{
namespace
{
void convert_json_value(tagged::writer& out, simdjson::dom::element value);

void convert_json_members(tagged::writer& out, simdjson::dom::object object)
{
    for(auto member : object)
    {
        // The writer keeps a name's text by address until the document is finished, and the
        // parser's keys stay put while it lives.
        out.key(member.key.data(), ser20::name_hash(member.key.data(), member.key.size()));
        convert_json_value(out, member.value);
    }
}

void convert_json_value(tagged::writer& out, simdjson::dom::element value)
{
    using simdjson::dom::element_type;
    switch(value.type())
    {
        case element_type::OBJECT:
            out.begin_object();
            convert_json_members(out, value.get_object().value_unsafe());
            out.end_node();
            break;
        case element_type::ARRAY:
            out.begin_array();
            for(auto element : value.get_array().value_unsafe())
            {
                convert_json_value(out, element);
            }
            out.end_node();
            break;
        case element_type::INT64:
        {
            const int64_t number = value.get_int64().value_unsafe();
            if(number >= INT32_MIN && number <= INT32_MAX)
            {
                out.value_number(static_cast<int32_t>(number));
            }
            else
            {
                out.value_number(number);
            }
            break;
        }
        case element_type::UINT64:
            out.value_number(value.get_uint64().value_unsafe());
            break;
        case element_type::DOUBLE:
        {
            const double number = value.get_double().value_unsafe();
            const auto as_float = static_cast<float>(number);
            if(static_cast<double>(as_float) == number)
            {
                out.value_number(as_float);
            }
            else
            {
                out.value_number(number);
            }
            break;
        }
        case element_type::STRING:
        {
            const auto text = value.get_string().value_unsafe();
            out.value_string(text.data(), text.size());
            break;
        }
        case element_type::BOOL:
            out.value_bool(value.get_bool().value_unsafe());
            break;
        case element_type::NULL_VALUE:
            out.value_null();
            break;
    }
}
} // namespace

auto json_to_binary_document(hpp::string_view json, std::string& error) -> std::vector<uint8_t>
{
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if(const auto failure = parser.parse(json.data(), json.size()).get(root); failure != simdjson::SUCCESS)
    {
        error = simdjson::error_message(failure);
        return {};
    }
    if(!root.is_object())
    {
        error = "The document's top level is not an object.";
        return {};
    }
    try
    {
        tagged::writer out;
        convert_json_members(out, root.get_object().value_unsafe());
        return out.take();
    }
    catch(const std::exception& e)
    {
        error = e.what();
        return {};
    }
}

} // namespace serialization
