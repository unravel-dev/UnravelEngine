#pragma once

#include <RmlUi/Core/FileInterface.h>

#include <filesystem/filesystem.h>

namespace unravel
{

    
    class RmlUi_FileInterface : public Rml::FileInterface
    {
        auto to_handle(FILE* f) -> Rml::FileHandle;

        auto to_file(Rml::FileHandle f) -> FILE*;

    public:
        static auto ResolveUrl(const Rml::String& url) -> Rml::String;
    
        auto Open(const Rml::String& url) -> Rml::FileHandle override;
    
        void Close(Rml::FileHandle file) override;
    
        auto Read(void* buffer, size_t size, Rml::FileHandle file) -> size_t override;
        
        auto Seek(Rml::FileHandle file, long offset, int origin) -> bool override;
    
        auto Tell(Rml::FileHandle file) -> size_t override;
    };

} // namespace unravel
