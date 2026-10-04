#include "filesystem.h"
#include <algorithm>
#include <fstream>
namespace fs
{

namespace detail
{

bool is_case_insensitive()
{
    static bool is_insensitive = []()
    {
        auto timestamp = fs::file_time_type::clock::now();
        auto temp_path = fs::temp_directory_path();

        auto salt = std::to_string(int64_t(timestamp.time_since_epoch().count())) + std::to_string(std::rand());
        std::string temp_name_lower = "_case_sensitivity_test_" + salt + ".txt";
        std::string temp_name_upper = "_CASE_SENSITIVITY_TEST_" + salt + ".txt";

        auto file_lower = temp_path / temp_name_lower;
        auto file_upper = temp_path / temp_name_upper;
        {
            std::ofstream os;
            os.open(file_lower);
        }

        fs::error_code ec;
        bool result = fs::equivalent(file_upper, file_lower, ec);
        fs::remove(file_lower, ec);

        return result;
    }();

    return is_insensitive;
}

namespace
{
bool is_parent_path(const path& parent, const path& child)
{
    return child.parent_path() == parent;
}

bool is_indirect_parent_path(const path& parent, const path& child)
{
    path rel = child.lexically_relative(parent);
    return !rel.empty() && rel.begin()->string() != "." && rel.begin()->string() != "..";
}

static std::string replace_seq(const std::string& str, const std::string& old_sequence, const std::string& new_sequence)
{
    std::string s = str;
    std::string::size_type location = 0;
    std::string::size_type old_length = old_sequence.length();
    std::string::size_type new_length = new_sequence.length();

    // Search for all replace std::string occurances.
    if(!s.empty())
    {
        while(std::string::npos != (location = s.find(old_sequence, location)))
        {
            s.replace(location, old_length, new_sequence);
            location += new_length;

            // Break out if we're done
            if(location >= s.length())
            {
                break;
            }

        } // Next

    } // End if not empty

    return s;
}

std::string to_lower(const std::string& str)
{
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(), tolower);
    return s;
}

/// ASCII-only case fold: path strings are compared byte by byte, so other bytes stay as they are.
auto fold_ascii_case(char c) -> char
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Whether root names the folder path_string is, or one of its ancestors. Letter case is ignored on a
/// case-insensitive filesystem, where a root registered from an executable path launched in another case
/// still names the folder on disk.
auto is_root_of(const std::string& root, const std::string& path_string) -> bool
{
    if(root.empty() || path_string.size() < root.size())
    {
        return false;
    }
    const bool ignore_case = is_case_insensitive();
    const auto chars_match = [ignore_case](char lhs, char rhs) -> bool
    {
        return ignore_case ? fold_ascii_case(lhs) == fold_ascii_case(rhs) : lhs == rhs;
    };
    if(!std::equal(root.begin(), root.end(), path_string.begin(), chars_match))
    {
        return false;
    }
    if(path_string.size() == root.size())
    {
        return true;
    }
    const auto is_separator = [](char c) -> bool
    {
        return c == '\\' || c == '/';
    };
    return is_separator(root.back()) || is_separator(path_string[root.size()]);
}

/**
 * @brief The spelling a protocol root is stored in.
 *
 * Absolute, lexically normal, as it is on disk where the folder exists (letter case, short names, links
 * resolved) and without a trailing separator - the form convert_to_protocol compares its canonical input to.
 */
auto normalize_protocol_root(const path& dir) -> path
{
    fs::error_code err;
    path root = fs::weakly_canonical(dir, err);
    if(err || root.empty())
    {
        root = dir.lexically_normal();
    }
    if(!root.has_filename() && root.has_relative_path())
    {
        root = root.parent_path();
    }
    return root.make_preferred();
}

template<typename Container = std::string, typename CharT = char, typename Traits = std::char_traits<char>>
auto read_stream_into_container(std::basic_istream<CharT, Traits>& in, Container& container) -> bool
{
    static_assert(
        std::is_same<Container, std::basic_string<CharT, Traits, typename Container::allocator_type>>::value ||
            std::is_same<Container, std::vector<CharT, typename Container::allocator_type>>::value ||
            std::is_same<Container,
                         std::vector<std::make_unsigned_t<CharT>, typename Container::allocator_type>>::value ||
            std::is_same<Container, std::vector<std::make_signed_t<CharT>, typename Container::allocator_type>>::value,
        "only strings and vectors of ((un)signed) CharT allowed");

    auto const start_pos = in.tellg();
    if(std::streamsize(-1) == start_pos || !in.good())
    {
        return false;
    }

    if(!in.seekg(0, std::ios_base::end) || !in.good())
    {
        return false;
    }

    auto const end_pos = in.tellg();

    if(std::streamsize(-1) == end_pos || !in.good())
    {
        return false;
    }

    auto const char_count = end_pos - start_pos;

    if(!in.seekg(start_pos) || !in.good())
    {
        return false;
    }

    container.resize(static_cast<std::size_t>(char_count));

    if(!container.empty())
    {
        in.read(reinterpret_cast<CharT*>(&container[0]), char_count);
        container.resize(in.gcount());
    }

    return in.good() || in.eof();
}
}

} // namespace detail


byte_array_t read_stream(std::istream& stream)
{
    byte_array_t result{};
    detail::read_stream_into_container<byte_array_t>(stream, result);
    return result;
}

std::string read_stream_str(std::istream& stream)
{
    std::string result{};
    detail::read_stream_into_container<std::string>(stream, result);
    return result;
}

stream_buffer<byte_array_t> read_stream_buffer(std::istream& stream)
{
    stream_buffer<byte_array_t> result{};
    result.data = read_stream(stream);
    return result;
}

stream_buffer<std::string> read_stream_buffer_str(std::istream& stream)
{
    stream_buffer<std::string> result{};
    result.data = read_stream_str(stream);
    return result;
}

bool add_path_protocol(const std::string& protocol, const path& dir)
{
    // Protocol matching is case insensitive, convert to lower case
    auto protocol_lower = detail::to_lower(protocol);

    auto& protocols = get_path_protocols();
    // Add to the list
    protocols[protocol_lower] = detail::normalize_protocol_root(dir).string();

    // Success!
    return true;
}

protocols_t& get_path_protocols()
{
    static protocols_t protocols;
    return protocols;
}

path extract_protocol(const path& _path)
{
    static const std::string separator = ":/";
    const auto string_path = _path.generic_string();
    auto pos = string_path.find(separator, 0);
    if(pos == std::string::npos)
    {
        return {};
    }

    const auto root = string_path.substr(0, pos);

    return root;
}

path resolve_protocol(const path& _path)
{
    static const std::string separator = ":/";
    const auto string_path = _path.generic_string();
    auto pos = string_path.find(separator, 0);
    if(pos == std::string::npos)
    {
        return _path;
    }

    const auto root = string_path.substr(0, pos);

    fs::path relative_path = string_path.substr(pos + separator.size());
    // Matching path protocol in our list?
    const auto& protocols = get_path_protocols();

    auto it = protocols.find(root);

    if(it == std::end(protocols))
    {
        return _path;
    }

    auto result = path(it->second);
    if(!relative_path.empty())
    {
        result = result / relative_path.make_preferred();
    }
    return result;
}

bool has_known_protocol(const path& _path)
{
    static const std::string separator = ":/";

    const auto string_path = _path.generic_string();
    auto pos = string_path.find(separator, 0);
    if(pos == std::string::npos)
    {
        return false;
    }

    const auto root = string_path.substr(0, pos);

    const auto& protocols = get_path_protocols();

    // Matching path protocol in our list?
    return (protocols.find(root) != std::end(protocols));
}

path convert_to_protocol(const path& _path)
{
    fs::error_code ec;
    auto canonical_path = fs::weakly_canonical(_path, ec);
    if(ec || canonical_path.empty())
    {
        canonical_path = _path.lexically_normal();
    }
    const auto string_path = fs::path(canonical_path).make_preferred().string();

    const auto& protocols = get_path_protocols();

    const protocols_t::value_type* best_protocol{};
    for(const auto& protocol_pair : protocols)
    {
        const auto& resolved_protocol = protocol_pair.second;
        if(!detail::is_root_of(resolved_protocol, string_path))
        {
            continue;
        }
        if(!best_protocol || best_protocol->second.size() < resolved_protocol.size())
        {
            best_protocol = &protocol_pair;
        }
    }
    if(!best_protocol)
    {
        return _path;
    }
    // The matched root is replaced by length: it may differ from the path in letter case.
    auto relative = path(string_path.substr(best_protocol->second.size())).generic_string();
    if(!relative.empty() && relative.front() != '/')
    {
        relative.insert(relative.begin(), '/');
    }
    return path(best_protocol->first + ":" + relative);
}

path replace(const path& _path, const path& _sequence, const path& _new_sequence)
{
    return path(detail::replace_seq(_path.string(), _sequence.string(), _new_sequence.string()));
}

std::vector<path> split_until(const path& _path, const path& _predicate)
{
    std::vector<path> result;

    auto f = _path;

    while(f.has_parent_path() && f.has_filename() && f != _predicate)
    {
        result.push_back(f);
        f = f.parent_path();
    }

    result.push_back(_predicate);
    std::reverse(std::begin(result), std::end(result));

    return result;
}

path reduce_trailing_extensions(const path& _path)
{
    fs::path reduced = _path;
    for(auto temp = reduced; temp.has_extension(); temp = reduced.stem())
    {
        reduced = temp;
    }

    fs::path result = _path;
    result.remove_filename();
    result /= reduced;
    return result;
}

bool is_any_parent_path(const path& parent, const path& child)
{
    return detail::is_parent_path(parent, child) || detail::is_indirect_parent_path(parent, child);
}
} // namespace fs
