#include "assembly_references.h"

#include <filesystem/mapped_file.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iterator>

// The metadata read here is ECMA-335's: a CLI header points at it, and its "#~" stream holds the tables. The rows of
// a table have a fixed size that depends on the sizes of the heaps and of the other tables, so the tables before the
// one wanted are measured, not read. Two containers hold it: a PE file (.dll), and Webcil, the form a browser build
// ships an assembly in, which keeps the PE's sections under a header of its own inside a WebAssembly module.
namespace unravel::deployment
{
namespace
{
/// The extensions of the files an assembly ships in: a library, and a browser build's module.
constexpr const char* LIBRARY_EXTENSION = ".dll";
constexpr std::array<const char*, 2> ASSEMBLY_EXTENSIONS = {LIBRARY_EXTENSION, ".wasm"};
/// What separates the names of a list a person typed.
constexpr const char* NAME_SEPARATORS = " ,;\t\r\n";

constexpr std::size_t PE_OFFSET_FIELD = 0x3C;
constexpr std::uint32_t PE_SIGNATURE = 0x00004550;
constexpr std::size_t COFF_HEADER_SIZE = 20;
constexpr std::size_t COFF_SECTION_COUNT_OFFSET = 2;
constexpr std::size_t COFF_OPTIONAL_SIZE_OFFSET = 16;
constexpr std::uint16_t PE32_MAGIC = 0x10B;
constexpr std::uint16_t PE32_PLUS_MAGIC = 0x20B;
/// Where the CLI header's entry lies in the optional header's data directories, per format.
constexpr std::size_t PE32_CLI_DIRECTORY_OFFSET = 208;
constexpr std::size_t PE32_PLUS_CLI_DIRECTORY_OFFSET = 224;
/// A PE section header: its name, then the fields both containers have.
constexpr std::size_t PE_SECTION_HEADER_SIZE = 40;
constexpr std::size_t PE_SECTION_FIELDS_OFFSET = 8;

/// "\0asm", and the module's version after it.
constexpr std::uint32_t WASM_SIGNATURE = 0x6D736100;
constexpr std::size_t WASM_HEADER_SIZE = 8;
constexpr std::uint8_t WASM_DATA_SECTION = 11;
/// A data segment that is only its bytes, with no place in the module's memory: the kind a wrapped assembly lies in.
constexpr std::uint32_t WASM_PASSIVE_SEGMENT = 1;
/// The data segment that holds the assembly; the one before it holds its size.
constexpr std::uint32_t WEBCIL_PAYLOAD_SEGMENT = 1;
/// "WbIL".
constexpr std::uint32_t WEBCIL_SIGNATURE = 0x4C496257;
constexpr std::size_t WEBCIL_SECTION_COUNT_OFFSET = 8;
constexpr std::size_t WEBCIL_CLI_HEADER_OFFSET = 12;
constexpr std::size_t WEBCIL_HEADER_SIZE = 28;
constexpr std::size_t WEBCIL_SECTION_HEADER_SIZE = 16;

/// The fields of a section header both containers have, each 32 bits, from where the fields start.
constexpr std::size_t SECTION_ADDRESS_FIELD = 4;
constexpr std::size_t SECTION_RAW_SIZE_FIELD = 8;
constexpr std::size_t SECTION_RAW_POINTER_FIELD = 12;

/// An unsigned LEB128 number: seven bits a byte, the eighth saying that another byte follows.
constexpr std::uint32_t LEB_BITS_PER_BYTE = 7;
constexpr std::uint32_t LEB_VALUE_BITS = 32;
constexpr std::uint8_t LEB_VALUE_MASK = 0x7F;
constexpr std::uint8_t LEB_MORE_FLAG = 0x80;

constexpr std::size_t CLI_METADATA_RVA_OFFSET = 8;
constexpr std::uint32_t METADATA_SIGNATURE = 0x424A5342;
constexpr std::size_t METADATA_VERSION_LENGTH_OFFSET = 12;
constexpr std::size_t METADATA_VERSION_OFFSET = 16;
constexpr std::size_t TABLES_HEAP_SIZES_OFFSET = 6;
constexpr std::size_t TABLES_VALID_OFFSET = 8;
constexpr std::size_t TABLES_ROWS_OFFSET = 24;
constexpr std::uint8_t WIDE_STRING_INDEXES = 0x01;
constexpr std::uint8_t WIDE_GUID_INDEXES = 0x02;
constexpr std::uint8_t WIDE_BLOB_INDEXES = 0x04;
constexpr std::size_t NARROW_INDEX_SIZE = 2;
constexpr std::size_t WIDE_INDEX_SIZE = 4;
/// A simple index is narrow while its table has fewer rows than this; a coded index loses its tag's bits of it.
constexpr std::uint32_t NARROW_INDEX_BITS = 16;
constexpr std::size_t TABLE_COUNT = 64;

// The tables, by their numbers.
enum table : std::uint8_t
{
    module_table = 0x00,
    type_ref_table = 0x01,
    type_def_table = 0x02,
    field_table = 0x04,
    method_def_table = 0x06,
    param_table = 0x08,
    interface_impl_table = 0x09,
    member_ref_table = 0x0A,
    decl_security_table = 0x0E,
    stand_alone_sig_table = 0x11,
    event_table = 0x14,
    property_table = 0x17,
    module_ref_table = 0x1A,
    type_spec_table = 0x1B,
    assembly_table = 0x20,
    assembly_ref_table = 0x23,
    file_table = 0x26,
    exported_type_table = 0x27,
    manifest_resource_table = 0x28,
    generic_param_table = 0x2A,
    method_spec_table = 0x2B,
    generic_param_constraint_table = 0x2C,
    /// No table: a tag value of a coded index that names none.
    no_table = 0xFF,
};

/// What a column of a table holds, which decides how many bytes it takes.
enum class column : std::uint8_t
{
    u16,
    u32,
    string,
    guid,
    blob,
    field_index,
    method_index,
    param_index,
    type_def_index,
    event_index,
    property_index,
    module_ref_index,
    type_def_or_ref,
    has_constant,
    has_custom_attribute,
    has_field_marshal,
    has_decl_security,
    member_ref_parent,
    has_semantics,
    method_def_or_ref,
    member_forwarded,
    custom_attribute_type,
    resolution_scope,
    end,
};

/// The most columns a table before AssemblyRef has.
constexpr std::size_t MAX_COLUMNS = 9;
using columns = std::array<column, MAX_COLUMNS + 1>;

/// The columns of the tables up to AssemblyRef, by table number; a table without columns here does not exist in
/// that range.
constexpr std::size_t DESCRIBED_TABLES = assembly_ref_table + 1;
constexpr std::array<columns, DESCRIBED_TABLES> TABLE_COLUMNS = {{
    /* 0x00 Module */ {column::u16, column::string, column::guid, column::guid, column::guid, column::end},
    /* 0x01 TypeRef */ {column::resolution_scope, column::string, column::string, column::end},
    /* 0x02 TypeDef */
    {column::u32,
     column::string,
     column::string,
     column::type_def_or_ref,
     column::field_index,
     column::method_index,
     column::end},
    /* 0x03 FieldPtr */ {column::field_index, column::end},
    /* 0x04 Field */ {column::u16, column::string, column::blob, column::end},
    /* 0x05 MethodPtr */ {column::method_index, column::end},
    /* 0x06 MethodDef */
    {column::u32, column::u16, column::u16, column::string, column::blob, column::param_index, column::end},
    /* 0x07 ParamPtr */ {column::param_index, column::end},
    /* 0x08 Param */ {column::u16, column::u16, column::string, column::end},
    /* 0x09 InterfaceImpl */ {column::type_def_index, column::type_def_or_ref, column::end},
    /* 0x0A MemberRef */ {column::member_ref_parent, column::string, column::blob, column::end},
    /* 0x0B Constant */ {column::u16, column::has_constant, column::blob, column::end},
    /* 0x0C CustomAttribute */ {column::has_custom_attribute, column::custom_attribute_type, column::blob, column::end},
    /* 0x0D FieldMarshal */ {column::has_field_marshal, column::blob, column::end},
    /* 0x0E DeclSecurity */ {column::u16, column::has_decl_security, column::blob, column::end},
    /* 0x0F ClassLayout */ {column::u16, column::u32, column::type_def_index, column::end},
    /* 0x10 FieldLayout */ {column::u32, column::field_index, column::end},
    /* 0x11 StandAloneSig */ {column::blob, column::end},
    /* 0x12 EventMap */ {column::type_def_index, column::event_index, column::end},
    /* 0x13 EventPtr */ {column::event_index, column::end},
    /* 0x14 Event */ {column::u16, column::string, column::type_def_or_ref, column::end},
    /* 0x15 PropertyMap */ {column::type_def_index, column::property_index, column::end},
    /* 0x16 PropertyPtr */ {column::property_index, column::end},
    /* 0x17 Property */ {column::u16, column::string, column::blob, column::end},
    /* 0x18 MethodSemantics */ {column::u16, column::method_index, column::has_semantics, column::end},
    /* 0x19 MethodImpl */ {column::type_def_index, column::method_def_or_ref, column::method_def_or_ref, column::end},
    /* 0x1A ModuleRef */ {column::string, column::end},
    /* 0x1B TypeSpec */ {column::blob, column::end},
    /* 0x1C ImplMap */ {column::u16, column::member_forwarded, column::string, column::module_ref_index, column::end},
    /* 0x1D FieldRVA */ {column::u32, column::field_index, column::end},
    /* 0x1E EncLog */ {column::u32, column::u32, column::end},
    /* 0x1F EncMap */ {column::u32, column::end},
    /* 0x20 Assembly */
    {column::u32,
     column::u16,
     column::u16,
     column::u16,
     column::u16,
     column::u32,
     column::blob,
     column::string,
     column::string,
     column::end},
    /* 0x21 AssemblyProcessor */ {column::u32, column::end},
    /* 0x22 AssemblyOS */ {column::u32, column::u32, column::u32, column::end},
    /* 0x23 AssemblyRef */
    {column::u16,
     column::u16,
     column::u16,
     column::u16,
     column::u32,
     column::blob,
     column::string,
     column::string,
     column::blob,
     column::end},
}};

/// The name column of the Assembly and AssemblyRef tables: after the version, the flags and the public key.
constexpr std::size_t ASSEMBLY_NAME_COLUMN = 7;
constexpr std::size_t ASSEMBLY_REF_NAME_COLUMN = 6;

/// The most tables a coded index chooses among: HasCustomAttribute's.
constexpr std::size_t MAX_CODED_TABLES = 22;

/// The tables a coded index chooses among, by its tag, and the bits its tag takes.
struct coded_index
{
    std::array<std::uint8_t, MAX_CODED_TABLES> tables;
    std::size_t table_count;
    std::uint32_t tag_bits;
};

constexpr coded_index TYPE_DEF_OR_REF{{type_def_table, type_ref_table, type_spec_table}, 3, 2};
constexpr coded_index HAS_CONSTANT{{field_table, param_table, property_table}, 3, 2};
constexpr coded_index HAS_CUSTOM_ATTRIBUTE{
    {method_def_table,        field_table,         type_ref_table,
     type_def_table,          param_table,         interface_impl_table,
     member_ref_table,        module_table,        decl_security_table,
     property_table,          event_table,         stand_alone_sig_table,
     module_ref_table,        type_spec_table,     assembly_table,
     assembly_ref_table,      file_table,          exported_type_table,
     manifest_resource_table, generic_param_table, generic_param_constraint_table,
     method_spec_table},
    MAX_CODED_TABLES,
    5};
constexpr coded_index HAS_FIELD_MARSHAL{{field_table, param_table}, 2, 1};
constexpr coded_index HAS_DECL_SECURITY{{type_def_table, method_def_table, assembly_table}, 3, 2};
constexpr coded_index MEMBER_REF_PARENT{
    {type_def_table, type_ref_table, module_ref_table, method_def_table, type_spec_table},
    5,
    3};
constexpr coded_index HAS_SEMANTICS{{event_table, property_table}, 2, 1};
constexpr coded_index METHOD_DEF_OR_REF{{method_def_table, member_ref_table}, 2, 1};
constexpr coded_index MEMBER_FORWARDED{{field_table, method_def_table}, 2, 1};
constexpr coded_index CUSTOM_ATTRIBUTE_TYPE{{no_table, no_table, method_def_table, member_ref_table, no_table}, 5, 3};
constexpr coded_index RESOLUTION_SCOPE{{module_table, module_ref_table, assembly_ref_table, type_ref_table}, 4, 2};

/// Bounds-checked reads of a file's bytes; a read outside them marks the reader failed and gives zero.
class byte_view
{
public:
    byte_view(const std::uint8_t* data, std::size_t size) : data_(data), size_(size)
    {
    }

    template<typename T>
    auto read(std::size_t offset) -> T
    {
        T value{};
        if(offset > size_ || sizeof(T) > size_ - offset)
        {
            has_failed_ = true;
            return value;
        }
        std::memcpy(&value, data_ + offset, sizeof(T));
        return value;
    }

    /// The text that starts at `offset` and ends at its zero byte; empty, and failed, when it does not end.
    auto read_text(std::size_t offset) -> std::string
    {
        const auto* end =
            offset < size_ ? static_cast<const std::uint8_t*>(std::memchr(data_ + offset, 0, size_ - offset)) : nullptr;
        if(end == nullptr)
        {
            has_failed_ = true;
            return {};
        }
        return {reinterpret_cast<const char*>(data_ + offset), static_cast<std::size_t>(end - (data_ + offset))};
    }

    /// The unsigned LEB128 number at `cursor`, which moves past it; one that does not end within 32 bits fails.
    auto read_leb(std::size_t& cursor) -> std::uint32_t
    {
        std::uint32_t value = 0;
        for(std::uint32_t shift = 0; shift < LEB_VALUE_BITS; shift += LEB_BITS_PER_BYTE)
        {
            const auto byte = read<std::uint8_t>(cursor++);
            value |= std::uint32_t(byte & LEB_VALUE_MASK) << shift;
            if((byte & LEB_MORE_FLAG) == 0)
            {
                return value;
            }
        }
        has_failed_ = true;
        return 0;
    }

    auto size() const -> std::size_t
    {
        return size_;
    }

    auto has_failed() const -> bool
    {
        return has_failed_;
    }

private:
    const std::uint8_t* data_{};
    std::size_t size_{};
    bool has_failed_{};
};

/// A range of the loaded image's addresses, and where its bytes lie in the file.
struct image_section
{
    std::uint32_t address{};
    std::uint32_t size{};
    std::size_t file_offset{};
};

/// How an assembly's image lies in its file, whichever container holds it.
struct image_layout
{
    std::vector<image_section> sections;
    /// The image address of the CLI header; 0 for a file that holds no .NET assembly.
    std::uint32_t cli_header{};
};

/// A container's table of section headers.
struct section_table
{
    std::size_t headers{};
    std::size_t count{};
    std::size_t header_size{};
    /// Where the fields both containers have start in a header.
    std::size_t fields_offset{};
    /// What a header's file pointer counts from.
    std::size_t data_start{};
};

auto read_sections(byte_view& file, const section_table& table) -> std::vector<image_section>
{
    std::vector<image_section> sections;
    for(std::size_t index = 0; index < table.count && !file.has_failed(); ++index)
    {
        const auto fields = table.headers + index * table.header_size + table.fields_offset;
        image_section section;
        section.address = file.read<std::uint32_t>(fields + SECTION_ADDRESS_FIELD);
        // The bytes the file holds of the section; an image address past them has none to read.
        section.size = file.read<std::uint32_t>(fields + SECTION_RAW_SIZE_FIELD);
        section.file_offset = table.data_start + file.read<std::uint32_t>(fields + SECTION_RAW_POINTER_FIELD);
        sections.push_back(section);
    }
    return sections;
}

/// The file offset of the address `rva` of the loaded image, through the section that holds it; 0 when none does.
auto get_file_offset(const image_layout& layout, std::uint32_t rva) -> std::size_t
{
    const auto holder = std::find_if(layout.sections.begin(),
                                     layout.sections.end(),
                                     [rva](const image_section& section)
                                     {
                                         return rva >= section.address && rva - section.address < section.size;
                                     });
    return holder == layout.sections.end() ? 0 : holder->file_offset + (rva - holder->address);
}

auto read_pe_layout(byte_view& file) -> image_layout
{
    const std::size_t pe = file.read<std::uint32_t>(PE_OFFSET_FIELD);
    if(file.read<std::uint32_t>(pe) != PE_SIGNATURE)
    {
        return {};
    }
    const auto coff = pe + sizeof(std::uint32_t);
    const auto optional = coff + COFF_HEADER_SIZE;
    const auto magic = file.read<std::uint16_t>(optional);
    if(magic != PE32_MAGIC && magic != PE32_PLUS_MAGIC)
    {
        return {};
    }
    image_layout layout;
    layout.cli_header = file.read<std::uint32_t>(
        optional + (magic == PE32_MAGIC ? PE32_CLI_DIRECTORY_OFFSET : PE32_PLUS_CLI_DIRECTORY_OFFSET));
    // A native library has no CLI header, and nothing more of it is read.
    if(layout.cli_header == 0)
    {
        return layout;
    }
    const auto headers = optional + file.read<std::uint16_t>(coff + COFF_OPTIONAL_SIZE_OFFSET);
    const auto count = file.read<std::uint16_t>(coff + COFF_SECTION_COUNT_OFFSET);
    layout.sections = read_sections(file, {headers, count, PE_SECTION_HEADER_SIZE, PE_SECTION_FIELDS_OFFSET, 0});
    return layout;
}

/// Where the content of a WebAssembly module's data section starts; 0 for a module without one.
auto find_data_section(byte_view& file) -> std::size_t
{
    std::size_t cursor = WASM_HEADER_SIZE;
    while(cursor < file.size() && !file.has_failed())
    {
        const auto id = file.read<std::uint8_t>(cursor++);
        const std::size_t size = file.read_leb(cursor);
        if(id == WASM_DATA_SECTION)
        {
            return cursor;
        }
        cursor += size;
    }
    return 0;
}

/// Where the assembly a WebAssembly module wraps starts in the file; 0 for a module that wraps none, as the
/// runtime's own modules do.
auto find_webcil_payload(byte_view& file) -> std::size_t
{
    auto cursor = find_data_section(file);
    if(cursor == 0)
    {
        return 0;
    }
    const auto segment_count = file.read_leb(cursor);
    for(std::uint32_t index = 0; index < segment_count && !file.has_failed(); ++index)
    {
        if(file.read_leb(cursor) != WASM_PASSIVE_SEGMENT)
        {
            return 0;
        }
        const std::size_t length = file.read_leb(cursor);
        if(index == WEBCIL_PAYLOAD_SEGMENT)
        {
            return cursor;
        }
        cursor += length;
    }
    return 0;
}

auto read_webcil_layout(byte_view& file) -> image_layout
{
    const auto payload = find_webcil_payload(file);
    if(payload == 0 || file.read<std::uint32_t>(payload) != WEBCIL_SIGNATURE)
    {
        return {};
    }
    image_layout layout;
    layout.cli_header = file.read<std::uint32_t>(payload + WEBCIL_CLI_HEADER_OFFSET);
    const auto count = file.read<std::uint16_t>(payload + WEBCIL_SECTION_COUNT_OFFSET);
    // A Webcil section header is the fields alone, and its file pointer counts from the payload's start.
    layout.sections =
        read_sections(file, {payload + WEBCIL_HEADER_SIZE, count, WEBCIL_SECTION_HEADER_SIZE, 0, payload});
    return layout;
}

auto read_image_layout(byte_view& file) -> image_layout
{
    return file.read<std::uint32_t>(0) == WASM_SIGNATURE ? read_webcil_layout(file) : read_pe_layout(file);
}

/// Where the metadata starts in the file; 0 for a file that is no .NET assembly.
auto find_metadata(byte_view& file) -> std::size_t
{
    const auto layout = read_image_layout(file);
    const auto cli = layout.cli_header == 0 ? 0 : get_file_offset(layout, layout.cli_header);
    if(cli == 0 || file.has_failed())
    {
        return 0;
    }
    const auto metadata = get_file_offset(layout, file.read<std::uint32_t>(cli + CLI_METADATA_RVA_OFFSET));
    return metadata != 0 && file.read<std::uint32_t>(metadata) == METADATA_SIGNATURE ? metadata : 0;
}

/// Where the streams the reader needs start in the file; 0 for one the metadata lacks.
struct metadata_streams
{
    std::size_t tables{};
    std::size_t strings{};
};

auto find_streams(byte_view& file, std::size_t metadata) -> metadata_streams
{
    // The version text is padded to a multiple of four; the flags and the stream count follow it.
    const auto version_length = file.read<std::uint32_t>(metadata + METADATA_VERSION_LENGTH_OFFSET);
    auto cursor = metadata + METADATA_VERSION_OFFSET + version_length + sizeof(std::uint16_t);
    const auto stream_count = file.read<std::uint16_t>(cursor);
    cursor += sizeof(std::uint16_t);
    metadata_streams streams;
    for(std::uint16_t index = 0; index < stream_count && !file.has_failed(); ++index)
    {
        const auto offset = file.read<std::uint32_t>(cursor);
        const auto name = file.read_text(cursor + 2 * sizeof(std::uint32_t));
        if(name == "#~" || name == "#-")
        {
            streams.tables = metadata + offset;
        }
        else if(name == "#Strings")
        {
            streams.strings = metadata + offset;
        }
        // A stream's name is padded with zeros to a multiple of four, its ending zero included.
        cursor += 2 * sizeof(std::uint32_t) + ((name.size() + 1 + 3) & ~std::size_t(3));
    }
    return streams;
}

/// The sizes the tables stream states: of its heaps' indexes and of every table.
struct table_sizes
{
    std::size_t string_index{NARROW_INDEX_SIZE};
    std::size_t guid_index{NARROW_INDEX_SIZE};
    std::size_t blob_index{NARROW_INDEX_SIZE};
    std::array<std::uint32_t, TABLE_COUNT> rows{};
    /// Where the first table's rows start in the file.
    std::size_t rows_start{};
};

auto read_table_sizes(byte_view& file, std::size_t tables) -> table_sizes
{
    table_sizes sizes;
    const auto heap_sizes = file.read<std::uint8_t>(tables + TABLES_HEAP_SIZES_OFFSET);
    sizes.string_index = (heap_sizes & WIDE_STRING_INDEXES) != 0 ? WIDE_INDEX_SIZE : NARROW_INDEX_SIZE;
    sizes.guid_index = (heap_sizes & WIDE_GUID_INDEXES) != 0 ? WIDE_INDEX_SIZE : NARROW_INDEX_SIZE;
    sizes.blob_index = (heap_sizes & WIDE_BLOB_INDEXES) != 0 ? WIDE_INDEX_SIZE : NARROW_INDEX_SIZE;
    const auto valid = file.read<std::uint64_t>(tables + TABLES_VALID_OFFSET);
    auto cursor = tables + TABLES_ROWS_OFFSET;
    for(std::size_t index = 0; index < TABLE_COUNT; ++index)
    {
        if((valid >> index) & 1)
        {
            sizes.rows[index] = file.read<std::uint32_t>(cursor);
            cursor += sizeof(std::uint32_t);
        }
    }
    sizes.rows_start = cursor;
    return sizes;
}

auto get_table_index_size(const table_sizes& sizes, std::uint8_t table_number) -> std::size_t
{
    return sizes.rows[table_number] < (std::uint32_t(1) << NARROW_INDEX_BITS) ? NARROW_INDEX_SIZE : WIDE_INDEX_SIZE;
}

auto get_coded_index_size(const table_sizes& sizes, const coded_index& coded) -> std::size_t
{
    std::uint32_t most_rows = 0;
    for(std::size_t index = 0; index < coded.table_count; ++index)
    {
        if(coded.tables[index] != no_table)
        {
            most_rows = std::max(most_rows, sizes.rows[coded.tables[index]]);
        }
    }
    return most_rows < (std::uint32_t(1) << (NARROW_INDEX_BITS - coded.tag_bits)) ? NARROW_INDEX_SIZE : WIDE_INDEX_SIZE;
}

auto get_column_size(const table_sizes& sizes, column kind) -> std::size_t
{
    switch(kind)
    {
        case column::u16:
            return sizeof(std::uint16_t);
        case column::u32:
            return sizeof(std::uint32_t);
        case column::string:
            return sizes.string_index;
        case column::guid:
            return sizes.guid_index;
        case column::blob:
            return sizes.blob_index;
        case column::field_index:
            return get_table_index_size(sizes, field_table);
        case column::method_index:
            return get_table_index_size(sizes, method_def_table);
        case column::param_index:
            return get_table_index_size(sizes, param_table);
        case column::type_def_index:
            return get_table_index_size(sizes, type_def_table);
        case column::event_index:
            return get_table_index_size(sizes, event_table);
        case column::property_index:
            return get_table_index_size(sizes, property_table);
        case column::module_ref_index:
            return get_table_index_size(sizes, module_ref_table);
        case column::type_def_or_ref:
            return get_coded_index_size(sizes, TYPE_DEF_OR_REF);
        case column::has_constant:
            return get_coded_index_size(sizes, HAS_CONSTANT);
        case column::has_custom_attribute:
            return get_coded_index_size(sizes, HAS_CUSTOM_ATTRIBUTE);
        case column::has_field_marshal:
            return get_coded_index_size(sizes, HAS_FIELD_MARSHAL);
        case column::has_decl_security:
            return get_coded_index_size(sizes, HAS_DECL_SECURITY);
        case column::member_ref_parent:
            return get_coded_index_size(sizes, MEMBER_REF_PARENT);
        case column::has_semantics:
            return get_coded_index_size(sizes, HAS_SEMANTICS);
        case column::method_def_or_ref:
            return get_coded_index_size(sizes, METHOD_DEF_OR_REF);
        case column::member_forwarded:
            return get_coded_index_size(sizes, MEMBER_FORWARDED);
        case column::custom_attribute_type:
            return get_coded_index_size(sizes, CUSTOM_ATTRIBUTE_TYPE);
        case column::resolution_scope:
            return get_coded_index_size(sizes, RESOLUTION_SCOPE);
        case column::end:
            break;
    }
    return 0;
}

/// The bytes of the first `count` columns of a table's row; of the whole row for MAX_COLUMNS.
auto get_columns_size(const table_sizes& sizes, std::uint8_t table_number, std::size_t count) -> std::size_t
{
    std::size_t size = 0;
    const auto& described = TABLE_COLUMNS[table_number];
    for(std::size_t index = 0; index < count && described[index] != column::end; ++index)
    {
        size += get_column_size(sizes, described[index]);
    }
    return size;
}

/// Where the rows of `table_number` start in the file: after the rows of every table before it.
auto get_table_start(const table_sizes& sizes, std::uint8_t table_number) -> std::size_t
{
    std::size_t start = sizes.rows_start;
    for(std::uint8_t index = 0; index < table_number; ++index)
    {
        start += std::size_t(sizes.rows[index]) * get_columns_size(sizes, index, MAX_COLUMNS);
    }
    return start;
}

/// The names in the string column `name_column` of every row of `table_number`.
auto read_names(byte_view& file,
                const table_sizes& sizes,
                std::size_t strings,
                std::uint8_t table_number,
                std::size_t name_column) -> std::vector<std::string>
{
    std::vector<std::string> names;
    const auto row_size = get_columns_size(sizes, table_number, MAX_COLUMNS);
    const auto name_offset = get_columns_size(sizes, table_number, name_column);
    const auto start = get_table_start(sizes, table_number);
    for(std::uint32_t row = 0; row < sizes.rows[table_number] && !file.has_failed(); ++row)
    {
        const auto cell = start + std::size_t(row) * row_size + name_offset;
        const std::size_t index =
            sizes.string_index == WIDE_INDEX_SIZE ? file.read<std::uint32_t>(cell) : file.read<std::uint16_t>(cell);
        names.push_back(file.read_text(strings + index));
    }
    return names;
}

auto read_assembly_metadata(byte_view& file) -> assembly_info
{
    const auto metadata = find_metadata(file);
    const auto streams = metadata == 0 ? metadata_streams{} : find_streams(file, metadata);
    if(streams.tables == 0 || streams.strings == 0 || file.has_failed())
    {
        return {};
    }
    const auto sizes = read_table_sizes(file, streams.tables);
    const auto own_names = read_names(file, sizes, streams.strings, assembly_table, ASSEMBLY_NAME_COLUMN);
    assembly_info info;
    info.references = read_names(file, sizes, streams.strings, assembly_ref_table, ASSEMBLY_REF_NAME_COLUMN);
    // A module of a multi-file assembly has metadata and no Assembly row: it is no assembly of its own.
    if(file.has_failed() || own_names.empty())
    {
        return {};
    }
    info.is_managed = true;
    info.name = own_names.front();
    return info;
}
} // namespace

auto read_assembly_info(const fs::path& file) -> assembly_info
{
    fs::mapped_file mapped;
    std::string error;
    if(!mapped.open(file, error))
    {
        return {};
    }
    byte_view bytes(mapped.data(), std::size_t(mapped.size()));
    return read_assembly_metadata(bytes);
}

auto has_assembly_extension(const fs::path& file) -> bool
{
    const auto extension = file.extension();
    return std::any_of(ASSEMBLY_EXTENSIONS.begin(),
                       ASSEMBLY_EXTENSIONS.end(),
                       [&extension](const char* candidate)
                       {
                           return extension == candidate;
                       });
}

auto split_assembly_names(const std::string& text) -> std::vector<std::string>
{
    const std::string extension(LIBRARY_EXTENSION);
    auto names = string_utils::tokenize(text, NAME_SEPARATORS);
    for(auto& name : names)
    {
        if(name.ends_with(extension))
        {
            name.erase(name.size() - extension.size());
        }
    }
    std::erase(names, std::string());
    return names;
}

auto list_assemblies(const fs::path& folder) -> assembly_files
{
    assembly_files assemblies;
    fs::error_code ec;
    for(fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        auto info = it->is_regular_file(ec) && has_assembly_extension(it->path()) ? read_assembly_info(it->path())
                                                                                  : assembly_info{};
        if(info.is_managed)
        {
            assemblies.emplace(std::move(info.name), listed_assembly{it->path(), std::move(info.references)});
        }
    }
    return assemblies;
}

auto find_unused_assemblies(const std::vector<fs::path>& roots,
                            const assembly_files& framework,
                            const std::vector<std::string>& kept) -> assembly_files
{
    std::set<std::string> reached;
    std::deque<std::string> pending;
    // Takes the framework's assembly of that name, when it has one, and has its references followed.
    const auto reach = [&](const std::string& name)
    {
        if(framework.contains(name) && reached.insert(name).second)
        {
            pending.push_back(name);
        }
    };
    std::for_each(kept.begin(), kept.end(), reach);
    for(const auto& root : roots)
    {
        const auto info = read_assembly_info(root);
        std::for_each(info.references.begin(), info.references.end(), reach);
    }
    while(!pending.empty())
    {
        const auto name = std::move(pending.front());
        pending.pop_front();
        const auto& references = framework.at(name).references;
        std::for_each(references.begin(), references.end(), reach);
    }
    assembly_files unused;
    std::copy_if(framework.begin(),
                 framework.end(),
                 std::inserter(unused, unused.end()),
                 [&reached](const assembly_files::value_type& assembly)
                 {
                     return !reached.contains(assembly.first);
                 });
    return unused;
}

auto find_trimmed_files(const fs::path& framework, const framework_trim& trim) -> std::set<std::string>
{
    std::set<std::string> trimmed;
    if(!trim.is_on)
    {
        return trimmed;
    }
    auto kept = trim.kept;
    kept.emplace_back(RUNTIME_CORE_ASSEMBLY);
    for(const auto& [name, assembly] : find_unused_assemblies(trim.roots, list_assemblies(framework), kept))
    {
        trimmed.insert(fs::to_utf8_string(assembly.file.filename()));
    }
    return trimmed;
}

} // namespace unravel::deployment
