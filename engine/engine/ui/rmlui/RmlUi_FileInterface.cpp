#include "RmlUi_FileInterface.h"
#include <string_utils/utils.h>
#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/pack.h>

#include <cstdio>

namespace unravel
{
namespace
{
    auto resolve_compiled_key(const std::string& key) -> std::string
    {
        return string_utils::replace(key + ".asset", ex::get_data_directory(), ex::get_compiled_directory());
    }
}

auto RmlUi_FileInterface::to_handle(std::istream* stream) -> Rml::FileHandle
{
    return reinterpret_cast<Rml::FileHandle>(stream);
}
auto RmlUi_FileInterface::to_stream(Rml::FileHandle file) -> std::istream*
{
    return reinterpret_cast<std::istream*>(file);
}
auto RmlUi_FileInterface::ResolveUrl(const Rml::String& url) -> Rml::String
{
    auto key = fs::convert_to_protocol(url);
    if(fs::has_known_protocol(key))
    {
        auto compiled_path = resolve_compiled_key(key.string());
        auto real = fs::resolve_protocol(compiled_path).string();
        return real;
    }
    return url;
}

auto RmlUi_FileInterface::ResolveAssetKey(const Rml::String& url) -> Rml::String
{
    if(fs::has_known_protocol(url))
    {
        return url;
    }
    const auto key = fs::convert_to_protocol(url);
    return fs::has_known_protocol(key) ? key.generic_string() : url;
}

auto RmlUi_FileInterface::Open(const Rml::String& url) -> Rml::FileHandle
{
    auto stream = fs::open_readable_stream(ResolveUrl(url));
    return stream ? to_handle(stream.release()) : 0;
}

void RmlUi_FileInterface::Close(Rml::FileHandle file)
{
    delete to_stream(file);
}

// RmlUi reads, seeks and tells on the handle of a file that failed to open (a style sheet it
// cannot find, for one), so a null handle reads as an empty file.

auto RmlUi_FileInterface::Read(void* buffer, size_t size, Rml::FileHandle file) -> size_t
{
    auto* stream = to_stream(file);
    if(stream == nullptr)
    {
        return 0;
    }
    stream->read(static_cast<char*>(buffer), std::streamsize(size));
    const auto count = size_t(stream->gcount());
    // A short read at the end sets eof and fail; the next seek has to work.
    stream->clear();
    return count;
}

auto RmlUi_FileInterface::Seek(Rml::FileHandle file, long offset, int origin) -> bool
{
    auto* stream = to_stream(file);
    if(stream == nullptr)
    {
        return false;
    }
    const auto direction = origin == SEEK_END ? std::ios::end : (origin == SEEK_CUR ? std::ios::cur : std::ios::beg);
    stream->clear();
    stream->seekg(offset, direction);
    return !stream->fail();
}

auto RmlUi_FileInterface::Tell(Rml::FileHandle file) -> size_t
{
    auto* stream = to_stream(file);
    if(stream == nullptr)
    {
        return 0;
    }
    const auto position = stream->tellg();
    return position < 0 ? 0 : size_t(position);
}
} // namespace unravel
