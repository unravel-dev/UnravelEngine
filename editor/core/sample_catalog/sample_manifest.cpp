#include "sample_manifest.h"

#include <ser20/external/simdjson/simdjson.h>

#include <algorithm>
#include <array>

namespace unravel::sample_catalog
{
namespace
{

constexpr std::array<hpp::string_view, 3> THUMBNAIL_EXTENSIONS{".jpg", ".jpeg", ".png"};
constexpr std::size_t COMMIT_ID_SIZE = 40;
constexpr std::size_t DATE_SIZE = 10;
/// Where "YYYY-MM-DD" holds digits.
constexpr std::array<std::size_t, 8> DATE_DIGIT_POSITIONS{0, 1, 2, 3, 5, 6, 8, 9};
constexpr std::size_t GENERATED_TIME_SIZE = 20;
constexpr std::array<std::size_t, 5> PROJECT_GUID_GROUP_SIZES{8, 4, 4, 4, 12};

using text_check = bool (*)(hpp::string_view);

auto is_lower_hex(char c) -> bool
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

auto is_digit(char c) -> bool
{
    return c >= '0' && c <= '9';
}

/// An ASCII letter, digit, dot, dash or underscore: what a name in an address or a file name may hold, whatever
/// the locale.
auto is_name_char(char c) -> bool
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) || c == '.' || c == '-' || c == '_';
}

/// Whether every part of a slash-separated path is a name, none empty and none "." or "..".
auto is_relative_name_path(hpp::string_view text) -> bool
{
    std::size_t start = 0;
    while(start <= text.size())
    {
        const std::size_t end = std::min(text.find('/', start), text.size());
        const hpp::string_view part = text.substr(start, end - start);
        if(part.empty() || part == "." || part == ".." || !std::all_of(part.begin(), part.end(), is_name_char))
        {
            return false;
        }
        start = end + 1;
    }
    return true;
}

/// Text a card shows: not empty, not longer than `max_size` bytes, no control characters and no space at either end.
auto is_display_text(hpp::string_view text, std::size_t max_size) -> bool
{
    const auto is_control = [](char c)
    {
        const auto byte = static_cast<unsigned char>(c);
        return byte < 0x20 || byte == 0x7f;
    };
    return !text.empty() && text.size() <= max_size && text.front() != ' ' && text.back() != ' ' &&
           std::none_of(text.begin(), text.end(), is_control);
}

auto is_title(hpp::string_view text) -> bool
{
    return is_display_text(text, MAX_TITLE_SIZE);
}

auto is_topic(hpp::string_view text) -> bool
{
    return is_display_text(text, MAX_TOPIC_SIZE);
}

auto is_description(hpp::string_view text) -> bool
{
    return is_display_text(text, MAX_DESCRIPTION_SIZE);
}

auto is_tag(hpp::string_view text) -> bool
{
    return is_display_text(text, MAX_TAG_SIZE);
}

auto is_date(hpp::string_view text) -> bool
{
    return parse_date(text).has_value();
}

/// The number in `text`, which holds digits only.
auto to_number(hpp::string_view text) -> int
{
    int value = 0;
    for(const char c : text)
    {
        value = value * 10 + (c - '0');
    }
    return value;
}

auto is_leap_year(int year) -> bool
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

auto get_days_in_month(int year, int month) -> int
{
    constexpr std::array<int, 12> DAYS{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && is_leap_year(year) ? 29 : DAYS[static_cast<std::size_t>(month - 1)];
}

/// The days from 1970-01-01 to a date of the proleptic Gregorian calendar.
auto get_days_from_civil(int year, int month, int day) -> std::int64_t
{
    const std::int64_t shifted_year = month <= 2 ? year - 1 : year;
    const std::int64_t era = (shifted_year >= 0 ? shifted_year : shifted_year - 399) / 400;
    const std::int64_t year_of_era = shifted_year - era * 400;
    const std::int64_t day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const std::int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

/// Whether `text` holds a two-digit number from 0 to `max`.
auto is_two_digits_up_to(hpp::string_view text, int max) -> bool
{
    return text.size() == 2 && is_digit(text[0]) && is_digit(text[1]) && to_number(text) <= max;
}

/**
 * @brief Reads the string field `name` of `object` into `out` when `is_valid` accepts it.
 * @return False when the field is there but is no string or is not valid, or is missing while `is_required`.
 */
auto read_text_field(const simdjson::dom::object& object,
                     const char* name,
                     bool is_required,
                     text_check is_valid,
                     std::string& out) -> bool
{
    auto field = object[name];
    if(field.error() == simdjson::NO_SUCH_FIELD)
    {
        return !is_required;
    }
    hpp::string_view value;
    if(field.get(value) != simdjson::SUCCESS || !is_valid(value))
    {
        return false;
    }
    out.assign(value.data(), value.size());
    return true;
}

/// A string field of an entry: its name, whether an entry must have it, what it may hold and where it goes.
struct entry_text_field
{
    const char* name;
    bool is_required;
    text_check is_valid;
    std::string sample_entry::*member;
};

const std::array<entry_text_field, 8> ENTRY_TEXT_FIELDS{{
    {"repository", true, is_repository_name, &sample_entry::repository},
    {"branch", true, is_branch_name, &sample_entry::branch},
    {"title", true, is_title, &sample_entry::title},
    {"topic", true, is_topic, &sample_entry::topic},
    {"description", true, is_description, &sample_entry::description},
    {"icon", false, is_icon_name, &sample_entry::icon},
    {"added", false, is_date, &sample_entry::added},
    {"project_guid", false, is_project_guid, &sample_entry::project_guid},
}};

auto read_entry_texts(const simdjson::dom::object& object, sample_entry& entry) -> bool
{
    return std::all_of(ENTRY_TEXT_FIELDS.begin(),
                       ENTRY_TEXT_FIELDS.end(),
                       [&](const entry_text_field& field)
                       {
                           return read_text_field(object, field.name, field.is_required, field.is_valid, entry.*field.member);
                       });
}

/// The optional "tags" array: at most MAX_TAGS tags, each one valid.
auto read_entry_tags(const simdjson::dom::object& object, sample_entry& entry) -> bool
{
    auto field = object["tags"];
    if(field.error() == simdjson::NO_SUCH_FIELD)
    {
        return true;
    }
    simdjson::dom::array tags;
    if(field.get(tags) != simdjson::SUCCESS || tags.size() > MAX_TAGS)
    {
        return false;
    }
    for(const simdjson::dom::element tag : tags)
    {
        hpp::string_view value;
        if(tag.get(value) != simdjson::SUCCESS || !is_tag(value))
        {
            return false;
        }
        entry.tags.emplace_back(value.data(), value.size());
    }
    return true;
}

/// The optional "min_engine" version.
auto read_entry_min_engine(const simdjson::dom::object& object, sample_entry& entry) -> bool
{
    std::string text;
    if(!read_text_field(object, "min_engine", false, [](hpp::string_view value) { return parse_min_engine(value).has_value(); }, text))
    {
        return false;
    }
    if(!text.empty())
    {
        entry.min_engine = parse_min_engine(text);
    }
    return true;
}

/// The optional "version" of a sample.json or of an entry: a whole number from 1 to MAX_SAMPLE_VERSION, left as it
/// is when the object names none.
auto read_version_field(const simdjson::dom::object& object, std::uint32_t& version) -> bool
{
    auto field = object["version"];
    if(field.error() == simdjson::NO_SUCH_FIELD)
    {
        return true;
    }
    std::uint64_t value = 0;
    if(field.get(value) != simdjson::SUCCESS || value < FIRST_SAMPLE_VERSION || value > MAX_SAMPLE_VERSION)
    {
        return false;
    }
    version = static_cast<std::uint32_t>(value);
    return true;
}

/// The required "thumbnail" object: the picture's path and commit.
auto read_entry_thumbnail(const simdjson::dom::object& object, sample_entry& entry) -> bool
{
    simdjson::dom::object thumbnail;
    if(object["thumbnail"].get(thumbnail) != simdjson::SUCCESS)
    {
        return false;
    }
    return read_text_field(thumbnail, "path", true, is_thumbnail_path, entry.thumbnail.path) &&
           read_text_field(thumbnail, "commit", true, is_commit_id, entry.thumbnail.commit);
}

/// One entry of the "samples" array, when it holds a valid sample.
auto read_entry(const simdjson::dom::element& element) -> hpp::optional<sample_entry>
{
    simdjson::dom::object object;
    if(element.get(object) != simdjson::SUCCESS)
    {
        return hpp::nullopt;
    }
    sample_entry entry;
    if(!read_entry_texts(object, entry) || !read_entry_tags(object, entry) || !read_entry_min_engine(object, entry) ||
       !read_version_field(object, entry.version) || !read_entry_thumbnail(object, entry))
    {
        return hpp::nullopt;
    }
    return entry;
}

/// Whether the manifest object has the "schema" this editor reads.
auto has_known_schema(const simdjson::dom::object& object) -> bool
{
    std::uint64_t schema = 0;
    return object["schema"].get(schema) == simdjson::SUCCESS && schema == MANIFEST_SCHEMA;
}

/// The "generated" time and the "samples" array of a manifest object of the schema this editor reads.
auto read_manifest_header(const simdjson::dom::element& root, sample_manifest& manifest) -> hpp::optional<simdjson::dom::array>
{
    simdjson::dom::object object;
    if(root.get(object) != simdjson::SUCCESS || !has_known_schema(object))
    {
        return hpp::nullopt;
    }
    if(!read_text_field(object, "generated", true, is_generated_time, manifest.generated))
    {
        return hpp::nullopt;
    }
    simdjson::dom::array samples;
    if(object["samples"].get(samples) != simdjson::SUCCESS)
    {
        return hpp::nullopt;
    }
    return samples;
}

auto has_repository(const sample_manifest& manifest, const std::string& repository) -> bool
{
    return std::any_of(manifest.samples.begin(),
                       manifest.samples.end(),
                       [&](const sample_entry& sample)
                       {
                           return sample.repository == repository;
                       });
}

} // namespace

auto read_sample_manifest(hpp::string_view text) -> hpp::optional<sample_manifest>
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
    sample_manifest manifest;
    const auto samples = read_manifest_header(root, manifest);
    if(!samples)
    {
        return hpp::nullopt;
    }
    for(const simdjson::dom::element element : *samples)
    {
        if(manifest.samples.size() == MAX_SAMPLES)
        {
            break;
        }
        auto entry = read_entry(element);
        if(entry && !has_repository(manifest, entry->repository))
        {
            manifest.samples.push_back(std::move(*entry));
        }
    }
    return manifest;
}

auto read_sample_version(hpp::string_view sample_json) -> hpp::optional<std::uint32_t>
{
    if(sample_json.size() > MAX_SAMPLE_JSON_SIZE)
    {
        return hpp::nullopt;
    }
    const simdjson::padded_string padded(sample_json.data(), sample_json.size());
    simdjson::dom::parser parser;
    simdjson::dom::object object;
    std::uint32_t version = FIRST_SAMPLE_VERSION;
    if(parser.parse(padded).get(object) != simdjson::SUCCESS || !read_version_field(object, version))
    {
        return hpp::nullopt;
    }
    return version;
}

auto is_replacement_manifest(const sample_manifest& candidate, const sample_manifest& current) -> bool
{
    if(candidate.samples.empty())
    {
        return false;
    }
    // Both times are "YYYY-MM-DDTHH:MM:SSZ", which order as text.
    return current.samples.empty() || candidate.generated >= current.generated;
}

auto is_repository_name(hpp::string_view text) -> bool
{
    const hpp::string_view prefix(REPOSITORY_PREFIX);
    return text.size() > prefix.size() && text.size() <= MAX_REPOSITORY_NAME_SIZE && text.substr(0, prefix.size()) == prefix &&
           std::all_of(text.begin(), text.end(), is_name_char);
}

auto is_branch_name(hpp::string_view text) -> bool
{
    return !text.empty() && text.size() <= MAX_BRANCH_NAME_SIZE && text.front() != '-' && is_relative_name_path(text);
}

auto is_commit_id(hpp::string_view text) -> bool
{
    return text.size() == COMMIT_ID_SIZE && std::all_of(text.begin(), text.end(), is_lower_hex);
}

auto is_thumbnail_path(hpp::string_view text) -> bool
{
    const bool has_picture_extension = std::any_of(THUMBNAIL_EXTENSIONS.begin(),
                                                   THUMBNAIL_EXTENSIONS.end(),
                                                   [&](hpp::string_view extension)
                                                   {
                                                       return text.ends_with(extension);
                                                   });
    return !text.empty() && text.size() <= MAX_THUMBNAIL_PATH_SIZE && has_picture_extension && is_relative_name_path(text);
}

auto is_project_guid(hpp::string_view text) -> bool
{
    std::size_t position = 0;
    for(std::size_t group = 0; group < PROJECT_GUID_GROUP_SIZES.size(); ++group)
    {
        if(group > 0)
        {
            if(position >= text.size() || text[position] != '-')
            {
                return false;
            }
            ++position;
        }
        const hpp::string_view digits = text.substr(std::min(position, text.size()), PROJECT_GUID_GROUP_SIZES[group]);
        if(digits.size() != PROJECT_GUID_GROUP_SIZES[group] || !std::all_of(digits.begin(), digits.end(), is_lower_hex))
        {
            return false;
        }
        position += digits.size();
    }
    return position == text.size();
}

auto is_icon_name(hpp::string_view text) -> bool
{
    const auto is_icon_char = [](char c)
    {
        return (c >= 'a' && c <= 'z') || is_digit(c) || c == '-';
    };
    return !text.empty() && text.size() <= MAX_ICON_NAME_SIZE && std::all_of(text.begin(), text.end(), is_icon_char);
}

auto is_generated_time(hpp::string_view text) -> bool
{
    return text.size() == GENERATED_TIME_SIZE && parse_date(text.substr(0, DATE_SIZE)) && text[10] == 'T' &&
           is_two_digits_up_to(text.substr(11, 2), 23) && text[13] == ':' && is_two_digits_up_to(text.substr(14, 2), 59) &&
           text[16] == ':' && is_two_digits_up_to(text.substr(17, 2), 59) && text[19] == 'Z';
}

auto parse_date(hpp::string_view text) -> hpp::optional<std::int64_t>
{
    const auto is_digit_at = [&](std::size_t index)
    {
        return is_digit(text[index]);
    };
    if(text.size() != DATE_SIZE || text[4] != '-' || text[7] != '-' ||
       !std::all_of(DATE_DIGIT_POSITIONS.begin(), DATE_DIGIT_POSITIONS.end(), is_digit_at))
    {
        return hpp::nullopt;
    }
    const int year = to_number(text.substr(0, 4));
    const int month = to_number(text.substr(5, 2));
    const int day = to_number(text.substr(8, 2));
    if(month < 1 || month > 12 || day < 1 || day > get_days_in_month(year, month))
    {
        return hpp::nullopt;
    }
    return get_days_from_civil(year, month, day);
}

auto parse_min_engine(hpp::string_view text) -> hpp::optional<version::engine_version>
{
    auto parsed = version::parse(std::string_view(text.data(), text.size()));
    // Written back, a version is in its plain spelling: no v, no commit, no marker, no leading zeros.
    if(!parsed || !parsed->sha.empty() || parsed->to_string() != text)
    {
        return hpp::nullopt;
    }
    return *parsed;
}

auto get_repository_url(const sample_entry& sample) -> std::string
{
    return UNRAVEL_GITHUB_ACCOUNT_URL "/" + sample.repository;
}

auto get_download_url(const sample_entry& sample) -> std::string
{
    return get_repository_url(sample) + "/archive/refs/heads/" + sample.branch + ".zip";
}

auto get_thumbnail_url(const sample_entry& sample) -> std::string
{
    return UNRAVEL_GITHUB_RAW_FILES_URL + sample.repository + "/" + sample.thumbnail.commit + "/" + sample.thumbnail.path;
}

} // namespace unravel::sample_catalog
