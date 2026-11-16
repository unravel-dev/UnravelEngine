#include "sample_cache.h"

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace unravel::sample_catalog
{
namespace
{

constexpr const char* CACHE_DIR_NAME = "samples";
constexpr const char* CACHED_MANIFEST_FILE_NAME = "samples.json";
constexpr const char* FETCH_STATE_FILE_NAME = "fetch_state.cfg";
constexpr const char* THUMBNAILS_DIR_NAME = "thumbnails";
constexpr const char* SAMPLE_JSON_FILE_NAME = "sample.json";
constexpr hpp::string_view JPEG_SIGNATURE = "\xFF\xD8\xFF";
constexpr hpp::string_view PNG_SIGNATURE = "\x89PNG\r\n\x1A\n";

/// The extension of the sample's picture, as its path ends: ".jpg", ".jpeg" or ".png".
auto get_thumbnail_extension(const sample_entry& sample) -> std::string
{
    return fs::path(sample.thumbnail.path).extension().string();
}

auto to_lower_ascii(char c) -> char
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

auto is_same_ignoring_case(hpp::string_view lhs, hpp::string_view rhs) -> bool
{
    return std::equal(lhs.begin(),
                      lhs.end(),
                      rhs.begin(),
                      rhs.end(),
                      [](char lhs_character, char rhs_character)
                      {
                          return to_lower_ascii(lhs_character) == to_lower_ascii(rhs_character);
                      });
}

/// Whether a folder name is the repository's name followed by a dash and more, case aside.
auto is_suffixed_repository_name(hpp::string_view folder_name, hpp::string_view repository) -> bool
{
    return folder_name.size() > repository.size() + 1 && folder_name[repository.size()] == '-' &&
           is_same_ignoring_case(folder_name.substr(0, repository.size()), repository);
}

} // namespace

auto get_cache_dir() -> fs::path
{
    return fs::persistent_path() / "unravel" / CACHE_DIR_NAME;
}

auto get_cached_manifest_path() -> fs::path
{
    return get_cache_dir() / CACHED_MANIFEST_FILE_NAME;
}

auto get_fetch_state_path() -> fs::path
{
    return get_cache_dir() / FETCH_STATE_FILE_NAME;
}

auto get_thumbnails_dir() -> fs::path
{
    return get_cache_dir() / THUMBNAILS_DIR_NAME;
}

auto get_thumbnail_file_name(const sample_entry& sample) -> std::string
{
    return sample.repository + "-" + sample.thumbnail.commit.substr(0, THUMBNAIL_COMMIT_DIGITS) +
           get_thumbnail_extension(sample);
}

auto get_shipped_thumbnail_file_name(const sample_entry& sample) -> std::string
{
    return sample.repository + get_thumbnail_extension(sample);
}

auto is_picture_data(hpp::string_view data) -> bool
{
    return data.starts_with(JPEG_SIGNATURE) || data.starts_with(PNG_SIGNATURE);
}

auto load_manifest_file(const fs::path& path) -> hpp::optional<sample_manifest>
{
    fs::error_code error;
    const auto size = fs::file_size(path, error);
    if(error || size > MAX_MANIFEST_SIZE)
    {
        return hpp::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if(!stream.good())
    {
        return hpp::nullopt;
    }
    return read_sample_manifest(fs::read_stream_str(stream));
}

auto get_copy_sample_json_path(const fs::path& copy_dir) -> fs::path
{
    return copy_dir / SAMPLE_JSON_FILE_NAME;
}

auto load_copy_version(const fs::path& copy_dir) -> hpp::optional<std::uint32_t>
{
    const fs::path path = get_copy_sample_json_path(copy_dir);
    fs::error_code error;
    const auto size = fs::file_size(path, error);
    if(error || size > MAX_SAMPLE_JSON_SIZE)
    {
        return hpp::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if(!stream.good())
    {
        return hpp::nullopt;
    }
    return read_sample_version(fs::read_stream_str(stream));
}

auto choose_manifest(hpp::optional<sample_manifest> shipped, hpp::optional<sample_manifest> fetched) -> sample_manifest
{
    sample_manifest chosen = shipped ? std::move(*shipped) : sample_manifest{};
    if(fetched && is_replacement_manifest(*fetched, chosen))
    {
        chosen = std::move(*fetched);
    }
    return chosen;
}

void remove_unlisted_thumbnails(const fs::path& dir, const sample_manifest& manifest)
{
    std::unordered_set<std::string> listed;
    for(const sample_entry& sample : manifest.samples)
    {
        listed.insert(get_thumbnail_file_name(sample));
    }
    fs::error_code error;
    for(fs::directory_iterator it(dir, error), end; !error && it != end; it.increment(error))
    {
        const std::string name = it->path().filename().string();
        fs::error_code type_error;
        if(it->is_regular_file(type_error) && hpp::string_view(name).starts_with(REPOSITORY_PREFIX) &&
           listed.count(name) == 0)
        {
            fs::error_code remove_error;
            fs::remove(it->path(), remove_error);
        }
    }
}

auto find_sample_copy(hpp::string_view repository,
                      hpp::string_view project_guid,
                      const std::vector<fs::path>& projects,
                      const project_lookup& lookup) -> fs::path
{
    for(const fs::path& project : projects)
    {
        if(is_same_ignoring_case(project.filename().string(), repository) && lookup.is_folder(project))
        {
            return project;
        }
    }
    if(project_guid.empty())
    {
        return {};
    }
    for(const fs::path& project : projects)
    {
        if(is_suffixed_repository_name(project.filename().string(), repository) &&
           lookup.read_project_guid(project) == project_guid && lookup.is_folder(project))
        {
            return project;
        }
    }
    return {};
}

} // namespace unravel::sample_catalog
