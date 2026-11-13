#include "package_manifest.h"

#include <engine/meta/core/common/basetypes.hpp>
#include <filesystem/file_fingerprint.h>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace unravel::packages
{

SAVE(package_file)
{
    try_save(ar, ser20::make_nvp("path", obj.path));
    try_save(ar, ser20::make_nvp("uid", obj.uid));
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("size", obj.size));
    try_save(ar, ser20::make_nvp("hash", obj.hash));
    try_save(ar, ser20::make_nvp("meta_hash", obj.meta_hash));
    if(obj.document_version != 0)
    {
        try_save(ar, ser20::make_nvp("document_version", obj.document_version));
    }
    if(!obj.script_types.empty())
    {
        try_save(ar, ser20::make_nvp("script_types", obj.script_types));
    }
}

LOAD(package_file)
{
    try_load(ar, ser20::make_nvp("path", obj.path));
    try_load(ar, ser20::make_nvp("uid", obj.uid));
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("size", obj.size));
    try_load(ar, ser20::make_nvp("hash", obj.hash));
    try_load(ar, ser20::make_nvp("meta_hash", obj.meta_hash));
    try_load(ar, ser20::make_nvp("document_version", obj.document_version));
    try_load(ar, ser20::make_nvp("script_types", obj.script_types));
}

SAVE(package_dependency)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("range", obj.range));
}

LOAD(package_dependency)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("range", obj.range));
}

SAVE(package_engine_asset)
{
    try_save(ar, ser20::make_nvp("uid", obj.uid));
    try_save(ar, ser20::make_nvp("key", obj.key));
}

LOAD(package_engine_asset)
{
    try_load(ar, ser20::make_nvp("uid", obj.uid));
    try_load(ar, ser20::make_nvp("key", obj.key));
}

/// Saves a field only when it holds something: a folder's package.json then lists what its author set.
template<typename Archive, typename T>
void save_if_set(Archive& ar, const char* name, const T& value)
{
    if(!value.empty())
    {
        try_save(ar, ser20::make_nvp(name, value));
    }
}

SAVE(package_manifest)
{
    try_save(ar, ser20::make_nvp("format_version", obj.format_version));
    try_save(ar, ser20::make_nvp("name", obj.name));
    save_if_set(ar, "display_name", obj.display_name);
    try_save(ar, ser20::make_nvp("version", obj.version));
    save_if_set(ar, "description", obj.description);
    save_if_set(ar, "author", obj.author);
    save_if_set(ar, "license", obj.license);
    save_if_set(ar, "exported_with", obj.exported_with);
    save_if_set(ar, "minimum_engine", obj.minimum_engine);
    save_if_set(ar, "root", obj.root);
    save_if_set(ar, "entry_scene", obj.entry_scene);
    save_if_set(ar, "dependencies", obj.dependencies);
    save_if_set(ar, "engine_assets", obj.engine_assets);
    save_if_set(ar, "script_types", obj.script_types);
    save_if_set(ar, "files", obj.files);
}

LOAD(package_manifest)
{
    try_load(ar, ser20::make_nvp("format_version", obj.format_version));
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("display_name", obj.display_name));
    try_load(ar, ser20::make_nvp("version", obj.version));
    try_load(ar, ser20::make_nvp("description", obj.description));
    try_load(ar, ser20::make_nvp("author", obj.author));
    try_load(ar, ser20::make_nvp("license", obj.license));
    try_load(ar, ser20::make_nvp("exported_with", obj.exported_with));
    try_load(ar, ser20::make_nvp("minimum_engine", obj.minimum_engine));
    try_load(ar, ser20::make_nvp("root", obj.root));
    try_load(ar, ser20::make_nvp("entry_scene", obj.entry_scene));
    try_load(ar, ser20::make_nvp("dependencies", obj.dependencies));
    try_load(ar, ser20::make_nvp("engine_assets", obj.engine_assets));
    try_load(ar, ser20::make_nvp("script_types", obj.script_types));
    try_load(ar, ser20::make_nvp("files", obj.files));
}

namespace
{
constexpr const char* MANIFEST_NODE_NAME = "package";

auto is_name_char(char value) -> bool
{
    const auto c = static_cast<unsigned char>(value);
    return (std::islower(c) != 0) || (std::isdigit(c) != 0) || value == '.' || value == '-' || value == '_';
}
} // namespace

auto package_manifest::get_title() const -> const std::string&
{
    return display_name.empty() ? name : display_name;
}

auto package_manifest::find_file(const std::string& path) const -> const package_file*
{
    const auto key = fs::make_path_key(path);
    const auto found = std::find_if(files.begin(),
                                    files.end(),
                                    [&key](const package_file& file)
                                    {
                                        return fs::make_path_key(file.path) == key;
                                    });
    return found == files.end() ? nullptr : &*found;
}

auto check_package_name(const std::string& name) -> std::string
{
    if(name.empty())
    {
        return "A package needs a name, such as com.studio.ui-kit.";
    }
    if(name.size() > PACKAGE_NAME_MAX_LENGTH)
    {
        return "The name is longer than " + std::to_string(PACKAGE_NAME_MAX_LENGTH) + " characters.";
    }
    if(std::islower(static_cast<unsigned char>(name.front())) == 0)
    {
        return "The name starts with a lower case letter.";
    }
    if(!std::all_of(name.begin(), name.end(), is_name_char))
    {
        return "The name holds only lower case letters, digits, dots, dashes and underscores.";
    }
    return {};
}

auto write_manifest(const package_manifest& manifest) -> std::string
{
    std::ostringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp(MANIFEST_NODE_NAME, manifest));
    }
    return stream.str();
}

auto read_manifest(const std::string& text, package_manifest& manifest, std::string& err) -> bool
{
    manifest = {};
    try
    {
        std::istringstream stream(text);
        auto ar = ser20::create_iarchive_associative(stream);
        if(!try_load(ar, ser20::make_nvp(MANIFEST_NODE_NAME, manifest)))
        {
            err = "It holds no package manifest.";
            return false;
        }
    }
    catch(const std::exception& e)
    {
        err = std::string("Its manifest does not read: ") + e.what();
        return false;
    }
    if(manifest.format_version > PACKAGE_FORMAT_VERSION)
    {
        err = "It was made in a newer package format (" + std::to_string(manifest.format_version) +
              ") than this editor reads (" + std::to_string(PACKAGE_FORMAT_VERSION) + "). Update the editor to read it.";
        return false;
    }
    return true;
}

auto save_manifest(const fs::path& file, const package_manifest& manifest, std::string& err) -> bool
{
    std::ofstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        err = "Could not write " + file.generic_string() + ".";
        return false;
    }
    const auto text = write_manifest(manifest);
    stream.write(text.data(), std::streamsize(text.size()));
    stream.close();
    if(!stream.good())
    {
        err = "Could not write " + file.generic_string() + ".";
        return false;
    }
    return true;
}

auto load_manifest(const fs::path& file, package_manifest& manifest, std::string& err) -> bool
{
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        err = "Could not read " + file.generic_string() + ".";
        return false;
    }
    std::ostringstream text;
    text << stream.rdbuf();
    return read_manifest(text.str(), manifest, err);
}

auto is_text(const fs::byte_array_t& bytes) -> bool
{
    return std::find(bytes.begin(), bytes.end(), std::uint8_t(0)) == bytes.end();
}

auto fingerprint_bytes(const fs::byte_array_t& bytes) -> std::string
{
    if(!is_text(bytes))
    {
        return fs::hash_bytes_fingerprint(bytes.data(), bytes.size());
    }
    fs::byte_array_t normalized;
    normalized.reserve(bytes.size());
    for(std::size_t index = 0; index < bytes.size(); ++index)
    {
        const bool is_crlf = bytes[index] == '\r' && index + 1 < bytes.size() && bytes[index + 1] == '\n';
        if(!is_crlf)
        {
            normalized.push_back(bytes[index]);
        }
    }
    return fs::hash_bytes_fingerprint(normalized.data(), normalized.size());
}

auto fingerprint_file(const fs::path& file) -> std::string
{
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        return {};
    }
    const fs::byte_array_t bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return fingerprint_bytes(bytes);
}

auto get_package_file_name(const package_manifest& manifest) -> std::string
{
    return manifest.name + "-" + manifest.version + PACKAGE_EXTENSION;
}

} // namespace unravel::packages
