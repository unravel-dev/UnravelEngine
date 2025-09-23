#include "RmlUi_FileInterface.h"
#include <string_utils/utils.h>
#include <engine/assets/impl/asset_extensions.h>

namespace unravel
{
namespace
{
    auto resolve_compiled_key(const std::string& key) -> std::string
    {
        return string_utils::replace(key + ".asset", ex::get_data_directory(), ex::get_compiled_directory());
    }
}

auto RmlUi_FileInterface::to_handle(FILE* f) -> Rml::FileHandle
{
    return reinterpret_cast<Rml::FileHandle>(f);
}
auto RmlUi_FileInterface::to_file(Rml::FileHandle f) -> FILE*
{
    return reinterpret_cast<FILE*>(f);
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

auto RmlUi_FileInterface::Open(const Rml::String& url) -> Rml::FileHandle
{
    auto real = ResolveUrl(url);
    if(FILE* f = std::fopen(real.c_str(), "rb"))
    {
        return to_handle(f);
    }
    return 0; // not found
}

void RmlUi_FileInterface::Close(Rml::FileHandle file)
{
    if(!file)
    {
        return;
    }
    // If it was ours, it’s still just a FILE* here in this example.
    std::fclose(to_file(file));
}

auto RmlUi_FileInterface::Read(void* buffer, size_t size, Rml::FileHandle file) -> size_t
{
    return std::fread(buffer, 1, size, to_file(file));
}

auto RmlUi_FileInterface::Seek(Rml::FileHandle file, long offset, int origin) -> bool
{
    return std::fseek(to_file(file), offset, origin) == 0;
}

auto RmlUi_FileInterface::Tell(Rml::FileHandle file) -> size_t
{
    return std::ftell(to_file(file));
}
} // namespace unravel
