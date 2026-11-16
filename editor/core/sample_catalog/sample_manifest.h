#pragma once

#include <version/version.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// The sample projects the start page offers. Each sample repository describes itself in its sample.json; the
/// sample catalog workflow gathers those into samples.json and attaches it to the latest release. The editor ships
/// a copy of that file and reads the latest release's copy, which can name samples published after the editor.
namespace unravel::sample_catalog
{

/// The largest samples.json that is read. An entry takes under a kilobyte.
constexpr std::size_t MAX_MANIFEST_SIZE = 256 * 1024;
/// The layout of samples.json this editor reads. A file of another layout is no answer.
constexpr std::uint64_t MANIFEST_SCHEMA = 1;
/// The most samples kept from one file; the entries after them are skipped.
constexpr std::size_t MAX_SAMPLES = 64;
constexpr std::size_t MAX_TAGS = 3;
constexpr std::size_t MAX_TITLE_SIZE = 64;
constexpr std::size_t MAX_TOPIC_SIZE = 48;
constexpr std::size_t MAX_DESCRIPTION_SIZE = 600;
constexpr std::size_t MAX_TAG_SIZE = 24;
constexpr std::size_t MAX_REPOSITORY_NAME_SIZE = 100;
constexpr std::size_t MAX_BRANCH_NAME_SIZE = 100;
constexpr std::size_t MAX_THUMBNAIL_PATH_SIZE = 128;
constexpr std::size_t MAX_ICON_NAME_SIZE = 48;

/// The name every sample repository starts with.
inline constexpr const char* REPOSITORY_PREFIX = "UnravelEngine-Sample-";
/// The account that owns the sample repositories, on GitHub.
inline constexpr const char* OWNER_URL = "https://github.com/unravel-dev";
/// Where GitHub serves the files of the account's repositories as they are; repository, commit and path follow.
inline constexpr const char* RAW_FILES_URL = "https://raw.githubusercontent.com/unravel-dev/";

/// The picture of a sample: a file of its repository at a commit.
struct sample_thumbnail
{
    /// Relative to the repository's root: "thumbnail.jpg".
    std::string path;
    /// The last commit that changed the picture, 40 hex digits. A new picture comes with a new commit, so the
    /// cached copy is named after it.
    std::string commit;
};

/// One sample, as samples.json lists it.
struct sample_entry
{
    /// The name of the repository on GitHub, and of the folder a clone of it gets.
    std::string repository;
    /// The branch a ZIP download takes.
    std::string branch;
    std::string title;
    /// One line over the title: the area of the engine the sample is about.
    std::string topic;
    std::string description;
    /// At most MAX_TAGS.
    std::vector<std::string> tags;
    /// The name of a Material Design icon ("sword-cross") for the stand-in picture; empty when none is named.
    std::string icon;
    /// The day the sample was published, "YYYY-MM-DD"; empty when not given.
    std::string added;
    /// The oldest editor the sample opens in, when it names one.
    hpp::optional<version::engine_version> min_engine;
    /// The project_guid of the sample's project.cfg, which every copy of it keeps; empty when not given.
    std::string project_guid;
    sample_thumbnail thumbnail;
};

/// What samples.json holds.
struct sample_manifest
{
    /// When the workflow wrote the file, "YYYY-MM-DDTHH:MM:SSZ". Of two files, the later one is the newer.
    std::string generated;
    /// In the order the start page shows them.
    std::vector<sample_entry> samples;
};

/**
 * @brief Reads samples.json.
 *
 * Nothing when the text is larger than MAX_MANIFEST_SIZE, is not JSON, or is not an object with "schema" equal to
 * MANIFEST_SCHEMA, a "generated" time and a "samples" array. An entry is kept only when every field it has is
 * valid and it has a repository, a branch, a title, a topic, a description and a thumbnail; other entries are
 * skipped, and so is a repository listed twice. Unknown fields are ignored. No address is read from the file:
 * every address is made from the names it holds.
 */
auto read_sample_manifest(hpp::string_view text) -> hpp::optional<sample_manifest>;

/// Whether `candidate` was written after `current`.
auto is_newer_manifest(const sample_manifest& candidate, const sample_manifest& current) -> bool;

/// Whether `text` names a sample repository: REPOSITORY_PREFIX, then letters, digits, dots, dashes and underscores.
auto is_repository_name(hpp::string_view text) -> bool;
/// Whether `text` is a branch name an address can carry: letters, digits, dots, dashes, underscores and slashes,
/// with no empty part, no ".." and no leading dash.
auto is_branch_name(hpp::string_view text) -> bool;
/// Whether `text` is a full commit id: 40 lower-case hex digits.
auto is_commit_id(hpp::string_view text) -> bool;
/// Whether `text` is the path of a picture inside a repository: relative, with no empty part and no "..", of
/// letters, digits, dots, dashes, underscores and slashes, ending in .jpg, .jpeg or .png.
auto is_thumbnail_path(hpp::string_view text) -> bool;
/// Whether `text` is a project_guid as project.cfg writes it: 8-4-4-4-12 lower-case hex digits.
auto is_project_guid(hpp::string_view text) -> bool;
/// Whether `text` is an icon name: lower-case letters, digits and dashes.
auto is_icon_name(hpp::string_view text) -> bool;
/// Whether `text` is a time as "generated" holds it: "YYYY-MM-DDTHH:MM:SSZ".
auto is_generated_time(hpp::string_view text) -> bool;
/// The days from 1970-01-01 to the date in `text`, "YYYY-MM-DD"; nothing for any other text or a date that does
/// not exist.
auto parse_date(hpp::string_view text) -> hpp::optional<std::int64_t>;
/// The version a sample names as the oldest editor it opens in ("1.2.0", "1.2.0-450"); nothing for any other
/// spelling, such as one with a commit or a leading v.
auto parse_min_engine(hpp::string_view text) -> hpp::optional<version::engine_version>;

/// The sample's page on GitHub.
auto get_repository_url(const sample_entry& sample) -> std::string;
/// A ZIP archive of the sample's branch.
auto get_download_url(const sample_entry& sample) -> std::string;
/// The sample's picture at its commit, served as it is.
auto get_thumbnail_url(const sample_entry& sample) -> std::string;

} // namespace unravel::sample_catalog
