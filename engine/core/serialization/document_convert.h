#pragma once

#include "archives/tagged_binary.hpp"

#include <hpp/string_view.hpp>

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace serialization
{
/**
 * @brief Converts a JSON document written by the associative archive into a tagged binary document
 *        that loads the same.
 *
 * Every member keeps its name and order. Numbers keep what they hold: integers as 32-bit when they
 * fit, fractions as float when float holds them exactly and as double otherwise; the binary reader
 * converts to whatever a field asks for. Lists of numbers stay lists, which the binary reader reads
 * like the blobs its own writer makes.
 *
 * @param json The document.
 * @param error Set to why the document did not convert; left alone on success.
 * @return The binary document, empty when the JSON does not parse or its top level is not an object.
 */
auto json_to_binary_document(hpp::string_view json, std::string& error) -> std::vector<uint8_t>;

/**
 * @brief Visits every value of a document in either format, depth first in document order, as
 *        tagged::walk_values does: a binary document in place, a JSON one converted first. For code that
 *        looks through a document without loading it, written once for both formats.
 *
 * @return False when the bytes hold neither format or stop reading partway, or fn ended the walk; the
 *         values visited before stand.
 */
template<typename F>
auto walk_document(const void* data, size_t size, F&& fn) -> bool
{
    try
    {
        if(tagged::is_document(data, size))
        {
            tagged::reader document(static_cast<const uint8_t*>(data), size);
            return tagged::walk_values(document, fn);
        }
        std::string error;
        const auto converted = json_to_binary_document(hpp::string_view(static_cast<const char*>(data), size), error);
        if(converted.empty())
        {
            return false;
        }
        tagged::reader document(converted.data(), converted.size());
        return tagged::walk_values(document, fn);
    }
    catch(const std::exception&)
    {
        return false;
    }
}

} // namespace serialization
