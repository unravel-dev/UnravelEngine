#include "third_party_notices.h"

#include <audiopp/backend.h>
#include <version/version.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace unravel
{

namespace
{
/// Where the notices name the engine version, which the text file cannot know.
constexpr const char* ENGINE_VERSION_PLACEHOLDER = "@ENGINE_VERSION@";
/// The line above and below the title of each library's license text.
constexpr const char* SECTION_RULE = "==============================================================================\n";
/// The headings of the part on the LGPL and of the list of the software after it.
constexpr const char* LGPL_HEADING = "LIBRARIES UNDER THE GNU LESSER GENERAL PUBLIC LICENSE\n";
constexpr const char* SOFTWARE_HEADING = "THE SOFTWARE IN THIS GAME\n";
/// How the lists indent the line of each library.
constexpr const char* LIST_INDENT = "  ";
/// What the lines of a library and the title of its license text begin with.
constexpr const char* OPENAL_SOFT_NAME = "OpenAL Soft";
constexpr const char* OBOE_NAME = "Oboe";
constexpr const char* LIBDWARF_NAME = "libdwarf-lite";
constexpr const char* MINIAUDIO_NAME = "miniaudio";
constexpr const char* CPPTRACE_NAME = "cpptrace";

constexpr unsigned char NOTICES_TEXT[] = {
#include <engine/third_party_notices_text.inc>
};

// LINKS_LIBDWARF and LINKS_CPPTRACE: whether this build links cpptrace and whether it reads symbols with libdwarf,
// which only CMake knows.
#include <engine/third_party_notices_build.inc>

/// The notices in the parts a build keeps or cuts whole.
struct notices_parts
{
    /// The title and what the notices are, up to the part on the LGPL.
    std::string preface;
    /// From the heading of the part on the LGPL up to the list of the software.
    std::string lgpl_part;
    /// From the heading of the list of the software up to the first license text.
    std::string software_list;
    /// Each library's license text, from the rule above its title.
    std::vector<std::string> sections;
};

auto is_notice_text_at(const std::string& text, std::size_t position, const std::string& part) -> bool
{
    return text.compare(position, part.size(), part) == 0;
}

/// Where the line that starts at `line` ends, after its line feed.
auto find_notice_line_end(const std::string& text, std::size_t line) -> std::size_t
{
    const auto feed = text.find('\n', line);
    return feed == std::string::npos ? text.size() : feed + 1;
}

/// Whether the text at `position` begins with the name of a library the build leaves out.
auto names_left_out_library(const std::string& text, std::size_t position, const std::vector<std::string>& left_out)
    -> bool
{
    return std::any_of(left_out.begin(),
                       left_out.end(),
                       [&](const std::string& name)
                       {
                           return is_notice_text_at(text, position, name);
                       });
}

/// The notices with this binary's engine version filled in.
auto get_notices_with_version() -> std::string
{
    std::string text(reinterpret_cast<const char*>(NOTICES_TEXT), sizeof(NOTICES_TEXT));
    const auto placeholder = text.find(ENGINE_VERSION_PLACEHOLDER);
    if(placeholder != std::string::npos)
    {
        text.replace(placeholder, std::strlen(ENGINE_VERSION_PLACEHOLDER), version::get_full());
    }
    return text;
}

/// Where each line that is a section rule starts.
auto find_rule_lines(const std::string& text) -> std::vector<std::size_t>
{
    std::vector<std::size_t> rules;
    for(std::size_t line = 0; line < text.size(); line = find_notice_line_end(text, line))
    {
        if(is_notice_text_at(text, line, SECTION_RULE))
        {
            rules.push_back(line);
        }
    }
    return rules;
}

auto split_notices(const std::string& text) -> notices_parts
{
    const auto rules = find_rule_lines(text);
    const auto first_section = rules.empty() ? text.size() : rules.front();
    // A missing heading leaves its part empty instead of losing the text after it.
    const auto software = std::min(text.find(SOFTWARE_HEADING), first_section);
    const auto lgpl = std::min(text.find(LGPL_HEADING), software);
    notices_parts parts;
    parts.preface = text.substr(0, lgpl);
    parts.lgpl_part = text.substr(lgpl, software - lgpl);
    parts.software_list = text.substr(software, first_section - software);
    // The rules come in pairs around each title, so every other one opens a section.
    for(std::size_t i = 0; i < rules.size(); i += 2)
    {
        const auto end = i + 2 < rules.size() ? rules[i + 2] : text.size();
        parts.sections.push_back(text.substr(rules[i], end - rules[i]));
    }
    return parts;
}

/// The part without the lines of its lists that name a library the build leaves out.
auto remove_left_out_lines(const std::string& part, const std::vector<std::string>& left_out) -> std::string
{
    std::string kept;
    for(std::size_t line = 0; line < part.size(); line = find_notice_line_end(part, line))
    {
        const bool is_left_out = is_notice_text_at(part, line, LIST_INDENT) &&
                                 names_left_out_library(part, line + std::strlen(LIST_INDENT), left_out);
        if(!is_left_out)
        {
            kept.append(part, line, find_notice_line_end(part, line) - line);
        }
    }
    return kept;
}

/// The license texts of the libraries the build links, in their order.
auto join_kept_sections(const std::vector<std::string>& sections, const std::vector<std::string>& left_out)
    -> std::string
{
    std::string kept;
    for(const auto& section : sections)
    {
        if(!names_left_out_library(section, std::strlen(SECTION_RULE), left_out))
        {
            kept += section;
        }
    }
    return kept;
}

/// The names of the optional libraries a build does not link.
auto get_left_out_names(const linked_libraries& libraries) -> std::vector<std::string>
{
    std::vector<std::string> names;
    if(!libraries.has_openal_soft)
    {
        names.insert(names.end(), {OPENAL_SOFT_NAME, OBOE_NAME});
    }
    if(!libraries.has_libdwarf)
    {
        names.emplace_back(LIBDWARF_NAME);
    }
    if(!libraries.has_miniaudio)
    {
        names.emplace_back(MINIAUDIO_NAME);
    }
    if(!libraries.has_cpptrace)
    {
        names.emplace_back(CPPTRACE_NAME);
    }
    return names;
}
} // namespace

auto get_linked_libraries() -> linked_libraries
{
    linked_libraries libraries;
    libraries.has_openal_soft = audio::is_backend_available(audio::backend_type::openal);
    libraries.has_libdwarf = LINKS_LIBDWARF;
    libraries.has_cpptrace = LINKS_CPPTRACE;
    libraries.has_miniaudio = audio::is_backend_available(audio::backend_type::miniaudio);
    return libraries;
}

auto get_third_party_notices(const linked_libraries& libraries) -> std::string
{
    const auto left_out = get_left_out_names(libraries);
    const auto parts = split_notices(get_notices_with_version());
    auto text = parts.preface;
    if(libraries.has_openal_soft || libraries.has_libdwarf)
    {
        text += remove_left_out_lines(parts.lgpl_part, left_out);
    }
    text += remove_left_out_lines(parts.software_list, left_out);
    text += join_kept_sections(parts.sections, left_out);
    return text;
}

auto get_third_party_notices() -> std::string
{
    return get_third_party_notices(get_linked_libraries());
}

} // namespace unravel
