#pragma once

#include <engine/assets/asset_storage.h>
#include <filesystem/filesystem.h>
#include <filesystem/mirror.h>
#include <filesystem/pack.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

/// Which files a deploy ships, and the copy and pack steps that move them. Every step throws on failure,
/// so the deploy reports a failed step instead of a partial build that looks finished.
namespace unravel::deployment
{

/// The asset index's name in builds of older engine versions: a folder holding it is an earlier deploy,
/// and a deploy removes it (the index is ex::get_asset_index_file_name()).
constexpr const char* LEGACY_ASSET_INDEX_NAME = "assets.pack";
/// The folder of a deployed build that holds one folder per protocol.
constexpr const char* DATA_FOLDER = "data";

/// Whether `key` is a script source: scripts ship as their library, not as assets.
auto is_script_key(const std::string& key) -> bool;

/**
 * @brief The compiled files a deployed game reads for the asset at `key`, in the protocol whose folder
 * is `protocol_root`: one per renderer of the platform for a shader; none for scripts and for keys
 * outside the protocol's data folder (the embedded primitives).
 */
auto get_compiled_paths(const std::string& key, const fs::path& protocol_root) -> std::vector<fs::path>;

/// The asset key ("app:/data/a/b.png") of a file in a protocol's compiled folder, from its path relative
/// to that folder ("a/b.png.asset").
auto get_source_key(const std::string& protocol, const fs::path& compiled_relative) -> std::string;

/// A file a script library build leaves beside its dll for debugging: symbols (.pdb, .mdb) or docs (an
/// .xml named like a dll next to it).
auto is_debug_companion_file(const fs::path& file) -> bool;
/// Files a pack does not take: script libraries and their companions, which the runtime loads by path.
auto is_loose_only_file(const fs::path& file) -> bool;
/// Whether a deploy ships a file of the compiled cache: not compiled script sources (their library ships
/// instead), not the compiler's manifests and temporary files, debug companions only when asked for.
auto is_shipped_compiled_file(const fs::path& file, bool with_debug_symbols) -> bool;

/// Decides whether a deploy ships a compiled file, given the file and its path relative to the protocol's
/// compiled folder (which names the asset it was compiled from).
using compiled_file_filter = std::function<bool(const fs::path& file, const fs::path& relative)>;

/**
 * @brief The files a protocol's pack holds, read straight from the compiled cache: the compiled files
 * `is_shipped` keeps, with the cooked variants in `cooked` in their place, and the asset index. In name
 * order, so the same build packs to the same bytes.
 */
auto collect_pack_inputs(const fs::path& compiled,
                         const fs::path& cooked,
                         const fs::path& index,
                         const compiled_file_filter& is_shipped) -> std::vector<fs::pack_input>;

/// fs::mirror_directory that throws on failure.
void mirror_or_throw(const fs::path& from, const fs::path& to, const fs::mirror_filter& include = {});
/// fs::copy_file_if_changed that throws on failure.
void copy_file_or_throw(const fs::path& from, const fs::path& to);
/// Overlays the cooked variants the filter keeps onto loose compiled files in `destination`.
void copy_cooked_or_throw(const fs::path& cooked, const fs::path& destination, const compiled_file_filter& is_shipped);

/// Writes the entries `include` keeps (by location) as an asset index.
void write_asset_index(const asset_database::database_t& database,
                       const fs::path& file,
                       const std::function<bool(const std::string& location)>& include);

/// Whether a file at the top of a deployed build is an executable or a library of this platform.
auto is_binary_file(const fs::path& file) -> bool;
/// The name a shipped binary is matched by; names compare as the file system does.
auto get_binary_key(const fs::path& file) -> std::string;
/// Removes the executables and libraries at the top of `deploy_root` this build no longer ships. `shipped`
/// holds get_binary_key names.
void remove_stale_binaries(const fs::path& deploy_root, const std::set<std::string>& shipped);
/// Removes everything next to `kept` in its folder: an older runtime version beside the current one.
void remove_siblings(const fs::path& kept);

} // namespace unravel::deployment
