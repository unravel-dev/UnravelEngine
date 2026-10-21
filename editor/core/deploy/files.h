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

/// Where the compiled file of the asset at `key` lies in its protocol's compiled folder ("app:/data/a/b.png"
/// -> "a/b.png.asset"); empty for keys outside the protocol's data folder.
auto get_compiled_relative_path(const std::string& key) -> fs::path;

/**
 * @brief The compiled files a deployed game reads for the asset at `key`, in the protocol whose folder
 * is `protocol_root`: one per renderer for a shader; none for scripts and for keys outside the protocol's
 * data folder (the embedded primitives).
 *
 * @param shader_variants The renderer extensions of the target (".spirv"); empty: every renderer this
 * platform compiles shaders for.
 */
auto get_compiled_paths(const std::string& key, const fs::path& protocol_root, const std::set<std::string>& shader_variants = {})
    -> std::vector<fs::path>;

/// Whether `file` is a compiled shader for a renderer outside `shader_variants`; never when that is empty.
auto is_other_shader_variant(const fs::path& file, const std::set<std::string>& shader_variants) -> bool;

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

/// Folders of cooked variants laid over a protocol's compiled cache, each holding files at the paths their
/// compiled files have. A variant in an earlier folder wins over one in a later folder.
using cooked_folders = std::vector<fs::path>;

/// The file that ships for the compiled file at `relative`: the variant in the first cooked folder that has
/// one, otherwise `compiled_file`.
auto get_shipped_variant(const cooked_folders& cooked, const fs::path& relative, const fs::path& compiled_file) -> fs::path;

/**
 * @brief The files a protocol's pack holds, read straight from the compiled cache: the compiled files
 * `is_shipped` keeps, with their cooked variants in their place, and the asset index. In name order, so
 * the same build packs to the same bytes.
 */
auto collect_pack_inputs(const fs::path& compiled,
                         const cooked_folders& cooked,
                         const fs::path& index,
                         const compiled_file_filter& is_shipped) -> std::vector<fs::pack_input>;

/// fs::mirror_directory that throws on failure.
void mirror_or_throw(const fs::path& from, const fs::path& to, const fs::mirror_filter& include = {});
/// fs::copy_file_if_changed that throws on failure.
void copy_file_or_throw(const fs::path& from, const fs::path& to);
/// Overlays the cooked variants the filter keeps onto loose compiled files in `destination`; an earlier
/// folder's variant wins.
void copy_cooked_or_throw(const cooked_folders& cooked, const fs::path& destination, const compiled_file_filter& is_shipped);

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
