#include "windows_resources.h"

#include <logging/logging.h>
#include <string_utils/utf8.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace unravel::deployment
{
namespace
{
/// An icon image of 256 pixels is written as 0 in the one byte its group entry has for the size.
constexpr std::uint32_t ICON_SIZE_WRITTEN_AS_ZERO = 256;
/// The resource type of an icon group's header (1: icons, 2: cursors).
constexpr std::uint16_t ICON_GROUP_TYPE = 1;
constexpr std::uint16_t ICON_PLANES = 1;
constexpr std::uint16_t ICON_BIT_COUNT = 32;

constexpr std::uint32_t FIXED_FILE_INFO_SIGNATURE = 0xFEEF04BD;
constexpr std::uint32_t FIXED_FILE_INFO_STRUCTURE_VERSION = 0x00010000;
/// VS_FF_DEBUG through VS_FF_INFOINFERRED: the flags this information states, all of them clear.
constexpr std::uint32_t FIXED_FILE_INFO_FLAGS_MASK = 0x3F;
/// VOS_NT_WINDOWS32.
constexpr std::uint32_t FILE_OS_WINDOWS_NT = 0x00040004;
/// VFT_APP.
constexpr std::uint32_t FILE_TYPE_APPLICATION = 0x1;
/// The bytes of VS_FIXEDFILEINFO: thirteen 32-bit fields.
constexpr std::uint16_t FIXED_FILE_INFO_SIZE = 13 * sizeof(std::uint32_t);
/// The wType of a version block whose value is text, and of one whose value is binary.
constexpr std::uint16_t TEXT_VALUE = 1;
constexpr std::uint16_t BINARY_VALUE = 0;
/// US English (0x0409) and the Unicode code page (1200, 0x04B0): the language the strings are listed under.
constexpr std::uint16_t US_ENGLISH = 0x0409;
constexpr std::uint16_t UNICODE_CODE_PAGE = 0x04B0;
constexpr const char16_t* STRING_TABLE_KEY = u"040904B0";
/// The value of a Translation entry: one language and code page pair.
constexpr std::uint16_t TRANSLATION_SIZE = 2 * sizeof(std::uint16_t);
/// Version blocks start at multiples of 32 bits.
constexpr std::size_t BLOCK_ALIGNMENT = 4;

constexpr unsigned BYTE_SHIFT = 8;
constexpr std::uint32_t BYTE_MASK = 0xFF;
constexpr unsigned HALF_SHIFT = 16;
constexpr std::uint32_t HALF_MASK = 0xFFFF;
constexpr std::uint32_t DECIMAL_BASE = 10;

/// UTF-8 text as UTF-16, the text of version strings; malformed bytes become replacement characters.
auto to_utf16(const std::string& text) -> std::u16string
{
    const std::string valid = utf8::replace_invalid(text);
    std::u16string result;
    utf8::utf8to16(valid.begin(), valid.end(), std::back_inserter(result));
    return result;
}

/// Little-endian resource bytes, with the nested, length-prefixed blocks of a VS_VERSIONINFO.
class resource_writer
{
public:
    void write_u8(std::uint8_t value)
    {
        bytes_.push_back(value);
    }

    void write_u16(std::uint16_t value)
    {
        write_u8(static_cast<std::uint8_t>(value & BYTE_MASK));
        write_u8(static_cast<std::uint8_t>(value >> BYTE_SHIFT));
    }

    void write_u32(std::uint32_t value)
    {
        write_u16(static_cast<std::uint16_t>(value & HALF_MASK));
        write_u16(static_cast<std::uint16_t>(value >> HALF_SHIFT));
    }

    /// The text and its terminating null.
    void write_text(const std::u16string& text)
    {
        for(const char16_t unit : text)
        {
            write_u16(static_cast<std::uint16_t>(unit));
        }
        write_u16(0);
    }

    /**
     * @brief Starts a version block: its length (set by end_block), the length and type of its value and its
     * key, padded so that the value starts at a 32-bit boundary.
     * @return Where the block starts, for end_block.
     */
    auto begin_block(const std::u16string& key, std::uint16_t value_length, std::uint16_t type) -> std::size_t
    {
        align();
        const std::size_t start = bytes_.size();
        write_u16(0);
        write_u16(value_length);
        write_u16(type);
        write_text(key);
        align();
        return start;
    }

    /// Ends the block begun at `start`: its length covers its value and its children.
    void end_block(std::size_t start)
    {
        const auto length = static_cast<std::uint16_t>(bytes_.size() - start);
        bytes_[start] = static_cast<std::uint8_t>(length & BYTE_MASK);
        bytes_[start + 1] = static_cast<std::uint8_t>(length >> BYTE_SHIFT);
    }

    auto take() -> std::vector<std::uint8_t>
    {
        return std::move(bytes_);
    }

private:
    void align()
    {
        while(bytes_.size() % BLOCK_ALIGNMENT != 0)
        {
            write_u8(0);
        }
    }

    std::vector<std::uint8_t> bytes_;
};

auto join_version_halves(std::uint16_t high, std::uint16_t low) -> std::uint32_t
{
    return (std::uint32_t(high) << HALF_SHIFT) | low;
}

void write_fixed_file_info(resource_writer& writer, const std::array<std::uint16_t, 4>& version)
{
    const std::uint32_t most_significant = join_version_halves(version[0], version[1]);
    const std::uint32_t least_significant = join_version_halves(version[2], version[3]);
    writer.write_u32(FIXED_FILE_INFO_SIGNATURE);
    writer.write_u32(FIXED_FILE_INFO_STRUCTURE_VERSION);
    // The file version, then the product version: one number for both.
    writer.write_u32(most_significant);
    writer.write_u32(least_significant);
    writer.write_u32(most_significant);
    writer.write_u32(least_significant);
    writer.write_u32(FIXED_FILE_INFO_FLAGS_MASK);
    writer.write_u32(0);
    writer.write_u32(FILE_OS_WINDOWS_NT);
    writer.write_u32(FILE_TYPE_APPLICATION);
    // No subtype and no date.
    writer.write_u32(0);
    writer.write_u32(0);
    writer.write_u32(0);
}

/// A String block; an empty value is left out.
void write_version_string(resource_writer& writer, const std::u16string& key, const std::string& value)
{
    if(value.empty())
    {
        return;
    }
    const auto text = to_utf16(value);
    const auto start = writer.begin_block(key, static_cast<std::uint16_t>(text.size() + 1), TEXT_VALUE);
    writer.write_text(text);
    writer.end_block(start);
}

void write_string_file_info(resource_writer& writer, const windows_version_info& info)
{
    const auto file_info = writer.begin_block(u"StringFileInfo", 0, TEXT_VALUE);
    const auto table = writer.begin_block(STRING_TABLE_KEY, 0, TEXT_VALUE);
    write_version_string(writer, u"CompanyName", info.company_name);
    write_version_string(writer, u"FileDescription", info.product_name);
    write_version_string(writer, u"FileVersion", info.version);
    write_version_string(writer, u"InternalName", info.product_name);
    write_version_string(writer, u"OriginalFilename", info.original_filename);
    write_version_string(writer, u"ProductName", info.product_name);
    write_version_string(writer, u"ProductVersion", info.version);
    writer.end_block(table);
    writer.end_block(file_info);
}

void write_var_file_info(resource_writer& writer)
{
    const auto file_info = writer.begin_block(u"VarFileInfo", 0, TEXT_VALUE);
    const auto translation = writer.begin_block(u"Translation", TRANSLATION_SIZE, BINARY_VALUE);
    writer.write_u16(US_ENGLISH);
    writer.write_u16(UNICODE_CODE_PAGE);
    writer.end_block(translation);
    writer.end_block(file_info);
}

#ifdef _WIN32
/// The resource ids this file writes: the version is resource 1, as rc names VS_VERSION_INFO, and the icon
/// images are numbered from 1, after every earlier one is removed.
constexpr WORD VERSION_RESOURCE_ID = 1;
constexpr WORD FIRST_ICON_ID = 1;
/// The name of the icon group when the file had none.
constexpr WORD DEFAULT_ICON_GROUP_ID = 1;

/// A resource's name: a number, or text.
struct resource_name
{
    std::wstring text;
    WORD id{};

    auto get() const -> LPCWSTR
    {
        return text.empty() ? MAKEINTRESOURCEW(id) : text.c_str();
    }
};

struct resource_entry
{
    resource_name name;
    WORD language{};
};

/// The icon, icon group and version resources an executable has.
struct existing_resources
{
    std::vector<resource_entry> icons;
    std::vector<resource_entry> icon_groups;
    std::vector<resource_entry> versions;
};

/// RT_ICON, RT_GROUP_ICON and RT_VERSION, as the wide functions take them whether or not UNICODE is defined.
constexpr WORD ICON_RESOURCE_TYPE = 3;
constexpr WORD ICON_GROUP_RESOURCE_TYPE = 14;
constexpr WORD VERSION_RESOURCE_TYPE = 16;

auto get_icon_type() -> LPCWSTR
{
    return MAKEINTRESOURCEW(ICON_RESOURCE_TYPE);
}

auto get_icon_group_type() -> LPCWSTR
{
    return MAKEINTRESOURCEW(ICON_GROUP_RESOURCE_TYPE);
}

auto get_version_type() -> LPCWSTR
{
    return MAKEINTRESOURCEW(VERSION_RESOURCE_TYPE);
}

auto copy_name(LPCWSTR name) -> resource_name
{
    if(IS_INTRESOURCE(name))
    {
        return {{}, static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name))};
    }
    return {name, 0};
}

BOOL CALLBACK collect_language(HMODULE /*module*/, LPCWSTR /*type*/, LPCWSTR name, WORD language, LONG_PTR entries)
{
    reinterpret_cast<std::vector<resource_entry>*>(entries)->push_back({copy_name(name), language});
    return TRUE;
}

BOOL CALLBACK collect_name(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR entries)
{
    EnumResourceLanguagesExW(module, type, name, collect_language, entries, RESOURCE_ENUM_LN, 0);
    return TRUE;
}

/// The resources of `type` in `module`'s own file, not in a language file (.mui) beside it, in the order Windows
/// lists them.
auto list_resources(HMODULE module, LPCWSTR type) -> std::vector<resource_entry>
{
    std::vector<resource_entry> entries;
    EnumResourceNamesExW(module, type, collect_name, reinterpret_cast<LONG_PTR>(&entries), RESOURCE_ENUM_LN, 0);
    return entries;
}

auto read_existing_resources(const fs::path& file, existing_resources& found) -> bool
{
    HMODULE module = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(module == nullptr)
    {
        return false;
    }
    found.icons = list_resources(module, get_icon_type());
    found.icon_groups = list_resources(module, get_icon_group_type());
    found.versions = list_resources(module, get_version_type());
    FreeLibrary(module);
    return true;
}

auto update_resource(HANDLE update, LPCWSTR type, LPCWSTR name, WORD language, const std::vector<std::uint8_t>& data) -> bool
{
    auto* bytes = const_cast<std::uint8_t*>(data.data());
    return UpdateResourceW(update, type, name, language, bytes, static_cast<DWORD>(data.size())) != FALSE;
}

auto remove_resources(HANDLE update, LPCWSTR type, const std::vector<resource_entry>& entries) -> bool
{
    return std::all_of(entries.begin(),
                       entries.end(),
                       [update, type](const resource_entry& entry)
                       {
                           return UpdateResourceW(update, type, entry.name.get(), entry.language, nullptr, 0) != FALSE;
                       });
}

/// Replaces the file's icons with `icon`: its images, then the group that lists them under the first group's
/// name and language.
auto replace_icon(HANDLE update, const existing_resources& found, const std::vector<windows_icon_image>& icon) -> bool
{
    if(!remove_resources(update, get_icon_type(), found.icons) || !remove_resources(update, get_icon_group_type(), found.icon_groups))
    {
        return false;
    }
    const resource_entry group = found.icon_groups.empty() ? resource_entry{{{}, DEFAULT_ICON_GROUP_ID}, US_ENGLISH}
                                                           : found.icon_groups.front();
    for(std::size_t i = 0; i < icon.size(); ++i)
    {
        const auto id = static_cast<WORD>(FIRST_ICON_ID + i);
        if(!update_resource(update, get_icon_type(), MAKEINTRESOURCEW(id), group.language, icon[i].png))
        {
            return false;
        }
    }
    return update_resource(update, get_icon_group_type(), group.name.get(), group.language, make_icon_group_resource(icon, FIRST_ICON_ID));
}

auto replace_version(HANDLE update, const existing_resources& found, const windows_version_info& info) -> bool
{
    return remove_resources(update, get_version_type(), found.versions) &&
           update_resource(update, get_version_type(), MAKEINTRESOURCEW(VERSION_RESOURCE_ID), US_ENGLISH, make_version_resource(info));
}

auto make_windows_error(const char* action, const fs::path& file) -> result
{
    return result::failure(fmt::format("{} {} failed (Windows error {}).", action, file.generic_string(), GetLastError()));
}
#endif
} // namespace

auto make_windows_icon_images(const icon_image& image) -> std::vector<windows_icon_image>
{
    std::vector<windows_icon_image> images;
    for(const std::uint32_t size : WINDOWS_ICON_SIZES)
    {
        images.push_back({size, encode_png(fit_icon_image(image, size, size))});
    }
    return images;
}

auto parse_windows_version(const std::string& version) -> std::array<std::uint16_t, 4>
{
    std::array<std::uint16_t, 4> numbers{};
    std::size_t index = !version.empty() && (version[0] == 'v' || version[0] == 'V') ? 1 : 0;
    for(auto& number : numbers)
    {
        if(index >= version.size() || !std::isdigit(static_cast<unsigned char>(version[index])))
        {
            break;
        }
        std::uint32_t value = 0;
        for(; index < version.size() && std::isdigit(static_cast<unsigned char>(version[index])); ++index)
        {
            value = std::min<std::uint32_t>(value * DECIMAL_BASE + std::uint32_t(version[index] - '0'),
                                            std::numeric_limits<std::uint16_t>::max());
        }
        number = static_cast<std::uint16_t>(value);
        if(index >= version.size() || version[index] != '.')
        {
            break;
        }
        ++index;
    }
    return numbers;
}

auto make_icon_group_resource(const std::vector<windows_icon_image>& images, std::uint16_t first_id) -> std::vector<std::uint8_t>
{
    resource_writer writer;
    writer.write_u16(0);
    writer.write_u16(ICON_GROUP_TYPE);
    writer.write_u16(static_cast<std::uint16_t>(images.size()));
    for(std::size_t i = 0; i < images.size(); ++i)
    {
        const auto side = static_cast<std::uint8_t>(images[i].size >= ICON_SIZE_WRITTEN_AS_ZERO ? 0 : images[i].size);
        writer.write_u8(side);
        writer.write_u8(side);
        // No palette, and a reserved byte.
        writer.write_u8(0);
        writer.write_u8(0);
        writer.write_u16(ICON_PLANES);
        writer.write_u16(ICON_BIT_COUNT);
        writer.write_u32(static_cast<std::uint32_t>(images[i].png.size()));
        writer.write_u16(static_cast<std::uint16_t>(first_id + i));
    }
    return writer.take();
}

auto make_version_resource(const windows_version_info& info) -> std::vector<std::uint8_t>
{
    resource_writer writer;
    const auto root = writer.begin_block(u"VS_VERSION_INFO", FIXED_FILE_INFO_SIZE, BINARY_VALUE);
    write_fixed_file_info(writer, parse_windows_version(info.version));
    write_string_file_info(writer, info);
    write_var_file_info(writer);
    writer.end_block(root);
    return writer.take();
}

auto set_windows_executable_resources(const fs::path& file,
                                      const std::vector<windows_icon_image>& icon,
                                      const windows_version_info& info) -> result
{
#ifdef _WIN32
    existing_resources found;
    if(!read_existing_resources(file, found))
    {
        return make_windows_error("Reading the resources of", file);
    }
    HANDLE update = BeginUpdateResourceW(file.c_str(), FALSE);
    if(update == nullptr)
    {
        return make_windows_error("Opening the resources of", file);
    }
    const bool is_written = (icon.empty() || replace_icon(update, found, icon)) && replace_version(update, found, info);
    const auto written = is_written ? result{} : make_windows_error("Writing the resources of", file);
    // A failed write is discarded and leaves the file as it was.
    if(!EndUpdateResourceW(update, is_written ? FALSE : TRUE) && is_written)
    {
        return make_windows_error("Saving the resources of", file);
    }
    return written;
#else
    (void)icon;
    (void)info;
    return result::failure("The resources of " + file.generic_string() + " can be written on Windows only.");
#endif
}

} // namespace unravel::deployment
