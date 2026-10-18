#pragma once

#include <engine/assets/asset_storage.h>
#include <filesystem/filesystem.h>

#include <string>
#include <unordered_set>

namespace unravel::deployment
{

/// One protocol's compiled assets and index, as a deploy step ships them. Every path is resolved and the
/// index taken before the step runs, so it reads no protocol and no asset manager.
struct protocol_job
{
    /// "engine" or "app".
    std::string protocol;
    /// The protocol's compiled cache.
    fs::path compiled_root;
    /// The cooked variants that replace compiled files (absent when nothing was cooked).
    fs::path cooked_root;
    /// The build's data/<protocol> folder.
    fs::path protocol_root;
    /// The protocol's asset index.
    asset_database::database_t database;
    /// The path keys (fs::make_path_key) of the assets the content selection leaves out.
    std::unordered_set<std::string> left_out;
    bool is_packed{true};
    bool with_debug_symbols{};
    /// The kept release's pack to patch; empty for a full pack.
    fs::path patch_release_pack;
};

/**
 * @brief Ships one protocol into protocol_root: a pack (or a patch over a kept release) beside the loose
 * script libraries, or loose files. An earlier build there is updated in place: unchanged files and pack
 * entries are kept, what this build no longer has is removed. Throws on failure.
 */
void deploy_protocol_data(const protocol_job& job);

} // namespace unravel::deployment
