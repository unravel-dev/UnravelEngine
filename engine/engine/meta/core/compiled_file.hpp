#pragma once

#include <filesystem/file_ostream.h>
#include <filesystem/pack.h>
#include <serialization/binary_archive.h>

#include <hpp/source_location.hpp>

#include <stdexcept>
#include <string>

namespace unravel
{
/**
 * @brief Saves obj under name as the binary document of the compiled file at absolute_path.
 *
 * @return False when the file cannot be opened or the save fails (logged).
 */
template<typename T>
auto save_compiled_file(const std::string& absolute_path, const char* name, const T& obj) -> bool
{
    fs::file_ostream stream(absolute_path);
    if(!stream.is_open())
    {
        return false;
    }
    return serialization::write_binary(stream, [&](auto& ar) { return try_save(ar, ser20::make_nvp(name, obj)); });
}

/**
 * @brief save_compiled_file for a save that writes inside asset_writer::atomic_write_file: a failed save throws, so the
 *        compiled file in place is kept rather than replaced by part of the document.
 */
template<typename T>
void write_compiled_file(const std::string& absolute_path, const char* name, const T& obj)
{
    if(!save_compiled_file(absolute_path, name, obj))
    {
        throw std::runtime_error("The compiled file was not saved whole.");
    }
}

/**
 * @brief Loads obj, saved under name, from the compiled file at absolute_path: in place from a mounted pack, else
 *        read from disk whole (fs::read_readable_file).
 *
 * @return False when there is no such file, or it does not load (logged, as from where this is called).
 */
template<typename T>
auto load_compiled_file(const std::string& absolute_path,
                        const char* name,
                        T& obj,
                        const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    const auto file = fs::read_readable_file(absolute_path);
    if(!file.is_found())
    {
        return false;
    }
    return serialization::read_binary(
        file.data,
        file.size,
        [&](auto& ar) { return try_load(ar, ser20::make_nvp(name, obj)); },
        loc);
}
} // namespace unravel
