#pragma once
#include "associative_archive.h"
#include "binary_archive.h"

#include <hpp/string_view.hpp>

#include <istream>
#include <ostream>
#include <string>
#include <utility>

/**
 * @file document_archive.h
 * @brief Reading and writing documents in either format: JSON for files people read, binary for
 *        documents only the engine reads. Code given to these takes `auto& ar` and is the same for both.
 */
namespace serialization
{
/**
 * @brief Calls fn with the input archive that reads the document in the bytes: binary when they start
 *        with its magic, JSON otherwise. Returns what fn returns.
 */
template<typename F>
decltype(auto) read_document(const void* data, size_t size, F&& fn)
{
    if(is_binary_document(data, size))
    {
        ser20::iarchive_binary_t ar(static_cast<const uint8_t*>(data), size);
        return std::forward<F>(fn)(ar);
    }
    auto ar = ser20::create_iarchive_associative(static_cast<const char*>(data), size);
    return std::forward<F>(fn)(ar);
}

template<typename F>
decltype(auto) read_document(hpp::string_view bytes, F&& fn)
{
    return read_document(bytes.data(), bytes.size(), std::forward<F>(fn));
}

/// Reads the rest of the stream, then the document in it.
template<typename F>
decltype(auto) read_document(std::istream& stream, F&& fn)
{
    const auto bytes = tagged::read_rest(stream);
    return read_document(bytes.data(), bytes.size(), std::forward<F>(fn));
}

/**
 * @brief Calls fn with the output archive the output format in force selects (scoped_output_format):
 *        binary for output_format::binary, JSON otherwise.
 *
 * The document reaches the stream when the archive closes, after fn returns. A binary document needs
 * a stream that does not translate line ends: a string stream, or a file opened with std::ios::binary.
 */
template<typename F>
decltype(auto) write_document(std::ostream& stream, F&& fn)
{
    if(get_output_format() == output_format::binary)
    {
        ser20::oarchive_binary_t ar(stream);
        return std::forward<F>(fn)(ar);
    }
    auto ar = ser20::create_oarchive_associative(stream);
    return std::forward<F>(fn)(ar);
}
} // namespace serialization
