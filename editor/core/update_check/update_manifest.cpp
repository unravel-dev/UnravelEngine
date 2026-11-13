#include "update_manifest.h"

#include <ser20/external/simdjson/simdjson.h>

#include <algorithm>
#include <cctype>

namespace unravel::update_check
{
namespace
{

constexpr hpp::string_view PACKAGE_NAME_PREFIX = "UnravelEngine-";
constexpr hpp::string_view PACKAGE_EXTENSION = ".zip";

auto is_platform_char(char c) -> bool
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
}

/// The string field `name` of a JSON object, or nothing when it is missing or not a string.
auto get_string_field(const simdjson::dom::element& object, const char* name) -> hpp::optional<hpp::string_view>
{
    hpp::string_view value;
    if(object[name].get(value) != simdjson::SUCCESS)
    {
        return hpp::nullopt;
    }
    return value;
}

/// One entry of the "builds" array, when it holds a valid build.
auto read_build(const simdjson::dom::element& entry) -> hpp::optional<release_build>
{
    const auto platform = get_string_field(entry, "platform");
    const auto version_text = get_string_field(entry, "version");
    const auto tag = get_string_field(entry, "tag");
    const auto asset = get_string_field(entry, "asset");
    if(!platform || !version_text || !tag || !asset || !is_platform_name(*platform) || !is_release_tag(*tag))
    {
        return hpp::nullopt;
    }
    const auto version = parse_package_version(*version_text);
    if(!version || *asset != get_package_file_name(*version, *platform))
    {
        return hpp::nullopt;
    }
    return release_build{std::string(*platform), *version, std::string(*tag)};
}

/// The "builds" array of a manifest object of the schema this editor reads.
auto get_builds_array(const simdjson::dom::element& root) -> hpp::optional<simdjson::dom::array>
{
    std::uint64_t schema = 0;
    simdjson::dom::array builds;
    if(!root.is_object() || root["schema"].get(schema) != simdjson::SUCCESS || schema != MANIFEST_SCHEMA ||
       root["builds"].get(builds) != simdjson::SUCCESS)
    {
        return hpp::nullopt;
    }
    return builds;
}

} // namespace

auto read_update_manifest(hpp::string_view text) -> hpp::optional<update_manifest>
{
    if(text.size() > MAX_MANIFEST_SIZE)
    {
        return hpp::nullopt;
    }
    const simdjson::padded_string padded(text.data(), text.size());
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if(parser.parse(padded).get(root) != simdjson::SUCCESS)
    {
        return hpp::nullopt;
    }
    const auto builds = get_builds_array(root);
    if(!builds)
    {
        return hpp::nullopt;
    }
    update_manifest manifest;
    for(const simdjson::dom::element entry : *builds)
    {
        if(auto build = read_build(entry))
        {
            manifest.builds.push_back(std::move(*build));
        }
    }
    return manifest;
}

auto find_platform_build(const update_manifest& manifest, hpp::string_view platform) -> hpp::optional<release_build>
{
    const auto found = std::find_if(manifest.builds.begin(),
                                    manifest.builds.end(),
                                    [&](const release_build& build)
                                    {
                                        return build.platform == platform;
                                    });
    if(found == manifest.builds.end())
    {
        return hpp::nullopt;
    }
    return *found;
}

auto is_platform_name(hpp::string_view text) -> bool
{
    return !text.empty() && std::isalpha(static_cast<unsigned char>(text.front())) &&
           std::all_of(text.begin(), text.end(), is_platform_char);
}

auto is_release_tag(hpp::string_view text) -> bool
{
    if(text.empty() || text.front() != 'v')
    {
        return false;
    }
    const auto version = parse_package_version(text.substr(1));
    return version && version->commit_count == 0;
}

auto parse_package_version(hpp::string_view text) -> hpp::optional<version::engine_version>
{
    auto version = version::parse(text);
    // Written back, a version is in its plain spelling: no v, no commit, no marker, no leading zeros.
    if(!version || !version->sha.empty() || version->to_string() != text)
    {
        return hpp::nullopt;
    }
    return version;
}

auto get_package_file_name(const version::engine_version& version, hpp::string_view platform) -> std::string
{
    std::string name(PACKAGE_NAME_PREFIX);
    name += version.to_string();
    name += '-';
    name += platform;
    name += PACKAGE_EXTENSION;
    return name;
}

} // namespace unravel::update_check
