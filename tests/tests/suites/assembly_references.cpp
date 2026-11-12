/*
 * Validation suite for the deploy's .NET assembly reader and framework trim (editor/core/deploy/assembly_references).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "assembly references"
 *
 * A trimmed build ships only the framework assemblies its own libraries reach through references, so a reference
 * read wrong or a file taken for something else removes a library the game loads. This pins:
 *   - an assembly's name and references read from a library (.dll), with narrow and with wide string indexes, and
 *     the same from the module a browser build wraps the assembly in (.wasm);
 *   - files that are no assembly read as such: an empty file, text, a library cut short anywhere, a native
 *     library, a WebAssembly module that wraps none, a metadata module without an assembly of its own;
 *   - a folder's assemblies listed by the name their metadata gives, whatever their files are called;
 *   - the assemblies nothing reaches: references followed through the framework from the roots and from the kept
 *     names, a cycle of references, roots that are missing or no assembly, names the framework lacks;
 *   - a build's trimmed files: none when the trim is off, the runtime's own library never among them;
 *   - names a person typed, split on spaces, commas, semicolons and line ends, with a library's file name taken as
 *     its name;
 *   - the script bridge beside the runner and the browser player's runtime, when they are there: real libraries
 *     with every kind of table before the references, and the player's own libraries kept by a browser build's trim.
 * Everything else lives in a temp folder.
 */

#include "../tests.h"

#include <deploy/assembly_references.h>
#include <deploy/web.h>
#include <filesystem/filesystem.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
namespace deployment = unravel::deployment;

/// Where the test assemblies' one section lies in the loaded image, and its name's size in a PE header.
constexpr std::uint32_t SECTION_ADDRESS = 0x2000;
constexpr std::size_t CLI_HEADER_SIZE = 72;
constexpr std::size_t PE_HEADERS_SIZE = 0x200;
constexpr std::size_t PE_OFFSET = 0x80;
constexpr std::size_t PE_OPTIONAL_HEADER_SIZE = 224;
constexpr std::size_t PE_CLI_DIRECTORY_OFFSET = 208;
constexpr std::size_t WEBCIL_HEADERS_SIZE = 28 + 16;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto get_temp_folder() -> fs::path
{
    return fs::temp_directory_path() / "unravel-tests-assembly-references";
}

using bytes = std::vector<std::uint8_t>;

template<typename T>
void append(bytes& out, T value)
{
    for(std::size_t index = 0; index < sizeof(T); ++index)
    {
        out.push_back(std::uint8_t(std::uint64_t(value) >> (8 * index)));
    }
}

void append_text(bytes& out, const std::string& text, std::size_t padded_size)
{
    out.insert(out.end(), text.begin(), text.end());
    out.resize(out.size() + padded_size - text.size(), 0);
}

void append_bytes(bytes& out, const bytes& more)
{
    out.insert(out.end(), more.begin(), more.end());
}

template<typename T>
void write_at(bytes& out, std::size_t offset, T value)
{
    for(std::size_t index = 0; index < sizeof(T); ++index)
    {
        out[offset + index] = std::uint8_t(std::uint64_t(value) >> (8 * index));
    }
}

void append_index(bytes& out, std::size_t index, bool is_wide)
{
    is_wide ? append(out, std::uint32_t(index)) : append(out, std::uint16_t(index));
}

/// What a test assembly is made of.
struct assembly_spec
{
    std::string name;
    std::vector<std::string> references;
    /// Four-byte indexes into the names, as a library with a large string heap has.
    bool has_wide_strings{};
    /// Without the row that makes metadata an assembly: a module of a multi-file assembly.
    bool is_module_only{};
};

/// The tables stream of a test assembly: a Module row, the Assembly row and one AssemblyRef row per reference.
auto make_tables(const assembly_spec& spec, const std::vector<std::size_t>& name_offsets) -> bytes
{
    constexpr std::uint64_t MODULE_TABLE = std::uint64_t(1) << 0x00;
    constexpr std::uint64_t ASSEMBLY_TABLE = std::uint64_t(1) << 0x20;
    constexpr std::uint64_t ASSEMBLY_REF_TABLE = std::uint64_t(1) << 0x23;
    const bool wide = spec.has_wide_strings;
    bytes tables;
    append(tables, std::uint32_t(0));
    append(tables, std::uint8_t(2));
    append(tables, std::uint8_t(0));
    append(tables, std::uint8_t(wide ? 0x01 : 0x00));
    append(tables, std::uint8_t(1));
    append(tables, MODULE_TABLE | (spec.is_module_only ? 0 : ASSEMBLY_TABLE) | ASSEMBLY_REF_TABLE);
    append(tables, std::uint64_t(0));
    append(tables, std::uint32_t(1));
    if(!spec.is_module_only)
    {
        append(tables, std::uint32_t(1));
    }
    append(tables, std::uint32_t(spec.references.size()));
    // Module: generation, name, three guids.
    append(tables, std::uint16_t(0));
    append_index(tables, 0, wide);
    append(tables, std::uint16_t(0));
    append(tables, std::uint16_t(0));
    append(tables, std::uint16_t(0));
    if(!spec.is_module_only)
    {
        // Assembly: hash algorithm, version, flags, public key, name, culture.
        append(tables, std::uint32_t(0));
        append(tables, std::uint64_t(0));
        append(tables, std::uint32_t(0));
        append(tables, std::uint16_t(0));
        append_index(tables, name_offsets.front(), wide);
        append_index(tables, 0, wide);
    }
    for(std::size_t index = 0; index < spec.references.size(); ++index)
    {
        // AssemblyRef: version, flags, public key token, name, culture, hash.
        append(tables, std::uint64_t(0));
        append(tables, std::uint32_t(0));
        append(tables, std::uint16_t(0));
        append_index(tables, name_offsets[index + 1], wide);
        append_index(tables, 0, wide);
        append(tables, std::uint16_t(0));
    }
    return tables;
}

/// A test assembly's one section: the CLI header, then the metadata with its tables and names.
auto make_section(const assembly_spec& spec) -> bytes
{
    bytes strings{0};
    std::vector<std::size_t> name_offsets;
    std::vector<std::string> names{spec.name};
    names.insert(names.end(), spec.references.begin(), spec.references.end());
    for(const auto& name : names)
    {
        name_offsets.push_back(strings.size());
        append_text(strings, name, name.size() + 1);
    }
    const auto tables = make_tables(spec, name_offsets);
    bytes metadata;
    append(metadata, std::uint32_t(0x424A5342));
    append(metadata, std::uint16_t(1));
    append(metadata, std::uint16_t(1));
    append(metadata, std::uint32_t(0));
    append(metadata, std::uint32_t(12));
    append_text(metadata, "v4.0.30319", 12);
    append(metadata, std::uint16_t(0));
    append(metadata, std::uint16_t(2));
    // Two stream headers: offset, size, name padded to four bytes.
    const std::size_t headers_size = (8 + 4) + (8 + 12);
    const auto tables_offset = metadata.size() + headers_size;
    append(metadata, std::uint32_t(tables_offset));
    append(metadata, std::uint32_t(tables.size()));
    append_text(metadata, "#~", 4);
    append(metadata, std::uint32_t(tables_offset + tables.size()));
    append(metadata, std::uint32_t(strings.size()));
    append_text(metadata, "#Strings", 12);
    append_bytes(metadata, tables);
    append_bytes(metadata, strings);
    bytes section(CLI_HEADER_SIZE, 0);
    write_at(section, 0, std::uint32_t(CLI_HEADER_SIZE));
    write_at(section, 8, std::uint32_t(SECTION_ADDRESS + CLI_HEADER_SIZE));
    write_at(section, 12, std::uint32_t(metadata.size()));
    append_bytes(section, metadata);
    return section;
}

/// A library file holding `section`; `cli_header` is the address its headers give for the CLI header, 0 for a
/// native library.
auto make_library(const bytes& section, std::uint32_t cli_header = SECTION_ADDRESS) -> bytes
{
    bytes file(PE_HEADERS_SIZE, 0);
    file[0] = 'M';
    file[1] = 'Z';
    write_at(file, 0x3C, std::uint32_t(PE_OFFSET));
    write_at(file, PE_OFFSET, std::uint32_t(0x00004550));
    const auto coff = PE_OFFSET + 4;
    write_at(file, coff + 2, std::uint16_t(1));
    write_at(file, coff + 16, std::uint16_t(PE_OPTIONAL_HEADER_SIZE));
    const auto optional = coff + 20;
    write_at(file, optional, std::uint16_t(0x10B));
    write_at(file, optional + PE_CLI_DIRECTORY_OFFSET, cli_header);
    write_at(file, optional + PE_CLI_DIRECTORY_OFFSET + 4, std::uint32_t(CLI_HEADER_SIZE));
    const auto header = optional + PE_OPTIONAL_HEADER_SIZE;
    write_at(file, header + 8, std::uint32_t(section.size()));
    write_at(file, header + 12, SECTION_ADDRESS);
    write_at(file, header + 16, std::uint32_t(section.size()));
    write_at(file, header + 20, std::uint32_t(PE_HEADERS_SIZE));
    append_bytes(file, section);
    return file;
}

void append_leb(bytes& out, std::uint32_t value)
{
    do
    {
        const std::uint8_t low = value & 0x7F;
        value >>= 7;
        out.push_back(std::uint8_t(low | (value != 0 ? 0x80 : 0)));
    } while(value != 0);
}

/// A WebAssembly module whose data section holds `segments`, each passive or placed in memory.
auto make_module(const std::vector<bytes>& segments, bool is_passive = true) -> bytes
{
    bytes data;
    append_leb(data, std::uint32_t(segments.size()));
    for(const auto& segment : segments)
    {
        if(is_passive)
        {
            append_leb(data, 1);
        }
        else
        {
            // An active segment: memory 0, at the address an i32.const gives.
            append_bytes(data, {0x00, 0x41, 0x00, 0x0B});
        }
        append_leb(data, std::uint32_t(segment.size()));
        append_bytes(data, segment);
    }
    bytes file{0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00};
    // A custom section first, which the reader passes over.
    append_bytes(file, {0x00, 0x03, 0x01, 'x', 0x00});
    file.push_back(11);
    append_leb(file, std::uint32_t(data.size()));
    append_bytes(file, data);
    return file;
}

/// The module a browser build wraps `section` in: a segment with the payload's size, then the payload, which is the
/// section under a header of its own.
auto make_browser_module(const bytes& section) -> bytes
{
    bytes payload{'W', 'b', 'I', 'L'};
    append(payload, std::uint16_t(0));
    append(payload, std::uint16_t(0));
    append(payload, std::uint16_t(1));
    append(payload, std::uint16_t(0));
    append(payload, SECTION_ADDRESS);
    append(payload, std::uint32_t(CLI_HEADER_SIZE));
    append(payload, std::uint32_t(0));
    append(payload, std::uint32_t(0));
    append(payload, std::uint32_t(section.size()));
    append(payload, SECTION_ADDRESS);
    append(payload, std::uint32_t(section.size()));
    append(payload, std::uint32_t(WEBCIL_HEADERS_SIZE));
    append_bytes(payload, section);
    bytes size;
    append(size, std::uint32_t(payload.size()));
    return make_module({size, payload});
}

void write_file(const fs::path& file, const bytes& content)
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(content.data()), std::streamsize(content.size()));
}

auto write_library(const fs::path& file, const std::string& name, const std::vector<std::string>& references)
    -> fs::path
{
    write_file(file, make_library(make_section({name, references})));
    return file;
}

auto is_assembly(const deployment::assembly_info& info,
                 const std::string& name,
                 const std::vector<std::string>& references) -> bool
{
    return info.is_managed && info.name == name && info.references == references;
}

void test_library(const fs::path& root)
{
    std::printf("test_library\n");
    const std::vector<std::string> references{"System.Runtime", "System.Collections", "Mono.Cecil"};
    write_file(root / "game.dll", make_library(make_section({"Game.Scripts", references})));
    check(is_assembly(deployment::read_assembly_info(root / "game.dll"), "Game.Scripts", references),
          "a library gives its name and its references in their order");
    write_file(root / "wide.dll", make_library(make_section({"Wide", references, true})));
    check(is_assembly(deployment::read_assembly_info(root / "wide.dll"), "Wide", references),
          "a library with four-byte string indexes reads the same");
    write_file(root / "alone.dll", make_library(make_section({"Alone", {}})));
    check(is_assembly(deployment::read_assembly_info(root / "alone.dll"), "Alone", {}),
          "a library without references has none");
}

void test_browser_module(const fs::path& root)
{
    std::printf("test_browser_module\n");
    const std::vector<std::string> references{"System.Runtime", "System.Linq"};
    write_file(root / "Game.Scripts.abc123.wasm", make_browser_module(make_section({"Game.Scripts", references})));
    check(is_assembly(deployment::read_assembly_info(root / "Game.Scripts.abc123.wasm"), "Game.Scripts", references),
          "the module a browser build wraps an assembly in gives the assembly's name and references");
    write_file(root / "runtime.wasm", make_module({{1, 2, 3, 4}, {5, 6, 7, 8}}, false));
    check(!deployment::read_assembly_info(root / "runtime.wasm").is_managed,
          "a module with data placed in memory wraps no assembly");
    write_file(root / "other.wasm", make_module({{1, 2, 3, 4}, {'N', 'o', 'p', 'e', 0, 0, 0, 0}}));
    check(!deployment::read_assembly_info(root / "other.wasm").is_managed,
          "a module whose data is no assembly wraps none");
    write_file(root / "bare.wasm", {0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00});
    check(!deployment::read_assembly_info(root / "bare.wasm").is_managed, "a module without data wraps none");
}

void test_other_files(const fs::path& root)
{
    std::printf("test_other_files\n");
    check(!deployment::read_assembly_info(root / "missing.dll").is_managed, "a missing file is no assembly");
    write_file(root / "empty.dll", {});
    check(!deployment::read_assembly_info(root / "empty.dll").is_managed, "an empty file is no assembly");
    write_file(root / "text.dll", bytes(300, 'a'));
    check(!deployment::read_assembly_info(root / "text.dll").is_managed, "text is no assembly");
    const auto section = make_section({"Whole", {"System.Runtime"}});
    write_file(root / "native.dll", make_library(section, 0));
    check(!deployment::read_assembly_info(root / "native.dll").is_managed,
          "a library without a CLI header is a native one");
    write_file(root / "module.dll", make_library(make_section({"Part", {"System.Runtime"}, false, true})));
    check(!deployment::read_assembly_info(root / "module.dll").is_managed,
          "metadata without an assembly of its own is no assembly");
    // Cut anywhere, a library and a browser module read as no assembly, never as part of one.
    for(const auto& whole : {make_library(section), make_browser_module(section)})
    {
        bool is_any_managed = false;
        for(std::size_t size = 0; size < whole.size(); ++size)
        {
            write_file(root / "cut.dll", bytes(whole.begin(), whole.begin() + std::ptrdiff_t(size)));
            is_any_managed |= deployment::read_assembly_info(root / "cut.dll").is_managed;
        }
        check(!is_any_managed, "a file cut short at any byte is no assembly");
    }
}

void test_listing(const fs::path& root)
{
    std::printf("test_listing\n");
    write_library(root / "System.Runtime.dll", "System.Runtime", {});
    write_file(root / "anything.x1y2.wasm", make_browser_module(make_section({"System.Linq", {"System.Runtime"}})));
    write_file(root / "coreclr.dll", make_library(make_section({"Native", {}}), 0));
    write_file(root / "System.Runtime.pdb", make_library(make_section({"Symbols", {}})));
    write_library(root / "ref/System.Inner.dll", "System.Inner", {});
    const auto listed = deployment::list_assemblies(root);
    check(listed.size() == 2 && listed.contains("System.Runtime") && listed.contains("System.Linq"),
          "a folder's own assemblies are listed, without native libraries, other files and subfolders");
    check(listed.at("System.Linq").file == root / "anything.x1y2.wasm" &&
              listed.at("System.Linq").references == std::vector<std::string>{"System.Runtime"},
          "an assembly is listed by its own name, whatever its file is called");
    check(deployment::list_assemblies(root / "none").empty(), "a missing folder has no assemblies");
    check(deployment::has_assembly_extension("a/b.dll") && deployment::has_assembly_extension("b.x.wasm") &&
              !deployment::has_assembly_extension("b.pdb") && !deployment::has_assembly_extension("dll"),
          "libraries and browser modules are the files an assembly ships in");
}

auto get_names(const deployment::assembly_files& files) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(const auto& [name, file] : files)
    {
        names.push_back(name);
    }
    return names;
}

void test_unused(const fs::path& root)
{
    std::printf("test_unused\n");
    const auto framework_folder = root / "framework";
    write_library(framework_folder / "System.Private.CoreLib.dll", "System.Private.CoreLib", {});
    write_library(framework_folder / "System.Runtime.dll", "System.Runtime", {"System.Private.CoreLib"});
    write_library(framework_folder / "System.Collections.dll", "System.Collections", {"System.Runtime", "System.Loop"});
    write_library(framework_folder / "System.Loop.dll", "System.Loop", {"System.Collections"});
    write_library(framework_folder / "System.Data.dll", "System.Data", {"System.Xml", "System.Runtime"});
    write_library(framework_folder / "System.Xml.dll", "System.Xml", {"System.Runtime"});
    write_library(framework_folder / "System.Unused.dll", "System.Unused", {"System.Runtime"});
    const auto framework = deployment::list_assemblies(framework_folder);
    const auto scripts =
        write_library(root / "scripts/game.dll", "Game", {"System.Collections", "Engine", "Missing.Library"});
    const auto engine = write_library(root / "scripts/engine.dll", "Engine", {"System.Runtime"});
    using names = std::vector<std::string>;
    check(get_names(deployment::find_unused_assemblies({scripts, engine}, framework, {})) ==
              names{"System.Data", "System.Unused", "System.Xml"},
          "what the roots reference stays with all it references, through a cycle; the rest is unused");
    check(get_names(deployment::find_unused_assemblies({scripts}, framework, {"System.Data", "Not.There"})) ==
              names{"System.Unused"},
          "a kept name stays with what it references, and a name the framework lacks changes nothing");
    check(get_names(deployment::find_unused_assemblies({}, framework, {})).size() == framework.size(),
          "without roots nothing is reached");
    const auto text = root / "scripts/notes.dll";
    write_file(text, bytes(64, 'x'));
    check(get_names(deployment::find_unused_assemblies({root / "scripts/none.dll", text, engine}, framework, {})) ==
              names{"System.Collections", "System.Data", "System.Loop", "System.Unused", "System.Xml"},
          "roots that are missing or no assembly are passed over");
}

void test_trim(const fs::path& root)
{
    std::printf("test_trim\n");
    const auto framework = root / "framework";
    write_library(framework / "System.Private.CoreLib.dll", "System.Private.CoreLib", {});
    write_library(framework / "System.Runtime.dll", "System.Runtime", {"System.Private.CoreLib"});
    write_library(framework / "renamed.dll", "System.Unused", {"System.Runtime"});
    write_library(framework / "System.Kept.dll", "System.Kept", {});
    write_file(framework / "coreclr.dll", make_library(make_section({"Native", {}}), 0));
    deployment::framework_trim trim;
    trim.roots = {write_library(root / "game.dll", "Game", {"System.Runtime"})};
    check(deployment::find_trimmed_files(framework, trim).empty(), "a trim that is off leaves nothing out");
    trim.is_on = true;
    check(deployment::find_trimmed_files(framework, trim) == std::set<std::string>{"System.Kept.dll", "renamed.dll"},
          "a trim leaves out the files of the assemblies nothing reaches, and no native library");
    trim.kept = {"System.Kept"};
    check(deployment::find_trimmed_files(framework, trim) == std::set<std::string>{"renamed.dll"},
          "a kept name keeps its file");
    trim.roots.clear();
    trim.kept.clear();
    const auto without_roots = deployment::find_trimmed_files(framework, trim);
    check(!without_roots.contains("System.Private.CoreLib.dll") && without_roots.size() == 3,
          "the runtime's own library stays whatever the build references");
}

void test_names()
{
    std::printf("test_names\n");
    using names = std::vector<std::string>;
    check(deployment::split_assembly_names("").empty() && deployment::split_assembly_names(" ,;\n").empty(),
          "an empty list and one of separators alone have no names");
    check(deployment::split_assembly_names("System.Data.Common") == names{"System.Data.Common"},
          "one name is one name");
    check(deployment::split_assembly_names(" System.Xml,System.Data.Common ;System.Text.Json\r\nSystem.Linq\t") ==
              names{"System.Xml", "System.Data.Common", "System.Text.Json", "System.Linq"},
          "names split on spaces, commas, semicolons and line ends");
    check(deployment::split_assembly_names("System.Xml.dll, .dll System.Dll") == names{"System.Xml", "System.Dll"},
          "a library's file name counts as its name");
}

/// The script bridge the runner was built with, and the browser player's runtime: real libraries, which have every
/// kind of table before their references.
void test_real_libraries()
{
    std::printf("test_real_libraries\n");
    const auto bridge = fs::resolve_protocol("binary:/clrpp/Clrpp.Managed.dll");
    fs::error_code ec;
    if(!fs::exists(bridge, ec))
    {
        std::printf("  (no script bridge beside the runner; skipped)\n");
        return;
    }
    const auto info = deployment::read_assembly_info(bridge);
    const auto has_reference = [&info](const std::string& name)
    {
        return std::find(info.references.begin(), info.references.end(), name) != info.references.end();
    };
    check(info.is_managed && info.name == "Clrpp.Managed", "the script bridge names itself");
    check(has_reference("System.Runtime") && has_reference("Mono.Cecil"),
          "the script bridge references the framework and its weaver");
    const auto bridge_folder = deployment::list_assemblies(bridge.parent_path());
    check(bridge_folder.contains("Clrpp.Managed") && bridge_folder.contains("Mono.Cecil"),
          "the bridge folder's libraries are listed by name");
    const auto runtime = fs::resolve_protocol("binary:/players/web/wwwroot/_framework");
    if(!fs::exists(runtime, ec))
    {
        std::printf("  (no browser player beside the runner; its runtime is skipped)\n");
        return;
    }
    const auto modules = deployment::list_assemblies(runtime);
    check(modules.contains("System.Private.CoreLib") && modules.contains("Clrpp.Managed") &&
              modules.contains("UnravelWebPlayer"),
          "the browser player's runtime lists its libraries by name, the largest among them");
    const auto player_references =
        modules.contains("UnravelWebPlayer") ? modules.at("UnravelWebPlayer").references : std::vector<std::string>{};
    check(std::find(player_references.begin(), player_references.end(), "Clrpp.Managed") != player_references.end(),
          "the browser player references the script bridge");
    deployment::web_template web_player;
    web_player.root = fs::resolve_protocol("binary:/players/web");
    deployment::framework_trim trim;
    trim.is_on = true;
    const auto trimmed = deployment::find_web_trimmed_files(web_player, trim);
    const bool has_player_file = std::any_of(trimmed.begin(),
                                             trimmed.end(),
                                             [](const std::string& file)
                                             {
                                                 return file.starts_with("UnravelWebPlayer.") ||
                                                        file.starts_with("Clrpp.Managed.") ||
                                                        file.starts_with("System.Private.CoreLib.") ||
                                                        file.starts_with("System.Runtime.InteropServices.JavaScript.");
                                             });
    check(!trimmed.empty() && trimmed.size() < modules.size() && !has_player_file,
          "a browser build without scripts leaves out part of the runtime, never the player's own libraries");
}

auto run_assembly_references_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto temp = get_temp_folder();
    fs::error_code ec;
    fs::remove_all(temp, ec);
    test_library(temp / "library");
    test_browser_module(temp / "browser");
    test_other_files(temp / "other");
    test_listing(temp / "listing");
    test_unused(temp / "unused");
    test_trim(temp / "trim");
    test_names();
    test_real_libraries();
    fs::remove_all(temp, ec);
    std::printf("assembly references: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("assembly references", run_assembly_references_suite)
