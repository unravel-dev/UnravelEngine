#pragma once

#include "files.h"

#include <engine/assets/asset_storage.h>
#include <filesystem/filesystem.h>

#include <set>
#include <string>
#include <unordered_set>

namespace unravel::deployment
{

/// One set of a web build's textures, shipped in a pack of its own (ex::get_texture_pack_file_name).
struct texture_set_job
{
    /// The set's name (one of ex::TEXTURE_SET_NAMES).
    std::string name;
    /// The folders of the set's texture variants, which replace compiled files.
    cooked_folders cooked;
};

/// One protocol's compiled assets and index, as a deploy step ships them. Every path is resolved and the
/// index taken before the step runs, so it reads no protocol and no asset manager.
struct protocol_job
{
    /// "engine" or "app".
    std::string protocol;
    /// The protocol's compiled cache.
    fs::path compiled_root;
    /// Folders of cooked variants that replace compiled files, the first winning: the prefabs the deploy
    /// cooked, the profile's texture variants. A folder that does not exist holds none.
    cooked_folders cooked;
    /// The build's data/<protocol> folder.
    fs::path protocol_root;
    /// The protocol's asset index.
    asset_database::database_t database;
    /// The path keys (fs::make_path_key) of the assets the content selection leaves out.
    std::unordered_set<std::string> left_out;
    bool is_packed{true};
    bool with_debug_symbols{};
    /// The renderer extensions of the compiled shaders to ship (".spirv" for Android); empty ships every
    /// variant.
    std::set<std::string> shader_variants;
    /// The kept release's pack to patch; empty for a full pack.
    fs::path patch_release_pack;
    /// A packed web build's texture sets when it ships several: the protocol's pack then holds no texture, each set
    /// goes into a pack of its own and the page downloads one. Empty: the textures ship in the protocol's pack.
    std::vector<texture_set_job> texture_sets;
};

/**
 * @brief Ships one protocol into protocol_root: a pack (or a patch over a kept release) beside the loose
 * script libraries, and a pack per texture set when the job has several, or loose files. An earlier build there is updated in place: unchanged files and pack
 * entries are kept, what this build no longer has is removed. Throws on failure.
 */
void deploy_protocol_data(const protocol_job& job);

} // namespace unravel::deployment
