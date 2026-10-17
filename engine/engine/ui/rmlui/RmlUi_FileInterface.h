#pragma once

#include <RmlUi/Core/FileInterface.h>

#include <filesystem/filesystem.h>

#include <istream>

namespace unravel
{


    /// RmlUi's documents, style sheets and fonts, read from the mounted packs first, then from disk.
    class RmlUi_FileInterface : public Rml::FileInterface
    {
        auto to_handle(std::istream* stream) -> Rml::FileHandle;

        auto to_stream(Rml::FileHandle file) -> std::istream*;

    public:
        static auto ResolveUrl(const Rml::String& url) -> Rml::String;

        /// @p url as an asset key: the protocol path of a file under a protocol root, or @p url
        /// unchanged. RmlUi joins a relative source onto its document's file path.
        static auto ResolveAssetKey(const Rml::String& url) -> Rml::String;
    
        auto Open(const Rml::String& url) -> Rml::FileHandle override;
    
        void Close(Rml::FileHandle file) override;
    
        auto Read(void* buffer, size_t size, Rml::FileHandle file) -> size_t override;
        
        auto Seek(Rml::FileHandle file, long offset, int origin) -> bool override;
    
        auto Tell(Rml::FileHandle file) -> size_t override;
    };

} // namespace unravel
