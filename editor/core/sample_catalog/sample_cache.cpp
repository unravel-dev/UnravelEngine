#include "sample_cache.h"

#include <engine/assets/impl/asset_writer.h>

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
constexpr hpp::string_view JPEG_SIGNATURE = "\xFF\xD8\xFF";
constexpr hpp::string_view PNG_SIGNATURE = "\x89PNG\r\n\x1A\n";

auto starts_with(hpp::string_view text, hpp::string_view prefix) -> bool
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

/// The extension of the sample's picture, as its path ends: ".jpg", ".jpeg" or ".png".
auto get_thumbnail_extension(const sample_entry& sample) -> std::string
{
    return fs::path(sample.thumbnail.path).extension().string();
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
    return starts_with(data, JPEG_SIGNATURE) || starts_with(data, PNG_SIGNATURE);
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

auto write_file_atomically(const fs::path& path, hpp::string_view data) -> bool
{
    fs::error_code error;
    fs::create_directories(path.parent_path(), error);
    bool is_written = false;
    asset_writer::atomic_write_file(
        path,
        [&](const fs::path& temp)
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            stream.write(data.data(), std::streamsize(data.size()));
            stream.close();
            is_written = stream.good();
        },
        error);
    return is_written && !error;
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
        if(it->is_regular_file(type_error) && starts_with(name, REPOSITORY_PREFIX) && listed.count(name) == 0)
        {
            fs::error_code remove_error;
            fs::remove(it->path(), remove_error);
        }
    }
}

} // namespace unravel::sample_catalog
