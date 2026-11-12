#pragma once

#include <filesystem/filesystem.h>

#include <map>
#include <set>
#include <string>
#include <vector>

/**
 * @brief Which .NET assemblies a build needs: what a managed library says of itself and of the assemblies it
 * references, read from its metadata, and the assemblies of a framework a set of libraries never reaches through
 * such references.
 *
 * A deploy uses it to leave out the framework assemblies nothing in the build references. References are what the
 * compiler wrote into a library; an assembly that code loads by a name it builds at run time is not among them,
 * which is why a deploy also takes a list of names to keep.
 */
namespace unravel::deployment
{

/// What a library file says of itself.
struct assembly_info
{
    /// Whether the file is a .NET assembly; a native library, or any other file, is not.
    bool is_managed{};
    /// Its simple name ("System.Collections").
    std::string name;
    /// The simple names of the assemblies it references, in the order its metadata lists them.
    std::vector<std::string> references;
};

/**
 * @brief Reads a library's metadata from either form a build ships it in: a .dll, or the module a browser build wraps
 * it in (.wasm). A file that is missing, cut short or no .NET assembly gives an info that is not managed; nothing is
 * thrown.
 */
auto read_assembly_info(const fs::path& file) -> assembly_info;

/// Whether a file's name is one an assembly ships under (.dll, or .wasm in a browser build); what the file holds is
/// read_assembly_info's to say.
auto has_assembly_extension(const fs::path& file) -> bool;

/// The names in a list a person typed, separated by spaces, commas, semicolons or line ends. A library's file name
/// ("System.Data.Common.dll") counts as its name.
auto split_assembly_names(const std::string& text) -> std::vector<std::string>;

/// An assembly of a framework: its file, and the simple names of the assemblies it references.
struct listed_assembly
{
    fs::path file;
    std::vector<std::string> references;
};

/// A framework's assemblies, by the simple name each one's metadata gives.
using assembly_files = std::map<std::string, listed_assembly>;

/// The .NET assemblies among the files directly in `folder`; the files that are none (native libraries, the
/// runtime's own modules) are left out. Empty for a folder that cannot be listed.
auto list_assemblies(const fs::path& folder) -> assembly_files;

/**
 * @brief The assemblies of `framework` a build can leave out: the ones `roots` do not reach. A root reaches the
 * assemblies it references, the ones those reference, and so on.
 * @param roots Library files the build loads itself; files that are missing or no assembly are passed over.
 * @param kept Names that stay whatever references them, and whose own references are followed: the runtime's own
 * library, and what code loads by name.
 */
auto find_unused_assemblies(const std::vector<fs::path>& roots,
                            const assembly_files& framework,
                            const std::vector<std::string>& kept) -> assembly_files;

/// The library the runtime loads on its own before any other, which no trim leaves out.
constexpr const char* RUNTIME_CORE_ASSEMBLY = "System.Private.CoreLib";

/// How a build trims the framework it ships.
struct framework_trim
{
    /// Whether the build trims at all; off, the whole framework ships.
    bool is_on{};
    /// The library files the build loads itself: its script libraries, the script bridge's libraries, the
    /// libraries among its extra files. A project without scripts has no script library: a missing file is passed
    /// over.
    std::vector<fs::path> roots;
    /// Assemblies that stay whatever references them, by simple name: the project's list, and what a platform's
    /// player starts with.
    std::vector<std::string> kept;
};

/**
 * @brief The names of the files in the folder `framework` a build with `trim` leaves out: its assemblies that
 * neither the roots nor the kept names reach, RUNTIME_CORE_ASSEMBLY never among them. None for a trim that is off.
 */
auto find_trimmed_files(const fs::path& framework, const framework_trim& trim) -> std::set<std::string>;

} // namespace unravel::deployment
